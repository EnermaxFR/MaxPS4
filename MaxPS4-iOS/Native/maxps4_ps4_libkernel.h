#pragma once
#include "maxps4_ps4_abi.h"
#include "maxps4_ps4_guest_memory.h"
#include "maxps4_ps4_threads.h"
#include <array>
#include <mutex>
#include <string_view>

namespace maxps4 {
// Entry snapshot for PS4 x86-64 System V calls. Host function addresses must
// never be installed directly into guest GOT/PLT slots on ARM64.
struct PS4KernelCall { uint64_t rdi = 0, rsi = 0, rdx = 0, rcx = 0, r8 = 0, r9 = 0, rsp = 0; };
enum class PS4KernelService : uint32_t {
    unresolved, open, read, close, lseek, stat, fstat,
    mmap, munmap, mprotect, clock_gettime, clock_getres, gettimeofday, process_time,
    thread_self, mutex_init, mutex_trylock, mutex_unlock, mutex_destroy,
    // Needs the image's TLS layout: served by the FEX Orbis HLE gate, not invoke().
    tls_get_addr,
    // Engine-internal, no NID: the exit function a general image's entry gets
    // in RSI. Served by the FEX Orbis HLE gate, which ends the run.
    process_exit
};
// Implemented libkernel functions (name, NID, service), e.g. for an HLE registry.
struct PS4KernelExport { const char* name; const char* nid; PS4KernelService service; };
const PS4KernelExport* libkernel_exports(size_t& count);
// Library/module/version are decoded by an ELF/PRX loader before resolution.
// This resolver deliberately does not guess the meaning of encoded # IDs.
PS4KernelService resolve_libkernel(std::string_view symbol,
    std::string_view library, std::string_view module, uint32_t version);

class PS4Libkernel final {
public:
    static constexpr size_t descriptor_limit = 256;
    static constexpr size_t transfer_limit = 1024 * 1024;
    static constexpr size_t stat_size = 120;
    explicit PS4Libkernel(PS4GuestMemory& memory, PS4VirtualMemory* dynamic = nullptr);
    ~PS4Libkernel();
    PS4Libkernel(const PS4Libkernel&) = delete;
    PS4Libkernel& operator=(const PS4Libkernel&) = delete;
    // Host-side mount configuration, never a guest pathname. A failed mount
    // preserves the previous mount. Successful remount closes guest descriptors.
    int64_t mount_app0(const char* host_directory);
    void reset();
    int64_t invoke(PS4KernelService service, const PS4KernelCall& call, uint64_t thread = 1);
    PS4Threads& threads() { return threads_; }
private:
    PS4GuestMemory& memory_;
    PS4VirtualMemory* dynamic_;
    PS4Threads threads_;
    std::mutex lock_;
    int root_ = -1;
    std::array<int, descriptor_limit> files_;
    void close_files();
    ps4::Error pathname(uint64_t address, char* path);
    int open_path(const char* path, bool directory, ps4::Error& error) const;
    int64_t open(const PS4KernelCall& call);
    int64_t read(const PS4KernelCall& call);
    int64_t close(int32_t descriptor);
    int64_t lseek(const PS4KernelCall& call);
    int64_t stat(const PS4KernelCall& call, bool by_descriptor);
    int64_t mmap(const PS4KernelCall& call);
    int64_t clock(const PS4KernelCall& call, bool resolution);
    int64_t gettimeofday(uint64_t output);
    uint64_t started_us_ = 0;
    bool writable(uint64_t address, size_t size) const;
};
} // namespace maxps4
