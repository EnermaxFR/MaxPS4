#import "PM4MetalRenderer.h"
#include "GCNTranslator.hpp"
#include "PM4ResourceScan.hpp"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <vector>

namespace maxps4::gpu {
namespace {
// Must match GCNDrawState in gcnMetalPrelude().
struct DrawState { std::uint64_t base, size; std::uint32_t vsUser[16], psUser[17]; std::uint32_t flags, rangeCount; std::uint64_t ranges[48]; };
static_assert(sizeof(DrawState) == 544, "GCNDrawState layout");
constexpr std::uint32_t rectList = 1, flipY = 2;

// CB_BLEND factor codes to Metal (ZERO..ONE_MINUS_DST_COLOR); others refused.
bool blendFactor(unsigned code, MTLBlendFactor& out) {
    static const MTLBlendFactor factors[] = {
        MTLBlendFactorZero, MTLBlendFactorOne, MTLBlendFactorSourceColor, MTLBlendFactorOneMinusSourceColor,
        MTLBlendFactorSourceAlpha, MTLBlendFactorOneMinusSourceAlpha, MTLBlendFactorDestinationAlpha,
        MTLBlendFactorOneMinusDestinationAlpha, MTLBlendFactorDestinationColor, MTLBlendFactorOneMinusDestinationColor};
    if (code >= sizeof(factors) / sizeof(factors[0])) return false;
    out = factors[code];
    return true;
}
std::string inputKey(const PM4Draw& d) {
    std::string key(reinterpret_cast<const char*>(d.inputCntl), sizeof d.inputCntl);
    return key;
}
}

id<MTLRenderPipelineState> PM4MetalRenderer::pipeline(const PM4Draw& d, MTLPixelFormat format) {
    const PipelineKey key{d.vs, d.ps, d.vsUser[0], d.vsUser[1], d.psInputAddr, inputKey(d), unsigned(format),
                          (d.blend >> 30) & 1 ? d.blend : 0u, d.mask};
    const auto found = pipelines_.find(key);
    if (found != pipelines_.end()) return found->second;
    GCNTranslateOptions vsOptions;
    vsOptions.stage = GCNStage::vertex; vsOptions.name = "vs_program"; vsOptions.userCount = d.vsUserCount;
    for (unsigned k = 0; k < 16; ++k) vsOptions.userData[k] = d.vsUser[k];
    GCNTranslateOptions psOptions;
    psOptions.name = "ps_program"; psOptions.userCount = d.psUserCount;
    for (unsigned k = 0; k < 16; ++k) psOptions.userData[k] = d.psUser[k];
    const auto vs = translateGCN(code_, d.vs, vsOptions);
    if (!vs) { fail("VS translation: " + vs.error); return nil; }
    const auto ps = translateGCN(code_, d.ps, psOptions);
    if (!ps) { fail("PS translation: " + ps.error); return nil; }
    const std::string source = gcnMetalPrelude() + vs.source + ps.source +
                               gcnMetalEntryPoints(vs, ps, "vs_program", "ps_program", d.psInputAddr, d.inputCntl);
    MTLCompileOptions* options = [MTLCompileOptions new];
    if (@available(macOS 15.0, iOS 18.0, *)) options.mathMode = MTLMathModeSafe;
    else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        options.fastMathEnabled = NO;
#pragma clang diagnostic pop
    }
    NSError* failure = nil;
    id<MTLLibrary> library = [device_ newLibraryWithSource:[NSString stringWithUTF8String:source.c_str()] options:options error:&failure];
    if (!library) { fail(std::string("MSL: ") + failure.localizedDescription.UTF8String); return nil; }
    MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.vertexFunction = [library newFunctionWithName:@"gcn_vertex"];
    descriptor.fragmentFunction = [library newFunctionWithName:@"gcn_fragment"];
    MTLRenderPipelineColorAttachmentDescriptor* color = descriptor.colorAttachments[0];
    color.pixelFormat = format;
    color.writeMask = ((d.mask & 1) ? MTLColorWriteMaskRed : 0) | ((d.mask & 2) ? MTLColorWriteMaskGreen : 0) |
                      ((d.mask & 4) ? MTLColorWriteMaskBlue : 0) | ((d.mask & 8) ? MTLColorWriteMaskAlpha : 0);
    if ((d.blend >> 30) & 1) {
        const bool separate = (d.blend >> 29) & 1;
        const unsigned colorFn = (d.blend >> 5) & 7, alphaFn = separate ? (d.blend >> 21) & 7 : colorFn;
        MTLBlendFactor cs, cd, as, ad;
        if (colorFn != 0 || alphaFn != 0 || !blendFactor(d.blend & 31, cs) || !blendFactor((d.blend >> 8) & 31, cd) ||
            !blendFactor(separate ? (d.blend >> 16) & 31 : d.blend & 31, as) ||
            !blendFactor(separate ? (d.blend >> 24) & 31 : (d.blend >> 8) & 31, ad)) {
            fail("blend mode");
            return nil;
        }
        color.blendingEnabled = YES;
        color.rgbBlendOperation = MTLBlendOperationAdd; color.alphaBlendOperation = MTLBlendOperationAdd;
        color.sourceRGBBlendFactor = cs; color.destinationRGBBlendFactor = cd;
        color.sourceAlphaBlendFactor = as; color.destinationAlphaBlendFactor = ad;
    }
    id<MTLRenderPipelineState> state = [device_ newRenderPipelineStateWithDescriptor:descriptor error:&failure];
    if (!state) { fail(std::string("pipeline: ") + failure.localizedDescription.UTF8String); return nil; }
    ++pipelinesBuilt;
    pipelines_[key] = state;
    return state;
}

