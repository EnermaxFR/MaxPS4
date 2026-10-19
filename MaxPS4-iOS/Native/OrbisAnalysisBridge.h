#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// Always NUL-terminated for valid output/capacity. Negative errno on rejection,
// I/O failure or truncation. Includes temporary native preparation in ordinary
// data vectors; never executes, enters FEX or allocates host executable memory.
// Status describes inspection; preparation outcome is explicit in the report.
int maxps4_orbis_analysis_report(const char* path, char* output, size_t capacity);
// Bounded recursive read-only inventory of an authorized plaintext game folder.
// Does not follow symlinks, extract PKG/PFS, persist files, or enter FEX. Zero
// means inspection completed, NOT launch readiness; blockers are in the report.
// Negative errno means incomplete/rejected inspection (including truncation).
int maxps4_game_directory_report(const char* path, char* output, size_t capacity);
// Explicit read-only SELF inspection, including structurally clear dynamic
// signed layouts. Signature remains unverified; encrypted/compressed data and
// unknown layouts are refused. Does not relax the strict launch/preflight API.
int maxps4_game_directory_clear_self_report(const char* path, char* output, size_t capacity);
#ifdef __cplusplus
}
#endif
