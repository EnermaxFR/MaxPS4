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
    process_exit,
    // Guest threads run on FEX: served by its Orbis HLE gate for general
    // images; invoke() answers ENOSYS. thread_exit is also the service of the
    // engine's thread-return trampoline.
    thread_create, thread_join, thread_exit,
    // Blocking synchronization (PS4Threads), served by invoke() without the
    // libkernel lock so that a blocked thread never holds up the others.
    mutex_lock, cond_init, cond_destroy, cond_wait, cond_timedwait, cond_signal, cond_broadcast,
    // scePthreadDetach: a guest thread service, like thread_create.
    thread_detach,
    // Thread and mutex attribute objects (PS4Threads). *_accept validates the
    // object for a setting that does not change anything here.
    attr_init, attr_destroy, attr_set_detach, attr_get_detach, attr_set_stack, attr_get_stack, attr_accept,
    mutexattr_init, mutexattr_destroy, mutexattr_settype, mutexattr_gettype, mutexattr_accept,
    // What a game's own libc.prx needs from libkernel (shadPS4 names):
    // __Error (the calling thread's errno cell), stack_chk_fail and
    // sceKernelDebugRaiseException (end the process like the exit function,
    // with status 134), sleeps and yields, thread identity and priority,
    // thread-specific keys, read/write locks, signals (no delivery: masks
    // and handlers are accepted), process facts, time zone (UTC).
    errno_location, abort_process, nanosleep, usleep, page_size, yield, thread_equal,
    key_create, key_delete, get_specific, set_specific,
    rwlock_init, rwlock_destroy, rwlock_rdlock, rwlock_wrlock, rwlock_tryrdlock, rwlock_trywrlock, rwlock_unlock,
    thread_getprio, thread_getaffinity, attr_get_np, attr_getaffinity,
    sigfillset, signal, utc_to_local, local_to_utc, proc_param, module_info_from_addr,
    // Accepted without effect, answering 0 (masks, priorities, heap API, ...).
    succeed,
    // Data objects (PS4KernelExport::data): filled by initialize_data().
    stack_chk_guard, progname, errno_table,
    // Guest code (PS4KernelExport::code), never a SYSCALL: pthread_once calls
    // its init routine from guest code.
    once
};
// Token bit of a trampoline whose function follows the POSIX convention: -1
// with the error in the calling thread's errno (__Error) instead of an Orbis
// kernel error code.
constexpr uint32_t posix_token = 0x40000000;
// Services only general images get: elsewhere their imports stay unresolved.
// A condition wait needs another thread to end it.
constexpr bool general_only(PS4KernelService service) {
    return service == PS4KernelService::thread_create || service == PS4KernelService::thread_join ||
           service == PS4KernelService::thread_exit || service == PS4KernelService::thread_detach ||
           service == PS4KernelService::cond_wait || service == PS4KernelService::cond_timedwait;
}
// Implemented libkernel functions (name, NID, service), e.g. for an HLE registry.
// `posix`: POSIX convention (see posix_token). `noreturn`: the trampoline ends
// with HLT. `data`: a data object of that many bytes instead of a function.
// `code`: a guest routine of `code_size` bytes instead of a trampoline.
struct PS4KernelExport {
    const char* name; const char* nid; PS4KernelService service;
    const char* library = "libkernel";
    bool posix = false, noreturn = false;
    uint32_t data = 0;
    const uint8_t* code = nullptr;
    size_t code_size = 0;
};
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
    // `posix`: the trampoline's token had posix_token; an Orbis error result
    // becomes -1 with that error in the thread's errno cell.
    int64_t invoke(PS4KernelService service, const PS4KernelCall& call, uint64_t thread = 1, bool posix = false);
    // Fills a linked data object (stack_chk_guard, progname, errno_table) at
    // its guest address. Returns false for an unknown service or bad memory.
    bool initialize_data(PS4KernelService service, uint64_t address, uint64_t size);
    // The root module's PT_SCE_PROCPARAM (sceKernelGetProcParam), 0 if none.
    void set_process_param(uint64_t address) { process_param_ = address; }
    PS4Threads& threads() { return threads_; }
    static constexpr int32_t abort_status = 134; // As a process killed by SIGABRT.
private:
    PS4GuestMemory& memory_;
    PS4VirtualMemory* dynamic_;
    PS4Threads threads_;
    uint64_t errno_table_ = 0, errno_table_size_ = 0, process_param_ = 0;
    int64_t posix_result(int64_t result, uint64_t thread);
    int64_t invoke_service(PS4KernelService service, const PS4KernelCall& call, uint64_t thread);
    int64_t extended(PS4KernelService service, const PS4KernelCall& call, uint64_t thread, bool& handled);
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
