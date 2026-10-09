# AetherPS4 integration roadmap for MaxPS4

Reference: https://github.com/Leviidev/AetherPS4
Relevant sources: AetherPS4-iOS/Sources/JITSupport.swift,
src/core/ios/, src/core/guest_cpu/, src/core/fex/, src/core/libraries/.
Review source and third-party component licenses before transferring code.

## What is integrated
- A user-initiated `stikjit://enable-jit?bundle-id=...` shortcut,
  modeled on AetherPS4's StikDebug launch route.
- No AetherPS4 binary/framework is bundled.
- No AetherPS4 JIT allocator or FEXCore engine is linked.
- The existing native shadPS4 utility and experimental CPU code remain unchanged.

## What must be ported before PS4 games are runnable
1. Dependency-aware CMake/Xcode build for core and transitive libraries.
2. FEXCore ARM64 guest CPU backend and PS4 HLE/kernel services.
3. iOS memory allocator with validated signed/debugged execution permissions.
4. Real execution proof using a generated ARM64 function, with fail-closed fallback.
5. Renderer integration (Vulkan/Metal translation) and shader recompiler.
6. Loader, controller, filesystem and per-game compatibility tests.

## JIT test policy
StikDebug reporting request completed != proof of generated code execution.
Only report JIT active after executing known generated ARM64 code and comparing
its return value. Never auto-issue BRK just because a debugger appears attached.

## Licensing
AetherPS4 is GPL-2.0-or-later for primary code, with differently licensed
third-party components (see upstream LICENSE, LICENSES/, REUSE.toml).
When actual code is copied, preserve headers, notices, source records and
GPL requirements. This document is a technical integration plan only.
