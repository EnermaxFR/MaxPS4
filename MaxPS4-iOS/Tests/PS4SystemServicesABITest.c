#include "../Native/maxps4_fex_runtime.h"
#include <stddef.h>
_Static_assert(sizeof(maxps4_ps4_kernel_call) == 56, "System V register snapshot");
_Static_assert(offsetof(maxps4_ps4_kernel_call, rsp) == 48, "stack register offset");
_Static_assert(sizeof(maxps4_ps4_elf_test_report) == 352, "Swift/C diagnostic report layout");
_Static_assert(offsetof(maxps4_ps4_elf_test_report, entry) == 64, "guest values offset");
_Static_assert(offsetof(maxps4_ps4_elf_test_report, error) == 96, "error text offset");
_Static_assert(sizeof(maxps4_v4_prx_request) == 40, "V4 request 64-bit C/Swift layout");
_Static_assert(offsetof(maxps4_v4_prx_request, entry) == 24, "V4 guest entry offset");
_Static_assert(offsetof(maxps4_v4_prx_request, abi) == 32, "V4 explicit ABI offset");
static int32_t (*run_v4)(maxps4_fex_engine*, const maxps4_v4_prx_request*, uint64_t*, uint32_t*) = maxps4_fex_engine_run_v4_prx;
static int32_t (*test_elf)(maxps4_ps4_elf_test_report*) = maxps4_fex_test_ps4_libkernel;
static uint32_t (*resolve)(const char*, const char*, const char*, uint32_t) = maxps4_ps4_resolve_libkernel;
static int64_t (*mount_app0)(maxps4_fex_engine*, const char*) = maxps4_fex_engine_mount_app0;
static int64_t (*dispatch)(maxps4_fex_engine*, uint32_t, const maxps4_ps4_kernel_call*, uint64_t) = maxps4_fex_engine_call_libkernel;
static int32_t (*link_ps4)(maxps4_fex_engine*, const uint8_t*, size_t, uint32_t, uint64_t*, uint32_t*) = maxps4_fex_engine_link_ps4;
static int32_t (*run_linked)(maxps4_fex_engine*, uint64_t*, uint32_t*, uint64_t*, uint32_t*) = maxps4_fex_engine_run_linked_ps4;
int main(void) { return test_elf == NULL || resolve == NULL || mount_app0 == NULL || dispatch == NULL || link_ps4 == NULL || run_linked == NULL || run_v4 == NULL; }
