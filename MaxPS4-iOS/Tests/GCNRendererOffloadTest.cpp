// GCNRenderer with a pixel backend against GCNRenderer alone: the same draws
// rendered twice, once with every fragment shaded by the GCNShader
// interpreter and once offloaded to the translated shader (compiled at run
// time as C++ by GCNCppJitBackend, the text a Metal backend compiles); the
// render targets must be identical byte for byte. Covers a full-screen clear
// (32_ABGR export), a textured, interpolated, alpha-blended triangle with
// partial quads (FP16 export, bilinear sRGB texture), the same shader saving
// EXEC into VCC, and one whose VCC is overwritten before EXEC is restored
// from it, which the translator refuses: that draw must fall back.
#include "../Native/GCNRenderer.hpp"
#include "../Native/GNMCommandExecutor.hpp"
#include "../Native/GuestProviders/GnmEmbeddedVS.hpp"
#include "GCNTranslatorFixtures.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#ifdef __OBJC__   // built as Objective-C++ on macOS: the Metal backend instead of the C++ JIT
#include "../Native/GCNMetalPixelBackend.hpp"
#else
#include "GCNCppJitBackend.hpp"
#endif

using namespace maxps4::gpu;
using namespace gcnfix;

struct TestMemory {
  uint64_t base; std::vector<uint8_t> bytes;
  bool permitted(uint64_t at, size_t n, uint32_t) const { return at >= base && at - base + n <= bytes.size(); }
  int read_data(uint64_t at, void* out, size_t n, bool = false) const {
    if (!permitted(at, n, 0)) return -1;
    std::memcpy(out, bytes.data() + (at - base), n); return 0;
  }
  int write_data(uint64_t at, const void* in, size_t n) {
    if (!permitted(at, n, 0)) return -1;
    std::memcpy(bytes.data() + (at - base), in, n); return 0;
  }
};

static uint32_t fbits(float v) { uint32_t b; std::memcpy(&b, &v, 4); return b; }
constexpr uint64_t base = 0x400000, vsAddr = base + 0x400, psAddr = base + 0x800, constants = base + 0x1500,
                   texture = base + 0x10000, target = base + 0x100000;

struct Frame { std::vector<uint8_t> pixels; GCNRenderer::Stats stats; std::string refusal; };

enum class Shader { clear, textured, vccSave, refused };

static Words vertexShader() {
  // Embedded full-screen VS position code (v1 = x, v0 = y in {-1, 1}), scaled
  // by 0.9, then param0 = (u, v, u * v, 1) with u = 0.5x + 0.5, v = 0.5y + 0.5.
  Words c(gnm_embedded::FullscreenVS.begin(), gnm_embedded::FullscreenVS.begin() + 11);
  c.push_back(vop2(8, 1, 255, 1)); c.push_back(fbits(0.9f));
  c.push_back(vop2(8, 0, 255, 0)); c.push_back(fbits(0.9f));
  add(c, {vop3(321, 4, V + 1, 240, 240), vop3(321, 5, V + 0, 240, 240)});
  c.push_back(vop2(8, 6, V + 4, 5));
  add(c, {exp(12, 15, false, true, false, 1, 0, 2, 3), exp(32, 15, false, false, false, 4, 5, 6, 3)});
  c.push_back(sopp(1));
  return c;
}

static Words pixelShader(Shader kind) {
  Words c;
  if (kind == Shader::clear) { // constants from the V# in s[0:3]
    c = {smrd(10, 4, 0, 0), sopp(12, 0x7f), vop1(1, 0, 4), vop1(1, 1, 5), vop1(1, 2, 6), vop1(1, 3, 7)};
    add(c, {exp(0, 15, false, true, true, 0, 1, 2, 3)});
  } else {
    // T# s[0:7], S# s[8:11]; EXEC saved in s[20:21] or in VCC. "refused"
    // overwrites VCC with a compare (u < 0.5) before restoring EXEC from it.
    const unsigned save = kind == Shader::textured ? 20 : VCC;
    c = {sop1(4, save, EXEC), sop1(10, EXEC, EXEC),
         vintrp(0, 2, 0, 0, 0), vintrp(1, 2, 0, 0, 1), vintrp(0, 3, 0, 1, 0), vintrp(1, 3, 0, 1, 1)};
    add(c, {mimg(32, 15, 2, 4, 0, 8)});
    if (kind == Shader::refused) c.push_back(vopc(1, 240, 2));   // v_cmp_lt_f32 vcc, 0.5, v2
    c.push_back(sop1(4, EXEC, save)); c.push_back(sopp(12, 0x70));
    c.push_back(vintrp(0, 8, 0, 2, 0)); c.push_back(vintrp(1, 8, 0, 2, 1));
    c.push_back(vop2(8, 4, V + 4, 8));                      // tint red by u * v
    c.push_back(vop2(47, 0, V + 4, 5)); c.push_back(vop2(47, 1, V + 6, 7));
    add(c, {exp(0, 15, true, true, true, 0, 1, 0, 0)});
  }
  c.push_back(sopp(1));
  return c;
}

