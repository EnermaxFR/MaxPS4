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
    MAXPS4_EXEC_PKG = 3,
} MaxPS4ExecutableKind;
typedef struct MaxPS4ExecutableInfo {
    MaxPS4ExecutableKind kind;
    uint16_t type;
    uint64_t entry;
    uint16_t program_header_count;

    // Populated for a recognized PS4 PKG container.
    uint32_t package_flags;
    uint32_t package_file_count;
    uint32_t package_table_entry_count;
    uint32_t package_drm_type;
    uint32_t package_content_type;
    uint64_t package_content_size;
    char package_content_id[37];
} MaxPS4ExecutableInfo;
bool maxps4_ps4_loader_validate(const char *path, MaxPS4ExecutableInfo *out_info,
                                char *diagnostic, unsigned long diagnostic_size);

// Conservative PKG path: copies only a directly stored, already-readable
// ELF/SELF entry from the declared PKG file table. No keys, DRM bypass,
// PFS decryption, or encrypted-content extraction is performed.
bool maxps4_ps4_loader_extract_plain_pkg_executable(
    const char *pkg_path, const char *output_path,
    char *diagnostic, unsigned long diagnostic_size);
#ifdef __cplusplus
}
#endif
