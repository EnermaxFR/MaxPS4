#include "maxps4_backend.h"

// 0.5 staging implementation. This is intentionally not a fake emulator core:
// READY is only returned once the real shadPS4/FEX runtime is linked into the
// iOS build. Keeping NOT_LINKED here makes the UI/build diagnostics truthful.

MaxPS4BackendState maxps4_backend_state(void) {
#if defined(MAXPS4_HAS_SHADPS4_FEX)
    return MAXPS4_BACKEND_READY;
#else
    return MAXPS4_BACKEND_NOT_LINKED;
#endif
}

const char *maxps4_backend_name(void) {
#if defined(MAXPS4_HAS_SHADPS4_FEX)
    return "shadPS4 + FEXCore ARM64";
#else
    return "FEXCore ARM64 validated • shadPS4 staging";
#endif
}

const char *maxps4_backend_diagnostic(void) {
#if defined(MAXPS4_HAS_SHADPS4_FEX)
    return "Backend linked; runtime initialization available";
#else
    return "0.6 FEXCore guest execution validated on-device; shadPS4 runtime not linked yet";
#endif
}

bool maxps4_backend_boot(const char *path) {
    if (path == nullptr || path[0] == '\0') return false;
#if defined(MAXPS4_HAS_SHADPS4_FEX)
    // Real runtime entry point is wired here when the licensed upstream objects
    // are part of the build. Do not report success before that integration.
    return false;
#else
    return false;
#endif
}

void maxps4_backend_stop(void) {
}
