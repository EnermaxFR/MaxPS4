#include "maxps4_guest_frame.h"
#include "maxps4_fex_runtime.h"
#include "maxps4_guest_stack.h"
#include "maxps4_orbis_v4_execution_gate.h"
#include <algorithm>
#include <iterator>
#include "maxps4_jit_memory.h"
#include "maxps4_guest_elf.h"
#include "SelfImage.hpp"
#include "maxps4_ps4_guest_memory.h"
#include "maxps4_ps4_libkernel.h"
#include "maxps4_ps4_linker.h"
#include "maxps4_ps4_synthetic_elf.h"
#include "PM4GuestSubmission.hpp"
#include "Common/HostFeatures.h"
#include <FEXCore/Config/Config.h>
#include <FEXCore/Core/Context.h>
#include <FEXCore/Core/SignalDelegator.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#else
#define TARGET_OS_IPHONE 0
#endif
#include <FEXCore/Core/CoreState.h>
#include <FEXCore/Core/X86Enums.h>
#include <FEXCore/Debug/InternalThreadState.h>
#include <FEXCore/HLE/SyscallHandler.h>
#include <array>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>
#include <mutex>
#include <new>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <optional>
#include <system_error>
#include <thread>
#include <pthread.h>
#include <signal.h>
#if defined(__APPLE__)
#include <sys/ucontext.h>
#else
#include <ucontext.h>
#endif

extern "C" int maxps4_native_guest_x86_run(const uint8_t*, size_t, uint32_t,
                                           uint64_t*) noexcept;
