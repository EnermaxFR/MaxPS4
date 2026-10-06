// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum MaxPS4ExecutableKind {
    MAXPS4_EXEC_INVALID = 0,
    MAXPS4_EXEC_ELF = 1,
    MAXPS4_EXEC_SELF = 2,
} MaxPS4ExecutableKind;
typedef struct MaxPS4ExecutableInfo {
    MaxPS4ExecutableKind kind;
    uint16_t type;
    uint64_t entry;
    uint16_t program_header_count;
} MaxPS4ExecutableInfo;
bool maxps4_ps4_loader_validate(const char *path, MaxPS4ExecutableInfo *out_info,
                                char *diagnostic, unsigned long diagnostic_size);
#ifdef __cplusplus
}
#endif
