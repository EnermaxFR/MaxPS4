#pragma once
#include "PM4CommandInterpreter.hpp"
#include <cerrno>
#include <new>

namespace maxps4::gpu {
constexpr size_t kMaxSubmissionWords = 65536;
struct PM4GuestSubmission {
    int error = 0;
    std::vector<uint32_t> words;
};
// Memory must implement translate(address, byteCount, ELF PF_R). The caller
// synchronizes guest execution/writes while taking this owned snapshot.
// Never cast a guest address to a host pointer or expose arena lifetime to Metal.
template<class Memory>
PM4GuestSubmission snapshotPM4(const Memory& memory, uint64_t address, size_t count) {
    if (!count || count > kMaxSubmissionWords || (address & 3U) ||
        address > UINT64_MAX - count * 4) return {-EINVAL, {}};
    const auto* bytes = memory.translate(address, count * 4, 4);
    if (!bytes) return {-EFAULT, {}};
    try {
        PM4GuestSubmission out;
        out.words.resize(count);
        for (size_t i = 0; i < count; ++i) {
            out.words[i] = uint32_t(bytes[i * 4]) | uint32_t(bytes[i * 4 + 1]) << 8 |
                uint32_t(bytes[i * 4 + 2]) << 16 | uint32_t(bytes[i * 4 + 3]) << 24;
        }
        if (!interpretPM4(out.words.data(), count).valid) return {-ENOTSUP, {}};
        return out;
    } catch (const std::bad_alloc&) { return {-ENOMEM, {}}; }
}
}
