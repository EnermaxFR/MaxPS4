#include "../Native/maxps4_guest_elf.h"
#include "GuestELFFixture.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
int main() {
    using guest_test::put;
    maxps4::GuestELF image;
    auto file = guest_test::generated_pm4();
    assert(image.load(file.data(), file.size(), 6) == 0);
    assert(image.translate(0x10000, 12, 6)[0] == 0); // Admission does not execute.
    assert(image.load(file.data(), file.size(), 5) == -E2BIG);
    auto rejects = [&](size_t offset, uint64_t value, size_t width) {
        auto bad = file; put(bad, value, offset, width);
        assert(image.load(bad.data(), bad.size(), 6) != 0 && image.segments().empty());
    };
    rejects(195, 0x00, 1); // Unsupported ModRM; no arbitrary addressing.
    rejects(196, 0, 4); // Store into next instruction's RX code.
    rejects(196, 0x7fffffff, 4); // Far outside guest mappings.
    rejects(196, 0x80000000, 4); // Negative displacement underflows guest address.
    rejects(196, 0x1003e - 0x1014, 4); // Four-byte store crosses RW segment end.
    rejects(124, 4, 4); // Read-only target.
    rejects(124, 7, 4); // Writable code is never admitted.
    const uint8_t store[] = {0xc7, 0x05, 0, 0, 0, 0, 1, 0, 0, 0, 0xf4};
    assert(maxps4::validate_guest_program(store, sizeof(store), 2) == -ENOTSUP);
    // Signed backwards displacement into a data segment preceding the code.
    auto backwards = file;
    put(backwards, 0x20000, 24, 8); put(backwards, 0x20000, 80, 8);
    for (size_t i = 0; i < 3; ++i)
        put(backwards, uint32_t(0x10000 + i * 4 - (0x20000 + 20 + i * 10)), 196 + i * 10, 4);
    assert(image.load(backwards.data(), backwards.size(), 6) == 0);
    // Entry offsets contribute to RIP; an incorrect target is not admitted.
    put(backwards, 0x20005, 24, 8);
    assert(image.load(backwards.data(), backwards.size(), 5) == -ENOTSUP); // ADD before MOV.
    std::puts("PASS bounded RIP-relative store admission: exact addressing, RW targets, budget and signed offsets");
}
