#include "maxps4_fex_runtime.h"
#include "maxps4_native.h"
#include "GuestELFFixture.h"
#include <FEXCore/Utils/AllocatorHooks.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <array>
#include <csignal>
#include <pthread.h>
#include <TargetConditionals.h>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::puts("BEGIN native ABI and real FEX context lifecycle test");
    // Same allocator implementation embedded in the native runtime. These
    // sizes/alignments are used by FEX's char and uint32_t containers.
    for (size_t alignment : {size_t{1}, size_t{4}, size_t{8}, size_t{64}}) {
        auto* memory = FEXCore::Allocator::aligned_alloc(alignment, 97);
        CHECK(memory && reinterpret_cast<uintptr_t>(memory) % alignment == 0);
        std::memset(memory, 0x5A, 97);
        FEXCore::Allocator::aligned_free(memory);
    }
    CHECK(FEXCore::Allocator::aligned_alloc(0, 97) == nullptr);
    CHECK(FEXCore::Allocator::aligned_alloc(3, 97) == nullptr);
    CHECK(maxps4_fex_engine_open(nullptr) == -EINVAL);
    CHECK(maxps4_fex_engine_stage(nullptr) == 0);
    maxps4_fex_engine_close(nullptr);
    CHECK(maxps4_jit_begin(nullptr, nullptr, nullptr) == -EINVAL);
#if !TARGET_OS_IPHONE
    uint64_t mailbox = 99, nonce = 99, page = 99;
    CHECK(maxps4_jit_begin(&mailbox, &nonce, &page) == -ENOTSUP);
    CHECK(mailbox == 0 && nonce == 0 && page == 0);
    CHECK(maxps4_jit_prepare() == -ENOTSUP);
