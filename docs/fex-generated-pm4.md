# FEX-generated diagnostic PM4

The restricted native ELF loader now admits one additional real x86-64
instruction: `C7 05 disp32 imm32`, MOV dword [RIP+disp32],imm32.
Each four-byte target must lie wholly within a declared RW/non-executable
guest segment. The loader proves signed displacement arithmetic, the original
entry address and segment permissions before allocating or executing anything.
Unsupported addressing modes, writes to code, gaps and segment overruns are
rejected. Every store counts against the finite instruction budget. There are
still no admitted branches, stack operations, arbitrary addressing or syscalls.

The arena preserves relative offsets between code and data when rebased to host
memory. Consequently the actual FEX-translated RIP-relative store addresses the
same guest data segment that the PM4 snapshot reads.

`run_elf_pm4` checks the requested readable command range before execution and
interprets the words after CPU completion. The fixture has a zero-sized file
payload for its data segment: three actual FEX stores generate a DRAW_INDEX_AUTO
packet in BSS. The result is copied to Swift, then passed to the C++/Metal bridge.
Tests vary the generated vertex count to check translation-cache reuse and
verify that invalid admission or budgets never publish commands.

This is an explicitly constructed ELF diagnostic, not a PS4 game. Its native
triangle shaders still do not translate GCN shader programs. General guest
memory access, a PS4 CPU address space/ABI, HLE, dynamic linking, real sceGnm
submission, GPU register application and physical iPhone execution remain
separate requirements.

Validation is covered by GuestRIPStoreAdmissionTest, the native
FEXRuntimeIntegrationTest and the FEX-enabled PM4MetalIntegrationTest. Linux
admission/memory tests do not claim FEX execution; only the macOS ARM64 runtime
CI executes the real translator. iOS compilation and IPA packaging do not
establish on-device JIT execution.
