// SPDX-License-Identifier: GPL-2.0-or-later
#include "maxps4_kernel.h"

#include <limits>

namespace MaxPS4::Kernel {

KernelState::KernelState() {
    Reset();
}

void KernelState::Reset() {
    process_ = {};
    process_.pid = 1;
    process_.ppid = 0;
    process_.uid = 0;
    process_.gid = 0;

    process_.threads[0] = ThreadState{1, true};
    process_.thread_count = 1;

    process_.descriptors[0] =
        FileDescriptor{0, FileDescriptorKind::Stdin, true, false};
    process_.descriptors[1] =
        FileDescriptor{1, FileDescriptorKind::Stdout, false, true};
    process_.descriptors[2] =
        FileDescriptor{2, FileDescriptorKind::Stderr, false, true};
    process_.descriptor_count = 3;
}

bool KernelState::RegisterMemoryRegion(uintptr_t base, size_t size,
                                       uint32_t protection) {
    if (base == 0 || size == 0 ||
        process_.memory_region_count >= process_.memory_regions.size()) {
        return false;
    }

    if (base > std::numeric_limits<uintptr_t>::max() - size) {
        return false;
    }

    auto& region =
        process_.memory_regions[process_.memory_region_count++];
    region.base = base;
    region.size = size;
    region.protection = protection;
    return true;
}

bool KernelState::ContainsMemory(uintptr_t address, size_t size,
                                 uint32_t required) const {
    if (size == 0) return true;
    if (address == 0 ||
        address > std::numeric_limits<uintptr_t>::max() - size) {
        return false;
    }

    const uintptr_t end = address + size;
    for (size_t i = 0; i < process_.memory_region_count; ++i) {
        const auto& region = process_.memory_regions[i];
        if ((region.protection & required) != required ||
            region.base > std::numeric_limits<uintptr_t>::max() - region.size) {
            continue;
        }
        const uintptr_t region_end = region.base + region.size;
        if (address >= region.base && end <= region_end) {
            return true;
        }
    }
    return false;
}

bool KernelState::HasFileDescriptor(int fd, bool require_read,
                                    bool require_write) const {
    for (size_t i = 0; i < process_.descriptor_count; ++i) {
        const auto& descriptor = process_.descriptors[i];
        if (descriptor.fd != fd) continue;
        if (require_read && !descriptor.readable) return false;
        if (require_write && !descriptor.writable) return false;
        return true;
    }
    return false;
}

SyscallResult KernelState::Dispatch(
    uint64_t syscall_number,
    const std::array<uint64_t, 6>& args) const {
    SyscallResult result{};

    switch (syscall_number) {
    case 1: // exit
        result.handled = true;
        result.request_exit = true;
        result.exit_code = static_cast<int>(args[0]);
        return result;

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

    default:
        return result;
    }
}

} // namespace MaxPS4::Kernel
