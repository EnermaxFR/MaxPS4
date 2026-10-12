# Native PM4 submission and plaintext PS4 segments

This work follows PR #151. It does not demonstrate a PS4 game boot or playability.
No player interface is added.

## PM4 to Metal

The existing Metal diagnostic's PM4 mode calls the exported
`maxps4_pm4_encode_triangle` Objective-C++ ABI. Swift constructs a synthetic
packet but does not decode it. The C++ decoder/interpreter is the single packet
implementation. The diagnostic selects its triangle pipeline, not its textured
pipeline. Auto-index DRAW_INITIATOR uses SOURCE_SELECT=2.

The diagnostic bridge validates the entire stream before touching the encoder.
Only native-shader three-vertex auto-index draws are translated. Register writes
are rejected by this bridge because their PS4 rendering effects are not applied.
Invalid later packets cannot produce an earlier partial draw.

The semantic interpreter recognizes NOP, SET_CONTEXT_REG, SET_SH_REG,
SET_UCONFIG_REG and DRAW_INDEX_AUTO. Writes retain their bank and offset; each
draw records how many writes precede it. This is a command representation,
not a PS4 shader or register-to-Metal implementation. Conditional, compute and
unknown header flags are rejected. Streams are bounded to 65536 words and
4096 packets.

## FEX and guest memory

`maxps4_fex_engine_run_elf_pm4` runs the existing admitted static ELF program
through actual FEX, then copies PM4 from a readable segment in the same owned
ELF arena before it is unmapped. It uses original guest addresses and explicit
translation, validates range/alignment/permissions and returns owned words.
The runtime lock serializes execution and snapshot; no guest arena pointer
escapes to Swift or Metal. Invalid streams do not publish words.

`MaxPS4PM4Submission` exposes this diagnostic operation to Swift. The
macOS integration test chains FEX completion, the native memory snapshot,
C++ PM4 interpretation, Metal draw and pixel readback. The readable range is checked
before execution; command semantics are interpreted after CPU completion.

The guest code remains restricted to MOV EAX immediate, ADD EAX immediate,
NOP, final HLT and exact RIP-relative immediate stores proven to target guest
RW/non-executable data. The FEX integration fixture starts with zero-filled BSS;
three actual translated x86 stores generate its PM4 packet. General memory
access, branches, real PS4 system calls and sceGnm submission remain unimplemented.
This diagnostic does not claim that a game ran. See fex-generated-pm4.md for
admission guarantees and the distinction between Linux validation and real FEX
execution in macOS ARM64 CI.

## Separate plaintext PS4 segment component

`PS4ExecutableImage` validates the PS4 ELF64/x86-64/FreeBSD identity and
ET_SCE_EXEC, ET_SCE_DYNEXEC or ET_SCE_DYNAMIC. It stages PT_LOAD and
PT_SCE_RELRO at their declared virtual addresses in owned byte storage, with
zero-filled BSS and checked software translation permissions. It does not
allocate executable host pages.

For a supported plaintext/uncompressed SELF layout, block IDs identify ELF
program headers. Block order is not assumed to match program-header order.
Bounds, sizes, duplicate IDs, overlaps and allocation budget are validated.
Encrypted, signed, compressed and unknown block formats are unsupported.
Dynamic/TLS headers are retained and their unresolved requirements are exposed.
Format definitions were checked against shadPS4
`443f022450f6857256e780daa3d5a14029a748f7`, elf.h and elf.cpp.

This component is separately tested and compiled for iPhone ARM64. It is not
wired into the restricted FEX execution API or the app's game launch path.
Imports, relocations, TLS setup, process parameters, PS4 HLE and CPU address-space
integration must be implemented before staged PS4 images can execute.

## Validation

Local Linux checks: PM4 framing, command semantics, Metal preflight, actual
ELF-memory snapshot, bounded RIP-relative store admission and PS4 ELF/SELF
segment tests under ASan/UBSan. LeakSanitizer
must be disabled in the local ptrace sandbox; CI retains the sanitizer defaults.

GitHub Actions checks:

- `pm4-decoder.yml`: C++ sanitizer tests, recorded Objective-C encoder calls,
  Swift/C++ linkage and actual Metal completion/pixel readback where a Metal
  device exists, plus iOS ARM64 compile/typecheck/link.
- `fexcore-app.yml`: genuine macOS ARM64 FEX lifecycle/execution, same-arena PM4
  integration, full iOS Xcode app build and re-signable IPA verification.
- `ps4-executable.yml`: PS4 ELF and plaintext SELF segment sanitizer tests and
  iOS ARM64 object compilation.

Only test fixtures have been used. An IPA build is packaging evidence; physical
iPhone JIT execution and genuine PS4 title execution require separate proof.
