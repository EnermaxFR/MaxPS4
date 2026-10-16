# Ubuntu guest-memory translation lifetime audit

Base: `27cabf5a92f91e4e2ae570f0e94a6c0b6cc20a2d`, branch
`experiment/maxps4-ubuntu-codex`. Local source audit and tests, 2026-10-16.

## Reproduced defect and correction

`OrbisV4Translation::release()` and `clear()` invalidate generated code before
unmapping host storage. Its implicit destructor previously destroyed
`GuestAddressSpace` directly, bypassing that invalidation. A surviving FEX
cache could therefore retain constants pointing into unmapped or reused host
storage. This is a missing owner lifetime boundary, not an address arithmetic
or instruction-emission correction inside FEXCore.

The regression in `OrbisV4TranslationTest.cpp` maps guest RX bytes at `0x400000`
to a distinct host address, retains that host address in two simulated thread
caches, and destroys the owner without explicit cleanup. The invalidator checks
the marker while storage is still readable and clears both caches. Before the
correction the executable exits 134 at `invalidations==before+1`; after the
correction it passes. This test exercises the real owner and OS mappings; the
two caches are test state, not executions of generated FEX code.

The destructor now locks the owner, invalidates and clears live reservations
before member destruction. An empty owner skips the callback, preserving the
existing linked-test ordering `memory.clear(); DestroyThread(...);` where the
callback's thread dependencies no longer exist at owner destruction.

The context and invalidator must outlive a nonempty owner, and leases must end
before their owner is destroyed. As with normal C++ destruction, callers must
not start concurrent operations on an object being destroyed. The destructor
is `noexcept`: a throwing invalidator terminates before unmapping rather than
continuing with stale native references. Callers needing recoverable invalidation
failure can call `clear()` while the context is live; existing mutation tests
verify that throwing invalidation preserves storage for retry.

## Address-domain audit

| Boundary | Observed behavior and remaining direct assumptions |
| --- | --- |
| `maxps4_guest_address_space.h` | Translated backend reserves arbitrary host storage, tracks logical regions, enforces permissions and guard pages. Checked copies validate the complete span before changing output. Direct backend requires numeric guest/host identity and refuses relocated hints. No fixed overwrite is used. |
| `maxps4_orbis_v4_memory_bridge.h` | `admit_fex_direct` checks explicit direct backend, permissions and numeric identity; translated dispatch remains refused. Instruction copies use checked guest reads. Native CAS resolves a single aligned operand with R+W; split atomics remain refused. Borrowed pointers require external lifetime serialization. |
| `maxps4_orbis_v4_translation.h` | Leases pin mappings under a mutex; mutation invalidates first. The missing destruction invalidation is corrected. Callbacks remain responsible for clearing all relevant thread/shared caches. |
| `patches/fexcore-guest-memory.patch`, `Core.cpp:GenerateIR` | Translation is opt-in via `UsesGuestMemoryTranslation`. Snapshot validation precedes decoding/IR generation. Without that mode, original direct pointer assumptions remain. |
| Same patch, `Frontend.cpp` and disassembly/code reads | Translated instruction fetch uses `ReadGuestInstruction`; the original branches still dereference instruction pointers in direct mode. Logical decoder positions must remain numeric guest addresses. |
| Same patch, `TranslateGuestOperands` and `JIT/MemoryOps.cpp` | Checked guest EA resolution supplies host addresses to native loads/stores. Scalar split accesses preflight all bytes; vector/pair operands require one complete mapping. Embedded host constants remain valid only while the snapshot is pinned and caches are invalidated before replacement/unmap. |
| Same patch, `AtomicOps.cpp` and `OpcodeDispatcher.h` | Native atomics consume translated host operands with alignment/bounds/R+W checks. Translated push/pop preserve logical RSP; original push/pop remain in direct mode. Host call-return cache storage is separate from guest stack storage. |
| Same patch, unsupported IR | Bulk memcpy/memset, remaining push/pop IR, masked/gather, x87 predicate, cache operations, prefetch and ValidateCode throw in translated mode. These paths have not acquired general translation support. |
| Same patch, lookup/call-return caches | Darwin clearing explicitly zeroes relevant tags instead of assuming `MADV_DONTNEED` zero-fill. Cache invalidation remains mandatory for host constants and guest instruction bytes. |
| `maxps4_fex_runtime.cpp:executeImage` | Restricted non-M1 execution rebases RIP/RSP to host storage. This cannot translate arbitrary logical pointers held in registers. Translated M1 publishes guest/host snapshots, serializes runtime ownership separately and clears code caches before thread cleanup. |
| `OrbisPRXFEXBridge.hpp` | PRX mapping/staging does not implement a general execution bridge; dispatch `-ENOTSUP` is retained. |

