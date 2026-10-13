#pragma once
#include <stddef.h>
#include <stdint.h>
#include "maxps4_jit_memory.h"
#include "OrbisLinkerBridge.h"
#ifdef __cplusplus
extern "C" {
#endif
// Opaque engine: no FEX C++ types cross the native/Swift boundary.
typedef struct maxps4_fex_engine maxps4_fex_engine;
// Self-contained synthetic Orbis test, using the existing loader/linker/FEX
// runtime. Status -ECANCELED means an operation was not attempted. Values are
// valid only when their operation succeeded; no interpreter fallback occurs.
typedef struct maxps4_ps4_elf_test_report {
    int32_t open_status, load_status, link_status, initialize_status;
    int32_t execute_status, read_status, verification_status;
    uint32_t backend, segments, imports, resolved_service, calls, resumed;
    uint32_t jit_regions_before, jit_regions_after, guest_segments_after;
    uint64_t entry, kernel_return, final_memory, rax;
    char error[256];
} maxps4_ps4_elf_test_report;
// Returns 0 only for verified FEX execution (backend 2 native, 3 simulator).
// Owns and closes its engine on every path. Call from the app's CPU queue.
int32_t maxps4_fex_test_ps4_libkernel(maxps4_ps4_elf_test_report *report);
// PS4 System V entry registers, not native ARM64 registers or host pointers.
typedef struct maxps4_ps4_kernel_call {
    uint64_t rdi, rsi, rdx, rcx, r8, r9, rsp;
} maxps4_ps4_kernel_call;
// Resolve a decoded sceKernel/scePthread import to an opaque service ID.
// Returns 0 for unknown symbols/versions. Does not apply ELF/PRX relocations.
uint32_t maxps4_ps4_resolve_libkernel(const char *symbol, const char *library,
    const char *module, uint32_t version);
// Mount configuration is host-side. Return 0 or signed Orbis kernel error.
// Must mount AFTER staging; each staging attempt resets all system services.
int64_t maxps4_fex_engine_mount_app0(maxps4_fex_engine *engine, const char *directory);
// Explicit service dispatch, serialized against staging and engine accesses.
// Returns a guest ABI result, including signed Orbis errors. Default bootstrap
// guest thread identity is 1. No guest CPU import trampoline is installed.
int64_t maxps4_fex_engine_call_libkernel(maxps4_fex_engine *engine, uint32_t service,
    const maxps4_ps4_kernel_call *call, uint64_t guest_thread);
enum { MAXPS4_CPU_RESTRICTED_INTERPRETER = 1, MAXPS4_CPU_FEX_ARM64_JIT = 2,
       MAXPS4_CPU_FEX_ARM64_SIMULATOR = 3 };
enum { MAXPS4_FEX_CONTEXT_CREATED = 1, MAXPS4_FEX_DISPATCHER_INITIALIZED = 2 };
// Creates the real executable dispatcher; does not execute guest code.
// iOS requires successful authorized debugger-region preparation first.
// Repeated initialization succeeds without allocating another dispatcher.
int32_t maxps4_fex_engine_initialize(maxps4_fex_engine *engine);
// Returns 0 or a negative errno. Engine owns its real FEX context/configuration.
int32_t maxps4_fex_engine_open(maxps4_fex_engine **out);
void maxps4_fex_engine_close(maxps4_fex_engine *engine);
int32_t maxps4_fex_engine_stage(const maxps4_fex_engine *engine);
int32_t maxps4_fex_engine_cpuid(maxps4_fex_engine *engine, uint32_t leaf,
                              uint32_t subleaf, uint32_t out[4]);
// Compatibility synthetic ELF fixture routed through run_elf; no host syscalls.
// Requires initialization and an available executable-memory provider.
// No interpreter fallback within this function.
int32_t maxps4_fex_engine_probe(maxps4_fex_engine *engine, uint64_t *result,
                              uint32_t *backend);
// Executes a bounded static ELF64 ET_EXEC image through real FEX only.
// Admits MOV EAX,imm32 / ADD EAX,imm32 / NOP / final HLT, and exact
// MOV dword [RIP+disp32],imm32 stores proven to target guest RW/non-X data.
// No branches, arbitrary memory access, relocations or syscalls. Every
// instruction, including a store, counts against the finite admission budget.
// Segments are rebased into owned host mappings, preserving relative offsets;
// entry reports the original ELF address, never a host pointer. No fallback.
// Caller keeps file bytes immutable during the call and serializes engine close.
int32_t maxps4_fex_engine_run_elf(maxps4_fex_engine *engine, const uint8_t *file,
                                size_t length, uint32_t budget, uint64_t *result,
                                uint32_t *backend, uint64_t *entry);
// Explicit ELF interpreter fallback; backend is never reported as FEX JIT.
// Reports logical next RIP and completed instruction count, including on errors.
int32_t maxps4_fex_engine_interpret_elf(maxps4_fex_engine *engine,
    const uint8_t *file, size_t length, uint32_t budget, uint64_t *result,
    uint32_t *backend, uint64_t *entry, uint64_t *rip, uint32_t *instructions);
// Same restricted real FEX execution, followed by a PM4 snapshot from that
// image's readable PT_LOAD memory before unmapping it. Caller supplies the guest
// address; admitted RIP-relative stores can generate commands in RW data.
// Range checks precede execution; PM4 interpretation follows CPU completion.
// Maximum 65536 words. On failure written=0 and words remain untouched. The
// copied stream has valid supported PM4 semantics, not necessarily Metal state.
int32_t maxps4_fex_engine_run_elf_pm4(maxps4_fex_engine *engine, const uint8_t *file,
    size_t length, uint32_t budget, uint64_t guest_address, size_t word_count,
    uint32_t *words, size_t capacity, size_t *written, uint64_t *result,
    uint32_t *backend, uint64_t *entry);
enum { MAXPS4_PS4_LINKER_REQUIRED = 1, MAXPS4_PS4_TLS_REQUIRED = 2,
       MAXPS4_PS4_CPU_ADDRESS_SPACE_REQUIRED = 4 };
// Stage a validated plaintext PS4 ELF/SELF in engine-owned, non-host-executable
// guest mappings. Does not initialize JIT, dispatch CPU or resolve imports.
// Guest addresses must be translated; rebasing is unsafe for arbitrary execution.
// On failure output fields are zero and any previous staging is discarded.
int32_t maxps4_fex_engine_stage_ps4(maxps4_fex_engine *engine, const uint8_t *file,
    size_t length, uint64_t *entry, uint32_t *segments, uint32_t *blockers);
// Real FEX execution of the already staged image, limited to the same finite
// instruction whitelist as run_elf. RIP-relative stores reach staged RW/non-X
// data in place. Requires initialized FEX; no fallback, linker or TLS support.
// Uses rebased CPU addresses internally, NOT a general PS4 virtual address
// translator. The CPU_ADDRESS_SPACE_REQUIRED blocker remains set. Returns the
// logical entry; on any failure outputs are zero. Stage/execute are serialized.
int32_t maxps4_fex_engine_run_staged_ps4(maxps4_fex_engine *engine,
    uint32_t budget, uint64_t *result, uint32_t *backend, uint64_t *entry);
// Link and admit a bounded Orbis ELF with standard dynamic tables, one
// libkernel dependency and imported GOT calls. No fallback, SCE tables, TLS,
// general PRX graph or arbitrary guest memory instructions. Clears old staging
// and services on each attempt. CPU_ADDRESS_SPACE_REQUIRED remains set.
int32_t maxps4_fex_engine_link_ps4(maxps4_fex_engine *engine, const uint8_t *file,
    size_t length, uint32_t budget, uint64_t *entry, uint32_t *imports);
// Execute that linked program through real FEX. The syscall trampoline captures
// SysV arguments, dispatches existing libkernel, updates RAX and resumes CPU.
// Only the program admitted at link time can execute; no interpreter fallback.
int32_t maxps4_fex_engine_run_linked_ps4(maxps4_fex_engine *engine,
    uint64_t *result, uint32_t *backend, uint64_t *entry, uint32_t *calls);
// SCE ELF/PRX graph -> exact NID/RELA linker -> owned pages -> finite CPU
// admission. Dependencies supplied explicitly; no disk discovery or decryption.
// Diagnostic describes the first refusal. Invalid preparation clears old state.
int32_t maxps4_fex_engine_prepare_orbis(maxps4_fex_engine *engine,
    const maxps4_orbis_module *modules, size_t count, const char *root,
    uint32_t budget, char *diagnostic, size_t capacity);
// Optional bounded RGBA8 output/input contract for an admitted SCE process.
// buttons: left=1, right=2, up=4, down=8, action=16. No host pointer returned.
// Invalid configuration clears previous output. Dimensions <=320x240; disjoint RW/non-X.
int32_t maxps4_fex_engine_configure_frame(maxps4_fex_engine *engine,
    uint64_t pixels, uint64_t input, uint32_t width, uint32_t height);
// One finite FEX dispatch to HLT, then exact copy of guest pixels. Output pixels
// unchanged on error; metadata zeroed. Caller serializes close against invocation.
int32_t maxps4_fex_engine_step_frame(maxps4_fex_engine *engine,
    uint32_t buttons, uint8_t *pixels, size_t capacity, uint64_t *sequence, uint32_t *backend);
// Only an admitted SCE process may run. Real FEX only, with guarded private
// SysV stack, acyclic guest CALL/RET and terminal HLT. No implicit init/fini,
// syscall, TLS, arbitrary instructions, absolute pointer dereference or fallback.
int32_t maxps4_fex_engine_run_orbis(maxps4_fex_engine *engine,
    uint64_t *result, uint32_t *backend, uint64_t *entry);
// Exact read from staged or anonymous readable memory; no host pointers cross the ABI.
// Failure leaves caller bytes untouched. Serialized against stage/close.
int32_t maxps4_fex_engine_read_ps4(maxps4_fex_engine *engine, uint64_t address,
    uint8_t *output, size_t length);
// Fetch instructions only from staged readable AND executable guest memory.
// Invalid ranges leave output unchanged. No host pointers cross the C ABI.
int32_t maxps4_fex_engine_fetch_ps4(maxps4_fex_engine *engine, uint64_t address,
    uint8_t *output, size_t length);
// Exact write to staged or anonymous writable, non-executable guest memory.
// No host pointers cross the ABI. Rejected accesses leave guest bytes intact.
// Serialized against stage/close; does not dispatch CPU or initialize JIT.
int32_t maxps4_fex_engine_write_ps4(maxps4_fex_engine *engine, uint64_t address,
    const uint8_t *input, size_t length);
// Query whether a staged PS4 guest range is readable (4), writable (2)
// or executable (1). Requires nonzero valid permission bits and a nonempty
// fully mapped range. Writes 0 or 1 to permitted, never a host pointer.
// Does not dispatch guest instructions.
int32_t maxps4_fex_engine_query_ps4(maxps4_fex_engine *engine, uint64_t address,
    size_t length, uint32_t permissions, uint32_t *permitted);
// Query the maximum contiguous bytes at a staged guest address, capped by
// limit (1 MiB). Never exposes a host pointer or crosses a segment boundary.
// On invalid arguments, available is zero; an unmapped address returns zero
// with success. Serialized against staging and close.
int32_t maxps4_fex_engine_contiguous_ps4(maxps4_fex_engine *engine,
    uint64_t address, size_t limit, uint32_t permissions, size_t *available);
// Existing bounded interpreter fallback; never describes its output as FEX JIT.
// Caller keeps engine alive during calls and serializes close against them.
int32_t maxps4_fex_engine_run(maxps4_fex_engine *engine, const uint8_t *code,
                            size_t length, uint32_t budget, uint64_t *result,
                            uint32_t *backend);
#ifdef __cplusplus
}
#endif




