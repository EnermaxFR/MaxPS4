# PS4 guest-memory to FEXCore integration contract

This branch starts from b49f0c7 and is intentionally separate from PR #166.

## Current verified interface
- `maxps4_fex_engine_stage_ps4` stages validated plaintext PS4 ELF/SELF segments in engine-owned, non-host-executable guest mappings.
- `maxps4_fex_engine_read_ps4` reads staged readable guest bytes using a guest virtual address.
- `maxps4_fex_engine_run_elf` executes only a bounded admitted ELF fixture and rebases segments. It is **not** a general PS4 guest address-space implementation.

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
