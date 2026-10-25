// Research only: where a translated pixel program's GPU time goes. One
// full-screen 1920x1080 draw (PM4MetalRendererTest's textured program,
// u = x / 1920, v = y / 1080 over a 512x256 RGBA8 texture) rendered with
// variants of the generated MSL, each removing one part, next to an empty
// pass and trivial Metal fragments on the same device. Variants change
// pixels: only the timings matter. Never selects a game backend.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "../Native/GCNTranslator.hpp"
#include "GCNTestPrograms.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
using namespace maxps4::gpu;
using namespace gcn_test;
namespace {
struct State { uint64_t base, size; uint32_t vs[16], ps[17], flags, count; uint64_t ranges[48]; };
static_assert(sizeof(State) == 544, "GCNDrawState layout");
std::string replaced(std::string s, const std::string& from, const std::string& to) {
    std::size_t count = 0;
    for (std::size_t at = 0; (at = s.find(from, at)) != std::string::npos; at += to.size(), ++count) s.replace(at, from.size(), to);
    if (!count) std::printf("variant pattern not found: %s\n", from.c_str());
    assert(count);
    return s;
}
}
int main() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) { std::puts("SKIP shader cost: no Metal device"); return 0; }
        constexpr unsigned screenW = 1920, screenH = 1080, imageW = 512, imageH = 256;
        constexpr uint64_t texturedPS = 0x11400, image = 0x40000;
        Arena arena;
        arena.bytes.resize(image + imageW * imageH * 4 - Arena::start);
        loadFetchVS(arena);
        const float triangle[3][4] = {{-1, -1, 0, 1}, {3, -1, 0, 1}, {-1, 3, 0, 1}};
        for (unsigned n = 0; n < 3; ++n) for (unsigned c = 0; c < 4; ++c) arena.put(0x10e00 + 16 * n + 4 * c, bits(triangle[n][c]));
        arena.code(texturedPS, {
            0xbe94047e, 0xbefe0a7e, 0xc8280000, 0xc8290001, 0x100804ff, bits(1.0f / screenW), 0x100a06ff, bits(1.0f / screenH),
            0xf0800f00, 0x00400604, 0xd8d48055, 0x0b000004, 0xd8d48000, 0x0c000004, 0x0816190b,
            0x100e16ff, 0x41000000, 0x061014ff, 0x41200000, 0x101010ff, 0x3ccccccd, 0x7c0208f4, 0xbf860001,
            0x7e120280, 0xbefe0414, 0xf800180f, 0x09080706, 0xbf810000});
        for (unsigned n = 0; n < imageW * imageH; ++n) arena.put(image + 4 * n, n * 2654435761u);
        GCNTranslateOptions vsOptions; vsOptions.stage = GCNStage::vertex; vsOptions.name = "vs_program"; vsOptions.userCount = 4;
        vsOptions.userData[0] = std::uint32_t(fetchShader); vsOptions.userData[2] = std::uint32_t(vertexTable);
        GCNTranslateOptions psOptions; psOptions.name = "ps_program"; psOptions.userCount = 12;
        const auto vs = translateGCN(arena.memory, fetchVS, vsOptions);
        const auto ps = translateGCN(arena.memory, texturedPS, psOptions);
        assert(vs && ps);
        std::uint32_t inputCntl[32] = {};
        const std::string entry = gcnMetalEntryPoints(vs, ps, "vs_program", "ps_program", 0x302, inputCntl);
        const std::string trivial = R"MSL(
