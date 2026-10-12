#pragma once
#include "GuestELFFixture.h"
#include <algorithm>
inline std::vector<uint8_t> ps4ELF() {
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
inline std::vector<uint8_t> plainSELF() {
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
