# Next milestone: guest-owned animated scene

Starting point: merged PR #182 and its physically validated iPhone FEXCore ARM64 JIT demo.

## Acceptance criteria

- The guest x86-64 program owns **all** animation state, player movement, sprite positions, collision responses, and framebuffer writes.
- The host Swift layer forwards a 32-bit input mask and displays the copied guest RGBA framebuffer. It must not synthesize sprites, movement, or collisions.
- Retain the existing input ABI: left=1, right=2, up=4, down=8, action=16.
- Extend the original 32x24 synthetic demo with a second autonomous moving sprite, a visible collision response, and frame-counter-based animation.
- Add deterministic native tests for initial frame, directional movement, Action pressed/released, animated frames without input, and collision response. Validate actual pixel differences and backend 2 on ARM64.
- Rebuild the bundled `app.elf` from the guest fixture, and require the checked-in fixture to match the generated artifact byte-for-byte in CI.
- Keep the existing iOS controls and diagnostic report. Do not regress the iPhone JIT / SideStore build.
- Never claim commercial PS4 game compatibility or merge before on-device validation.

## Implementation order

1. Introduce a bounded guest state layout for second-sprite coordinates, direction, and collision state.
2. Extend the fixture's x86-64 machine-code emitter to update that state once per guest frame, with bounded arithmetic and deterministic animation.
3. Add pixel-level assertions for guest-generated animation and collisions.
4. Regenerate the embedded demo ELF and validate its byte-for-byte equivalence in CI.
5. Produce an IPA and validate on physical iPhone.

This document defines the target and test contract; it is **not** an implementation or a passing validation.