fragment float4 trivial_fragment(GCNVaryings in [[stage_in]]) { return float4(in.position.x / 1920.0f, in.position.y / 1080.0f, in.p0.x, 1.0f); }
fragment float4 metal_sample_fragment(GCNVaryings in [[stage_in]], texture2d<float> image [[texture(0)]]) {
    constexpr sampler linear(coord::normalized, address::repeat, filter::linear);
    return image.sample(linear, in.position.xy / float2(1920.0f, 1080.0f), level(0.0f));
}
)MSL";
        const std::string noSample = replaced(ps.source, "gcn_sample(M, t, sm, false, gf(v4), gf(v5))", "float4(gf(v4), gf(v5), 0.0f, 1.0f)");
        auto noQuad = [&](std::string s) {
            s = replaced(s, "gcn_quad_any(Q, L, ", "(");
            return replaced(s, "gcn_quad_shuffle(Q, L, ", "gcn_no_shuffle(");
        };
        const std::string noShuffle = "GCN_FN uint gcn_no_shuffle(uint value, uint lane) { return value + lane; }\n";
        const std::string noMask = replaced(replaced(entry, "uint sampleCoverage [[sample_mask]],", ""), "sampleCoverage != 0u", "true");
        const std::string entryOnly = replaced(entry, "    ps_program(L, M, Q);\n", "");
        const std::string noClear = replaced(entry, "GCNLane L; gcn_clear(L);\n    for (uint k = 0u; k < 17u",
            "GCNLane L; L.exec = true; L.killed = false; L.colorWritten = 0u; L.color[0] = float4(0.0f);\n    for (uint k = 0u; k < 17u");
        // attr/param/color arrays of one element: this program uses only index 0.
        auto smallLane = [&](std::string s) {
            s = replaced(s, "float4 attr[32];", "float4 attr[1];");
            s = replaced(s, "float4 param[32]; float4 color[8];", "float4 param[1]; float4 color[1];");
            s = replaced(s, "k < 32u; ++k) { L.attr[k]", "k < 1u; ++k) { L.attr[k]");
            return replaced(s, "k < 8u; ++k) L.color[k]", "k < 1u; ++k) L.color[k]");
        };
        struct Variant { const char* name; std::string source; const char* fragment; bool draws; bool smallLane = false; };
        std::vector<Variant> variants = {
            {"empty pass (clear only)", "", "", false},
            {"trivial Metal fragment", vs.source + ps.source + entry + trivial, "trivial_fragment", true},
            {"trivial + Metal bilinear sample", vs.source + ps.source + entry + trivial, "metal_sample_fragment", true},
            {"translated program", vs.source + ps.source + entry, "gcn_fragment", true},
            {"translated, no gcn_sample", vs.source + noSample + entry, "gcn_fragment", true},
            {"translated, no quad ops", noShuffle + vs.source + noQuad(ps.source) + entry, "gcn_fragment", true},
            {"translated, no sample/quad", noShuffle + vs.source + noQuad(noSample) + entry, "gcn_fragment", true},
            {"translated, no sample_mask input", vs.source + ps.source + noMask, "gcn_fragment", true},
            {"entry only (program not run)", vs.source + ps.source + entryOnly, "gcn_fragment", true},
            {"translated, no gcn_clear in fragment", vs.source + ps.source + noClear, "gcn_fragment", true},
            {"translated, small GCNLane", vs.source + ps.source + entry, "gcn_fragment", true, true},
            {"entry only, small GCNLane", vs.source + ps.source + entryOnly, "gcn_fragment", true, true},
        };
        MTLCompileOptions* options = [MTLCompileOptions new];
        if (@available(macOS 15.0, iOS 18.0, *)) options.mathMode = MTLMathModeSafe;
        std::vector<id<MTLRenderPipelineState>> pipelines(variants.size());
        for (std::size_t k = 0; k < variants.size(); ++k) {
            if (!variants[k].draws) continue;
            NSError* error = nil;
            const std::string source = (variants[k].smallLane ? smallLane(gcnMetalPrelude()) : gcnMetalPrelude()) + variants[k].source;
            id<MTLLibrary> library = [device newLibraryWithSource:[NSString stringWithUTF8String:source.c_str()] options:options error:&error];
            if (!library) std::printf("%s MSL: %s\n", variants[k].name, error.localizedDescription.UTF8String);
            assert(library);
            MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
            descriptor.vertexFunction = [library newFunctionWithName:@"gcn_vertex"];
            descriptor.fragmentFunction = [library newFunctionWithName:[NSString stringWithUTF8String:variants[k].fragment]];
            descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
            pipelines[k] = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
            if (!pipelines[k]) std::printf("%s pipeline: %s\n", variants[k].name, error.localizedDescription.UTF8String);
            assert(pipelines[k]);
        }
        id<MTLBuffer> memory = [device newBufferWithBytes:arena.bytes.data() length:arena.bytes.size() options:MTLResourceStorageModeShared];
        State state{}; state.base = Arena::start; state.size = arena.bytes.size();
        state.vs[0] = std::uint32_t(fetchShader); state.vs[2] = std::uint32_t(vertexTable);
        const uint32_t t[12] = {std::uint32_t(image >> 8), 10u << 20, (imageW - 1) | ((imageH - 1) << 14), 0x90800facu,
                                (imageW - 1) << 13, 0, 0, 0, 0, 0, 0x500000, 0};
        std::memcpy(state.ps, t, sizeof t);
        const uint32_t indices[3] = {0, 1, 2};
        auto* targetDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:screenW height:screenH mipmapped:NO];
        targetDescriptor.usage = MTLTextureUsageRenderTarget; targetDescriptor.storageMode = MTLStorageModePrivate;
        id<MTLTexture> target = [device newTextureWithDescriptor:targetDescriptor];
        auto* imageDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:imageW height:imageH mipmapped:NO];
        imageDescriptor.usage = MTLTextureUsageShaderRead;
        id<MTLTexture> metalImage = [device newTextureWithDescriptor:imageDescriptor];
        [metalImage replaceRegion:MTLRegionMake2D(0, 0, imageW, imageH) mipmapLevel:0
                        withBytes:arena.bytes.data() + (image - Arena::start) bytesPerRow:imageW * 4];
        id<MTLCommandQueue> queue = [device newCommandQueue];
        std::vector<double> total(variants.size()); std::vector<unsigned> timed(variants.size());
        constexpr unsigned runs = 14, warmup = 4;
        for (unsigned run = 0; run < runs; ++run) for (std::size_t turn = 0; turn < variants.size(); ++turn) {
            const std::size_t k = (turn + run) % variants.size(); // rotate the order between runs
            id<MTLCommandBuffer> command = [queue commandBuffer];
            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = target;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
            if (variants[k].draws) {
                [encoder setRenderPipelineState:pipelines[k]];
                [encoder setViewport:MTLViewport{0, 0, double(screenW), double(screenH), 0, 1}];
                [encoder setVertexBuffer:memory offset:0 atIndex:0]; [encoder setFragmentBuffer:memory offset:0 atIndex:0];
                [encoder setVertexBytes:&state length:sizeof state atIndex:1]; [encoder setFragmentBytes:&state length:sizeof state atIndex:1];
                [encoder setVertexBytes:indices length:sizeof indices atIndex:2];
                [encoder setFragmentTexture:metalImage atIndex:0];
                [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            }
            [encoder endEncoding]; [command commit]; [command waitUntilCompleted];
            assert(command.status == MTLCommandBufferStatusCompleted && !command.error);
            if (run >= warmup && command.GPUStartTime > 0) { total[k] += (command.GPUEndTime - command.GPUStartTime) * 1000; ++timed[k]; }
        }
        std::printf("SHADER COST on %s, %ux%u full-screen draw (GPU ms, mean of %u):\n", device.name.UTF8String, screenW, screenH, runs - warmup);
        for (std::size_t k = 0; k < variants.size(); ++k) {
            const double ms = timed[k] ? total[k] / timed[k] : -1;
            std::printf("SHADER COST %-36s %9.3f ms  %7.2f ns/pixel\n", variants[k].name, ms, ms * 1e6 / (screenW * screenH));
        }
        std::puts("PASS shader cost research (timings only, CI GPU, not iPhone game FPS)");
    }
}
