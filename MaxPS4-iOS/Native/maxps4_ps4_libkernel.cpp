#include "maxps4_ps4_libkernel.h"
#include "maxps4_fex_runtime.h"
#include "maxps4_ps4_kernel_path.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <random>
#include <thread>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace maxps4::ps4 {
Error host_error(int e) {
    switch (e) {
    case 0: return ok;
    case EPERM: return perm;
    case ENOENT: return noent;
    case EINTR: return intr;
    case EIO: return io;
    case EBADF: return badf;
    case ENOMEM: return nomem;
    case EACCES: return access;
    case EFAULT: return fault;
    case EBUSY: return busy;
    case ENOTDIR: return notdir;
    case EISDIR: return isdir;
    case EINVAL: return inval;
    case EMFILE: case ENFILE: return mfile;
    case EROFS: return rofs;
    case EAGAIN: return again;
    case ELOOP: return loop;
    case ENAMETOOLONG: return nametoolong;
    case ENOSYS: return nosys;
    case EOVERFLOW: return overflow;
    case ENOTSUP: return notsup;
    default: return io; // Never leak a host errno with a different Orbis meaning.
    }
}
}
namespace maxps4 {
using namespace ps4;
namespace {
using Binding = PS4KernelExport;
// scePthreadOnce(once, init) as guest code, since init is guest code. The
// control word (FreeBSD): 0 never done, 1 done, 2 in progress. The first
// caller moves it 0 -> 2, calls init and stores 1; others wait for 1.
// Returns 0. Called with RSP = 8 (mod 16), so PUSH RDI aligns the CALL.
constexpr uint8_t once_code[] = {
    0x8b,0x07, 0x83,0xf8,0x01, 0x74,0x21,      // 00 MOV EAX,[RDI]; CMP EAX,1; JE done
    0x31,0xc0, 0xb9,0x02,0x00,0x00,0x00,       // 07 XOR EAX,EAX; MOV ECX,2
    0xf0,0x0f,0xb1,0x0f, 0x75,0x0d,            // 0e LOCK CMPXCHG [RDI],ECX; JNZ wait
    0x57, 0xff,0xd6, 0x5f,                     // 14 PUSH RDI; CALL RSI; POP RDI
    0xc7,0x07,0x01,0x00,0x00,0x00,             // 18 MOV DWORD [RDI],1
    0x31,0xc0, 0xc3,                           // 1e XOR EAX,EAX; RET
    0xf3,0x90, 0x83,0x3f,0x01, 0x75,0xf9,      // 21 wait: PAUSE; CMP DWORD [RDI],1; JNE wait
    0x31,0xc0, 0xc3};                          // 28 done: XOR EAX,EAX; RET
static_assert(sizeof(once_code) == 0x2b, "pthread_once routine layout");
// ABI metadata checked against shadPS4 kernel/file_system.{h,cpp}; original
// implementation below. Only sceKernel variants, not POSIX errno/TLS variants.
constexpr Binding bindings[] = {
    {"sceKernelOpen", "1G3lF1Gg1k8", PS4KernelService::open},
    {"sceKernelRead", "Cg4srZ6TKbU", PS4KernelService::read},
    {"sceKernelClose", "UK2Tl2DWUns", PS4KernelService::close},
    {"sceKernelLseek", "oib76F-12fk", PS4KernelService::lseek},
    {"sceKernelStat", "eV9wAD2riIA", PS4KernelService::stat},
    {"sceKernelFstat", "kBwCPsYX-m4", PS4KernelService::fstat},
    {"sceKernelMmap", "PGhQHd-dzv8", PS4KernelService::mmap},
    {"sceKernelMunmap", "cQke9UuBQOk", PS4KernelService::munmap},
    {"sceKernelMprotect", "vSMAm3cxYTY", PS4KernelService::mprotect},
    {"sceKernelClockGettime", "QBi7HCK03hw", PS4KernelService::clock_gettime},
    {"sceKernelClockGetres", "wRYVA5Zolso", PS4KernelService::clock_getres},
    {"sceKernelGettimeofday", "ejekcaNQNq0", PS4KernelService::gettimeofday},
    {"sceKernelGetProcessTime", "4J2sUJmuHZQ", PS4KernelService::process_time},
    {"scePthreadSelf", "aI+OeCz8xrQ", PS4KernelService::thread_self},
    {"scePthreadMutexInit", "cmo1RIYva9o", PS4KernelService::mutex_init},
    {"scePthreadMutexTrylock", "upoVrzMHFeE", PS4KernelService::mutex_trylock},
    {"scePthreadMutexUnlock", "tn3VlD0hG60", PS4KernelService::mutex_unlock},
    {"scePthreadMutexDestroy", "2Of0f+3mhhE", PS4KernelService::mutex_destroy},
    // NIDs from shadPS4 kernel/threads/mutex.cpp and condvar.cpp (Orbis variants).
    {"scePthreadMutexLock", "9UK1vLZQft4", PS4KernelService::mutex_lock},
    {"scePthreadCondInit", "2Tb92quprl0", PS4KernelService::cond_init},
    {"scePthreadCondDestroy", "g+PZd2hiacg", PS4KernelService::cond_destroy},
    {"scePthreadCondWait", "WKAXJ4XBPQ4", PS4KernelService::cond_wait},
    {"scePthreadCondTimedwait", "BmMjYxmew1w", PS4KernelService::cond_timedwait},
    {"scePthreadCondSignal", "kDh-NfxgMtE", PS4KernelService::cond_signal},
    {"scePthreadCondBroadcast", "JGgj7Uvrl+A", PS4KernelService::cond_broadcast},
    // NIDs from shadPS4 kernel/threads/pthread.cpp, pthread_attr.cpp and
    // mutex.cpp (Orbis variants). Scheduling, affinity, guard size and the
    // mutex protocol are accepted without effect.
    {"scePthreadDetach", "4qGrR6eoP9Y", PS4KernelService::thread_detach},
    {"scePthreadAttrInit", "nsYoNRywwNg", PS4KernelService::attr_init},
    {"scePthreadAttrDestroy", "62KCwEMmzcM", PS4KernelService::attr_destroy},
    {"scePthreadAttrSetdetachstate", "-Wreprtu0Qs", PS4KernelService::attr_set_detach},
    {"scePthreadAttrGetdetachstate", "JaRMy+QcpeU", PS4KernelService::attr_get_detach},
    {"scePthreadAttrSetstacksize", "UTXzJbWhhTE", PS4KernelService::attr_set_stack},
    {"scePthreadAttrGetstacksize", "-fA+7ZlGDQs", PS4KernelService::attr_get_stack},
    {"scePthreadAttrSetschedparam", "DzES9hQF4f4", PS4KernelService::attr_accept},
    {"scePthreadAttrSetinheritsched", "eXbUSpEaTsA", PS4KernelService::attr_accept},
    {"scePthreadAttrSetschedpolicy", "4+h9EzwKF4I", PS4KernelService::attr_accept},
    {"scePthreadAttrSetaffinity", "3qxgM4ezETA", PS4KernelService::attr_accept},
    {"scePthreadAttrSetguardsize", "El+cQ20DynU", PS4KernelService::attr_accept},
    {"scePthreadMutexattrInit", "F8bUHwAG284", PS4KernelService::mutexattr_init},
    {"scePthreadMutexattrInitForInternalLibc", "n2MMpvU8igI", PS4KernelService::mutexattr_init},
    {"scePthreadMutexattrDestroy", "smWEktiyyG0", PS4KernelService::mutexattr_destroy},
    {"scePthreadMutexattrSettype", "iMp8QpE+XO4", PS4KernelService::mutexattr_settype},
    {"scePthreadMutexattrGettype", "gquEhBrS2iw", PS4KernelService::mutexattr_gettype},
    {"scePthreadMutexattrSetprotocol", "1FGvU0i9saQ", PS4KernelService::mutexattr_accept},
    // NIDs from shadPS4 kernel/threads/pthread.cpp (Orbis variants).
    {"scePthreadCreate", "6UgtwV+0zb4", PS4KernelService::thread_create},
    {"scePthreadJoin", "onNY9Byn-W8", PS4KernelService::thread_join},
    {"scePthreadExit", "3kg7rT0NQIs", PS4KernelService::thread_exit},
    // NID from shadPS4 kernel/threads/tcb.cpp (RegisterRtld).
    {"__tls_get_addr", "vNe1w4diLCs", PS4KernelService::tls_get_addr},
    // What a game's libc.prx imports. NIDs from shadPS4 kernel/*.cpp and
    // threads/*.cpp; the last few, absent there, from the NID of their name.
    // POSIX-convention functions set the thread's errno and return -1.
    {"__Error", "9BcDykPmo1I", PS4KernelService::errno_location, "libkernel", true},
    {"exit", "6Z83sYWFlA8", PS4KernelService::process_exit, "libkernel", true, true},
    {"__stack_chk_fail", "Ou3iL1abvng", PS4KernelService::abort_process, "libkernel", true, true},
    {"sceKernelDebugRaiseException", "OMDRKKAZ8I4", PS4KernelService::abort_process, "libkernel", false, true},
    {"sceKernelDebugRaiseExceptionOnReleaseMode", "zE-wXIZjLoM", PS4KernelService::abort_process, "libkernel", false, true},
    {"nanosleep", "NhpspxdjEKU", PS4KernelService::nanosleep, "libkernel", true},
    {"_nanosleep", "yS8U2TGCe1A", PS4KernelService::nanosleep, "libkernel", true},
    {"sceKernelUsleep", "1jfXLRVzisc", PS4KernelService::usleep},
    {"clock_gettime", "lLMT9vJAck0", PS4KernelService::clock_gettime, "libkernel", true},
    {"gettimeofday", "n88vx3C5nW8", PS4KernelService::gettimeofday, "libkernel", true},
    {"getpagesize", "k+AXqu2-eBc", PS4KernelService::page_size, "libkernel", true},
    {"sched_yield", "6XG4B33N09g", PS4KernelService::yield, "libkernel", true},
    {"pthread_yield", "T72hz6ffq08", PS4KernelService::yield, "libkernel", true},
    {"pthread_equal", "3PtV6p3QNX4", PS4KernelService::thread_equal, "libkernel", true},
    {"pthread_getspecific", "eoht7mQOCmo", PS4KernelService::get_specific, "libkernel", true},
    {"scePthreadSetspecific", "+BzXYkqYeLE", PS4KernelService::set_specific},
    {"scePthreadKeyCreate", "geDaqgH9lTg", PS4KernelService::key_create},
    {"scePthreadKeyDelete", "PrdHuuDekhY", PS4KernelService::key_delete},
    {"scePthreadOnce", "14bOACANTBo", PS4KernelService::once, "libkernel", false, false, 0, once_code, sizeof(once_code)},
    {"scePthreadRwlockInit", "6ULAa0fq4jA", PS4KernelService::rwlock_init},
    {"scePthreadRwlockDestroy", "BB+kb08Tl9A", PS4KernelService::rwlock_destroy},
    {"scePthreadRwlockRdlock", "Ox9i0c7L5w0", PS4KernelService::rwlock_rdlock},
    {"scePthreadRwlockWrlock", "mqdNorrB+gI", PS4KernelService::rwlock_wrlock},
    {"scePthreadRwlockTryrdlock", "XD3mDeybCnk", PS4KernelService::rwlock_tryrdlock},
    {"scePthreadRwlockTrywrlock", "bIHoZCTomsI", PS4KernelService::rwlock_trywrlock},
    {"scePthreadRwlockUnlock", "+L98PIbGttk", PS4KernelService::rwlock_unlock},
    {"scePthreadSetprio", "W0Hpm2X0uPE", PS4KernelService::succeed},
    {"scePthreadGetprio", "1tKyG7RlMJo", PS4KernelService::thread_getprio},
    {"scePthreadGetaffinity", "rcrVFJsQWRY", PS4KernelService::thread_getaffinity},
    {"scePthreadSetaffinity", "bt3CTBKmGyI", PS4KernelService::succeed},
    {"scePthreadAttrGetaffinity", "8+s5BzZjxSg", PS4KernelService::attr_getaffinity},
    {"scePthreadAttrGet", "x1X76arYMxU", PS4KernelService::attr_get_np},
    {"sigprocmask", "aPcyptbOiZs", PS4KernelService::succeed, "libkernel", true},
    {"_sigprocmask", "6xVpy0Fdq+I", PS4KernelService::succeed, "libkernel", true},
    {"sigfillset", "VkTAsrZDcJ0", PS4KernelService::sigfillset, "libkernel", true},
    {"signal", "VADc3MNQ3cM", PS4KernelService::signal, "libkernel", true},
    {"sceKernelIsNeoMode", "WslcK1FQcGI", PS4KernelService::succeed},
    {"sceKernelIsAddressSanitizerEnabled", "jh+8XiK4LeE", PS4KernelService::succeed},
    {"sceKernelConvertUtcToLocaltime", "-o5uEDpN+oY", PS4KernelService::utc_to_local},
    {"sceKernelConvertLocaltimeToUtc", "0NTHN1NKONI", PS4KernelService::local_to_utc},
    {"sceKernelGetProcParam", "959qrazPIrg", PS4KernelService::proc_param},
    {"_sceKernelRtldSetApplicationHeapAPI", "p5EcQeEeJAE", PS4KernelService::succeed},
    {"sceKernelGetModuleInfoFromAddr", "f7KBOafysXo", PS4KernelService::module_info_from_addr},
    {"sceKernelGetModuleInfoForUnwind", "RpQJJVKTiFM", PS4KernelService::module_info_from_addr},
    {"sceKernelInternalMemoryGetModuleSegmentInfo", "-YTW+qXc3CQ", PS4KernelService::module_info_from_addr},
    {"__elf_phdr_match_addr", "Fjc4-n1+y2g", PS4KernelService::succeed, "libkernel", true},
    {"sceKernelPrintBacktraceWithModuleInfo", "Wl2o5hOVZdw", PS4KernelService::succeed},
    {"_is_signal_return", "crb5j7mkk1c", PS4KernelService::succeed, "libkernel", true},
    {"sigreturn", "mo0bFmWppIw", PS4KernelService::succeed, "libkernel", true},
    {"__pthread_cxa_finalize", "kbw4UHHSYy0", PS4KernelService::succeed, "libkernel", true},
    {"(libkernel hHlZQUnlxSM)", "hHlZQUnlxSM", PS4KernelService::succeed},
    // Data objects, filled by PS4Libkernel::initialize_data. "MaxPS4errno" is
    // engine-internal (no game imports it): the per-thread errno cells.
    {"__stack_chk_guard", "f7uOxY9mM1U", PS4KernelService::stack_chk_guard, "libkernel", false, false, 8},
    {"__progname", "djxxOmW6-aw", PS4KernelService::progname, "libkernel", false, false, 64},
    {"(errno table)", "MaxPS4errno", PS4KernelService::errno_table, "libkernel", false, false,
        uint32_t(PS4Threads::thread_limit * 8)}
};
uint64_t monotonic_us() {
    timespec t{};
    if (::clock_gettime(CLOCK_MONOTONIC, &t) != 0) return 0;
    return uint64_t(t.tv_sec) * 1000000 + uint64_t(t.tv_nsec) / 1000;
}
class HostFD {
public:
    explicit HostFD(int fd) : fd(fd) {}
    ~HostFD() { if (fd >= 0) ::close(fd); }
    HostFD(const HostFD&) = delete;
    HostFD& operator=(const HostFD&) = delete;
    int fd;
};
}
const PS4KernelExport* libkernel_exports(size_t& count) {
    count = sizeof(bindings) / sizeof(bindings[0]);
    return bindings;
}
PS4KernelService resolve_libkernel(std::string_view symbol, std::string_view library,
                                   std::string_view module, uint32_t version) {
    // A binding's library names its module too (libkernel/libkernel).
    if (version != 1 || library != module) return PS4KernelService::unresolved;
    for (const auto& binding : bindings)
        if (library == binding.library && (symbol == binding.name || symbol == binding.nid)) return binding.service;
    return PS4KernelService::unresolved;
}
PS4Libkernel::PS4Libkernel(PS4GuestMemory& memory, PS4VirtualMemory* dynamic)
    : memory_(memory), dynamic_(dynamic), threads_(memory), started_us_(monotonic_us()) { files_.fill(-1); }
PS4Libkernel::~PS4Libkernel() { reset(); }
void PS4Libkernel::close_files() {
    for (int& fd : files_) { if (fd >= 0) ::close(fd); fd = -1; }
}
void PS4Libkernel::reset() {
    std::lock_guard<std::mutex> guard(lock_);
    close_files();
    if (root_ >= 0) ::close(root_);
    root_ = -1;
    if (dynamic_) dynamic_->reset();
    threads_.reset();
    errno_table_ = errno_table_size_ = process_param_ = 0;
    started_us_ = monotonic_us();
}
int64_t PS4Libkernel::mount_app0(const char* directory) {
    std::lock_guard<std::mutex> guard(lock_);
    if (!directory || !*directory) return kernel_error(inval);
    const int fd = ::open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return kernel_error(host_error(errno));
    close_files();
    if (root_ >= 0) ::close(root_);
    root_ = fd;
    return 0;
}
Error PS4Libkernel::pathname(uint64_t address, char* path) {
    char validated[PS4KernelPath::max_path];
    // The mount root itself is a legitimate directory for stat/open.
    if (!memory_.read_cstring(address, validated, sizeof(validated))) return fault;
    if (std::strcmp(validated, "/app0") == 0 || std::strcmp(validated, "/app0/") == 0) {
        path[0] = '\0'; return ok;
    }
    switch (PS4KernelPath::read(memory_, address, validated)) {
    case PS4KernelPathResult::invalid_guest_address: return fault;
    case PS4KernelPathResult::invalid_path: return inval;
    case PS4KernelPathResult::unsupported_mount: return noent;
    case PS4KernelPathResult::ok: break;
    }
    std::strcpy(path, validated + 6);
    return ok;
}
int PS4Libkernel::open_path(const char* path, bool directory, Error& error) const {
    if (root_ < 0) { error = noent; return -1; }
    // Walk from a held directory descriptor; reject symlinks at EVERY component.
    // Do not canonicalize then reopen a host path (a race can escape /app0).
    HostFD parent(::fcntl(root_, F_DUPFD_CLOEXEC, 0));
    if (parent.fd < 0) { error = host_error(errno); return -1; }
    char relative[PS4KernelPath::max_path];
    std::strcpy(relative, *path ? path : ".");
    char* component = relative;
    while (char* separator = std::strchr(component, '/')) {
        *separator = '\0';
        const int next = ::openat(parent.fd, component,
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (next < 0) { error = host_error(errno); return -1; }
        ::close(parent.fd); parent.fd = next;
        component = separator + 1;
    }
    // NONBLOCK avoids blocking on FIFOs/devices before fstat rejects them.
    const int fd = ::openat(parent.fd, component, O_RDONLY | O_CLOEXEC |
        O_NOFOLLOW | O_NONBLOCK | (directory ? O_DIRECTORY : 0));
    if (fd < 0) { error = host_error(errno); return -1; }
    struct ::stat info{};
    if (::fstat(fd, &info) != 0) { error = host_error(errno); ::close(fd); return -1; }
    if (!S_ISREG(info.st_mode) && !S_ISDIR(info.st_mode)) {
        error = notsup; ::close(fd); return -1;
    }
    error = ok; return fd;
}
bool PS4Libkernel::writable(uint64_t address, size_t size) const {
    return address && memory_.permitted(address, size, 2) && !memory_.permitted(address, size, 1);
}
int64_t PS4Libkernel::open(const PS4KernelCall& c) {
    const uint32_t flags = uint32_t(c.rsi);
    if ((flags & 3U) == 3U) return kernel_error(inval);
    if (flags & (3U | 0x8U | 0x200U | 0x400U)) return kernel_error(rofs);
    if (flags & ~0x20000U) return kernel_error(notsup); // Only RDONLY and DIRECTORY.
    char path[PS4KernelPath::max_path];
    Error e = pathname(c.rdi, path);
    if (e) return kernel_error(e);
    size_t slot = 3; // Standard streams are not host stdin/stdout/stderr aliases.
    while (slot < files_.size() && files_[slot] >= 0) ++slot;
    if (slot == files_.size()) return kernel_error(mfile);
    const int fd = open_path(path, (flags & 0x20000U) != 0, e);
    if (fd < 0) return kernel_error(e);
    files_[slot] = fd;
    return int64_t(slot);
}
int64_t PS4Libkernel::close(int32_t fd) {
    if (fd < 3 || size_t(fd) >= files_.size() || files_[fd] < 0) return kernel_error(badf);
    const int host = files_[fd]; files_[fd] = -1;
    // Never retry close after EINTR: the host descriptor may already be released.
    return ::close(host) == 0 ? 0 : kernel_error(host_error(errno));
}
int64_t PS4Libkernel::read(const PS4KernelCall& c) {
    const int32_t fd = signed32(c.rdi);
    if (fd < 3 || size_t(fd) >= files_.size() || files_[fd] < 0) return kernel_error(badf);
    if (c.rdx > transfer_limit) return kernel_error(inval);
    const size_t count = size_t(c.rdx);
    if (count && !writable(c.rsi, count)) return kernel_error(fault);
    struct ::stat info{};
    if (::fstat(files_[fd], &info) != 0) return kernel_error(host_error(errno));
    if (S_ISDIR(info.st_mode)) return kernel_error(isdir);
    if (!count) return 0;
    std::vector<uint8_t> buffer(count); // Allocate before advancing file offset.
    ssize_t got;
    do { got = ::read(files_[fd], buffer.data(), count); } while (got < 0 && errno == EINTR);
    if (got < 0) return kernel_error(host_error(errno));
    if (got && !memory_.write(c.rsi, buffer.data(), size_t(got))) return kernel_error(fault);
    return got;
}
int64_t PS4Libkernel::lseek(const PS4KernelCall& c) {
    const int32_t fd = signed32(c.rdi);
    if (fd < 3 || size_t(fd) >= files_.size() || files_[fd] < 0) return kernel_error(badf);
    int whence;
    switch (uint32_t(c.rdx)) {
    case 0: whence = SEEK_SET; break;
    case 1: whence = SEEK_CUR; break;
    case 2: whence = SEEK_END; break;
    default: return kernel_error(inval);
    }
    static_assert(sizeof(off_t) == 8, "PS4 requires 64-bit file offsets");
    const off_t result = ::lseek(files_[fd], signed64(c.rsi), whence);
    return result < 0 ? kernel_error(host_error(errno)) : int64_t(result);
}
int64_t PS4Libkernel::stat(const PS4KernelCall& c, bool by_descriptor) {
    if (!writable(c.rsi, stat_size)) return kernel_error(fault);
    struct ::stat info{};
    if (by_descriptor) {
        const int32_t fd = signed32(c.rdi);
        if (fd < 3 || size_t(fd) >= files_.size() || files_[fd] < 0) return kernel_error(badf);
        if (::fstat(files_[fd], &info) != 0) return kernel_error(host_error(errno));
    } else {
        char path[PS4KernelPath::max_path];
        Error e = pathname(c.rdi, path);
        if (e) return kernel_error(e);
        HostFD fd(open_path(path, false, e));
        if (fd.fd < 0) return kernel_error(e);
        if (::fstat(fd.fd, &info) != 0) return kernel_error(host_error(errno));
    }
    uint8_t wire[stat_size] = {};
    // OrbisKernelStat is 120 bytes; encode fields explicitly, never copy host stat.
    put_le(wire, 0, info.st_dev, 4); put_le(wire, 4, info.st_ino, 4);
    const uint16_t mode = (S_ISDIR(info.st_mode) ? 0040000 : 0100000) |
        (uint16_t(info.st_mode) & 0777U & ~0222U);
    put_le(wire, 8, mode, 2); put_le(wire, 10, info.st_nlink, 2);
    put_le(wire, 12, info.st_uid, 4); put_le(wire, 16, info.st_gid, 4);
#if defined(__APPLE__)
    const auto at = info.st_atimespec, mt = info.st_mtimespec, ct = info.st_ctimespec;
    put_le(wire, 104, info.st_birthtimespec.tv_sec, 8);
    put_le(wire, 112, info.st_birthtimespec.tv_nsec, 8);
#else
    const auto at = info.st_atim, mt = info.st_mtim, ct = info.st_ctim;
#endif
    put_le(wire, 24, at.tv_sec, 8); put_le(wire, 32, at.tv_nsec, 8);
    put_le(wire, 40, mt.tv_sec, 8); put_le(wire, 48, mt.tv_nsec, 8);
    put_le(wire, 56, ct.tv_sec, 8); put_le(wire, 64, ct.tv_nsec, 8);
    put_le(wire, 72, info.st_size, 8); put_le(wire, 80, info.st_blocks, 8);
    put_le(wire, 88, info.st_blksize, 4);
    return memory_.write(c.rsi, wire, sizeof(wire)) ? 0 : kernel_error(fault);
}
int64_t PS4Libkernel::mmap(const PS4KernelCall& c) {
    if (!dynamic_) return kernel_error(nosys);
    // The seventh System V argument is at entry RSP+8, after return address.
    uint8_t pointer[8];
    if (!c.rsp || c.rsp > UINT64_MAX - 8 || !memory_.read(c.rsp + 8, pointer, 8))
        return kernel_error(fault);
    const uint64_t output = get_le(pointer, 8);
    if (!writable(output, 8)) return kernel_error(fault);
    uint64_t mapped = 0;
    const Error e = dynamic_->map(c.rdi, c.rsi, uint32_t(c.rdx), uint32_t(c.rcx),
                                 signed32(c.r8), signed64(c.r9), mapped);
    if (e) return kernel_error(e);
    put_le(pointer, 0, mapped, 8);
    if (!memory_.write(output, pointer, 8)) {
        dynamic_->unmap(mapped, c.rsi);
        return kernel_error(fault);
    }
    return 0;
}
int64_t PS4Libkernel::clock(const PS4KernelCall& c, bool resolution) {
    clockid_t id;
    switch (uint32_t(c.rdi)) {
    case 0: id = CLOCK_REALTIME; break;
    case 4: id = CLOCK_MONOTONIC; break;
    default: return kernel_error(inval);
    }
    if (!writable(c.rsi, 16)) return kernel_error(fault);
    timespec time{};
    const int result = resolution ? ::clock_getres(id, &time) : ::clock_gettime(id, &time);
    if (result != 0) return kernel_error(host_error(errno));
    uint8_t wire[16]{};
    put_le(wire, 0, time.tv_sec, 8); put_le(wire, 8, time.tv_nsec, 8);
    return memory_.write(c.rsi, wire, sizeof(wire)) ? 0 : kernel_error(fault);
}
int64_t PS4Libkernel::gettimeofday(uint64_t output) {
    if (!writable(output, 16)) return kernel_error(fault);
    timeval time{};
    if (::gettimeofday(&time, nullptr) != 0) return kernel_error(host_error(errno));
    uint8_t wire[16]{};
    put_le(wire, 0, time.tv_sec, 8); put_le(wire, 8, time.tv_usec, 8);
    return memory_.write(output, wire, sizeof(wire)) ? 0 : kernel_error(fault);
}
int64_t PS4Libkernel::posix_result(int64_t result, uint64_t thread) {
    // Orbis kernel errors are kernel_error(1..) = -(0x7ffe0000 - e).
    constexpr int64_t first = -int64_t(0x7ffe0000) + 1, last = -int64_t(0x7ffe0000) + 0xffff;
    if (result < first || result > last) return result;
    const size_t index = threads_.index(thread);
    if (errno_table_ && index < errno_table_size_ / 8) {
        uint8_t wire[4];
        put_le(wire, 0, uint64_t(result + 0x7ffe0000), 4);
        (void)memory_.write(errno_table_ + index * 8, wire, sizeof(wire));
    }
    return -1;
}
int64_t PS4Libkernel::invoke(PS4KernelService service, const PS4KernelCall& c, uint64_t thread, bool posix) {
    bool handled = false;
    int64_t result = extended(service, c, thread, handled);
    if (!handled) result = invoke_service(service, c, thread);
    return posix ? posix_result(result, thread) : result;
}
int64_t PS4Libkernel::extended(PS4KernelService service, const PS4KernelCall& c, uint64_t thread, bool& handled) {
    handled = true;
    auto write = [&](uint64_t address, uint64_t value, size_t width) {
        uint8_t wire[8];
        put_le(wire, 0, value, width);
        return address && memory_.write(address, wire, width);
    };
    using S = PS4KernelService;
    switch (service) {
    case S::errno_location: {
        const size_t index = threads_.index(thread);
        return errno_table_ && index < errno_table_size_ / 8 ? int64_t(errno_table_ + index * 8) : kernel_error(fault);
    }
    case S::nanosleep: {
        // nanosleep(const timespec* request, timespec* remaining).
        uint8_t wire[16];
        if (!memory_.read(c.rdi, wire, sizeof(wire))) return kernel_error(fault);
        const int64_t seconds = signed64(get_le(wire, 8)), nanoseconds = signed64(get_le(wire + 8, 8));
        if (seconds < 0 || nanoseconds < 0 || nanoseconds >= 1000000000) return kernel_error(inval);
        const Error slept = threads_.sleep(uint64_t(seconds) * 1000000 + uint64_t(nanoseconds) / 1000);
        if (c.rsi && (!write(c.rsi, 0, 8) || !write(c.rsi + 8, 0, 8))) return kernel_error(fault);
        return kernel_error(slept);
    }
    case S::usleep: return kernel_error(threads_.sleep(c.rdi));
    case S::page_size: return 16384; // PS4 pages are 16 KiB.
    case S::yield: std::this_thread::yield(); return 0;
    case S::thread_equal: return c.rdi == c.rsi;
    case S::key_create: return kernel_error(threads_.key_create(c.rdi, c.rsi));
    case S::key_delete: return kernel_error(threads_.key_delete(c.rdi));
    case S::get_specific: return int64_t(threads_.get_specific(c.rdi, thread));
    case S::set_specific: return kernel_error(threads_.set_specific(c.rdi, c.rsi, thread));
    case S::rwlock_init: return kernel_error(threads_.rwlock_init(c.rdi));
    case S::rwlock_destroy: return kernel_error(threads_.rwlock_destroy(c.rdi));
    case S::rwlock_rdlock: return kernel_error(threads_.rwlock_lock(c.rdi, thread, false, true));
    case S::rwlock_wrlock: return kernel_error(threads_.rwlock_lock(c.rdi, thread, true, true));
    case S::rwlock_tryrdlock: return kernel_error(threads_.rwlock_lock(c.rdi, thread, false, false));
    case S::rwlock_trywrlock: return kernel_error(threads_.rwlock_lock(c.rdi, thread, true, false));
    case S::rwlock_unlock: return kernel_error(threads_.rwlock_unlock(c.rdi, thread));
    // scePthreadGetprio(thread, int* prio): the default priority, 700.
    case S::thread_getprio: return write(c.rsi, 700, 4) ? 0 : kernel_error(fault);
    // Affinity masks: the 7 cores a game may use.
    case S::thread_getaffinity: return write(c.rsi, 0x7f, 8) ? 0 : kernel_error(fault);
    case S::attr_getaffinity:
        if (const Error e = threads_.attr_accept(c.rdi)) return kernel_error(e);
        return write(c.rsi, 0x7f, 8) ? 0 : kernel_error(fault);
    // pthread_attr_get_np(thread, attr*): the attribute object keeps its defaults.
    case S::attr_get_np: return kernel_error(threads_.attr_accept(c.rsi));
    case S::sigfillset: // sigset_t: 128 bits.
        return write(c.rdi, UINT64_MAX, 8) && write(c.rdi + 8, UINT64_MAX, 8) ? 0 : kernel_error(fault);
    case S::signal: return 0; // SIG_DFL: signals are never delivered.
    // sceKernelConvertUtcToLocaltime(time, time_t* local, Timesec* st, u64* dst):
    // the local time zone is UTC.
    case S::utc_to_local:
        if (c.rsi && !write(c.rsi, c.rdi, 8)) return kernel_error(fault);
        if (c.rdx && (!write(c.rdx, c.rdi, 8) || !write(c.rdx + 8, 0, 8))) return kernel_error(fault);
        if (c.rcx && !write(c.rcx, 0, 8)) return kernel_error(fault);
        return 0;
    // sceKernelConvertLocaltimeToUtc(time, unused, time_t* utc, Timezone* tz, s32* dst).
    case S::local_to_utc:
        if (!c.r8 && !c.rcx) return kernel_error(inval);
        if (c.rcx && !write(c.rcx, 0, 8)) return kernel_error(fault);
        if (c.rdx && !write(c.rdx, c.rdi, 8)) return kernel_error(fault);
        if (c.r8 && !write(c.r8, 0, 4)) return kernel_error(fault);
        return 0;
    case S::proc_param: return int64_t(process_param_);
    case S::module_info_from_addr: return kernel_error(srch);
    case S::succeed: return 0;
    default: handled = false; return 0;
    }
}
bool PS4Libkernel::initialize_data(PS4KernelService service, uint64_t address, uint64_t size) {
    switch (service) {
    case PS4KernelService::stack_chk_guard: {
        std::random_device random;
        const uint64_t guard = (uint64_t(random()) << 32) | random();
        uint8_t wire[8];
        put_le(wire, 0, guard, 8);
        return size >= 8 && memory_.write(address, wire, 8);
    }
    case PS4KernelService::progname: {
        // char* __progname, pointing at the name stored right after it.
        static constexpr char name[] = "eboot.bin";
        uint8_t wire[8];
        put_le(wire, 0, address + 16, 8);
        return size >= 16 + sizeof(name) && memory_.write(address, wire, 8) &&
            memory_.write(address + 16, name, sizeof(name));
    }
    case PS4KernelService::errno_table:
        errno_table_ = address;
        errno_table_size_ = size;
        return true;
    default: return false;
    }
}
int64_t PS4Libkernel::invoke_service(PS4KernelService service, const PS4KernelCall& c, uint64_t thread) {
    // Thread services use only PS4Threads (its own lock): a thread blocked in
    // a lock or a wait must not hold up the thread that would release it.
    switch (service) {
    case PS4KernelService::thread_self: return threads_.known(thread) ? int64_t(thread) : kernel_error(inval);
    case PS4KernelService::mutex_init:
        return threads_.known(thread) ? kernel_error(threads_.init(c.rdi, c.rsi, c.rdx)) : kernel_error(inval);
    case PS4KernelService::mutex_trylock: return kernel_error(threads_.trylock(c.rdi, thread));
    case PS4KernelService::mutex_lock: return kernel_error(threads_.lock(c.rdi, thread));
    case PS4KernelService::mutex_unlock: return kernel_error(threads_.unlock(c.rdi, thread));
    case PS4KernelService::mutex_destroy:
        return threads_.known(thread) ? kernel_error(threads_.destroy(c.rdi)) : kernel_error(inval);
    case PS4KernelService::cond_init:
        return threads_.known(thread) ? kernel_error(threads_.cond_init(c.rdi, c.rsi, c.rdx)) : kernel_error(inval);
    case PS4KernelService::cond_destroy:
        return threads_.known(thread) ? kernel_error(threads_.cond_destroy(c.rdi)) : kernel_error(inval);
    case PS4KernelService::cond_wait: return kernel_error(threads_.cond_wait(c.rdi, c.rsi, thread, false, 0));
    // scePthreadCondTimedwait(cond, mutex, usec): a relative timeout.
    case PS4KernelService::cond_timedwait: return kernel_error(threads_.cond_wait(c.rdi, c.rsi, thread, true, c.rdx));
    case PS4KernelService::cond_signal:
    case PS4KernelService::cond_broadcast:
        return threads_.known(thread) ?
            kernel_error(threads_.cond_signal(c.rdi, service == PS4KernelService::cond_broadcast)) : kernel_error(inval);
    case PS4KernelService::attr_init: return kernel_error(threads_.attr_init(c.rdi));
    case PS4KernelService::attr_destroy: return kernel_error(threads_.attr_destroy(c.rdi));
    case PS4KernelService::attr_set_detach: return kernel_error(threads_.attr_set_detach(c.rdi, c.rsi));
    case PS4KernelService::attr_get_detach: return kernel_error(threads_.attr_get_detach(c.rdi, c.rsi));
    case PS4KernelService::attr_set_stack: return kernel_error(threads_.attr_set_stack(c.rdi, c.rsi));
    case PS4KernelService::attr_get_stack: return kernel_error(threads_.attr_get_stack(c.rdi, c.rsi));
    case PS4KernelService::attr_accept: return kernel_error(threads_.attr_accept(c.rdi));
    case PS4KernelService::mutexattr_init: return kernel_error(threads_.mutexattr_init(c.rdi));
    case PS4KernelService::mutexattr_destroy: return kernel_error(threads_.mutexattr_destroy(c.rdi));
    case PS4KernelService::mutexattr_settype: return kernel_error(threads_.mutexattr_settype(c.rdi, c.rsi));
    case PS4KernelService::mutexattr_gettype: return kernel_error(threads_.mutexattr_gettype(c.rdi, c.rsi));
    case PS4KernelService::mutexattr_accept: return kernel_error(threads_.mutexattr_accept(c.rdi));
    default: break;
    }
    std::lock_guard<std::mutex> guard(lock_);
    try {
        if (!threads_.known(thread)) return kernel_error(inval);
        switch (service) {
        case PS4KernelService::open: return open(c);
        case PS4KernelService::read: return read(c);
        case PS4KernelService::close: return close(signed32(c.rdi));
        case PS4KernelService::lseek: return lseek(c);
        case PS4KernelService::stat: return stat(c, false);
        case PS4KernelService::fstat: return stat(c, true);
        case PS4KernelService::mmap: return mmap(c);
        case PS4KernelService::munmap: return kernel_error(dynamic_ ? dynamic_->unmap(c.rdi, c.rsi) : nosys);
        case PS4KernelService::mprotect: return kernel_error(dynamic_ ? dynamic_->protect(c.rdi, c.rsi, uint32_t(c.rdx)) : nosys);
        case PS4KernelService::clock_gettime: return clock(c, false);
        case PS4KernelService::clock_getres: return clock(c, true);
        case PS4KernelService::gettimeofday: return gettimeofday(c.rdi);
        case PS4KernelService::process_time: {
            const uint64_t now = monotonic_us();
            return now >= started_us_ ? int64_t(now - started_us_) : 0;
        }
        default: return kernel_error(nosys);
        }
    } catch (const std::bad_alloc&) { return kernel_error(nomem); }
}
} // namespace maxps4
extern "C" uint32_t maxps4_ps4_resolve_libkernel(const char* symbol, const char* library,
                                               const char* module, uint32_t version) {
    if (!symbol || !library || !module) return 0;
    return uint32_t(maxps4::resolve_libkernel(symbol, library, module, version));
}
