// Metal stage of the translator test (macOS CI): the translated pixel shaders
// are compiled as Metal Shading Language from the very same generated text as
// the C++ twin, executed on the GPU one thread per fragment, and compared with
// the C++ twin (itself proven equal to the GCNShader interpreter).
// Compiled with fast math disabled. Image sampling is not bound in this test:
// shaders that sample are compiled (syntax and types) but not executed.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "../Native/GCNKernel.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

struct TwinEnv { const float* attr; std::vector<gcn_u32> out; };
#define GCN_UNIFORM
#define GCN_ENV_PARAM TwinEnv& env
#define GCN_ENV_INTERP(a, c, k) env.attr[((a) * 4 + (c)) * 3 + (k)]
#define GCN_ENV_SAMPLE(w0, w1, c, t) ((void)(w0), (void)(w1), (void)(c), (void)(t))
#define GCN_ENV_EXPORT(tg, en, cp, dn, e) \
  do { env.out.push_back(tg); for (int q = 0; q < 4; ++q) env.out.push_back((e)[q]); } while (0)
#include GCN_TRANSLATED
#undef GCN_UNIFORM
#undef GCN_ENV_PARAM
#undef GCN_ENV_INTERP
#undef GCN_ENV_SAMPLE
#undef GCN_ENV_EXPORT

static std::string slurp(const char* path) { std::ifstream f(path); std::stringstream s; s << f.rdbuf(); return s.str(); }

int main(int argc, char** argv) {
  @autoreleasepool {
    if (argc != 3 && argc != 4) {
      std::fprintf(stderr, "usage: GCNTranslatorMetal GCNKernel.h translated.inc [EMIT.metal]\n"); return 2;
    }
    constexpr unsigned lanes = 4096, vgprs = 16, attrs = 8 * 4 * 3, outs = 5 * 4;
    const std::string kernels = R"MSL(
struct GcnEnv { device const float* attr; device gcn_u32* out; uint count; };
#define GCN_UNIFORM constant
#define GCN_ENV_PARAM thread GcnEnv& env
#define GCN_ENV_INTERP(a, c, k) env.attr[((a) * 4 + (c)) * 3 + (k)]
#define GCN_ENV_SAMPLE(w0, w1, c, t) ((void)(w0), (void)(w1), (void)(c), (void)(t))
#define GCN_ENV_EXPORT(tg, en, cp, dn, e) \
  do { env.out[env.count++] = (tg); for (int q = 0; q < 4; ++q) env.out[env.count++] = (e)[q]; } while (0)
)MSL";
    std::string source = slurp(argv[1]) + kernels + slurp(argv[2]);
    const char* names[] = {"ps_clear", "ps_alu", "ps_textured"};
    for (const char* n : names) {
      source += std::string("kernel void run_") + n + "(constant gcn_u32* s [[buffer(0)]], device const gcn_u32* vin [[buffer(1)]],"
                " device const float* attr [[buffer(2)]], device gcn_u32* out [[buffer(3)]], uint lane [[thread_position_in_grid]]) {\n"
                "  gcn_u32 v[16]; for (uint r = 0; r < 16; ++r) v[r] = vin[lane * 16 + r];\n"
                "  GcnEnv env; env.attr = attr + lane * " + std::to_string(attrs) + "; env.out = out + lane * " + std::to_string(outs) + "; env.count = 0;\n"
                "  " + n + "(s, v, env);\n}\n";
    }
    if (argc == 4) {   // the exact source, for the offline compiler (xcrun metal), which needs no GPU
      std::ofstream(argv[3]) << source;
      std::printf("MSL source written: %zu bytes\n", source.size());
      return 0;
    }
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) { NSArray<id<MTLDevice>>* all = MTLCopyAllDevices(); if (all.count) device = all[0]; }
    if (!device) {
      // Hosted macOS runners are virtual machines without a Metal device: nothing is
      // executed, and this is reported as a skip, never as a pass.
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
    std::printf("MSL: %zu bytes compiled (3 translated pixel shaders)\n", source.size());

    // Inputs as the C++ stage: barycentrics in v0/v1, other VGPRs zero.
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
    using Fn = void (*)(const gcn_u32*, gcn_u32*, TwinEnv&);
    const std::map<std::string, Fn> twins{{"ps_clear", ps_clear}, {"ps_alu", ps_alu}};
    unsigned exact = 0, close = 0, compared = 0;
    for (const auto& [name, twin] : twins) {
      id<MTLFunction> fn = [library newFunctionWithName:[NSString stringWithUTF8String:("run_" + name).c_str()]];
      id<MTLComputePipelineState> pipe = [device newComputePipelineStateWithFunction:fn error:&error];
      if (!pipe) { std::fprintf(stderr, "%s: pipeline: %s\n", name.c_str(), error.localizedDescription.UTF8String); return 1; }
      id<MTLBuffer> bs = [device newBufferWithBytes:s.data() length:s.size() * 4 options:MTLResourceStorageModeShared];
      id<MTLBuffer> bv = [device newBufferWithBytes:vin.data() length:vin.size() * 4 options:MTLResourceStorageModeShared];
      id<MTLBuffer> ba = [device newBufferWithBytes:attr.data() length:attr.size() * 4 options:MTLResourceStorageModeShared];
      id<MTLBuffer> bo = [device newBufferWithLength:lanes * outs * 4 options:MTLResourceStorageModeShared];
      id<MTLCommandQueue> queue = [device newCommandQueue];
      id<MTLCommandBuffer> cb = [queue commandBuffer];
      id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
      [enc setComputePipelineState:pipe];
      [enc setBuffer:bs offset:0 atIndex:0]; [enc setBuffer:bv offset:0 atIndex:1];
      [enc setBuffer:ba offset:0 atIndex:2]; [enc setBuffer:bo offset:0 atIndex:3];
      [enc dispatchThreads:MTLSizeMake(lanes, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
      [enc endEncoding]; [cb commit]; [cb waitUntilCompleted];
      const gcn_u32* gpu = static_cast<const gcn_u32*>(bo.contents);
      unsigned shaderExact = 0, shaderClose = 0, worstUlp = 0;
      for (unsigned l = 0; l < lanes; ++l) {
        TwinEnv env{attr.data() + l * attrs, {}};
        std::vector<gcn_u32> v(vin.begin() + l * vgprs, vin.begin() + (l + 1) * vgprs);
        twin(s.data(), v.data(), env);
        for (size_t k = 0; k < env.out.size(); ++k) {
          const gcn_u32 want = env.out[k], got = gpu[l * outs + k];
          ++compared;
          if (want == got || (gcn_isnan(gcn_f(want)) && gcn_isnan(gcn_f(got)))) { ++shaderExact; continue; }
          const unsigned ulp = unsigned(std::abs(int64_t(int32_t(want)) - int64_t(int32_t(got))));
          if (ulp <= 4 && ((want ^ got) & 0x80000000u) == 0) { ++shaderClose; worstUlp = std::max(worstUlp, ulp); continue; }
          std::fprintf(stderr, "%s lane %u value %zu: GPU %08x, C++ %08x\n", name.c_str(), l, k, got, want);
          return 1;
        }
      }
      std::printf("%s: %u values identical, %u within %u ulp (Metal transcendental precision)\n", name.c_str(), shaderExact, shaderClose, worstUlp);
      exact += shaderExact; close += shaderClose;
    }
    std::printf("PASS translator Metal stage: %u values compared, %u identical, %u within 4 ulp\n", compared, exact, close);
  }
}
