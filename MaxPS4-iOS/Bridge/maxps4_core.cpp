#include "maxps4_core.h"\n#include "maxps4_backend.h"

#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <cstdint>

#if defined(__APPLE__)
#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#endif

static char g_jit_diagnostic[192] = "JIT probe not run";

const char *maxps4_core_version(void) {
    return "MaxPS4 iOS core bridge 0.4 StikDebug JIT26";
}

bool maxps4_core_is_available(void) {
    return false;
}

const char *maxps4_core_jit_diagnostic(void) {
    return g_jit_diagnostic;
}

#if defined(__APPLE__) && defined(__aarch64__)
extern "C" int csops(pid_t pid, unsigned int ops, void *useraddr, size_t usersize);

static bool debugger_attached(void) {
    uint32_t flags = 0;
    if (csops(getpid(), 0, &flags, sizeof(flags)) != 0) return false;
    return (flags & 0x10000000u) != 0; // CS_DEBUGGED
}

__attribute__((noinline, optnone, naked))
static void *jit26_prepare_region(void *address, size_t length) {
    __asm__ volatile(
        "mov x16, #1\n"
        "brk #0xf00d\n"
        "ret\n"
    );
}

static bool usable_region(void *p) {
    const uintptr_t value = reinterpret_cast<uintptr_t>(p);
    return value != 0 && value != UINTPTR_MAX && (value & 0x3fffu) == 0;
}
#endif

bool maxps4_core_jit_available(void) {
#if defined(__APPLE__) && defined(__aarch64__)
    if (!debugger_attached()) {
        std::snprintf(g_jit_diagnostic, sizeof(g_jit_diagnostic),
                      "WAIT StikDebug universal.js (debugger not attached)");
        return false;
    }

    const size_t regionSize = 16 * 1024;
    void *rx = jit26_prepare_region(nullptr, regionSize);
    if (!usable_region(rx)) {
        std::snprintf(g_jit_diagnostic, sizeof(g_jit_diagnostic),
                      "FAIL StikDebug JIT26 prepare region (%p)", rx);
        return false;
    }

    vm_address_t rw = 0;
    vm_prot_t currentProtection = VM_PROT_NONE;
    vm_prot_t maximumProtection = VM_PROT_NONE;
    kern_return_t kr = vm_remap(mach_task_self(), &rw,
                                static_cast<vm_size_t>(regionSize), 0,
                                VM_FLAGS_ANYWHERE, mach_task_self(),
                                reinterpret_cast<vm_address_t>(rx), FALSE,
                                &currentProtection, &maximumProtection,
                                VM_INHERIT_NONE);
    if (kr != KERN_SUCCESS) {
        std::snprintf(g_jit_diagnostic, sizeof(g_jit_diagnostic),
                      "FAIL vm_remap kr=%d rx=%p", static_cast<int>(kr), rx);
        return false;
    }

    kr = vm_protect(mach_task_self(), rw, static_cast<vm_size_t>(regionSize),
                    FALSE, VM_PROT_READ | VM_PROT_WRITE);
    if (kr != KERN_SUCCESS) {
        vm_deallocate(mach_task_self(), rw, static_cast<vm_size_t>(regionSize));
        std::snprintf(g_jit_diagnostic, sizeof(g_jit_diagnostic),
                      "FAIL writable alias kr=%d", static_cast<int>(kr));
        return false;
    }

    const unsigned int code[] = { 0x52800020u, 0xD65F03C0u }; // mov w0,#1 ; ret
    std::memcpy(reinterpret_cast<void *>(rw), code, sizeof(code));
    sys_icache_invalidate(rx, sizeof(code));

    using ProbeFn = int (*)(void);
    const int result = reinterpret_cast<ProbeFn>(rx)();
    vm_deallocate(mach_task_self(), rw, static_cast<vm_size_t>(regionSize));

    if (result != 1) {
        std::snprintf(g_jit_diagnostic, sizeof(g_jit_diagnostic),
                      "FAIL JIT26 ARM64 returned %d", result);
        return false;
    }

    std::snprintf(g_jit_diagnostic, sizeof(g_jit_diagnostic),
                  "OK StikDebug universal.js + RX/RW + ARM64");
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
    if (!maxps4_backend_validate_executable(path)) return MAXPS4_CORE_INVALID_PATH;
    if (!maxps4_backend_boot(path)) return MAXPS4_CORE_NOT_READY;
    return MAXPS4_CORE_OK;
}

void maxps4_core_stop(void) {
}
