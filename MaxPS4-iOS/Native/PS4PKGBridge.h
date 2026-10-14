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
// Revalidates the PKG/PFS, reads one exact uroot-relative selection to memory.
// No writes, path joining, decryption or execution. Maximum 64 Mio.
// Query with output=NULL/capacity=0: -ENOSPC and length=required (0 for empty).
// Other failures leave length=0; output is untouched. Source must remain stable.
int maxps4_pkg_pfs_read_file(const char* path, const char* selected,
                            unsigned char* output, size_t capacity, size_t* length);
#ifdef __cplusplus
}
#endif

// Read a vetted plaintext PNG artwork descriptor (0x1200 icon0, 0x1006 pic1).
// Query with output=null to get required length; no PFS/decryption.
int maxps4_pkg_read_artwork(const char* path, unsigned int entry_id,
                           unsigned char* output, size_t capacity, size_t* length);
