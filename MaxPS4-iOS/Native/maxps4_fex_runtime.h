#pragma once
#include <stddef.h>
#include <stdint.h>
#include "maxps4_jit_memory.h"
#ifdef __cplusplus
extern "C" {
#endif
// Opaque engine: no FEX C++ types cross the native/Swift boundary.
typedef struct maxps4_fex_engine maxps4_fex_engine;
enum { MAXPS4_CPU_RESTRICTED_INTERPRETER = 1, MAXPS4_CPU_FEX_ARM64_JIT = 2 };
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
// Exact read from a staged readable segment; no host pointers cross the ABI.
// Failure leaves caller bytes untouched. Serialized against stage/close.
int32_t maxps4_fex_engine_read_ps4(maxps4_fex_engine *engine, uint64_t address,
    uint8_t *output, size_t length);
// Exact write to a staged writable, non-executable guest segment.
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
// Existing bounded interpreter fallback; never describes its output as FEX JIT.
// Caller keeps engine alive during calls and serializes close against them.
int32_t maxps4_fex_engine_run(maxps4_fex_engine *engine, const uint8_t *code,
                            size_t length, uint32_t budget, uint64_t *result,
                            uint32_t *backend);
#ifdef __cplusplus
}
#endif


