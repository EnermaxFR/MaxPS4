#include "maxps4_ps4_libkernel.h"
#include "maxps4_ps4_kernel_path.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <sys/stat.h>
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
struct Binding { const char* name; const char* nid; PS4KernelService service; };
// ABI metadata checked against shadPS4 kernel/file_system.{h,cpp}; original
// implementation below. Only sceKernel variants, not POSIX errno/TLS variants.
constexpr Binding bindings[] = {
    {"sceKernelOpen", "1G3lF1Gg1k8", PS4KernelService::open},
    {"sceKernelRead", "Cg4srZ6TKbU", PS4KernelService::read},
    {"sceKernelClose", "UK2Tl2DWUns", PS4KernelService::close},
    {"sceKernelLseek", "oib76F-12fk", PS4KernelService::lseek},
    {"sceKernelStat", "eV9wAD2riIA", PS4KernelService::stat},
    {"sceKernelFstat", "kBwCPsYX-m4", PS4KernelService::fstat}
};
class HostFD {
public:
    explicit HostFD(int fd) : fd(fd) {}
    ~HostFD() { if (fd >= 0) ::close(fd); }
    HostFD(const HostFD&) = delete;
    HostFD& operator=(const HostFD&) = delete;
    int fd;
};
}
PS4KernelService resolve_libkernel(std::string_view symbol, std::string_view library,
                                   std::string_view module, uint32_t version) {
    if (library != "libkernel" || module != "libkernel" || version != 1)
        return PS4KernelService::unresolved;
    for (const auto& binding : bindings)
        if (symbol == binding.name || symbol == binding.nid) return binding.service;
    return PS4KernelService::unresolved;
}
PS4Libkernel::PS4Libkernel(PS4GuestMemory& memory) : memory_(memory) { files_.fill(-1); }
PS4Libkernel::~PS4Libkernel() { reset(); }
void PS4Libkernel::close_files() {
    for (int& fd : files_) { if (fd >= 0) ::close(fd); fd = -1; }
}
void PS4Libkernel::reset() {
    std::lock_guard<std::mutex> guard(lock_);
    close_files();
    if (root_ >= 0) ::close(root_);
    root_ = -1;
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
int64_t PS4Libkernel::invoke(PS4KernelService service, const PS4KernelCall& c) {
    std::lock_guard<std::mutex> guard(lock_);
    try {
        switch (service) {
        case PS4KernelService::open: return open(c);
        case PS4KernelService::read: return read(c);
        case PS4KernelService::close: return close(signed32(c.rdi));
        case PS4KernelService::lseek: return lseek(c);
        case PS4KernelService::stat: return stat(c, false);
        case PS4KernelService::fstat: return stat(c, true);
        default: return kernel_error(nosys);
        }
    } catch (const std::bad_alloc&) { return kernel_error(nomem); }
}
} // namespace maxps4