namespace {
class GuestThreads;
// scePthreadCreate/Join/Exit and the entry's exit function for a general run
// (see GuestThreads), defined once GuestThreads is complete.
int64_t guest_thread_service(GuestThreads& threads, maxps4::PS4KernelService service,
                             const maxps4::PS4KernelCall& call);
// Anonymous immutable guest memory, not a PS4 or Linux syscall ABI.
// A syscall has no permitted host effect and returns the real ENOSYS error.
class ProbeAddressSpace final : public FEXCore::HLE::SyscallHandler {
public:
    ProbeAddressSpace() { OSABI = FEXCore::HLE::SyscallOSABI::OS_GENERIC; }
    maxps4::GuestELF* image = nullptr;
    maxps4::PS4DynamicLinker* linker = nullptr;
    maxps4::PS4Libkernel* kernel = nullptr;
    maxps4::PS4GuestMemory* memory = nullptr;
    GuestThreads* threads = nullptr; // General runs only.
    uint64_t private_stack = 0;
    size_t private_stack_size = 0;
    // Guest threads of a general run call in concurrently.
    std::atomic<uint32_t> calls {0};
    std::atomic<int> callback_error {0};
    std::atomic<bool> exited {false};
    std::atomic<int32_t> exit_status {0};
    // PS4 identity (PS4Threads) of the guest thread on this host thread.
    static inline thread_local uint64_t guest_thread = 1;
    // Static TLS thread pointer of a child guest thread (0: the image's own).
    static inline thread_local uint64_t guest_tls = 0;
    uint64_t HandleSyscall(FEXCore::Core::CpuStateFrame* frame, FEXCore::HLE::SyscallArguments*) override {
        namespace x86 = FEXCore::X86State;
        auto& state = frame->State;
        auto& r = state.gregs;
        maxps4::PS4KernelService service{};
        const uint64_t rsp = r[x86::REG_RSP];
        int64_t result = maxps4::ps4::kernel_error(maxps4::ps4::nosys);
        if (!linker && image && kernel && memory && r[x86::REG_RAX] <= UINT32_MAX &&
            image->hle_gate(state.rip, static_cast<uint32_t>(r[x86::REG_RAX]))) {
            // Orbis path: SYSCALL from a linker-synthesized trampoline. The token is
            // the service; arguments follow SysV (RCX already moved to R10). The
            // seventh (stack) argument is readable for general images only.
            // The posix_token bit selects the POSIX convention (errno, -1).
            const bool posix = (r[x86::REG_RAX] & maxps4::posix_token) != 0;
            const auto orbis = static_cast<maxps4::PS4KernelService>(r[x86::REG_RAX] & ~uint64_t{maxps4::posix_token});
            // Admitted code builds pointers with RIP-relative LEA, i.e. rebased host
            // addresses. libkernel works on logical PS4 addresses and re-checks
            // permissions, so translate any argument that lies in the image arena.
            auto logical = [&](uint64_t value) {
                uint64_t guest = 0;
                return image->guest_address(value, guest) ? guest : value;
            };
            const maxps4::PS4KernelCall call{logical(r[x86::REG_RDI]),logical(r[x86::REG_RSI]),
                logical(r[x86::REG_RDX]),logical(r[x86::REG_R10]),logical(r[x86::REG_R8]),
                logical(r[x86::REG_R9]),
                // A general image's stack is guest memory: stack arguments (the
                // seventh, at RSP+8 past the trampoline's return address) are
                // readable. Bounded images keep 0, which makes them fault.
                image->general() ? r[x86::REG_RSP] : 0};
            if (orbis == maxps4::PS4KernelService::tls_get_addr) {
                // __tls_get_addr(&tls_index{module, offset}): the guest may have changed
                // tls_index, so validate it now. The admitted next instruction reads
                // through RAX, so never return an invalid pointer: on failure return the
                // (mapped) TCB and fail the run once FEX returns.
                uint64_t index[2]{};
                const uint8_t* address = !memory->read(call.rdi,index,sizeof(index)) ? nullptr :
                    guest_tls ? image->tls_address(guest_tls,index[0],index[1]) :
                    image->tls_address(index[0],index[1]);
                if (!address) {
                    callback_error = -EFAULT;
                    address = guest_tls ? reinterpret_cast<const uint8_t*>(guest_tls) : image->thread_pointer_host();
                }
                result = static_cast<int64_t>(reinterpret_cast<uintptr_t>(address));
            } else if (orbis == maxps4::PS4KernelService::process_exit || orbis == maxps4::PS4KernelService::abort_process) {
                // The entry's exit function: the status (EDI) becomes RAX, and the
                // HLT that follows this SYSCALL ends the calling thread. The first
                // exit sets the status; from a child thread it also ends the run.
                // exit() is the same; __stack_chk_fail and sceKernelDebugRaiseException
                // end the process with PS4Libkernel::abort_status.
                const bool abort = orbis == maxps4::PS4KernelService::abort_process;
                result = abort ? maxps4::PS4Libkernel::abort_status : static_cast<int32_t>(r[x86::REG_RDI]);
                bool first = false;
                if (exited.compare_exchange_strong(first, true)) exit_status = static_cast<int32_t>(result);
                if (threads) guest_thread_service(*threads,maxps4::PS4KernelService::process_exit,call);
            } else if (orbis == maxps4::PS4KernelService::thread_create ||
                       orbis == maxps4::PS4KernelService::thread_join ||
                       orbis == maxps4::PS4KernelService::thread_exit ||
                       orbis == maxps4::PS4KernelService::thread_detach) {
                // scePthreadExit's trampoline and the thread-return trampoline
                // both end with HLT after their SYSCALL.
                result = threads ? guest_thread_service(*threads,orbis,call) :
                    maxps4::ps4::kernel_error(maxps4::ps4::nosys);
            } else {
                result = kernel->invoke(orbis,call,guest_thread,posix);
            }
            ++calls;
            r[x86::REG_RAX] = static_cast<uint64_t>(result);
            state.rip += 2;
            return static_cast<uint64_t>(result);
        }
        if (!image || !linker || !kernel || !memory || r[x86::REG_RAX] > UINT32_MAX ||
            !linker->gate(*image,state.rip,static_cast<uint32_t>(r[x86::REG_RAX]),service)) {
            callback_error = -EPERM;
        } else if (rsp < private_stack || private_stack_size < 16 ||
                   rsp - private_stack > private_stack_size - 16 || (rsp & 7)) {
            callback_error = -EFAULT;
            result = maxps4::ps4::kernel_error(maxps4::ps4::fault);
        } else {
            // Never expose the rebased CPU RSP or CALL return pointer. Copy only
            // the seventh argument from the proven private CPU stack into a
            // logical guest frame consumed by existing sceKernelMmap.
            uint8_t argument[8];
            std::memcpy(argument,reinterpret_cast<const void*>(rsp+8),sizeof(argument));
            const uint64_t guest_rsp = linker->stack_top()-16;
            if (!memory->write(guest_rsp+8,argument,sizeof(argument))) {
                callback_error = -EFAULT;
                result = maxps4::ps4::kernel_error(maxps4::ps4::fault);
            } else {
                const maxps4::PS4KernelCall call{r[x86::REG_RDI],r[x86::REG_RSI],r[x86::REG_RDX],r[x86::REG_R10],
                    r[x86::REG_R8],r[x86::REG_R9],guest_rsp};
                result = kernel->invoke(service,call,1);
                ++calls;
            }
        }
        // OS_GENERIC spills/refills CPU registers, ignores the C++ return value
        // and resumes from State.rip. SYSCALL has already clobbered RCX/R11.
        r[x86::REG_RAX] = static_cast<uint64_t>(result);
        state.rip += 2;
        return static_cast<uint64_t>(result);
    }
    FEXCore::HLE::ExecutableRangeInfo QueryGuestExecutableRange(
        FEXCore::Core::InternalThreadState*, uint64_t address) override {
        if (image) for (const auto& segment : image->segments()) {
            if (!(segment.permissions & 1)) continue;
            // FEX receives rebased host addresses only for the restricted
            // admitted program. Resolve executable ranges through the same
            // guest permission contract used by entry dispatch.
            maxps4::PS4GuestMemory memory(*image);
            const size_t executable_bytes = memory.contiguous(
                segment.address, segment.file_size, 5);
            if (!executable_bytes) continue;
            // The executable file extent must be entirely backed by a
            // permission-checked segment. Never advertise a truncated range
            // to FEX as though the complete code extent were mapped.
            if (executable_bytes != segment.file_size) continue;
            const auto* pointer = image->translate(segment.address, executable_bytes, 5);
            if (!pointer) continue;
            const auto begin = reinterpret_cast<uintptr_t>(pointer);
            if (address >= begin && address - begin < executable_bytes) {
                // Verify the exact requested instruction address in the PS4
                // logical address domain, not only the segment's first byte.
                const uint64_t displacement = address - begin;
                if (displacement > UINT64_MAX - segment.address) continue;
                const uint64_t guest_address = segment.address + displacement;
                if (memory.contiguous(guest_address, 1, 5) != 1) continue;
                return {begin, executable_bytes, false};
            }
        }
        return {0, 0, false};
    }
    std::optional<FEXCore::ExecutableFileSectionInfo> LookupExecutableFileSection(
        FEXCore::Core::InternalThreadState*, uint64_t) override {
        return std::nullopt; // Anonymous mapping has no executable file section.
    }
};
class Mapping final {
public:
    Mapping(size_t bytes, int protection) : size(bytes), address(
        ::mmap(nullptr, bytes, protection, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) {}
    ~Mapping() {
        if (address != MAP_FAILED && ::munmap(address, size) != 0) {
            std::fprintf(stderr, "FEX probe mapping cleanup failed: errno=%d\n", errno);
            std::abort();
        }
    }
    Mapping(const Mapping&) = delete;
    Mapping& operator=(const Mapping&) = delete;
    size_t size;
    void* address;
};
// Guest fault containment for one ExecuteThread on an ARM64 host. A SIGSEGV,
// SIGBUS, SIGILL or SIGTRAP raised by this thread's JIT code (or by the
// dispatcher's guest-signal stubs) ends the run: the host stack is reset to
// the dispatcher frame and the PC moved to FEX's thread-stop handler, so
// ExecuteThread returns normally. Any other fault, including one inside FEX
// or HLE C++ code, chains to the previous handler. Under the VIXL simulator
// the JIT code is interpreted by C++, so nothing is contained there.
// Time limit: after `limit_ms`, a watchdog sends SIGURG to the executing thread
// every millisecond until it is stopped the same way, from JIT or dispatcher
// code (signal SIGURG). While the thread is in FEX or HLE C++ code the signal
// chains (SIGURG is ignored by default) and the next one retries. `expire`
// runs once when the limit elapses (it wakes HLE waits such as a join).
// A child guest thread's guard has `install` false: the main run's guard owns
// the process handlers for the whole run, this one only routes its thread.
class GuestFaultGuard final {
public:
    static constexpr bool available =
#if (defined(__aarch64__) || defined(__arm64__)) && !defined(VIXL_SIMULATOR)
        true;
#else
        false;
#endif
    GuestFaultGuard(FEXCore::Context::Context* context, FEXCore::Core::InternalThreadState* thread,
                    const FEXCore::SignalDelegatorConfig& config, uint32_t limit_ms,
                    bool install = true, std::function<void()> expire = {})
        : context(context), thread(thread), config(config) {
        if (!available) return;
        active = this;
        if (!install) return;
        struct sigaction action {};
        action.sa_sigaction = handle;
        action.sa_flags = SA_SIGINFO;
        sigemptyset(&action.sa_mask);
        for (; installed < signals.size(); ++installed)
            if (::sigaction(signals[installed], &action, &previous[installed]) != 0) break;
        if (limit_ms && installed == signals.size()) {
            const pthread_t target = ::pthread_self();
            watchdog = std::thread([this, target, limit_ms, expire = std::move(expire)] {
                std::unique_lock lock(watch_lock);
                if (watch.wait_for(lock, std::chrono::milliseconds(limit_ms), [this] { return finished; })) return;
                if (expire) expire();
                while (!finished && !signal.load()) {
                    ::pthread_kill(target, SIGURG);
                    watch.wait_for(lock, std::chrono::milliseconds(1), [this] { return finished; });
                }
            });
        }
    }
    ~GuestFaultGuard() {
        if (watchdog.joinable()) {
            { std::lock_guard lock(watch_lock); finished = true; }
            watch.notify_all();
            watchdog.join();
        }
        active = nullptr;
        while (installed) {
            --installed;
            if (::sigaction(signals[installed], &previous[installed], nullptr) != 0) std::abort();
        }
    }
    GuestFaultGuard(const GuestFaultGuard&) = delete;
    GuestFaultGuard& operator=(const GuestFaultGuard&) = delete;
    std::atomic<int> signal {0};
    uint64_t address = 0;
private:
    static constexpr std::array<int, 5> signals {SIGSEGV, SIGBUS, SIGILL, SIGTRAP, SIGURG};
    static inline std::array<struct sigaction, 5> previous {};
    static inline thread_local GuestFaultGuard* active = nullptr;
    static void handle(int number, siginfo_t* info, void* raw) {
#if (defined(__aarch64__) || defined(__arm64__)) && !defined(VIXL_SIMULATOR)
        auto* machine = static_cast<ucontext_t*>(raw);
#if defined(__APPLE__)
        auto& pc = machine->uc_mcontext->__ss.__pc;
        auto& sp = machine->uc_mcontext->__ss.__sp;
#else
        auto& pc = machine->uc_mcontext.pc;
        auto& sp = machine->uc_mcontext.sp;
#endif
        auto* guard = active;
        if (guard && !guard->signal.load()) {
            // JIT code keeps guest registers in host registers: spill them first.
            // The dispatcher's guest-signal stubs have already spilled.
            uint64_t stop = 0;
            if (guard->context->IsAddressInCodeBuffer(guard->thread, pc))
                stop = guard->config.ThreadStopHandlerAddressSpillSRA;
            else if (pc >= guard->config.DispatcherBegin && pc < guard->config.DispatcherEnd)
                stop = guard->config.ThreadStopHandlerAddress;
            if (stop) {
                guard->address = number == SIGURG ? 0 : reinterpret_cast<uintptr_t>(info->si_addr);
                guard->signal.store(number);
                sp = guard->thread->CurrentFrame->ReturningStackLocation;
                pc = stop;
                return;
            }
        }
#endif
        for (size_t i = 0; i < signals.size(); ++i) if (signals[i] == number) {
            const auto& old = previous[i];
            // SIGURG's default action is to ignore it: keep this handler installed
            // so the watchdog's next signal is still seen.
            if (old.sa_handler == SIG_IGN || (number == SIGURG && old.sa_handler == SIG_DFL)) return;
            if (old.sa_handler == SIG_DFL) {
                ::sigaction(number, &old, nullptr);
                ::raise(number);
            } else if (old.sa_flags & SA_SIGINFO) old.sa_sigaction(number, info, raw);
            else old.sa_handler(number);
            return;
        }
    }
    FEXCore::Context::Context* context;
    FEXCore::Core::InternalThreadState* thread;
    FEXCore::SignalDelegatorConfig config;
    size_t installed = 0;
    std::thread watchdog;
    std::mutex watch_lock;
    std::condition_variable watch;
    bool finished = false;
};
// Initial FEX state of one guest thread (main or child): zero integer
// registers, RIP/RSP, FS base, a guarded CALL/RET stack and a GDT holding one
// 64-bit user code segment. Owns that host memory until the thread is gone.
class FEXThreadSetup final {
public:
    static constexpr size_t callret_size = FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE;
    explicit FEXThreadSetup(size_t page) : page(page), callret(callret_size + 2 * page, PROT_NONE) {}
    bool mapped() const { return callret.address != MAP_FAILED; }
    int prepare(FEXCore::Core::InternalThreadState* thread, uint64_t rip, uint64_t rsp, uint64_t fs) {
        if (!mapped()) return -ENOMEM;
        auto* base = static_cast<uint8_t*>(callret.address) + page;
        if (::mprotect(base, callret_size, PROT_READ | PROT_WRITE) != 0) return -errno;
        auto& state = thread->CurrentFrame->State;
        std::fill(std::begin(state.gregs), std::end(state.gregs), uint64_t{});
        state.gregs[FEXCore::X86State::REG_RSP] = rsp;
        state.rip = rip;
        if (fs) state.fs_cached = fs;
        thread->CallRetStackBase = base;
        state.callret_sp = reinterpret_cast<uintptr_t>(base) + callret_size / 4;
        state.segment_arrays[FEXCore::Core::CPUState::SEGMENT_ARRAY_INDEX_GDT] = gdt.data();
        state.segment_arrays[FEXCore::Core::CPUState::SEGMENT_ARRAY_INDEX_LDT] = gdt.data();
        state.cs_idx = FEXCore::Core::CPUState::DEFAULT_USER_CS << 3;
        auto* segment = FEXCore::Core::CPUState::GetSegmentFromIndex(state, state.cs_idx);
        FEXCore::Core::CPUState::SetGDTBase(segment, 0);
        FEXCore::Core::CPUState::SetGDTLimit(segment, 0xF'FFFFU);
        segment->L = 1;
        segment->D = 0;
        state.cs_cached = FEXCore::Core::CPUState::CalculateGDTBase(*segment);
        return 0;
    }
private:
    size_t page;
    Mapping callret;
    std::array<FEXCore::Core::CPUState::gdt_segment, 32> gdt {};
};
// Child guest threads of one general run (scePthreadCreate). Each runs on its
// own host thread with its own FEX thread state: RIP = entry, RDI = arg, RSP =
// its GuestELF slot's stack top - 8 holding the thread-return trampoline, FS =
// the slot's TCB. Returning from the entry or calling scePthreadExit ends it on
// the trampoline's HLT; scePthreadJoin waits for that and frees the slot.
// The attribute object gives the detach state (a detached thread's slot is
// reused once it ends; scePthreadDetach does the same later) and the stack
// size, which must fit the slot's 1 MiB stack (larger: EAGAIN). Scheduling
// and affinity are not applied. After the main thread's scePthreadExit the run waits for the other
// threads. When the main thread's run ends, every remaining thread is stopped
// (SIGURG, as the watchdog does; without fault containment they are waited
// for) and joined. A contained fault in a child thread, or a child calling the
// entry's exit function, stops the main thread too; a fault becomes the run's
// result.
class GuestThreads final {
public:
    GuestThreads(FEXCore::Context::Context* context, const FEXCore::SignalDelegatorConfig& config,
                 maxps4::GuestELF& image, maxps4::PS4GuestMemory& memory,
                 maxps4::PS4Threads& identities, size_t page)
        : context(context), config(config), image(image), memory(memory), identities(identities),
          page(page), main_thread(::pthread_self()) { identities.resume(); }
    // Locks and waits of a stopping run return EINTR (PS4Threads::interrupt);
    // later runs block normally again.
    ~GuestThreads() { finish(); identities.resume(); }
    GuestThreads(const GuestThreads&) = delete;
    GuestThreads& operator=(const GuestThreads&) = delete;
    // scePthreadCreate(PthreadT* out, attr, entry, arg, name): *out = the new
    // thread's PS4 identity (also what scePthreadSelf returns in it).
    int64_t create(const maxps4::PS4KernelCall& call) {
        using namespace maxps4::ps4;
#if defined(VIXL_SIMULATOR)
        // FEX's VIXL simulator belongs to the context's dispatcher: concurrent
        // guest threads would share one simulated CPU.
        (void)call;
        return kernel_error(nosys);
#endif
        if (!call.rdi || !image.translate(call.rdx, 1, 5)) return kernel_error(inval);
        bool detached = false;
        uint64_t stack = 0;
        if (const auto error = identities.thread_attributes(call.rsi, detached, stack)) return kernel_error(error);
        if (stack > maxps4::GuestELF::generalStack) return kernel_error(again);
        std::unique_lock guard(lock);
        if (stopping) return kernel_error(again);
        reap(guard);
        if (stopping) return kernel_error(again);
        const auto slot = std::find_if(threads.begin(), threads.end(), [](const Thread& t) { return !t.used; });
        if (slot == threads.end()) return kernel_error(again);
        uint64_t top = 0, tp = 0;
        if (!image.prepare_thread(static_cast<size_t>(slot - threads.begin()), top, tp)) return kernel_error(nomem);
        const uint64_t id = identities.attach();
        if (!id) return kernel_error(again);
        uint8_t handle[8], back[8];
        put_le(handle, 0, id, 8);
        put_le(back, 0, image.thread_return(), 8);
        if (!memory.write(call.rdi, handle, sizeof(handle)) || !memory.write(top - 8, back, sizeof(back))) {
            identities.detach(id);
            return kernel_error(fault);
        }
        slot->id = id; slot->entry = call.rdx; slot->arg = call.rcx;
        slot->rsp = top - 8; slot->fs = tp; slot->value = 0;
        slot->finished = slot->joining = slot->ends_process = false;
        slot->detached = detached;
        try {
            slot->host = std::thread([this, thread = &*slot] { run(*thread); });
        } catch (const std::system_error&) {
            identities.detach(id);
            return kernel_error(again);
        }
        slot->used = true;
        return 0;
    }
    // scePthreadJoin(thread, void** value).
    int64_t join(uint64_t id, uint64_t out) {
        using namespace maxps4::ps4;
        if (id == ProbeAddressSpace::guest_thread) return kernel_error(deadlk);
        std::unique_lock guard(lock);
        const auto target = std::find_if(threads.begin(), threads.end(),
            [id](const Thread& t) { return t.used && t.id == id; });
        if (target == threads.end()) return kernel_error(srch);
        if (target->joining || target->detached) return kernel_error(inval);
        target->joining = true;
        changed.wait(guard, [&] { return target->finished || stopping; });
        // Stopping (time limit or a child's fault): return to guest code, where
        // the stop signals land, and leave the thread to finish(). A faulting
        // child only exits once the main run is done, so joining it here would
        // never return.
        if (stopping) { target->joining = false; return kernel_error(intr); }
        // finish() may join hosts too: whoever moves the handle out joins it.
        std::thread host = std::move(target->host);
        const uint64_t value = target->value;
        guard.unlock();
        if (host.joinable()) host.join();
        guard.lock();
        // A thread that still owns a mutex keeps its identity (PS4Threads).
        identities.detach(id);
        target->used = target->joining = false;
        guard.unlock();
        if (!out) return 0;
        uint8_t wire[8];
        put_le(wire, 0, value, 8);
        return memory.write(out, wire, sizeof(wire)) ? 0 : kernel_error(fault);
    }
    // scePthreadDetach(thread): its slot is reused once it ends.
    int64_t detach(uint64_t id) {
        using namespace maxps4::ps4;
        std::unique_lock guard(lock);
        const auto target = std::find_if(threads.begin(), threads.end(),
            [id](const Thread& t) { return t.used && t.id == id; });
        if (target == threads.end()) return kernel_error(srch);
        if (target->joining || target->detached) return kernel_error(inval);
        target->detached = true;
        reap(guard);
        return 0;
    }
    // scePthreadExit(value), or the return of a child's entry: the HLT after
    // the SYSCALL ends the calling thread. On the main thread the process
    // lives on until its other threads end (wait()).
    void exit(uint64_t value) {
        if (current) { current->value = value; return; }
        std::lock_guard guard(lock);
        main_exited = true;
    }
    // The entry's exit function: called from a child thread, that thread ends
    // on the HLT after the SYSCALL and then stops the main run.
    void process_exit() { if (current) current->ends_process = true; }
    bool main_thread_exited() const {
        std::lock_guard guard(lock);
        return main_exited;
    }
    // After the main thread's scePthreadExit: until every child has ended, or
    // a stop (time limit, fault, exit function) is requested.
    void wait() {
        std::unique_lock guard(lock);
        changed.wait(guard, [&] {
            return stopping || std::none_of(threads.begin(), threads.end(),
                [](const Thread& t) { return t.used && !t.finished; });
        });
    }
    // Time limit expired: wake every join, lock and wait so the stop signals
    // can land.
    void interrupt() {
        { std::lock_guard guard(lock); stopping = timed_out = true; }
        changed.notify_all();
        identities.interrupt();
    }
    bool expired() const {
        std::lock_guard guard(lock);
        return timed_out;
    }
    // The main thread's run ended: stop and join every child thread.
    void finish() {
        std::unique_lock guard(lock);
        if (main_done) return;
        stopping = main_done = true;
        changed.notify_all();
        identities.interrupt();
        for (auto& thread : threads) {
            while (thread.used && !thread.finished) {
                if (GuestFaultGuard::available && thread.host.joinable())
                    ::pthread_kill(thread.host.native_handle(), SIGURG);
                changed.wait_for(guard, std::chrono::milliseconds(1));
            }
        }
        // Every child has finished, so every join in progress has returned.
        for (auto& thread : threads) {
            if (!thread.used) continue;
            std::thread host = std::move(thread.host);
            guard.unlock();
            if (host.joinable()) host.join();
            guard.lock();
            identities.detach(thread.id);
            thread.used = false;
        }
    }
    bool faulted(maxps4_fex_guest_fault& out) const {
        std::lock_guard guard(lock);
        out = first_fault;
        return first_fault.signal != 0;
    }
private:
    struct Thread {
        bool used = false, joining = false, finished = false, ends_process = false, detached = false;
        uint64_t id = 0, entry = 0, arg = 0, rsp = 0, fs = 0, value = 0;
        std::thread host;
    };
    // Gives back the slots of detached threads that have ended. Never while
    // stopping: a thread that stops the main run waits for it to be done.
    void reap(std::unique_lock<std::mutex>& guard) {
        for (auto& thread : threads) {
            if (stopping || !thread.used || !thread.detached || !thread.finished) continue;
            std::thread host = std::move(thread.host);
            const uint64_t id = thread.id;
            thread.used = false;
            guard.unlock();
            if (host.joinable()) host.join(); // It has ended: only returns from run().
            guard.lock();
            identities.detach(id);
        }
    }
    void run(Thread& thread) {
        ProbeAddressSpace::guest_thread = thread.id;
        ProbeAddressSpace::guest_tls = thread.fs;
        current = &thread;
        maxps4_fex_guest_fault fault {};
        bool skip = false;
        { std::lock_guard guard(lock); skip = stopping; }
        if (!skip) {
            FEXThreadSetup setup(page);
            if (auto* fex = context->CreateThread(thread.entry, thread.rsp)) {
                if (setup.prepare(fex, thread.entry, thread.rsp, thread.fs) == 0) {
                    fex->CurrentFrame->State.gregs[FEXCore::X86State::REG_RDI] = thread.arg;
                    GuestFaultGuard guard(context, fex, config, 0, false);
                    context->ExecuteThread(fex);
                    const int signal = guard.signal.load();
                    // SIGURG only comes from finish(): a stop, not a fault.
                    if (signal && signal != SIGURG)
                        fault = {signal, 0, guard.address, fex->CurrentFrame->State.rip};
                }
                context->DestroyThread(fex);
            }
        }
        current = nullptr;
        bool stop_main = false;
        {
            std::lock_guard guard(lock);
            if (fault.signal && !first_fault.signal) { first_fault = fault; stop_main = stopping = true; }
            if (thread.ends_process) stop_main = stopping = true;
            thread.finished = true; // `thread` may be reused from here on.
        }
        changed.notify_all();
        if (!stop_main) return;
        // A faulting or exiting thread ends the process: stop the main thread too.
        identities.interrupt();
        std::unique_lock guard(lock);
        while (!main_done) {
            ::pthread_kill(main_thread, SIGURG);
            changed.wait_for(guard, std::chrono::milliseconds(1));
        }
    }
    FEXCore::Context::Context* context;
    FEXCore::SignalDelegatorConfig config;
    maxps4::GuestELF& image;
    maxps4::PS4GuestMemory& memory;
    maxps4::PS4Threads& identities;
    size_t page;
    pthread_t main_thread;
    mutable std::mutex lock;
    std::condition_variable changed;
    std::array<Thread, maxps4::GuestELF::generalThreads> threads {};
    bool stopping = false, main_done = false, main_exited = false, timed_out = false;
    maxps4_fex_guest_fault first_fault {};
    static inline thread_local Thread* current = nullptr;
};
int64_t guest_thread_service(GuestThreads& threads, maxps4::PS4KernelService service,
                             const maxps4::PS4KernelCall& call) {
    using maxps4::PS4KernelService;
    switch (service) {
    case PS4KernelService::thread_create: return threads.create(call);
    case PS4KernelService::thread_join: return threads.join(call.rdi, call.rsi);
    case PS4KernelService::thread_exit: threads.exit(call.rdi); return 0;
    case PS4KernelService::thread_detach: return threads.detach(call.rdi);
    case PS4KernelService::process_exit: threads.process_exit(); return 0;
    default: return maxps4::ps4::kernel_error(maxps4::ps4::nosys);
    }
}
}
struct maxps4_fex_engine {
    // This real FEX configuration holder installs no process signal handlers.
    // It outlives the context. Only admitted no-fault straight-line code executes.
    FEXCore::SignalDelegator signals;
    ProbeAddressSpace probe;
    maxps4::GuestELF staged_ps4;
    // V4 is opt-in and isolated from the stable V3 image/virtual memory.
    maxps4::GuestAddressSpace v4_address_space;
    maxps4::OrbisV4ExecutionGate v4_execution_gate{v4_address_space};
    bool orbis_admitted = false;
    maxps4_fex_cpu_report cpu_report{};
    maxps4_fex_guest_fault last_fault{};
    uint32_t time_limit_ms = 0; // 0: no limit.
    maxps4::GuestFrame frame;
    uint64_t frame_sequence = 0;
    bool frame_code_cached = false;
    maxps4::PS4VirtualMemory dynamic_ps4{staged_ps4};
    maxps4::PS4GuestMemory system_memory{staged_ps4, &dynamic_ps4};
    maxps4::PS4Libkernel libkernel{system_memory, &dynamic_ps4};
    maxps4::PS4DynamicLinker linker;
    fextl::unique_ptr<FEXCore::Context::Context> context;
    int32_t stage = MAXPS4_FEX_CONTEXT_CREATED;
};
namespace {
// FEX configuration is process-global. This adapter owns it until the last
// engine closes. Future independent FEX consumers must share this ownership.
std::mutex runtime_lock;
size_t engines = 0;
#if TARGET_OS_IPHONE
// The prepared pool contains one dispatcher and one code cache.
maxps4_fex_engine* jit_owner = nullptr;
#endif
void release_config_if_unused() {
    if (engines == 0) FEXCore::Config::Shutdown();
}
}
extern "C" int32_t maxps4_fex_engine_open(maxps4_fex_engine **out) {
    if (!out) return -EINVAL;
    *out = nullptr;
    std::lock_guard lock(runtime_lock);
    auto *engine = new (std::nothrow) maxps4_fex_engine;
    if (!engine) return -ENOMEM;
    if (engines == 0) {
        FEXCore::Config::Initialize();
        FEXCore::Config::Load();
        FEXCore::Config::Set(FEXCore::Config::CONFIG_IS64BIT_MODE, "1");
        FEXCore::Config::Set(FEXCore::Config::CONFIG_DISABLETELEMETRY, "1");
        FEXCore::Config::Set(FEXCore::Config::CONFIG_ENABLECODECACHINGWIP, "0");
#if defined(VIXL_SIMULATOR)
        // The Darwin dependency defaults to native indirect runtime calls.
        // x86 CI's upstream ARM64 simulator must use its real C++ ABI wrappers.
        FEXCore::Config::Set(FEXCore::Config::CONFIG_DISABLE_VIXL_INDIRECT_RUNTIME_CALLS, "0");
#endif
    }
    // Real Darwin sysctl-based feature discovery; never fabricated features.
    const auto features = FEX::FetchHostFeatures();
    engine->context = FEXCore::Context::Context::CreateNewContext(features);
    if (!engine->context) {
        delete engine;
        release_config_if_unused();
        return -ENOMEM;
    }
    // InitCore is explicit; opening a context never enables guest execution.
    ++engines;
    *out = engine;
    return 0;
}
extern "C" void maxps4_fex_engine_close(maxps4_fex_engine *engine) {
    if (!engine) return;
    std::lock_guard lock(runtime_lock);
#if TARGET_OS_IPHONE
    const bool owned_pool = jit_owner == engine;
#endif
    delete engine; // Context dies under runtime_lock before another owner opens.
#if TARGET_OS_IPHONE
    if (owned_pool) jit_owner = nullptr;
#endif
    --engines;
    release_config_if_unused();
}
extern "C" int32_t maxps4_fex_engine_initialize(maxps4_fex_engine *engine) {
    if (!engine) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    if (engine->stage == MAXPS4_FEX_DISPATCHER_INITIALIZED) return 0;
#if TARGET_OS_IPHONE
    if (jit_owner && jit_owner != engine) return -EBUSY;
    // Verify the current nonce, Mach W^X aliases and native canary before FEX
    // emits code into the prepared dispatcher/cache pool.
    const int32_t prepared = maxps4_jit_prepare();
    if (prepared != 0) {
        std::fprintf(stderr, "MaxPS4 FEX JIT: RX/RW preparation failed (%d).\n", prepared);
        return prepared;
    }
    // Retain ownership even after a partial InitCore failure until close.
    jit_owner = engine;
#endif
    engine->context->SetSignalDelegator(&engine->signals);
    // Only admitted HLT-terminated straight-line programs are exposed.
    engine->context->SetSyscallHandler(&engine->probe);
    engine->context->EnableExitOnHLT();
    errno = 0;
    try {
        if (!engine->context->InitCore()) return -(errno ? errno : ENOMEM);
    } catch (const std::bad_alloc&) { return -ENOMEM; }
    engine->stage = MAXPS4_FEX_DISPATCHER_INITIALIZED;
    return 0;
}
extern "C" int32_t maxps4_fex_engine_stage(const maxps4_fex_engine *engine) {
    std::lock_guard lock(runtime_lock);
    return engine ? engine->stage : 0;
}
extern "C" int32_t maxps4_fex_engine_last_cpu_report(const maxps4_fex_engine* engine,
    maxps4_fex_cpu_report* report) {
    if (!report) return -EINVAL;
    *report = {};
    if (!engine) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    *report = engine->cpu_report;
    return report->completed ? 0 : -EAGAIN;
}
extern "C" int32_t maxps4_fex_engine_last_guest_fault(const maxps4_fex_engine* engine,
    maxps4_fex_guest_fault* fault) {
    if (!fault) return -EINVAL;
    *fault = {};
    if (!engine) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    *fault = engine->last_fault;
    return fault->signal ? 0 : -ENOENT;
}
extern "C" int32_t maxps4_fex_engine_set_time_limit(maxps4_fex_engine* engine,
    uint32_t milliseconds) {
    if (!engine || milliseconds > 3600 * 1000) return -EINVAL;
    if (milliseconds && !GuestFaultGuard::available) return -ENOTSUP;
    std::lock_guard lock(runtime_lock);
    engine->time_limit_ms = milliseconds;
    return 0;
}
extern "C" int32_t maxps4_fex_engine_cpuid(maxps4_fex_engine *engine,
                                          uint32_t leaf, uint32_t subleaf,
                                          uint32_t out[4]) {
    if (!engine || !out) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    const auto values = engine->context->RunCPUIDFunction(leaf, subleaf);
    out[0] = values.eax;
    out[1] = values.ebx;
    out[2] = values.ecx;
    out[3] = values.edx;
    return 0;
}
extern "C" int32_t maxps4_fex_engine_run(maxps4_fex_engine *engine,
                                        const uint8_t *code, size_t length,
                                        uint32_t budget, uint64_t *result,
                                        uint32_t *backend) {
    if (result) *result = 0;
    if (backend) *backend = 0;
    if (!engine || !code || !length || length > 65536 || !budget ||
        budget > 1000000 || !result || !backend) return -EINVAL;
    if (maxps4_native_guest_x86_run(code, length, budget, result) != 1) {
        *result = 0;
        return -ENOTSUP;
    }
    *backend = MAXPS4_CPU_RESTRICTED_INTERPRETER;
    return 0;
}

extern "C" int32_t maxps4_fex_engine_probe(maxps4_fex_engine *engine,
                                          uint64_t *result, uint32_t *backend) {
    // Compatibility wrapper: even the legacy probe goes through the ELF loader.
    std::array<uint8_t, 131> file {};
    auto put = [&](uint64_t value, size_t at, size_t width) {
        for (size_t i = 0; i < width; ++i) file[at + i] = value >> (i * 8);
    };
    std::memcpy(file.data(), "\177ELF\2\1\1", 7);
    put(2,16,2); put(62,18,2); put(1,20,4); put(0x1000,24,8);
    put(64,32,8); put(64,52,2); put(56,54,2); put(1,56,2);
    put(1,64,4); put(5,68,4); put(120,72,8); put(0x1000,80,8);
    put(11,96,8); put(11,104,8); put(1,112,8);
    constexpr uint8_t code[] = {0xB8,40,0,0,0,0x05,2,0,0,0,0xF4};
    std::memcpy(file.data() + 120, code, sizeof(code));
    uint64_t entry = 0;
    return maxps4_fex_engine_run_elf(engine, file.data(), file.size(), 3, result, backend, &entry);
}

namespace {
struct PM4Request {
    uint64_t address;
    size_t count;
    uint32_t* words;
    size_t* written;
};
int32_t executeImage(maxps4_fex_engine* engine, maxps4::GuestELF& image,
    uint64_t* result, uint32_t* backend, const PM4Request* submission, bool linked = false, bool persistent_frame = false) {
    if (engine->stage != MAXPS4_FEX_DISPATCHER_INITIALIZED) return -EAGAIN;
    // FEX still uses rebased host addresses for the admitted instruction set.
    // Check the logical PS4 entry via the shared guest-memory contract before
    // giving FEX a host pointer. This is not general address-space translation.
    maxps4::PS4GuestMemory guest_memory(image);
    if (guest_memory.contiguous(image.start(), 1, 5) != 1) return -EFAULT;
    // Fetch a bounded instruction window through the same permission-checked
    // guest contract used by CPU clients. Never cross the executable segment.
    // The decoder still enforces the admitted instruction whitelist.
    std::array<uint8_t, 15> entry_bytes {};
    const size_t fetch_length = guest_memory.contiguous(image.start(), entry_bytes.size(), 5);
    if (!fetch_length || !guest_memory.fetch(image.start(), entry_bytes.data(), fetch_length))
        return -EFAULT;
    const auto* entry_pointer = image.start_pointer();
    if (!entry_pointer) return -EFAULT;
    const long host_page = ::sysconf(_SC_PAGESIZE);
    if (host_page <= 0) return -EIO;
    const size_t page = static_cast<size_t>(host_page);
    Mapping stack(3 * page, PROT_NONE);
    if (stack.address == MAP_FAILED) return -errno;
    FEXThreadSetup setup(page);
    if (!setup.mapped()) return -errno;
    auto* stack_base = static_cast<uint8_t*>(stack.address) + page;
    if (::mprotect(stack_base, page, PROT_READ | PROT_WRITE) != 0) return -errno;
    engine->probe.image = &image;
    engine->probe.linker = linked ? &engine->linker : nullptr;
    // Orbis images with synthesized HLE trampolines also reach libkernel.
    engine->probe.kernel = linked || image.has_hle() ? &engine->libkernel : nullptr;
    engine->probe.memory = linked || image.has_hle() ? &engine->system_memory : nullptr;
    engine->probe.private_stack = reinterpret_cast<uintptr_t>(stack_base);
    engine->probe.private_stack_size = page;
    engine->probe.calls = 0;
    engine->probe.callback_error = 0;
    struct ClearRange {
        ProbeAddressSpace& space;
        ~ClearRange() {
            space.image = nullptr; space.linker = nullptr; space.kernel = nullptr;
            space.memory = nullptr; space.private_stack = 0; space.private_stack_size = 0;
            space.threads = nullptr;
        }
    } clear_range {engine->probe};
    engine->cpu_report = {};
    uint64_t initial_rsp = 0;
    const uint64_t initial_rip = reinterpret_cast<uintptr_t>(entry_pointer);
    // A general image owns its stack inside guest memory, so stack buffers are
    // valid HLE pointer arguments; the bounded paths keep the private page.
    // Its startup stub then lays out the PS4 entry stack from EntryParams at
    // the stack top, as shadPS4 does; the bounded paths keep the SysV auxv.
    const bool ps4_entry = image.general();
    if (ps4_entry) {
        initial_rsp = image.write_entry_params();
        if (!initial_rsp) return -EFAULT;
    } else {
        size_t guest_stack_size = page;
        uint8_t* guest_stack = image.stack_host(guest_stack_size);
        if (!guest_stack) { guest_stack = stack_base; guest_stack_size = page; }
        const int stack_status = maxps4::initialize_guest_stack(guest_stack, guest_stack_size, initial_rip, page, initial_rsp);
        if (stack_status) return stack_status;
    }
    engine->probe.exited = false;
    engine->probe.exit_status = 0;
    auto* thread = engine->context->CreateThread(initial_rip, initial_rsp);
    if (!thread) return -ENOMEM;
    struct DestroyThread {
        FEXCore::Context::Context* context;
        FEXCore::Core::InternalThreadState* thread;
        ~DestroyThread() { context->DestroyThread(thread); }
    } destroy {engine->context.get(), thread};
    auto& state = thread->CurrentFrame->State;
    // Static TLS: FS base is the host address of the image's TCB (variant II).
    const auto* tcb = image.thread_pointer_host();
    if (const int prepared = setup.prepare(thread, initial_rip, initial_rsp, reinterpret_cast<uintptr_t>(tcb)))
        return prepared;
    auto& observed = engine->cpu_report;
    observed.entry = image.entry();
    observed.rsp_alignment = state.gregs[FEXCore::X86State::REG_RSP] & 15;
    observed.entry_matches = state.rip == initial_rip;
    observed.zero_integer_registers = 1;
    for (size_t i = 0; i < std::size(state.gregs); ++i)
        if (i != FEXCore::X86State::REG_RSP && state.gregs[i]) observed.zero_integer_registers = 0;
    if (ps4_entry) {
        uint32_t argc = 0;
        std::memcpy(&argc, reinterpret_cast<const void*>(initial_rsp), sizeof(argc));
        observed.argc = argc; // EntryParams; no SysV argv/envp/auxv.
    } else {
        uint64_t stack_words[9]{};
        std::memcpy(stack_words, reinterpret_cast<const void*>(initial_rsp), sizeof(stack_words));
        observed.argc = stack_words[0];
        observed.argv_env_null = stack_words[1] == 0 && stack_words[2] == 0;
        observed.page_size = stack_words[4];
        observed.auxv_valid = stack_words[3] == 6 && stack_words[5] == 9 &&
            stack_words[6] == state.rip && stack_words[7] == 0 && stack_words[8] == 0;
    }
    // mmap can reuse a previous image's address. The context retains a shared
    // L3 guest-to-host cache across threads: discard those old translations
    // without allocating another iOS JIT buffer. All engine execution is locked.
    if (!persistent_frame || !engine->frame_code_cached) engine->context->ClearCodeCache(thread, false);
    engine->frame_code_cached = persistent_frame;
    engine->last_fault = {};
    {
        // General images may create guest threads; all are joined before the
        // guard (which owns the signal handlers) goes away.
        std::optional<GuestThreads> threads;
        if (ps4_entry && engine->probe.kernel)
            threads.emplace(engine->context.get(), engine->signals.GetConfig(), image,
                            engine->system_memory, engine->libkernel.threads(), page);
        engine->probe.threads = threads ? &*threads : nullptr;
        GuestFaultGuard guard(engine->context.get(), thread, engine->signals.GetConfig(), engine->time_limit_ms,
                              true, [&threads] { if (threads) threads->interrupt(); });
        engine->context->ExecuteThread(thread);
        int signal = guard.signal.load();
        // scePthreadExit on the main thread: the other threads carry on.
        const bool main_exit = threads && !signal && threads->main_thread_exited();
        if (main_exit) threads->wait();
        maxps4_fex_guest_fault child {};
        if (threads) threads->finish();
        engine->probe.threads = nullptr;
        if (main_exit && threads->expired() && !engine->probe.exited) signal = SIGURG;
        // A child's exit function stops the main thread with SIGURG: not a timeout.
        if (signal == SIGURG && engine->probe.exited) signal = 0;
        // RIP is the guest position FEX last stored (block granularity).
        if (threads && threads->faulted(child)) { engine->last_fault = child; signal = child.signal; }
        else if (signal) engine->last_fault = {signal, 0, guard.address, state.rip};
        if (signal) {
            engine->frame_code_cached = false;
            return signal == SIGURG ? -ETIMEDOUT :
                signal == SIGSEGV || signal == SIGBUS ? -EFAULT : -EILSEQ;
        }
        // The last thread ended after the main thread's scePthreadExit: exit(0).
        if (main_exit) engine->probe.exited = true;
    }
    if (engine->probe.callback_error) return engine->probe.callback_error;
    if (submission) {
        const auto snapshot = maxps4::gpu::snapshotPM4(image, submission->address, submission->count);
        if (snapshot.error) return snapshot.error;
        std::memcpy(submission->words, snapshot.words.data(), snapshot.words.size() * sizeof(uint32_t));
        *submission->written = snapshot.words.size();
    }
    *result = state.gregs[FEXCore::X86State::REG_RAX];
    observed.rax = *result;
    observed.exited = engine->probe.exited ? 1 : 0;
    observed.exit_status = engine->probe.exit_status;
    observed.completed = 1;
#if defined(VIXL_SIMULATOR)
    *backend = MAXPS4_CPU_FEX_ARM64_SIMULATOR;
#else
    *backend = MAXPS4_CPU_FEX_ARM64_JIT;
#endif
    return 0;
}
int32_t runELF(maxps4_fex_engine *engine,
    const uint8_t *file, size_t length, uint32_t budget, uint64_t *result,
    uint32_t *backend, uint64_t *entry, const PM4Request* submission) {
    if (result) *result = 0;
    if (backend) *backend = 0;
    if (entry) *entry = 0;
    if (!engine || !result || !backend || !entry) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    // iOS uses the bounded Darwin ELF execution path after verified InitCore.
    try {
    maxps4::GuestELF image;
    const int loaded = image.load(file, length, budget);
    if (loaded) return loaded;
    *entry = image.entry();
    if (submission) {
        if ((submission->address & 3U) ||
            submission->address > UINT64_MAX - submission->count * 4) return -EINVAL;
        if (!image.translate(submission->address, submission->count * 4, 4)) return -EFAULT;
        // The CPU may produce PM4 in initially zero-filled RW data. Interpret
        // only after FEX completes; range/permissions are checked before execution.
    }
    return executeImage(engine, image, result, backend, submission);
    } catch (const std::bad_alloc&) { return -ENOMEM; }
}


} // namespace
extern "C" int32_t maxps4_fex_engine_stage_ps4(maxps4_fex_engine* engine,
    const uint8_t* file, size_t length, uint64_t* entry, uint32_t* segments, uint32_t* blockers) {
    if (entry) *entry = 0;
    if (segments) *segments = 0;
    if (blockers) *blockers = 0;
    if (!engine) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    engine->libkernel.reset();
    engine->linker.reset();
    engine->staged_ps4.clear();
    engine->orbis_admitted = false; engine->frame = {}; engine->frame_sequence = 0; engine->frame_code_cached = false;
    if (!entry || !segments || !blockers) return -EINVAL;
    const int error = engine->staged_ps4.stage_ps4(file, length);
    if (error) return error;
    *entry = engine->staged_ps4.entry();
    *segments = static_cast<uint32_t>(engine->staged_ps4.segments().size());
    *blockers = engine->staged_ps4.ps4_blockers();
    std::fprintf(stderr, "MaxPS4 PS4 native staging: entry=0x%llx segments=%u blockers=0x%x; CPU not dispatched\n",
        static_cast<unsigned long long>(*entry), *segments, *blockers);
    return 0;
}
extern "C" int32_t maxps4_fex_engine_link_ps4(maxps4_fex_engine* engine,
    const uint8_t* file, size_t length, uint32_t budget, uint64_t* entry, uint32_t* imports) {
    if (entry) *entry = 0;
    if (imports) *imports = 0;
    if (!engine) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    engine->libkernel.reset();
    engine->dynamic_ps4.reset();
    engine->linker.reset();
    engine->staged_ps4.clear();
    engine->orbis_admitted = false; engine->frame = {}; engine->frame_sequence = 0; engine->frame_code_cached = false;
    if (!entry || !imports) return -EINVAL;
    const int error = engine->linker.load(engine->staged_ps4,file,length,budget);
    if (error) return error;
    *entry = engine->staged_ps4.entry();
    *imports = static_cast<uint32_t>(engine->linker.imports().size());
    return 0;
}
extern "C" int32_t maxps4_fex_engine_run_linked_ps4(maxps4_fex_engine* engine,
    uint64_t* result, uint32_t* backend, uint64_t* entry, uint32_t* calls) {
    if (result) *result = 0;
    if (backend) *backend = 0;
    if (entry) *entry = 0;
    if (calls) *calls = 0;
    if (!engine || !result || !backend || !entry || !calls) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    if (engine->linker.imports().empty()) return -EFAULT;
    try {
        const int error = executeImage(engine,engine->staged_ps4,result,backend,nullptr,true);
        if (error) return error;
    } catch (const std::bad_alloc&) { return -ENOMEM; }
    *entry = engine->staged_ps4.entry();
    *calls = engine->probe.calls;
    return 0;
}
namespace {
// Validates the C module table into linker inputs; libkernel is provided by
// HLE: one synthesized trampoline per implemented NID, identity
// libkernel/libkernel version 1 (as resolve_libkernel). Services only general
// images get are offered when `general`; scePthreadExit's trampoline ends
// with HLT: FEX goes on after a SYSCALL whatever the handler sets in RIP.
// A SELF with plaintext blocks (homebrew fSELF) is linked as its plain ELF,
// kept in `plain` until the image is prepared; an encrypted one is refused
// with `why`.
int orbisInputs(const maxps4_orbis_module* modules, size_t count, bool general,
    std::vector<std::vector<uint8_t>>& plain, std::string& why,
    std::vector<maxps4::OrbisLinker::Input>& inputs, std::vector<maxps4::OrbisLinker::HLE>& hle) {
    for (size_t i=0;i<count;++i) {
        const auto& m = modules[i];
        if (!m.path || !m.bytes || m.dependency_count > 128 ||
            (m.dependency_count && !m.dependencies)) return -EINVAL;
        if (::strnlen(m.path,256)>255 || m.size>32*1024*1024) return -E2BIG;
        const uint8_t* bytes = m.bytes;
        size_t size = m.size;
        std::vector<uint8_t> elf;
        const int unwrapped = maxps4::SelfImage::unwrap(bytes,size,elf,why);
        if (unwrapped < 0) { why = std::string(m.path) + ": " + why; return unwrapped; }
        if (unwrapped) {
            plain.push_back(std::move(elf)); // Moving keeps the buffer where it is.
            bytes = plain.back().data();
            size = plain.back().size();
        }
        maxps4::OrbisLinker::Input in{{{m.path,{}},bytes,size},m.load_bias};
        for (size_t j=0;j<m.dependency_count;++j) {
            if (!m.dependencies[j] || ::strnlen(m.dependencies[j],256)>255) return -EINVAL;
            in.file.request.dependencies.emplace_back(m.dependencies[j]);
        }
        inputs.push_back(std::move(in));
    }
    size_t exported = 0;
    const auto* kernel_exports = maxps4::libkernel_exports(exported);
    for (size_t i=0;i<exported;++i) {
        const auto& e = kernel_exports[i];
        // Data objects and guest routines serve the libc of general images.
        if (!general && (maxps4::general_only(e.service) || e.data || e.code)) continue;
        maxps4::OrbisLinker::Identity id{e.nid,e.library,e.library,1,1,uint8_t(e.data ? 1 : 2)};
        if (e.data) { hle.push_back({id,0,e.data}); continue; }
        if (e.code) { hle.push_back({id,0,0,false,std::vector<uint8_t>(e.code,e.code+e.code_size)}); continue; }
        const uint32_t token = static_cast<uint32_t>(e.service) | (e.posix ? maxps4::posix_token : 0);
        hle.push_back({id,0,token,e.noreturn || e.service == maxps4::PS4KernelService::thread_exit});
    }
    return 0;
}
}
extern "C" int32_t maxps4_fex_engine_prepare_orbis_general(maxps4_fex_engine* engine,
    const maxps4_orbis_module* modules, size_t count, const char* root,
    uint64_t* relocation, char* diagnostic, size_t capacity) {
    if (diagnostic && capacity) diagnostic[0] = 0;
    if (relocation) *relocation = 0;
    if (!engine) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    engine->libkernel.reset(); engine->dynamic_ps4.reset(); engine->linker.reset();
    engine->staged_ps4.clear(); engine->orbis_admitted = false; engine->frame = {}; engine->frame_sequence = 0; engine->frame_code_cached = false;
    auto message = [&](const std::string& text) {
        if (diagnostic && capacity) std::snprintf(diagnostic,capacity,"%s",text.c_str());
    };
    if (!modules || !count || count > 128 || !root || !relocation || !diagnostic || capacity < 2)
        return -EINVAL;
    if (::strnlen(root,256)>255) return -E2BIG;
    try {
        std::vector<maxps4::OrbisLinker::Input> inputs;
        std::vector<maxps4::OrbisLinker::HLE> hle;
        std::vector<std::vector<uint8_t>> plain;
        std::string why;
        const int collected = orbisInputs(modules,count,true,plain,why,inputs,hle);
        if (collected) { message(why); return collected; }
        std::string reason;
        uint64_t moved = 0;
        int status = engine->staged_ps4.prepare_orbis_general(inputs,hle,root,reason,moved);
        if (!status) {
            // HLE data objects (stack guard, __progname, errno cells) and the
            // process parameters libc asks libkernel for.
            size_t exported = 0;
            const auto* kernel_exports = maxps4::libkernel_exports(exported);
            for (const auto& object : engine->staged_ps4.data_objects())
                for (size_t i = 0; i < exported; ++i)
                    if (kernel_exports[i].data && object.nid == kernel_exports[i].nid &&
                        !engine->libkernel.initialize_data(kernel_exports[i].service, object.address, object.size)) {
                        reason = "HLE data object " + object.nid + " could not be initialized";
                        status = -EFAULT;
                    }
            engine->libkernel.set_process_param(engine->staged_ps4.process_param());
            if (status) engine->staged_ps4.clear();
        }
        message(status ? reason : "SCE image linked and identity-mapped; execution not attempted");
        engine->orbis_admitted = status == 0;
        if (!status) *relocation = moved;
        return status;
    } catch (const std::bad_alloc&) { message("Orbis allocation failed"); return -ENOMEM; }
}
extern "C" int32_t maxps4_fex_engine_prepare_orbis(maxps4_fex_engine* engine,
    const maxps4_orbis_module* modules, size_t count, const char* root,
    uint32_t budget, char* diagnostic, size_t capacity) {
    if (diagnostic && capacity) diagnostic[0] = 0;
    if (!engine) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    engine->libkernel.reset(); engine->dynamic_ps4.reset(); engine->linker.reset();
    engine->staged_ps4.clear(); engine->orbis_admitted = false; engine->frame = {}; engine->frame_sequence = 0; engine->frame_code_cached = false;
    auto message = [&](const std::string& text) {
        if (diagnostic && capacity) std::snprintf(diagnostic,capacity,"%s",text.c_str());
    };
    if (!modules || !count || count > 128 || !root || !diagnostic || capacity < 2)
        return -EINVAL;
    if (::strnlen(root,256)>255) return -E2BIG;
    try {
        std::vector<maxps4::OrbisLinker::Input> inputs;
        std::vector<maxps4::OrbisLinker::HLE> hle;
        std::vector<std::vector<uint8_t>> plain;
        std::string why;
        const int collected = orbisInputs(modules,count,false,plain,why,inputs,hle);
        if (collected) { message(why); return collected; }
        maxps4::OrbisLinker::Image linked;
        std::string reason;
        if (!maxps4::OrbisLinker::link(inputs,hle,linked,reason)) {
            message(reason); return -ENOEXEC;
        }
        const int status = engine->staged_ps4.prepare_orbis(linked,root,budget,reason);
        message(status ? reason : "SCE image linked, mapped and CPU-admitted; execution not attempted");
        engine->orbis_admitted = status == 0;
        return status;
    } catch (const std::bad_alloc&) { message("Orbis allocation failed"); return -ENOMEM; }
}
extern "C" int32_t maxps4_fex_engine_configure_frame(maxps4_fex_engine* engine,
    uint64_t pixels, uint64_t input, uint32_t width, uint32_t height) {
    if (!engine) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    engine->frame = {}; engine->frame_sequence = 0; engine->frame_code_cached = false;
    if (!engine->orbis_admitted) return -EFAULT;
    const maxps4::GuestFrame candidate{pixels,input,width,height};
    const int status = candidate.validate(engine->staged_ps4);
    if (!status) engine->frame = candidate;
    return status;
}
extern "C" int32_t maxps4_fex_engine_step_frame(maxps4_fex_engine* engine,
    uint32_t buttons, uint8_t* pixels, size_t capacity, uint64_t* sequence, uint32_t* backend) {
    if (sequence) *sequence = 0;
    if (backend) *backend = 0;
    if (!engine || !pixels || !sequence || !backend) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    if (!engine->orbis_admitted) return -EFAULT;
    // Validate every output before dispatch: failures never publish partial frames.
    const int valid = engine->frame.validate(engine->staged_ps4);
    if (valid) return valid;
    if (capacity != engine->frame.bytes() || (buttons & ~31u)) return -EINVAL;
    if (engine->stage != MAXPS4_FEX_DISPATCHER_INITIALIZED) return -EAGAIN;
    try {
        const int input_status = engine->frame.setInput(engine->staged_ps4,buttons);
        if (input_status) return input_status;
        uint64_t result = 0; uint32_t cpu = 0;
        const int status = executeImage(engine,engine->staged_ps4,&result,&cpu,nullptr,false,true);
        if (status) { engine->orbis_admitted = false; engine->frame = {}; return status; }
        const int copied = engine->frame.snapshot(engine->staged_ps4,pixels,capacity);
        if (copied) return copied;
        *sequence = ++engine->frame_sequence; *backend = cpu;
        return 0;
    } catch (const std::bad_alloc&) {
        engine->orbis_admitted = false; engine->frame = {}; return -ENOMEM;
    }
}
extern "C" int32_t maxps4_fex_engine_v4_preflight(maxps4_fex_engine* engine,
    uint64_t entry, uint64_t stack, size_t stack_bytes) {
    if (!engine) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    if (engine->stage != MAXPS4_FEX_DISPATCHER_INITIALIZED) return -EAGAIN;
    const uint8_t* executable = nullptr;
    const uint8_t* writable_stack = nullptr;
    return engine->v4_execution_gate.preflight(entry,stack,stack_bytes,
                                               executable,writable_stack);
}
extern "C" int32_t maxps4_fex_engine_run_orbis(maxps4_fex_engine* engine,
    uint64_t* result, uint32_t* backend, uint64_t* entry) {
    if (result) *result = 0;
    if (backend) *backend = 0;
    if (entry) *entry = 0;
    if (!engine || !result || !backend || !entry) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    if (!engine->orbis_admitted) return -EFAULT;
    try {
        const int status = executeImage(engine,engine->staged_ps4,result,backend,nullptr);
        if (!status) *entry = engine->staged_ps4.entry();
        return status;
    } catch (const std::bad_alloc&) { return -ENOMEM; }
}
extern "C" int32_t maxps4_fex_test_ps4_libkernel(maxps4_ps4_elf_test_report* report) {
    if (!report) return -EINVAL;
    *report = {};
    report->open_status = report->load_status = report->link_status =
        report->initialize_status = report->execute_status = report->read_status =
        report->verification_status = -ECANCELED;
    report->jit_regions_before = maxps4_jit_leases();
    maxps4_fex_engine* engine = nullptr;
    // Scope owns the engine even when fixture construction throws. The public
    // execution APIs remain the sole path to CPU dispatch and kernel services.
    int32_t status = [&]() -> int32_t {
        auto fail = [&](int32_t code, const char* operation, const char* detail = "") {
            std::snprintf(report->error, sizeof(report->error), "%s: %d %s", operation, code, detail);
            return code;
        };
        try {
            const auto file = maxps4_ps4_synthetic::minimal();
            report->open_status = maxps4_fex_engine_open(&engine);
            if (report->open_status) return fail(report->open_status, "open FEX");
            uint32_t blockers = 0;
            report->load_status = maxps4_fex_engine_stage_ps4(engine, file.data(), file.size(),
                &report->entry, &report->segments, &blockers);
            if (report->load_status) return fail(report->load_status, "load Orbis ELF");
            uint64_t linked_entry = 0;
            report->link_status = maxps4_fex_engine_link_ps4(engine, file.data(), file.size(),
                10, &linked_entry, &report->imports);
            {
                std::lock_guard lock(runtime_lock);
                if (report->link_status)
                    return fail(report->link_status, "link NID/RELA", engine->linker.diagnostic().c_str());
                if (engine->linker.imports().size() == 1)
                    report->resolved_service = static_cast<uint32_t>(engine->linker.imports()[0].service);
            }
            const auto self = maxps4_ps4_resolve_libkernel("aI+OeCz8xrQ", "libkernel", "libkernel", 1);
            if (!self || report->resolved_service != self || linked_entry != report->entry)
                return fail(-ENOENT, "resolve scePthreadSelf NID");
            report->initialize_status = maxps4_fex_engine_initialize(engine);
            if (report->initialize_status) return fail(report->initialize_status, "initialize FEX ARM64 JIT");
            uint64_t executed_entry = 0;
            report->execute_status = maxps4_fex_engine_run_linked_ps4(engine,
                &report->rax, &report->backend, &executed_entry, &report->calls);
            if (report->execute_status) return fail(report->execute_status, "execute linked ELF");
            uint8_t output[16]{};
            report->read_status = maxps4_fex_engine_read_ps4(engine,
                maxps4_ps4_synthetic::data + 0x100, output, sizeof(output));
            if (report->read_status) return fail(report->read_status, "read guest memory");
            for (size_t i = 0; i < 8; ++i) {
                report->final_memory |= uint64_t(output[i]) << (8 * i);
                report->kernel_return |= uint64_t(output[8 + i]) << (8 * i);
            }
            // Both stores follow CALL/RET. The second value is observed before
            // ADD, the first after ADD. Neither is assigned by this harness.
            report->resumed = report->calls == 1 && report->kernel_return == 1 &&
                report->final_memory == 2 && report->rax == 2 && executed_entry == report->entry;
            report->verification_status = report->resumed &&
                (report->backend == MAXPS4_CPU_FEX_ARM64_JIT ||
                 report->backend == MAXPS4_CPU_FEX_ARM64_SIMULATOR) ? 0 : -EIO;
            if (report->verification_status) return fail(report->verification_status, "verify call/return/stores");
            return 0;
        } catch (const std::bad_alloc&) { return fail(-ENOMEM, "allocate ELF/FEX"); }
    }();
    if (engine) {
        {
            std::lock_guard lock(runtime_lock);
            engine->libkernel.reset();
            engine->dynamic_ps4.reset();
            engine->linker.reset();
            engine->staged_ps4.clear();
            engine->orbis_admitted = false; engine->frame = {}; engine->frame_sequence = 0; engine->frame_code_cached = false;
            report->guest_segments_after = static_cast<uint32_t>(engine->staged_ps4.segments().size());
        }
        maxps4_fex_engine_close(engine);
    }
    report->jit_regions_after = maxps4_jit_leases();
    if (!status && (report->guest_segments_after || report->jit_regions_after != report->jit_regions_before)) {
        status = -EBUSY;
        std::snprintf(report->error, sizeof(report->error), "cleanup: guest segments=%u, RX/RW leases=%u (before=%u)",
            report->guest_segments_after, report->jit_regions_after, report->jit_regions_before);
    }
    return status;
}
extern "C" int32_t maxps4_fex_engine_run_staged_ps4(maxps4_fex_engine* engine,
    uint32_t budget, uint64_t* result, uint32_t* backend, uint64_t* entry) {
    if (result) *result = 0;
    if (backend) *backend = 0;
    if (entry) *entry = 0;
    if (!engine || !result || !backend || !entry) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    try {
        const int admitted = engine->staged_ps4.admit_staged_cpu(budget);
        if (admitted) return admitted;
        const int status = executeImage(engine, engine->staged_ps4, result, backend, nullptr);
        if (!status) *entry = engine->staged_ps4.entry();
        return status;
    } catch (const std::bad_alloc&) { return -ENOMEM; }
}
extern "C" int64_t maxps4_fex_engine_mount_app0(maxps4_fex_engine* engine, const char* directory) {
    if (!engine) return maxps4::ps4::kernel_error(maxps4::ps4::inval);
    std::lock_guard lock(runtime_lock);
    if (engine->staged_ps4.segments().empty()) return maxps4::ps4::kernel_error(maxps4::ps4::inval);
    return engine->libkernel.mount_app0(directory);
}
extern "C" int64_t maxps4_fex_engine_mount(maxps4_fex_engine* engine, const char* guest, const char* directory) {
    if (!engine) return maxps4::ps4::kernel_error(maxps4::ps4::inval);
    std::lock_guard lock(runtime_lock);
    if (engine->staged_ps4.segments().empty()) return maxps4::ps4::kernel_error(maxps4::ps4::inval);
    return engine->libkernel.mount(guest, directory);
}
extern "C" int64_t maxps4_fex_engine_call_libkernel(maxps4_fex_engine* engine, uint32_t service,
    const maxps4_ps4_kernel_call* call, uint64_t guest_thread) {
    if (!engine || !call) return maxps4::ps4::kernel_error(maxps4::ps4::inval);
    std::lock_guard lock(runtime_lock);
    if (engine->staged_ps4.segments().empty()) return maxps4::ps4::kernel_error(maxps4::ps4::inval);
    const maxps4::PS4KernelCall registers{call->rdi, call->rsi, call->rdx, call->rcx,
        call->r8, call->r9, call->rsp};
    return engine->libkernel.invoke(static_cast<maxps4::PS4KernelService>(service), registers, guest_thread);
}
extern "C" int32_t maxps4_fex_engine_read_ps4(maxps4_fex_engine* engine,
    uint64_t address, uint8_t* output, size_t length) {
    if (!engine || !output || !length || length > 1024 * 1024) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    auto& memory = engine->system_memory;
    return memory.read(address, output, length) ? 0 : -EFAULT;
}
extern "C" int32_t maxps4_fex_engine_fetch_ps4(maxps4_fex_engine* engine,
    uint64_t address, uint8_t* output, size_t length) {
    if (!engine || !output || !length || length > 1024 * 1024) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    auto& memory = engine->system_memory;
    return memory.fetch(address, output, length) ? 0 : -EFAULT;
}
extern "C" int32_t maxps4_fex_engine_write_ps4(maxps4_fex_engine* engine,
    uint64_t address, const uint8_t* input, size_t length) {
    if (!engine || !input || !length || length > 1024 * 1024) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    auto& memory = engine->system_memory;
    return memory.write(address, input, length) ? 0 : -EFAULT;
}
extern "C" int32_t maxps4_fex_engine_query_ps4(maxps4_fex_engine* engine,
    uint64_t address, size_t length, uint32_t permissions, uint32_t* permitted) {
    if (permitted) *permitted = 0;
    if (!engine || !permitted || !length || length > 1024 * 1024 ||
        !permissions || (permissions & ~7U)) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    auto& memory = engine->system_memory;
    *permitted = memory.permitted(address, length, permissions) ? 1U : 0U;
    return 0;
}
extern "C" int32_t maxps4_fex_engine_contiguous_ps4(maxps4_fex_engine* engine,
    uint64_t address, size_t limit, uint32_t permissions, size_t* available) {
    if (available) *available = 0;
    if (!engine || !available || !limit || limit > 1024 * 1024 ||
        !permissions || (permissions & ~7U)) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    auto& memory = engine->system_memory;
    *available = memory.contiguous(address, limit, permissions);
    return 0;
}
extern "C" int32_t maxps4_fex_engine_run_elf(maxps4_fex_engine* engine,
    const uint8_t* file, size_t length, uint32_t budget, uint64_t* result,
    uint32_t* backend, uint64_t* entry) {
    return runELF(engine, file, length, budget, result, backend, entry, nullptr);
}
extern "C" int32_t maxps4_fex_engine_run_elf_pm4(maxps4_fex_engine* engine,
    const uint8_t* file, size_t length, uint32_t budget, uint64_t address, size_t count,
    uint32_t* words, size_t capacity, size_t* written, uint64_t* result,
    uint32_t* backend, uint64_t* entry) {
    if (written) *written = 0;
    if (result) *result = 0;
    if (backend) *backend = 0;
    if (entry) *entry = 0;
    if (!written || !words || !count || count > maxps4::gpu::kMaxSubmissionWords || capacity < count)
        return -EINVAL;
    const PM4Request request {address, count, words, written};
    return runELF(engine, file, length, budget, result, backend, entry, &request);
}


extern "C" int32_t maxps4_fex_engine_interpret_elf(maxps4_fex_engine* engine,
    const uint8_t* file, size_t length, uint32_t budget, uint64_t* result,
    uint32_t* backend, uint64_t* entry, uint64_t* rip, uint32_t* instructions) {
    if (result) *result = 0;
    if (backend) *backend = 0;
    if (entry) *entry = 0;
    if (rip) *rip = 0;
    if (instructions) *instructions = 0;
    if (!engine || !result || !backend || !entry || !rip || !instructions) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    try {
        maxps4::GuestELF image;
        const int loaded = image.load(file, length, budget);
        if (loaded) return loaded;
        *entry = image.entry();
        maxps4::GuestELF::Execution execution;
        const int status = image.interpret(budget, execution);
        *result = execution.rax;
        *rip = execution.rip;
        *instructions = execution.instructions;
        *backend = MAXPS4_CPU_RESTRICTED_INTERPRETER;
        return status;
    } catch (const std::bad_alloc&) { return -ENOMEM; }
}


