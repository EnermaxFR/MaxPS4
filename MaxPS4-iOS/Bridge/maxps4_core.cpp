#include "maxps4_core.h"

#include <sys/mman.h>
#include <unistd.h>

const char *maxps4_core_version(void) {
    return "MaxPS4 iOS core bridge 0.1";
}

bool maxps4_core_is_available(void) {
    // The ABI is present, but the shadPS4/FEXCore runtime is not linked yet.
    return false;
}

bool maxps4_core_jit_available(void) {
#if defined(__APPLE__) && defined(__aarch64__)
#ifdef MAP_JIT
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) return false;
    void *p = mmap(nullptr, (size_t)pageSize, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    if (p == MAP_FAILED) return false;
    munmap(p, (size_t)pageSize);
    return true;
#else
    return false;
#endif
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
