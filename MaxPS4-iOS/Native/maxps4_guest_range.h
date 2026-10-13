#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
namespace maxps4 {
// Pure guest-range validation. Never returns a host address.
inline bool guest_range_permitted(uint64_t address, size_t length, uint32_t requested,
                                  uint64_t segment_address, uint64_t segment_size,
                                  uint32_t segment_permissions) {
    if (!length || !requested || (requested & ~7U) ||
        (segment_permissions & requested) != requested ||
        address < segment_address || segment_size > UINT64_MAX - segment_address)
        return false;
    // size_t may be wider than 64 bits on a future host: never truncate
    // the requested transfer length before checking the guest interval.
    if constexpr (sizeof(size_t) > sizeof(uint64_t)) {
        if (length > UINT64_MAX) return false;
    }
    const uint64_t relative = address - segment_address;
    return relative < segment_size &&
           static_cast<uint64_t>(length) <= segment_size - relative;
}
}
