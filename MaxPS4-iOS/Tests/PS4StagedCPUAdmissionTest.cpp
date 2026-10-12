#include "../Native/maxps4_guest_elf.h"
#include "../Native/maxps4_ps4_guest_memory.h"
#include "PS4ExecutableFixture.h"
#include <cerrno>
#include <cstdio>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
    maxps4::GuestELF image;
    maxps4::PS4GuestMemory memory(image);
    CHECK(image.admit_staged_cpu(6) == -EFAULT);
    auto file = guest_test::generated_pm4();
    file[7] = 9;
    guest_test::put(file, 0xfe00, 16, 2);
    CHECK(image.stage_ps4(file.data(), file.size()) == 0);
    CHECK(image.admit_staged_cpu(6) == 0);
    CHECK(image.ps4_blockers() == 4); // General address space still missing.
    CHECK(image.admit_staged_cpu(5) == -E2BIG);
    CHECK(image.admit_staged_cpu(0) == -EINVAL);
    CHECK(image.admit_staged_cpu(1000001) == -EINVAL);
    uint8_t data[12] = {};
    CHECK(memory.read(0x10000, data, sizeof(data)));
    for (auto byte : data) CHECK(byte == 0); // Admission never executes stores.
    // Re-admit all instructions before CPU dispatch. A late unsupported opcode
    // must reject the entire image before any early store can take effect.
    auto bad = file;
    bad[224] = 0xc3;
    CHECK(image.stage_ps4(bad.data(), bad.size()) == 0);
    CHECK(image.admit_staged_cpu(6) == -ENOTSUP);
    for (uint64_t target : {uint64_t{0x1000}, uint64_t{0}, uint64_t{0x1003e}, UINT64_MAX}) {
        bad = file;
        guest_test::put(bad, target - 0x1014, 196, 4);
        CHECK(image.stage_ps4(bad.data(), bad.size()) == 0);
        CHECK(image.admit_staged_cpu(6) == -ENOTSUP);
    }
    // Read-only data cannot receive a store.
    bad = file; guest_test::put(bad, 4, 124, 4);
    CHECK(image.stage_ps4(bad.data(), bad.size()) == 0);
    CHECK(image.admit_staged_cpu(6) == -ENOTSUP);
    bad = file; guest_test::put(bad, 0xfe10, 16, 2);
    CHECK(image.stage_ps4(bad.data(), bad.size()) == 0);
    CHECK(image.admit_staged_cpu(6) == -ENOTSUP);
    // TLS metadata must also block bounded dispatch.
    bad = ps4ELF();
    bad.resize(264, 0);
    std::copy(bad.begin() + 184, bad.begin() + 207, bad.begin() + 240);
    guest_test::put(bad, 3, 56, 2);
    guest_test::put(bad, 240, 72, 8); guest_test::put(bad, 251, 128, 8);
    std::fill(bad.begin() + 176, bad.begin() + 232, 0);
    guest_test::put(bad, 7, 176, 4);
    CHECK(image.stage_ps4(bad.data(), bad.size()) == 0);
    CHECK(image.ps4_blockers() == 6);
    CHECK(image.admit_staged_cpu(6) == -ENOTSUP);
    // Relocate logical addresses above 4 GiB: admission uses guest addresses.
    bad = file;
    constexpr uint64_t base = 0x100000000ULL;
    guest_test::put(bad, base + 0x1000, 24, 8);
    guest_test::put(bad, base + 0x1000, 80, 8);
    guest_test::put(bad, base + 0x10000, 136, 8);
    CHECK(image.stage_ps4(bad.data(), bad.size()) == 0);
    CHECK(image.admit_staged_cpu(6) == 0);
    CHECK(memory.read(base + 0x10000, data, sizeof(data)));
    CHECK(!memory.read(0x10000, data, sizeof(data)));
    // Negative RIP displacement: data before code.
    guest_test::put(bad, base + 0x20000, 24, 8);
    guest_test::put(bad, base + 0x20000, 80, 8);
    for (size_t i = 0; i < 3; ++i)
        guest_test::put(bad, (base + 0x10000 + i * 4) - (base + 0x20014 + i * 10), 196 + i * 10, 4);
    CHECK(image.stage_ps4(bad.data(), bad.size()) == 0);
    CHECK(image.admit_staged_cpu(6) == 0);
    // Clear and failed restaging must revoke admission immediately.
    image.clear();
    CHECK(image.admit_staged_cpu(6) == -EFAULT);
    CHECK(image.stage_ps4(file.data(), file.size()) == 0);
    const uint8_t invalid[] = {0, 1};
    CHECK(image.stage_ps4(invalid, sizeof(invalid)) != 0);
    CHECK(image.admit_staged_cpu(6) == -EFAULT);
    std::puts("PASS staged CPU admission: permissions, budget, logical addresses and teardown; no CPU executed");
}
