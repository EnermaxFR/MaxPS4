// Software graphics pipeline tests: GCN encodings are assembled here from the
// public Sea Islands field layout; expected values are computed independently
// in this file (tiling bijection, sRGB/blend arithmetic, texel mapping).
// Optionally (MAXPS4_KERO_DIR) the title's own texture_vv.sb / texture_p.sb
// programs are read (read-only) and executed with synthetic inputs.
#include "../Native/GNMCommandExecutor.hpp"
#include "../Native/GuestProviders/GnmEmbeddedVS.hpp"
#include "../Native/maxps4_orbis_v4_translation.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>

using namespace maxps4;
using namespace maxps4::gpu;
using Words = std::vector<uint32_t>;

namespace enc {
uint32_t sop1(unsigned op, unsigned dst, unsigned src) { return 0xbe800000U | dst << 16 | op << 8 | src; }
uint32_t sopp(unsigned op, int16_t imm = 0) { return 0xbf800000U | op << 16 | uint16_t(imm); }
uint32_t smrd(unsigned op, unsigned dst, unsigned base, unsigned dwords) { return 0xc0000000U | op << 22 | dst << 15 | (base / 2) << 9 | 256 | dwords; }
uint32_t vop1(unsigned op, unsigned dst, unsigned src) { return 0x7e000000U | dst << 17 | op << 9 | src; }
uint32_t vop2(unsigned op, unsigned dst, unsigned src0, unsigned vsrc1) { return op << 25 | dst << 17 | (vsrc1 & 255) << 9 | src0; }
Words vop3(unsigned op, unsigned dst, unsigned s0, unsigned s1, unsigned s2 = 0, unsigned neg = 0) { return {0xd0000000U | op << 17 | dst, s0 | s1 << 9 | s2 << 18 | neg << 29}; }
uint32_t vopc(unsigned op, unsigned src0, unsigned vsrc1) { return 0x7c000000U | op << 17 | (vsrc1 & 255) << 9 | src0; }
uint32_t vintrp(unsigned op, unsigned dst, unsigned attr, unsigned chan, unsigned src) { return 0xc8000000U | dst << 18 | op << 16 | attr << 10 | chan << 8 | src; }
Words exp(unsigned target, unsigned en, bool compr, bool done, bool vm, unsigned a, unsigned b, unsigned c, unsigned d) {
  return {0xf8000000U | vm << 12 | done << 11 | compr << 10 | target << 4 | en, a | b << 8 | c << 16 | d << 24};
}
Words mubuf(unsigned op, unsigned vaddr, unsigned vdata, unsigned rsrc, unsigned soffset, bool idxen, unsigned offset = 0) {
  return {0xe0000000U | op << 18 | unsigned(idxen) << 13 | offset, vaddr | vdata << 8 | (rsrc / 4) << 16 | soffset << 24};
}
Words mtbuf(unsigned op, unsigned dfmt, unsigned nfmt, unsigned vaddr, unsigned vdata, unsigned rsrc, unsigned soffset, unsigned offset) {
  return {0xe8000000U | nfmt << 23 | dfmt << 19 | op << 16 | 1U << 13 | offset, vaddr | vdata << 8 | (rsrc / 4) << 16 | soffset << 24};
}
Words mimg(unsigned op, unsigned dmask, unsigned vaddr, unsigned vdata, unsigned rsrc, unsigned ssamp) {
  return {0xf0000000U | op << 18 | dmask << 8, vaddr | vdata << 8 | (rsrc / 4) << 16 | (ssamp / 4) << 21};
}
void add(Words& code, std::initializer_list<Words> parts) { for (const auto& p : parts) code.insert(code.end(), p.begin(), p.end()); }
} // namespace enc
constexpr unsigned V = 256; // VGPR operand base
constexpr unsigned VCC = 106, EXEC = 126, M0 = 124;

static float toLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
static float toSrgb(float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1 / 2.4f) - 0.055f; }
static uint32_t f(float v) { uint32_t b; std::memcpy(&b, &v, 4); return b; }

static void tilingTests() {
  Display2DThin32 t; std::string error;
  assert(t.init(1920, 1080, error) && t.paddedHeight == 1088 && t.sizeBytes() == 1920ull * 1088 * 4);
  assert(!t.init(1000, 1080, error));
  // Hand-derived anchors: element order x0,x1,y0,x2,y1,y2; pipe from x3^y3^x4.
  assert(t.offset(0, 0) == 0 && t.offset(1, 0) == 4 && t.offset(2, 0) == 8 && t.offset(0, 1) == 16 && t.offset(4, 0) == 32);
  assert(t.offset(8, 0) == 256);           // pipe 1
  assert(t.offset(0, 8) == 256 + (8 << 11)); // pipe 1, bank 8 (ty bit0)
  assert(t.offset(128, 0) == (uint64_t(1) << 15) + (uint64_t(Display2DThin32::bank(128, 0)) << 11));
  // Bijection over the padded 1080p surface; every offset in range and aligned.
  std::vector<uint8_t> seen(size_t(t.sizeBytes() / 4), 0);
  for (uint32_t y = 0; y < t.paddedHeight; ++y)
    for (uint32_t x = 0; x < 1920; ++x) {
      const uint64_t o = t.offset(x, y);
      assert(o % 4 == 0 && o < t.sizeBytes() && !seen[o / 4]);
      seen[o / 4] = 1;
    }
}

