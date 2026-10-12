#include "../Native/PS4GuestMappingPlan.hpp"
#include "GuestELFFixture.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

int main() {
    auto file = guest_test::elf(40, 2, true);
    file[7] = 9;
    guest_test::put(file, 0xfe00, 16, 2);
    maxps4::PS4ExecutableImage image;
    assert(image.load(file.data(), file.size()) == 0);
    const auto plan = maxps4::planPS4GuestMappings(image);
    assert(plan && plan.entry == image.entry);
    assert(plan.mappings.size() == image.segments.size());
    for (size_t i = 0; i < plan.mappings.size(); ++i) {
        const auto& mapping = plan.mappings[i];
        const auto& segment = image.segments[i];
        assert(mapping.guestAddress == segment.header.address);
        assert(mapping.length == segment.header.memorySize);
        assert(mapping.permissions == segment.header.flags);
        assert(mapping.bytes == segment.bytes.data());
    }
    image.requiresLinker = true;
    auto rejected = maxps4::planPS4GuestMappings(image);
    assert(!rejected && rejected.mappings.empty());
    image.requiresLinker = false;
    image.requiresTLS = true;
    rejected = maxps4::planPS4GuestMappings(image);
    assert(!rejected && rejected.mappings.empty());
    image.requiresTLS = false;
    image.entry = 0;
    rejected = maxps4::planPS4GuestMappings(image);
    assert(!rejected && rejected.mappings.empty());
    std::puts("PASS PS4 guest mapping preflight: segments, entry, dynamic/TLS rejection");
    std::puts("Preflight only; FEXCore guest mappings and execution are not implemented here");
}
