#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "maxps4_guest_elf.h"
namespace maxps4 {
// Non-owning checked access to a staged PS4 image. CPU address-space wiring
// must use this contract rather than exposing raw host pointers across the ABI.
class PS4GuestMemory final {
public:
    explicit PS4GuestMemory(GuestELF& image) : image_(image) {}
    bool read(uint64_t address, void* destination, size_t length) const {
        if (!destination || !length) return false;
        const auto* source = image_.translate(address, length, 4);
        if (!source) return false;
        std::memmove(destination, source, length);
        return true;
    }
    // Instruction fetch requires both guest read and execute permissions.
    // Copy into a caller-owned buffer; never leak host arena pointers to CPU clients.
    bool fetch(uint64_t address, void* destination, size_t length) const {
        if (!destination || !length) return false;
        const auto* source = image_.translate(address, length, 5);
        if (!source) return false;
        std::memmove(destination, source, length);
        return true;
    }
    bool write(uint64_t address, const void* source, size_t length) {
        if (!source || !length) return false;
        const auto* destination = image_.translate(address, length, 2);
        if (!destination || image_.translate(address, length, 1)) return false;
        std::memmove(const_cast<uint8_t*>(destination), source, length);
        return true;
    }
    bool permitted(uint64_t address, size_t length, uint32_t permissions) const {
        return image_.translate(address, length, permissions) != nullptr;
    }
    bool executable(uint64_t address, size_t length) const {
        return permitted(address, length, 5);
    }
    // Report the contiguous permitted bytes in the containing segment, capped
    // by the caller's limit. Never return a raw host address or cross a segment.
    size_t contiguous(uint64_t address, size_t limit, uint32_t permissions) const {
        if (!limit || !permissions || (permissions & ~7U)) return 0;
        for (const auto& segment : image_.segments()) {
            if ((segment.permissions & permissions) != permissions ||
                address < segment.address ||
                segment.memory_size > UINT64_MAX - segment.address)
                continue;
            const uint64_t offset = address - segment.address;
            if (offset >= segment.memory_size) continue;
            const uint64_t available = segment.memory_size - offset;
            // Compare in the wider integer domain before converting. On a
            // host with size_t wider than uint64_t, a huge limit must not
            // truncate and incorrectly reduce the reported guest span.
            if constexpr (sizeof(size_t) > sizeof(uint64_t)) {
                return static_cast<uint64_t>(limit) > available &&
                       limit > UINT64_MAX
                    ? static_cast<size_t>(available)
                    : (available < static_cast<uint64_t>(limit)
                        ? static_cast<size_t>(available) : limit);
            }
            return available < static_cast<uint64_t>(limit)
                ? static_cast<size_t>(available) : limit;
        }
        return 0;
    }
private:
    GuestELF& image_;
};
}
