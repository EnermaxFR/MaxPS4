#include "maxps4_fex_runtime.h"
#include "maxps4_native.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::puts("BEGIN native ABI and real FEX context lifecycle test");
    CHECK(maxps4_fex_engine_open(nullptr) == -EINVAL);
    CHECK(maxps4_fex_engine_stage(nullptr) == 0);
    maxps4_fex_engine_close(nullptr);
    CHECK(maxps4_native_capabilities() == 0);
    CHECK(maxps4_native_launch("probe") == -38);
    CHECK(maxps4_native_aether_cpu_probe() == 1);
    for (unsigned repeat = 0; repeat != 3; ++repeat) {
        maxps4_fex_engine *first = nullptr, *second = nullptr;
        std::printf("OPEN contexts cycle=%u\n", repeat);
        CHECK(maxps4_fex_engine_open(&first) == 0 && first);
        CHECK(maxps4_fex_engine_open(&second) == 0 && second);
        CHECK(maxps4_fex_engine_stage(first) == MAXPS4_FEX_CONTEXT_CREATED);
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
    }
    std::puts("PASS real FEX context lifecycle/CPUID and explicit interpreter fallback; FEX JIT NOT executed");
}
