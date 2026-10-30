# Linear BC4/BC5 SNORM textures

Extends BC4/BC5 (AMD formats 38/39) with number format 1, SNORM. Existing
UNORM operation and BC1/BC2/BC3 restrictions are preserved.

The shared interpreter/C++/Metal helper decodes signed 8-bit endpoints to
[-1,1], maps -128 to -1, and interpolates with signed arithmetic. Palette mode
uses the original signed endpoint order, including -127 > -128 despite both
normalizing to -1. The ascending/equal palette has explicit -1 and +1 entries.
The layout and endpoint rule were checked against Microsoft's
[DirectXTex BC4_SNORM decoder](https://github.com/microsoft/DirectXTex/blob/main/DirectXTex/BC4BC5.cpp)
and [block compression documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-block-compression).

BC4 exposes R; BC5 exposes independent R and G. Missing channels remain zero,
alpha remains one, and swizzling/filtering operates on decoded signed values.
The existing block addressing, padded pitch, cache and checked/resolved
footprint logic are reused. SNORM is accepted only for BC4/BC5 in this change.
Other number formats, tiled layouts, mip chains and compressed writes are not
added. No normal reconstruction is performed by the texture decoder.

## Local verification, 2026-10-30

Cygwin GCC 14.4, C++17, warnings treated as errors, without sanitizers:

- GCNBC45Test runs UNORM and SNORM. Fixed independent palettes cover both
  endpoint orders, equal endpoints, -128/-127 in both orders, mixed signs,
  all selectors, the cross-word selector, and independent green selectors.
- Cached/uncached interpreter and translated runtime loads agree with those
  palettes. Swizzling, cross-block filtering, padded/partial blocks, cache
  read fallback, invalid descriptors and truncated final blocks pass.
- GCNTextureFootprintTest passes 1,843,200 exact checked/resolved comparisons,
  now including number format 1 across the complete matrix.
- BC1/BC2/BC3 and interpreter regressions pass; generated C++ shader execution
  matches the interpreter lane for lane.

Logs: build/toolchain/test-results/*-snorm.log.

## Prepared, not yet run

GCNTextureFootprintMetalTest adds signed BC4/BC5 at 16x8 and 256x128 with
independent R/G data, checked/resolved equality and CPU/GPU channel comparison.
PM4MetalRendererTest adds signed translated pixel-shader scenes, with native
textures disabled/enabled and software image comparison. Negative decoded
values are tested directly by the sampling test, before UNORM target clamping.

This lot has not been published or run on macOS/Metal or a physical iPhone.
Earlier UNORM CI results do not validate SNORM. No IPA built; Spyro unverified.

Publication payload (eight files): Native/GCNBC1Shared.inc,
Native/GCNInterpreter.hpp, Native/GCNShaderRuntimeShared.inc,
Tests/GCNBC45Test.cpp, Tests/GCNTextureFootprintTest.cpp,
Tests/GCNTextureFootprintMetalTest.mm, Tests/PM4MetalRendererTest.mm
(all under MaxPS4-iOS), and this document. Workflow edits remain local.
