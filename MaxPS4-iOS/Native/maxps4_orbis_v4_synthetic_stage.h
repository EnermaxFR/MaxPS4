#pragma once
#include "maxps4_guest_address_space.h"
#include <cerrno>
#include <cstddef>
#include <cstdint>

namespace maxps4 {
// Bounded synthetic code/stack staging, not a general ELF/PRX loader.
// Uses translated memory only; FEX direct admission remains explicitly gated.
inline int stage_v4_synthetic_image(GuestAddressSpace& space,
                                    uint64_t code_address, const uint8_t* code,
                                    size_t code_size, uint64_t stack_address,
                                    size_t stack_size) {
    const size_t page = space.page_size();
    if (!page || !code || !code_size || !stack_size ||
        code_size > page || stack_size != page ||
        code_address % page || stack_address % page ||
        code_address == stack_address ||
        page > UINT64_MAX / 2 || code_address > UINT64_MAX - 2 * page)
        return -EINVAL;
    if (space.fex_direct()) return -ENOTSUP;
    // Reserve a single two-page region only when code and stack are adjacent.
    // Check arithmetic bounds before computing any guest address.
    if (stack_address != code_address + page) return -EINVAL;
    const int reserved = space.reserve(code_address, 2 * page);
    if (reserved) return reserved;
    const int mapped_code = space.map(code_address,page,5,code,code_size);
    if (mapped_code) { space.release(code_address); return mapped_code; }
    const int mapped_stack = space.map(stack_address,page,6);
    if (mapped_stack) { space.release(code_address); return mapped_stack; }
    return 0;
}
}
