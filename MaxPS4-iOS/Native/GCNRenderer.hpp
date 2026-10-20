#pragma once
// First software graphics pipeline for guest GCN draws (base PS4 register
// layout). Every stage is executed from guest state: index fetch, the bound
// vertex shader (GCNShader), primitive assembly, homogeneous clipping,
// viewport transform, fixed-point triangle rasterisation, the bound pixel
// shader in 2x2 quads, depth test/clear, blending and the colour write into
// guest memory in the render target's own layout. Only the profile the title
// actually uses is accepted; any other register state is refused with a
// precise reason instead of being approximated.
//
// The backend is portable C++17 (no host code generation, no W^X changes).
// Pixel shading can be offloaded to a PixelBackend (GCNPixelOffload.hpp)
// running the translated shader; everything else stays here, so the result
// is the same whichever path shades the fragments.
#include "GCNShader.hpp"
#include "GCNPixelOffload.hpp"
#include <array>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace maxps4::gpu {
struct GCNRenderer {
  using Bank = std::array<uint32_t, 1024>;
  struct DrawCall {
    uint8_t opcode = 0; uint32_t count = 0, initiator = 0; uint64_t indexAddress = 0;
    uint32_t indexType = 0, instances = 1;
  };
  struct Stats {
    uint64_t draws = 0, vertices = 0, vsWaves = 0, vsInstructions = 0, primitives = 0, culled = 0,
             clippedAway = 0, rasterized = 0, quads = 0, psWaves = 0, psInstructions = 0, fragments = 0,
             pixelsWritten = 0, pixelsChanged = 0, sourceAlphaNonZero = 0, depthWrites = 0, depthRejected = 0, samples = 0,
             offloadedDraws = 0, offloadBatches = 0, offloadFragments = 0, offloadRefusals = 0;
  };
  // Optional pixel shading backend (not owned). Draws it cannot take stay on
  // the interpreter; lastOffloadRefusal says why for the last such draw.
  PixelBackend* pixelBackend = nullptr;
  std::string lastOffloadRefusal;
  Stats total, last;
  std::string lastVertexHistogram, lastPixelHistogram;
  // Host-side depth planes keyed by guest DB_Z_WRITE_BASE. Depth/HTILE guest
  // memory is not written by this backend (documented limitation).
  struct DepthPlane { uint32_t width = 0, height = 0; std::vector<float> z; };
  std::map<uint64_t, DepthPlane> depth;
  bool verbose = true;
  unsigned maxThreads = 0; // pixel-shading workers; 0 = hardware concurrency

  struct Vertex { std::array<float, 4> pos{}; std::vector<std::array<float, 4>> params; };
  struct ClipVertex { std::array<float, 4> pos{}; std::array<float, 3> bary{}; };
  struct Setup { // one rasterised triangle (after clipping), attribute references
    std::array<const Vertex*, 3> source{};   // original (pre-clip) triangle vertices
    std::array<std::array<float, 3>, 3> bary{}; // each clipped vertex as barycentrics of 'source'
    std::array<double, 3> invW{}, z{}; bool front = true;
  };
  struct Fragment { uint32_t x = 0, y = 0; uint32_t setup = 0; float i = 0, j = 0, li = 0, lj = 0, z = 0, w = 1; bool covered = false; };

  template <class Memory>
  static bool readGuest(Memory& memory, uint64_t address, void* out, size_t size) {
    return !memory.read_data(address, out, size);
  }