bool PM4MetalRenderer::encode(const PM4Draw& d, id<MTLCommandBuffer> commands) {
    error.clear();
    if (d.swap > 1) return fail("colour target component swap " + std::to_string(d.swap));
    if ((d.vte & 0x3f) != 0x3f) return fail("viewport without scale and offset");
    const MTLPixelFormat format = d.swap == 1 ? MTLPixelFormatBGRA8Unorm : MTLPixelFormatRGBA8Unorm;
    if (d.scissor[2] <= d.scissor[0] || d.scissor[3] <= d.scissor[1]) return true; // nothing visible
    id<MTLRenderPipelineState> state = pipeline(d, format);
    if (!state) return false;
    DrawState drawState{};
    id<MTLBuffer> memory = guest_;
    if (memory) { drawState.base = guestBase_; drawState.size = memory.length; }
    else {
        // Copies of the ranges this draw's programs read, packed 256-byte aligned.
        const auto ranges = scanDrawResources(code_, d);
        if (ranges.size() > 16) return fail("more than 16 memory ranges");
        std::uint64_t total = 0;
        for (const auto& r : ranges) total += (r.size + 255) & ~std::uint64_t{255};
        if (total > (256ull << 20)) return fail("draw reads more than 256 MiB");
        memory = [device_ newBufferWithLength:std::max<std::uint64_t>(total, 256) options:MTLResourceStorageModeShared];
        if (!memory) return fail("memory buffer");
        std::uint64_t offset = 0;
        auto* bytes = static_cast<std::uint8_t*>(memory.contents);
        for (std::size_t k = 0; k < ranges.size(); ++k) {
            // An unreadable range stays zero rather than exposing anything else.
            if (!code_.read(ranges[k].base, bytes + offset, ranges[k].size)) std::memset(bytes + offset, 0, ranges[k].size);
            drawState.ranges[3 * k] = ranges[k].base;
            drawState.ranges[3 * k + 1] = ranges[k].size;
            drawState.ranges[3 * k + 2] = offset;
            offset += (ranges[k].size + 255) & ~std::uint64_t{255};
        }
        drawState.rangeCount = std::uint32_t(ranges.size());
        lastRanges = drawState.rangeCount;
    }
    Target& target = targets_[d.target];
    if (!target.texture || target.texture.width != d.pitch || target.texture.height != d.height ||
        target.texture.pixelFormat != format) {
        MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                                                              width:d.pitch height:d.height mipmapped:NO];
        descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        descriptor.storageMode = MTLStorageModeShared;
        target = {[device_ newTextureWithDescriptor:descriptor], true};
        if (!target.texture) return fail("colour target texture");
    }
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = target.texture;
    pass.colorAttachments[0].loadAction = target.fresh ? MTLLoadActionClear : MTLLoadActionLoad;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    target.fresh = false;
    id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
    [encoder setRenderPipelineState:state];
    // PA_CL_VPORT: x = xscale * ndc + xoffset. A positive Y scale points NDC +1 down: flip it.
    const float xs = d.viewport[0], xo = d.viewport[1], ys = d.viewport[2], yo = d.viewport[3];
    MTLViewport viewport;
    viewport.originX = xo - xs; viewport.width = 2 * xs;
    viewport.originY = yo - std::fabs(ys); viewport.height = 2 * std::fabs(ys);
    viewport.znear = d.viewport[5]; viewport.zfar = d.viewport[5] + d.viewport[4];
    [encoder setViewport:viewport];
    [encoder setScissorRect:MTLScissorRect{d.scissor[0], d.scissor[1], d.scissor[2] - d.scissor[0], d.scissor[3] - d.scissor[1]}];
    [encoder setCullMode:MTLCullModeNone];
    for (unsigned k = 0; k < 16; ++k) { drawState.vsUser[k] = d.vsUser[k]; drawState.psUser[k] = d.psUser[k]; }
    drawState.flags = (d.primitive == 0x11 ? rectList : 0) | (ys > 0 ? flipY : 0);
    [encoder setVertexBuffer:memory offset:0 atIndex:0];
    [encoder setFragmentBuffer:memory offset:0 atIndex:0];
    [encoder setVertexBytes:&drawState length:sizeof drawState atIndex:1];
    [encoder setFragmentBytes:&drawState length:sizeof drawState atIndex:1];
    const std::size_t bytes = d.indices.size() * sizeof(std::uint32_t);
    if (bytes <= 4096) [encoder setVertexBytes:d.indices.data() length:bytes atIndex:2];
    else [encoder setVertexBuffer:[device_ newBufferWithBytes:d.indices.data() length:bytes options:MTLResourceStorageModeShared]
                           offset:0 atIndex:2];
    const NSUInteger count = d.primitive == 0x11 ? NSUInteger(d.indices.size() / 3 * 6) : NSUInteger(d.indices.size());
    [encoder drawPrimitives:d.primitive == 6 ? MTLPrimitiveTypeTriangleStrip : MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:count];
    [encoder endEncoding];
    return true;
}

id<MTLTexture> PM4MetalRenderer::target(std::uint64_t address) const {
    const auto found = targets_.find(address);
    return found == targets_.end() ? nil : found->second.texture;
}
} // namespace maxps4::gpu
