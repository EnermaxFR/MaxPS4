# MaxPS4 — shadPS4 iOS core integration

This branch is the experimental iOS core port. `main` remains the known-good SwiftUI frontend build.

## Architecture

MaxPS4 SwiftUI -> stable C ABI (`maxps4_core.h`) -> iOS host adapter -> shadPS4 core -> FEXCore x86-64 to ARM64 JIT -> renderer.

## Upstream reference

The iOS work should be based on the GPL-compatible shadPS4-derived iOS approach demonstrated by AetherPS4, especially its iOS-specific core, FEXCore Darwin ARM64 runtime, JIT allocator and IPA build path. Do not copy proprietary game or firmware content into this repository.

## Integration gates

1. Keep the SwiftUI frontend launchable without the emulator runtime.
2. Compile and link the native C/C++ bridge for arm64-apple-ios.
3. Add the FEXCore Darwin ARM64 runtime and prove a minimal JIT smoke test.
4. Integrate the shadPS4 loader/HLE core behind the stable C ABI.
5. Add the graphics path required on iOS.
6. Boot only legally dumped user-owned PS4 content.
7. Merge into `main` only after an unsigned IPA launches successfully under the intended JIT-enabled container environment.

## Current state

The C ABI scaffold exists, but the real shadPS4/FEXCore runtime is not linked yet. Functions must report unavailable rather than pretending emulation is active until the corresponding runtime gate is complete.
