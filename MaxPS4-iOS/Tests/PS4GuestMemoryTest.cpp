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
    assert(memory.permitted(0x10000, 4, 6));
    assert(memory.permitted(0x10000, 4, 4));
    assert(memory.permitted(0x10000, 4, 2));
    assert(!memory.permitted(0x10000, 4, 1));
    assert(!memory.permitted(0x10000, 4, 0));
    assert(!memory.permitted(0x10000, 4, 8));
    assert(!memory.permitted(UINT64_MAX, 4, 4));
    assert(!memory.executable(0x10000, 4));
    assert(memory.executable(0x1000, 1));
    assert(!memory.write(0x1000, bytes, sizeof(bytes)));
    // RX code must be readable and executable but never writable.
    uint8_t code_byte = 0;
    assert(memory.read(0x1000, &code_byte, 1));
    assert(code_byte == 0xB8);
    uint8_t fetched = 0;
    assert(memory.fetch(0x1000, &fetched, 1) && fetched == code_byte);
    assert(!memory.fetch(0x10000, &fetched, 1));
    assert(!memory.fetch(0x1000, nullptr, 1));
    assert(!memory.fetch(0x1000, &fetched, 0));
    assert(!memory.fetch(UINT64_MAX, &fetched, 1));
    // An invalid instruction fetch must not change the destination buffer.
    fetched = 0xA7;
    assert(!memory.fetch(0x10000, &fetched, 1));
    assert(fetched == 0xA7);
    assert(!memory.fetch(0x1000 - 1, &fetched, 1));
    assert(fetched == 0xA7);
    assert(!memory.fetch(0x1000, &fetched, SIZE_MAX));
    assert(fetched == 0xA7);
    // An instruction fetch must not partially modify output when it crosses
    // the executable segment boundary.
    uint8_t unchanged_fetch[4] = {0x11, 0x22, 0x33, 0x44};
    uint64_t executable_end = 0;
    for (const auto& segment : image.segments()) {
        if ((segment.permissions & 5U) == 5U) {
            executable_end = segment.address + segment.memory_size;
            break;
        }
    }
    assert(executable_end > 0);
    assert(!memory.fetch(executable_end - 1, unchanged_fetch, sizeof(unchanged_fetch)));
    assert(unchanged_fetch[0] == 0x11 && unchanged_fetch[1] == 0x22 &&
           unchanged_fetch[2] == 0x33 && unchanged_fetch[3] == 0x44);
    // A valid fetch may overlap its destination inside the same RX mapping:
    // memmove semantics are required for the host-side adapter.
    auto* code = const_cast<uint8_t*>(image.translate(0x1000, 8, 5));
    assert(code);
    uint8_t code_snapshot[4] = {};
    assert(memory.fetch(0x1000, code_snapshot, sizeof(code_snapshot)));
    // Destination is the code mapping itself; an identical-range copy is safe.
    assert(memory.fetch(0x1000, code, sizeof(code_snapshot)));
    for (size_t i = 0; i < sizeof(code_snapshot); ++i)
        assert(code[i] == code_snapshot[i]);
    assert(memory.executable(0x1000, 1));
    assert(!memory.executable(0x10000, 1));
    // Reject requests straddling an unmapped boundary.
    assert(!memory.read(0x10000 - 1, output, sizeof(output)));
    assert(!memory.write(0x10000 - 1, bytes, sizeof(bytes)));
    // A failed read must not overwrite the caller's destination buffer.
    uint8_t unchanged[4] = {0xA5, 0x5A, 0xC3, 0x3C};
    assert(!memory.read(0x10000 - 1, unchanged, sizeof(unchanged)));
    assert(unchanged[0] == 0xA5 && unchanged[1] == 0x5A &&
           unchanged[2] == 0xC3 && unchanged[3] == 0x3C);
    // A rejected write must leave the valid data segment untouched.
    uint8_t before_write[4] = {};
    uint8_t after_write[4] = {};
    assert(memory.read(0x10000, before_write, sizeof(before_write)));
    assert(!memory.write(0x10000 - 1, bytes, sizeof(bytes)));
    assert(memory.read(0x10000, after_write, sizeof(after_write)));
    for (unsigned i = 0; i < 4; ++i) assert(after_write[i] == before_write[i]);


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
    // Host callers may supply overlapping buffers inside the staged RW arena.
    // Transfers must have memmove semantics, not undefined memcpy overlap.
    auto* rw = const_cast<uint8_t*>(image.translate(0x10000, 8, 6));
    assert(rw);
    for (uint8_t i = 0; i < 8; ++i) rw[i] = i;
    assert(memory.write(0x10002, rw, 6));
    for (uint8_t i = 0; i < 6; ++i) assert(rw[i + 2] == i);
    assert(memory.read(0x10002, rw, 6));
    for (uint8_t i = 0; i < 6; ++i) assert(rw[i] == i);
    // A failed PS4 staging attempt must discard the previous ELF arena.
    const uint8_t invalid_ps4[] = {0, 1, 2, 3};
    assert(image.stage_ps4(invalid_ps4, sizeof(invalid_ps4)) != 0);
    assert(image.segments().empty());
    assert(image.entry() == 0);
    assert(!memory.permitted(0x10000, 4, 4));
    assert(!memory.read(0x10000, output, 4));
    assert(!memory.write(0x10000, bytes, 4));
    image.clear();
    assert(!memory.read(0x10000, output, 4));
    std::puts("PS4 guest memory adapter checks passed");
}
