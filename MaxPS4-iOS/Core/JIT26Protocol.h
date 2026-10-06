// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Universal StikJIT/StikDebug iOS 26 breakpoint protocol.
/// IMPORTANT: these functions must only be called after the external JIT
/// script is attached and CS_DEBUGGED is active. Executing BRK otherwise will
/// terminate the process.
void JIT26Detach(void);
void* JIT26PrepareRegion(void* address, size_t length);

#ifdef __cplusplus
}
#endif
