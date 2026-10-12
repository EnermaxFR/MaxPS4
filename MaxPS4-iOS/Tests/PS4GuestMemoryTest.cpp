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
    // RX code must be readable and executable but never writable.
    uint8_t code_byte = 0;
    assert(memory.read(0x1000, &code_byte, 1));
    assert(code_byte == 0xB8);
    assert(memory.executable(0x1000, 1));
    assert(!memory.executable(0x10000, 1));
    // Reject requests straddling an unmapped boundary.
    assert(!memory.read(0x10000 - 1, output, sizeof(output)));
    assert(!memory.write(0x10000 - 1, bytes, sizeof(bytes)));

    // Reject writes crossing the RW segment boundary, without partial mutation.
    const auto& regions = image.segments();
    bool checked_boundary = false;
    for (const auto& region : regions) {
        if ((region.permissions & 7U) != 6U || region.memory_size < 4) continue;
        checked_boundary = true;
        const uint64_t last = region.address + region.memory_size - 1;
        uint8_t sentinel = 0;
        assert(memory.read(last, &sentinel, 1));
        assert(!memory.write(last, bytes, sizeof(bytes)));
        uint8_t after = 0;
        assert(memory.read(last, &after, 1));
        assert(after == sentinel);
    }
    assert(checked_boundary);
    assert(!memory.read(0x1000, output, 0));
    assert(!memory.write(0x10000, bytes, 0));

    assert(!memory.read(UINT64_MAX, output, sizeof(output)));
    assert(!memory.write(UINT64_MAX, bytes, sizeof(bytes)));
    assert(!memory.read(0x10000, nullptr, 4));
    assert(!memory.write(0x10000, nullptr, 4));
    image.clear();
    assert(!memory.read(0x10000, output, 4));
    std::puts("PS4 guest memory adapter checks passed");
}
