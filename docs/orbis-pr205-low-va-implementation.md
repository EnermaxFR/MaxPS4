# PR #205: checked low-VA dispatch

M1 now uses translated storage on all hosts, including iPhone. Its private
process and mappings are held under `runtime_lock` from loading through CPU
destruction. `ProbeAddressSpace` publishes a permission-checked immutable range
snapshot to FEX; RIP, RSP and FS remain logical guest addresses. Stack inspection
uses checked guest reads. No mapping-mutating VM provider is attached to M1's
libkernel during dispatch. General ELF/PKG instruction admission remains bounded.

The generated operand resolver rejects missing permissions, holes, split operands
and misaligned atomics before a native access. It records an operand fault,
restores NZCV, resets the JIT spill stack and exits through FEX's dispatcher stop
handler. The adapter returns `-EFAULT`; this aborts the invocation, with no promise
of instruction restart or rollback of earlier instructions. It does not recover
arbitrary native SIGSEGV/SIGBUS faults. Mapping lifetime and host protection must
therefore remain stable. Unsupported memcpy/memset, masked/gather, cache and
ValidateCode IR still fail compilation; arbitrary translated programs remain closed.

The checked fault record belongs to the dispatch invocation. Its pointer uses an
existing reserved CPU-state slot, preserving the pinned frame layout. The
interrupt-page store uses TMP2 and preserves TMP1's compiled branch target.
Temporary emission capacity includes each translation node's range ladder;
oversized translated blocks are rejected before emission at the existing iPhone
code-pool ceiling of 16 MiB. This avoids relying on a native guard-page fault to
grow the temporary buffer, which this frontend cannot recover.

All translated invocation caches are cleared before destroying CPU threads or
releasing their snapshots and mappings. The existing translation owner tests also
cover invalidation-before-mutation and leases blocking concurrent release.

`maxps4_fex_test_m2_low_va` accepts no executable input. It dispatches constant
code at guest `0x400000`, checks load/store/XADD result 42, stack PUSH/POP,
static TLS result 8, guest RX/RW-NX permissions and cleanup. One guest data
address numerically equals an occupied host mapping; that host marker must remain
unchanged. The existing ELF/libkernel device report now includes this diagnostic.
The legacy V4 synthetic/preflight APIs retain their original fail-closed behavior.

Regression coverage adds checked rejection of split/hole/protected operands and
unaligned atomics, followed by successful retry. M1's occupied host-VA test now
requires translated execution to succeed while preserving the original host data.
The V3 synthetic ELF -> FEXCore -> libkernel result-2 regression is retained.

Local validation uses connector-retrieved pinned files because the shell cannot
connect to `proxy:8080`. Clang, CMake and Xcode are unavailable locally. Patch
application, exact reapplication and drift rejection run against pristine FEX
sources from AetherPS4 `3347fc13f19860855ce91b66356940989ea6b6a6`.
GCC14 runs the memory/TLS/M1 suites. LeakSanitizer is blocked by the executor's
ptrace environment; any local ASan/UBSan rerun uses `detect_leaks=0` only for
that invocation. CI configuration retains its full checks.

Final-SHA CI outcomes and any successful SideStore artifact are recorded in the
PR status comment. No physical iPhone is available here. Neither CI nor this
implementation establishes that the device's original `-48` is fixed, or that
any PS4 game works. Keep the PR draft; do not merge or change billing/budgets.
