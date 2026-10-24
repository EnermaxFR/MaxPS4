#include "../Native/maxps4_fex_runtime.h"
#include <stddef.h>
_Static_assert(sizeof(maxps4_ps4_kernel_call) == 56, "System V register snapshot");
_Static_assert(offsetof(maxps4_ps4_kernel_call, rsp) == 48, "stack register offset");
_Static_assert(sizeof(maxps4_videoout_frame) == 56, "linear VideoOut host frame layout");
_Static_assert(offsetof(maxps4_videoout_frame, byte_count) == 48, "linear VideoOut byte count offset");
_Static_assert(sizeof(maxps4_gnm_submission) == 16, "GNM host submission layout");
_Static_assert(offsetof(maxps4_gnm_submission, word_count) == 8, "GNM dword count offset");
_Static_assert(sizeof(maxps4_ps4_elf_test_report) == 352, "Swift/C diagnostic report layout");
_Static_assert(offsetof(maxps4_ps4_elf_test_report, entry) == 64, "guest values offset");
_Static_assert(offsetof(maxps4_ps4_elf_test_report, error) == 96, "error text offset");
static int32_t (*test_elf)(maxps4_ps4_elf_test_report*) = maxps4_fex_test_ps4_libkernel;
static uint32_t (*resolve)(const char*, const char*, const char*, uint32_t) = maxps4_ps4_resolve_libkernel;
static int64_t (*mount_app0)(maxps4_fex_engine*, const char*) = maxps4_fex_engine_mount_app0;
static int64_t (*mount_directory)(maxps4_fex_engine*, const char*, const char*) = maxps4_fex_engine_mount;
static int64_t (*dispatch)(maxps4_fex_engine*, uint32_t, const maxps4_ps4_kernel_call*, uint64_t) = maxps4_fex_engine_call_libkernel;
static int64_t (*graphics_ticket)(maxps4_fex_engine*, uint64_t, uint64_t, uint64_t*) = maxps4_fex_engine_graphics_event_ticket;
static int64_t (*graphics_complete)(maxps4_fex_engine*, uint64_t) = maxps4_fex_engine_complete_graphics_event;
static int32_t (*gnm_backend)(maxps4_fex_engine*, uint32_t) = maxps4_fex_engine_gnm_backend;
static int32_t (*gnm_snapshot)(maxps4_fex_engine*, maxps4_gnm_submission*, uint32_t*, size_t) = maxps4_fex_engine_gnm_snapshot;
static int32_t (*gnm_complete)(maxps4_fex_engine*, uint64_t, uint32_t) = maxps4_fex_engine_gnm_complete;
static int32_t (*video_snapshot)(maxps4_fex_engine*, maxps4_videoout_frame*, uint8_t*, size_t) = maxps4_fex_engine_videoout_snapshot;
static int64_t (*video_complete)(maxps4_fex_engine*, uint64_t, uint32_t) = maxps4_fex_engine_videoout_complete;
static int64_t (*video_vblank)(maxps4_fex_engine*, uint64_t) = maxps4_fex_engine_videoout_vblank;
static int32_t (*link_ps4)(maxps4_fex_engine*, const uint8_t*, size_t, uint32_t, uint64_t*, uint32_t*) = maxps4_fex_engine_link_ps4;
static int32_t (*run_linked)(maxps4_fex_engine*, uint64_t*, uint32_t*, uint64_t*, uint32_t*) = maxps4_fex_engine_run_linked_ps4;
int main(void) { return test_elf == NULL || resolve == NULL || mount_app0 == NULL || mount_directory == NULL || dispatch == NULL || graphics_ticket == NULL || graphics_complete == NULL || gnm_backend == NULL || gnm_snapshot == NULL || gnm_complete == NULL || video_snapshot == NULL || video_complete == NULL || video_vblank == NULL || link_ps4 == NULL || run_linked == NULL; }