static void srgbTableTests() {
  for (unsigned b = 0; b < 256; ++b) assert(f(srgbByteToLinear(uint8_t(b))) == f(srgbToLinear(b / 255.0f)));
  auto bitsOf = [](float v) { uint32_t b; std::memcpy(&b, &v, 4); return b; };
  auto floatOf = [](uint32_t b) { float v; std::memcpy(&v, &b, 4); return v; };
  // Around every quantisation step (+-4096 ulps) and over 20M random floats.
  for (unsigned k = 1; k < 256; ++k) {
    uint32_t lo = 0, hi = bitsOf(1.0f);
    while (lo < hi) { const uint32_t mid = lo + (hi - lo) / 2; if (linearToSrgbByteDirect(floatOf(mid)) >= k) hi = mid; else lo = mid + 1; }
    for (uint32_t d = lo > 4096 ? lo - 4096 : 0; d < lo + 4096; ++d) assert(linearToSrgbByte(floatOf(d)) == linearToSrgbByteDirect(floatOf(d)));
  }
  uint64_t state = 61;
  for (unsigned n = 0; n < 20000000; ++n) {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    const float v = floatOf(uint32_t(state >> 33) % (bitsOf(1.0f) + 1));
    assert(linearToSrgbByte(v) == linearToSrgbByteDirect(v));
  }
  for (float v : {-1.0f, 0.0f, 1.0f, 2.0f, std::nanf("")}) assert(linearToSrgbByte(v) == linearToSrgbByteDirect(v));
}

static void conversionTests() {
  srgbTableTests();
  assert(gcnHalf(1.0f, true) == 0x3c00 && gcnHalf(-2.0f, true) == 0xc000 && gcnHalf(0.0f, true) == 0);
  assert(gcnHalf(65504.0f, true) == 0x7bff && gcnHalf(1e9f, true) == 0x7bff && gcnHalf(1e9f, false) == 0x7c00);
  // 1/3 truncates toward zero (0x3555) while round-to-nearest-even gives 0x3555 too; 0.1f differs.
  assert(gcnHalf(0.1f, true) == 0x2e66 && gcnHalf(0.1f, false) == 0x2e66);
  assert(gcnHalf(1.0f + 1.0f / 1024 * 0.75f, true) == 0x3c00 && gcnHalf(1.0f + 1.0f / 1024 * 0.75f, false) == 0x3c01);
  assert(gcnHalfToFloat(0x3c00) == 1.0f && gcnHalfToFloat(0x3800) == 0.5f && gcnHalfToFloat(0x0001) == std::ldexp(1.0f, -24));
  for (int k = 0; k <= 255; ++k) assert(std::lround(linearToSrgb(srgbToLinear(k / 255.0f)) * 255) == k);
}

struct Fixture {
  static constexpr uint64_t base = 0x400000, size = 16 * 1024 * 1024;
  OrbisV4Translation owner{[] {}};
  Fixture() { assert(!owner.reserve(base, size)); assert(!owner.map(base, size, 6)); }
};

