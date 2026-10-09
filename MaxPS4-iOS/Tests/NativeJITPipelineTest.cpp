#include <cstddef>
#include <cstdint>
#include <cstdio>

extern "C" int maxps4_arm64_translate_preview(const std::uint8_t*, std::size_t,
    std::uint32_t*, std::size_t, std::size_t*) noexcept;
extern "C" int maxps4_arm64_verify_preview(const std::uint32_t*, std::size_t) noexcept;
extern "C" int maxps4_native_guest_run_with_backend(const std::uint8_t*, std::size_t,
    std::uint32_t, int, int*, std::uint64_t*) noexcept;
extern "C" int maxps4_native_arm64_jit_ready() noexcept;
extern "C" int maxps4_native_arm64_static_execute_probe() noexcept;

static bool run(const char* label, const std::uint8_t* input, std::size_t length,
                std::uint64_t expected) {
    std::uint32_t words[256] = {};
    std::size_t emitted = 0;
    if (!maxps4_arm64_translate_preview(input, length, words, 256, &emitted) ||
        !maxps4_arm64_verify_preview(words, emitted)) {
        std::fprintf(stderr, "FAIL %s: translation or verification\n", label);
        return false;
    }
    int used_jit = -1;
    std::uint64_t result = 0;
    if (!maxps4_native_guest_run_with_backend(input, length, 64, 1,
                                               &used_jit, &result) ||
        used_jit != 0 || result != expected) {
        std::fprintf(stderr, "FAIL %s: interpreter fallback\n", label);
        return false;
    }
    std::printf("PASS %s: %zu ARM64 words (data only), result=%llu\n",
                label, emitted, static_cast<unsigned long long>(result));
    return true;
}
// Invalid guest bytecode must never be reported as a successful JIT run.
static bool reject_invalid_guest(const std::uint8_t* input, std::size_t length) {
    int used_jit = -1;
    std::uint64_t result = 0;
    return maxps4_native_guest_run_with_backend(input, length, 64, 1,
                                                &used_jit, &result) == 0 &&
           used_jit == 0 && maxps4_native_arm64_jit_ready() == 0;
}
// A nonterminating guest loop must stop at its instruction budget; never
// return a result or claim execution by an unavailable dynamic JIT.
static bool reject_exhausted_budget() {
    constexpr std::uint8_t endless[] = {0xB8, 1, 0, 0, 0, 0x2D, 0, 0, 0, 0, 0x75, 0xF9};
    int used_jit = -1;
    std::uint64_t result = 0;
    return maxps4_native_guest_run_with_backend(endless, sizeof(endless), 12, 1,
                                                &used_jit, &result) == 0 &&
           used_jit == 0;
}
int main() {
    constexpr std::uint8_t add[] = {0xB8,40,0,0,0,0x05,2,0,0,0,0xC3};
    constexpr std::uint8_t loop[] = {0xB8,3,0,0,0,0x2D,1,0,0,0,0x75,0xF9,0xC3};
    constexpr std::uint8_t xor_mask[] = {0xB8,0xFF,0,0,0,0x35,0xF0,0,0,0,0xC3};
    constexpr std::uint8_t and_mask[] = {0xB8,0xFF,0,0,0,0x25,0x0F,0,0,0,0xC3};
    constexpr std::uint8_t or_mask[] = {0xB8,0x01,0,0,0,0x0D,0x10,0,0,0,0xC3};
    constexpr std::uint8_t large_add[] = {0xB8,0x01,0,0,0,0x05,0x00,0x10,0,0,0xC3};
    constexpr std::uint8_t cmp_branch[] = {
        0xB8,7,0,0,0,0x3D,7,0,0,0,0x74,0x05,
        0xB8,0,0,0,0,0xC3
    };
    constexpr std::uint8_t invalid_jump[] = {0xB8,1,0,0,0,0x3D,1,0,0,0,0x74,0x7F,0xC3};
    constexpr std::uint8_t invalid[] = {0x0F,0x05};
    std::uint32_t words[16] = {};
    std::size_t emitted = 0;
    const std::uint32_t invalid_words[] = {0xFFFFFFFF,0xD65F03C0};
    const bool checks =
        run("add", add, sizeof(add), 42) &&
        run("loop", loop, sizeof(loop), 0) &&
        run("xor", xor_mask, sizeof(xor_mask), 15) &&
        run("and", and_mask, sizeof(and_mask), 15) &&
        run("or", or_mask, sizeof(or_mask), 17) &&
        run("large immediate", large_add, sizeof(large_add), 4097) &&
        run("comparison/branch", cmp_branch, sizeof(cmp_branch), 7) &&
        !maxps4_arm64_translate_preview(invalid_jump, sizeof(invalid_jump), words, 16, &emitted) &&
        !maxps4_arm64_translate_preview(invalid, sizeof(invalid), words, 16, &emitted) &&
        !maxps4_arm64_verify_preview(nullptr, 0) &&
        !maxps4_arm64_verify_preview(invalid_words, 2) &&
        reject_invalid_guest(invalid, sizeof(invalid)) &&
        reject_invalid_guest(nullptr, 0) &&
        // Reject invalid backend selectors rather than silently falling back.
        ([] {
            constexpr std::uint8_t ret[] = {0xC3};
            int used_jit = -1;
            std::uint64_t result = 0;
            return maxps4_native_guest_run_with_backend(ret, sizeof(ret), 64, 2,
                                                        &used_jit, &result) == 0;
        }()) &&
        reject_exhausted_budget() &&
        ([] {
            constexpr std::uint8_t ret[] = {0xC3};
            int used_jit = -1;
            std::uint64_t result = 0;
            return maxps4_native_guest_run_with_backend(ret, sizeof(ret), 0, 1,
                                                        &used_jit, &result) == 0 &&
                   used_jit == 0;
        }()) &&
        maxps4_native_arm64_jit_ready() == 0 &&
#if defined(__aarch64__)
        maxps4_native_arm64_static_execute_probe() == 1;
#else
        maxps4_native_arm64_static_execute_probe() == 0;
#endif
    std::puts(checks ? "PASS JIT pipeline regression" : "FAIL JIT pipeline regression");
    return checks ? 0 : 1;
}
