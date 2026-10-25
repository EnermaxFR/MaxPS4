# Sonic performance investigation

The target is 60 displayed frames/s at the existing rendering resolution. No
new IPA is to be built during this profiling pass (user request, 2026-10-25).

The latest device timings supplied by the user were approximately 15 ms CPU
encoding, 50 ms GPU execution and 51 ms CPU wait for the latest submission.
The wait includes GPU execution: these are not three independent costs to add.
The image copy was approximately 1.2 ms CPU / 0.3 ms GPU. These submission-level
timings do not establish the cost of an entire frame, which can contain several
submissions. The reported display rate remained around 6 frames/s.

## Bilinear interior experiment: rejected

A prototype bypassed address wrapping for fully interior RGBA8/RGB565 bilinear
samples and applied channel swizzling after interpolation. CPU differential
tests passed 819,200 exact comparisons, including partial/split/missing memory.
It nevertheless regressed on the Metal benchmark, so it was removed from the
production shader helper. Existing image quality and shader behavior remain.

GPU measurements from test run [40160368342](https://github.com/EnermaxFR/MaxPS4/actions/runs/40160368342):

| Texture | Existing resolved path | Interior prototype |
| --- | ---: | ---: |
| 16x8 RGBA8 | 0.751 ms | 0.950 ms |
| 16x8 RGB565 | 0.725 ms | 0.908 ms |
| 256x128 RGBA8 | 0.723 ms | 0.885 ms |
| 256x128 RGB565 | 0.723 ms | 0.843 ms |

These are fixture timings on the CI Mac, not iPhone FPS. The benchmark now
interleaves variants, discards warmups and covers both sizes and formats.

## Retained CPU change: group instruction reads

The interpreter and translator previously fetched a decode window using up to
three separate four-byte guest-memory callbacks. They now try one twelve-byte
permission-checked read. Readers refusing that span, or code at mapping edges,
fall back to the original word-sized reads. No instruction cache or assumption
that shader code cannot change was added.

The contiguous-code regression executes 129 instructions with 129 callbacks,
instead of the previous 387. Separate tests cover bulk-read refusal and an
end instruction at the final mapped word. This is a reduction in callback count,
not a measurement of whole-game CPU time or a claim of reaching 60 frames/s.

Local ASan/UBSan interpreter, translator generation and lane-by-lane translated
program comparisons passed. Only the PM4 CPU/GPU workflow is dispatched for
validation; no application packaging workflow is invoked.

Final validation of the retained change passed in
[40160869887](https://github.com/EnermaxFR/MaxPS4/actions/runs/40160869887):
CPU sanitizer regressions, 819,200 texture comparisons, actual Metal pixel
comparisons (worst channel difference 0), iPhone bridge compilation and Swift
frontend checks. No IPA was generated or replaced. The on-device CPU time and
frame rate of this retained CPU change have not been measured.

## Reused resource-scanner workspace

The renderer now owns a reusable single-lane scanner instead of constructing
temporary waves and vectors per draw. Registers, flags, callbacks, errors and
exports are reset between stages. The instruction cache is cleared before
every draw; only allocated storage is retained. Resource bytes and code are
read again. Tests change shader code and descriptors, remove user SGPRs and
recover from an unreadable program to detect stale state.

An interleaved, warmed local Clang `-O2` fixture benchmark measured 2.586 us/draw
with fresh scanner storage and 1.711 us/draw when reused. This measures scanner
work alone, not the full renderer or game frame rate. Sanitizers passed.

The portrait display keeps the original aspect ratio without cropping. Only
landscape uses centered aspect-fill cropping; native resolution is unchanged.
The orientation change passed the targeted VideoOut Metal workflow
[40161216429](https://github.com/EnermaxFR/MaxPS4/actions/runs/40161216429).