static void interpreterTests() {
  Fixture fx;
  auto m = fx.owner.lease();
  using namespace enc;
  const uint64_t code = Fixture::base, data = Fixture::base + 0x1000;
  // Scalar/vector ALU, compare + EXEC, branches and a raw buffer load.
  Words p{sop1(3, 0, 255), f(3.0f),           // s_mov_b32 s0, 3.0
          vop1(1, 1, 0),                      // v_mov_b32 v1, s0
          vop2(31, 1, 0, 0 + 0),              // v_mac_f32 v1, s0, v0  (v1 += 3*v0bits? v0 holds floats)
          vop1(5, 2, V + 3),                  // v_cvt_f32_i32 v2, v3
          vop2(8, 2, 240, V + 2),             // v_mul_f32 v2, 0.5, v2
          vopc(0xd4, 129 + 2, V + 3),         // v_cmpx_gt_u32 vcc, 3, v3  -> lanes 0..2
          sopp(8, 2),                         // s_cbranch_execz +2 (not taken)
          vop2(37, 4, 129 + 9, V + 3),        // v_add_i32 v4, vcc, 10, v3
          sopp(2, 1),                         // s_branch +1
          vop1(1, 4, 128),                    // (skipped) v_mov_b32 v4, 0
          sop1(4, EXEC, 193),                 // s_mov_b64 exec, -1
          smrd(8 + 1, 4, 0 + 8, 1)};          // s_buffer_load_dwordx2 s[4:5], s[8:11], 1
  add(p, {mubuf(12, V - V + 3, 5, 8, 128, true, 4)}); // buffer_load_dword v5, v3, s[8:11], 0 idxen offset:4
  add(p, {vop3(321, 6, V + 0, 240 + 2, V + 1, 1)});    // v_mad_f32 v6, -v0, 1.0, v1
  p.push_back(sopp(1));
  assert(!m.write_data(code, p.data(), p.size() * 4));
  const uint32_t buffer[8] = {11, 22, 33, 44, 55, 66, 77, 88};
  assert(!m.write_data(data, buffer, sizeof(buffer)));
  GCNShader shader;
  GCNShader::Wave w;
  w.configure(16, 8);
  w.setExec(0xff);
  for (unsigned lane = 0; lane < 8; ++lane) { w.v[0][lane] = f(float(lane)); w.v[3][lane] = lane; }
  w.s[8] = uint32_t(data); w.s[9] = uint32_t(data >> 32) | (8u << 16); w.s[10] = 3; w.s[11] = 0x20077fac;
  std::string error;
  assert(shader.run(m, code, w, error));
  for (unsigned lane = 0; lane < 8; ++lane) {
    assert(gcnFloat(w.v[1][lane]) == 3.0f + 3.0f * float(lane));
    assert(gcnFloat(w.v[2][lane]) == 0.5f * float(lane));
    if (lane < 3) assert(w.v[4][lane] == 10 + lane);
    else assert(w.v[4][lane] == 0); // inactive under EXEC: unchanged
    // stride 8, 3 records: index 3+ is out of range and reads zero.
    assert(w.v[5][lane] == (lane < 3 ? buffer[lane * 2 + 1] : 0));
    assert(gcnFloat(w.v[6][lane]) == 3.0f + 2.0f * float(lane));
  }
  assert(w.s[4] == 22 && w.s[5] == 33 && w.exec() == ~uint64_t(0) && shader.stats.bufferOutOfRange == 5);
  // Unknown opcodes fail closed with the program counter.
  const Words bad{vop1(1, 0, 128), 0x7e00ff00U /* VOP1 op 127 */, sopp(1)};
  assert(!m.write_data(code + 0x400, bad.data(), bad.size() * 4));
  GCNShader::Wave w2; w2.configure(8, 4); w2.setExec(1);
  assert(!shader.run(m, code + 0x400, w2, error));
  assert(error.find("pc=0x400404") != std::string::npos && error.find("not implemented") != std::string::npos);
  // Missing s_endpgm into unmapped memory fails as unreadable code.
  const uint64_t tail = Fixture::base + Fixture::size - 4;
  const uint32_t mov = vop1(1, 0, 128);
  assert(!m.write_data(tail, &mov, 4));
  GCNShader::Wave w3; w3.configure(8, 4); w3.setExec(1);
  assert(!shader.run(m, tail, w3, error) && error.find("unreadable") != std::string::npos);
  // VGPR beyond the allocation is refused.
  const Words over{vop1(1, 9, 128), sopp(1)};
  assert(!m.write_data(code + 0x800, over.data(), over.size() * 4));
  GCNShader::Wave w4; w4.configure(8, 4); w4.setExec(1);
  assert(!shader.run(m, code + 0x800, w4, error) && error.find("beyond allocation") != std::string::npos);
}

// Common render state mirroring the title's first frame (base PS4 registers).
struct Scene {
  GNMCommandExecutor engine;
  static constexpr uint64_t base = Fixture::base;
  static constexpr uint64_t clearVs = base + 0x0, clearPs = base + 0x200, vs = base + 0x400, fetch = base + 0x600, ps = base + 0x800,
                            table = base + 0x1000, vertices = base + 0x1100, colors = base + 0x1200, matrix = base + 0x1300,
                            indices = base + 0x1400, clearColor = base + 0x1500, texture = base + 0x10000, target = base + 0x100000;
  void common() {
    auto& c = engine.state.context;
    GNMCommandExecutor::clearContext(c);
    c[0x10f] = f(64); c[0x110] = f(64); c[0x111] = f(-32); c[0x112] = f(32); c[0x113] = f(0.5f); c[0x114] = f(0.5f);
    c[0x206] = 0x43f; c[0x2f9] = 0x2d; c[0x0c] = 0; c[0x0d] = (64u << 16) | 128u;
    c[0x318] = uint32_t(target >> 8); c[0x319] = (128 / 8 - 1) | ((128 / 8 - 1) << 20); c[0x31a] = 128 * 64 / 64 - 1;
    c[0x31c] = (10 << 2) | (6 << 8) | (1 << 11) | (1 << 15); c[0x31d] = 10 | (10 << 5);
    c[0x202] = 0x00cc0010; c[0x8e] = 0xf; c[0x8f] = 0xf; c[0x1b1] = 2; c[0x1c3] = 4;
  }
};

static std::array<uint8_t, 4> pixelAt(OrbisV4Translation::Lease& m, uint64_t target, uint32_t x, uint32_t y) {
  Display2DThin32 t; std::string e; assert(t.init(128, 64, e));
  std::array<uint8_t, 4> p{}; assert(!m.read_data(target + t.offset(x, y), p.data(), 4)); return p;
}

