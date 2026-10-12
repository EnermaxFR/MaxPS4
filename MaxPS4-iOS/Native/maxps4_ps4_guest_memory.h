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
private:
    GuestELF& image_;
};
}
