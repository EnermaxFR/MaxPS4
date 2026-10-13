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
#ifdef __cplusplus
}
#endif
