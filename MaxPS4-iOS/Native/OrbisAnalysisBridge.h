#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// Always NUL-terminated for valid output/capacity. Negative errno on rejection,
// I/O failure or truncation. File inspection never executes or maps guest code.
int maxps4_orbis_analysis_report(const char* path, char* output, size_t capacity);
#ifdef __cplusplus
}
#endif
