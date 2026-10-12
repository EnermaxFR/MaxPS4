#import <Metal/Metal.h>
#include "PM4CommandInterpreter.hpp"

// Experimental bridge: translates a validated PM4 DRAW_INDEX_AUTO into
// a Metal triangle draw. The caller owns the active render pass, pipeline,
// vertex buffers and all shader state. This does NOT emulate PS4 shaders.
namespace maxps4::gpu {
enum class PM4MetalError {
    none, invalid_stream, missing_encoder, missing_pipeline, unsupported_draw
};
struct PM4MetalResult {
    PM4MetalError error = PM4MetalError::none;
    std::size_t drawCount = 0;
};

// Only a three-vertex, non-indexed draw is supported in this first bridge.
// Never issue partial GPU work for malformed or unsupported command streams.
inline PM4MetalResult encodePM4TriangleDraws(
    const std::uint32_t* words, std::size_t wordCount,
    id<MTLRenderCommandEncoder> encoder, id<MTLRenderPipelineState> pipeline) {
    PM4MetalResult result;
    const auto commands = interpretPM4(words, wordCount);
    if (!commands.valid) { result.error = PM4MetalError::invalid_stream; return result; }
    if (commands.draws.empty()) {
        result.error = PM4MetalError::unsupported_draw;
        return result;
    }
    for (const auto& draw : commands.draws) {
        if (draw.indexCount != 3 || draw.initiator != 0) {
            result.error = PM4MetalError::unsupported_draw;
            return result;
        }
    }
    if (!encoder) { result.error = PM4MetalError::missing_encoder; return result; }
    if (!pipeline) { result.error = PM4MetalError::missing_pipeline; return result; }
    [encoder setRenderPipelineState:pipeline];
    for (const auto& draw : commands.draws) {
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:draw.indexCount];
        ++result.drawCount;
    }
    return result;
}
} // namespace maxps4::gpu
