#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Stable boundary between the MaxPS4 iOS frontend and the PS4 emulation runtime.
// The first 0.5 milestone deliberately keeps shadPS4/FEX behind this interface so
// upstream code can be integrated with its provenance and license notices intact.

typedef enum MaxPS4BackendState {
    MAXPS4_BACKEND_NOT_LINKED = 0,
    MAXPS4_BACKEND_READY = 1,
    MAXPS4_BACKEND_ERROR = 2,
} MaxPS4BackendState;

MaxPS4BackendState maxps4_backend_state(void);
const char *maxps4_backend_name(void);
const char *maxps4_backend_diagnostic(void);
bool maxps4_backend_boot(const char *path);
void maxps4_backend_stop(void);

#ifdef __cplusplus
}
#endif
