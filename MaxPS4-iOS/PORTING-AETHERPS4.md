# AetherPS4 integration roadmap for MaxPS4

Reference: https://github.com/Leviidev/AetherPS4
Relevant sources: AetherPS4-iOS/Sources/JITSupport.swift,
src/core/ios/, src/core/guest_cpu/, src/core/fex/, src/core/libraries/.
Review source and third-party component licenses before transferring code.

## What is integrated
- A user-initiated `stikjit://enable-jit?bundle-id=...` shortcut,
  modeled on AetherPS4's StikDebug launch route.
- No AetherPS4 binary/framework is bundled.
- No AetherPS4 JIT allocator or FEXCore engine is linked.
- The existing native shadPS4 utility and experimental CPU code remain unchanged.

## What must be ported before PS4 games are runnable
1. Dependency-aware CMake/Xcode build for core and transitive libraries.
2. FEXCore ARM64 guest CPU backend and PS4 HLE/kernel services.
3. iOS memory allocator with validated signed/debugged execution permissions.
4. Real execution proof using a generated ARM64 function, with fail-closed fallback.
5. Renderer integration (Vulkan/Metal translation) and shader recompiler.
6. Loader, controller, filesystem and per-game compatibility tests.

## JIT test policy
StikDebug reporting request completed != proof of generated code execution.
Only report JIT active after executing known generated ARM64 code and comparing
its return value. Never auto-issue BRK just because a debugger appears attached.

## Licensing
AetherPS4 is GPL-2.0-or-later for primary code, with differently licensed
third-party components (see upstream LICENSE, LICENSES/, REUSE.toml).
When actual code is copied, preserve headers, notices, source records and
GPL requirements. This document is a technical integration plan only.

## Verified StikDebug memory contract (AetherPS4 source review, 2026-10-09)

Source inspected: `src/core/ios/ios_jit_allocator.{h,cpp}` in
`Leviidev/AetherPS4` (GPL-2.0-or-later).

- Their JIT allocator uses a **fresh debugger-allocated RX region** via
  `BRK #0xf00d`, register `x16=1`, with `x0=nullptr`, then aliases
  the region as writable using Darwin `vm_remap`.
- Their source expressly warns that supplying an existing application's RW
  address in `x0` fails to establish executable memory reliably.
  **Do not pass MaxPS4's existing RW staging page to the BRK protocol.**
- Their implementation documents a recoverable SIGTRAP guard specifically
  for a failed JIT trap. MaxPS4 presently has no equivalent guard, so it
  must never invoke its existing naked `maxps4_stikdebug_jit26_prepare_region`
  from a button or normal app startup.
- Their iOS mapping API uses `<mach/vm_map.h>` (`vm_remap`,
  `vm_protect`, `vm_deallocate`) rather than unavailable iOS
  `mach_vm.h`. Keep `MAP_JIT`, ordinary RW mappings, and debugger RX
  allocations conceptually separate.

### Integration checkpoints (do not claim achieved prematurely)
1. Add isolated, bounds-checked RW-alias probe using `vm_remap`
   without `PROT_EXEC` or `BRK`; validate shared writes and cleanup.
2. Add a narrowly scoped, opt-in BRK trap and recovery mechanism,
   tested on device with the real StikDebug Universal JIT Script.
3. Allocate debugger-owned RX pages (x0=0) and make a RW alias;
   carefully validate executable permission and code-signing state.
4. Execute a minimal generated ARM64 function, verify exact return,
   only then report JIT readiness.
5. Port and link the full FEXCore guest engine with shadPS4.

The current StikDebug deep link only requests that the debugger open.
It does not establish JIT memory or make the MaxPS4 guest backend runnable.


## FEXCore ARM64 object-link gate (2026-10-09)

The pinned FEXCore `JIT.cpp`, `ALUOps.cpp` and
`Arm64Relocations.cpp` compiled successfully in GitHub Actions run #809.
These are only **three translation units**, not an integrated FEXCore runtime.

The workflow now attempts to archive these objects and link them into a
relocatable ARM64 object, deliberately permitting unresolved references.
Do not treat a successful archive, `ld -r`, or `nm` inventory as proof
that a complete FEXCore backend can be linked to the iOS app.

**Integration sequence:**
1. Inspect `fexcore-jit-partial-link-status.txt` and
   `fexcore-jit-partial-demangled-symbols.txt` from a *new* Actions run;
   the #809 build predates this linker probe.
2. Identify each missing symbol's owning FEXCore translation unit or
   external dependency; add the required real implementation instead of
   stub functions just to satisfy the linker.
3. Resolve Darwin/iOS ABI, exception/signal handling, executable page
   allocation and guest-state ownership with separately testable components.
4. Add a full native linking test independent of the shipping IPA and require
   zero unexpected missing symbols before changing the application link.
5. Enable a device-only executable-memory probe **only with verified JIT
   authorization**. A debugger deep link is not evidence that generated
   ARM64 code ran. Keep the interpreter fallback when authorization fails.

Status: **JIT not linked, no generated-code execution, no PS4 title launch.**