static void pipelineTests() {
  Fixture fx;
  auto m = fx.owner.lease();
  using namespace enc;
  Scene s;
  // --- draw A: embedded full-screen VS (rect list) + constant-colour PS ---
  const auto& evs = gnm_embedded::FullscreenVS;
  assert(!m.write_data(Scene::clearVs, evs.data(), 16 * 4));
  Words clearPs{smrd(10, 4, 0, 0), sopp(12, 0x7f), vop1(1, 0, 4), vop1(1, 1, 5), vop1(1, 2, 6), vop1(1, 3, 7)};
  add(clearPs, {exp(0, 15, false, true, true, V - V + 0, 1, 2, 3)});
  clearPs.push_back(sopp(1));
  assert(!m.write_data(Scene::clearPs, clearPs.data(), clearPs.size() * 4));
  const float clear[4] = {0.25f, 0.5f, 0.75f, 1.0f};
  assert(!m.write_data(Scene::clearColor, clear, 16));
  // Poison the target so every pixel must be produced by the pipeline.
  std::vector<uint8_t> poison(128 * 64 * 4, 0xa5);
  assert(!m.write_data(Scene::target, poison.data(), poison.size()));
  s.common();
  auto& c = s.engine.state.context; auto& sh = s.engine.state.shader;
  sh[0x48] = uint32_t(Scene::clearVs >> 8); sh[0x4a] = 0x000c0000; sh[0x4b] = 4;
  sh[0x8] = uint32_t(Scene::clearPs >> 8); sh[0xa] = 0x002c0000; sh[0xb] = 8;
  sh[0xc] = uint32_t(Scene::clearColor); sh[0xd] = 0x00100000; sh[0xe] = 1; sh[0xf] = 0x20077fac;
  c[0x1b1] = 0; c[0x1c5] = 9; c[0x1b3] = 2; c[0x1b4] = 2; c[0x1b6] = 0; c[0x205] = 0x242;
  s.engine.state.uconfig[0x242] = 0x11;
  auto packet = [](uint32_t op, std::initializer_list<uint32_t> data) {
    Words words{0xc0000000U | (uint32_t(data.size() - 1) << 16) | (op << 8)}; words.insert(words.end(), data); return words;
  };
  std::string error;
  s.engine.remaining = 65536;
  assert(s.engine.execute(m, Scene::base, packet(0x2d, {3, 2}), error));
  assert(s.engine.draws == 1 && s.engine.renderer.last.pixelsWritten == 128 * 64 && s.engine.renderer.last.vertices == 3);
  uint8_t expectClear[4];
  for (unsigned k = 0; k < 3; ++k) expectClear[k] = uint8_t(std::lround(toSrgb(clear[k]) * 255));
  expectClear[3] = 255;
  for (uint32_t y = 0; y < 64; ++y) for (uint32_t x = 0; x < 128; ++x) {
    const auto p = pixelAt(m, Scene::target, x, y); // memory order B,G,R,A (COMP_SWAP ALT)
    assert(p[0] == expectClear[2] && p[1] == expectClear[1] && p[2] == expectClear[0] && p[3] == 255);
  }
  // Disabled colour target + DEPTH_CLEAR: depth plane written, no colour.
  c[0x8e] = 0; c[0x0] = 1; c[0x200] = 0x76; c[0x0b] = f(1.0f); c[0x10] = 3; c[0x14] = 0x1234; c[0x16] = (128 / 8 - 1) | ((64 / 8 - 1) << 11);
  assert(s.engine.execute(m, Scene::base, packet(0x2d, {3, 2}), error));
  assert(s.engine.renderer.last.pixelsWritten == 0 && s.engine.renderer.last.depthWrites == 128 * 64);
  assert(pixelAt(m, Scene::target, 5, 5)[0] == expectClear[2]);
  c[0x8e] = 0xf; c[0x0] = 0; c[0x200] = 0x14;

  // --- draw B: indexed list, fetch shader call, matrix, texture, blend -----
  Words fetchCode;
  add(fetchCode, {{smrd(2, 24, 2, 0), smrd(2, 28, 2, 4), sopp(12, 0x7f)},
                  mubuf(2, 0, 4, 24, 128, true),                 // buffer_load_format_xyz v[4:6], v0, s[24:27]
                  mubuf(3, 0, 8, 28, 128, true),                 // buffer_load_format_xyzw v[8:11], v0, s[28:31] (8_8_8_8 UNORM)
                  mtbuf(1, 11, 7, 0, 12, 24, 128, 12),           // tbuffer_load_format_xy v[12:13] dfmt 32_32 float, offset 12
                  {sopp(12, 0xf70), sop1(32, 0, 0)}});           // s_setpc_b64 s[0:1]
  assert(!m.write_data(Scene::fetch, fetchCode.data(), fetchCode.size() * 4));
  Words vsCode{sop1(33, 0, 0), smrd(12, 8, 4, 0), sopp(12, 0x7f),
               vop1(1, 1, 20), vop1(1, 2, 21), vop1(1, 3, 22), vop1(1, 7, 23)};
  for (unsigned k = 0; k < 3; ++k) {
    vsCode.push_back(vop2(31, 1, 8 + 4 * k, V + 4 + k)); vsCode.push_back(vop2(31, 2, 9 + 4 * k, V + 4 + k));
    vsCode.push_back(vop2(31, 3, 10 + 4 * k, V + 4 + k)); vsCode.push_back(vop2(31, 7, 11 + 4 * k, V + 4 + k));
  }
  add(vsCode, {exp(12, 15, false, true, false, 1, 2, 3, 7), exp(32, 15, false, false, false, 8, 9, 10, 11), exp(33, 15, false, false, false, 12, 13, 13, 13)});
  vsCode.push_back(sopp(1));
  assert(!m.write_data(Scene::vs, vsCode.data(), vsCode.size() * 4));
  Words psCode{sop1(3, M0, 12), sop1(4, VCC, EXEC), sop1(10, EXEC, EXEC),
               vintrp(0, 2, 1, 0, 0), vintrp(1, 2, 1, 0, 1), vintrp(0, 3, 1, 1, 0), vintrp(1, 3, 1, 1, 1)};
  add(psCode, {mimg(32, 15, 2, 4, 0, 8), {sopp(12, 0xf70)}});
  for (unsigned k = 0; k < 4; ++k) { psCode.push_back(vintrp(0, 8 + k, 0, k, 0)); psCode.push_back(vintrp(1, 8 + k, 0, k, 1)); }
  for (unsigned k = 0; k < 4; ++k) psCode.push_back(vop2(8, 4 + k, V + 4 + k, 8 + k));
  add(psCode, {{vop2(47, 0, V + 4, 5), vop2(47, 1, V + 6, 7), sop1(4, EXEC, VCC)}, exp(0, 15, true, true, true, 0, 1, 0, 0), {sopp(1)}});
  assert(!m.write_data(Scene::ps, psCode.data(), psCode.size() * 4));
  // Vertex data: TL, TR, BL, BR covering pixels [16,48) x [8,40); positions are
  // half NDC (matrix scales by 2), stride 20 = xyz + uv.
  struct VertexData { float x, y, z, u, v; };
  const VertexData verts[4] = {{-0.375f, 0.375f, 0.5f, 0, 0}, {-0.125f, 0.375f, 0.5f, 1, 0}, {-0.375f, -0.125f, 0.5f, 0, 1}, {-0.125f, -0.125f, 0.5f, 1, 1}};
  assert(!m.write_data(Scene::vertices, verts, sizeof(verts)));
  const uint8_t vcolor[4][4] = {{255, 255, 255, 204}, {255, 255, 255, 204}, {255, 255, 255, 204}, {255, 255, 255, 204}};
  assert(!m.write_data(Scene::colors, vcolor, sizeof(vcolor)));
  const float mat[16] = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  assert(!m.write_data(Scene::matrix, mat, sizeof(mat)));
  // V# table: position/uv buffer (32_32_32 float, stride 20) and colour buffer (8_8_8_8 unorm, stride 4).
  const uint32_t vsharps[8] = {uint32_t(Scene::vertices), 20u << 16, 4, 0xfac | (7u << 12) | (13u << 15),
                               uint32_t(Scene::colors), 4u << 16, 4, 0xfac | (0u << 12) | (10u << 15)};
  assert(!m.write_data(Scene::table, vsharps, sizeof(vsharps)));
  // Visually counter-clockwise on screen (y down) is front: two front faces + one back face.
  const uint16_t idx[9] = {0, 2, 1, 1, 2, 3, 0, 1, 2};
  assert(!m.write_data(Scene::indices, idx, sizeof(idx)));
  // 4x4 sRGB RGBA texture, linear aligned, pitch 64.
  uint8_t texels[4][4][4];
  for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
    texels[y][x][0] = uint8_t(40 + 50 * x); texels[y][x][1] = uint8_t(30 + 60 * y); texels[y][x][2] = uint8_t(200 - 10 * (x + y)); texels[y][x][3] = uint8_t(255 - 40 * ((x + y) & 3));
    assert(!m.write_data(Scene::texture + (uint64_t(y) * 64 + x) * 4, texels[y][x], 4));
  }
  sh[0x48] = uint32_t(Scene::vs >> 8); sh[0x4a] = 0x002c00c3; sh[0x4b] = 0x10;
  sh[0x4c] = uint32_t(Scene::fetch); sh[0x4d] = 0; sh[0x4e] = uint32_t(Scene::table); sh[0x4f] = 0;
  sh[0x50] = uint32_t(Scene::matrix); sh[0x51] = 16u << 16; sh[0x52] = 4; sh[0x53] = 0x20077fac;
  sh[0x8] = uint32_t(Scene::ps >> 8); sh[0xa] = 0x002c0043; sh[0xb] = 0x18;
  const uint32_t tsharp[8] = {uint32_t(Scene::texture >> 8), (10u << 20) | (9u << 26), 3 | (3u << 14), 0xfac | (8u << 20) | (9u << 28), 63u << 13, 0, 0, 0};
  for (unsigned k = 0; k < 8; ++k) sh[0xc + k] = tsharp[k];
  for (unsigned k = 0; k < 4; ++k) sh[0x14 + k] = 0; // point sampling, wrap
  c[0x1b1] = 2; c[0x1c5] = 4; c[0x1b6] = 2; c[0x191] = 0; c[0x192] = 1;
  c[0x1e0] = 0x40000504; // SRC_ALPHA, ONE_MINUS_SRC_ALPHA
  c[0x205] = 0x242;      // cull back faces, CCW front
  s.engine.state.uconfig[0x242] = 4;
  s.engine.state.indexType = 0;
  assert(s.engine.execute(m, Scene::base, packet(0x27, {9, uint32_t(Scene::indices), 0, 9, 0}), error));
  const auto& st = s.engine.renderer.last;
  assert(st.primitives == 3 && st.culled == 1 && st.rasterized == 2 && st.vertices == 4 && st.pixelsWritten == 32 * 32);
  int worst = 0;
  for (uint32_t y = 0; y < 64; ++y) for (uint32_t x = 0; x < 128; ++x) {
    const auto p = pixelAt(m, Scene::target, x, y);
    uint8_t expect[4] = {expectClear[0], expectClear[1], expectClear[2], 255};
    if (x >= 16 && x < 48 && y >= 8 && y < 40) {
      const auto& t = texels[(y - 8) / 8][(x - 16) / 8];
      const float a = (t[3] / 255.0f) * (204 / 255.0f);
      for (unsigned k = 0; k < 3; ++k) {
        const float src = toLinear(t[k] / 255.0f), dst = clear[k];
        expect[k] = uint8_t(std::lround(toSrgb(src * a + dst * (1 - a)) * 255));
      }
      expect[3] = uint8_t(std::lround(std::min(1.0f, a * a + 1.0f * (1 - a)) * 255));
    }
    const int d[4] = {p[2] - expect[0], p[1] - expect[1], p[0] - expect[2], p[3] - expect[3]};
    for (int v : d) worst = std::max(worst, std::abs(v));
  }
  assert(worst <= 1); // FP16 export rounding only
  const uint64_t texturedPixels = st.pixelsWritten;
  // Refusals: unsupported tiling, unmapped target, instancing.
  c[0x31d] = 14;
  assert(!s.engine.execute(m, Scene::base, packet(0x27, {9, uint32_t(Scene::indices), 0, 9, 0}), error) && error.find("tile mode index 14") != std::string::npos);
  c[0x31d] = 10; c[0x318] = uint32_t((Fixture::base + Fixture::size - 0x4000) >> 8);
  assert(!s.engine.execute(m, Scene::base, packet(0x27, {9, uint32_t(Scene::indices), 0, 9, 0}), error) && error.find("not mapped") != std::string::npos);
  c[0x318] = uint32_t(Scene::target >> 8); s.engine.state.instances = 2;
  assert(!s.engine.execute(m, Scene::base, packet(0x27, {9, uint32_t(Scene::indices), 0, 9, 0}), error) && error.find("instanced") != std::string::npos);
  s.engine.state.instances = 1;
  std::cout << "pipeline: clear " << 128 * 64 << " px, textured " << texturedPixels << " px, worst channel error " << worst << "\n";
}

