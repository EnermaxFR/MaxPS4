# JIT #169 reconciliation

This branch starts from main `28a20ce36c0a38e37163da6d5bdba928a27e5df9`
and the partial transplant `cf4ee174104f43cb6d60b3ede67df146a1401da7`.
It preserves main's Orbis linker, libkernel, guest-memory and frame paths.
It does not update main or install an IPA.

The completion repairs the unmatched runtime conditional, consolidates the
shared InitCore path, enforces one iOS context owner until context destruction,
declares the mailbox diagnostic for Swift, and restores unsupported-host
confirmation, launch-failure and usable-size definitions. The diagnostic uses
EAGAIN/EACCES for pending/inactive iOS sessions and interpolates actual errors.
The view retains main's newer diagnostics, clears old test results on activation
and release, and reports publication separately when the app becomes active.

The exact script/URL and dependency patches match #169. The pinned allocator
patch delegates iOS allocations to the prepared pool, removes late debugger
allocation, unwinds partial dispatcher/thread/cache construction, and excludes
the entire host guard page. The build enables exceptions in the FEX objects.
The pool checks the current nonce, every RX VM region, shared RW remaps,
protection ceilings, alias coherence and a native canary before InitCore.
Publication and CPUID do not establish JIT execution.

Local validation: both pinned-source patch suites pass (3 tests each), the
unsupported-host session ABI links and passes, and ELF interpreter/memory,
guest-memory and staged CPU admission pass with ASan/UBSan. LeakSanitizer cannot
run under this traced Linux sandbox; local runs use detect_leaks=0. Real FEX
local configuration requires Clang, absent here; GCC is rejected by upstream.
Swift, Mach VM, iOS linkage, Metal, leaks and IPA packaging require macOS CI.
The first CI run exposed Linux dispatcher compilation with exceptions disabled.
The linked-test CMake configuration now enables exceptions for the same FEX
object/base targets as the Darwin configuration; both use the unwind patch.

Keep the PR draft. Attach CI results for the exact final commit, including the
SideStore IPA and diagnostics. No physical iPhone execution or game launch is
validated. Follow docs/jit-167-validation.md for device evidence, including
init=0, execution=0, provider=2, stage=2, native backend=2, expected RAX and zero
leases after cleanup. A simulator or interpreter result is not iPhone proof.
