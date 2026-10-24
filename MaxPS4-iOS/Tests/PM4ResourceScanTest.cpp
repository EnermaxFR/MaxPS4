#include "../Native/PM4ResourceScan.hpp"
#include "GCNTestPrograms.h"
#include <cassert>
#include <cstdio>
using namespace maxps4::gpu;
using namespace gcn_test;
int main() {
    Arena arena;
    loadFetchVS(arena); loadQuadPS(arena);
    PM4Draw d;
    d.vs = fetchVS; d.vsUserCount = 4;
    d.vsUser[0] = std::uint32_t(fetchShader); d.vsUser[2] = std::uint32_t(vertexTable);
    d.ps = quadPS; d.psUserCount = 16;
    for (unsigned k = 0; k < 16; ++k) d.psUser[k] = quadSgprs[k];
    d.psInputAddr = 2; d.indices = {0, 1, 2};
    d.scissor[2] = 16; d.scissor[3] = 8;
    const auto ranges = scanDrawResources(arena.memory, d);
    for (const auto& r : ranges) std::printf("range 0x%llx + 0x%llx\n", (unsigned long long)r.base, (unsigned long long)r.size);
    // VS: the V# table read by s_load (0x10c00, 16 bytes) and the 3 vertices it
    // describes (0x10e00, 48 bytes), merged. PS: the 4x2 RGB565 texture
    // (0x12000, 16 bytes) and the constant buffer (0x13000, 16 bytes), merged.
    assert(ranges.size() == 2);
    assert(ranges[0].base == 0x10c00 && ranges[0].size == 0x230);
    assert(ranges[1].base == 0x12000 && ranges[1].size == 0x1010);
    // A program that faults keeps what it found before; nothing is invented.
    PM4Draw lost = d; lost.vs = 0x40000; lost.ps = 0x40000;
    assert(scanDrawResources(arena.memory, lost).empty());
    std::puts("PASS PM4 resource scan: scalar table, vertex buffer, texture and constant buffer extents, merged");
}