// Optional: the title's own shader programs, read-only.
static Words sbProgram(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::vector<char> bytes((std::istreambuf_iterator<char>(in)), {});
  Words words(bytes.size() / 4);
  std::memcpy(words.data(), bytes.data(), words.size() * 4);
  for (size_t k = 0; k < words.size(); ++k)
    if (words[k] == 0xbeeb03ffU) { // s_mov_b32 vcc_hi, literal: program entry
      for (size_t e = k; e < words.size(); ++e) if (words[e] == 0xbf810000U) return Words(words.begin() + long(k), words.begin() + long(e) + 1);
    }
  return {};
}
static void titleShaderTests() {
  const char* dir = std::getenv("MAXPS4_KERO_DIR");
  if (!dir) { std::cout << "title shaders: skipped (MAXPS4_KERO_DIR unset)\n"; return; }
  const Words vsCode = sbProgram(std::string(dir) + "/texture_vv.sb"), psCode = sbProgram(std::string(dir) + "/texture_p.sb");
  if (vsCode.empty() || psCode.empty()) { std::cout << "title shaders: not found\n"; return; }
  Fixture fx;
  auto m = fx.owner.lease();
  using namespace enc;
  Scene s;
  assert(!m.write_data(Scene::vs, vsCode.data(), vsCode.size() * 4));
  assert(!m.write_data(Scene::ps, psCode.data(), psCode.size() * 4));
  // Synthetic fetch shader with the input layout texture_vv.sb expects:
  // v[4:6] position, v[8:11] colour, v[12:13] uv.
  Words fetchCode;
  add(fetchCode, {{smrd(2, 8, 2, 0), smrd(2, 12, 2, 4), sopp(12, 0x7f)}, mubuf(2, 0, 4, 8, 128, true), mubuf(3, 0, 8, 12, 128, true),
                  mtbuf(1, 11, 7, 0, 12, 8, 128, 12), {sopp(12, 0xf70), sop1(32, 0, 0)}});
  assert(!m.write_data(Scene::fetch, fetchCode.data(), fetchCode.size() * 4));
  struct VertexData { float x, y, z, u, v; };
  const VertexData verts[4] = {{16, 8, 0, 0, 0}, {48, 8, 0, 1, 0}, {16, 40, 0, 0, 1}, {48, 40, 0, 1, 1}};
  assert(!m.write_data(Scene::vertices, verts, sizeof(verts)));
  const uint8_t vcolor[4][4] = {{255, 255, 255, 255}, {255, 255, 255, 255}, {255, 255, 255, 255}, {255, 255, 255, 255}};
  assert(!m.write_data(Scene::colors, vcolor, sizeof(vcolor)));
  // texture_vv computes pos = M * (x, y, z, 1) with M rows in s[0:15]
  // (s12..s15 the translation): pixel -> NDC orthographic projection.
  const float mat[16] = {1 / 64.0f, 0, 0, 0, 0, -1 / 32.0f, 0, 0, 0, 0, 1, 0, -1, 1, 0.5f, 1};
  assert(!m.write_data(Scene::matrix, mat, sizeof(mat)));
  const uint32_t vsharps[8] = {uint32_t(Scene::vertices), 20u << 16, 4, 0xfac | (7u << 12) | (13u << 15), uint32_t(Scene::colors), 4u << 16, 4, 0xfac | (10u << 15)};
  assert(!m.write_data(Scene::table, vsharps, sizeof(vsharps)));
  const uint16_t idx[6] = {0, 1, 2, 1, 3, 2};
  assert(!m.write_data(Scene::indices, idx, sizeof(idx)));
  for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) { const uint8_t t[4] = {uint8_t(60 * x), uint8_t(60 * y), 128, 255}; assert(!m.write_data(Scene::texture + (uint64_t(y) * 64 + x) * 4, t, 4)); }
  std::vector<uint8_t> zero(128 * 64 * 4, 0);
  assert(!m.write_data(Scene::target, zero.data(), zero.size()));
  s.common();
  auto& c = s.engine.state.context; auto& sh = s.engine.state.shader;
  sh[0x48] = uint32_t(Scene::vs >> 8); sh[0x4a] = 0x002c0083; sh[0x4b] = 0x10; // title RSRC1/RSRC2
  sh[0x4c] = uint32_t(Scene::fetch); sh[0x4e] = uint32_t(Scene::table);
  sh[0x50] = uint32_t(Scene::matrix); sh[0x51] = 16u << 16; sh[0x52] = 4; sh[0x53] = 0x20077fac;
  sh[0x8] = uint32_t(Scene::ps >> 8); sh[0xa] = 0x002c0041; sh[0xb] = 0x18;
  const uint32_t tsharp[8] = {uint32_t(Scene::texture >> 8), (10u << 20) | (0u << 26), 3 | (3u << 14), 0xfac | (8u << 20) | (9u << 28), 63u << 13, 0, 0, 0};
  for (unsigned k = 0; k < 8; ++k) sh[0xc + k] = tsharp[k];
  c[0x1b1] = 2; c[0x1c5] = 4; c[0x1b3] = 2; c[0x1b4] = 2; c[0x1b6] = 1; c[0x191] = 1; c[0x205] = 0; c[0x31c] = (10 << 2) | (1 << 11);
  s.engine.state.uconfig[0x242] = 4;
  std::string error;
  Words draw{0xc0042700U, 6, uint32_t(Scene::indices), 0, 6, 0};
  s.engine.remaining = 65536;
  if (!s.engine.execute(m, Scene::base, draw, error)) { std::cerr << error << "\n"; assert(false); }
  assert(s.engine.renderer.last.pixelsWritten == 32 * 32);
  for (uint32_t y = 8; y < 40; ++y) for (uint32_t x = 16; x < 48; ++x) {
    const auto p = pixelAt(m, Scene::target, x, y);
    assert(p[2] == 60 * ((x - 16) / 8) && p[1] == 60 * ((y - 8) / 8) && p[0] == 128 && p[3] == 255);
  }
  std::cout << "title shaders: texture_vv.sb (" << vsCode.size() << " words) + texture_p.sb (" << psCode.size() << " words) rendered "
            << s.engine.renderer.last.pixelsWritten << " px; VS ops " << s.engine.renderer.lastVertexHistogram << "; PS ops " << s.engine.renderer.lastPixelHistogram << "\n";
}

