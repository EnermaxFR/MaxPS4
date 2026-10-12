# ELF64 FEXCore → Metal position demo (#148)

The branch explicitly merges PR #145 (`527dac1`) into PR #147 (`0a41864`).
All existing triangle, two-texture, touch/recenter, CPU diagnostic, restricted
interpreter and native ELF protections remain available.

In Diagnostic GPU, select **FEX → Metal**, start rendering, choose left/center/right
and press **Exécuter ELF → Metal**. The immutable ELF bytes contain MOV EAX,n;
ADD EAX,250; HLT. Inputs 0/750/1500 produce RAX 250/1000/1750 through the real
native `maxps4_fex_engine_run_elf` ABI. Swift does not calculate a replacement
CPU result. The bounded native admission prevents loops, syscalls and guest
memory accesses. Each execution closes its own context, guest mappings and JIT
leases before the result is published.

Only backend 2 (FEXCore ARM64 JIT) is accepted. RAX must fit UInt32; its centered
normalized x is `(RAX - 1000) / 1000`, clamped to ±0.55. Y is zero for this
one-axis demo; coordinate validation rejects NaN/infinity and clamps y to ±0.65.
The renderer consumes the same value-based object-offset function tested by
the integration suite and binds it at Metal vertex buffer index 2. CPU execution
uses a serial queue shared with the existing CPU diagnostic. UI/renderer state
publication occurs on the main queue. Request identifiers discard stale results
on mode change, background, reset or dismissal; native resources still finish
and close on the worker queue. CPU errors reset the CPU snapshot, show the
error and never change the independent touch state. Reset is an explicit UI
operation, not a claimed FEX execution. The standalone touch mode remains usable
without JIT.

## Automated verification

- Smoke: coordinate conversion/clamp, backend and invalid-result rejection,
  value-copy isolation, reset, independent touch/CPU modes; existing Swift
  CPU/ELF/memory and native guest mapping/protection/cleanup regression suites.
- SideStore workflow: existing native FEX ARM64 and Swift ABI suites, then 24
  real ELF executions with changed inputs, exact RAX/backend checks, malformed
  and unsupported ELF failures, zero active JIT leases, and actual Metal offscreen
  rendering. Completed commands and texture readback must show distinct pixels
  and monotonically moving object centroids. Missing Metal is an integration
  failure, not a successful substitute. The integration executable also runs
  under `leaks` before the full Xcode IPA build and ABI/signature verification.
- Heavy and full Darwin linkage workflows retain upstream compilation and
  strict iOS/macOS runtime checks. The heavy generic linkage probe can still
  report BLOCKED separately; that is not evidence of generic FEX portability.

## Physical iPhone validation still required

Re-sign the generated IPA with SideStore. Activate the existing StikDebug JIT
flow in Diagnostic FEXCore, return after detachment, then run the GPU steps
above. Record RAX/backend/status and visible movement for all three inputs.
Check missing JIT produces an error, touch/recenter still works, background and
closing discard pending results, and repeated CPU runs succeed. macOS CPU/GPU
readback and an iOS build do not prove authorized JIT execution on an iPhone.

This is a controlled CPU/GPU integration demonstration, not PS4 GPU emulation,
PS4 game compatibility, or evidence that any PS4 game runs. Do not merge before
physical validation.
