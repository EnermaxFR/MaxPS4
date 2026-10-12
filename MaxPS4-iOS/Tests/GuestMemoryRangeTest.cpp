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
    std::puts("Guest range permission and overflow checks passed");
}
