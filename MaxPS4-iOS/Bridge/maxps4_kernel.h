// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace MaxPS4::Kernel {

constexpr size_t kMaxThreads = 64;
constexpr size_t kMaxDescriptors = 64;
constexpr size_t kMaxMemoryRegions = 128;
constexpr size_t kMaxVirtualFiles = 16;
constexpr size_t kMaxVirtualFileBytes = 4096;
constexpr size_t kMaxKernelMutexes = 64;

enum MemoryProtection : uint32_t {
    MemoryRead = 1u << 0,
    MemoryWrite = 1u << 1,
    MemoryExecute = 1u << 2,
};

struct MemoryRegion {
    uintptr_t base{};
    size_t size{};
    uint32_t protection{};
    bool active{};
    bool managed{};
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
    int node_index{-1};
    size_t offset{};
};

struct VirtualFileNode {
    bool active{};
    bool writable{};
    std::array<char, 128> path{};
    std::array<uint8_t, kMaxVirtualFileBytes> data{};
    size_t size{};
};

struct ThreadState {
    uint32_t tid{};
    bool active{};
    bool running{};
    bool exited{};
    int exit_code{};
    std::array<char, 32> name{};
};

struct KernelMutexState {
    uint32_t id{};
    bool active{};
    uint32_t owner_tid{};
    uint32_t recursion{};
};

struct ProcessState {
    uint32_t pid{1};
    uint32_t ppid{};
    uint32_t uid{};
    uint32_t gid{};
    uint32_t current_tid{1};
    uint32_t next_tid{2};
    uint32_t next_mutex_id{1};

    std::array<ThreadState, kMaxThreads> threads{};
    size_t thread_count{};

    std::array<FileDescriptor, kMaxDescriptors> descriptors{};
    size_t descriptor_count{};

    std::array<MemoryRegion, kMaxMemoryRegions> memory_regions{};
    size_t memory_region_count{};

    std::array<VirtualFileNode, kMaxVirtualFiles> virtual_files{};
    size_t virtual_file_count{};

    std::array<KernelMutexState, kMaxKernelMutexes> mutexes{};
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

    // External mappings (ELF image, stack, HLE storage) and a pre-mapped
    // managed arena used for guest mmap/munmap bookkeeping.
    bool RegisterMemoryRegion(uintptr_t base, size_t size, uint32_t protection);
    bool ConfigureManagedArena(uintptr_t base, size_t size);
    bool ContainsMemory(uintptr_t address, size_t size, uint32_t required) const;
    uintptr_t AllocateVirtualMemory(size_t size, uint32_t protection, int& error);
    bool ProtectVirtualMemory(uintptr_t address, size_t size,
                              uint32_t protection, int& error);
    bool UnmapVirtualMemory(uintptr_t address, size_t size, int& error);

    // Virtual filesystem is intentionally sandboxed inside MaxPS4Kernel. It
    // never opens host/iOS paths.
    bool SeedVirtualFile(std::string_view path, std::string_view contents,
                         bool writable);
    int OpenVirtualFile(std::string_view path, int flags, int& error);
    bool CloseFile(int fd, int& error);
    bool ReadFile(int fd, void* destination, size_t length,
                  size_t& transferred, int& error);
    bool WriteFile(int fd, const void* source, size_t length,
                   size_t& transferred, int& error);
    bool HasFileDescriptor(int fd, bool require_read, bool require_write) const;

    // Logical guest thread/synchronization state. This establishes kernel
    // object semantics now; real parallel FEX guest scheduling is a later
    // execution-engine stage.
    uint32_t CreateLogicalThread(std::string_view name, int& error);
    bool CompleteLogicalThread(uint32_t tid, int exit_code, int& error);
    bool JoinLogicalThread(uint32_t tid, int& exit_code, int& error);
    uint32_t CurrentTid() const { return process_.current_tid; }

    uint32_t CreateMutex(int& error);
    bool LockMutex(uint32_t id, uint32_t tid, int& error);
    bool UnlockMutex(uint32_t id, uint32_t tid, int& error);
    bool DestroyMutex(uint32_t id, int& error);

    SyscallResult Dispatch(uint64_t syscall_number,
                           const std::array<uint64_t, 6>& args);

    const ProcessState& Process() const { return process_; }

private:
    int FindVirtualFile(std::string_view path) const;
    int AllocateDescriptorSlot();
    MemoryRegion* FindManagedAllocation(uintptr_t address, size_t size);

    ProcessState process_{};
    uintptr_t managed_arena_base_{};
    size_t managed_arena_size_{};
};

} // namespace MaxPS4::Kernel