static Frame render(Shader kind, PixelBackend* backend) {
  TestMemory m{base, std::vector<uint8_t>(2 * 1024 * 1024)};
  const Words vs = vertexShader(), ps = pixelShader(kind);
  assert(!m.write_data(vsAddr, vs.data(), vs.size() * 4));
  assert(!m.write_data(psAddr, ps.data(), ps.size() * 4));
  const float clear[4] = {0.25f, 0.5f, 0.75f, 1.0f};
  assert(!m.write_data(constants, clear, 16));
  std::vector<uint8_t> tex(64 * 64 * 4);
  for (size_t k = 0; k < tex.size(); ++k) tex[k] = uint8_t((k * 2654435761u) >> 11);
  assert(!m.write_data(texture, tex.data(), tex.size()));
  std::vector<uint8_t> background(128 * 64 * 4);
  for (size_t k = 0; k < background.size(); ++k) background[k] = uint8_t(k * 7 + 3);
  assert(!m.write_data(target, background.data(), background.size()));

  GCNRenderer::Bank ctx{}, sh{}, uc{};
  GNMCommandExecutor::clearContext(ctx);
  ctx[0x10f] = fbits(64); ctx[0x110] = fbits(64); ctx[0x111] = fbits(-32); ctx[0x112] = fbits(32); ctx[0x113] = fbits(0.5f); ctx[0x114] = fbits(0.5f);
  ctx[0x206] = 0x43f; ctx[0x2f9] = 0x2d; ctx[0x0c] = 0; ctx[0x0d] = (64u << 16) | 128u;
  ctx[0x318] = uint32_t(target >> 8); ctx[0x319] = 128 / 8 - 1; ctx[0x31a] = 128 * 64 / 64 - 1;
  ctx[0x31c] = (10 << 2) | (6 << 8) | (1 << 11) | (1 << 15); ctx[0x31d] = 10 | (10 << 5);
  ctx[0x202] = 0x00cc0010; ctx[0x8e] = 0xf; ctx[0x8f] = 0xf; ctx[0x1c3] = 4;
  ctx[0x1b1] = 0;                                   // one VS parameter
  ctx[0x1b3] = 2; ctx[0x1b4] = 2;                   // perspective centre barycentrics
  ctx[0x205] = 0;                                   // no culling
  sh[0x48] = uint32_t(vsAddr >> 8); sh[0x4a] = 0x000c0001; sh[0x4b] = 0;
  sh[0x8] = uint32_t(psAddr >> 8);
  sh[0xa] = 0x002c0000 | (2u << 6) | 3u;            // 16 VGPRs, 24 SGPRs, FLOAT_MODE 0xc0 (fp32 denormals flushed)
  GCNRenderer::DrawCall call; call.opcode = 0x2d; call.initiator = 2; call.count = 3;
  if (kind == Shader::clear) {
    sh[0xb] = 4 << 1;
    sh[0xc] = uint32_t(constants); sh[0xd] = 0x00100000; sh[0xe] = 1; sh[0xf] = 0x20077fac;
    ctx[0x1c5] = 9; ctx[0x1b6] = 0;
    uc[0x242] = 0x11;                               // rect list: full screen
  } else {
    sh[0xb] = 12 << 1;
    const uint32_t tsharp[8] = {uint32_t(texture >> 8), (10u << 20) | (9u << 26), 63 | (63u << 14), 0xfac | (8u << 20) | (9u << 28), 63u << 13, 0, 0, 0};
    const uint32_t ssharp[4] = {2 | (2u << 3), 0, (1u << 20) | (1u << 22), 0};   // clamp, bilinear
    for (unsigned k = 0; k < 8; ++k) sh[0xc + k] = tsharp[k];
    for (unsigned k = 0; k < 4; ++k) sh[0x14 + k] = ssharp[k];
    ctx[0x1c5] = 4; ctx[0x1b6] = 1; ctx[0x191] = 0;
    ctx[0x1e0] = 0x40000504;                        // SRC_ALPHA, ONE_MINUS_SRC_ALPHA
    uc[0x242] = 4;                                  // one triangle: diagonal edge, partial quads
  }
  GCNRenderer renderer; renderer.verbose = false; renderer.pixelBackend = backend; renderer.maxThreads = 3;
  std::string error;
  if (!renderer.draw(m, ctx, sh, uc, call, error)) { std::fprintf(stderr, "draw: %s\n", error.c_str()); std::exit(1); }
  Frame f; f.stats = renderer.last; f.refusal = renderer.lastOffloadRefusal;
  f.pixels.assign(m.bytes.begin() + (target - base), m.bytes.begin() + (target - base) + 128 * 64 * 4);
  return f;
}

