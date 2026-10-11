# iOS JIT Fix Plan (Investigation Branch)

Branch: `fix/ios-jit-runtime-investigation`

## Confirmed blocker

In `MaxPS4-iOS/Native/maxps4_fex_runtime.cpp`, `maxps4_fex_engine_initialize()` returns `-ENOTSUP` under `TARGET_OS_IPHONE`. `maxps4_fex_engine_probe()` is also gated on iPhone. Therefore, a successful StikDebug Enable JIT action does not currently enable the FEXCore execution path in this runtime.

## Safety rule

Do not remove the iPhone guards or report the JIT as operational until the actual FEXCore allocator is supported and verified on a signed iOS device build. A successful app launch or interpreter test is not proof that FEXCore JIT code was generated and executed.

## Work items

- [ ] Trace the pinned FEXCore revision's JIT allocator and its `BreakpointJIT` integration.
- [ ] Identify the supported iOS JIT entitlement/signing and allocation contract for the actual install method.
- [ ] Add a capability check that reflects a real successful executable-memory allocation, not just the presence of MaxPS4 in StikDebug.
- [ ] Preserve the explicit `-ENOTSUP` failure when the capability is unavailable.
- [ ] Log the failing operation and its native error domain (POSIX errno vs. Mach return code) without logging private keys, pairing data, serial numbers, or device identifiers.
- [ ] Build and install a separate test IPA; do not replace existing IPAs.
- [ ] Verify on device that FEXCore `InitCore()` succeeds, a known x86-64 probe returns 42 with backend `MAXPS4_CPU_FEX_ARM64_JIT`, and a negative test fails safely when executable memory is unavailable.

## Acceptance criteria

1. iPhone builds compile with the guards intact until the allocator is implemented.
2. Unsupported devices fail clearly and safely.
3. A supported device passes the real FEXCore JIT probe; the restricted interpreter is reported separately.
4. Existing `main`, existing IPA files, and existing features remain unchanged.

## Current status

Investigation branch created from `main`. This commit documents the confirmed blocker and the validation gates; it does not claim to implement or enable an iOS JIT allocator.
