# PS4 guest-memory to FEXCore integration contract

This branch starts from b49f0c7 and is intentionally separate from PR #166.

## Current verified interface
- `maxps4_fex_engine_stage_ps4` stages validated plaintext PS4 ELF/SELF segments in engine-owned, non-host-executable guest mappings.
- `maxps4_fex_engine_read_ps4` reads staged readable guest bytes using a guest virtual address.
- `maxps4_fex_engine_run_elf` executes only a bounded admitted ELF fixture and rebases segments. It is **not** a general PS4 guest address-space implementation.

## Implemented native memory contract (PR #170)

The C ABI exposes three bounded operations on an already staged image:
- `maxps4_fex_engine_read_ps4(engine, guest_address, output, length)` copies readable bytes into the caller buffer.
- `maxps4_fex_engine_write_ps4(engine, guest_address, input, length)` copies bytes into a writable, non-executable guest segment.
- `maxps4_fex_engine_query_ps4(engine, guest_address, length, permissions, &permitted)` reports whether all requested permission bits (R=4, W=2, X=1) cover the entire range. `permitted` is initialized to zero even for invalid arguments when the pointer is non-null.

All three require a non-null engine and buffer/output where applicable, a nonzero length of at most 1 MiB, and serialize against staging through the native runtime lock. Invalid arguments return `-EINVAL`; a mapped-but-inaccessible or unmapped read/write returns `-EFAULT`; the permission query returns success with `permitted=0` for inaccessible ranges. They do **not** execute instructions or return a host address. No CPU guest-memory callback is installed by these operations.

The `PS4GuestMemory` adapter owns no mapping. Its lifetime is bounded by the owning `GuestELF` image; staging or clearing that image invalidates previously translated host pointers. Exact reads/writes validate the full range before copying, and overlapping source/destination buffers have memmove semantics. Host mapping permissions remain separate from the guest's logical X bit: PS4 code pages are not made host-executable.

Native tests cover RW data, RX code, invalid permission masks, overflow, cross-segment rejection, preservation of buffers on rejected accesses, overlapping transfers, and teardown. These are regression tests, **not evidence of a working PS4 CPU execution path**. CI status must be checked independently.

## Required next implementation
1. Define a checked guest-address translator for address + length with overflow protection, mapping permissions, and deterministic errors for unmapped/cross-boundary accesses.
2. Expose read/write/execute access modes internally, never raw host pointers over the Swift/C boundary.
3. Wire the translator to FEXCore's guest memory access mechanism, not only the ELF staging reader; preserve guest virtual addresses for RIP-relative and arbitrary supported accesses.
4. Handle page boundaries, overlapping mappings, W^X permissions and mapping lifetime under engine close/stage.
5. Keep JIT code-cache memory separate from PS4 guest memory; never use debugger/JIT preparation as proof of PS4 executable support.
6. Add native CI fixtures covering RW, RX, unmapped addresses, overflow, boundary crossing, overlapping PT_LOADs and stage rollback.
7. Add a minimal staged plaintext PS4 ELF execution integration test only when linker/TLS/address-space blockers are actually resolved. Never label interpreter fallback as JIT.

## Acceptance criteria
- Existing FEXCore JIT probe and ELF interpreter CI remain green.
- A CPU memory access to a staged guest address demonstrably reaches the same guest bytes as the staging reader.
- Tests prove permission enforcement and deterministic teardown.
- No claim of commercial PS4 game execution until the real ELF/SELF loader, imports/syscalls and GPU path are integrated.

## Bounded staged-image CPU handoff

`maxps4_fex_engine_run_staged_ps4` now admits and executes the engine-owned
staged image in place using the existing real FEX ARM64 execution path.
Admission validates the complete entry-to-end-of-file code sequence before
dispatch: MOV EAX, ADD EAX, NOP, exact RIP-relative immediate stores and a final
HLT. Every store must fit entirely in an RW/non-X segment. Budget exhaustion,
unsupported instructions and linker/TLS blockers reject dispatch before any
store. Errors leave result/backend/entry zero. An uninitialized dispatcher
returns -EAGAIN; an absent image returns -EFAULT. No interpreter fallback is used.

The runtime lock covers admission, CPU execution and thread cleanup, preventing
restaging while FEX uses the mapping. Executable-range metadata is scoped to the
execution, and the shared FEX code cache is cleared before every dispatch so
restaging cannot reuse a previous translation. The existing ELF interpreter
implementation and backend reporting are preserved.

This is a bounded **rebased** handoff, not the general translator requested in
step 3 above. Logical guest addresses are used for admission and staging access;
FEX RIP and effective memory addresses still refer to the host arena. Relative
offsets are preserved, including negative displacements and logical addresses
above 4 GiB. Absolute operands remain rejected. The
CPU_ADDRESS_SPACE_REQUIRED blocker remains set. General PS4 execution still
needs virtual-address-aware decoding and generated memory accesses, linker,
TLS, syscalls and GPU integration. No commercial-game playability is claimed.

### Validation for this change

- Linux: checked range, ELF interpreter, ELF memory, PS4 memory, PS4 staging and
  new staged CPU admission tests passed with AddressSanitizer/UBSan.
  LeakSanitizer was disabled locally because this executor is under ptrace.
- C11 ABI signature compilation passed, including the new entry point.
- The automated real-FEX runtime test now stages synthetic PS4 ELF code, executes
  stores and reads their results through the staging API, repeating execution and
  restaging with different results, high addresses and negative displacements.
  It also checks pre-initialization refusal, whole-program rejection and teardown.
- Darwin ARM64 execution and iOS linkage cannot run in this Linux executor.
  They must be verified by the existing FEXCore workflows. At inspection, all
  current-head workflows were queued; a queued workflow is not a passing check.

