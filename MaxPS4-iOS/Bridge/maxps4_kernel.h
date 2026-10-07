// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace MaxPS4::Kernel {

enum MemoryProtection : uint32_t {
    MemoryRead = 1u << 0,
    MemoryWrite = 1u << 1,
    MemoryExecute = 1u << 2,
};

struct MemoryRegion {
    uintptr_t base{};
    size_t size{};
    uint32_t protection{};
};

enum class FileDescriptorKind : uint8_t {
    Stdin,
    Stdout,
    Stderr,
    VirtualFile,
};

struct FileDescriptor {
    int fd{-1};
    FileDescriptorKind kind{FileDescriptorKind::VirtualFile};
    bool readable{};
    bool writable{};
};

struct ThreadState {
    uint32_t tid{};
    bool running{};
};

struct ProcessState {
    uint32_t pid{1};
    uint32_t ppid{};
    uint32_t uid{};
    uint32_t gid{};
    std::array<ThreadState, 64> threads{};
    size_t thread_count{};
    std::array<FileDescriptor, 64> descriptors{};
    size_t descriptor_count{};
    std::array<MemoryRegion, 128> memory_regions{};
    size_t memory_region_count{};
};

struct SyscallResult {
    bool handled{};
    bool request_exit{};
    uint64_t value{};
    int error{};
    int exit_code{};
};

class KernelState {
public:
    KernelState();

    void Reset();
    bool RegisterMemoryRegion(uintptr_t base, size_t size, uint32_t protection);
    bool ContainsMemory(uintptr_t address, size_t size, uint32_t required) const;

    bool HasFileDescriptor(int fd, bool require_read, bool require_write) const;

    SyscallResult Dispatch(uint64_t syscall_number,
                           const std::array<uint64_t, 6>& args) const;

    const ProcessState& Process() const { return process_; }

private:
    ProcessState process_{};
};

} // namespace MaxPS4::Kernel
