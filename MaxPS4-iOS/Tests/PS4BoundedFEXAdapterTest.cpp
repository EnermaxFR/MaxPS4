#include "../Native/PS4BoundedFEXAdapter.hpp"
#include "GuestELFFixture.h"
#include <cassert>
#include <cstdio>
int main() {
    auto elf = guest_test::elf(40, 2, true);
    elf[7] = 9;
    guest_test::put(elf, 0xfe00, 16, 2);
    maxps4::PS4ExecutableImage image;
    assert(image.load(elf.data(), elf.size()) == 0);
    const auto converted = maxps4::adaptPS4ToBoundedELF(image);
    assert(converted.error == 0 && converted.file.size() > elf.size());
    assert(converted.file[7] == 0 && converted.file[16] == 2);
    assert(converted.file[24] == 0x00 && converted.file[25] == 0x10);
    assert(converted.file[56] == 2);
    const auto secondOffset = [](const std::vector<uint8_t>& bytes) {
        uint64_t n = 0;
        for (unsigned i = 0; i < 8; ++i) n |= uint64_t(bytes[64 + 56 + 8 + i]) << (8 * i);
        return n;
    }(converted.file);
    assert(secondOffset < converted.file.size());
    assert(converted.file[secondOffset] == image.segments[1].bytes[0]);
    image.requiresLinker = true;
    const auto rejected = maxps4::adaptPS4ToBoundedELF(image);
    assert(rejected.error != 0 && rejected.file.empty());
    std::puts("PASS PS4 plaintext staged segments to bounded ELF fixture adapter");
    std::puts("This test does not prove FEXCore execution or PS4 game compatibility");
}
