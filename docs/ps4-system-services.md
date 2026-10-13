# PS4 system services: implemented scope

The first libkernel service layer operates on checked logical PS4 addresses.
It can be exercised independently of FEX/JIT and never calls an x86 guest
function as an ARM64 host function. It does not make Kero Blaster playable.

## Filesystem

`PS4Libkernel::mount_app0` mounts a host directory read-only. Guest descriptors
3–255 have independent host descriptors and offsets; 0–2 are not host streams.
`sceKernelOpen`, `sceKernelRead`, `sceKernelClose`, `sceKernelLseek`,
`sceKernelStat` and `sceKernelFstat` work on regular files and directories.
Open supports RDONLY and the PS4 DIRECTORY flag; writes/creation/truncation
return EROFS and other flags return ENOTSUP. Reads are bounded to 1 MiB per call.
Seeking supports SET/CUR/END with signed 64-bit offsets. Invalid guest buffers
are checked before read changes a file offset. Stat uses an explicit 120-byte
little-endian Orbis layout, not the host struct, and clears write mode bits.
Linux does not supply birthtime through stat, so that field remains zero there.

Paths must be absolute under `/app0`; its root accepts `/app0` and `/app0/`.
Traversal, duplicate separators and ambiguous components are rejected.
Directory-relative `openat` walks reject symlinks at each component; special
files (including FIFOs) are unsupported. Remount/reset closes descriptors;
a failed mount leaves the old mount intact. The host must treat mounted content
as stable while the guest runs. Image mutation and dispatch must be serialized
by the owner. PS4 errors use Orbis errno numbers and signed `0x80020000 | errno`.

## Imports and ABI

`resolve_libkernel` recognizes the implemented sceKernel names and NIDs only
for library/module `libkernel`, version 1. ABI metadata was checked against
[shadPS4 file_system.h](https://github.com/shadps4-emu/shadPS4/blob/main/src/core/libraries/kernel/file_system.h)
and its registration table. POSIX variants are deliberately unresolved because
their `-1`/thread-local errno convention differs. An ELF/PRX loader must decode
library/module IDs before using the resolver. Encoded `nid#library#module`
strings are not guessed. The result is a service ID, never a host code pointer.
`invoke` consumes RDI/RSI/RDX/RCX/R8/R9 register values. Guest results are returned
as signed 64-bit values for the CPU adapter to place in RAX.

This begins import resolution; it does not parse dynamic tables, apply
relocations or install guest trampolines. Existing linker/TLS/address-space
execution blockers remain in force, and the restricted FEX syscall handler
remains ENOSYS.

## Validation

## Anonymous virtual memory and clocks

`PS4VirtualMemory` supplies zero-filled logical anonymous PRIVATE mappings in
16 KiB PS4 pages with a 32 MiB backing budget and at most 128 allocations.
`sceKernelMmap` supports only flags `0x1002`, a zero address hint, fd=-1 and
offset=0. Its seventh output-pointer argument is read at guest entry RSP+8.
PROT_READ/WRITE/EXEC are translated to ELF permissions; writable/executable
mappings are rejected. Guest RX is host data, never an iOS JIT allocation.
`sceKernelMprotect` changes permissions per page; `sceKernelMunmap` removes
whole or partial page intervals. Both currently require the entire interval
to lie in one live anonymous allocation; holes and ELF segments return ENOMEM.
Partially unmapped pages become inaccessible immediately, but the backing
budget is released only when the allocation's last page is unmapped. File,
shared and fixed mappings, hints, direct memory and GPU memory are unsupported.
Allocations avoid staged ELF segments and other anonymous allocations.
The optional `PS4DynamicMemory` provider preserves existing ELF-only clients.
The owner must reset dynamic memory on restaging and serialize all accesses.

`sceKernelClockGettime` and `sceKernelClockGetres` support PS4 REALTIME=0 and
MONOTONIC=4, using the corresponding host clocks. `sceKernelGettimeofday`
returns Unix seconds/microseconds; `sceKernelGetProcessTime` returns elapsed
monotonic microseconds since service creation/reset (elapsed time, not CPU time).
Unsupported clock IDs return EINVAL. No fabricated network/TSC clock is exposed.

## Validation

Run `bash scripts/test-ps4-system-services.sh` with C++17. The Ubuntu/macOS CI
matrix enables ASan/UBSan and treats compiler warnings as errors. Tests use
synthetic ELF/data fixtures only. A Linux ptrace environment may need
`ASAN_OPTIONS=detect_leaks=0` because LeakSanitizer cannot operate under ptrace;
address and undefined-behavior instrumentation remain enabled.
