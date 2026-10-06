#ifndef MAXPS4_CORE_H
#define MAXPS4_CORE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MaxPS4CoreResult {
    MAXPS4_CORE_OK = 0,
    MAXPS4_CORE_NOT_READY = 1,
    MAXPS4_CORE_INVALID_PATH = 2,
    MAXPS4_CORE_JIT_UNAVAILABLE = 3,
    MAXPS4_CORE_START_FAILED = 4
} MaxPS4CoreResult;

const char *maxps4_core_version(void);
bool maxps4_core_is_available(void);
bool maxps4_core_jit_available(void);
const char *maxps4_core_jit_diagnostic(void);
MaxPS4CoreResult maxps4_core_boot_game(const char *path);
void maxps4_core_stop(void);

#ifdef __cplusplus
}
#endif

#endif