This audit reads the repository's patch and runtime sources. Fetching pinned
AetherPS4 `3347fc13f19860855ce91b66356940989ea6b6a6` failed with
`Could not resolve host: github.com`, including outside the sandbox. A direct
web fetch also failed. The full upstream tree, patch application and linked
FEX/VIXL execution could not be revalidated in this session. No FEX patch or
emitter code is changed. The owner is currently exercised by V4 and linked test
clients; this change does not wire it into the separate M1 runtime owner.

## iOS error -48

Darwin direct reservation uses `vm_allocate(..., VM_FLAGS_FIXED)` without
overwrite, including one guard page on each side. `KERN_NO_SPACE` becomes
`-EADDRINUSE` (Darwin -48) and retains raw Mach status. Occupation of either a
payload page or a guard can cause failure. Candidate owners include dyld, heap,
another allocation or a retained previous reservation; none is identified by
the available historical trace. An overlap in this owner's table returns
`-EEXIST`; direct low-address policy refusal returns `-ENOTSUP` before Mach.
These are distinct failures.

The existing Ubuntu tests verify collision refusal without overwriting the
occupant, translated use of the same guest number with separate host storage,
and retry after release. They do not reproduce the original iPhone layout or
prove -48 fixed. Device validation must record the original failing layout,
guard extent, raw Mach result and reservation owner, then repeat lifecycle and
cache invalidation checks with the new source on a physical iPhone.

## Validation

The regression was compiled with C++17, pthreads, `-Wall -Wextra -Werror
-pedantic` before and after the correction. The existing V4 script also runs it
with ASan/UBSan alongside all its other native and sanitized suites.

All four requested scripts exited 0 after the correction:

| Script | Result |
| --- | --- |
| `bash scripts/test-orbis-analysis.sh` | PRX/FEX bridge, metadata and directory preflight passed with ASan/UBSan. |
| `bash scripts/test-orbis-runtime.sh` | 3370 linker checks and executable-image checks passed with ASan/UBSan. |
| `bash scripts/test-orbis-v4-memory.sh` | Five suites plus M1 memory in native and ASan/UBSan modes passed, including the new destruction regression in both modes. |
| `bash scripts/test-ps4-system-services.sh` | Fourteen C++ suites with ASan/UBSan and both C ABI compilation checks passed. |

No sanitizer diagnostic was emitted. Runs use the normal scripts outside the
sandbox because LeakSanitizer fails under ptrace inside it. No sanitizer or
leak-detection setting is disabled. `git diff --check` also passed.

These are the relevant local validations for the owner-only change. They do
not include the unavailable optional linked FEX/VIXL build, native ARM64 or an
iOS build. Existing linked fixtures explicitly clear this owner before
destroying their FEX threads; that ordering was inspected but not executed here.
The FEX patch and its instruction translation behavior remain unchanged.

No PFS/key/decryption work, PR merge, GitHub push, paid operation or device run
is part of this change. General guest execution, unsupported translated IR,
PRX dispatch and physical iPhone collision diagnosis remain open.
