# MaxPS4 0.6 — FEXCore provenance lock

This document locks the provenance/compliance inputs before FEXCore-derived source is incorporated into MaxPS4.

## FEXCore

- Upstream project: FEX-Emu/FEX
- Upstream revision used by the Apple/Darwin port: `f2b679f6028ce1c38875233aecfcf5d3f8ebecec`
- Reference Darwin snapshot inspected for the MaxPS4 iOS port: `Leviidev/AetherPS4` commit `3347fc13f19860855ce91b66356940989ea6b6a6`, path `runtime/sources/fexcore-darwin`
- FEXCore snapshot license: MIT
- Required attribution: Copyright (c) 2019 Ryan Houdek <Sonicadvance1@gmail.com>

The MIT copyright and permission notice must be retained with every copy or substantial portion of FEX-derived source distributed by MaxPS4.

## shadPS4 / AetherPS4-derived integration

The inspected AetherPS4 repository is distributed under GNU GPL v2. Any source copied or adapted from that GPL-covered integration must retain its copyright/license notices and MaxPS4 distributions containing that code must meet the corresponding GPL source-distribution requirements.

FEXCore 0.6 has now been compiled, linked, initialized and validated on-device with a real x86-64 guest smoke probe. This validates the FEXCore execution layer only. The MaxPS4 PS4 backend remains STAGING until the real shadPS4 runtime is linked and validated on-device.

## iOS JIT boundary

MaxPS4's validated iOS 26 JIT path uses the StikDebug breakpoint protocol and separate RX/RW mappings. The macOS FEXCore snapshot's MAP_JIT/pthread_jit_write_protect_np allocator must not be copied unchanged to iOS. The FEX code-cache allocator must be adapted behind MaxPS4's validated JIT26 allocation boundary before execution is enabled.

## Distribution boundary

MaxPS4 must not ship PS4 games, copyrighted game assets, Sony firmware, cryptographic keys, credentials, or other proprietary console content. This repository contains emulator/frontend code only; external legally required content is supplied by the user.

## 0.6 acceptance gate

FEXCore 0.6 acceptance status:

1. COMPLETE — Exact FEXCore-derived source revision is recorded.
2. COMPLETE — MIT notice is included in source and binary distribution notices.
3. COMPLETE FOR CURRENT FEXCORE STAGE — GPL-covered shadPS4/AetherPS4 provenance is recorded; any future copied GPL integration must retain notices and corresponding source.
4. COMPLETE — FEXCore builds for `arm64-apple-ios` in GitHub Actions.
5. COMPLETE — The FEXCore code-cache path uses the validated iOS JIT26 RX/RW mechanism rather than assuming macOS MAP_JIT behavior.
6. COMPLETE — A minimal x86-64 guest probe executed successfully through FEXCore on-device and reported `FEXCORE_SMOKE_OK`.
7. NOT YET — `MAXPS4_HAS_SHADPS4_FEX` stays disabled until the actual shadPS4 runtime is linked to this validated FEXCore layer and passes an on-device integration test.

This is a project compliance checklist, not legal advice.
