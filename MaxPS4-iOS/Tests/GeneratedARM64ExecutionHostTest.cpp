// Host-only dynamic ARM64 execution proof. This is NOT FEXCore,
// a PS4 emulator, nor an iOS JIT entitlement test.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

extern "C" int maxps4_arm64_translate_preview(const std::uint8_t*, std::size_t,
    std::uint32_t*, std::size_t, std::size_t*) noexcept;
extern "C" int maxps4_arm64_verify_preview(const std::uint32_t*, std::size_t) noexcept;

int main() {
#if !defined(__APPLE__) || !defined(__aarch64__)
    std::puts("SKIPPED: requires Apple Silicon macOS");
    return 0;
#else
    const long length = sysconf(_SC_PAGESIZE);
    if (length <= 0) {
        std::fputs("FAIL: cannot determine page size\n", stderr);
        return 1;
    }

    void* page = mmap(nullptr, static_cast<std::size_t>(length),
                      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (page == MAP_FAILED) {
        std::fputs("FAIL: writable memory unavailable\n", stderr);
        return 1;
    }

    // Translate actual x86 instructions with MaxPS4's experimental translator:
    // MOV EAX, 40; ADD EAX, 2; RET.
    constexpr std::uint8_t guest[] = {0xB8, 40, 0, 0, 0, 0x05, 2, 0, 0, 0, 0xC3};
    std::uint32_t translated[64] = {};
    std::size_t count = 0;
    if (!maxps4_arm64_translate_preview(guest, sizeof(guest), translated, 64, &count) ||
        !maxps4_arm64_verify_preview(translated, count) ||
        count == 0 || count > static_cast<std::size_t>(length) / sizeof(std::uint32_t)) {
        std::fputs("FAIL: MaxPS4 x86-to-ARM64 translation or verification failed\n", stderr);
        munmap(page, static_cast<std::size_t>(length));
        return 1;
    }
    std::memcpy(page, translated, count * sizeof(std::uint32_t));
    __builtin___clear_cache(static_cast<char*>(page),
                            static_cast<char*>(page) + count * sizeof(std::uint32_t));

    // Never use RWX: switch writable memory to read+execute on the host.
    if (mprotect(page, static_cast<std::size_t>(length),
                 PROT_READ | PROT_EXEC) != 0) {
        std::fputs("FAIL: executable memory denied on host\n", stderr);
        munmap(page, static_cast<std::size_t>(length));
        return 1;
    }

    using GeneratedFunction = int (*)();
    const int result = reinterpret_cast<GeneratedFunction>(page)();
    if (munmap(page, static_cast<std::size_t>(length)) != 0) {
        std::fputs("FAIL: executable page cleanup failed\n", stderr);
        return 1;
    }
    if (result != 42) {
        std::fprintf(stderr, "FAIL: generated ARM64 returned %d, expected 42\n", result);
        return 1;
    }
    std::puts("PASS: generated ARM64 instructions executed and returned 42 on macOS host");
    std::puts("This does not prove JIT execution on iOS or PS4-game compatibility.");
    return 0;
#endif
}
