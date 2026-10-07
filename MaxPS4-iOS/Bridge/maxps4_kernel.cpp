// SPDX-License-Identifier: GPL-2.0-or-later
#include "maxps4_kernel.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <thread>

namespace MaxPS4::Kernel {

thread_local const KernelState* KernelState::bound_kernel_ = nullptr;
thread_local uint32_t KernelState::bound_tid_ = 0;

namespace {

constexpr size_t kGuestPageSize = 16 * 1024;
constexpr int kAccessMask = 0x3;
constexpr int kOpenCreate = 0x0200; // BSD O_CREAT value used by legal smoke.

size_t AlignPage(size_t value) {
    if (value == 0 || value > std::numeric_limits<size_t>::max() -
                                  (kGuestPageSize - 1)) {
        return 0;
    }
    return (value + kGuestPageSize - 1) & ~(kGuestPageSize - 1);
}

bool SpanValid(uintptr_t base, size_t size) {
    return base != 0 && size != 0 &&
           base <= std::numeric_limits<uintptr_t>::max() - size;
}

bool PathAllowed(std::string_view path) {
    return path.starts_with("/app0/") || path.starts_with("/savedata/");
}

} // namespace

KernelState::KernelState() {
    Reset();
}

void KernelState::Reset() {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    process_ = {};
    managed_arena_base_ = 0;
    managed_arena_size_ = 0;

    process_.pid = 1;
    process_.ppid = 0;
    process_.uid = 0;
    process_.gid = 0;
    process_.current_tid = 1;
    process_.next_tid = 2;
    process_.next_mutex_id = 1;

    process_.threads[0].tid = 1;
    process_.threads[0].active = true;
    process_.threads[0].running = true;
    std::memcpy(process_.threads[0].name.data(), "main", 5);
    process_.thread_count = 1;

    for (auto& descriptor : process_.descriptors) {
        descriptor.fd = -1;
    }

    process_.descriptors[0] =
        FileDescriptor{0, FileDescriptorKind::Stdin, true, false, -1, 0};
    process_.descriptors[1] =
        FileDescriptor{1, FileDescriptorKind::Stdout, false, true, -1, 0};
    process_.descriptors[2] =
        FileDescriptor{2, FileDescriptorKind::Stderr, false, true, -1, 0};
    process_.descriptor_count = 3;

    SeedVirtualFile("/app0/kernel.txt", "MaxPS4Kernel\n", false);
    SeedVirtualFile("/savedata/state.bin", "", true);
}

bool KernelState::RegisterMemoryRegion(uintptr_t base, size_t size,
                                       uint32_t protection) {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    if (!SpanValid(base, size) ||
        process_.memory_region_count >= process_.memory_regions.size()) {
        return false;
    }

    auto& region = process_.memory_regions[process_.memory_region_count++];
    region.base = base;
    region.size = size;
    region.protection = protection;
    region.active = true;
    region.managed = false;
    return true;
}

bool KernelState::ConfigureManagedArena(uintptr_t base, size_t size) {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    if (!SpanValid(base, size)) return false;
    managed_arena_base_ = base;
    managed_arena_size_ = size;
    return true;
}

bool KernelState::ContainsMemory(uintptr_t address, size_t size,
                                 uint32_t required) const {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    if (size == 0) return true;
    if (!SpanValid(address, size)) return false;

    const uintptr_t end = address + size;
    for (size_t i = 0; i < process_.memory_region_count; ++i) {
        const auto& region = process_.memory_regions[i];
        if (!region.active ||
            (region.protection & required) != required ||
            !SpanValid(region.base, region.size)) {
            continue;
        }
        const uintptr_t region_end = region.base + region.size;
        if (address >= region.base && end <= region_end) return true;
    }
    return false;
}

uintptr_t KernelState::AllocateVirtualMemory(size_t size, uint32_t protection,
                                             int& error) {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    error = 0;
    const size_t aligned = AlignPage(size);
    if (aligned == 0 || managed_arena_base_ == 0 || managed_arena_size_ == 0) {
        error = EINVAL;
        return 0;
    }
    if ((protection & ~(MemoryRead | MemoryWrite | MemoryExecute)) != 0) {
        error = EINVAL;
        return 0;
    }
    if (process_.memory_region_count >= process_.memory_regions.size()) {
        error = ENOMEM;
        return 0;
    }

    uintptr_t candidate = managed_arena_base_;
    const uintptr_t arena_end = managed_arena_base_ + managed_arena_size_;
    while (candidate <= arena_end && aligned <= arena_end - candidate) {
        bool collision = false;
        uintptr_t next_candidate = candidate + kGuestPageSize;
        for (size_t i = 0; i < process_.memory_region_count; ++i) {
            const auto& region = process_.memory_regions[i];
            if (!region.active || !region.managed) continue;
            const uintptr_t region_end = region.base + region.size;
            const uintptr_t candidate_end = candidate + aligned;
            if (candidate < region_end && candidate_end > region.base) {
                collision = true;
                next_candidate = std::max(next_candidate, region_end);
            }
        }
        if (!collision) {
            auto& region = process_.memory_regions[process_.memory_region_count++];
            region.base = candidate;
            region.size = aligned;
            region.protection = protection;
            region.active = true;
            region.managed = true;
            return candidate;
        }
        candidate = (next_candidate + kGuestPageSize - 1) &
                    ~(static_cast<uintptr_t>(kGuestPageSize - 1));
    }

    error = ENOMEM;
    return 0;
}

MemoryRegion* KernelState::FindManagedAllocation(uintptr_t address, size_t size) {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    if (!SpanValid(address, size)) return nullptr;
    const uintptr_t end = address + size;
    for (size_t i = 0; i < process_.memory_region_count; ++i) {
        auto& region = process_.memory_regions[i];
        if (!region.active || !region.managed) continue;
        if (address >= region.base && end <= region.base + region.size) {
            return &region;
        }
    }
    return nullptr;
}

bool KernelState::ProtectVirtualMemory(uintptr_t address, size_t size,
                                       uint32_t protection, int& error) {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    error = 0;
    if ((protection & ~(MemoryRead | MemoryWrite | MemoryExecute)) != 0) {
        error = EINVAL;
        return false;
    }
    auto* region = FindManagedAllocation(address, size);
    if (!region) {
        error = EINVAL;
        return false;
    }
    if (address != region->base || AlignPage(size) != region->size) {
        error = EINVAL;
        return false;
    }
    region->protection = protection;
    return true;
}

bool KernelState::UnmapVirtualMemory(uintptr_t address, size_t size, int& error) {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    error = 0;
    auto* region = FindManagedAllocation(address, size);
    if (!region || address != region->base || AlignPage(size) != region->size) {
        error = EINVAL;
        return false;
    }
    region->active = false;
    return true;
}

int KernelState::FindVirtualFile(std::string_view path) const {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    for (size_t i = 0; i < process_.virtual_files.size(); ++i) {
        const auto& node = process_.virtual_files[i];
        if (!node.active) continue;
        if (std::string_view(node.path.data()) == path) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool KernelState::SeedVirtualFile(std::string_view path,
                                  std::string_view contents,
                                  bool writable) {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    if (!PathAllowed(path) || path.empty() || path.size() >= 128 ||
        contents.size() > kMaxVirtualFileBytes) {
        return false;
    }

    int slot = FindVirtualFile(path);
    if (slot < 0) {
        for (size_t i = 0; i < process_.virtual_files.size(); ++i) {
            if (!process_.virtual_files[i].active) {
                slot = static_cast<int>(i);
                break;
            }
        }
    }
    if (slot < 0) return false;

    auto& node = process_.virtual_files[static_cast<size_t>(slot)];
    const bool was_active = node.active;
    node = {};
    node.active = true;
    node.writable = writable;
    std::memcpy(node.path.data(), path.data(), path.size());
    node.path[path.size()] = '\0';
    if (!contents.empty()) {
        std::memcpy(node.data.data(), contents.data(), contents.size());
    }
    node.size = contents.size();
    if (!was_active) ++process_.virtual_file_count;
    return true;
}

int KernelState::AllocateDescriptorSlot() {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    for (size_t i = 3; i < process_.descriptors.size(); ++i) {
        if (process_.descriptors[i].fd < 0) return static_cast<int>(i);
    }
    return -1;
}

int KernelState::OpenVirtualFile(std::string_view path, int flags, int& error) {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    error = 0;
    if (!PathAllowed(path) || path.empty() || path.size() >= 128) {
        error = EACCES;
        return -1;
    }

    int node_index = FindVirtualFile(path);
    if (node_index < 0 && (flags & kOpenCreate) != 0 &&
        path.starts_with("/savedata/")) {
        if (!SeedVirtualFile(path, "", true)) {
            error = ENOSPC;
            return -1;
        }
        node_index = FindVirtualFile(path);
    }
    if (node_index < 0) {
        error = ENOENT;
        return -1;
    }

    const int access = flags & kAccessMask;
    const bool readable = access == 0 || access == 2;
    const bool writable = access == 1 || access == 2;
    auto& node = process_.virtual_files[static_cast<size_t>(node_index)];
    if (writable && !node.writable) {
        error = EACCES;
        return -1;
    }

    const int slot = AllocateDescriptorSlot();
    if (slot < 0) {
        error = EMFILE;
        return -1;
    }
    process_.descriptors[static_cast<size_t>(slot)] =
        FileDescriptor{slot, FileDescriptorKind::VirtualFile,
                       readable, writable, node_index, 0};
    process_.descriptor_count =
        std::max(process_.descriptor_count, static_cast<size_t>(slot + 1));
    return slot;
}

bool KernelState::CloseFile(int fd, int& error) {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    error = 0;
    if (fd < 3 || static_cast<size_t>(fd) >= process_.descriptors.size() ||
        process_.descriptors[static_cast<size_t>(fd)].fd != fd) {
        error = EBADF;
        return false;
    }
    process_.descriptors[static_cast<size_t>(fd)] = {};
    process_.descriptors[static_cast<size_t>(fd)].fd = -1;
    return true;
}

bool KernelState::ReadFile(int fd, void* destination, size_t length,
                           size_t& transferred, int& error) {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    transferred = 0;
    error = 0;
    if (!destination && length != 0) {
        error = EFAULT;
        return false;
    }
    if (fd < 0 || static_cast<size_t>(fd) >= process_.descriptors.size()) {
        error = EBADF;
        return false;
    }
    auto& descriptor = process_.descriptors[static_cast<size_t>(fd)];
    if (descriptor.fd != fd || !descriptor.readable ||
        descriptor.kind != FileDescriptorKind::VirtualFile ||
        descriptor.node_index < 0) {
        error = EBADF;
        return false;
    }
    auto& node = process_.virtual_files[static_cast<size_t>(descriptor.node_index)];
    if (!node.active) {
        error = ENOENT;
        return false;
    }

    const size_t available =
        descriptor.offset < node.size ? node.size - descriptor.offset : 0;
    transferred = std::min(length, available);
    if (transferred != 0) {
        std::memcpy(destination, node.data.data() + descriptor.offset, transferred);
        descriptor.offset += transferred;
    }
    return true;
}

bool KernelState::WriteFile(int fd, const void* source, size_t length,
                            size_t& transferred, int& error) {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    transferred = 0;
    error = 0;
    if (!source && length != 0) {
        error = EFAULT;
        return false;
    }
    if (fd < 0 || static_cast<size_t>(fd) >= process_.descriptors.size()) {
        error = EBADF;
        return false;
    }
    auto& descriptor = process_.descriptors[static_cast<size_t>(fd)];
    if (descriptor.fd != fd || !descriptor.writable ||
        descriptor.kind != FileDescriptorKind::VirtualFile ||
        descriptor.node_index < 0) {
        error = EBADF;
        return false;
    }
    auto& node = process_.virtual_files[static_cast<size_t>(descriptor.node_index)];
    if (!node.active || !node.writable) {
        error = EACCES;
        return false;
    }
    if (descriptor.offset >= node.data.size()) {
        error = ENOSPC;
        return false;
    }

    transferred = std::min(length, node.data.size() - descriptor.offset);
    if (transferred != 0) {
        std::memcpy(node.data.data() + descriptor.offset, source, transferred);
        descriptor.offset += transferred;
        node.size = std::max(node.size, descriptor.offset);
    }
    if (transferred != length) {
        error = ENOSPC;
        return false;
    }
    return true;
}

bool KernelState::HasFileDescriptor(int fd, bool require_read,
                                    bool require_write) const {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    if (fd < 0 || static_cast<size_t>(fd) >= process_.descriptors.size()) {
        return false;
    }
    const auto& descriptor = process_.descriptors[static_cast<size_t>(fd)];
    if (descriptor.fd != fd) return false;
    if (require_read && !descriptor.readable) return false;
    if (require_write && !descriptor.writable) return false;
    return true;
}

uint32_t KernelState::CreateLogicalThread(std::string_view name, int& error) {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    error = 0;
    if (name.size() >= 32) {
        error = EINVAL;
        return 0;
    }
    for (auto& thread : process_.threads) {
        if (thread.active) continue;
        thread = {};
        thread.tid = process_.next_tid++;
        thread.active = true;
        thread.running = true;
        if (!name.empty()) {
            std::memcpy(thread.name.data(), name.data(), name.size());
            thread.name[name.size()] = '\0';
        }
        ++process_.thread_count;
        return thread.tid;
    }
    error = EAGAIN;
    return 0;
}

bool KernelState::CompleteLogicalThread(uint32_t tid, int exit_code, int& error) {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    error = 0;
    if (tid == process_.pid) {
        error = EBUSY;
        return false;
    }
    for (auto& thread : process_.threads) {
        if (!thread.active || thread.tid != tid) continue;
        thread.running = false;
        thread.exited = true;
        thread.exit_code = exit_code;
        return true;
    }
    error = ESRCH;
    return false;
}

bool KernelState::JoinLogicalThread(uint32_t tid, int& exit_code, int& error) {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    error = 0;
    for (auto& thread : process_.threads) {
        if (!thread.active || thread.tid != tid) continue;
        if (!thread.exited) {
            error = EBUSY;
            return false;
        }
        exit_code = thread.exit_code;
        thread = {};
        if (process_.thread_count != 0) --process_.thread_count;
        return true;
    }
    error = ESRCH;
    return false;
}

bool KernelState::BindCurrentThread(uint32_t tid, int& error) {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    error = 0;
    for (const auto& thread : process_.threads) {
        if (!thread.active || thread.tid != tid) continue;
        bound_kernel_ = this;
        bound_tid_ = tid;
        return true;
    }
    error = ESRCH;
    return false;
}

void KernelState::UnbindCurrentThread() {
    if (bound_kernel_ == this) {
        bound_kernel_ = nullptr;
        bound_tid_ = 0;
    }
}

uint32_t KernelState::CurrentTid() const {
    if (bound_kernel_ == this && bound_tid_ != 0) {
        return bound_tid_;
    }
    return process_.current_tid;
}

uint32_t KernelState::CreateMutex(int& error) {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    error = 0;
    for (auto& mutex : process_.mutexes) {
        if (mutex.active) continue;
        mutex = {};
        mutex.id = process_.next_mutex_id++;
        mutex.active = true;
        return mutex.id;
    }
    error = ENOSPC;
    return 0;
}

bool KernelState::LockMutex(uint32_t id, uint32_t tid, int& error) {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    error = 0;
    for (auto& mutex : process_.mutexes) {
        if (!mutex.active || mutex.id != id) continue;
        if (mutex.owner_tid == 0 || mutex.owner_tid == tid) {
            mutex.owner_tid = tid;
            ++mutex.recursion;
            return true;
        }
        error = EBUSY;
        return false;
    }
    error = EINVAL;
    return false;
}

bool KernelState::UnlockMutex(uint32_t id, uint32_t tid, int& error) {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    error = 0;
    for (auto& mutex : process_.mutexes) {
        if (!mutex.active || mutex.id != id) continue;
        if (mutex.owner_tid != tid || mutex.recursion == 0) {
            error = EPERM;
            return false;
        }
        if (--mutex.recursion == 0) mutex.owner_tid = 0;
        return true;
    }
    error = EINVAL;
    return false;
}

bool KernelState::DestroyMutex(uint32_t id, int& error) {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    error = 0;
    for (auto& mutex : process_.mutexes) {
        if (!mutex.active || mutex.id != id) continue;
        if (mutex.owner_tid != 0) {
            error = EBUSY;
            return false;
        }
        mutex = {};
        return true;
    }
    error = EINVAL;
    return false;
}

KernelTimeValue KernelState::GetTimeOfDay() const {
    using namespace std::chrono;
    const auto micros =
        duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();

    KernelTimeValue value{};
    value.seconds = micros / 1000000;
    value.fraction = micros % 1000000;
    return value;
}

bool KernelState::ClockGetTime(int clock_id, KernelTimeValue& out,
                               int& error) const {
    using namespace std::chrono;
    error = 0;

    const bool realtime =
        clock_id == 0 || clock_id == 9 || clock_id == 10 || clock_id == 13;
    const bool monotonic =
        clock_id == 4 || clock_id == 5 || clock_id == 7 || clock_id == 8 ||
        clock_id == 11 || clock_id == 12;

    int64_t nanos = 0;
    if (realtime) {
        nanos =
            duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
    } else if (monotonic) {
        nanos =
            duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    } else {
        error = EINVAL;
        return false;
    }

    out.seconds = nanos / 1000000000LL;
    out.fraction = nanos % 1000000000LL;
    return true;
}

bool KernelState::ClockGetResolution(int clock_id, KernelTimeValue& out,
                                     int& error) const {
    error = 0;
    switch (clock_id) {
    case 0:
    case 4:
    case 5:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
        out.seconds = 0;
        out.fraction = 1;
        return true;
    case 13: // CLOCK_SECOND
        out.seconds = 1;
        out.fraction = 0;
        return true;
    default:
        error = EINVAL;
        return false;
    }
}

bool KernelState::SleepFor(int64_t seconds, int64_t nanoseconds,
                           int& error) const {
    error = 0;
    if (seconds < 0 || nanoseconds < 0 || nanoseconds >= 1000000000LL) {
        error = EINVAL;
        return false;
    }

    using namespace std::chrono;
    const auto duration = std::chrono::seconds(seconds) +
                          std::chrono::nanoseconds(nanoseconds);
    std::this_thread::sleep_for(duration);
    return true;
}

void KernelState::YieldCurrentThread() const {
    std::this_thread::yield();
}

bool KernelState::LegacyUmtxLock(uintptr_t address, int& error) {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    error = 0;
    if (!ContainsMemory(address, sizeof(uint64_t), MemoryRead | MemoryWrite)) {
        error = EFAULT;
        return false;
    }

    uint64_t owner = 0;
    std::memcpy(&owner, reinterpret_cast<const void*>(address), sizeof(owner));
    const uint64_t tid = static_cast<uint64_t>(CurrentTid());

    if (owner == 0) {
        std::memcpy(reinterpret_cast<void*>(address), &tid, sizeof(tid));
        return true;
    }
    if ((owner & ~(1ULL << 63)) == tid) {
        error = EDEADLK;
        return false;
    }

    error = EBUSY;
    return false;
}

bool KernelState::LegacyUmtxUnlock(uintptr_t address, int& error) {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    error = 0;
    if (!ContainsMemory(address, sizeof(uint64_t), MemoryRead | MemoryWrite)) {
        error = EFAULT;
        return false;
    }

    uint64_t owner = 0;
    std::memcpy(&owner, reinterpret_cast<const void*>(address), sizeof(owner));
    const uint64_t tid = static_cast<uint64_t>(CurrentTid());
    if ((owner & ~(1ULL << 63)) != tid) {
        error = EPERM;
        return false;
    }

    const uint64_t unowned = 0;
    std::memcpy(reinterpret_cast<void*>(address), &unowned, sizeof(unowned));
    return true;
}

bool KernelState::UmtxOperation(uintptr_t object, int operation, uint64_t value,
                                uintptr_t uaddr, uintptr_t uaddr2,
                                uint64_t& result_value, int& error) {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    result_value = 0;
    error = 0;

    constexpr int kUmtxOpLock = 0;
    constexpr int kUmtxOpUnlock = 1;
    constexpr int kUmtxOpWait = 2;
    constexpr int kUmtxOpWake = 3;
    constexpr int kUmtxOpMutexTryLock = 4;
    constexpr int kUmtxOpMutexLock = 5;
    constexpr int kUmtxOpMutexUnlock = 6;
    constexpr int kUmtxOpWaitUInt = 11;
    constexpr int kUmtxOpWaitUIntPrivate = 15;
    constexpr int kUmtxOpWakePrivate = 16;

    switch (operation) {
    case kUmtxOpLock:
        return LegacyUmtxLock(object, error);

    case kUmtxOpUnlock:
        return LegacyUmtxUnlock(object, error);

    case kUmtxOpWake:
    case kUmtxOpWakePrivate:
        // There is no parallel guest waiter yet. Treat wake as a successful
        // no-op so userland can complete uncontended synchronization paths.
        return true;

    case kUmtxOpWait: {
        if (!ContainsMemory(object, sizeof(uint64_t), MemoryRead)) {
            error = EFAULT;
            return false;
        }
        uint64_t current = 0;
        std::memcpy(&current, reinterpret_cast<const void*>(object), sizeof(current));
        if (current != value) {
            error = EWOULDBLOCK;
            return false;
        }

        // Until parallel FEX guest scheduling exists, never sleep forever.
        // A supplied timeout is honored; otherwise report would-block.
        if (uaddr2 == 0) {
            error = EWOULDBLOCK;
            return false;
        }
        if (!ContainsMemory(uaddr2, sizeof(int64_t) * 2, MemoryRead)) {
            error = EFAULT;
            return false;
        }
        int64_t timeout[2]{};
        std::memcpy(timeout, reinterpret_cast<const void*>(uaddr2), sizeof(timeout));
        if (!SleepFor(timeout[0], timeout[1], error)) return false;
        error = ETIMEDOUT;
        return false;
    }

    case kUmtxOpWaitUInt:
    case kUmtxOpWaitUIntPrivate: {
        if (!ContainsMemory(object, sizeof(uint32_t), MemoryRead)) {
            error = EFAULT;
            return false;
        }
        uint32_t current = 0;
        std::memcpy(&current, reinterpret_cast<const void*>(object), sizeof(current));
        if (current != static_cast<uint32_t>(value)) {
            error = EWOULDBLOCK;
            return false;
        }
        if (uaddr2 == 0) {
            error = EWOULDBLOCK;
            return false;
        }
        if (!ContainsMemory(uaddr2, sizeof(int64_t) * 2, MemoryRead)) {
            error = EFAULT;
            return false;
        }
        int64_t timeout[2]{};
        std::memcpy(timeout, reinterpret_cast<const void*>(uaddr2), sizeof(timeout));
        if (!SleepFor(timeout[0], timeout[1], error)) return false;
        error = ETIMEDOUT;
        return false;
    }

    case kUmtxOpMutexTryLock:
    case kUmtxOpMutexLock: {
        // struct umutex starts with a 32-bit owner field followed by flags.
        if (!ContainsMemory(object, sizeof(uint32_t) * 2,
                            MemoryRead | MemoryWrite)) {
            error = EFAULT;
            return false;
        }

        uint32_t owner = 0;
        uint32_t flags = 0;
        std::memcpy(&owner, reinterpret_cast<const void*>(object), sizeof(owner));
        std::memcpy(&flags,
                    reinterpret_cast<const void*>(object + sizeof(uint32_t)),
                    sizeof(flags));

        const uint32_t tid = CurrentTid();
        const uint32_t plain_owner = owner & ~0x80000000U;
        if (owner == 0 || owner == 0x80000000U) {
            std::memcpy(reinterpret_cast<void*>(object), &tid, sizeof(tid));
            return true;
        }
        if (plain_owner == tid) {
            error = (flags & 0x0002U) ? EDEADLK : EBUSY;
            return false;
        }

        error = EBUSY;
        return false;
    }

    case kUmtxOpMutexUnlock: {
        if (!ContainsMemory(object, sizeof(uint32_t),
                            MemoryRead | MemoryWrite)) {
            error = EFAULT;
            return false;
        }

        uint32_t owner = 0;
        std::memcpy(&owner, reinterpret_cast<const void*>(object), sizeof(owner));
        if ((owner & ~0x80000000U) != CurrentTid()) {
            error = EPERM;
            return false;
        }

        const uint32_t unowned = 0;
        std::memcpy(reinterpret_cast<void*>(object), &unowned, sizeof(unowned));
        return true;
    }

    default:
        (void)uaddr;
        error = ENOSYS;
        return false;
    }
}

SyscallResult KernelState::Dispatch(
    uint64_t syscall_number,
    const std::array<uint64_t, 6>& args) {
    std::lock_guard<std::recursive_mutex> state_lock(state_mutex_);
    SyscallResult result{};

    switch (syscall_number) {
    case 1: // exit
        result.handled = true;
        result.request_exit = true;
        result.exit_code = static_cast<int>(args[0]);
        return result;

    case 6: { // close
        result.handled = true;
        int error = 0;
        if (!CloseFile(static_cast<int>(args[0]), error)) {
            result.error = error;
        }
        return result;
    }

    case 20: // getpid
        result.handled = true;
        result.value = process_.pid;
        return result;

    case 39: // getppid
        result.handled = true;
        result.value = process_.ppid;
        return result;

    case 24: // getuid
    case 25: // geteuid
        result.handled = true;
        result.value = process_.uid;
        return result;

    case 43: // getegid
    case 47: // getgid
        result.handled = true;
        result.value = process_.gid;
        return result;

    case 73: { // munmap
        result.handled = true;
        int error = 0;
        if (!UnmapVirtualMemory(static_cast<uintptr_t>(args[0]),
                                static_cast<size_t>(args[1]), error)) {
            result.error = error;
        }
        return result;
    }

    case 74: { // mprotect
        result.handled = true;
        int error = 0;
        if (!ProtectVirtualMemory(static_cast<uintptr_t>(args[0]),
                                 static_cast<size_t>(args[1]),
                                 static_cast<uint32_t>(args[2]), error)) {
            result.error = error;
        }
        return result;
    }

    case 331: // sched_yield
        result.handled = true;
        YieldCurrentThread();
        return result;

    case 434: { // legacy _umtx_lock
        result.handled = true;
        int error = 0;
        if (!LegacyUmtxLock(static_cast<uintptr_t>(args[0]), error)) {
            result.error = error;
        }
        return result;
    }

    case 435: { // legacy _umtx_unlock
        result.handled = true;
        int error = 0;
        if (!LegacyUmtxUnlock(static_cast<uintptr_t>(args[0]), error)) {
            result.error = error;
        }
        return result;
    }

    case 454: { // _umtx_op
        result.handled = true;
        int error = 0;
        uint64_t value = 0;
        if (!UmtxOperation(static_cast<uintptr_t>(args[0]),
                           static_cast<int>(args[1]),
                           args[2],
                           static_cast<uintptr_t>(args[3]),
                           static_cast<uintptr_t>(args[4]),
                           value, error)) {
            result.error = error;
        } else {
            result.value = value;
        }
        return result;
    }

    case 477: { // FreeBSD mmap
        result.handled = true;
        if (args[0] != 0 || static_cast<int64_t>(args[4]) != -1 ||
            args[5] != 0) {
            result.error = EINVAL;
            return result;
        }
        int error = 0;
        const uintptr_t address =
            AllocateVirtualMemory(static_cast<size_t>(args[1]),
                                  static_cast<uint32_t>(args[2]), error);
        if (address == 0) {
            result.error = error ? error : ENOMEM;
        } else {
            result.value = address;
        }
        return result;
    }

    default:
        return result;
    }
}

} // namespace MaxPS4::Kernel
