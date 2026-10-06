#include "maxps4_core.h"

#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <cstdio>

#if defined(__APPLE__)
#include <libkern/OSCacheControl.h>
#endif

static char g_jit_diagnostic[160] = "JIT probe not run";

const char *maxps4_core_version(void) {
    return "MaxPS4 iOS core bridge 0.3 JIT diagnostics";
}

bool maxps4_core_is_available(void) {
    return false;
}

const char *maxps4_core_jit_diagnostic(void) {
    return g_jit_diagnostic;
}

bool maxps4_core_jit_available(void) {
#if defined(__APPLE__) && defined(__aarch64__)
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) {
        std::snprintf(g_jit_diagnostic, sizeof(g_jit_diagnostic), "FAIL page size");
        return false;
    }

    int flags = MAP_PRIVATE | MAP_ANON;
#ifdef MAP_JIT
    flags |= MAP_JIT;
#endif

    errno = 0;
    void *p = mmap(nullptr, (size_t)pageSize, PROT_READ | PROT_WRITE,
                   flags, -1, 0);
    if (p == MAP_FAILED) {
        std::snprintf(g_jit_diagnostic, sizeof(g_jit_diagnostic),
                      "FAIL mmap MAP_JIT errno=%d (%s)", errno, std::strerror(errno));
        return false;
    }

    const unsigned int code[] = { 0x52800020u, 0xD65F03C0u }; // mov w0,#1 ; ret
    std::memcpy(p, code, sizeof(code));

    errno = 0;
    if (mprotect(p, (size_t)pageSize, PROT_READ | PROT_EXEC) != 0) {
        const int savedErrno = errno;
        munmap(p, (size_t)pageSize);
        std::snprintf(g_jit_diagnostic, sizeof(g_jit_diagnostic),
                      "FAIL mprotect RX errno=%d (%s)", savedErrno, std::strerror(savedErrno));
        return false;
    }

    sys_icache_invalidate(p, sizeof(code));
    std::snprintf(g_jit_diagnostic, sizeof(g_jit_diagnostic),
                  "RX ready; executing ARM64 probe");

    using ProbeFn = int (*)(void);
    const int result = reinterpret_cast<ProbeFn>(p)();
    munmap(p, (size_t)pageSize);

    if (result != 1) {
        std::snprintf(g_jit_diagnostic, sizeof(g_jit_diagnostic),
                      "FAIL ARM64 returned %d", result);
        return false;
    }

    std::snprintf(g_jit_diagnostic, sizeof(g_jit_diagnostic),
                  "OK mmap + RX + ARM64 execution");
    return true;
#else
    std::snprintf(g_jit_diagnostic, sizeof(g_jit_diagnostic),
                  "FAIL unsupported architecture/platform");
    return false;
#endif
}

MaxPS4CoreResult maxps4_core_boot_game(const char *path) {
    if (path == nullptr || path[0] == '\0') return MAXPS4_CORE_INVALID_PATH;
    if (!maxps4_core_jit_available()) return MAXPS4_CORE_JIT_UNAVAILABLE;
    return MAXPS4_CORE_NOT_READY;
}

void maxps4_core_stop(void) {
}
