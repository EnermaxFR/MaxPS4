#include "maxps4_backend.h"
#include "maxps4_ps4_loader.h"

#include <cstdio>

#if defined(MAXPS4_HAS_SHADPS4_FEX)
extern "C" int maxps4_fex_guest_harness_run(void);
extern "C" const char* maxps4_fex_guest_last_error(void);
#endif

static char g_backend_diagnostic[256] =
#if defined(MAXPS4_HAS_SHADPS4_FEX)
    "shadPS4/FEX guest backend linked • on-device self-test not run";
#else
    "FEXCore ARM64 validated • shadPS4 guest backend staging";
#endif

MaxPS4BackendState maxps4_backend_state(void) {
#if defined(MAXPS4_HAS_SHADPS4_FEX)
    return MAXPS4_BACKEND_READY;
#else
    return MAXPS4_BACKEND_NOT_LINKED;
#endif
}

const char *maxps4_backend_name(void) {
#if defined(MAXPS4_HAS_SHADPS4_FEX)
    return "shadPS4/FEXCore ARM64 guest backend";
#else
    return "FEXCore ARM64 validated • shadPS4 staging";
#endif
}

const char *maxps4_backend_diagnostic(void) {
    return g_backend_diagnostic;
}

bool maxps4_backend_self_test(void) {
#if defined(MAXPS4_HAS_SHADPS4_FEX)
    const int result = maxps4_fex_guest_harness_run();
    const char *detail = maxps4_fex_guest_last_error();
    if (result == 0) {
        std::snprintf(g_backend_diagnostic, sizeof(g_backend_diagnostic),
                      "shadPS4/FEX guest backend self-test OK");
        return true;
    }
    std::snprintf(g_backend_diagnostic, sizeof(g_backend_diagnostic),
                  "shadPS4/FEX self-test FAIL code=%d • %s",
                  result, detail ? detail : "no detail");
    return false;
#else
    std::snprintf(g_backend_diagnostic, sizeof(g_backend_diagnostic),
                  "shadPS4/FEX guest backend not linked");
    return false;
#endif
}

bool maxps4_backend_validate_executable(const char *path) {
    MaxPS4ExecutableInfo info{};
    char loader_diagnostic[192]{};
    const bool ok = maxps4_ps4_loader_validate(path, &info, loader_diagnostic,
                                                sizeof(loader_diagnostic));
    std::snprintf(g_backend_diagnostic, sizeof(g_backend_diagnostic),
                  "%s", loader_diagnostic);
    return ok;
}

bool maxps4_backend_boot(const char *path) {
    if (!maxps4_backend_validate_executable(path)) {
        return false;
    }
#if defined(MAXPS4_HAS_SHADPS4_FEX)
    char validated[sizeof(g_backend_diagnostic)]{};
    std::snprintf(validated, sizeof(validated), "%s", g_backend_diagnostic);
    std::snprintf(g_backend_diagnostic, sizeof(g_backend_diagnostic),
                  "%s • shadPS4 runtime handoff pending", validated);
    return false;
#else
    return false;
#endif
}

void maxps4_backend_stop(void) {
}
