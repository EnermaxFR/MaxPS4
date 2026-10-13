#include "../Native/maxps4_fex_runtime.h"
#include <stddef.h>
_Static_assert(sizeof(maxps4_ps4_kernel_call) == 56, "System V register snapshot");
_Static_assert(offsetof(maxps4_ps4_kernel_call, rsp) == 48, "stack register offset");
static uint32_t (*resolve)(const char*, const char*, const char*, uint32_t) = maxps4_ps4_resolve_libkernel;
static int64_t (*mount_app0)(maxps4_fex_engine*, const char*) = maxps4_fex_engine_mount_app0;
static int64_t (*dispatch)(maxps4_fex_engine*, uint32_t, const maxps4_ps4_kernel_call*, uint64_t) = maxps4_fex_engine_call_libkernel;
int main(void) { return resolve == NULL || mount_app0 == NULL || dispatch == NULL; }
