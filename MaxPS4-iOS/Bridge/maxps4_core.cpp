#include "maxps4_core.h"

#include <sys/mman.h>
#include <unistd.h>
#include <cstring>

#if defined(__APPLE__)
#include <libkern/OSCacheControl.h>
#endif

const char *maxps4_core_version(void) {
    return "MaxPS4 iOS core bridge 0.2 JIT probe";
}

bool maxps4_core_is_available(void) {
    // The ABI is present, but the shadPS4/FEXCore runtime is not linked yet.
    return false;
}

bool maxps4_core_jit_available(void) {
#if defined(__APPLE__) && defined(__aarch64__)
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) return false;

    int flags = MAP_PRIVATE | MAP_ANON;
#ifdef MAP_JIT
    flags |= MAP_JIT;
#endif

    // This is deliberately stronger than the old allocation-only probe:
    // allocate writable memory, place a tiny ARM64 function in it, switch the
    // page to RX, flush the instruction cache, then execute it. If iOS or the
    // container has not granted executable/JIT memory, one of these operations
    // fails and we report JIT unavailable.
    void *p = mmap(nullptr, (size_t)pageSize, PROT_READ | PROT_WRITE,
                   flags, -1, 0);
    if (p == MAP_FAILED) return false;

    // mov w0, #1 ; ret
    const unsigned int code[] = { 0x52800020u, 0xD65F03C0u };
    std::memcpy(p, code, sizeof(code));

    if (mprotect(p, (size_t)pageSize, PROT_READ | PROT_EXEC) != 0) {
        munmap(p, (size_t)pageSize);
        return false;
    }

    sys_icache_invalidate(p, sizeof(code));
    using ProbeFn = int (*)(void);
    const int result = reinterpret_cast<ProbeFn>(p)();
    munmap(p, (size_t)pageSize);
    return result == 1;
#else
    return false;
#endif
}

MaxPS4CoreResult maxps4_core_boot_game(const char *path) {
    if (path == nullptr || path[0] == '\0') {
        return MAXPS4_CORE_INVALID_PATH;
    }
    if (!maxps4_core_jit_available()) {
        return MAXPS4_CORE_JIT_UNAVAILABLE;
    }
    // Next integration stage: hand this path to the iOS shadPS4 runtime.
    return MAXPS4_CORE_NOT_READY;
}

void maxps4_core_stop(void) {
    // Reserved for the emulator runtime shutdown path.
}
