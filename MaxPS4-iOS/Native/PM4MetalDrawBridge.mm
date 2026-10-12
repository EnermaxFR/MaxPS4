#import "PM4MetalDrawBridge.h"
#include "PM4MetalDrawPlan.hpp"
#include <new>

extern "C" int32_t maxps4_pm4_encode_triangle(const uint32_t* words, size_t count,
    id<MTLRenderCommandEncoder> encoder, id<MTLRenderPipelineState> pipeline,
    size_t* drawn) {
    using namespace maxps4::gpu;
    if (drawn) *drawn = 0;
    if (!drawn) return static_cast<int32_t>(PM4MetalError::invalid_stream);
    try {
        const auto plan = planPM4Triangles(words, count);
        if (plan.error != PM4MetalError::none) return static_cast<int32_t>(plan.error);
        if (!encoder) return static_cast<int32_t>(PM4MetalError::missing_encoder);
        if (!pipeline) return static_cast<int32_t>(PM4MetalError::missing_pipeline);
        [encoder setRenderPipelineState:pipeline];
        for (size_t i = 0; i < plan.drawCount; ++i)
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        *drawn = plan.drawCount;
        return 0;
    } catch (const std::bad_alloc&) {
        return static_cast<int32_t>(PM4MetalError::allocation_failure);
    }
}

