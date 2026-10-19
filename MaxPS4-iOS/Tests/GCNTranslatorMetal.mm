// Metal stage of the translator test (macOS CI). The very same generated text
// as the C++ twin (itself proven equal to the GCNShader interpreter) is
// compiled as Metal Shading Language and executed on the GPU:
// * every vector ALU opcode of the prelude on special and random operands
//   (denormals included), GPU against C++: differences are classified as
//   denormal-related, within 4 ulp, or other; only "other" fails;
// * the image sampling cases of GCNImageCases.hpp through gcn_image, one
//   thread per (case, lane), compared bit for bit with the C++ gcn_image;
// * the translated pixel shaders, one thread per fragment, compared with the
//   C++ twin (bit for bit, or within 4 ulp of the same sign for Metal's
//   transcendental functions), the textured one sampling a real texture.
// Compiled with fast math disabled.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "../Native/GCNKernel.h"
#include "../Native/GCNImageBinding.hpp"
#include "../Native/GCNShaderTranslator.hpp"
#include "GCNImageCases.hpp"
#include "GCNTranslatorFixtures.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace maxps4::gpu;

struct TwinEnv { const float* attr; std::vector<gcn_u32> out; const GcnImage* images; const unsigned char* texels; };
#define GCN_UNIFORM
#define GCN_ENV_PARAM TwinEnv& env
#define GCN_ENV_INTERP(a, c, k) env.attr[((a) * 4 + (c)) * 3 + (k)]
#define GCN_ENV_SAMPLE(k, w0, w1, c, t) ((void)(w1), gcn_image(env.images[k], env.texels, w0, c, t))
#define GCN_ENV_EXPORT(tg, en, cp, dn, e) \
  do { env.out.push_back(tg); for (int q = 0; q < 4; ++q) env.out.push_back((e)[q]); } while (0)
#include GCN_TRANSLATED
#undef GCN_UNIFORM
#undef GCN_ENV_PARAM
#undef GCN_ENV_INTERP
#undef GCN_ENV_SAMPLE
#undef GCN_ENV_EXPORT

static std::string slurp(const char* path) { std::ifstream f(path); std::stringstream s; s << f.rdbuf(); return s.str(); }

