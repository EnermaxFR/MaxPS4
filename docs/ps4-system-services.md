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

## Thread and synchronization preparation

`PS4Threads` registers up to 64 guest identities; identity 1 is the bootstrap
thread and the CPU scheduler may attach/detach additional identities. This
does not create or run x86 threads. `scePthreadSelf` returns the dispatcher's
registered identity. Up to 256 explicit default private/error-check mutexes
support `scePthreadMutexInit`, `scePthreadMutexTrylock`, `scePthreadMutexUnlock`
and `scePthreadMutexDestroy`. Guest slots contain opaque 64-bit handles, never
host pointers. Ownership is checked, locked mutex destruction/detach returns
EBUSY, and stale handles are invalid after reset. Named mutexes validate the
guest string; attributes are unsupported. Mutexes require explicit init;
static/adaptive initialization and guest libc access to internal mutex layouts
are not supported. ScePthread errors use Orbis kernel codes, as their upstream
Orbis wrappers do, rather than the POSIX pthread positive-errno convention.
`scePthreadCreate/Join`, blocking lock/wait, condition variables, semaphores,
TLS, scheduling and cancellation remain unresolved. Host-worker tests verify
nonblocking mutual exclusion across eight registered guest identities.

## Engine integration

The engine owns a virtual-memory provider, checked memory adapter and libkernel
instance referencing its existing staged ELF. The C/Swift ABI exposes
`maxps4_ps4_resolve_libkernel`, `maxps4_fex_engine_mount_app0` and
`maxps4_fex_engine_call_libkernel`. Stage first, mount second, resolve a decoded
import, then dispatch a PS4 register snapshot with bootstrap guest_thread=1.
The return is the guest ABI result. IDs from the resolver are opaque and valid
only for this runtime's registry; they are not guest code addresses. Staging
attempts reset descriptors, mount, anonymous mappings and mutex/thread state,
including failed staging. Existing engine read/write/fetch/query calls also
access anonymous mappings. All engine operations use the existing runtime lock.
The Darwin/iOS CMake target includes the service implementations. This enables
explicit host-driven service calls; a guest CPU-to-HLE trampoline and dynamic
ELF/PRX relocation processing are still required to call them from a real game.

The full FEX runtime integration test now checks these APIs before InitCore:
file reads reach the existing staged image, mmap addresses are usable by engine
read/write, and failed restaging discards services. The local Linux environment
has no Apple SDK or vendored FEX dependencies, so the complete FEX/iPhone ARM64
build and this integration test must run in the existing macOS linkage CI.

## Validation

Run `bash scripts/test-ps4-system-services.sh` with C++17. The Ubuntu/macOS CI
matrix enables ASan/UBSan and treats compiler warnings as errors. Tests use
synthetic ELF/data fixtures only. A Linux ptrace environment may need
`ASAN_OPTIONS=detect_leaks=0` because LeakSanitizer cannot operate under ptrace;
address and undefined-behavior instrumentation remain enabled.
