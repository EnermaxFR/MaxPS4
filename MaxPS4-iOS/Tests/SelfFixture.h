#pragma once
// Wraps a plain ELF in a SELF container the way homebrew fake-signed SELF
// (fSELF) tools lay it out: SELF header, one blocked segment per program
// header that has file bytes and is not inside another one (flags: ordered,
// signed, blocked, ID in bits 20..31), the ELF header and program headers,
// then each block's bytes. `extra` is ORed into every block's flags (0x2:
// encrypted, 0x8: deflated) to build refused files.
#include <cstdint>
#include <cstring>
#include <vector>

namespace self_fixture {
inline uint64_t get(const std::vector<uint8_t>& b, size_t at, size_t width) {
    uint64_t value = 0;
    for (size_t i = 0; i < width; ++i) value |= uint64_t(b[at + i]) << (8 * i);
    return value;
}
inline void set(std::vector<uint8_t>& b, size_t at, uint64_t value, size_t width) {
    for (size_t i = 0; i < width; ++i) b[at + i] = uint8_t(value >> (8 * i));
}
inline std::vector<uint8_t> wrap(const std::vector<uint8_t>& elf, uint64_t extra = 0) {
    const uint64_t phoff = get(elf, 32, 8), phnum = get(elf, 56, 2);
    struct Block { uint64_t id, offset, size; };
    std::vector<Block> blocks;
    auto inside = [&](uint64_t i) {
        const uint64_t offset = get(elf, phoff + i * 56 + 8, 8), size = get(elf, phoff + i * 56 + 32, 8);
        for (uint64_t j = 0; j < phnum; ++j) {
            if (j == i) continue;
            const uint64_t start = get(elf, phoff + j * 56 + 8, 8), bytes = get(elf, phoff + j * 56 + 32, 8);
            if (bytes > size && offset >= start && offset + size <= start + bytes) return true;
        }
        return false;
    };
    for (uint64_t i = 0; i < phnum; ++i) {
        const uint64_t size = get(elf, phoff + i * 56 + 32, 8);
        if (size && !inside(i)) blocks.push_back({i, get(elf, phoff + i * 56 + 8, 8), size});
    }
    const uint64_t origin = 32 + blocks.size() * 32, headers = origin + phoff + phnum * 56;
    uint64_t payload = (headers + 15) & ~uint64_t{15};
    std::vector<uint8_t> self(payload, 0);
    set(self, 0, 0x1d3d154f, 4);
    self[4] = 0; self[5] = 1; self[6] = 1; self[7] = 0x12; self[8] = 1; self[9] = 1;
    set(self, 12, headers, 2);
    set(self, 24, blocks.size(), 2);
    std::memcpy(self.data() + origin, elf.data(), size_t(phoff + phnum * 56));
    for (size_t k = 0; k < blocks.size(); ++k) {
        const auto& block = blocks[k];
        const uint64_t flags = 0x1 | 0x4 | 0x800 | (block.id << 20) | extra;
        set(self, 32 + k * 32, flags, 8);
        set(self, 32 + k * 32 + 8, self.size(), 8);
        set(self, 32 + k * 32 + 16, block.size, 8);
        set(self, 32 + k * 32 + 24, block.size, 8);
        self.insert(self.end(), elf.begin() + long(block.offset), elf.begin() + long(block.offset + block.size));
        self.resize((self.size() + 15) & ~size_t{15}, 0);
    }
    set(self, 16, self.size(), 4);
    return self;
}
}
