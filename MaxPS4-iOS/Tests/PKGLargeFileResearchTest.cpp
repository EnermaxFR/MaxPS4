#include "PS4PKGBridge.h"
#include <array>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
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
    std::puts("PASS sparse >2GB/4GB PKG inventory and exact artwork read");
}
