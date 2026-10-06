# MaxPS4 0.7 — shadPS4/FEX guest bridge compliance lock

This document freezes the source and license boundary for the next MaxPS4 integration stage before any shadPS4 guest-CPU bridge source is copied or compiled into the application.

## Locked reference

- Reference repository: `Leviidev/AetherPS4`
- Locked commit: `3347fc13f19860855ce91b66356940989ea6b6a6`
- FEX upstream revision used by the locked Darwin snapshot: `f2b679f6028ce1c38875233aecfcf5d3f8ebecec`
- Existing FEXCore provenance: see `docs/fexcore-0.6-lock.md`

## MIT-only guest bridge candidate set

The following files in the locked reference each carry `SPDX-License-Identifier: MIT` and are the only shadPS4/AetherPS4 guest bridge files approved for the first 0.7 import:

- `src/core/fex/fex_guest_engine.cpp` — blob `bc3c4408ef6c4e14b9c8a5ceb3d236e6a28c2d8f`
- `src/core/fex/fex_guest_engine.h` — blob `bdb08c8b986dc2fe37d954cf315f9a62a20ccaf4`
- `src/core/guest_cpu/fex_guest_cpu.cpp` — blob `9d0092ce1f70abd771f96123d9d37da74f6fca58`
- `src/core/guest_cpu/fex_guest_cpu.h` — blob `a38ac457e12e3d2c082359c919b52423e81a16fb`
- `src/core/guest_cpu/fex_hle_bridge.cpp` — blob `0f40ae6b413e0473de9bb13eaf302d9fdf34405c`
- `src/core/guest_cpu/fex_hle_bridge.h` — blob `97d9f7956dcb75802bf3124298c065c4921d3930`

Any imported copy must retain its SPDX header and preserve provenance back to the locked reference.

## Explicitly excluded GPL surface

The locked AetherPS4 tree also contains GPL-covered shadPS4 files. They are not covered by the MIT-only bridge allowance above and must not be copied into the 0.7 MIT bridge layer without a separate license decision and corresponding distribution obligations.

Examples include:

- `src/core/ios/ios_jit_allocator.cpp`
- `src/core/ios/ios_jit_allocator.h`
- `src/core/guest_cpu/guest_memory_validation_cache.h`

MaxPS4 will continue using its independently implemented StikDebug/JIT26 RX/RW allocator boundary validated on-device instead of importing the GPL iOS JIT allocator.

## Runtime truthfulness gate

The successful 0.6 `FEXCORE_SMOKE_OK` result proves real x86-64 guest execution through FEXCore on iOS. It does not prove PS4 emulation.

The backend remains STAGING until:

1. the approved guest bridge subset compiles against the locked FEXCore revision on arm64 iOS;
2. the bridge executes a controlled guest request on-device through the validated JIT26 allocator;
3. shadPS4 runtime/HLE integration is added under its applicable license terms;
4. no Sony firmware, keys, games, copyrighted game assets, or credentials are bundled;
5. an on-device integration test passes before READY is exposed.

This is a project compliance checklist, not legal advice.