  // ------------------------------------------------------------------------
  template <class Memory>
  bool draw(Memory& memory, const Bank& ctx, const Bank& sh, const Bank& uc, const DrawCall& call, std::string& error) {
    last = {};
    const auto started = std::chrono::steady_clock::now();
    auto fail = [&](const std::string& why) { error = why; return false; };
    const unsigned primitive = uc[0x242];
    if (call.instances != 1) return fail("instanced draw unsupported (NUM_INSTANCES=" + std::to_string(call.instances) + ")");
    if (primitive != 4 && primitive != 0x11 && primitive != 5)
      return fail("VGT_PRIMITIVE_TYPE " + std::to_string(primitive) + " unsupported (triangle list/strip, rect list)");
    if (ctx[0x2d5] != 0) return fail("VGT_SHADER_STAGES_EN requires GS/HS/LS stages (unsupported)");
    if (ctx[0x2a5] & 1) return fail("primitive restart unsupported");
    // ---------------- vertex indices --------------------------------------
    std::vector<uint32_t> indices(call.count);
    if (call.opcode == 0x2d) {
      if (((call.initiator >> 0) & 3) != 2) return fail("DRAW_INDEX_AUTO initiator source select");
      for (uint32_t k = 0; k < call.count; ++k) indices[k] = k;
    } else {
      if (call.initiator & 3) return fail("DRAW_INDEX_2 initiator source select");
      const unsigned size = (call.indexType & 3) == 1 ? 4 : 2;
      if ((call.indexType & ~3U) || (call.indexType & 3) > 1) return fail("index type/swap mode unsupported");
      if (call.indexAddress & (size - 1)) return fail("misaligned index buffer");
      std::vector<uint8_t> raw(size_t(call.count) * size);
      for (size_t at = 0; at < raw.size();) {
        const size_t n = std::min<size_t>(raw.size() - at, 1 << 20);
        if (!memory.permitted(call.indexAddress + at, n, 4) || memory.read_data(call.indexAddress + at, raw.data() + at, n))
          return fail("index buffer unreadable");
        at += n;
      }
      for (uint32_t k = 0; k < call.count; ++k) {
        uint32_t v = 0; std::memcpy(&v, raw.data() + size_t(k) * size, size); indices[k] = v;
      }
    }
    const uint32_t indexOffset = ctx[0x102], minIndex = ctx[0x101], maxIndex = ctx[0x100];
    for (auto& v : indices) { v += indexOffset; v = std::min(std::max(v, minIndex), maxIndex); }

    // ---------------- vertex shader ----------------------------------------
    const uint64_t vsProgram = (uint64_t(sh[0x48]) << 8) | (uint64_t(sh[0x49] & 0xff) << 40);
    const uint32_t vsRsrc1 = sh[0x4a], vsRsrc2 = sh[0x4b];
    if (!vsProgram) return fail("no vertex shader bound");
    if ((vsRsrc1 >> 24) & 3) return fail("VS VGPR_COMP_CNT>0 (instance inputs) unsupported");
    if (vsRsrc2 & ~0x3eU) return fail("VS RSRC2 scratch/streamout/LDS inputs unsupported");
    const unsigned vsUser = (vsRsrc2 >> 1) & 31, vsVgprs = ((vsRsrc1 & 63) + 1) * 4, vsSgprs = (((vsRsrc1 >> 6) & 15) + 1) * 8;
    const unsigned paramCount = ((ctx[0x1b1] >> 1) & 31) + 1;
    if ((ctx[0x1c3] & ~0xfU) || (ctx[0x1c3] & 0xf) != 4) return fail("SPI_SHADER_POS_FORMAT other than POS0 4COMP unsupported");
    if (ctx[0x207] & 0xffU) return fail("PA_CL_VS_OUT_CNTL clip/cull distances unsupported");
    std::vector<uint32_t> unique;
    std::map<uint32_t, uint32_t> slot;
    for (auto v : indices) if (slot.emplace(v, uint32_t(unique.size())).second) unique.push_back(v);
    std::vector<Vertex> vertices(unique.size());
    GCNShader vs;
    for (size_t first = 0; first < unique.size(); first += 64) {
      const unsigned lanes = unsigned(std::min<size_t>(64, unique.size() - first));
      GCNShader::Wave wave;
      wave.configure(std::min(vsSgprs, 104u), vsVgprs);
      wave.fp32Denorm = (vsRsrc1 >> 16) & 3;   // FLOAT_MODE fp32 denormals
      for (unsigned k = 0; k < vsUser; ++k) wave.s[k] = sh[0x4c + k];
      wave.setExec(lanes == 64 ? ~uint64_t(0) : (uint64_t(1) << lanes) - 1);
      for (unsigned lane = 0; lane < lanes; ++lane) wave.v[0][lane] = unique[first + lane];
      std::string why;
      if (!vs.run(memory, vsProgram, wave, why)) return fail("VS 0x" + hex(vsProgram) + ": " + why);
      ++last.vsWaves;
      std::vector<bool> position(lanes, false);
      for (unsigned lane = 0; lane < lanes; ++lane) vertices[first + lane].params.assign(paramCount, {0, 0, 0, 0});
      std::vector<std::vector<bool>> paramSeen(lanes, std::vector<bool>(paramCount, false));
      for (const auto& e : wave.exports) {
        if (e.compressed) return fail("compressed VS export unsupported");
        const bool isPos = e.target == 12, isParam = e.target >= 32 && e.target < 64;
        if (!isPos && !isParam) return fail("VS export target " + std::to_string(e.target) + " unsupported");
        const unsigned p = e.target - 32;
        if (isParam && p >= paramCount) return fail("VS exports more parameters than SPI_VS_OUT_CONFIG");
        for (unsigned lane = 0; lane < lanes; ++lane) {
          if (!((e.exec >> lane) & 1)) continue;
          auto& vert = vertices[first + lane];
          for (unsigned c = 0; c < 4; ++c) {
            if (!((e.enable >> c) & 1)) continue;
            const float value = gcnFloat(e.data[c][lane]);
            if (isPos) vert.pos[c] = value; else vert.params[p][c] = value;
          }
          if (isPos) position[lane] = true; else paramSeen[lane][p] = true;
        }
      }
      for (unsigned lane = 0; lane < lanes; ++lane) {
        if (!position[lane]) return fail("VS lane did not export POS0");
        for (unsigned p = 0; p < paramCount; ++p) if (!paramSeen[lane][p]) return fail("VS lane did not export PARAM" + std::to_string(p));
      }
    }
    last.vertices = unique.size();
    last.vsInstructions = vs.stats.instructions;
    lastVertexHistogram = vs.histogramText();

    // ---------------- fixed-function state ---------------------------------
    const uint32_t vte = ctx[0x206], clipCntl = ctx[0x204], modeCntl = ctx[0x205], vtxCntl = ctx[0x2f9];
    if (vte != 0x43f) return fail("PA_CL_VTE_CNTL " + hex(vte) + " unsupported (expected perspective-divided viewport 0x43f)");
    if (clipCntl & ~(0x00080000U | 0x00010000U | 0x01000000U)) return fail("PA_CL_CLIP_CNTL user planes/kill modes unsupported");
    if (modeCntl & ((3U << 3) | (7U << 11))) return fail("polygon mode/offset unsupported");
    if (!(vtxCntl & 1)) return fail("PA_SU_VTX_CNTL pixel center 0.0 unsupported");
    const bool clipDisabled = clipCntl & 0x10000U, dxClip = clipCntl & 0x80000U;
    const bool cullFront = modeCntl & 1, cullBack = modeCntl & 2, frontCw = modeCntl & 4, provokeLast = modeCntl & (1U << 19);
    const float xScale = gcnFloat(ctx[0x10f]), xOffset = gcnFloat(ctx[0x110]), yScale = gcnFloat(ctx[0x111]),
                yOffset = gcnFloat(ctx[0x112]), zScale = gcnFloat(ctx[0x113]), zOffset = gcnFloat(ctx[0x114]);
    const float zMin = gcnFloat(ctx[0xb4]), zMax = gcnFloat(ctx[0xb5]);
    // Scissor: screen, window (with offset), generic, optional viewport.
    int sx0 = 0, sy0 = 0, sx1 = 16384, sy1 = 16384;
    auto clipRect = [&](uint32_t tl, uint32_t br, bool windowRelative) {
      int x0 = int(tl & 0x7fff), y0 = int((tl >> 16) & 0x7fff), x1 = int(br & 0x7fff), y1 = int((br >> 16) & 0x7fff);
      if (windowRelative && !(tl & 0x80000000U)) {
        const int ox = int(int16_t(ctx[0x80] & 0xffff)), oy = int(int16_t(ctx[0x80] >> 16));
        x0 += ox; x1 += ox; y0 += oy; y1 += oy;
      }
      sx0 = std::max(sx0, x0); sy0 = std::max(sy0, y0); sx1 = std::min(sx1, x1); sy1 = std::min(sy1, y1);
    };
    clipRect(ctx[0x0c], ctx[0x0d], false);
    clipRect(ctx[0x81], ctx[0x82], true);
    clipRect(ctx[0x90], ctx[0x91], true);
    if (ctx[0x292] & 2) clipRect(ctx[0x94], ctx[0x95], true);

    // Render target 0 (other targets must be disabled).
    const uint32_t targetMask = ctx[0x8e], shaderMask = ctx[0x8f], colorControl = ctx[0x202];
    const unsigned cbMode = (colorControl >> 4) & 7;
    if (cbMode > 1) return fail("CB_COLOR_CONTROL mode " + std::to_string(cbMode) + " (resolve/fast-clear elimination) unsupported");
    if (((colorControl >> 16) & 255) != 0xcc) return fail("CB ROP3 other than copy unsupported");
    for (unsigned t = 1; t < 8; ++t) if (((targetMask >> (t * 4)) & 15) && ((ctx[0x31c + t * 15] >> 2) & 31))
      return fail("multiple render targets unsupported");
    const bool colorEnabled = cbMode == 1 && (targetMask & 15) && (shaderMask & 15);
    const uint64_t cbBase = (uint64_t(ctx[0x318]) << 8) & GCNShader::addressMask;
    const uint32_t cbInfo = ctx[0x31c], cbAttrib = ctx[0x31d];
    const unsigned cbPitch = ((ctx[0x319] & 0x7ff) + 1) * 8;
    const uint64_t cbSlice = (uint64_t(ctx[0x31a] & 0x3fffff) + 1) * 64;
    const unsigned cbHeight = cbPitch ? unsigned(cbSlice / cbPitch) : 0;
    const unsigned cbFormat = (cbInfo >> 2) & 31, cbNumber = (cbInfo >> 8) & 7, cbSwap = (cbInfo >> 11) & 3, cbTile = cbAttrib & 31;
    Display2DThin32 cbLayout;
    uint8_t* cbHost = nullptr; // whole colour buffer in one read/write range
    if (colorEnabled) {
      if (cbFormat != 10) return fail("colour buffer format " + std::to_string(cbFormat) + " unsupported (8_8_8_8 only)");
      if (cbNumber != 0 && cbNumber != 6) return fail("colour buffer number type unsupported (UNORM/SRGB only)");
      if (cbSwap > 1) return fail("colour buffer component swap unsupported");
      if (cbInfo & ((1U << 13) | (1U << 14) | (1U << 28))) return fail("fast-clear/compressed colour buffer unsupported");
      if (cbInfo & 3) return fail("colour buffer endian swap unsupported");
      if (ctx[0x31b]) return fail("colour buffer slice view unsupported");
      if (cbTile == 10) { if (!cbLayout.init(cbPitch, cbHeight, error)) return false; }
      else if (cbTile != 8) return fail("colour buffer tile mode index " + std::to_string(cbTile) + " unsupported");
      const uint64_t span = uint64_t(cbPitch) * (cbTile == 10 ? cbLayout.paddedHeight : cbHeight) * 4;
      for (uint64_t at = 0; at < span; at += 1 << 20) {
        const uint64_t n = std::min<uint64_t>(span - at, 1 << 20);
        if (!memory.permitted(cbBase + at, size_t(n), 6)) return fail("colour buffer 0x" + hex(cbBase) + " not mapped read/write");
      }
      sx1 = std::min(sx1, int(cbPitch)); sy1 = std::min(sy1, int(cbHeight));
      cbHost = GCNShader::hostSpan(memory, cbBase, span, 6);
    }
    const uint32_t blend = ctx[0x1e0];
    const bool blending = colorEnabled && (blend & (1U << 30)) && !(cbInfo & (1U << 16)); // BLEND_BYPASS
    // Depth.
    const uint32_t depthControl = ctx[0x200], renderControl = ctx[0x0], zInfo = ctx[0x10];
    if (depthControl & 1) return fail("stencil test unsupported");
    if (renderControl & ~1U) return fail("DB_RENDER_CONTROL mode " + hex(renderControl) + " unsupported");
    const unsigned zFormat = zInfo & 3;
    const bool depthClear = renderControl & 1;
    const bool depthEnabled = zFormat != 0 && ((depthControl & 2) || depthClear);
    const bool depthTest = depthEnabled && (depthControl & 2) && !depthClear;
    const bool depthWrite = depthEnabled && (((depthControl & 2) && (depthControl & 4)) || depthClear);
    const unsigned depthFunc = (depthControl >> 4) & 7;
    DepthPlane* plane = nullptr;
    if (depthEnabled) {
      if (zFormat != 3 && zFormat != 1) return fail("depth format unsupported");
      const unsigned pitch = ((ctx[0x16] & 0x7ff) + 1) * 8, height = (((ctx[0x16] >> 11) & 0x7ff) + 1) * 8;
      auto& p = depth[(uint64_t(ctx[0x14]) << 8) & GCNShader::addressMask];
      if (p.width != pitch || p.height != height) { p.width = pitch; p.height = height; p.z.assign(size_t(pitch) * height, 1.0f); }
      plane = &p;
      sx1 = std::min(sx1, int(pitch)); sy1 = std::min(sy1, int(height));
    }
    const float clearDepth = gcnFloat(ctx[0x0b]);
    if (ctx[0x203] & 1) return fail("pixel shader depth export unsupported");
    // Pixel shader.
    const uint64_t psProgram = (uint64_t(sh[0x8]) << 8) | (uint64_t(sh[0x9] & 0xff) << 40);
    const uint32_t psRsrc1 = sh[0xa], psRsrc2 = sh[0xb], inputEnable = ctx[0x1b3], inputAddress = ctx[0x1b4];
    if (!psProgram) return fail("no pixel shader bound");
    if (psRsrc2 & ~0x3eU) return fail("PS RSRC2 scratch/LDS/wave-count inputs unsupported");
    const unsigned psUser = (psRsrc2 >> 1) & 31, psVgprs = ((psRsrc1 & 63) + 1) * 4, psSgprs = (((psRsrc1 >> 6) & 15) + 1) * 8;
    const unsigned colorFormat = ctx[0x1c5] & 15, numInterp = ctx[0x1b6] & 63;
    if (ctx[0x1c4] & 15) return fail("SPI_SHADER_Z_FORMAT export unsupported");
    if (colorEnabled && colorFormat != 4 && colorFormat != 9) return fail("SPI_SHADER_COL_FORMAT " + std::to_string(colorFormat) + " unsupported");
    if ((inputEnable | inputAddress) & ~(0x2U | 0x20U | 0x300U)) return fail("SPI_PS_INPUT_ENA/ADDR modes " + hex(inputEnable | inputAddress) + " unsupported");
    if (inputEnable & ~inputAddress) return fail("SPI_PS_INPUT_ENA not a subset of ADDR");
    std::vector<int> inputParam(numInterp, -1);
    std::vector<uint32_t> inputControl(numInterp, 0);
    for (unsigned n = 0; n < numInterp; ++n) {
      const uint32_t c = ctx[0x191 + n];
      if (c & ~(0x3fU | 0x300U | 0x400U)) return fail("SPI_PS_INPUT_CNTL cylindrical/point-sprite modes unsupported");
      inputControl[n] = c;
      if (!(c & 0x20U)) { if ((c & 0x1f) >= paramCount) return fail("PS input reads a parameter the VS does not export"); inputParam[n] = int(c & 0x1f); }
    }

    // ---------------- primitive assembly -----------------------------------
    std::vector<std::array<uint32_t, 3>> triangles;
    std::vector<Vertex> extra; // rect-list fourth vertices
    if (primitive == 0x11) extra.reserve(indices.size() / 3 + 1);
    auto vtx = [&](uint32_t k) -> const Vertex* { return &vertices[slot.at(indices[k])]; };
    std::vector<std::array<const Vertex*, 3>> prims;
    if (primitive == 4) for (size_t k = 0; k + 2 < indices.size(); k += 3) prims.push_back({vtx(k), vtx(k + 1), vtx(k + 2)});
    else if (primitive == 5) for (size_t k = 0; k + 2 < indices.size(); ++k)
      prims.push_back(k & 1 ? std::array<const Vertex*, 3>{vtx(k + 1), vtx(k), vtx(k + 2)} : std::array<const Vertex*, 3>{vtx(k), vtx(k + 1), vtx(k + 2)});
    else for (size_t k = 0; k + 2 < indices.size(); k += 3) {
      const Vertex *a = vtx(k), *b = vtx(k + 1), *c = vtx(k + 2);
      if (a->pos[3] != b->pos[3] || a->pos[3] != c->pos[3]) return fail("rect list with non-uniform W");
      Vertex d; d.params.resize(a->params.size());
      for (unsigned q = 0; q < 4; ++q) d.pos[q] = b->pos[q] + c->pos[q] - a->pos[q];
      for (size_t p = 0; p < d.params.size(); ++p) for (unsigned q = 0; q < 4; ++q) d.params[p][q] = b->params[p][q] + c->params[p][q] - a->params[p][q];
      extra.push_back(std::move(d));
      prims.push_back({a, b, c});
      prims.push_back({c, b, &extra.back()});
    }
    last.primitives = prims.size();

    // ---------------- raster + pixel shading --------------------------------
    // Setup runs serially in submission order. Shading runs in horizontal
    // bands of 32 rows (quads never straddle a band): every band visits all
    // triangles in order and writes only its own rows, so each pixel sees
    // exactly the serial sequence of fragments. Workers own their interpreter,
    // wave batch and counters; the result is independent of the thread count.
    struct Screen { int64_t x, y; double z, invW; };
    struct Tri { uint32_t setup; Screen a, b, c; int64_t area; };
    struct Worker {
      GCNShader ps; std::vector<Fragment> batch; Stats stats; std::string error;
      uint64_t minX = ~0ull, minY = ~0ull, maxX = 0, maxY = 0;
      // Offload: fragments to shade, and the batches to resolve afterwards.
      struct Deferred { std::vector<Fragment> batch; std::vector<uint8_t> lanes; size_t first = 0; };
      std::vector<GcnPixelFragment> pending; std::vector<Deferred> deferred;
    };
    std::vector<Setup> setups;
    std::vector<Tri> tris;
    uint64_t writtenMinX = ~0ull, writtenMinY = ~0ull, writtenMaxX = 0, writtenMaxY = 0;
    // P0/P10/P20 of one PS input channel for a setup (the interpreter's
    // interpolation callback and the offload table use this one function).
    auto interpolant = [&](const Setup& s, unsigned attr, unsigned chan, float out[3]) {
      const uint32_t control = inputControl[attr];
      if (inputParam[attr] < 0) {
        const unsigned def = (control >> 8) & 3;
        const float values[4][4] = {{0, 0, 0, 0}, {0, 0, 0, 1}, {1, 1, 1, 0}, {1, 1, 1, 1}};
        out[0] = values[def][chan]; out[1] = out[2] = 0; return;
      }
      const unsigned p = unsigned(inputParam[attr]);
      float value[3];
      for (unsigned v = 0; v < 3; ++v) {
        value[v] = 0;
        for (unsigned k = 0; k < 3; ++k) value[v] += s.bary[v][k] * s.source[k]->params[p][chan];
      }
      if (control & 0x400U) { const float flat = s.source[provokeLast ? 2 : 0]->params[p][chan]; out[0] = flat; out[1] = out[2] = 0; return; }
      out[0] = value[0]; out[1] = value[1] - value[0]; out[2] = value[2] - value[0];
    };
    PixelDraw offload;
    PixelDraw* offloadDraw = nullptr;
    struct OffloadRelease {   // the backend's per-draw resources, on every exit path
      PixelBackend*& backend; PixelDraw*& draw;
      ~OffloadRelease() { if (backend && draw) backend->release(*draw); }
    } offloadRelease{pixelBackend, offloadDraw};
    // Depth test/write and colour write of a shaded batch, in lane order.
    auto resolve = [&](Worker& wk, const std::vector<Fragment>& batch, const GCNShader::Export* color) -> bool {
      auto& last = wk.stats; std::string& error = wk.error;
      uint64_t &writtenMinX = wk.minX, &writtenMinY = wk.minY, &writtenMaxX = wk.maxX, &writtenMaxY = wk.maxY;
      for (unsigned lane = 0; lane < batch.size(); ++lane) {
        const auto& f = batch[lane];
        if (!f.covered || !((color->exec >> lane) & 1)) continue;
        ++last.fragments;
        if (depthTest) {
          const float stored = plane->z[size_t(f.y) * plane->width + f.x];
          bool pass = true;
          switch (depthFunc) { case 0: pass = false; break; case 1: pass = f.z < stored; break; case 2: pass = f.z == stored; break; case 3: pass = f.z <= stored; break;
            case 4: pass = f.z > stored; break; case 5: pass = f.z != stored; break; case 6: pass = f.z >= stored; break; default: pass = true; }
          if (!pass) { ++last.depthRejected; continue; }
        }
        if (depthWrite) { plane->z[size_t(f.y) * plane->width + f.x] = depthClear ? clearDepth : f.z; ++last.depthWrites; }
        if (!colorEnabled) continue;
        std::array<float, 4> src{};
        for (unsigned c = 0; c < 4; ++c) {
          if (colorFormat == 4) {
            if (!color->compressed) { error = "FP16 colour format requires a compressed export"; return false; }
            const uint32_t packed = color->data[c / 2][lane];
            src[c] = gcnHalfToFloat(uint16_t(c & 1 ? packed >> 16 : packed));
          } else {
            if (color->compressed) { error = "32_ABGR colour format with compressed export"; return false; }
            src[c] = gcnFloat(color->data[c][lane]);
          }
        }
        if (src[3] > 0) ++last.sourceAlphaNonZero;
        bool changed = false;
        if (!writeColor(memory, cbBase, cbHost, cbTile, cbLayout, cbPitch, cbNumber == 6, cbSwap == 1, f.x, f.y, src,
                        blending, blend, ctx, targetMask & shaderMask & 15, changed, error)) return false;
        if (changed) ++last.pixelsChanged;
        ++last.pixelsWritten;
        writtenMinX = std::min<uint64_t>(writtenMinX, f.x); writtenMaxX = std::max<uint64_t>(writtenMaxX, f.x);
        writtenMinY = std::min<uint64_t>(writtenMinY, f.y); writtenMaxY = std::max<uint64_t>(writtenMaxY, f.y);
      }
      return true;
    };
    auto flushWorker = [&](Worker& wk) -> bool {
      auto& batch = wk.batch; auto& ps = wk.ps; auto& last = wk.stats; std::string& error = wk.error;
      if (batch.empty()) return true;
      GCNShader::Wave wave;
      wave.configure(std::min(psSgprs, 104u), psVgprs);
      wave.fp32Denorm = (psRsrc1 >> 16) & 3;   // FLOAT_MODE fp32 denormals
      for (unsigned k = 0; k < psUser; ++k) wave.s[k] = sh[0xc + k];
      if (psUser < wave.sgprs) wave.s[psUser] = 0; // PRIM_MASK: interpolation uses per-lane setups
      uint64_t covered = 0;
      unsigned reg = 0;
      const bool persp = inputAddress & 2, linear = inputAddress & 0x20;
      const unsigned perspReg = reg; if (persp) reg += 2;
      const unsigned linearReg = reg; if (linear) reg += 2;
      const unsigned posXReg = reg; if (inputAddress & 0x100) reg += 1;
      const unsigned posYReg = reg; if (inputAddress & 0x200) reg += 1;
      if (reg > psVgprs) { error = "PS input VGPRs exceed allocation"; return false; }
      for (unsigned lane = 0; lane < batch.size(); ++lane) {
        const auto& f = batch[lane];
        if (f.covered) covered |= uint64_t(1) << lane;
        if (inputEnable & 2) { wave.v[perspReg][lane] = gcnBits(f.i); wave.v[perspReg + 1][lane] = gcnBits(f.j); }
        if (inputEnable & 0x20) { wave.v[linearReg][lane] = gcnBits(f.li); wave.v[linearReg + 1][lane] = gcnBits(f.lj); }
        if (inputEnable & 0x100) wave.v[posXReg][lane] = gcnBits(float(f.x) + 0.5f);
        if (inputEnable & 0x200) wave.v[posYReg][lane] = gcnBits(float(f.y) + 0.5f);
      }
      wave.setExec(covered);
      wave.interpolate = [&](unsigned attr, unsigned chan, unsigned lane, float out[3]) {
        if (attr >= numInterp || lane >= batch.size()) return false;
        interpolant(setups[batch[lane].setup], attr, chan, out);
        return true;
      };
      std::string why;
      if (offloadDraw) {
        // Deferred: covered lanes only (helper lanes never export, no derivative
        // is taken); the worker shades all its fragments in one backend call
        // after its bands are rasterised (finishOffload), then resolves the
        // batches in their original order.
        typename Worker::Deferred d; d.batch = batch; d.first = wk.pending.size();
        for (unsigned lane = 0; lane < batch.size(); ++lane) {
          if (!batch[lane].covered) continue;
          GcnPixelFragment f{}; f.setup = batch[lane].setup;
          for (unsigned r = 0; r < gcnPixelInputs && r < psVgprs; ++r) f.inputs[r] = wave.v[r][lane];
          wk.pending.push_back(f); d.lanes.push_back(uint8_t(lane));
        }
        wk.deferred.push_back(std::move(d));
        batch.clear();
        return true;
      }
      if (!ps.run(memory, psProgram, wave, why)) { error = "PS 0x" + hex(psProgram) + ": " + why; return false; }
      ++last.psWaves;
      const GCNShader::Export* color = nullptr;
      for (const auto& e : wave.exports) {
        if (e.target == 0) { if (color) { error = "PS exported MRT0 twice"; return false; } color = &e; }
        else if (e.target != 9) { error = "PS export target " + std::to_string(e.target) + " unsupported"; return false; }
      }
      if (!color) { error = "PS did not export MRT0"; return false; }
      const bool ok = resolve(wk, batch, color);
      batch.clear();
      return ok;
    };
    auto finishOffload = [&](Worker& wk) -> bool {
      if (wk.deferred.empty()) return true;
      std::vector<GcnPixelOutput> outputs(wk.pending.size());
      std::string why;
      if (!wk.pending.empty() && !pixelBackend->shade(*offloadDraw, wk.pending.data(), wk.pending.size(), outputs.data(), why)) {
        wk.error = "PS 0x" + hex(psProgram) + " offload: " + why; return false;
      }
      ++wk.stats.offloadBatches; wk.stats.offloadFragments += wk.pending.size();
      GCNShader::Export color;
      color.target = 0; color.compressed = offloadDraw->translation.mrt0Compressed;
      for (const auto& d : wk.deferred) {
        color.exec = 0;
        for (size_t k = 0; k < d.lanes.size(); ++k) {
          const GcnPixelOutput& o = outputs[d.first + k];
          if (!o.exported) continue;
          color.exec |= uint64_t(1) << d.lanes[k];
          for (unsigned c = 0; c < 4; ++c) color.data[c][d.lanes[k]] = o.data[c];
        }
        if (!resolve(wk, d.batch, &color)) return false;
      }
      wk.deferred.clear(); wk.pending.clear();
      return true;
    };

    for (const auto& prim : prims) {
      // Homogeneous clipping (W > 0 and the Z range), Sutherland-Hodgman.
      std::vector<ClipVertex> poly(3);
      for (unsigned k = 0; k < 3; ++k) { poly[k].pos = prim[k]->pos; poly[k].bary = {0, 0, 0}; poly[k].bary[k] = 1; }
      auto clipPlane = [&](auto distance) {
        std::vector<ClipVertex> out;
        for (size_t k = 0; k < poly.size(); ++k) {
          const auto& a = poly[k]; const auto& b = poly[(k + 1) % poly.size()];
          const double da = distance(a.pos), db = distance(b.pos);
          if (da >= 0) out.push_back(a);
          if ((da >= 0) != (db >= 0)) {
            const double t = da / (da - db);
            ClipVertex c;
            for (unsigned q = 0; q < 4; ++q) c.pos[q] = float(a.pos[q] + t * (b.pos[q] - a.pos[q]));
            for (unsigned q = 0; q < 3; ++q) c.bary[q] = float(a.bary[q] + t * (b.bary[q] - a.bary[q]));
            out.push_back(c);
          }
        }
        poly.swap(out);
      };
      if (!clipDisabled) {
        clipPlane([](const std::array<float, 4>& p) { return double(p[3]) - 1e-6; });
        if (dxClip) clipPlane([](const std::array<float, 4>& p) { return double(p[2]); });
        else clipPlane([](const std::array<float, 4>& p) { return double(p[2]) + p[3]; });
        clipPlane([](const std::array<float, 4>& p) { return double(p[3]) - p[2]; });
      } else {
        for (const auto& v : poly) if (!(v.pos[3] > 0)) return fail("clipping disabled with W <= 0");
      }
      if (poly.size() < 3) { ++last.clippedAway; continue; }
      // Viewport transform and 1/256 sub-pixel snapping.
      std::vector<Screen> scr(poly.size());
      for (size_t k = 0; k < poly.size(); ++k) {
        const double w = poly[k].pos[3], inv = 1.0 / w;
        const double x = poly[k].pos[0] * inv * xScale + xOffset, y = poly[k].pos[1] * inv * yScale + yOffset;
        const double z = std::min<double>(std::max<double>(poly[k].pos[2] * inv * zScale + zOffset, zMin), zMax);
        if (!(std::fabs(x) < 1e6 && std::fabs(y) < 1e6)) return fail("vertex outside the rasteriser range");
        scr[k] = {int64_t(std::llround(x * 256)), int64_t(std::llround(y * 256)), z, inv};
      }
      for (size_t k = 1; k + 1 < poly.size(); ++k) {
        const size_t idx[3] = {0, k, k + 1};
        const Screen &a = scr[idx[0]], &b = scr[idx[1]], &c = scr[idx[2]];
        const int64_t area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
        if (!area) { ++last.culled; continue; }
        // PA_SU_SC_MODE_CNTL.FACE refers to the winding seen on screen
        // (window y grows downward): visually counter-clockwise triangles
        // have a negative determinant here. Kero Blaster's first frame
        // (FACE=CCW, cull back) submits all 1032 sprite triangles with
        // negative determinants; the opposite sign culled every one.
        const bool ccw = area < 0, front = frontCw ? !ccw : ccw;
        if (primitive != 0x11 && ((front && cullFront) || (!front && cullBack))) { ++last.culled; continue; }
        ++last.rasterized;
        Setup s;
        s.source = prim; s.front = front;
        for (unsigned v = 0; v < 3; ++v) { s.bary[v] = poly[idx[v]].bary; s.invW[v] = scr[idx[v]].invW; s.z[v] = scr[idx[v]].z; }
        const uint32_t setupIndex = uint32_t(setups.size());
        setups.push_back(s);
        tris.push_back({setupIndex, a, b, c, area});
      }
    }
    if (pixelBackend) {
      std::string why;
      if (prepareOffload(memory, offload, psProgram, psRsrc1, psVgprs, psSgprs, psUser, sh, numInterp, setups.size(),
                         [&](size_t setup, unsigned attr, unsigned chan, float out[3]) { interpolant(setups[setup], attr, chan, out); }, why) &&
          pixelBackend->prepare(offload, why)) {
        offloadDraw = &offload; last.offloadedDraws = 1;
      } else { last.offloadRefusals = 1; lastOffloadRefusal = why; }
    }
    constexpr int64_t bandRows = 32;
    const int64_t bandCount = sy1 > sy0 ? (int64_t(sy1) + bandRows - 1) / bandRows : 0;
    auto rasterBand = [&](Worker& wk, int64_t bandY0, int64_t bandY1) -> bool {
      for (const auto& t : tris) {
        const uint32_t setupIndex = t.setup; const Screen &a = t.a, &b = t.b, &c = t.c; const int64_t area = t.area;
        const int64_t minX = std::max<int64_t>(sx0, (std::min({a.x, b.x, c.x}) >> 8) - 1), maxX = std::min<int64_t>(sx1 - 1, (std::max({a.x, b.x, c.x}) >> 8) + 1);
        const int64_t minY = std::max<int64_t>(std::max<int64_t>(sy0, bandY0), (std::min({a.y, b.y, c.y}) >> 8) - 1), maxY = std::min<int64_t>(std::min<int64_t>(sy1, bandY1) - 1, (std::max({a.y, b.y, c.y}) >> 8) + 1);
        if (minX > maxX || minY > maxY) continue;
        const auto& s = setups[setupIndex];
        // Edge functions oriented so that the interior is positive; top-left fill rule.
        const Screen* v[3] = {&a, &b, &c};
        const double sign = area > 0 ? 1.0 : -1.0;
        auto edge = [&](const Screen& p, const Screen& q, int64_t px, int64_t py) { return (q.x - p.x) * (py - p.y) - (q.y - p.y) * (px - p.x); };
        auto topLeft = [&](const Screen& p, const Screen& q) {
          const int64_t dx = (q.x - p.x) * int64_t(sign), dy = (q.y - p.y) * int64_t(sign);
          return (dy < 0) || (dy == 0 && dx > 0);
        };
        const bool tl[3] = {topLeft(*v[1], *v[2]), topLeft(*v[2], *v[0]), topLeft(*v[0], *v[1])};
        const double invArea = 1.0 / double(area);
        for (int64_t qy = minY & ~int64_t(1); qy <= maxY; qy += 2)
          for (int64_t qx = minX & ~int64_t(1); qx <= maxX; qx += 2) {
            Fragment quad[4];
            bool any = false;
            for (unsigned k = 0; k < 4; ++k) {
              const int64_t px = qx + (k & 1), py = qy + (k >> 1);
              const int64_t cx = px * 256 + 128, cy = py * 256 + 128;
              const int64_t e0 = edge(*v[1], *v[2], cx, cy), e1 = edge(*v[2], *v[0], cx, cy), e2 = edge(*v[0], *v[1], cx, cy);
              auto inside = [&](int64_t e, bool isTopLeft) { const int64_t o = e * int64_t(sign); return o > 0 || (o == 0 && isTopLeft); };
              const bool cov = px >= sx0 && py >= sy0 && px < sx1 && py < sy1 && inside(e0, tl[0]) && inside(e1, tl[1]) && inside(e2, tl[2]);
              const double b0 = e0 * invArea, b1 = e1 * invArea, b2 = e2 * invArea;
              const double pw0 = b0 * s.invW[0], pw1 = b1 * s.invW[1], pw2 = b2 * s.invW[2], sum = pw0 + pw1 + pw2;
              Fragment& f = quad[k];
              f.x = uint32_t(std::max<int64_t>(px, 0)); f.y = uint32_t(std::max<int64_t>(py, 0)); f.setup = setupIndex; f.covered = cov;
              f.li = float(b1); f.lj = float(b2);
              f.i = float(sum != 0 ? pw1 / sum : b1); f.j = float(sum != 0 ? pw2 / sum : b2);
              f.z = float(b0 * s.z[0] + b1 * s.z[1] + b2 * s.z[2]);
              any |= cov;
            }
            if (!any) continue;
            ++wk.stats.quads;
            for (auto& f : quad) wk.batch.push_back(f);
            if (wk.batch.size() == 64 && !flushWorker(wk)) return false;
          }
      }
      return flushWorker(wk);
    };
    uint64_t pixelArea = 0;
    for (const auto& t : tris) {
      const int64_t w = (std::max({t.a.x, t.b.x, t.c.x}) - std::min({t.a.x, t.b.x, t.c.x})) >> 8, h = (std::max({t.a.y, t.b.y, t.c.y}) - std::min({t.a.y, t.b.y, t.c.y})) >> 8;
      pixelArea += uint64_t(std::max<int64_t>(w, 1)) * uint64_t(std::max<int64_t>(h, 1));
    }
    const unsigned hardware = [] { // MAXPS4_GPU_THREADS overrides the hardware concurrency
      if (const char* e = std::getenv("MAXPS4_GPU_THREADS")) if (const unsigned long n = std::strtoul(e, nullptr, 10)) return unsigned(std::min(n, 256ul));
      return std::max(1u, std::thread::hardware_concurrency()); }();
    const unsigned threads = unsigned(std::min<uint64_t>({uint64_t(maxThreads ? maxThreads : hardware), uint64_t(std::max<int64_t>(bandCount, 1)), pixelArea / 16384 + 1}));
    std::vector<Worker> workers(threads);
    std::vector<std::string> bandErrors(size_t(std::max<int64_t>(bandCount, 0)));
    std::atomic<int64_t> nextBand{0}; std::atomic<bool> failed{false};
    std::vector<std::string> offloadErrors(workers.size());
    auto work = [&](Worker& wk) {
      for (int64_t band; !failed && (band = nextBand.fetch_add(1)) < bandCount;)
        if (!rasterBand(wk, band * bandRows, (band + 1) * bandRows)) { bandErrors[size_t(band)] = wk.error; failed = true; }
      if (offloadDraw && !failed && !finishOffload(wk)) { offloadErrors[size_t(&wk - workers.data())] = wk.error; failed = true; }
    };
    if (threads <= 1) work(workers[0]);
    else {
      std::vector<std::thread> pool;
      for (unsigned k = 1; k < threads; ++k) pool.emplace_back(work, std::ref(workers[k]));
      work(workers[0]);
      for (auto& t : pool) t.join();
    }
    if (failed) {
      for (const auto& e : bandErrors) if (!e.empty()) { error = e; break; }
      if (error.empty()) for (const auto& e : offloadErrors) if (!e.empty()) { error = e; break; }
      return false;
    }
    GCNShader ps;
    for (auto& wk : workers) {
      last.quads += wk.stats.quads; last.psWaves += wk.stats.psWaves; last.fragments += wk.stats.fragments;
      last.pixelsWritten += wk.stats.pixelsWritten; last.pixelsChanged += wk.stats.pixelsChanged; last.sourceAlphaNonZero += wk.stats.sourceAlphaNonZero;
      last.depthWrites += wk.stats.depthWrites; last.depthRejected += wk.stats.depthRejected;
      last.offloadBatches += wk.stats.offloadBatches; last.offloadFragments += wk.stats.offloadFragments;
      writtenMinX = std::min(writtenMinX, wk.minX); writtenMinY = std::min(writtenMinY, wk.minY);
      writtenMaxX = std::max(writtenMaxX, wk.maxX); writtenMaxY = std::max(writtenMaxY, wk.maxY);
      ps.stats.merge(wk.ps.stats);
    }
    last.psInstructions = ps.stats.instructions;
    last.samples = ps.stats.samples;
    lastPixelHistogram = ps.histogramText();
    last.draws = 1;
    total.draws += 1; total.vertices += last.vertices; total.vsWaves += last.vsWaves; total.vsInstructions += last.vsInstructions;
    total.primitives += last.primitives; total.culled += last.culled; total.clippedAway += last.clippedAway; total.rasterized += last.rasterized;
    total.quads += last.quads; total.psWaves += last.psWaves; total.psInstructions += last.psInstructions; total.fragments += last.fragments;
    total.pixelsWritten += last.pixelsWritten; total.pixelsChanged += last.pixelsChanged; total.sourceAlphaNonZero += last.sourceAlphaNonZero; total.depthWrites += last.depthWrites; total.depthRejected += last.depthRejected; total.samples += last.samples;
    total.offloadedDraws += last.offloadedDraws; total.offloadBatches += last.offloadBatches; total.offloadFragments += last.offloadFragments; total.offloadRefusals += last.offloadRefusals;
    if (verbose) {
      std::fprintf(stderr, "GPU.DRAW n=%llu opcode=0x%x count=%u prim=0x%x VS=0x%llx PS=0x%llx cb=0x%llx tile=%u fmt=%u/%u swap=%u depth=%s%s blend=%08x\n",
                   (unsigned long long)total.draws, call.opcode, call.count, primitive, (unsigned long long)vsProgram, (unsigned long long)psProgram,
                   (unsigned long long)cbBase, cbTile, cbFormat, cbNumber, cbSwap, depthEnabled ? (depthClear ? "clear" : "on") : "off",
                   colorEnabled ? "" : " color=off", blending ? blend : 0);
      std::fprintf(stderr, "GPU.VERTEX invocations=%llu waves=%llu instructions=%llu ops=%s\n", (unsigned long long)last.vertices,
                   (unsigned long long)last.vsWaves, (unsigned long long)last.vsInstructions, lastVertexHistogram.c_str());
      if (!vertices.empty())
        std::fprintf(stderr, "GPU.VERTEX first_pos=%g,%g,%g,%g params=%zu\n", vertices[0].pos[0], vertices[0].pos[1], vertices[0].pos[2], vertices[0].pos[3], vertices[0].params.size());
      std::fprintf(stderr, "GPU.RASTER primitives=%llu rasterized=%llu culled=%llu clipped_away=%llu quads=%llu scissor=%d,%d-%d,%d\n",
                   (unsigned long long)last.primitives, (unsigned long long)last.rasterized, (unsigned long long)last.culled,
                   (unsigned long long)last.clippedAway, (unsigned long long)last.quads, sx0, sy0, sx1, sy1);
      if (pixelBackend)
        std::fprintf(stderr, "GPU.OFFLOAD %s batches=%llu fragments=%llu%s%s\n", last.offloadedDraws ? "on" : "off",
                     (unsigned long long)last.offloadBatches, (unsigned long long)last.offloadFragments,
                     last.offloadedDraws ? "" : " reason=", last.offloadedDraws ? "" : lastOffloadRefusal.c_str());
      std::fprintf(stderr, "GPU.PIXEL waves=%llu instructions=%llu fragments=%llu samples=%llu ops=%s\n", (unsigned long long)last.psWaves,
                   (unsigned long long)last.psInstructions, (unsigned long long)last.fragments, (unsigned long long)last.samples, lastPixelHistogram.c_str());
      if (ps.stats.texels)
        std::fprintf(stderr, "GPU.PIXEL texture=0x%llx %llux%llu texels_read=%llu texels_nonzero=%llu\n", (unsigned long long)ps.stats.textureBase,
                     (unsigned long long)ps.stats.textureWidth, (unsigned long long)ps.stats.textureHeight, (unsigned long long)ps.stats.texels,
                     (unsigned long long)ps.stats.texelsNonZero);
      if (ps.stats.texels) scanTexture(memory, ps.stats);
      if (std::getenv("MAXPS4_GPU_MEMSCAN") && !memoryScanned) { memoryScanned = true; scanGuestMemory(memory); }
      std::fprintf(stderr, "GPU.TIME draw=%llu ms=%.1f\n", (unsigned long long)total.draws,
                   std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
      std::fprintf(stderr, "GPU.FRAMEBUFFER base=0x%llx pitch=%u height=%u pixels_written=%llu changed=%llu src_alpha_nonzero=%llu depth_writes=%llu depth_rejected=%llu rect=%llu,%llu-%llu,%llu\n",
                   (unsigned long long)cbBase, cbPitch, cbHeight, (unsigned long long)last.pixelsWritten, (unsigned long long)last.pixelsChanged,
                   (unsigned long long)last.sourceAlphaNonZero, (unsigned long long)last.depthWrites,
                   (unsigned long long)last.depthRejected, last.pixelsWritten ? (unsigned long long)writtenMinX : 0ull,
                   last.pixelsWritten ? (unsigned long long)writtenMinY : 0ull, (unsigned long long)writtenMaxX, (unsigned long long)writtenMaxY);
    }
    return true;
  }

  // Diagnostics: whole-resource content of each sampled texture (once per
  // base) and, on request, a coarse map of non-empty guest memory.
  std::set<uint64_t> scannedTextures;
  bool memoryScanned = false;
  template <class Memory, class ShaderStats>
  void scanTexture(Memory& memory, const ShaderStats& t) {
    if (!scannedTextures.insert(t.textureBase).second) return;
    const uint64_t bpp = t.textureDfmt == 1 ? 1 : t.textureDfmt == 14 ? 16 : 4;
    const uint64_t rows = t.textureTile == 8 ? t.textureHeight : (t.textureHeight + 7) / 8 * 8;
    const uint64_t size = uint64_t(t.texturePitch) * rows * bpp;
    std::vector<uint8_t> chunk(1 << 20);
    uint64_t nonzero = 0, first = ~0ull, lastAt = 0, unreadable = 0;
    for (uint64_t at = 0; at < size; at += chunk.size()) {
      const size_t n = size_t(std::min<uint64_t>(chunk.size(), size - at));
      if (!readGuest(memory, t.textureBase + at, chunk.data(), n)) { unreadable += n; continue; }
      for (size_t k = 0; k < n; ++k)
        if (chunk[k]) { ++nonzero; if (first == ~0ull) first = at + k; lastAt = at + k; }
    }
    std::fprintf(stderr, "GPU.TEXTURE base=0x%llx %llux%llu pitch=%llu tile=%llu dfmt=%llu nfmt=%llu bytes=0x%llx nonzero_bytes=%llu unreadable=%llu first=0x%llx last=0x%llx\n",
                 (unsigned long long)t.textureBase, (unsigned long long)t.textureWidth, (unsigned long long)t.textureHeight,
                 (unsigned long long)t.texturePitch, (unsigned long long)t.textureTile, (unsigned long long)t.textureDfmt,
                 (unsigned long long)t.textureNfmt, (unsigned long long)size, (unsigned long long)nonzero, (unsigned long long)unreadable,
                 (unsigned long long)(nonzero ? first : 0), (unsigned long long)lastAt);
  }
  template <class Memory>
  void scanGuestMemory(Memory& memory) {
    // MAXPS4_GPU_MEMSCAN=start-end (hex); default covers the direct-memory window.
    uint64_t start = 0x200000000ull, end = 0x290000000ull;
    if (const char* r = std::getenv("MAXPS4_GPU_MEMSCAN")) {
      char* rest = nullptr; const uint64_t a = std::strtoull(r, &rest, 16);
      if (rest && *rest == '-') { const uint64_t b = std::strtoull(rest + 1, nullptr, 16); if (b > a) { start = a; end = b; } }
    }
    constexpr uint64_t granule = 1 << 16;
    std::vector<uint8_t> chunk(granule);
    uint64_t runStart = 0, runBytes = 0; bool inRun = false; unsigned printed = 0;
    auto close = [&](uint64_t at) {
      if (inRun && printed++ < 200)
        std::fprintf(stderr, "GPU.MEMSCAN range=0x%llx-0x%llx nonzero_bytes=%llu\n", (unsigned long long)runStart, (unsigned long long)at, (unsigned long long)runBytes);
      inRun = false; runBytes = 0;
    };
    for (uint64_t at = start; at < end; at += granule) {
      uint64_t count = 0;
      if (readGuest(memory, at, chunk.data(), granule)) for (uint8_t b : chunk) count += b != 0;
      if (count) { if (!inRun) { inRun = true; runStart = at; } runBytes += count; }
      else close(at);
    }
    close(end);
    std::fprintf(stderr, "GPU.MEMSCAN done start=0x%llx end=0x%llx ranges=%u\n", (unsigned long long)start, (unsigned long long)end, printed);
  }

  static std::string hex(uint64_t v) { std::ostringstream s; s << std::hex << v; return s.str(); }

  // Everything a PixelBackend needs for one draw, or false (with the reason)
  // when the draw must stay on the interpreter.
  template <class Memory, class Interp>
  static bool prepareOffload(Memory& memory, PixelDraw& d, uint64_t program, uint32_t rsrc1, unsigned vgprs, unsigned sgprs,
                             unsigned user, const Bank& sh, unsigned numInterp, size_t setupCount, Interp interp, std::string& why) {
    d = PixelDraw{};
    d.program = program; d.rsrc1 = rsrc1; d.vgprs = vgprs; d.numInterp = numInterp;
    if (!GCNShaderTranslator::floatModeSupported(rsrc1)) { why = "FLOAT_MODE keeps fp32 denormals (translated code flushes them)"; return false; }
    d.translation = GCNShaderTranslator::translate(memory, program, vgprs);
    if (!d.translation.ok) { why = "translation: " + d.translation.error; return false; }
    const auto& targets = d.translation.exportTargets;
    if (!targets.count(0) || d.translation.exports != targets.size() || targets.size() > 2 || (targets.size() == 2 && !targets.count(9))) {
      why = "exports other than one MRT0 (and the null target)"; return false;
    }
    // Scalar prepass: one lane through the interpreter, with the batch's user SGPRs.
    GCNShader pre; GCNShader::Wave w;
    w.configure(std::min(sgprs, 104u), vgprs);
    w.fp32Denorm = (rsrc1 >> 16) & 3;
    for (unsigned k = 0; k < user; ++k) w.s[k] = sh[0xc + k];
    if (user < w.sgprs) w.s[user] = 0;
    w.setExec(1);
    w.interpolate = [](unsigned, unsigned, unsigned, float out[3]) { out[0] = out[1] = out[2] = 0; return true; };
    if (!pre.run(memory, program, w, why)) { why = "scalar prepass: " + why; return false; }
    d.sgprs.assign(w.s.begin(), w.s.end());
    for (const auto& image : d.translation.images) {
      GCNImageBinding binding;
      if (!binding.describe(&d.sgprs[image.rsrc], &d.sgprs[image.ssamp], image.sampled, why)) { why = "image: " + why; return false; }
      const size_t at = (d.texels.size() + 15) & ~size_t(15);
      if (at + binding.bytes >= (uint64_t(1) << 30)) { why = "images of 1 GiB or more in one draw"; return false; }
      d.texels.resize(at + size_t(binding.bytes));
      for (uint64_t off = 0; off < binding.bytes; off += 1 << 20) {
        const size_t n = size_t(std::min<uint64_t>(binding.bytes - off, 1 << 20));
        if (!memory.permitted(binding.guestBase + off, n, 4) || memory.read_data(binding.guestBase + off, d.texels.data() + at + off, n)) {
          why = "image 0x" + hex(binding.guestBase) + " unreadable"; return false;
        }
      }
      binding.image.base = uint32_t(at);
      d.images.push_back(binding.image);
    }
    d.interp.assign(setupCount * numInterp * 12, 0.0f);
    for (size_t setup = 0; setup < setupCount; ++setup)
      for (unsigned attr = 0; attr < numInterp; ++attr)
        for (unsigned chan = 0; chan < 4; ++chan) interp(setup, attr, chan, &d.interp[((setup * numInterp + attr) * 4 + chan) * 3]);
    d.source = GCNPixelProgram::generate(d.translation);
    d.key = GCNPixelProgram::hash(d.source);
    return true;
  }

  static float blendFactor(unsigned f, const std::array<float, 4>& s, const std::array<float, 4>& d, const std::array<float, 4>& k, unsigned c, bool& ok) {
    switch (f) {
    case 0: return 0; case 1: return 1;
    case 2: return s[c]; case 3: return 1 - s[c];
    case 4: return s[3]; case 5: return 1 - s[3];
    case 6: return d[3]; case 7: return 1 - d[3];
    case 8: return d[c]; case 9: return 1 - d[c];
    case 10: return c == 3 ? 1.0f : std::min(s[3], 1 - d[3]);
    case 13: return k[c]; case 14: return 1 - k[c];
    case 19: return k[3]; case 20: return 1 - k[3];
    default: ok = false; return 0;
    }
  }
  template <class Memory>
  bool writeColor(Memory& memory, uint64_t base, uint8_t* host, unsigned tile, const Display2DThin32& layout, unsigned pitch, bool srgb, bool swapAlt,
                  uint32_t x, uint32_t y, std::array<float, 4> src, bool blending, uint32_t blend, const Bank& ctx, unsigned mask, bool& changed, std::string& error) {
    const uint64_t offset = tile == 10 ? layout.offset(x, y) : (uint64_t(y) * pitch + x) * 4, at = base + offset;
    uint8_t px[4];
    if (host) std::memcpy(px, host + offset, 4);
    else if (memory.read_data(at, px, 4)) { error = "colour buffer unreadable at 0x" + hex(at); return false; }
    uint32_t before = 0;
    std::memcpy(&before, px, 4);
    // Memory component order: STD = R,G,B,A; ALT = B,G,R,A (8_8_8_8).
    const unsigned order[4] = {swapAlt ? 2u : 0u, 1u, swapAlt ? 0u : 2u, 3u};
    std::array<float, 4> dst{};
    for (unsigned c = 0; c < 4; ++c) dst[c] = srgb && c < 3 ? srgbByteToLinear(px[order[c]]) : px[order[c]] / 255.0f;
    for (auto& v : src) v = std::min(1.0f, std::max(0.0f, std::isnan(v) ? 0.0f : v));
    std::array<float, 4> out = src;
    if (blending) {
      const std::array<float, 4> k{gcnFloat(ctx[0x105]), gcnFloat(ctx[0x106]), gcnFloat(ctx[0x107]), gcnFloat(ctx[0x108])};
      const bool separate = blend & (1U << 29);
      bool ok = true;
      for (unsigned c = 0; c < 4; ++c) {
        const bool alpha = c == 3 && separate;
        const unsigned sf = alpha ? (blend >> 16) & 31 : blend & 31, fn = alpha ? (blend >> 21) & 7 : (blend >> 5) & 7, df = alpha ? (blend >> 24) & 31 : (blend >> 8) & 31;
        const float a = src[c] * blendFactor(sf, src, dst, k, c, ok), b = dst[c] * blendFactor(df, src, dst, k, c, ok);
        switch (fn) { case 0: out[c] = a + b; break; case 1: out[c] = a - b; break; case 2: out[c] = std::min(src[c], dst[c]); break;
          case 3: out[c] = std::max(src[c], dst[c]); break; case 4: out[c] = b - a; break; default: ok = false; }
        out[c] = std::min(1.0f, std::max(0.0f, out[c]));
      }
      if (!ok) { error = "blend factor/function unsupported: " + hex(blend); return false; }
    }
    for (unsigned c = 0; c < 4; ++c) {
      if (!((mask >> c) & 1)) continue;
      px[order[c]] = srgb && c < 3 ? linearToSrgbByte(out[c]) : uint8_t(std::lround(out[c] * 255.0f));
    }
    uint32_t after = 0;
    std::memcpy(&after, px, 4);
    changed = before != after;
    if (host) std::memcpy(host + offset, px, 4);
    else if (memory.write_data(at, px, 4)) { error = "colour buffer write failed at 0x" + hex(at); return false; }
    return true;
  }
};
} // namespace maxps4::gpu
