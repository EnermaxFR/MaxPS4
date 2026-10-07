#include "maxps4_backend.h"
#include "maxps4_ps4_loader.h"

#include <cstdio>

#if defined(MAXPS4_HAS_SHADPS4_FEX)
extern "C" int maxps4_fex_guest_harness_run(void);
extern "C" const char* maxps4_fex_guest_last_error(void);
extern "C" int maxps4_fex_guest_run_elf(const char* path);
extern "C" const char* maxps4_fex_guest_run_last_error(void);
extern "C" void maxps4_fex_guest_run_live_diagnostic(char* out, size_t out_size);
extern "C" void maxps4_fex_guest_run_live_output(char* out, size_t out_size);
extern "C" void maxps4_fex_set_controller_state(unsigned int buttons,
                                                  float left_x, float left_y,
                                                  float right_x, float right_y,
                                                  float left_trigger, float right_trigger);
extern "C" bool maxps4_fex_get_guest_render_state(MaxPS4GuestRenderState* out);
extern "C" bool maxps4_fex_get_guest_frame(MaxPS4GuestFrame* out);
extern "C" bool maxps4_fex_get_guest_scene_frame(MaxPS4GuestSceneFrame* out);
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

void maxps4_backend_live_diagnostic(char *out, size_t out_size) {
    if (!out || out_size == 0) return;
#if defined(MAXPS4_HAS_SHADPS4_FEX)
    maxps4_fex_guest_run_live_diagnostic(out, out_size);
#else
    std::snprintf(out, out_size, "%s", g_backend_diagnostic);
#endif
}

void maxps4_backend_live_output(char *out, size_t out_size) {
    if (!out || out_size == 0) return;
#if defined(MAXPS4_HAS_SHADPS4_FEX)
    maxps4_fex_guest_run_live_output(out, out_size);
#else
    out[0] = '\0';
#endif
}

void maxps4_backend_set_controller_state(unsigned int buttons,
                                         float left_x, float left_y,
                                         float right_x, float right_y,
                                         float left_trigger, float right_trigger) {
#if defined(MAXPS4_HAS_SHADPS4_FEX)
    maxps4_fex_set_controller_state(buttons, left_x, left_y,
                                    right_x, right_y,
                                    left_trigger, right_trigger);
#else
    (void)buttons; (void)left_x; (void)left_y;
    (void)right_x; (void)right_y;
    (void)left_trigger; (void)right_trigger;
#endif
}

bool maxps4_backend_guest_render_state(MaxPS4GuestRenderState *out) {
    if (!out) return false;
#if defined(MAXPS4_HAS_SHADPS4_FEX)
    return maxps4_fex_get_guest_render_state(out);
#else
    *out = {};
    return false;
#endif
}

bool maxps4_backend_guest_frame(MaxPS4GuestFrame *out) {
    if (!out) return false;
#if defined(MAXPS4_HAS_SHADPS4_FEX)
    return maxps4_fex_get_guest_frame(out);
#else
    *out = {};
    return false;
#endif
}

bool maxps4_backend_guest_scene_frame(MaxPS4GuestSceneFrame *out) {
    if (!out) return false;
#if defined(MAXPS4_HAS_SHADPS4_FEX)
    return maxps4_fex_get_guest_scene_frame(out);
#else
    *out = {};
    return false;
#endif
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
    const int rc = maxps4_fex_guest_run_elf(path);
    const char* detail = maxps4_fex_guest_run_last_error();
    std::snprintf(g_backend_diagnostic, sizeof(g_backend_diagnostic),
                  "%s", detail ? detail : "FEX guest runner: no detail");
    return rc == 0;
#else
    return false;
#endif
}

void maxps4_backend_stop(void) {
}
