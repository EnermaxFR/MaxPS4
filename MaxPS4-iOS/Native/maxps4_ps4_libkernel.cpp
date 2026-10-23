#include "maxps4_ps4_libkernel.h"
#include "maxps4_fex_runtime.h"
#include "maxps4_ps4_kernel_path.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <new>
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
    {"__tls_get_addr", "vNe1w4diLCs", PS4KernelService::tls_get_addr}
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
    if (library != "libkernel" || module != "libkernel" || version != 1)
        return PS4KernelService::unresolved;
    for (const auto& binding : bindings)
        if (symbol == binding.name || symbol == binding.nid) return binding.service;
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
int64_t PS4Libkernel::invoke(PS4KernelService service, const PS4KernelCall& c, uint64_t thread) {
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
