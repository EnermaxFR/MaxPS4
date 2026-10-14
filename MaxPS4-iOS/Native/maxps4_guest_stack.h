#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cerrno>
namespace maxps4 {
// Minimal bounded process ABI: argc=0, argv/envp terminators, AT_PAGESZ,
// AT_ENTRY and AT_NULL. No host strings, pointers or environment are exposed.
// The entry is the rebased CPU address used by the restricted FEX backend.
inline int initialize_guest_stack(void* memory, size_t size, uint64_t entry,
                                  size_t page_size, uint64_t& rsp) {
    rsp = 0;
    constexpr size_t bytes = 9 * sizeof(uint64_t);
    const auto base = reinterpret_cast<uintptr_t>(memory);
    if (!memory || size < bytes + 15 || !entry || !page_size ||
        (page_size & (page_size - 1)) || size > UINTPTR_MAX - base) return -EINVAL;
    const uint64_t words[] = {0, 0, 0, 6, page_size, 9, entry, 0};
    const auto start = (base + size - bytes) & ~uintptr_t(15);
    std::memcpy(reinterpret_cast<void*>(start), words, sizeof(words));
    const uint64_t zero = 0;
    std::memcpy(reinterpret_cast<void*>(start + sizeof(words)), &zero, sizeof(zero));
    rsp = start;
    return 0;
}
}
