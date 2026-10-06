# MaxPS4 0.7 — shadPS4/FEX guest bridge compliance lock

This document freezes the source and license boundary for the next MaxPS4 integration stage before any shadPS4 guest-CPU bridge source is copied or compiled into the application.

## Locked reference

- Reference repository: `Leviidev/AetherPS4`
- Locked commit: `3347fc13f19860855ce91b66356940989ea6b6a6`
- FEX upstream revision used by the locked Darwin snapshot: `f2b679f6028ce1c38875233aecfcf5d3f8ebecec`
- Existing FEXCore provenance: see `docs/fexcore-0.6-lock.md`

## Guest bridge candidate set and effective GPL boundary

The following implementation files in the locked reference each carry `SPDX-License-Identifier: MIT` and are the initial guest bridge implementation set:

- `src/core/fex/fex_guest_engine.cpp` — blob `bc3c4408ef6c4e14b9c8a5ceb3d236e6a28c2d8f`
- `src/core/fex/fex_guest_engine.h` — blob `bdb08c8b986dc2fe37d954cf315f9a62a20ccaf4`
- `src/core/guest_cpu/fex_guest_cpu.cpp` — blob `9d0092ce1f70abd771f96123d9d37da74f6fca58`
- `src/core/guest_cpu/fex_guest_cpu.h` — blob `a38ac457e12e3d2c082359c919b52423e81a16fb`
- `src/core/guest_cpu/fex_hle_bridge.cpp` — blob `0f40ae6b413e0473de9bb13eaf302d9fdf34405c`
- `src/core/guest_cpu/fex_hle_bridge.h` — blob `97d9f7956dcb75802bf3124298c065c4921d3930`

Any imported copy must retain its SPDX header and preserve provenance back to the locked reference.

These implementation files are not treated as an isolated MIT-only deliverable because their include graph reaches GPL-2.0-or-later shadPS4 headers, including `src/common/types.h` and `src/core/libraries/kernel/threads/exception.h`. The compiled guest bridge is therefore treated as GPL-covered integration in MaxPS4. MaxPS4's repository is public and the corresponding source/provenance must remain available with binary distributions.

## GPL surface and exclusions

The bridge build may include GPL-2.0-or-later shadPS4 headers required by the guest CPU interface. Those GPL notices must be preserved and the resulting combined integration is handled under compatible GPL terms.

The shadPS4 iOS JIT allocator (`src/core/ios/ios_jit_allocator.{h,cpp}`) is still intentionally excluded from MaxPS4 because MaxPS4 already has an independently implemented StikDebug/JIT26 RX/RW allocator boundary validated on-device. It must not be copied merely to duplicate functionality.

## Runtime truthfulness gate

The successful 0.6 `FEXCORE_SMOKE_OK` result proves real x86-64 guest execution through FEXCore on iOS. It does not prove PS4 emulation.

The backend remains STAGING until:

1. the approved guest bridge subset compiles against the locked FEXCore revision on arm64 iOS;
2. the bridge executes a controlled guest request on-device through the validated JIT26 allocator;
3. shadPS4 runtime/HLE integration is added under its applicable license terms;
4. no Sony firmware, keys, games, copyrighted game assets, or credentials are bundled;
5. an on-device integration test passes before READY is exposed.

This is a project compliance checklist, not legal advice.
