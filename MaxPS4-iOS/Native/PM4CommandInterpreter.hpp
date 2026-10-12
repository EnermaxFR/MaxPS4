#pragma once
#include "PM4Type3Decoder.hpp"
#include <cstdint>
#include <vector>

namespace maxps4::gpu {
// A small, deliberately strict subset of GCN PM4 command semantics.
// Register writes are retained as GPU state; no Metal calls are issued here.
constexpr std::uint8_t kSetContextReg = 0x69;
constexpr std::uint8_t kDrawIndexAuto = 0x2d;
constexpr std::uint32_t kContextRegCount = 0x400;
struct ContextWrite { std::uint32_t offset; std::uint32_t value; };
struct DrawAuto { std::uint32_t indexCount; std::uint32_t initiator; };
struct PM4CommandStream {
    PM4Error framingError = PM4Error::none;
    bool valid = true;
    std::vector<ContextWrite> writes;
    std::vector<DrawAuto> draws;
};
inline PM4CommandStream interpretPM4(const std::uint32_t* words, std::size_t wordCount) {
    PM4CommandStream out;
    const auto parsed = decodePM4Type3(words, wordCount);
    if (!parsed) { out.valid = false; out.framingError = parsed.error; return out; }
    for (const auto& packet : parsed.packets) {
        const auto* data = words + packet.payloadWord;
        if (packet.opcode == kSetContextReg) {
            if (packet.payloadCount < 2) { out.valid = false; break; }
            const std::uint32_t start = data[0];
            const std::size_t values = packet.payloadCount - 1;
            if (start >= kContextRegCount || values > kContextRegCount - start) {
                out.valid = false; break;
            }
            for (std::size_t i = 0; i < values; ++i)
                out.writes.push_back({start + static_cast<std::uint32_t>(i), data[i + 1]});
        } else if (packet.opcode == kDrawIndexAuto) {
            if (packet.payloadCount != 2 || data[0] == 0 || data[0] > 65536) {
                out.valid = false; break;
            }
            out.draws.push_back({data[0], data[1]});
        } else {
            out.valid = false; break; // no silently ignored GPU operations
        }
    }
    if (!out.valid) { out.writes.clear(); out.draws.clear(); }
    return out;
}
} // namespace maxps4::gpu