// Full-screen 1920x1080 textured, alpha-blended quad with the title's own
// shaders (its draw 2): wall time, and an output hash that every renderer
// optimisation must reproduce bit for bit.
static void titleFullscreenBench() {
  const char* dir = std::getenv("MAXPS4_KERO_DIR");
  if (!dir) return;
  const Words vsCode = sbProgram(std::string(dir) + "/texture_vv.sb"), psCode = sbProgram(std::string(dir) + "/texture_p.sb");
  if (vsCode.empty() || psCode.empty()) return;
  Fixture fx;
  auto m = fx.owner.lease();
  using namespace enc;
  Scene s;
  assert(!m.write_data(Scene::vs, vsCode.data(), vsCode.size() * 4));
  assert(!m.write_data(Scene::ps, psCode.data(), psCode.size() * 4));
  Words fetchCode;
  add(fetchCode, {{smrd(2, 8, 2, 0), smrd(2, 12, 2, 4), sopp(12, 0x7f)}, mubuf(2, 0, 4, 8, 128, true), mubuf(3, 0, 8, 12, 128, true),
                  mtbuf(1, 11, 7, 0, 12, 8, 128, 12), {sopp(12, 0xf70), sop1(32, 0, 0)}});
  assert(!m.write_data(Scene::fetch, fetchCode.data(), fetchCode.size() * 4));
  struct VertexData { float x, y, z, u, v; };
  const VertexData verts[4] = {{0, 0, 0, 0, 0}, {1920, 0, 0, 1, 0}, {0, 1080, 0, 0, 1}, {1920, 1080, 0, 1, 1}};
  assert(!m.write_data(Scene::vertices, verts, sizeof(verts)));
  const uint8_t vcolor[4][4] = {{255, 255, 255, 255}, {255, 200, 255, 255}, {255, 255, 100, 255}, {255, 255, 255, 128}};
  assert(!m.write_data(Scene::colors, vcolor, sizeof(vcolor)));
  const float mat[16] = {2 / 1920.0f, 0, 0, 0, 0, -2 / 1080.0f, 0, 0, 0, 0, 1, 0, -1, 1, 0.5f, 1};
  assert(!m.write_data(Scene::matrix, mat, sizeof(mat)));
  const uint32_t vsharps[8] = {uint32_t(Scene::vertices), 20u << 16, 4, 0xfac | (7u << 12) | (13u << 15), uint32_t(Scene::colors), 4u << 16, 4, 0xfac | (10u << 15)};
  assert(!m.write_data(Scene::table, vsharps, sizeof(vsharps)));
  const uint16_t idx[6] = {0, 1, 2, 1, 3, 2};
  assert(!m.write_data(Scene::indices, idx, sizeof(idx)));
  std::vector<uint8_t> tex(256 * 256 * 4);
  for (size_t i = 0; i < tex.size(); ++i) tex[i] = uint8_t((i * 2654435761u) >> 13);
  assert(!m.write_data(Scene::texture, tex.data(), tex.size()));
  std::vector<uint8_t> background(1920 * 1088 * 4);
  for (size_t i = 0; i < background.size(); ++i) background[i] = uint8_t(i * 7 + (i >> 11));
  for (size_t at = 0; at < background.size(); at += 1 << 20)
    assert(!m.write_data(Scene::target + at, background.data() + at, std::min<size_t>(1 << 20, background.size() - at)));
  s.common();
  auto& c = s.engine.state.context; auto& sh = s.engine.state.shader;
  c[0x10f] = f(960); c[0x110] = f(960); c[0x111] = f(-540); c[0x112] = f(540);
  c[0x0d] = (1080u << 16) | 1920u; // screen scissor
  c[0x319] = (1920 / 8 - 1) | ((1088 / 8 - 1) << 20); c[0x31a] = 1920 * 1088 / 64 - 1;
  sh[0x48] = uint32_t(Scene::vs >> 8); sh[0x4a] = 0x002c0083; sh[0x4b] = 0x10;
  sh[0x4c] = uint32_t(Scene::fetch); sh[0x4e] = uint32_t(Scene::table);
  sh[0x50] = uint32_t(Scene::matrix); sh[0x51] = 16u << 16; sh[0x52] = 4; sh[0x53] = 0x20077fac;
  sh[0x8] = uint32_t(Scene::ps >> 8); sh[0xa] = 0x002c0041; sh[0xb] = 0x18;
  // 256x256 RGBA8 sRGB, linear-aligned, bilinear clamp sampler.
  const uint32_t tsharp[8] = {uint32_t(Scene::texture >> 8), (10u << 20) | (9u << 26), 255 | (255u << 14), 0xfac | (8u << 20) | (9u << 28), 255u << 13, 0, 0, 0};
  for (unsigned k = 0; k < 8; ++k) sh[0xc + k] = tsharp[k];
  const uint32_t ssharp[4] = {2 | (2u << 3) | (2u << 6), 0, (1u << 20) | (1u << 22), 0};
  for (unsigned k = 0; k < 4; ++k) sh[0x14 + k] = ssharp[k];
  c[0x1b1] = 2; c[0x1c5] = 4; c[0x1b3] = 2; c[0x1b4] = 2; c[0x1b6] = 1; c[0x191] = 1; c[0x205] = 0;
  c[0x31c] = (10 << 2) | (6 << 8) | (1 << 11); // RGBA8 sRGB tiled target, blending enabled
  c[0x1e0] = 0x40000504; // SRC_ALPHA / ONE_MINUS_SRC_ALPHA, as the title
  s.engine.state.uconfig[0x242] = 4;
  std::string error;
  Words draw{0xc0042700U, 6, uint32_t(Scene::indices), 0, 6, 0};
  s.engine.remaining = 65536;
  const auto started = std::chrono::steady_clock::now();
  if (!s.engine.execute(m, Scene::base, draw, error)) { std::cerr << error << "\n"; assert(false); }
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
  std::vector<uint8_t> out(background.size());
  for (size_t at = 0; at < out.size(); at += 1 << 20)
    assert(!m.read_data(Scene::target + at, out.data() + at, std::min<size_t>(1 << 20, out.size() - at)));
  uint64_t hash = 1469598103934665603ULL; for (uint8_t b : out) hash = (hash ^ b) * 1099511628211ULL;
  std::fprintf(stderr, "title full-screen draw: %llu px written, %.1f ms, fnv64=%016llx\n", (unsigned long long)s.engine.renderer.last.pixelsWritten, ms, (unsigned long long)hash);
  assert(s.engine.renderer.last.pixelsWritten == 1920ull * 1080);
  if (const char* golden = std::getenv("MAXPS4_BENCH_GOLDEN")) assert(hash == std::strtoull(golden, nullptr, 16));
}

int main() {
  titleFullscreenBench();
  if (std::getenv("MAXPS4_BENCH_ONLY")) return 0;
  tilingTests();
  conversionTests();
  interpreterTests();
  pipelineTests();
  titleShaderTests();
  std::cout << "PASS display 2D-thin tiling bijection, FP16/sRGB conversion, GCN wave interpreter, fetch-shader call, rect/indexed draws, culling, texture sampling, sRGB blending and explicit refusals\n";
}