#endif
    CHECK(maxps4_jit_cancel() == 0);
    CHECK(maxps4_jit_leases() == 0);
    CHECK(maxps4_fex_engine_initialize(nullptr) == -EINVAL);
    uint64_t probe_result = 99; uint32_t probe_backend = 99;
    CHECK(maxps4_fex_engine_probe(nullptr, &probe_result, &probe_backend) == -EINVAL);
    CHECK(probe_result == 0 && probe_backend == 0);
    CHECK(maxps4_native_capabilities() == 0);
    CHECK(maxps4_native_launch("probe") == -38);
    CHECK(maxps4_native_aether_cpu_probe() == 1);
    constexpr std::array signal_numbers {SIGILL, SIGSEGV, SIGBUS, SIGTRAP};
    std::array<struct sigaction, signal_numbers.size()> original_actions {};
    for (size_t i = 0; i < signal_numbers.size(); ++i)
        CHECK(::sigaction(signal_numbers[i], nullptr, &original_actions[i]) == 0);
    sigset_t original_mask {};
    CHECK(::pthread_sigmask(SIG_SETMASK, nullptr, &original_mask) == 0);
    for (unsigned repeat = 0; repeat != 3; ++repeat) {
        maxps4_fex_engine *first = nullptr, *second = nullptr;
        std::printf("OPEN contexts cycle=%u\n", repeat);
        CHECK(maxps4_fex_engine_open(&first) == 0 && first);
        CHECK(maxps4_fex_engine_open(&second) == 0 && second);
        CHECK(maxps4_fex_engine_stage(first) == MAXPS4_FEX_CONTEXT_CREATED);
        auto not_initialized = guest_test::elf();
        uint64_t pending_entry = 99;
        CHECK(maxps4_fex_engine_run_elf(first, not_initialized.data(), not_initialized.size(), 3,
            &probe_result, &probe_backend, &pending_entry) == -EAGAIN);
        CHECK(probe_result == 0 && probe_backend == 0 && pending_entry == 0x1000);
#if TARGET_OS_IPHONE
        CHECK(maxps4_fex_engine_probe(first, &probe_result, &probe_backend) == -EAGAIN);
        CHECK(probe_result == 0 && probe_backend == 0);
        CHECK(maxps4_fex_engine_initialize(first) == -EACCES);
        CHECK(maxps4_fex_engine_stage(first) == MAXPS4_FEX_CONTEXT_CREATED);
#else
        CHECK(maxps4_fex_engine_probe(first, &probe_result, &probe_backend) == -EAGAIN);
        CHECK(probe_result == 0 && probe_backend == 0);
        CHECK(maxps4_fex_engine_initialize(first) == 0);
        CHECK(maxps4_fex_engine_stage(first) == MAXPS4_FEX_DISPATCHER_INITIALIZED);
        CHECK(maxps4_fex_engine_initialize(first) == 0);
        CHECK(maxps4_fex_engine_initialize(second) == 0);
        std::puts("PASS real InitCore dispatcher initialized twice without reallocation");
#endif
#if !TARGET_OS_IPHONE
        CHECK(maxps4_fex_engine_probe(first, nullptr, &probe_backend) == -EINVAL);
        CHECK(probe_backend == 0);
        uint64_t elf_entry = 99;
        auto elf = guest_test::elf(100, 23, true);
        CHECK(maxps4_fex_engine_run_elf(first, elf.data(), elf.size(), 3,
            &probe_result, &probe_backend, &elf_entry) == 0);
        CHECK(elf_entry == 0x1000 && probe_result == 123 && probe_backend == MAXPS4_CPU_FEX_ARM64_JIT);
        // Real FEX execution through the PS4-specific staging loader and
        // conservative ET_EXEC fixture adapter (not a full Orbis ABI).
        auto ps4_fixture = guest_test::elf(100, 23, true);
        ps4_fixture[7] = 9; // ELFOSABI FreeBSD/PS4
        guest_test::put(ps4_fixture, 0xfe00, 16, 2); // ET_SCE_EXEC
        CHECK(maxps4_fex_engine_run_ps4_fixture(first, ps4_fixture.data(),
            ps4_fixture.size(), 3, &probe_result, &probe_backend, &elf_entry) == 0);
        CHECK(elf_entry == 0x1000 && probe_result == 123 &&
            probe_backend == MAXPS4_CPU_FEX_ARM64_JIT);
        guest_test::put(ps4_fixture, 0xfe18, 16, 2); // PS4 shared library needs linker
        CHECK(maxps4_fex_engine_run_ps4_fixture(first, ps4_fixture.data(),
            ps4_fixture.size(), 3, &probe_result, &probe_backend, &elf_entry) != 0);
        CHECK(probe_result == 0 && probe_backend == 0);
        std::puts("PASS PS4-format staged ELF fixture -> bounded real FEX CPU result");
        auto pm4_elf = guest_test::elf(40, 2, true);
        pm4_elf.resize(207);
        guest_test::put(pm4_elf, 12, 152, 8);
        guest_test::put(pm4_elf, 0xc0012d00, 195, 4);
        guest_test::put(pm4_elf, 3, 199, 4);
        guest_test::put(pm4_elf, 2, 203, 4);
        uint32_t submitted[3] {99, 99, 99}; size_t written = 99;
        CHECK(maxps4_fex_engine_run_elf_pm4(first, pm4_elf.data(), pm4_elf.size(), 3,
            0x10000, 3, submitted, 3, &written, &probe_result, &probe_backend, &elf_entry) == 0);
        CHECK(written == 3 && submitted[0] == 0xc0012d00 && submitted[1] == 3 && submitted[2] == 2);
        CHECK(probe_result == 42 && probe_backend == MAXPS4_CPU_FEX_ARM64_JIT && elf_entry == 0x1000);
        submitted[0] = 99;
        for (uint64_t address : {uint64_t{0}, uint64_t{0x1003c}, uint64_t{0x10001}, UINT64_MAX - 3}) {
            CHECK(maxps4_fex_engine_run_elf_pm4(first, pm4_elf.data(), pm4_elf.size(), 3,
                address, 3, submitted, 3, &written, &probe_result, &probe_backend, &elf_entry) != 0);
            CHECK(written == 0 && submitted[0] == 99 && probe_result == 0 && probe_backend == 0);
        }
        CHECK(maxps4_fex_engine_run_elf_pm4(first, pm4_elf.data(), pm4_elf.size(), 3,
            0x10000, 3, submitted, 2, &written, &probe_result, &probe_backend, &elf_entry) == -EINVAL);
        CHECK(written == 0 && submitted[0] == 99);
        guest_test::put(pm4_elf, 0xc001ff00, 195, 4);
        CHECK(maxps4_fex_engine_run_elf_pm4(first, pm4_elf.data(), pm4_elf.size(), 3,
            0x10000, 3, submitted, 3, &written, &probe_result, &probe_backend, &elf_entry) == -ENOTSUP);
        CHECK(written == 0 && submitted[0] == 99 && probe_backend == 0);
        std::puts("PASS actual FEX execution -> same ELF arena PM4 snapshot; no CPU-generated commands claimed");
        for (uint32_t count : {3U, 6U, 3U}) {
            auto generated = guest_test::generated_pm4(count);
            CHECK(maxps4_fex_engine_run_elf_pm4(first, generated.data(), generated.size(), 6,
                0x10000, 3, submitted, 3, &written, &probe_result, &probe_backend, &elf_entry) == 0);
            CHECK(written == 3 && submitted[0] == 0xc0012d00 && submitted[1] == count && submitted[2] == 2);
            CHECK(probe_result == 42 && probe_backend == MAXPS4_CPU_FEX_ARM64_JIT);
            // The data segment had no file payload. These words came from real
            // x86 RIP-relative stores executed through FEX, not Swift or an interpreter.
            CHECK(maxps4_fex_engine_run_elf_pm4(first, generated.data(), generated.size(), 5,
                0x10000, 3, submitted, 3, &written, &probe_result, &probe_backend, &elf_entry) == -E2BIG);
            CHECK(written == 0 && probe_result == 0 && probe_backend == 0);
        }
        std::puts("PASS real FEX RIP-relative stores generate PM4 in initially zero-filled guest data");
        // Entry offset is honored; bytes preceding it must never execute.
        guest_test::put(elf, 0x1005, 24, 8);
        elf[189] = 0xB8;
        guest_test::put(elf, 77, 190, 4);
        CHECK(maxps4_fex_engine_run_elf(first, elf.data(), elf.size(), 2,
            &probe_result, &probe_backend, &elf_entry) == 0);
        CHECK(elf_entry == 0x1005 && probe_result == 77 && probe_backend == MAXPS4_CPU_FEX_ARM64_JIT);
        CHECK(maxps4_fex_engine_run_elf(first, elf.data(), elf.size(), 1,
            &probe_result, &probe_backend, &elf_entry) == -E2BIG);
        CHECK(probe_result == 0 && probe_backend == 0 && elf_entry == 0);
        elf[194] = 0xC3;
        CHECK(maxps4_fex_engine_run_elf(first, elf.data(), elf.size(), 3,
            &probe_result, &probe_backend, &elf_entry) == -ENOTSUP);
        CHECK(probe_result == 0 && probe_backend == 0);
        elf = guest_test::elf(UINT32_MAX, 2);
        CHECK(maxps4_fex_engine_run_elf(first, elf.data(), elf.size(), 3,
            &probe_result, &probe_backend, &elf_entry) == 0);
        CHECK(probe_result == 1 && probe_backend == MAXPS4_CPU_FEX_ARM64_JIT);
        // Reused mmap addresses must not run a cached prior ELF translation.
        for (uint32_t value = 50; value < 54; ++value) {
            elf = guest_test::elf(value, 9);
            CHECK(maxps4_fex_engine_run_elf(first, elf.data(), elf.size(), 3,
                &probe_result, &probe_backend, &elf_entry) == 0);
            CHECK(probe_result == value + 9 && probe_backend == MAXPS4_CPU_FEX_ARM64_JIT);
        }
        std::puts("PASS real ELF segments/entry -> FEX ARM64 JIT; varied RAX, overflow, admission and budget");
        for (unsigned probe = 0; probe != 2; ++probe) {
            std::printf("BEGIN real FEX ARM64 JIT fixed guest probe cycle=%u attempt=%u\n", repeat, probe);
            CHECK(maxps4_fex_engine_probe(first, &probe_result, &probe_backend) == 0);
            CHECK(probe_result == 42 && probe_backend == MAXPS4_CPU_FEX_ARM64_JIT);
            std::printf("PASS real FEX ARM64 JIT guest RAX=%llu backend=%u\n",
                static_cast<unsigned long long>(probe_result), probe_backend);
        }
#endif
        uint32_t cpuid[4]{};
        CHECK(maxps4_fex_engine_cpuid(first, 0, 0, cpuid) == 0);
        // Real FEX virtual CPU leaf, not an adapter-supplied fake CPUID response.
        char vendor[13]{};
        std::memcpy(vendor, &cpuid[1], 4);
        std::memcpy(vendor + 4, &cpuid[3], 4);
        std::memcpy(vendor + 8, &cpuid[2], 4);
        CHECK(cpuid[0] > 0 && std::strlen(vendor) == 12);
        std::printf("FEX context CPUID vendor=%s leaf=%u\n",vendor,cpuid[0]);
        maxps4_fex_engine_close(first);
        CHECK(maxps4_fex_engine_cpuid(second, 0, 0, cpuid) == 0);
        constexpr uint8_t block[] = {0xB8,40,0,0,0,0x05,2,0,0,0,0xC3};
        uint64_t result = 99; uint32_t backend = 99;
        CHECK(maxps4_fex_engine_run(second, block, sizeof(block), 32, &result, &backend) == 0);
        CHECK(result == 42 && backend == MAXPS4_CPU_RESTRICTED_INTERPRETER);
        CHECK(maxps4_fex_engine_run(second, block, sizeof(block), 1, &result, &backend) == -ENOTSUP);
        CHECK(result == 0 && backend == 0);
        constexpr uint8_t invalid[] = {0x0F,0x0B};
        CHECK(maxps4_fex_engine_run(second, invalid, sizeof(invalid), 32, &result, &backend) == -ENOTSUP);
        CHECK(result == 0 && backend == 0);
        CHECK(maxps4_fex_engine_run(nullptr, block, sizeof(block), 32, &result, &backend) == -EINVAL);
        CHECK(maxps4_fex_engine_cpuid(second, 0, 0, nullptr) == -EINVAL);
        maxps4_fex_engine_close(second);
        for (size_t i = 0; i < signal_numbers.size(); ++i) {
            struct sigaction action {};
            CHECK(::sigaction(signal_numbers[i], nullptr, &action) == 0);
            CHECK(action.sa_handler == original_actions[i].sa_handler);
            CHECK(action.sa_flags == original_actions[i].sa_flags);
            for (int signal = 1; signal < NSIG; ++signal)
                CHECK(sigismember(&action.sa_mask, signal) ==
                      sigismember(&original_actions[i].sa_mask, signal));
        }
        sigset_t mask {};
        CHECK(::pthread_sigmask(SIG_SETMASK, nullptr, &mask) == 0);
        for (int signal = 1; signal < NSIG; ++signal)
            CHECK(sigismember(&mask, signal) == sigismember(&original_mask, signal));
        std::puts("PASS runtime cleanup preserves host signal handlers and mask");
    }
    std::puts("PASS real FEX context lifecycle/CPUID and explicit interpreter fallback; device JIT NOT tested");
}

