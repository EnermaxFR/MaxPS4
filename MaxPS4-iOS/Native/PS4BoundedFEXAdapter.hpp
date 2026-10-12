#pragma once
// Adapter for the strictly bounded FEX ELF fixture runner.
// This is NOT a general PS4 loader or an Orbis ABI implementation.
#include "PS4ExecutableImage.hpp"
#include "PS4GuestMappingPlan.hpp"
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace maxps4 {
struct PS4BoundedELF {
    int error = 0;
    std::vector<uint8_t> file;
};
inline void ps4PutLE(std::vector<uint8_t>& out, size_t at, uint64_t value, size_t count) {
    for (size_t i = 0; i < count; ++i) out[at + i] = static_cast<uint8_t>(value >> (8 * i));
}
// Rebuild an ordinary ET_EXEC container from validated plaintext PS4 segments.
// The restricted FEX runner independently validates all headers/instructions,
// owns its mappings, enforces W^X and applies its own instruction budget.
inline PS4BoundedELF adaptPS4ToBoundedELF(const PS4ExecutableImage& image) {
    PS4BoundedELF result;
    const auto plan = planPS4GuestMappings(image);
    if (!plan) { result.error = plan.error; return result; }
    if (image.segments.size() > 64) { result.error = -E2BIG; return result; }
    constexpr size_t phSize = 56, headerSize = 64, cap = 32 * 1024 * 1024;
    const size_t count = image.segments.size();
    size_t offset = headerSize + count * phSize;
    // Align file-backed segments to 4 KiB to preserve ELF p_vaddr/p_offset congruence.
    // This format is only for the restricted fixture admission path.
    try {
        result.file.resize(offset, 0);
        const uint8_t ident[] = {0x7f,'E','L','F',2,1,1,0};
        std::memcpy(result.file.data(), ident, sizeof(ident));
        ps4PutLE(result.file, 16, 2, 2);
        ps4PutLE(result.file, 18, 62, 2);
        ps4PutLE(result.file, 20, 1, 4);
        ps4PutLE(result.file, 24, image.entry, 8);
        ps4PutLE(result.file, 32, headerSize, 8);
        ps4PutLE(result.file, 52, headerSize, 2);
        ps4PutLE(result.file, 54, phSize, 2);
        ps4PutLE(result.file, 56, count, 2);
        for (size_t i = 0; i < count; ++i) {
            const auto& s = image.segments[i];
            const auto& h = s.header;
            const size_t padding = static_cast<size_t>((h.address & 4095) + 4096 - (offset & 4095)) & 4095;
            if (offset > cap || padding > cap - offset) { result.error = -E2BIG; break; }
            offset += padding;
            if (h.fileSize > cap - offset || h.fileSize > s.bytes.size()) {
                result.error = -E2BIG; break;
            }
            const size_t ph = headerSize + i * phSize;
            ps4PutLE(result.file, ph, 1, 4);
            ps4PutLE(result.file, ph + 4, h.flags, 4);
            ps4PutLE(result.file, ph + 8, offset, 8);
            ps4PutLE(result.file, ph + 16, h.address, 8);
            ps4PutLE(result.file, ph + 24, h.address, 8);
            ps4PutLE(result.file, ph + 32, h.fileSize, 8);
            ps4PutLE(result.file, ph + 40, h.memorySize, 8);
            ps4PutLE(result.file, ph + 48, 4096, 8);
            result.file.resize(offset + static_cast<size_t>(h.fileSize), 0);
            if (h.fileSize) std::memcpy(result.file.data() + offset, s.bytes.data(), static_cast<size_t>(h.fileSize));
            offset += static_cast<size_t>(h.fileSize);
        }
    } catch (const std::bad_alloc&) { result.error = -ENOMEM; }
    if (result.error) result.file.clear();
    return result;
}
} // namespace maxps4
