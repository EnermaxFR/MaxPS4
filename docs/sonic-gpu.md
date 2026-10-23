# Sonic Mania GPU implementation status

Sonic Mania's local executable declares 113 `libSceGnmDriver` imports.
This is a static import inventory, not a trace proving all are executed.
Only NID/name metadata was used; no game code, shaders or assets are included
in this repository. The existing Pad/VideoOut demo is a CPU rasterizer and
does not provide a PS4 shader backend.

## Implemented GNM command writers

Five more imports now have real packet generation through checked guest memory:

| Function | NID | Output dwords |
| --- | --- | --- |
| sceGnmDrawIndexAuto | GGsn7jMTxw4 | 7 |
| sceGnmDrawIndex | HlTPoZ-oY7Y | 10 |
| sceGnmSetVsShader | gAhCn6UiU4Y | 29 |
| sceGnmSetPsShader350 | 5uFKckiJYRM | 40 |
| sceGnmSetVgtControl | cFCp0NX8wf0 | 3 |

The base-PS4 sequences, descriptor layouts, validation and NIDs follow the
pinned [shadPS4 GNM implementation](https://github.com/shadps4-emu/shadPS4/blob/91e6afa69b0a64b0afaa0281b80f8fbb0cd88136/src/core/libraries/gnmdriver/gnmdriver.cpp).
Neo behavior is not selected. These functions build PM4 in the caller's
buffer; success does not mean submission or GPU completion. Unsupported
functions remain unresolved instead of reporting successful work.

VS consumes seven register words, applies the permitted modifier mask, and
emits program address/resources, VS output configuration and position format.
PS350 consumes twelve words, emitting program/resources, color/depth export
formats, input configuration, barycentric control and color/depth masks.
A null PS descriptor emits the base-PS4 disabled-PS sequence. VGT emits
IA_MULTI_VGT_PARAM. Trailing NOP payloads are zeroed deterministically.

Guest pointers are never passed to a host driver. All input descriptors are
copied before output, including aliased input/output. Invalid inputs leave
output unchanged. Outputs must be aligned, writable and non-executable.
The maximum copied output is 160 bytes even if advertised capacity is huge.
There is no retained command-writer state or native GPU allocation to reset.

## Indexed PM4 semantics

The interpreter now retains DRAW_INDEX_2, INDEX_TYPE (16/32 bit) and
NUM_INSTANCES in command order. Each draw carries its index address/count,
maximum index count, index size, instance count and preceding register writes.
Invalid alignment, counts, 48-bit addresses, flags or later packets invalidate
the complete stream. These are bounded command semantics; fetching index data
and executing a shader remain renderer responsibilities.

The native triangle diagnostic explicitly refuses indexed/instanced draws
and guest shader state, so adding parser support cannot silently turn a game
draw into the hardcoded native triangle.

## Validation and remaining work

`PS4GnmTest` verifies exact packets, descriptor fields, modifier bounds,
invalid guest memory, atomic failures and aliased buffers. PM4 tests verify
index width, address and instance transitions, invalid streams and unchanged
Metal rejection of unsupported state. The linked FEX fixture calls all five
real GNM NIDs, then checks the resulting VS/PS/VGT state and indexed/automatic
draws from the same engine's memory. Its shader descriptors are fabricated
register values; no shader execution is claimed.

The Sonic GPU backend is **not complete**. Required remaining components:

1. Command submission and synchronization, including indirect buffers,
   barriers, fences, flip patching and persistent register state.
2. GCN vertex/pixel shader decoding and translation into Metal-compatible
   shaders, with buffer/texture/sampler descriptors and fetch shaders.
3. Render-target/depth formats, tiled memory, blending, viewport/scissor,
   indexed geometry and resource lifetime/coherency.
4. Actual game execution through that backend, pixel validation and physical
   iPhone performance/stability tests.

Neither these command writers nor an offscreen native triangle establish
Sonic gameplay. No IPA or firmware is added by this change.
