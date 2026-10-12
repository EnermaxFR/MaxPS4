# PS4 ELF/SELF → FEXCore integration gate

This branch begins issue #155 on top of PR #154. The PS4 executable segment loader is currently a **staging** component, not a FEXCore execution path. The iPhone PM4 triangle and FEX JIT diagnostics are independent validation milestones; do not interpret them as PS4 game execution.

## Required contract before executing guest code

1. **Validate entire image before mutation.** Reject encrypted/compressed or unsupported SELF segments, malformed headers, overlapping virtual ranges, arithmetic overflow, and unsupported dynamic imports/relocations. Preserve declared guest virtual addresses; never silently relocate to synthetic ones.
2. **Prepare transactional guest mappings.** For each PT_LOAD, map page-aligned ranges with checked address arithmetic; copy file bytes, zero-fill BSS, and apply final guest R/W/X permissions. Handle shared boundary pages deliberately. Roll back every mapping on failure.
3. **Connect FEXCore to the *same* guest memory.** Instruction fetches, guest loads/stores and entrypoint must resolve through the mapped image. Avoid a separate hardcoded synthetic ELF execution path. Check that entrypoint lies within an executable segment.
4. **Bound execution.** Construct a minimal unprotected PS4-format fixture with a deterministic return or stop condition; enforce instruction/time limits and a safe stop for unsupported syscalls or unresolved imports.
5. **Test and document.** Include positive and negative fixtures, BSS verification, permissions, cleanup, FEX guest result, macOS ARM64 runtime checks and iOS ARM64 linkage. CI passing on host is not proof of physical-iPhone JIT execution.

## First acceptance test

Construct a PS4-format ELF fixture with a declared code segment and writable data segment. Load it using `PS4ExecutableImage`, map it into the FEXCore guest address space, execute instructions from its declared entrypoint, and assert a deterministic guest result **and** a data write read back from that same mapped guest memory. Verify cleanup and refusal of an unresolved import. Report the exact path from loader → guest mapping → FEXCore in test output.

## Not in scope

Commercial PS4 game compatibility, protected SELF decryption, full Orbis syscall/HLE, PS4 GCN shader translation, Player Mode UI.

References: #155, #154, #153. This checklist is a design/acceptance gate; it does not implement or prove runtime integration.
