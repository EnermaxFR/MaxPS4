# PS4 native guest staging

The imported-file diagnostic now stages a validated plaintext PS4 ELF/SELF into
engine-owned GuestELF mappings, the arena class used by the restricted FEX
execution path. The loader retains logical guest entry/segment addresses, copies
file-backed bytes, zero-fills BSS and applies host read/write protections without
PROT_EXEC. Host-page overlaps, writable executable segments and sparse address
spans above 32 MiB reject before mapping. Unknown/encrypted/compressed SELF
blocks still reject. Shared libraries reject pending linker ownership.

The C ABI exposes staging and exact readable-segment copies, never host pointers.
Invalid input clears previous staging and zeroes output metadata. A staged image
lives until restaging or engine close. Swift reads an imported file with a 64 MiB
cap, calls staging, reads one entry byte back and displays concrete CPU blockers.
Staging does not require or initialize JIT and cannot dispatch CPU instructions.

Blocker bits identify dynamic linking (1), TLS (2), and the CPU/address-space
contract (4, always set). A successful staging operation does not prove valid
relocations/imports, PS4 services or executable readiness. Dynamic and TLS headers
are retained by the parser, not initialized or linked. Guest code remains rebased:
RIP-relative offsets survive but arbitrary PS4 absolute addresses would not.
Passing a rebased PS4 entry directly to FEX would therefore be incorrect. The
existing bounded synthetic CPU admission path is unchanged and cannot consume
this staging API. iPhone ENOTSUP guards remain unchanged.

Local Linux evidence: PS4GuestStagingTest passes with ASan/UBSan (leak detection
unavailable under ptrace); PS4ExecutableImageTest, GuestELFMemoryTest (including
kernel read-only and NX fault tests), and 3 IOSSignatureAuditTest checks pass.
Fixtures are generated, not proprietary binaries. CI additionally tests the
opaque C/Swift API against the actual FEX runtime on macOS, compiles iPhone ARM64
and builds an IPA. An IPA build proves packaging only.

Next blockers in order: obtain and validate a legally accessible real PS4
executable; establish guest address mapping compatible with FEX's memory
operands; validate SCE dynamic imports/relocations/TLS and process startup;
observe a bounded first CPU trace under authorized device execution. Only those
traces can justify HLE services and PM4/Metal work for the game's actual commands.
No Kero Blaster binary or iPhone is present here. No PS4 instruction, first game
frame, gameplay or device compatibility is claimed.
