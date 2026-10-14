#include "PS4PKGBridge.h"
#include <array>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
int main(int argc, char** argv) {
    assert(argc == 2);
    std::array<char, 65536> report{};
    assert(maxps4_pkg_inventory_report(argv[1], report.data(), report.size()) == 0);
    assert(std::strstr(report.data(), "icon0.png"));
    size_t length = 0;
    assert(maxps4_pkg_read_artwork(argv[1], 0x1200, nullptr, 0, &length) == -ENOSPC);
    assert(length == 24);
    std::array<unsigned char, 24> output{};
    assert(maxps4_pkg_read_artwork(argv[1], 0x1200, output.data(), output.size(), &length) == 0);
    assert(output[0] == 137 && output[19] == 1 && output[23] == 1);
    // A short destination must not receive a partial PNG or change its guard bytes.
    std::array<unsigned char, 23> shortOutput{};
    shortOutput.fill(0xA5);
    length = 0;
    assert(maxps4_pkg_read_artwork(argv[1], 0x1200, shortOutput.data(), shortOutput.size(), &length) == -ENOSPC);
    assert(length == 24);
    for (auto b : shortOutput) assert(b == 0xA5);
    // Artwork is not an executable. A sparse CNT without a readable PFS must
    // never return an eboot.bin payload, even when given a writable buffer.
    std::array<unsigned char, 32> ebootOutput{};
    ebootOutput.fill(0xA5);
    length = 99;
    assert(maxps4_pkg_pfs_read_file(argv[1], "eboot.bin", ebootOutput.data(), ebootOutput.size(), &length) != 0);
    assert(length == 0);
    for (auto b : ebootOutput) assert(b == 0xA5);
    // An oversized selection name must be rejected before any PFS inspection.
    std::string oversizedName(4097, 'x');
    assert(maxps4_pkg_pfs_read_file(argv[1], oversizedName.c_str(), ebootOutput.data(), ebootOutput.size(), &length) == -EACCES);
    // Only the two explicitly admitted artwork descriptors are readable.
    length = 99;
    assert(maxps4_pkg_read_artwork(argv[1], 0x1000, output.data(), output.size(), &length) == -EINVAL);
    assert(length == 0);
    std::puts("PASS sparse >2GB/4GB PKG inventory, exact artwork read, and bounded output");
}
