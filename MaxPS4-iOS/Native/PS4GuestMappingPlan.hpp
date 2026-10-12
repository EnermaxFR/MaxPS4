#pragma once
// Strict preflight for the PS4 segment staging -> FEX guest memory boundary.
// No host memory is mapped here; the caller must implement transactional mapping.
#include "PS4ExecutableImage.hpp"
#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <limits>
#include <vector>

namespace maxps4 {
struct PS4GuestMapping {
    std::uint64_t guestAddress;
    std::uint64_t length;
    std::uint32_t permissions;
    const std::uint8_t* bytes;
};
struct PS4GuestMappingPlan {
    int error = 0;
    std::uint64_t entry = 0;
    std::vector<PS4GuestMapping> mappings;
    explicit operator bool() const { return error == 0; }
};

// Reject unsupported dynamic imports/TLS before any guest-memory mutations.
// The bytes pointers are borrowed from image; keep image alive until copying ends.
inline PS4GuestMappingPlan planPS4GuestMappings(const PS4ExecutableImage& image) {
    PS4GuestMappingPlan plan;
    if (image.segments.empty() || image.type != 0xfe00 || image.requiresLinker || image.requiresTLS) {
        plan.error = -ENOTSUP;
        return plan;
    }
    bool entryExecutable = false;
    for (const auto& segment : image.segments) {
        const auto& h = segment.header;
        if (!h.memorySize || h.memorySize != segment.bytes.size() ||
            h.address > std::numeric_limits<std::uint64_t>::max() - h.memorySize ||
            (h.flags & ~7U) || !(h.flags & 4U)) {
            plan.error = -ENOEXEC;
            plan.mappings.clear();
            return plan;
        }
        for (const auto& old : plan.mappings) {
            if (h.address < old.guestAddress + old.length &&
                old.guestAddress < h.address + h.memorySize) {
                plan.error = -ENOEXEC;
                plan.mappings.clear();
                return plan;
            }
        }
        if ((h.flags & 1U) && image.entry >= h.address &&
            image.entry - h.address < h.fileSize) entryExecutable = true;
        plan.mappings.push_back({h.address, h.memorySize, h.flags, segment.bytes.data()});
    }
    if (!entryExecutable) {
        plan.error = -ENOEXEC;
        plan.mappings.clear();
        return plan;
    }
    plan.entry = image.entry;
    return plan;
}
} // namespace maxps4
