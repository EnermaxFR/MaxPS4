#include "maxps4_fex_runtime.h"
#include "maxps4_native.h"
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
#if TARGET_OS_IPHONE
        CHECK(maxps4_fex_engine_probe(first, &probe_result, &probe_backend) == -ENOTSUP);
        CHECK(probe_result == 0 && probe_backend == 0);
        CHECK(maxps4_fex_engine_initialize(first) == -ENOTSUP);
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
                CHECK(::sigismember(&action.sa_mask, signal) ==
                      ::sigismember(&original_actions[i].sa_mask, signal));
        }
        sigset_t mask {};
        CHECK(::pthread_sigmask(SIG_SETMASK, nullptr, &mask) == 0);
        for (int signal = 1; signal < NSIG; ++signal)
            CHECK(::sigismember(&mask, signal) == ::sigismember(&original_mask, signal));
        std::puts("PASS runtime cleanup preserves host signal handlers and mask");
    }
    std::puts("PASS real FEX context lifecycle/CPUID and explicit interpreter fallback; device JIT NOT tested");
}
