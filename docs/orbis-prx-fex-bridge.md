# Experimental PRX → FEX memory boundary (issue #207)

Starting branch: `experiment/pkg-pfs-mode-validation-85cc9d4`, reference
`87179d6a024603abd43484b01f1b99a7bc23774c`. Work stays on
`experiment/codex-orbis-prx-fex-bridge`; no merge or changes to main.

## Audit and contracts

| Component | Contract / limits |
| --- | --- |
| `OrbisMetadata` | Bounded ELF/SCE parsing, lower 47-bit guest addresses, full export extent coverage, TLS inventory, qualified NIDs, RELA inventory; no execution. |
| `OrbisPRXPreparation` | Up to 32 explicit plaintext PRXs, 16 KiB aligned biases, 64 MiB aggregate staging budget. Dependency/version validation, topological order, qualified NID resolution and supported 8-byte RELA writes in vectors. Reject missing imports, cycles, weak/special symbols, overlapping segments/RELA writes, overflow and unsupported metadata. Initialization order is inventory only; constructors are not called. Failed preparation empties its output. |
| `OrbisLinker` | Already provides the reusable `Image::mappings()` / `Mapping` contract. Owns dependency, NID and relocation work for its supported ELF/PRX subset; applies SCE RELRO sealing. No second loader is needed. |
| `OrbisPRXFEXBridge` | Adapts prepared regions to that same mapping contract, or accepts an existing linker image. Copies into owned `GuestAddressSpace` translated storage. One guarded reservation spans all regions, including gaps, at most 32 MiB / 4096 mappings. Invalid ranges, unknown permissions, W+X, execute-only, and page-sharing aliases are refused before reservation. Gaps remain inaccessible. |
| Permissions / RELRO | RX=5, RW=6, R=4, ELF flag convention. Guest X is metadata, never host executable x86 memory. Standalone SCE RELRO is mapped R/NX after relocations. Overlapping RELRO/PT_LOAD is outside the accepted subset. GNU RELRO overlays are explicitly refused for PRX preparation mapping. |
| TLS / imports | PRX TLS is refused; no thread allocation or game startup ABI is supplied. No host HLE function pointers or synthetic success stubs. Import matching now also requires identical symbol kind (function/object mismatch is a regression refusal). |
| Transaction / lifetime | Caller serializes access and keeps input bytes stable during mapping. Pending storage is private; publish only after all mappings succeed. Failure, including allocation failure after partial mapping, destroys pending storage and retains the previous image. Replacement/unload destroys owned reservations; pointers become invalid on successful replacement/unload/destruction. No shared live CPU mapping is mutated. |
| FEX/V4 | `OrbisV4MemoryBridge::admit_fex_direct` requires a direct backend AND numeric guest/host address equality. This bridge deliberately uses translated storage, so dispatch returns `-ENOTSUP`, clears the host pointer and explains the missing translated JIT integration, startup stack/TLS and initialization. No dispatch or guest instruction is attempted. Existing V4 leases/invalidators are the separate path for future live translated execution. |

The latest PRX regressions already cover exports outside regions, reverse-order
RELA overlap and noncanonical/overflowing destinations. They remain in the test
suite. A new negative test covers function/object NID mismatch.

## Reproduce

```sh
bash scripts/test-orbis-analysis.sh
bash scripts/test-orbis-runtime.sh
bash scripts/test-orbis-v4-memory.sh
bash scripts/test-ps4-system-services.sh
c++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  MaxPS4-iOS/Native/PS4PKGBridge.cpp \
  MaxPS4-iOS/Tests/PS4PFSHeaderDiagnosticTest.cpp -o /tmp/pfs-header-test
/tmp/pfs-header-test
```

The analysis script runs the new bridge suite with ASan/UBSan on Linux and macOS.
It uses fabricated ELF/PRX modules/NIDs, verifies relocated bytes and BSS,
RX/RW/NX, SCE RELRO, overflow, byte/page collisions, sparse-budget refusal,
retained prior image on failure, allocation-failure sweep (including allocations
after reservation/mapping), input destruction, unload and dispatch refusal.
CI also compiles the test for iPhone ARM64. Compilation is not physical-device
execution. Existing ELF/libkernel and linker suites remain independent gates.

## Kero Blaster boot matrix

| Stage | Evidence / status for CUSA06015 |
| --- | --- |
| Lawful executable access | Proven first blocker for the reported PKG: encrypted PFS `0x000d`, offset 1572864, size 8650752, `eboot.bin` inaccessible. No payload decryption or key search. |
| ELF/PRX, NIDs, RELA | Implemented/tested only on synthetic plaintext fixtures. Title executable/dependencies unknown. |
| FEX memory | Experimental translated mapping tested on fixtures; game dispatch refused. General error `-48` unresolved. |
| Threads/TLS, system services | Separate synthetic libkernel/TLS checks exist; title startup ABI, imports and service coverage unknown. |
| Graphics/audio | General PS4 game pipeline not validated; synthetic rendering does not prove game rendering/audio. |
| Touch/controller, saves | Title integration and stability unknown; no title tests on iPhone. |
| Physical iPhone | Prior synthetic FEX/JIT results reported on iOS 27.2 do not validate this bridge or Kero Blaster. |

The inventory report labels `Kero Blaster readiness` only for the declared
CUSA06015 header identity (not proof of authenticity). It derives the first
blocker from actual PFS readability/eboot presence, then explicitly labels later
stages untested. Metadata, artwork or an eboot filename never admit runtime.

Authorized inputs needed for further work: a legally obtained, already readable,
unencrypted Orbis ELF `eboot.bin`, its explicitly selected plaintext PRX files,
and their explicit dependency/bias manifest (`maxps4.json` for the existing
runtime path). Optional `sce_sys/param.sfo` confirms declared title/version;
provide no keys, licenses or secrets. SELF/PFS decryption is outside this work.

## CI, IPA and iPhone procedure

Correctness workflows must pass on the final branch SHA before dispatching
`fexcore-app.yml` for the SideStore diagnostic IPA. Automatic PR packaging is
skipped only for this experimental branch in the two existing IPA workflows;
their build/signature/distribution commands and entitlements are unchanged.
Use the Actions run/artifact SHA to match the installed diagnostic build.

On a physical iPhone, install the resulting diagnostic IPA through the existing
SideStore procedure. Record model, iOS/SideStore versions, source SHA, JIT state,
full PKG inventory/readiness report and native CPU error report. For the known
encrypted PKG, expect first blocker PFS/eboot and no guest dispatch. With lawful
plaintext inputs, record the first actual ELF/import/memory refusal rather than
assuming progress. Any playability claim requires recorded game startup, visible
rendering, audio, touch/controller response, save/reload and sustained stability.
No such physical-device result is claimed here.
