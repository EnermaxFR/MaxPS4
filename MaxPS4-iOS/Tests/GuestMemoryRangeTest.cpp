#include "../Native/maxps4_guest_range.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
int main() {
    using maxps4::guest_range_permitted;
    assert(guest_range_permitted(0x1000, 4, 4, 0x1000, 0x100, 6));
    assert(guest_range_permitted(0x10ff, 1, 2, 0x1000, 0x100, 6));
    assert(!guest_range_permitted(0x10ff, 2, 4, 0x1000, 0x100, 6));
    assert(!guest_range_permitted(0x1000, 0, 4, 0x1000, 0x100, 6));
    assert(!guest_range_permitted(0x1000, 1, 1, 0x1000, 0x100, 6));
    assert(!guest_range_permitted(0x1000, 1, 0, 0x1000, 0x100, 6));
    assert(!guest_range_permitted(0x1000, 1, 8, 0x1000, 0x100, 6));
    assert(!guest_range_permitted(0xffffffffffffffffULL, 1, 4,
                                  0xffffffffffffffffULL, 2, 4));
    assert(!guest_range_permitted(0xffffffffffffffffULL, 2, 4,
                                  0xfffffffffffffffeULL, 2, 4));
    assert(!guest_range_permitted(0xfff, 1, 4, 0x1000, 0x100, 6));
    // The CPU store validator and the staged memory adapter share this proof.
    assert(guest_range_permitted(0x10fc, 4, 2, 0x1000, 0x100, 6));
    assert(!guest_range_permitted(0x10fd, 4, 2, 0x1000, 0x100, 6));
    assert(!guest_range_permitted(0x10fc, 4, 2, 0x1000, 0x100, 5));
    assert(guest_range_permitted(0x10fc, 4, 2, 0x1000, 0x100, 7));
    assert(!guest_range_permitted(UINT64_MAX - 1, 4, 2,
                                  UINT64_MAX - 8, 8, 6));
    // Validate every byte position and length around a short segment end.
    // This catches off-by-one errors independently of the hand-picked cases.
    for (uint64_t address = 0x1ff8; address <= 0x2008; ++address) {
        for (size_t length = 0; length <= 16; ++length) {
            const bool expected = length != 0 && address >= 0x2000 &&
                address - 0x2000 < 8 && length <= 8 - (address - 0x2000);
            assert(guest_range_permitted(address, length, 4,
                                         0x2000, 8, 6) == expected);
        }
    }
    std::puts("Guest range permission and overflow checks passed");
}
