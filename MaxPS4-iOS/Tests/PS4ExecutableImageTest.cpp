#include "../Native/PS4ExecutableImage.hpp"
#include "../Native/PM4GuestSubmission.hpp"
#include "GuestELFFixture.h"
#include <cassert>
#include <algorithm>
#include <cstdio>

std::vector<uint8_t> ps4ELF() {
    auto file = guest_test::elf(40, 2, true);
    file[7] = 9;
    guest_test::put(file, 0xfe00, 16, 2);
    file.resize(207);
    guest_test::put(file, 12, 152, 8);
    guest_test::put(file, 0xc0012d00, 195, 4);
    guest_test::put(file, 3, 199, 4);
    guest_test::put(file, 2, 203, 4);
    return file;
}
std::vector<uint8_t> plainSELF() {
    const auto elf = ps4ELF();
    std::vector<uint8_t> file(311, 0);
    guest_test::put(file, 0x1d3d154f, 0, 4);
    file[5] = 1; file[6] = 1; file[7] = 0x12; file[8] = 1; file[9] = 1;
    guest_test::put(file, 288, 12, 2); guest_test::put(file, file.size(), 16, 4);
    guest_test::put(file, 2, 24, 2); guest_test::put(file, 0x22, 26, 2);
    // Deliberately reverse block order. Segment IDs, not ordinal order or
    // arbitrary synthetic addresses, determine the destination PT_LOAD.
    guest_test::put(file, 0x100800, 32, 8);
    guest_test::put(file, 299, 40, 8); guest_test::put(file, 12, 48, 8); guest_test::put(file, 12, 56, 8);
    guest_test::put(file, 0x800, 64, 8);
    guest_test::put(file, 288, 72, 8); guest_test::put(file, 11, 80, 8); guest_test::put(file, 11, 88, 8);
    std::copy(elf.begin(), elf.begin() + 176, file.begin() + 96);
    std::copy(elf.begin() + 184, elf.begin() + 195, file.begin() + 288);
    std::copy(elf.begin() + 195, elf.end(), file.begin() + 299);
    return file;
}
int main() {
    maxps4::PS4ExecutableImage image;
    auto elf = ps4ELF();
    assert(image.load(elf.data(), elf.size()) == 0);
    assert(!image.isSelf && image.entry == 0x1000 && image.type == 0xfe00);
    assert(image.segments.size() == 2 && !image.requiresLinker && !image.requiresTLS);
    assert(image.translate(0x1000, 11, 5)[0] == 0xb8);
    assert(image.translate(0x1000, 1, 2) == nullptr);
    assert(image.translate(0x10000, 64, 6)[12] == 0); // Zero-filled BSS.
    auto commands = maxps4::gpu::snapshotPM4(image, 0x10000, 3);
    assert(commands.error == 0 && commands.words[2] == 2);
    auto self = plainSELF();
    assert(image.load(self.data(), self.size()) == 0);
    assert(image.isSelf && image.entry == 0x1000 && image.segments.size() == 2);
    assert(image.translate(0x1000, 11, 5)[0] == 0xb8);
    assert(image.translate(0x10000, 64, 6)[12] == 0);
    commands = maxps4::gpu::snapshotPM4(image, 0x10000, 3);
    assert(commands.error == 0 && commands.words[0] == 0xc0012d00);
    auto reject = [&](const std::vector<uint8_t>& original, size_t offset, uint64_t value, size_t width) {
        auto bad = original; guest_test::put(bad, value, offset, width);
        assert(image.load(bad.data(), bad.size()) != 0);
        assert(image.entry == 0 && image.segments.empty() && image.headers.empty());
    };
    reject(self, 5, 2, 1); reject(self, 7, 0, 1);
    reject(self, 24, 129, 2); reject(self, 16, self.size() + 1, 4);
    reject(self, 12, 64, 2); reject(self, 40, UINT64_MAX, 8);
    reject(self, 48, 13, 8); reject(self, 56, 13, 8);
    reject(self, 32, 0x200800, 8); // Block ID outside program header table.
    reject(self, 64, 0x100800, 8); // Duplicate block for program header 1.
    reject(self, 32, 0x100802, 8); // Encrypted: no substitute/decryption.
    reject(self, 32, 0x100808, 8); // Compressed: no guessed layout.
    reject(self, 32, 0x100810, 8); // Unknown flags.
    reject(elf, 7, 0, 1); reject(elf, 16, 2, 2); reject(elf, 18, 183, 2);
    reject(elf, 32, UINT64_MAX, 8); reject(elf, 56, 129, 2);
    reject(elf, 24, 0x10000, 8); reject(elf, 96, 33, 8);
    reject(elf, 104, maxps4::PS4ExecutableImage::memoryBudget + 1, 8);
    reject(elf, 136, 0x1001, 8); reject(elf, 80, UINT64_MAX - 2, 8);
    reject(elf, 72, UINT64_MAX, 8); reject(elf, 112, 3, 8);
    for (size_t length = 0; length < 272; ++length)
        assert(image.load(self.data(), length) != 0 && image.segments.empty());
    // A PS4 library has no required entry. Dynamic/TLS metadata is retained,
    // explicitly marked as unresolved, and never passed to FEX by this class.
    guest_test::put(elf, 0xfe18, 16, 2); guest_test::put(elf, 0, 24, 8);
    guest_test::put(elf, 2, 120, 4);
    assert(image.load(elf.data(), elf.size()) == 0 && image.requiresLinker && image.headers.size() == 2);
    guest_test::put(elf, 7, 120, 4);
    assert(image.load(elf.data(), elf.size()) == 0 && image.requiresTLS);
    std::puts("PASS real PS4 ELF identity and plaintext SELF block-ID mapping, BSS, PM4 read, bounds/rejections");
    std::puts("Segment staging only: no PS4 imports, relocations, TLS initialization, HLE or execution");
}
