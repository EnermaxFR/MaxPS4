#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// Returns a UTF-8 report in caller-owned storage. Always NUL-terminates when
// capacity > 0; -ENOSPC means the report was truncated. No extraction to disk.
int maxps4_pkg_inventory_report(const char* path, char* output, size_t capacity);
#ifdef __cplusplus
}
#endif
