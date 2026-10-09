#include <cstddef>
#include <cstdint>
#include <cstdio>

extern "C" int maxps4_arm64_translate_preview(const std::uint8_t*, std::size_t,
    std::uint32_t*, std::size_t, std::size_t*) noexcept;
extern "C" int maxps4_arm64_verify_preview(const std::uint32_t*, std::size_t) noexcept;
extern "C" int maxps4_native_guest_run_with_backend(const std::uint8_t*, std::size_t,
    std::uint32_t, int, int*, std::uint64_t*) noexcept;
extern "C" int maxps4_native_arm64_jit_ready() noexcept;

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
int main() {
    constexpr std::uint8_t add[] = {0xB8,40,0,0,0,0x05,2,0,0,0,0xC3};
    constexpr std::uint8_t loop[] = {0xB8,3,0,0,0,0x2D,1,0,0,0,0x75,0xF9,0xC3};
    constexpr std::uint8_t invalid[] = {0x0F,0x05};
    std::uint32_t words[16] = {};
    std::size_t emitted = 0;
    const std::uint32_t invalid_words[] = {0xFFFFFFFF,0xD65F03C0};
    const bool checks =
        run("add", add, sizeof(add), 42) &&
        run("loop", loop, sizeof(loop), 0) &&
        !maxps4_arm64_translate_preview(invalid, sizeof(invalid), words, 16, &emitted) &&
        !maxps4_arm64_verify_preview(nullptr, 0) &&
        !maxps4_arm64_verify_preview(invalid_words, 2) &&
        maxps4_native_arm64_jit_ready() == 0;
    std::puts(checks ? "PASS JIT pipeline regression" : "FAIL JIT pipeline regression");
    return checks ? 0 : 1;
}
