#include "../Native/maxps4_guest_elf.h"
#include "PS4ExecutableFixture.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
    maxps4::GuestELF mapped;
    const auto elf = ps4ELF();
    const auto self = plainSELF();
    for (const auto& file : {elf, self}) {
        CHECK(mapped.stage_ps4(file.data(), file.size()) == 0);
        CHECK(mapped.entry() == 0x1000 && mapped.segments().size() == 2);
        CHECK(mapped.ps4_blockers() == 4);
        CHECK(mapped.entry_pointer() && mapped.entry_pointer()[0] == 0xb8);
        CHECK(!mapped.translate(0x1000, 1, 2));
        const auto* data = mapped.translate(0x10000, 64, 6);
        CHECK(data && data[12] == 0 && data[63] == 0);
        CHECK(!mapped.translate(0x10000, 1, 1));
        CHECK(!mapped.translate(0x10040, 1, 4));
        CHECK(!mapped.translate(UINT64_MAX, SIZE_MAX, 4));
        CHECK(data - mapped.entry_pointer() == 0xf000); // Shared FEX arena.
        CHECK(std::memcmp(data, "\0\x2d\1\xc0", 4) == 0);
        *const_cast<uint8_t*>(data + 12) = 99;
        CHECK(mapped.translate(0x1000c, 1, 4)[0] == 99);
        mapped.clear(); CHECK(!mapped.entry_pointer() && !mapped.ps4_blockers());
    }
    auto broken = elf;
    auto reject = [&](size_t at, uint64_t value, size_t width, int error) {
        CHECK(mapped.stage_ps4(elf.data(), elf.size()) == 0);
        broken = elf; guest_test::put(broken, value, at, width);
        CHECK(mapped.stage_ps4(broken.data(), broken.size()) == error);
        CHECK(mapped.segments().empty() && !mapped.entry_pointer() && !mapped.ps4_blockers());
        return 0;
    };
    CHECK(reject(68, 7, 4, -ENOEXEC) == 0); // No writable executable region.
    CHECK(reject(136, 0x10000000, 8, -E2BIG) == 0); // Sparse arena budget.
    const uint64_t page = static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
    CHECK(reject(136, 0x1000 + 32, 8, -ENOEXEC) == 0); // Host page alias.
    CHECK(page > 0);
    broken = elf; guest_test::put(broken, 0xfe10, 16, 2);
    CHECK(mapped.stage_ps4(broken.data(), broken.size()) == 0 && mapped.ps4_blockers() == 5);
    // Retain TLS and dynamic metadata as blockers, never silently dispatch.
    broken.resize(264, 0);
    std::copy(broken.begin() + 184, broken.begin() + 207, broken.begin() + 240);
    guest_test::put(broken, 3, 56, 2);
    guest_test::put(broken, 240, 72, 8); guest_test::put(broken, 251, 128, 8);
    std::fill(broken.begin() + 176, broken.begin() + 232, 0);
    guest_test::put(broken, 7, 176, 4);
    // An empty PT_TLS declares no TLS: only a sized one is a blocker.
    CHECK(mapped.stage_ps4(broken.data(), broken.size()) == 0 && mapped.ps4_blockers() == 5);
    guest_test::put(broken, 8, 216, 8);
    CHECK(mapped.stage_ps4(broken.data(), broken.size()) == 0 && mapped.ps4_blockers() == 7);
    for (uint64_t flag : {0x100802ULL, 0x100808ULL, 0x100810ULL}) {
        broken = self; guest_test::put(broken, flag, 32, 8);
        CHECK(mapped.stage_ps4(broken.data(), broken.size()) == -ENOTSUP);
        CHECK(mapped.segments().empty());
    }
    // Staging arbitrary instruction bytes cannot widen the existing CPU loader.
    broken = elf; broken[184] = 0x0f; broken[185] = 0x05;
    CHECK(mapped.stage_ps4(broken.data(), broken.size()) == 0);
    CHECK(mapped.load(broken.data(), broken.size(), 3) == -ENOEXEC);
    CHECK(!mapped.entry_pointer());
    for (unsigned i = 0; i < 100; ++i) {
        CHECK(mapped.stage_ps4(elf.data(), elf.size()) == 0);
        CHECK(mapped.translate(0x1000c, 1, 4)[0] == 0);
    }
    std::puts("PASS PS4 plaintext ELF/SELF -> shared native FEX arena -> entry/data/BSS readback; permissions, budgets, blockers, rejection and lifecycle");
    std::puts("Synthetic fixtures; no PS4 CPU dispatch, iPhone execution or game rendering tested");
}
