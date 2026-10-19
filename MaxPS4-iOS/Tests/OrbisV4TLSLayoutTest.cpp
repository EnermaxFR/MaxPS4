#include "../Native/OrbisV4TLSLayout.hpp"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

int main() {
    using namespace maxps4;
    const std::vector<OrbisV4TLSModule> modules = {
        {1, 64, 16, {1,2,3}}, {2, 1168, 64, {9,8}}, {3, 32, 256, {7}}
    };
    std::vector<OrbisV4TLSPlacement> positions;
    size_t size = 0;
    assert(OrbisV4TLSLayout::plan(modules, positions, size) == 0);
    assert(positions.size() == 3 && positions[1].offset % 64 == 0);
    assert(positions[2].offset % 256 == 0);
    std::vector<uint8_t> bytes;
    assert(OrbisV4TLSLayout::instantiate(modules, positions, size, bytes) == 0);
    assert(bytes[0] == 1 && bytes[3] == 0);
    assert(bytes[positions[1].offset] == 9 && bytes[positions[1].offset + 2] == 0);
    assert(bytes[positions[2].offset] == 7);

    auto old = positions;
    size_t old_size = size;
    auto invalid = modules;
    invalid[1].id = 1;
    assert(OrbisV4TLSLayout::plan(invalid, positions, size) == -EINVAL);
    assert(size == old_size && positions[1].id == old[1].id);
    invalid = modules;
    invalid[0].initial.resize(65);
    assert(OrbisV4TLSLayout::plan(invalid, positions, size) == -EINVAL);
    invalid = modules;
    invalid[1].alignment = 24;
    assert(OrbisV4TLSLayout::plan(invalid, positions, size) == -EINVAL);
    invalid = modules;
    invalid[1].memory_size = OrbisV4TLSLayout::max_total_size;
    assert(OrbisV4TLSLayout::plan(invalid, positions, size) == -EOVERFLOW);

    // Independent per-thread template instantiation (not an FEX integration test).
    std::vector<uint8_t> first, second;
    std::thread a([&] { assert(OrbisV4TLSLayout::instantiate(modules, positions, size, first) == 0); });
    std::thread b([&] { assert(OrbisV4TLSLayout::instantiate(modules, positions, size, second) == 0); });
    a.join(); b.join();
    first[0] = 99;
    assert(second[0] == 1);
    auto corrupt = positions;
    corrupt[1].offset++;
    auto preserved = second;
    assert(OrbisV4TLSLayout::instantiate(modules, corrupt, size, second) == -EINVAL);
    assert(second == preserved);
    std::cout << "PASS TLS multi-module layout, bounds, rollback, independent templates\n";
}