int main(int argc, char** argv) {
  if (argc != 3) { std::fprintf(stderr, "usage: GCNRendererOffloadTest NATIVE_DIR WORK_DIR\n"); return 2; }
#ifdef __OBJC__
  // The host provider (GCNMetalPixelOffloadHost.mm), as an app or runner gets it.
  setenv("MAXPS4_GPU_PRELUDE_DIR", argv[1], 1);
  auto* host = static_cast<GCNMetalPixelBackend*>(hostPixelBackend());
  if (!host) { std::printf("SKIP renderer pixel offload on Metal: no Metal backend on this machine (nothing compared)\n"); return 0; }
  GCNMetalPixelBackend& jit = *host;
  const char* backendName = "Metal";
#else
  GCNCppJitBackend jit(argv[1], argv[2]);
  const char* backendName = "C++ JIT";
#endif
  unsigned offloadedPixels = 0;
  const struct { Shader kind; const char* name; bool offloads; } cases[] = {
    {Shader::clear, "clear", true}, {Shader::textured, "textured", true}, {Shader::vccSave, "vcc-save", true},
    {Shader::refused, "refused", false}};
  for (const auto& c : cases) {
    const Frame reference = render(c.kind, nullptr), offloaded = render(c.kind, &jit);
    if (bool(offloaded.stats.offloadedDraws) != c.offloads) {
      std::fprintf(stderr, "%s: offload %s (%s)\n", c.name, offloaded.stats.offloadedDraws ? "taken" : "refused", offloaded.refusal.c_str());
      return 1;
    }
    if (!c.offloads && offloaded.refusal.find("VCC that no longer holds it") == std::string::npos) {
      std::fprintf(stderr, "%s: unexpected refusal reason: %s\n", c.name, offloaded.refusal.c_str()); return 1;
    }
    if (reference.stats.pixelsWritten == 0 || reference.stats.pixelsWritten != offloaded.stats.pixelsWritten) {
      std::fprintf(stderr, "%s: pixels written %llu vs %llu\n", c.name, (unsigned long long)reference.stats.pixelsWritten,
                   (unsigned long long)offloaded.stats.pixelsWritten); return 1;
    }
    size_t differing = 0, first = 0;
    for (size_t k = 0; k < reference.pixels.size(); ++k) if (reference.pixels[k] != offloaded.pixels[k]) { if (!differing++) first = k; }
    if (differing) {
      std::fprintf(stderr, "%s: %zu bytes differ, first at %zu: interpreter %02x, offload %02x\n", c.name, differing, first,
                   reference.pixels[first], offloaded.pixels[first]); return 1;
    }
    std::printf("%s: %llu pixels, %s, identical to the interpreter%s%s\n", c.name, (unsigned long long)reference.stats.pixelsWritten,
                c.offloads ? "offloaded" : "kept on the interpreter", c.offloads ? "" : " (", c.offloads ? "" : (offloaded.refusal + ")").c_str());
    if (c.offloads) offloadedPixels += unsigned(offloaded.stats.offloadFragments);
  }
  std::printf("PASS renderer pixel offload (%s): %u fragments shaded by translated code, render targets identical to the interpreter (%u shaders compiled)\n",
              backendName, offloadedPixels, jit.compiled);
}
