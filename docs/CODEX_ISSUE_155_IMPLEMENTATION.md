# Codex implementation mission — issue #155, PR #156

Implement the entire bounded PS4 ELF/SELF → FEXCore guest execution milestone in **one cohesive coding pass**, with automated tests. Work on this branch, stacked on PR #154. Do not add UI or claim commercial game support.

## Current interfaces
- `PS4ExecutableImage.hpp`: parses PS4 ELF/unencrypted SELF into segment bytes, retains declared virtual addresses and flags, marks linker/TLS requirements.
- `PS4GuestMappingPlan.hpp`: rejects unsupported images and describes mappings, but does not allocate or map memory.
- `maxps4_guest_elf.{h,cpp}`: existing restricted host-backed ELF arena, guest address translation and protections.
- `maxps4_fex_runtime.{h,cpp}`: existing FEX engine and bounded synthetic ELF runner. The latter accepts only MOV/ADD/NOP/HLT; do not silently bypass this admission.
- `GuestELFFixture.h`, `PS4ExecutableImageTest.cpp`, `PS4GuestMappingPlanTest.cpp`, `FEXRuntimeIntegrationTest.cpp`: existing test fixtures.
- `.github/workflows/ps4-executable.yml`: Linux ASan/UBSan, macOS and iPhone ARM64 checks.

## Implement
1. Implement a transactional PS4 image guest arena with checked address span, page alignment, W^X, segment boundary pages, BSS, read/write/execute permissions, and rollback on all errors. Never use MAP_FIXED over host addresses or treat declared PS4 addresses as raw host pointers. Preserve a guest virtual address ↔ arena mapping.
2. Connect this arena to the existing FEXCore runtime, including executable range query and guest RIP. Ensure FEX instruction fetches and guest data loads/stores refer to the same memory. Keep existing synthetic ELF tests passing.
3. Add a **separate** PS4-format bounded fixture execution API, or a guarded dispatch with explicit PS4 identity. Reject unsupported imports, relocations, TLS, syscalls, encrypted SELF and unknown instructions with clear errors. Use a deterministic stop condition and finite instruction budget. Avoid broad unbounded PS4 execution.
4. Add integration test: PS4-format ELF/SELF plaintext → loader → mapped guest memory → FEXCore → deterministic CPU result, plus guest memory write verified by host readback. Include failure/cleanup, BSS, overflow, permissions, overlap and malformed image tests.
5. Run Linux sanitizers, macOS runtime tests, iOS arm64 compilation and existing SideStore IPA workflow. Preserve PM4→Metal diagnostic. Fix failures and post exact test results and artifact URL on PR #156.
6. Update documentation to state precisely what runs on macOS and what has been verified on a physical iPhone. Never label synthetic tests as PS4 game support.

## Acceptance
The milestone is complete only if the integration test demonstrates FEXCore actually executing instructions from a PS4-format image loaded through `PS4ExecutableImage` into the shared guest memory, including a verified data write, and CI builds the iOS IPA. If any prerequisite is missing, explicitly report the blocker rather than inventing a passing test.

Current PR #156 is only a preflight + CI skeleton. Do not mistake it for completed integration.
