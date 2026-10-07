#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Stable boundary between the MaxPS4 iOS frontend and the PS4 emulation runtime.
// READY means the validated shadPS4/FEX guest backend is actually linked into
// the current binary. It does not claim that the PS4 executable loader is wired yet.

typedef enum MaxPS4BackendState {
    MAXPS4_BACKEND_NOT_LINKED = 0,
    MAXPS4_BACKEND_READY = 1,
    MAXPS4_BACKEND_ERROR = 2,
} MaxPS4BackendState;

MaxPS4BackendState maxps4_backend_state(void);
const char *maxps4_backend_name(void);
const char *maxps4_backend_diagnostic(void);
void maxps4_backend_live_diagnostic(char *out, size_t out_size);
bool maxps4_backend_self_test(void);
// Validates an imported PS4 SELF/ELF executable using the same structural
// requirements as the locked shadPS4 loader before any runtime handoff.
bool maxps4_backend_validate_executable(const char *path);
bool maxps4_backend_boot(const char *path);
void maxps4_backend_stop(void);

#ifdef __cplusplus
}
#endif
