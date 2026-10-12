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
// Restricted PS4 ELF/unencrypted SELF fixture -> staging -> bounded FEX runner.
// Uses existing instruction admission, guest arena, W^X and finite budget.
// No PS4 syscalls, dynamic imports, relocations, or game compatibility.
int32_t maxps4_fex_engine_run_ps4_fixture(maxps4_fex_engine *engine,
    const uint8_t *file, size_t length, uint32_t budget, uint64_t *result,
    uint32_t *backend, uint64_t *entry);
// Existing bounded interpreter fallback; never describes its output as FEX JIT.
// Caller keeps engine alive during calls and serializes close against them.
int32_t maxps4_fex_engine_run(maxps4_fex_engine *engine, const uint8_t *code,
                            size_t length, uint32_t budget, uint64_t *result,
                            uint32_t *backend);
#ifdef __cplusplus
}
#endif

