# Mission #209: FEX / PRX memory boundary audit

Scope: [mission comment 6217070031](https://github.com/EnermaxFR/MaxPS4/pull/209#issuecomment-6217070031).
Branch `experiment/maxps4-209-translation-audit` starts at
`12d33cd02a3b81ceccc60202635f82501973ef32`, from
`experiment/orbis-v4-post208-runtime-validation`. Target:
`integration/orbis-v4-implementation`. No main changes or merges.

## Phase 0: original CI gate

Observed 2026-10-16. All seven runs associated with head `12d33cd...` completed
successfully before any runtime correction. Orbis V3 IPA was initially
`in_progress` (packaging step); it subsequently succeeded, rather than requiring
a runtime fix. No run was failed, cancelled or skipped at the final gate.

| Workflow | Run | Jobs / evidence |
| --- | --- | --- |
| Orbis SCE analysis / PRX | [38911063960](https://github.com/EnermaxFR/MaxPS4/actions/runs/38911063960) | native `117006737863`, swift-arm64 `117006738244`: Linux/macOS sanitizers, Swift bridge, PRX iPhone compile |
| PS4 services | [38911063792](https://github.com/EnermaxFR/MaxPS4/actions/runs/38911063792) | simulator `117006738441`, native ARM64 `117006738703`, iPhone compile `117006738683`, macOS `117006738715`, Linux `117006738907` |
| PKG inventory | [38911063881](https://github.com/EnermaxFR/MaxPS4/actions/runs/38911063881) | inventory `117006737823`, swift-ios `117006737372` |
| Smoke | [38911063828](https://github.com/EnermaxFR/MaxPS4/actions/runs/38911063828) | CPU ABI, Swift frontend, bounded ELF/memory, ARM64 bridge, Metal state |
| Heavy | [38911063759](https://github.com/EnermaxFR/MaxPS4/actions/runs/38911063759) | FEX ARM64 sources `117006737742`, HLE compile `117006737195` |
| FEXCore SideStore IPA | [38911063998](https://github.com/EnermaxFR/MaxPS4/actions/runs/38911063998) | application `117006738155`: macOS native/Swift/Metal, ARM64 library, Xcode and IPA checks |
| Orbis V3 IPA | [38911063611](https://github.com/EnermaxFR/MaxPS4/actions/runs/38911063611) | orbis-test-ipa `117006740985`: host fixture, strict iOS linkage, application and diagnostic IPA |

The API associates these runs with the PR head. Logs distinguish actual checkout:
analysis jobs used synthetic merge `e4a490fe9a7e294235af67c793025b64cc4cc337`
(merging `12d33cd...` into `7d48fe8...`), while jobs with explicit head checkout
used `12d33cd...`. New analysis checkouts explicitly select the head SHA too.
The original SideStore [IPA artifact](https://github.com/EnermaxFR/MaxPS4/actions/runs/38911063998/artifacts/12049953790)
belongs to the original source, not the changes here. IPA compilation is not a
physical iPhone test. Logs of linked simulator/native ARM64 and macOS IPA jobs
confirm scalar `11/11`, refusal `55/55`, and boundary `16/16` results.

## Phase 1: audited call chains

FEX paths below are relative to `runtime/sources/fexcore-darwin` in pinned
AetherPS4 `3347fc13f19860855ce91b66356940989ea6b6a6`, with the repository's
dispatcher and `patches/fexcore-guest-memory.patch` applied. This is the actual
build dependency, not a replacement CPU. VIXL pin is
`5f418449c48f6ca3ad37c47ec632f17a561c0580`.

| File:function | Precondition -> effect | Failure / proof | Classification |
| --- | --- | --- | --- |
| `OrbisPRXPreparation.hpp:prepare/build` | Explicit plaintext modules; dependency/version topological traversal, qualified NID and matching function/object kind; supported RELA in owned vectors | Missing/version/type/cycle/overlap/overflow refuse and empty preparation; `OrbisMetadataTest` | Code + synthetic tests |
| `OrbisLinker.hpp:link/build/parse` | Validated ELF/SCE tables and plaintext segments -> relocated, RELRO-sealed staging image | Strong transaction retains previous image; unsupported TLS/import/tag/service refuses; `OrbisLinkerTest` | Code + synthetic tests |
| `OrbisLinker.hpp:Image::mappings` | Public mutable image -> shared mapping contract `(guest,size,permissions,bytes)` | **Fixed here:** wrapped bias and truncated owned bytes previously escaped this boundary; `OrbisPRXFEXBridgeTest` reproduces acceptance before fix | Code + local regression |
| `OrbisPRXFEXBridge.hpp:map/commit` | Shared mappings, <=4096, canonical lower 47-bit addresses, <=32 MiB span including gaps; no page aliases/W+X/X-only | Private pending address space, copy then publish; bad allocation/partial map keeps previous image; TLS/GNU RELRO refused for PRX preparation | Code + allocation sweep |
| `maxps4_guest_address_space.h:reserve` | Real host page 4/16 KiB; whole-page bounded interval and guards | Direct backend requires same numeric VA, denies guards below 4 GiB with `-ENOTSUP`; translated backend uses arbitrary host address | Code + low-VA test |
| Same: Darwin direct allocation | `vm_allocate(VM_FLAGS_FIXED)` **without overwrite**, then `vm_protect(NONE)` | `KERN_NO_SPACE -> -EADDRINUSE` (Darwin `-48`); raw Mach status retained; no mapping published | Code + collision tests; no new device trace |
| Same: POSIX direct allocation | `mmap` hint, never replacement | Relocated hint is unmapped and returns `-EADDRINUSE`; original occupant unchanged; V4 collision/retry tests | Code + local tests |
| Same: `map/protect/read/write/translate/release` | Each host page belongs to one region; logical R/W/X, real host R/W/NX | Copy source into temporary RW storage, then seal; no host execute for x86 bytes. Complete span validation before copies; guard/hole/RX-write/NX-fetch fail; whole reservation rollback after sealing failure | Code + V4/PRX tests |
| `maxps4_orbis_v4_memory_bridge.h:admit_fex_direct` | Explicit direct backend AND translated pointer numerically equals guest | Otherwise `-ENOTSUP`, output host pointer cleared | Code + PRX/V4 tests |
| `maxps4_fex_runtime.cpp:executeImage` restricted path | `GuestELF` finite instruction admission -> **host-rebased** initial RIP and private RSP | This path cannot provide general logical guest memory; register operands are not magically rebased | Code; bounded ELF tests only |
| `maxps4_orbis_m1.h:load/admit` -> `executeImage` | Separate translated space, finite instruction whitelist, guest thread stack and static/dynamic TLS fixtures -> logical RIP/RSP/FS | `runtime_lock` and process lifetime pin mappings; no mapping-mutating VM provider during dispatch. Arbitrary ELF/PRX rejected | Code + linked FEX tests |
| `maxps4_fex_runtime.cpp:ProbeAddressSpace` | M1 translated -> immutable `(Guest,Host,Size,Permissions)` snapshot and `UsesGuestMemoryTranslation=true` | Snapshot cleared by RAII after thread/cache cleanup; callback copies/validates arguments via guest memory | Code + linked service/TLS tests |
| FEX `Interface/Core/Core.cpp:GenerateIR` -> `Frontend.cpp:PeekByte/ReadData` | Logical RIP -> checked `ReadGuestInstruction` copies, including split RX mappings | **Fixed here:** malformed snapshot rejected before decoding/IR; fetch refuses W+X/unknown flags and wrapping extents without modifying output | Code + failing-before/fixed-after FEX tests |
| FEX `Core.cpp:TranslateGuestOperands` -> `JIT/MemoryOps.cpp:EmitGuestScalarMemory` | EA (guest base + signed/scaled offset, including FS/GS) -> immutable range checks -> host native operand | Scalar 1/2/4/8-byte split paths preflight every byte before stores; fault record and thread stop before invalid access; preserve NZCV/TSO | Code + linked matrix |
| FEX `JIT/MemoryOps.cpp:EmitGuestAddressTranslation` / `AtomicOps.cpp:TranslateGuestAtomicAddress` | Vector/pair/native atomic complete operand in one range; atomic alignment | Never split an atomic; reject misalignment, bounds and permission faults; unsupported IR throws before entry dispatch | Code + vector/atomic/refusal tests |
| FEX `OpcodeDispatcher.h:_Push/Pop` | Translation enabled -> checked loads/stores but RSP stays guest numeric | Native call-return cache remains host storage, separate from guest stack; stack/TLS tests | Code + linked tests |
| `maxps4_orbis_v4_translation.h:Lease/map/protect/release/write` | Owner lock excludes mutation while a lease holds pointers | Invalidate before mutation; throwing invalidation leaves storage intact. Release/remap two-thread cache tests | Code + lease tests |
| `executeImage:DestroyThread` -> FEX `Core.cpp:ClearCodeCache`, `LookupCache.cpp`, `InternalThreadState.h:ClearCallRetStack` | Clear embedded host constants/shared L3 and current thread caches before destroying thread or storage | Darwin explicit zeroing of allocated L2/L1/call-return tags; advisory discard is insufficient | Code + remap/two-thread regressions |

The PRX staging adapter is not connected to `ProbeAddressSpace`, M1 admission,
thread startup or cache ownership. Its `-ENOTSUP` remains mandatory. Mapping PRX
bytes, or passing their storage pointer as RIP, does not implement that bridge.
No live PRX execution is added here.

### Error -48: what is and is not established

Confirmed by code: direct fixed reservation can collide and return Darwin
`-EADDRINUSE` (`-48`); policy refusal of the low direct profile is `-ENOTSUP`,
not a Mach allocation error. The latter now records guest address, guard extent,
zero host status and the actual policy error instead of an empty success record.

Historical reported trace: `docs/orbis-v4-integration-review.md` records #203
`m1_thread_api_status=-48`, `reason=direct_mapping_collision`, after successful
engine/JIT setup. This proves that reported boundary, not the identity of the
colliding VM owner. Later #205 notes record successful **bounded** M2 low-VA
physical tests; they neither reproduce this branch nor prove a general fix.
Reservation owner (dyld/heap/previous reservation/other mapping), original failing
layout, and behavior of this new source on device remain unproved hypotheses.
Do not interpret a Mach return code as errno or infer a physical run from an IPA.

## Phase 2/3: contract and narrow corrections

Each copied interval owns enough host bytes for its complete logical extent.
Bias additions must not wrap before canonical-range checks. Each snapshot range
has a nonzero host base/size, nonwrapping guest and host extents, known R/W/X
flags and no W+X or X-only permission. Logical ranges cannot overlap; writable
host aliases of executable ranges are also refused. Read-only host aliases and
unsorted disjoint ranges remain valid. Validation proves arithmetic/permission
consistency, **not** OS ownership of arbitrary pointers: only an owning frontend
can prove and pin that storage. Compile validation does not replace mandatory
cache invalidation on snapshot replacement, including when a cache hit skips IR.

FEX snapshot validation is pairwise at compilation, outside emitted data-access
code. Instruction copies check range arithmetic/flags before dereferencing;
whole-span preflight preserves output on holes/NX. Native scalar operations keep
their existing fast path and split preflight. Unsupported bulk/masked/gather,
cache-management and ValidateCode paths remain refused. No fault recovery,
general scheduler, title TLS ABI or translated PRX dispatch is claimed.

PRX regression fixtures exercise wrapped bias, short/empty source vectors,
oversized announced extent, unsupported TLS, retained previous image, cleared
host pointer after dispatch refusal, and unload/reload. The shared
`Image::mappings()` contract is strengthened instead of adding another loader.
Existing dependency/NID/RELA/BSS/RELRO and rollback checks remain the proof for
their supported subsets; initialization order remains inventory, not constructor
execution. Unsupported imports/services continue to fail explicitly.

## Phase 4: separate readiness blockers

| Stage | Evidence / remaining blocker |
| --- | --- |
| CUSA06015 PFS / eboot | Reported encrypted PFS `0x000d`; `eboot.bin` inaccessible. No PKG supplied to this session and no decryption/key/license work. Existing inventory/readiness tests retained. |
| ELF/PRX | Synthetic plaintext fixtures only. No lawful title ELF/PRXs explicitly supplied for this mission. |
| FEX memory | Narrow translated M1/M2 CPU integration exists; PRX dispatch remains unsupported; general -48 unresolved. |
| Services / TLS | Bounded libkernel/TLS fixtures; full title service/startup ABI unproved. |
| Rendering/audio | Synthetic Metal evidence is not title rendering/audio. |
| Controls / saves | Title input/save/reload untested. |
| Physical iPhone | New source not run on a physical device. |

## Phase 5: reproduction and device evidence

```sh
bash scripts/test-orbis-analysis.sh
bash scripts/test-orbis-runtime.sh
bash scripts/test-orbis-v4-memory.sh
bash scripts/test-ps4-system-services.sh
# With AetherPS4 and VIXL at the exact pins above:
bash scripts/test-ps4-fex-linked.sh /path/to/aether build/ps4-linked
```

Local Linux uses GCC 14 for strict `-Wall -Wextra -Werror -pedantic` C++17
ASan/UBSan suites, and Clang 19 for the real FEX/VIXL build. In this managed
workspace LeakSanitizer terminates because the execution environment is traced;
local sanitizer runs use `ASAN_OPTIONS=detect_leaks=0`. Address and undefined
behavior checks stay enabled. CI retains its original leak detection. FEX itself
uses its pinned project compiler flags; the full JIT is not advertised as an
ASan build. New snapshot assertions run through the existing Linux simulator,
native ARM64 and macOS linked FEX suites; strict iOS library compilation remains
a separate gate. Test logs and final SHA/CI links belong in the draft PR.

Local results after corrections: all four scripts above exited 0; the linker
suite reports 3370 checks, services 14 suites, and V4 five suites in both native
and sanitized modes. The complete pinned FEX/VIXL script exited 0 with all four
TSO/LSE configurations, ten malformed snapshot refusals per configuration,
scalar 11/11, refusal 55/55 and boundary 16/16, plus existing linked libkernel,
TLS, remap, two-thread and framebuffer fixtures. Backend 3 is the ARM64
instruction simulator on Linux x86, never an iPhone result.

Before corrections, the PRX wrapped-bias fixture aborted at
`assert(!bridge.map(invalid))`; FEX W+X fetch aborted at
`!space.ReadGuestInstruction(...)`; the low direct-policy diagnostic test aborted
at its guest/guard-extent assertion. Each passes after its corresponding fix.
No test reproduces or resolves the original physical-device -48 here.

Before requesting an IPA, await all correctness workflows on the final source
SHA, then dispatch the existing `fexcore-app.yml` on this experimental branch.
Automatic PR IPA packaging is skipped only for this branch and the prior #208
branch; signing/build/distribution behavior is unchanged. Do not reuse the
original #209 IPA to validate these changes.

On device, record iPhone model, iOS build, SideStore/StikDebug versions, exact
installed source SHA and IPA artifact/run, JIT state and activation logs. First
repeat the **original** collision diagnostic/input/layout unchanged; retain
guest range, guards, page size, operation, raw Mach status and POSIX error, plus
memory ownership diagnostics. Then run the existing synthetic ELF/libkernel and
M2 diagnostics: backend 2, RAX/memory 42, TLS 8, scalar 11/11, refusal 55/55,
boundary 16/16, RX/RW-NX and collision marker preservation. Repeat unload/reload
and JIT invalidation cycles; collect first failure and teardown counts. Run PRX
staging/refusal fixtures and verify no host pointer or guest dispatch is exposed.
For the encrypted title, first readiness blocker must remain PFS/eboot, with
later stages untested. A real user/device trace is required to update the
physical column; no general -48 or gameplay success is inferred here.
