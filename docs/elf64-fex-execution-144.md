# ELF64 execution through FEXCore — issue #144

`maxps4_fex_engine_run_elf` reads a bounded static ELF64 little-endian x86-64
ET_EXEC image, preflights every PT_LOAD, maps the segments in an owned anonymous
arena, copies file bytes and zero-fills BSS, then applies final protections.
FEX's thread RIP is the mapped pointer corresponding to the ELF entry. The
original guest entry is returned separately for diagnostics. Relative segment
spacing is preserved; absolute guest addresses are rebased, not installed with
MAP_FIXED. This is a controlled position-independent demonstration, not a PS4
address space or a general ET_EXEC loader.

The admitted instruction stream, from entry to the end of its file-backed
segment, consists only of MOV EAX,imm32, ADD EAX,imm32, NOP and a final HLT. MOV
must precede ADD, and at least one MOV is required. Every instruction, including
HLT, counts against the budget. Rejection happens before FEX execution. No
branches, memory operands, calls, returns, relocations, dynamic metadata, section
tables or syscalls are supported. This admission rule guarantees termination
and prevents guest faults without installing process signal handlers. Supporting
more instructions requires fault containment and a runtime budget mechanism.

Limits: 32 MiB file and address span, 64 segments, 64 KiB entry instruction
stream, 1–1,000,000 instruction budget. Header/table bounds, sizes, integer
wraparound, alignment congruence and file-backed executable entry are checked.
Every segment must be readable; writable executable segments and shared host
pages between segments are rejected. Unmapped holes remain PROT_NONE. Guest RX
is host read-only data for FEX's translator, never host executable memory. Guest
RW is host read/write and non-executable. ARM64 generated code continues to use
the existing authorized JIT provider. Thread, stack, call-return stack and ELF
mappings are released in dependency order; backend stays zero on errors.

The compatibility probe builds a tiny ELF and calls the same loader. The actual
Swift diagnostic and `MaxPS4SyntheticELFFEXBridge.run(url:)` pass ELF file bytes
to the native ABI. The injectable Swift admission tests retain their exact
fixture contract. The diagnostic reports original ELF entry, JIT status, actual
backend, RAX and cleanup. Its explicit interpreter fallback does not count as
FEX execution. No PS4 syscall, SELF decryption, game or graphics support is claimed.

## Automated checks

Locally on Linux x86-64:

- `GuestELFMemoryTest.cpp`: passed with GCC C++23, `-Wall -Wextra -Werror`.
  Covers malformed ELF headers and segments, truncation, permissions,
  overflow, entry in BSS, overlap, span limits, CPU admission and budgets;
  actual writable data/read-only code, kernel-denied writes and host execution,
  isolation, BSS, unmapping (`mincore`) and repeated load/cleanup.
- `NativeJITPipelineTest.cpp`: passed; translator/interpreter regression only.
- `FEXDiagnosticsTest.py`: 3 tests passed.
- `StikScriptTest.js`: passed.

CI smoke also runs the existing Swift CPU/ELF/guest-memory prototype suite.
The SideStore IPA workflow runs the same native ELF/memory tests, the pinned
real FEX ARM64 macOS lifecycle suite and the Swift native bridge. FEX tests
verify RAX=123 and RAX=77 from changed ELF bytes/entry, 32-bit wraparound,
budget and unsupported-code errors, repeated execution and strict backend
separation. The IPA verifier requires the application to import and the runtime
to export `maxps4_fex_engine_run_elf`. Heavy runs the upstream compilation gates.
Exact workflow URLs and outcomes are recorded in the PR description.

## Remaining physical validation

An authorized iPhone run is required before merging: enable JIT using the
existing StikDebug flow, run the FEX diagnostic, and record entry 0x1000,
JIT status 0, backend FEXCore JIT ARM64, RAX=42 and zero active leases after
cleanup. Repeat execution and verify that an unavailable JIT reports its error
and any interpreter result explicitly. macOS FEX execution, iOS compilation and
IPA verification do not prove iPhone JIT execution. This issue remains open
until that device validation is recorded.
