# MaxPS4 third-party notices

MaxPS4 is an independent experimental iOS frontend. FEXCore/shadPS4 integration work is developed on the `shadps4-ios-core` branch.

## shadPS4 / GPL-covered integration

Project: shadPS4
Source: https://github.com/shadps4-emu/shadPS4

Any shadPS4 or other GPL-covered source incorporated into MaxPS4 must retain the applicable copyright and license notices. Binary distributions containing such code must satisfy the corresponding GPL source-distribution requirements.

The AetherPS4 integration reference inspected for the 0.6 port carries GNU GPL version 2.


For the next guest-CPU bridge stage, the locked AetherPS4 reference contains individual bridge implementation files with MIT SPDX headers, but those files include and depend on shadPS4 headers carrying GPL-2.0-or-later notices (for example `src/common/types.h` and `src/core/libraries/kernel/threads/exception.h`). Therefore MaxPS4 treats the integrated guest-CPU bridge as a GPL-covered combined integration rather than claiming an MIT-only boundary.

The public MaxPS4 repository remains the corresponding source location for MaxPS4 changes. Imported or compiled shadPS4/AetherPS4 code must retain its original SPDX/copyright notices and the upstream source/revision must remain recorded. MaxPS4 continues using its independently implemented JIT26/StikDebug interoperability shim for the validated iOS executable-memory path; GPL shadPS4 JIT allocator files are not copied unless a later integration explicitly requires them and records that decision.

## FEXCore

Project: FEX
Source: https://github.com/FEX-Emu/FEX
Pinned upstream revision for the inspected Darwin port: `f2b679f6028ce1c38875233aecfcf5d3f8ebecec`
License: MIT

Copyright (c) 2019 Ryan Houdek <Sonicadvance1@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Reference snapshot inspected for Apple/Darwin adaptation: `Leviidev/AetherPS4` commit `3347fc13f19860855ce91b66356940989ea6b6a6`, `runtime/sources/fexcore-darwin`.

## StikDebug interoperability

Project: StikDebug
Source: https://github.com/StikDebug/StikDebug
License of the StikDebug project: GNU AGPL version 3.

MaxPS4 does not bundle or redistribute StikDebug, StikJIT, or the third-party BreakpointJIT framework in the 0.6 FEXCore probe. MaxPS4 only implements the small public debugger breakpoint calling convention needed to interoperate with an independently installed StikDebug session. This keeps the MaxPS4 probe source and binary separate from StikDebug's AGPL-covered implementation while preserving attribution to the interoperability source.

## Distribution boundaries

MaxPS4 does not include PlayStation 4 games, copyrighted game content, console firmware, cryptographic keys, user credentials, or other proprietary Sony content. Users must provide any legally required external content themselves.

Third-party code must not be copied into this repository without preserving its provenance and applicable notices. The 0.6 FEXCore integration must also preserve the iOS JIT26 W^X boundary validated with StikDebug; the macOS MAP_JIT allocator is not assumed to be valid on iOS.

See `docs/fexcore-0.6-lock.md` for the 0.6 acceptance gate. This notice is a compliance inventory, not legal advice.
