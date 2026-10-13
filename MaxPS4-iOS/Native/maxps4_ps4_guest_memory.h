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
    // Copy a NUL-terminated guest string for future libkernel path/name calls.
    // Require a terminator within capacity and leave the caller buffer unchanged
    // on failure (unmapped pages, missing terminator, or invalid arguments).
    bool read_cstring(uint64_t address, char* destination, size_t capacity) const {
        if (!destination || !capacity || capacity > 4096) return false;
        char scratch[4096];
        for (size_t i = 0; i < capacity; ++i) {
            if (address > UINT64_MAX - i ||
                !read(address + i, &scratch[i], 1)) return false;
            if (scratch[i] == '\\0') {
                std::memmove(destination, scratch, i + 1);
                return true;
            }
        }
        return false;
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
            // uintmax_t avoids truncating a wide host size_t before comparison.
            return static_cast<uintmax_t>(limit) > available
                ? static_cast<size_t>(available) : limit;
        }
        return 0;
    }
private:
    GuestELF& image_;
};
}
