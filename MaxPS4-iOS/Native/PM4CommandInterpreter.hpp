#pragma once
#include "PM4Type3Decoder.hpp"
#include <cstdint>
#include <vector>

namespace maxps4::gpu {
// A small, deliberately strict subset of GCN PM4 command semantics.
// Register writes are retained as GPU state; no Metal calls are issued here.
constexpr std::uint8_t kNop = 0x10;
constexpr std::uint8_t kSetContextReg = 0x69;
constexpr std::uint8_t kSetShReg = 0x76;
constexpr std::uint8_t kSetUconfigReg = 0x79;
constexpr std::uint8_t kDrawIndexAuto = 0x2d;
constexpr std::uint32_t kContextRegCount = 0x400;
enum class RegisterBank { context, shader, uconfig };
struct ContextWrite {
    std::uint32_t offset;
    std::uint32_t value;
    RegisterBank bank = RegisterBank::context;
};
// writesBefore is the number of register writes preceding this draw. It preserves
// write/draw ordering without copying a register bank for every draw.
struct DrawAuto {
    std::uint32_t indexCount;
    std::uint32_t initiator;
    std::size_t writesBefore;
};
enum class PM4CommandError { none, unsupported_flags, unsupported_opcode, invalid_register, invalid_draw };
struct PM4CommandStream {
    PM4Error framingError = PM4Error::none;
    bool valid = true;
    PM4CommandError commandError = PM4CommandError::none;
    std::vector<ContextWrite> writes;
    std::vector<DrawAuto> draws;
};
inline PM4CommandStream interpretPM4(const std::uint32_t* words, std::size_t wordCount) {
    PM4CommandStream out;
    const auto parsed = decodePM4Type3(words, wordCount);
    if (!parsed) { out.valid = false; out.framingError = parsed.error; return out; }
    for (const auto& packet : parsed.packets) {
        const auto* data = words + packet.payloadWord;
        // Predicated/compute/reserved flags require semantics this graphics subset
        // does not implement. Do not execute a conditional packet unconditionally.
        if (words[packet.headerWord] & 0xffU) {
            out.valid = false; out.commandError = PM4CommandError::unsupported_flags; break;
        }
        if (packet.opcode == kNop) continue; // Explicit PM4 no-op, arbitrary payload.
        if (packet.opcode == kSetContextReg || packet.opcode == kSetShReg ||
            packet.opcode == kSetUconfigReg) {
            if (packet.payloadCount < 2) {
                out.valid = false; out.commandError = PM4CommandError::invalid_register; break;
            }
            const std::uint32_t start = data[0];
            const std::size_t values = packet.payloadCount - 1;
            if (start >= kContextRegCount || values > kContextRegCount - start) {
                out.valid = false; out.commandError = PM4CommandError::invalid_register; break;
            }
            const auto bank = packet.opcode == kSetContextReg ? RegisterBank::context :
                packet.opcode == kSetShReg ? RegisterBank::shader : RegisterBank::uconfig;
            for (std::size_t i = 0; i < values; ++i)
                out.writes.push_back({start + static_cast<std::uint32_t>(i), data[i + 1], bank});
        } else if (packet.opcode == kDrawIndexAuto) {
            if (packet.payloadCount != 2 || data[0] == 0 || data[0] > 65536) {
                out.valid = false; out.commandError = PM4CommandError::invalid_draw; break;
            }
            out.draws.push_back({data[0], data[1], out.writes.size()});
        } else {
            out.valid = false; out.commandError = PM4CommandError::unsupported_opcode; break;
        }
    }
    if (!out.valid) { out.writes.clear(); out.draws.clear(); }
    return out;
}
} // namespace maxps4::gpu

