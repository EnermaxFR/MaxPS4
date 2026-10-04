#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// MaxPS4-owned boundary to the external emulator core.
// The implementation is linked against shadps4_ios during CI.
int maxps4_core_available(void);

#ifdef __cplusplus
}
#endif
