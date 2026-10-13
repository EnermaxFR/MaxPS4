#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// Returns a UTF-8 report in caller-owned storage. Always NUL-terminates when
// capacity > 0; -ENOSPC means the report was truncated. No extraction to disk.
// Zero means a complete report, which may describe a rejected/malformed PKG;
// negative errno reports source I/O or report-generation failures with text.
int maxps4_pkg_inventory_report(const char* path, char* output, size_t capacity);
#ifdef __cplusplus
}
#endif
