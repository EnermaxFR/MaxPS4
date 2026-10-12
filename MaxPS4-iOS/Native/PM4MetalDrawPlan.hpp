#pragma once
#include "PM4CommandInterpreter.hpp"

namespace maxps4::gpu {
enum class PM4MetalError : int32_t {
    none, invalid_stream, missing_encoder, missing_pipeline, unsupported_draw,
    unsupported_state, allocation_failure
};
struct PM4MetalPlan {
    PM4MetalError error = PM4MetalError::none;
    size_t drawCount = 0;
};
// A caller-provided native pipeline cannot implement arbitrary guest registers.
inline PM4MetalPlan planPM4Triangles(const uint32_t* words, size_t count) {
    const auto commands = interpretPM4(words, count);
    if (!commands.valid) return {PM4MetalError::invalid_stream, 0};
    if (!commands.writes.empty()) return {PM4MetalError::unsupported_state, 0};
    if (commands.draws.empty()) return {PM4MetalError::unsupported_draw, 0};
    for (const auto& draw : commands.draws) {
        // DRAW_INITIATOR SOURCE_SELECT=2 is auto-index. Other bits must be zero.
        if (draw.indexCount != 3 || draw.initiator != 2)
            return {PM4MetalError::unsupported_draw, 0};
    }
    return {PM4MetalError::none, commands.draws.size()};
}
}
