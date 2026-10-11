# iOS JIT Fix Plan

Branch: `fix/ios-jit-runtime-investigation`

## Confirmed runtime blocker

In `MaxPS4-iOS/Native/maxps4_fex_runtime.cpp`, `maxps4_fex_engine_initialize()` returns `-ENOTSUP` under `TARGET_OS_IPHONE`; `maxps4_fex_engine_probe()` is also gated on iPhone. This is an explicit product-code refusal before FEXCore's JIT dispatcher is initialized. Selecting MaxPS4 in StikDebug cannot remove this code-level gate.

The existing experimental native dual-map allocator in `MaxPS4-iOS/Native/shadps4_bridge.cpp` is also explicitly disabled for the FEX-only iPhone build by `MAXPS4_PLATFORM_JIT_ONLY`. It is not wired into FEXCore's allocator, and it uses the StikDebug BRK protocol, which can terminate the process if the matching script is not actively servicing it.

## iOS 26+ protocol requirements

The official StikJIT integration guide says that on devices where TXM/SPTM is present, debugger attachment alone is insufficient. The host allocator must cooperate with the matching StikDebug script using the universal JIT protocol:

- `JIT26PrepareRegion`: `x16 = 1`, `brk #0xf00d`, address in `x0`, size in `x1`; a null address requests a fresh RX region.
- `JIT26Detach`: `x16 = 0`, `brk #0xf00d`.
- The matching script must be attached before any breakpoint call. An unserviced BRK can crash the process.
- Prepare executable regions before detaching. Do not report readiness just because the StikDebug URL opened or `CS_DEBUGGED`/`P_TRACED` is present.
- Preserve W^X: FEX needs correct RX/RW alias translation, cache invalidation, allocation ownership, and cleanup on every error path.

Reference: https://github.com/StikDebug/StikJIT/blob/main/INTEGRATION.md

## Work required for an actual fix

- [ ] Trace the exact pinned FEXCore allocator and its `iOSJITAlloc` / `BreakpointJIT` integration.
- [ ] Integrate the iOS 26 universal breakpoint protocol into the allocator used by FEXCore itself, not just the separate MaxPS4 translator.
- [ ] Establish one bounded executable-region allocation policy before detaching, or a correctly supervised reconnect protocol for later allocations.
- [ ] Implement and validate FEX's RW-to-RX address translation, write alias, instruction-cache invalidation, safepoints, and cleanup.
- [ ] Install safe handling for expected BRK failure/session loss; never execute BRK based only on `P_TRACED`.
- [ ] Ensure the signed app has `get-task-allow` and uses the intended StikDebug script, then inspect the final resigned app bundle and entitlements.
- [ ] Preserve `-ENOTSUP` whenever any prerequisite is unknown or unavailable.
- [ ] Build a separate test artifact without replacing existing IPAs. Test on the actual iPhone.
- [ ] Verify `InitCore()` succeeds and the fixed x86-64 probe returns 42 with backend `MAXPS4_CPU_FEX_ARM64_JIT`; test unavailable-session and allocation-failure paths too.

## Acceptance criteria

1. iPhone builds remain safe when the JIT script or executable-memory provider is absent.
2. Debugger attachment and interpreter results are not presented as FEX JIT success.
3. A supported device passes the real FEXCore JIT probe with proof of the FEX backend.
4. Existing `main`, IPA files, signing configuration, and features remain unchanged.

## Current status

This branch contains an investigation plan only. It does **not** implement the allocator integration, build an IPA, or claim the iPhone JIT works. The source findings are strong enough to identify the code gate, but a correct runtime fix requires changes to FEXCore's allocator and a real signed-device validation; simply deleting the guard would be unsafe.