template <class T> static id<MTLBuffer> buffer(id<MTLDevice> device, const std::vector<T>& v) {
  if (v.empty()) return [device newBufferWithLength:16 options:MTLResourceStorageModeShared];
  return [device newBufferWithBytes:v.data() length:v.size() * sizeof(T) options:MTLResourceStorageModeShared];
}
static bool dispatch(id<MTLDevice> device, id<MTLLibrary> library, const std::string& name, NSArray<id<MTLBuffer>>* buffers, unsigned threads) {
  NSError* error = nil;
  id<MTLFunction> fn = [library newFunctionWithName:[NSString stringWithUTF8String:name.c_str()]];
  id<MTLComputePipelineState> pipe = fn ? [device newComputePipelineStateWithFunction:fn error:&error] : nil;
  if (!pipe) { std::fprintf(stderr, "%s: pipeline: %s\n", name.c_str(), error ? error.localizedDescription.UTF8String : "no function"); return false; }
  id<MTLCommandQueue> queue = [device newCommandQueue];
  id<MTLCommandBuffer> cb = [queue commandBuffer];
  id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
  [enc setComputePipelineState:pipe];
  for (NSUInteger k = 0; k < buffers.count; ++k) [enc setBuffer:buffers[k] offset:0 atIndex:k];
  [enc dispatchThreads:MTLSizeMake(threads, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
  [enc endEncoding]; [cb commit]; [cb waitUntilCompleted];
  if (cb.error) { std::fprintf(stderr, "%s: %s\n", name.c_str(), cb.error.localizedDescription.UTF8String); return false; }
  return true;
}

int main(int argc, char** argv) {
  @autoreleasepool {
    if (argc != 4 && argc != 5) {
      std::fprintf(stderr, "usage: GCNTranslatorMetal GCNKernel.h GCNKernelImage.h translated.inc [EMIT.metal]\n"); return 2;
    }
    constexpr unsigned lanes = 4096, vgprs = 16, attrs = 8 * 4 * 3, outs = 5 * 4;
    const std::string kernels = R"MSL(
struct GcnEnv { device const float* attr; device gcn_u32* out; uint count; constant GcnImage* images; device const unsigned char* texels; };
#define GCN_UNIFORM constant
#define GCN_ENV_PARAM thread GcnEnv& env
#define GCN_ENV_INTERP(a, c, k) env.attr[((a) * 4 + (c)) * 3 + (k)]
#define GCN_ENV_SAMPLE(k, w0, w1, c, t) ((void)(w1), gcn_image(env.images[k], env.texels, w0, c, t))
#define GCN_ENV_EXPORT(tg, en, cp, dn, e) \
  do { env.out[env.count++] = (tg); for (int q = 0; q < 4; ++q) env.out[env.count++] = (e)[q]; } while (0)
kernel void run_valu_cases(device const gcn_u32* in [[buffer(0)]], device gcn_u32* out [[buffer(1)]], uint id [[thread_position_in_grid]]) {
  bool carry = in[id * 5u + 4u] != 0u;
  out[id * 2u] = gcn_valu(in[id * 5u], in[id * 5u + 1u], in[id * 5u + 2u], in[id * 5u + 3u], carry);
  out[id * 2u + 1u] = carry ? 1u : 0u;
}
kernel void run_image_cases(device const GcnImage* images [[buffer(0)]], device const unsigned char* texels [[buffer(1)]],
                            device const gcn_u32* w0 [[buffer(2)]], device const gcn_u32* coords [[buffer(3)]],
                            device gcn_u32* out [[buffer(4)]], uint id [[thread_position_in_grid]]) {
  const uint n = id / 64u;
  gcn_u32 c[4] = {coords[id * 2u], coords[id * 2u + 1u], 0u, 0u};
  gcn_u32 t[4];
  gcn_image(images[n], texels, w0[n], c, t);
  for (uint k = 0; k < 4u; ++k) out[id * 4u + k] = t[k];
}
)MSL";
    std::string source = slurp(argv[1]) + slurp(argv[2]) + kernels + slurp(argv[3]);
    const char* names[] = {"ps_clear", "ps_alu", "ps_textured"};
    for (const char* n : names) {
      source += std::string("kernel void run_") + n + "(constant gcn_u32* s [[buffer(0)]], device const gcn_u32* vin [[buffer(1)]],"
                " device const float* attr [[buffer(2)]], device gcn_u32* out [[buffer(3)]],"
                " constant GcnImage* images [[buffer(4)]], device const unsigned char* texels [[buffer(5)]], uint lane [[thread_position_in_grid]]) {\n"
                "  gcn_u32 v[16]; for (uint r = 0; r < 16; ++r) v[r] = vin[lane * 16 + r];\n"
                "  GcnEnv env; env.attr = attr + lane * " + std::to_string(attrs) + "; env.out = out + lane * " + std::to_string(outs) +
                "; env.count = 0; env.images = images; env.texels = texels;\n"
                "  " + n + "(s, v, env);\n}\n";
    }
    if (argc == 5) {   // the exact source, for the offline compiler (xcrun metal), which needs no GPU
      std::ofstream(argv[4]) << source;
      std::printf("MSL source written: %zu bytes\n", source.size());
      return 0;
    }
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) { NSArray<id<MTLDevice>>* all = MTLCopyAllDevices(); if (all.count) device = all[0]; }
    if (!device) {
      // Without a Metal device nothing is executed: reported as a skip, never as a pass.
      std::printf("SKIP translator Metal execution: no Metal device on this machine (GPU results NOT compared)\n");
      return 0;
    }
    std::printf("Metal device: %s\n", device.name.UTF8String);
    MTLCompileOptions* options = [MTLCompileOptions new];
    // fastMathEnabled is deprecated on newer SDKs in favour of mathMode, which the
    // macos-14 runner SDK lacks; both select IEEE-conformant math here.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    options.fastMathEnabled = NO;
#pragma clang diagnostic pop
    NSError* error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:[NSString stringWithUTF8String:source.c_str()] options:options error:&error];
    if (!library) { std::fprintf(stderr, "MSL compilation failed:\n%s\n", error.localizedDescription.UTF8String); return 1; }
    std::printf("MSL: %zu bytes compiled (image sampler, 3 translated pixel shaders)\n", source.size());

    // 0. Vector ALU opcodes: GPU gcn_valu against C++ gcn_valu.
    {
      const std::vector<gcn_u32> specials = {0u, 0x80000000u, 0x3f800000u, 0xbf800000u, 0x7f800000u, 0xff800000u, 0x7fc00000u,
                                             0xffc00001u, 0x00000001u, 0x807fffffu, 0x00800000u, 0x7f7fffffu, 0x4f800000u, 0xcf000000u,
                                             0x3effffffu, 0x3f000000u, 0x477fe000u, 0x477ff000u, 0x33800000u, 0x387fc000u, 1u, 31u, 32u,
                                             0xffffffffu, 0x7fffffffu, 0x12345678u, 255u, 256u, 0x00400000u, 0x80000123u};
      uint64_t st = 0x6d61787073343231ull;
      auto rnd = [&] { st = st * 6364136223846793005ull + 1442695040888963407ull; return gcn_u32(st >> 32); };
      auto operand = [&]() -> gcn_u32 {
        const gcn_u32 kind = rnd() % 8;
        if (kind == 0) return specials[rnd() % specials.size()];
        if (kind == 1) return gcn_b(float(gcn_i32(rnd() % 2001) - 1000) / 8.0f);
        if (kind == 2) return rnd() % 64;
        if (kind == 3) return (rnd() & 0x807fffffu) | ((rnd() % 3u) << 23);   // denormals and the smallest normals
        return rnd();
      };
      std::vector<gcn_u32> in;
      constexpr unsigned perOp = 16384;
      for (gcn_u32 op = 256; op < 512; ++op) {
        if (!gcn_valu_supported(op)) continue;
        for (unsigned n = 0; n < perOp; ++n) { in.push_back(op); in.push_back(operand()); in.push_back(operand()); in.push_back(operand()); in.push_back(rnd() & 1); }
      }
      const unsigned threads = unsigned(in.size() / 5);
      id<MTLBuffer> out = [device newBufferWithLength:size_t(threads) * 8 options:MTLResourceStorageModeShared];
      if (!dispatch(device, library, "run_valu_cases", @[buffer(device, in), out], threads)) return 1;
      const gcn_u32* gpu = static_cast<const gcn_u32*>(out.contents);
      auto denormal = [](gcn_u32 v) { return (v & 0x7f800000u) == 0 && (v & 0x007fffffu) != 0; };
      struct Count { unsigned exact = 0, denorm = 0, ulp = 0, other = 0, worst = 0; gcn_u32 sample[4] = {}; };
      std::map<gcn_u32, Count> perOpCount;
      unsigned other = 0;
      for (unsigned k = 0; k < threads; ++k) {
        const gcn_u32 op = in[k * 5], a = in[k * 5 + 1], b = in[k * 5 + 2], c = in[k * 5 + 3];
        bool carry = in[k * 5 + 4] != 0;
        const gcn_u32 want = gcn_valu(op, a, b, c, carry), got = gpu[k * 2];
        Count& n = perOpCount[op];
        const bool fout = GCNShader::isFloatOutput(op);
        const bool minMax = op == 271 || op == 272 || op == 337 || op == 340 || op == 343;
        if ((want == got || (fout && gcn_isnan(gcn_f(want)) && gcn_isnan(gcn_f(got))) || (minMax && ((got | want) & 0x7fffffffu) == 0))
            && (gpu[k * 2 + 1] != 0) == carry) { ++n.exact; continue; }
        if (denormal(a) || denormal(b) || denormal(c) || denormal(want) || denormal(got)) { ++n.denorm; continue; }
        const unsigned ulp = unsigned(std::abs(int64_t(int32_t(want)) - int64_t(int32_t(got))));
        if (fout && ulp <= 4 && ((want ^ got) & 0x80000000u) == 0 && (gpu[k * 2 + 1] != 0) == carry) { ++n.ulp; n.worst = std::max(n.worst, ulp); continue; }
        if (!n.other) { n.sample[0] = a; n.sample[1] = b; n.sample[2] = c; n.sample[3] = got; }
        ++n.other; ++other;
        if (other <= 12) std::fprintf(stderr, "valu op %u a=%08x b=%08x c=%08x: GPU %08x/%u, C++ %08x/%d\n", op, a, b, c, got, gpu[k * 2 + 1], want, int(carry));
      }
      unsigned exact = 0, denorm = 0, close = 0;
      for (const auto& [op, n] : perOpCount) {
        exact += n.exact; denorm += n.denorm; close += n.ulp;
        if (n.denorm || n.ulp || n.other)
          std::printf("  valu op %u: %u identical, %u differ with denormal operands/results, %u within %u ulp, %u other\n",
                      op, n.exact, n.denorm, n.ulp, n.worst, n.other);
      }
      if (other) { std::fprintf(stderr, "FAIL vector ALU on the GPU: %u results differ beyond denormals and 4 ulp\n", other); return 1; }
      std::printf("PASS vector ALU on the GPU: %zu ops x %u cases, %u identical, %u denormal-related, %u within 4 ulp\n",
                  perOpCount.size(), perOp, exact, denorm, close);
    }

    // 1. Image sampling cases: GPU gcn_image against C++ gcn_image.
    {
      const gcnimg::Suite suite = gcnimg::make(6000, 0x9e3779b97f4a7c15ull);
      std::vector<GcnImage> images; std::vector<gcn_u32> w0s, coords; std::vector<size_t> origin;
      for (size_t n = 0; n < suite.cases.size(); ++n) {
        const auto& c = suite.cases[n];
        GCNImageBinding binding; std::string why;
        if (!binding.describe(c.t, c.smp, ((c.w0 >> 18) & 127) != 0, why)) continue;
        binding.image.base = uint32_t(binding.guestBase - suite.base);
        images.push_back(binding.image); w0s.push_back(c.w0); origin.push_back(n);
        for (const auto& xy : c.coords) { coords.push_back(xy[0]); coords.push_back(xy[1]); }
      }
      const unsigned threads = unsigned(images.size() * 64);
      id<MTLBuffer> out = [device newBufferWithLength:size_t(threads) * 16 options:MTLResourceStorageModeShared];
      if (!dispatch(device, library, "run_image_cases", @[buffer(device, images), buffer(device, suite.bytes), buffer(device, w0s), buffer(device, coords), out], threads)) return 1;
      const gcn_u32* gpu = static_cast<const gcn_u32*>(out.contents);
      unsigned equal = 0, different = 0;
      std::map<std::string, unsigned> classes;
      for (size_t k = 0; k < images.size(); ++k) {
        const GcnImage& img = images[k];
        for (unsigned lane = 0; lane < 64; ++lane) {
          const gcn_u32 a[4] = {coords[(k * 64 + lane) * 2], coords[(k * 64 + lane) * 2 + 1], 0u, 0u};
          gcn_u32 t[4];
          gcn_image(img, suite.bytes.data(), w0s[k], a, t);
          for (unsigned ch = 0; ch < 4; ++ch) {
            const gcn_u32 want = t[ch], got = gpu[(k * 64 + lane) * 4 + ch];
            if (want == got || (gcn_isnan(gcn_f(want)) && gcn_isnan(gcn_f(got)))) { ++equal; continue; }
            const unsigned op = (w0s[k] >> 18) & 127;
            std::ostringstream key;
            key << "op " << op << " filter " << img.filter << " dfmt " << img.dfmt << " nfmt " << img.nfmt << " tile " << img.tile;
            if (different < 12)
              std::fprintf(stderr, "image case %zu lane %u channel %u (%s, clamp %u/%u, coords %08x %08x): GPU %08x, C++ %08x\n",
                           origin[k], lane, ch, key.str().c_str(), img.clampX, img.clampY, a[0], a[1], got, want);
            ++different; ++classes[key.str()];
          }
        }
      }
      if (different) {
        for (const auto& [key, n] : classes) std::fprintf(stderr, "  %u differences: %s\n", n, key.c_str());
        std::fprintf(stderr, "FAIL image sampling on the GPU: %u of %u channels differ from the C++ kernel\n", different, equal + different);
        return 1;
      }
      std::printf("PASS image sampling on the GPU: %u channels over %zu cases identical to the C++ kernel\n", equal, images.size());
    }

    // 2. Translated pixel shaders. Inputs as the C++ stage: barycentrics in v0/v1, other VGPRs zero.
    std::vector<gcn_u32> vin(lanes * vgprs, 0), s(104, 0);
    std::vector<float> attr(lanes * attrs);
    uint64_t state = 61;
    auto next = [&] { state = state * 6364136223846793005ull + 1442695040888963407ull; return uint32_t(state >> 33); };
    for (unsigned l = 0; l < lanes; ++l) {
      vin[l * vgprs] = gcn_b(float(int(next() % 3001) - 1000) / 1000.0f);
      vin[l * vgprs + 1] = gcn_b(float(int(next() % 3001) - 1000) / 1000.0f);
      for (unsigned k = 0; k < attrs; ++k) attr[l * attrs + k] = float(int(next() % 4001) - 2000) / 1000.0f;
    }
    const float consts[8] = {0.25f, -1.5f, 3.0f, 0.75f, 2.0f, 0.5f, 7.0f, 0.0f};
    for (unsigned k = 0; k < 8; ++k) s[4 + k] = gcn_b(consts[k]);
    s[6] = 0xfffffff0u;
    // Textured: 64x64 sRGB RGBA8 texture (stage 2's T#/S#); the texels buffer is the resource.
    constexpr uint64_t textureGuest = 0x420000;
    std::vector<unsigned char> texture(64 * 64 * 4);
    for (size_t k = 0; k < texture.size(); ++k) texture[k] = uint8_t((k * 2654435761u) >> 11);
    std::vector<gcn_u32> st(104, 0);
    const uint32_t tsharp[8] = {uint32_t(textureGuest >> 8), (10u << 20) | (9u << 26), 63 | (63u << 14), 0xfac | (8u << 20) | (9u << 28), 63u << 13, 0, 0, 0};
    const uint32_t ssharp[4] = {2 | (2u << 3) | (2u << 6), 0, (1u << 20) | (1u << 22), 0};
    for (unsigned k = 0; k < 8; ++k) st[8 + k] = tsharp[k];
    for (unsigned k = 0; k < 4; ++k) st[16 + k] = ssharp[k];

    using Fn = void (*)(const gcn_u32*, gcn_u32*, TwinEnv&);
    const std::map<std::string, Fn> twins{{"ps_clear", ps_clear}, {"ps_alu", ps_alu}, {"ps_textured", ps_textured}};
    unsigned exact = 0, close = 0, compared = 0;
    for (const auto& f : gcnfix::all()) {
      if (!f.translatable) continue;
      const auto twin = twins.at(f.name);
      const auto translation = GCNShaderTranslator::translate(f.code, f.vgprs);
      if (!translation.ok) { std::fprintf(stderr, "%s: %s\n", f.name.c_str(), translation.error.c_str()); return 1; }
      const std::vector<gcn_u32>& sv = translation.usesImages ? st : s;
      std::vector<GcnImage> images;
      for (const auto& image : translation.images) {
        GCNImageBinding binding; std::string why;
        if (!binding.describe(&sv[image.rsrc], &sv[image.ssamp], image.sampled, why)) { std::fprintf(stderr, "%s: binding: %s\n", f.name.c_str(), why.c_str()); return 1; }
        binding.image.base = uint32_t(binding.guestBase - textureGuest);
        images.push_back(binding.image);
      }
      id<MTLBuffer> bo = [device newBufferWithLength:lanes * outs * 4 options:MTLResourceStorageModeShared];
      if (!dispatch(device, library, "run_" + f.name,
                    @[buffer(device, sv), buffer(device, vin), buffer(device, attr), bo, buffer(device, images), buffer(device, texture)], lanes)) return 1;
      const gcn_u32* gpu = static_cast<const gcn_u32*>(bo.contents);
      unsigned shaderExact = 0, shaderClose = 0, worstUlp = 0;
      for (unsigned l = 0; l < lanes; ++l) {
        TwinEnv env{attr.data() + l * attrs, {}, images.data(), texture.data()};
        std::vector<gcn_u32> v(vin.begin() + l * vgprs, vin.begin() + (l + 1) * vgprs);
        twin(sv.data(), v.data(), env);
        for (size_t k = 0; k < env.out.size(); ++k) {
          const gcn_u32 want = env.out[k], got = gpu[l * outs + k];
          ++compared;
          if (want == got || (gcn_isnan(gcn_f(want)) && gcn_isnan(gcn_f(got)))) { ++shaderExact; continue; }
          const unsigned ulp = unsigned(std::abs(int64_t(int32_t(want)) - int64_t(int32_t(got))));
          if (ulp <= 4 && ((want ^ got) & 0x80000000u) == 0) { ++shaderClose; worstUlp = std::max(worstUlp, ulp); continue; }
          std::fprintf(stderr, "%s lane %u value %zu: GPU %08x, C++ %08x\n", f.name.c_str(), l, k, got, want);
          return 1;
        }
      }
      std::printf("%s: %u values identical, %u within %u ulp (Metal transcendental precision)%s\n", f.name.c_str(), shaderExact, shaderClose,
                  worstUlp, translation.usesImages ? ", texture sampled on the GPU" : "");
      exact += shaderExact; close += shaderClose;
    }
    std::printf("PASS translator Metal stage: %u values compared, %u identical, %u within 4 ulp\n", compared, exact, close);
  }
}
