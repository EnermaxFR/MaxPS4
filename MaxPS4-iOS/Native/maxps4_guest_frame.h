#pragma once
#include "maxps4_guest_elf.h"
#include <cerrno>
#include <cstring>
namespace maxps4 {
// RGBA8 tightly packed snapshots. Addresses remain logical guest addresses.
struct GuestFrame {
    uint64_t pixels = 0, input = 0;
    uint32_t width = 0, height = 0;
    size_t bytes() const { return size_t(width) * height * 4; }
    int validate(const GuestELF& image) const {
        if (!width || !height || width > 320 || height > 240 ||
            !pixels || !input || (pixels & 3) || (input & 3)) return -EINVAL;
        if (!image.translate(pixels, bytes(), 6) || image.translate(pixels, bytes(), 1) ||
            !image.translate(input, 4, 6) || image.translate(input, 4, 1)) return -EFAULT;
        if (input >= pixels ? input - pixels < bytes() : pixels - input < 4) return -EINVAL;
        return 0;
    }
    int setInput(const GuestELF& image, uint32_t buttons) const {
        const int error = validate(image);
        if (error) return error;
        if (buttons & ~31u) return -EINVAL;
        auto* destination = const_cast<uint8_t*>(image.translate(input, 4, 2));
        for (size_t i = 0; i < 4; ++i) destination[i] = uint8_t(buttons >> (8*i));
        return 0;
    }
    int snapshot(const GuestELF& image, uint8_t* out, size_t capacity) const {
        const int error = validate(image);
        if (error) return error;
        if (!out || capacity != bytes()) return -EINVAL;
        std::memcpy(out, image.translate(pixels, bytes(), 4), bytes());
        return 0;
    }
};
}
