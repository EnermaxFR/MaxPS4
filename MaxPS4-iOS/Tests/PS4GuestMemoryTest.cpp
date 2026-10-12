#include "../Native/maxps4_ps4_guest_memory.h"
#include "GuestELFFixture.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
int main() {
    maxps4::GuestELF image;
    auto file = guest_test::generated_pm4();
    assert(image.load(file.data(), file.size(), 6) == 0);
    maxps4::PS4GuestMemory memory(image);
    uint8_t bytes[4] = {1, 2, 3, 4};
    uint8_t output[4] = {};
    assert(memory.write(0x10000, bytes, sizeof(bytes)));
    assert(memory.read(0x10000, output, sizeof(output)));
    for (unsigned i = 0; i < 4; ++i) assert(output[i] == bytes[i]);
    assert(!memory.executable(0x10000, 4));
    assert(memory.executable(0x1000, 1));
    assert(!memory.write(0x1000, bytes, sizeof(bytes)));
    assert(!memory.read(UINT64_MAX, output, sizeof(output)));
    assert(!memory.write(UINT64_MAX, bytes, sizeof(bytes)));
    assert(!memory.read(0x10000, nullptr, 4));
    assert(!memory.write(0x10000, nullptr, 4));
    image.clear();
    assert(!memory.read(0x10000, output, 4));
    std::puts("PS4 guest memory adapter checks passed");
}
