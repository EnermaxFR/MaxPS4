// Host-only dynamic ARM64 execution proof. This is NOT FEXCore,
// a PS4 emulator, nor an iOS JIT entitlement test.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

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

    // mov w0, #42; ret. These instruction bytes are generated test data,
    // not compiler-emitted function instructions.
    constexpr std::uint32_t code[] = {0x52800540u, 0xD65F03C0u};
    std::memcpy(page, code, sizeof(code));
    __builtin___clear_cache(static_cast<char*>(page),
                            static_cast<char*>(page) + sizeof(code));

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
