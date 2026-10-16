#include "maxps4_guest_frame.h"
#include "maxps4_guest_dynamic_tls.h"
#include "maxps4_orbis_m1.h"
#include "maxps4_fex_runtime.h"
#include "maxps4_guest_stack.h"
#include "maxps4_orbis_v4_execution_gate.h"
#include "maxps4_orbis_v4_synthetic_stage.h"
#include "OrbisV4PRXLaunch.hpp"
#include <algorithm>
#include <iterator>
#include "maxps4_jit_memory.h"
#include "maxps4_guest_elf.h"
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
#include <memory>
#include <new>
#include <stdexcept>

extern "C" int maxps4_native_guest_x86_run(const uint8_t*, size_t, uint32_t,
                                           uint64_t*) noexcept;
namespace {
// Anonymous immutable guest memory, not a PS4 or Linux syscall ABI.
// A syscall has no permitted host effect and returns the real ENOSYS error.
class ProbeAddressSpace final : public FEXCore::HLE::SyscallHandler {
public:
    ProbeAddressSpace() { OSABI = FEXCore::HLE::SyscallOSABI::OS_GENERIC; }
    maxps4::GuestELF* image = nullptr;
    maxps4::OrbisM1* m1 = nullptr;
    maxps4::PS4DynamicLinker* linker = nullptr;
    maxps4::PS4Libkernel* kernel = nullptr;
    maxps4::PS4GuestMemory* memory = nullptr;
    uint64_t private_stack = 0;
    uint64_t guest_id = 1;
    size_t private_stack_size = 0;
    // Private M1 storage is pinned by runtime_lock and the process lifetime.
    // No mapping-mutating libkernel VM provider is exposed during dispatch.
    std::vector<FEXCore::HLE::GuestMemoryRange> ranges;
    bool v4_translation = false;
    bool UsesGuestMemoryTranslation() const override { return v4_translation || (m1 && !m1->space.fex_direct()); }
    std::span<const FEXCore::HLE::GuestMemoryRange> GuestMemoryRanges() const override { return ranges; }
    uint32_t calls = 0;
    int callback_error = 0;
    uint64_t HandleSyscall(FEXCore::Core::CpuStateFrame* frame, FEXCore::HLE::SyscallArguments*) override {
        namespace x86 = FEXCore::X86State;
        auto& state = frame->State;
        auto& r = state.gregs;
        maxps4::PS4KernelService service{};
        const uint64_t rsp = r[x86::REG_RSP];
        int64_t result = maxps4::ps4::kernel_error(maxps4::ps4::nosys);
        if (!image || !linker || !kernel || !memory || r[x86::REG_RAX] > UINT32_MAX ||
            !linker->gate(*image,m1 ? reinterpret_cast<uintptr_t>(image->translate(state.rip,2,5)) : state.rip,
                static_cast<uint32_t>(r[x86::REG_RAX]),service)) {
            callback_error = -EPERM;
        } else if (rsp < private_stack || private_stack_size < 16 ||
                   rsp - private_stack > private_stack_size - 16 || (rsp & 7)) {
            callback_error = -EFAULT;
            result = maxps4::ps4::kernel_error(maxps4::ps4::fault);
        } else if (m1) {
            const maxps4::PS4KernelCall call{r[x86::REG_RDI],r[x86::REG_RSI],r[x86::REG_RDX],
                r[x86::REG_R10],r[x86::REG_R8],r[x86::REG_R9],rsp};
            result = kernel->invoke(service,call,guest_id);
            ++calls;
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
                const maxps4::PS4KernelCall call{r[x86::REG_RDI],r[x86::REG_RSI],r[x86::REG_RDX],
                    service == maxps4::PS4KernelService::mmap ? r[x86::REG_R10] : 0,
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
        if (UsesGuestMemoryTranslation()) {
            for (const auto& r : ranges)
                if ((r.Permissions & 5) == 5 && address >= r.Guest && address-r.Guest < r.Size)
                    return {r.Guest,r.Size,false};
            return {0,0,false};
        }
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
            const auto begin = m1 ? segment.address : reinterpret_cast<uintptr_t>(pointer);
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
    Mapping(size_t bytes, int protection, void* hint = nullptr) : size(bytes), address(
        ::mmap(hint, bytes, protection, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) {}
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
    uint64_t* result, uint32_t* backend, const PM4Request* submission, bool linked = false, bool persistent_frame = false,
    maxps4::OrbisM1* m1 = nullptr, const maxps4::GuestThreadMemory* guest_thread = nullptr,
    FEXCore::Core::InternalThreadState* scheduled = nullptr, uint64_t guest_id = 1,
    maxps4::PS4Libkernel* shared_kernel = nullptr, bool* scheduled_consumed = nullptr) {
    if (scheduled_consumed) *scheduled_consumed = false;
    if (scheduled && !scheduled_consumed) return -EINVAL;
    engine->cpu_report = {};
    if (engine->stage != MAXPS4_FEX_DISPATCHER_INITIALIZED) return -EAGAIN;
    const bool translated = m1 && !m1->space.fex_direct();
    // Translated M1 requires a logical stack and the immutable snapshot below.
    // Arbitrary ELF/PKG dispatch still passes its existing admission gate.
    if (translated && !guest_thread) return -ENOTSUP;
    // Check the logical PS4 entry via the shared guest-memory contract before
    // giving FEX a host pointer. This is not general address-space translation.
    maxps4::PS4GuestMemory guest_memory(image);
    if (guest_memory.contiguous(image.entry(), 1, 5) != 1) return -EFAULT;
    // Fetch a bounded instruction window through the same permission-checked
    // guest contract used by CPU clients. Never cross the executable segment.
    // The decoder still enforces the admitted instruction whitelist.
    std::array<uint8_t, 15> entry_bytes {};
    const size_t fetch_length = guest_memory.contiguous(image.entry(), entry_bytes.size(), 5);
    if (!fetch_length || !guest_memory.fetch(image.entry(), entry_bytes.data(), fetch_length))
        return -EFAULT;
    const auto* entry_pointer = m1 ? m1->space.translate(image.entry(),1,5) : image.entry_pointer();
    if (!entry_pointer) return -EFAULT;
    const long host_page = ::sysconf(_SC_PAGESIZE);
    if (host_page <= 0) return -EIO;
    const size_t page = static_cast<size_t>(host_page);
    Mapping stack(3 * page, PROT_NONE);
    if (stack.address == MAP_FAILED) return -errno;
    constexpr size_t callret_size = FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE;
    Mapping callret(callret_size + 2 * page, PROT_NONE);
    if (callret.address == MAP_FAILED) return -errno;
    auto* stack_base = static_cast<uint8_t*>(stack.address) + page;
    auto* callret_base = static_cast<uint8_t*>(callret.address) + page;
    if (::mprotect(stack_base, page, PROT_READ | PROT_WRITE) != 0 ||
        ::mprotect(callret_base, callret_size, PROT_READ | PROT_WRITE) != 0) return -errno;
    std::array<FEXCore::Core::CPUState::gdt_segment, 32> gdt {};
    maxps4::GuestELF empty_image;
    maxps4::PS4GuestMemory m1_memory(empty_image,m1 ? &m1->space : nullptr);
    maxps4::PS4Libkernel m1_kernel(m1_memory);
    engine->probe.ranges.clear();
    if (translated) for (const auto& r : m1->space.regions()) {
        if (!r.permissions) continue;
        const auto* host = m1->space.translate(r.address,r.size,r.permissions);
        if (!host) return -EFAULT;
        engine->probe.ranges.push_back({r.address,reinterpret_cast<uintptr_t>(host),r.size,r.permissions});
    }
    engine->probe.image = &image;
    engine->probe.m1 = m1;
    engine->probe.linker = m1 ? &m1->linker : linked ? &engine->linker : nullptr;
    engine->probe.kernel = m1 ? (shared_kernel ? shared_kernel : &m1_kernel) : linked ? &engine->libkernel : nullptr;
    engine->probe.guest_id = guest_id;
    engine->probe.memory = m1 ? &m1_memory : linked ? &engine->system_memory : nullptr;
    engine->probe.private_stack = guest_thread ? guest_thread->stack_begin : reinterpret_cast<uintptr_t>(stack_base);
    engine->probe.private_stack_size = guest_thread ? guest_thread->stack_size : page;
    engine->probe.calls = 0;
    engine->probe.callback_error = 0;
    struct ClearRange {
        ProbeAddressSpace& space;
        ~ClearRange() {
            space.ranges.clear();
            space.m1 = nullptr; space.image = nullptr; space.linker = nullptr; space.kernel = nullptr;
            space.memory = nullptr; space.private_stack = 0; space.private_stack_size = 0;
            space.guest_id = 1;
        }
    } clear_range {engine->probe};
    uint64_t initial_rsp = 0;
    const uint64_t initial_rip = m1 ? image.entry() : reinterpret_cast<uintptr_t>(entry_pointer);
    const int stack_status = maxps4::initialize_guest_stack(stack_base, page, initial_rip, page, initial_rsp);
    if (stack_status) return stack_status;
    if (guest_thread) initial_rsp = guest_thread->rsp;
    auto* thread = scheduled ? scheduled : engine->context->CreateThread(initial_rip, initial_rsp);
    if (!thread) return -ENOMEM;
    struct DestroyThread {
        FEXCore::Context::Context* context;
        FEXCore::Core::InternalThreadState* thread;
        bool translated;
        ~DestroyThread() {
            if (!thread) return;
            // Discard embedded host pointers BEFORE mappings/permissions change.
            // Other scheduled contexts have not dispatched yet; no stale cache
            // survives this invocation in the shared L3 or the current thread.
            if (translated) context->ClearCodeCache(thread,false);
            context->DestroyThread(thread);
        }
    } destroy {engine->context.get(), thread, translated};
    if (scheduled_consumed) *scheduled_consumed = true;
    auto& state = thread->CurrentFrame->State;
    // Set the complete integer register entry contract explicitly on every run.
    // Preserve FEX-owned flags, segment and dispatcher frame initialization.
    std::fill(std::begin(state.gregs), std::end(state.gregs), uint64_t{});
    state.gregs[FEXCore::X86State::REG_RSP] = initial_rsp;
    state.rip = initial_rip;
    if (guest_thread) state.fs_cached = guest_thread->fs_base;
    auto& observed = engine->cpu_report;
    observed.entry = image.entry();
    observed.rsp_alignment = state.gregs[FEXCore::X86State::REG_RSP] & 15;
    observed.entry_matches = state.rip == initial_rip;
    observed.zero_integer_registers = 1;
    for (size_t i = 0; i < std::size(state.gregs); ++i)
        if (i != FEXCore::X86State::REG_RSP && state.gregs[i]) observed.zero_integer_registers = 0;
    uint64_t stack_words[9]{};
    if (m1) {
        if (m1->space.read(initial_rsp,stack_words,sizeof(stack_words))) return -EFAULT;
    } else std::memcpy(stack_words, reinterpret_cast<const void*>(initial_rsp), sizeof(stack_words));
    observed.argc = stack_words[0];
    observed.argv_env_null = stack_words[1] == 0 && stack_words[2] == 0;
    observed.page_size = stack_words[4];
    observed.auxv_valid = stack_words[3] == 6 && stack_words[5] == 9 &&
        stack_words[6] == state.rip && stack_words[7] == 0 && stack_words[8] == 0;
    thread->CallRetStackBase = callret_base;
    state.callret_sp = reinterpret_cast<uintptr_t>(callret_base) + callret_size / 4;
    state.segment_arrays[FEXCore::Core::CPUState::SEGMENT_ARRAY_INDEX_GDT] = gdt.data();
    state.segment_arrays[FEXCore::Core::CPUState::SEGMENT_ARRAY_INDEX_LDT] = gdt.data();
    state.cs_idx = FEXCore::Core::CPUState::DEFAULT_USER_CS << 3;
    auto* segment = FEXCore::Core::CPUState::GetSegmentFromIndex(state, state.cs_idx);
    FEXCore::Core::CPUState::SetGDTBase(segment, 0);
    FEXCore::Core::CPUState::SetGDTLimit(segment, 0xF'FFFFU);
    segment->L = 1;
    segment->D = 0;
    state.cs_cached = FEXCore::Core::CPUState::CalculateGDTBase(*segment);
    // mmap can reuse a previous image's address. The context retains a shared
    // L3 guest-to-host cache across threads: discard those old translations
    // without allocating another iOS JIT buffer. All engine execution is locked.
    if (!persistent_frame || !engine->frame_code_cached) engine->context->ClearCodeCache(thread, false);
    engine->frame_code_cached = persistent_frame;
    FEXCore::Core::GuestMemoryFaultData fault {};
    state.GuestMemoryFaultRecord = &fault;
    struct ClearFaultRecord {
        FEXCore::Core::CPUState& state;
        ~ClearFaultRecord() { state.GuestMemoryFaultRecord = nullptr; }
    } clear_fault_record {state};
    try {
        // Reject unsupported entry-block IR on the C++ stack, before entering
        // native dispatcher frames that cannot unwind C++ exceptions safely.
        if (translated) engine->context->CompileRIP(thread,initial_rip);
        engine->context->ExecuteThread(thread);
    } catch (const std::runtime_error& error) {
        std::fprintf(stderr,"FEX translated compilation rejected: %.160s\n",error.what());
        return -ENOTSUP;
    }
    if (fault.Bytes) {
        std::fprintf(stderr,"M2 checked operand rejected: guest=0x%llx bytes=%u permissions=%u; dispatch stopped\n",
            static_cast<unsigned long long>(fault.Address),fault.Bytes,fault.Permissions);
        return -EFAULT;
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
    engine->staged_ps4.clear(); engine->cpu_report = {};
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
    engine->staged_ps4.clear(); engine->cpu_report = {};
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
extern "C" int32_t maxps4_fex_engine_prepare_orbis(maxps4_fex_engine* engine,
    const maxps4_orbis_module* modules, size_t count, const char* root,
    uint32_t budget, char* diagnostic, size_t capacity) {
    if (diagnostic && capacity) diagnostic[0] = 0;
    if (!engine) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    engine->libkernel.reset(); engine->dynamic_ps4.reset(); engine->linker.reset();
    engine->staged_ps4.clear(); engine->cpu_report = {}; engine->orbis_admitted = false; engine->frame = {}; engine->frame_sequence = 0; engine->frame_code_cached = false;
    auto message = [&](const std::string& text) {
        if (diagnostic && capacity) std::snprintf(diagnostic,capacity,"%s",text.c_str());
    };
    if (!modules || !count || count > 128 || !root || !diagnostic || capacity < 2)
        return -EINVAL;
    if (::strnlen(root,256)>255) return -E2BIG;
    try {
        std::vector<maxps4::OrbisLinker::Input> inputs;
        for (size_t i=0;i<count;++i) {
            const auto& m = modules[i];
            if (!m.path || !m.bytes || m.dependency_count > 128 ||
                (m.dependency_count && !m.dependencies)) return -EINVAL;
            if (::strnlen(m.path,256)>255 || m.size>32*1024*1024) return -E2BIG;
            maxps4::OrbisLinker::Input in{{{m.path,{}},m.bytes,m.size},m.load_bias};
            for (size_t j=0;j<m.dependency_count;++j) {
                if (!m.dependencies[j] || ::strnlen(m.dependencies[j],256)>255) return -EINVAL;
                in.file.request.dependencies.emplace_back(m.dependencies[j]);
            }
            inputs.push_back(std::move(in));
        }
        maxps4::OrbisLinker::Image linked;
        std::string reason;
        if (!maxps4::OrbisLinker::link(inputs,{},linked,reason)) {
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
extern "C" int32_t maxps4_fex_engine_v4_synthetic(maxps4_fex_engine* engine,
    uint32_t instruction_budget, maxps4_v4_synthetic_report* report) {
    if (!report) return -EINVAL;
    *report = {};
    auto fail = [&](int status, uint32_t reason) {
        report->status = status; report->stop_reason = reason; return status;
    };
    if (!engine || instruction_budget < 2 || instruction_budget > 16)
        return fail(-EINVAL,MAXPS4_V4_INVALID_REQUEST);
    std::lock_guard lock(runtime_lock);
    if (engine->stage != MAXPS4_FEX_DISPATCHER_INITIALIZED)
        return fail(-EAGAIN,MAXPS4_V4_NOT_INITIALIZED);
    // Local ownership guarantees cleanup on every return and sequential retry.
    // Do not mutate stable V3 image, probe callbacks or FEX code caches.
    maxps4::GuestAddressSpace memory;
    const size_t page = memory.page_size();
    constexpr uint64_t entry = 0x300000000ULL;
    constexpr uint8_t code[] = {0xb8,42,0,0,0,0xf4};
    try {
        const int staged = maxps4::stage_v4_synthetic_image(memory,entry,code,
            sizeof(code),entry+page,page);
        if (staged) return fail(staged,MAXPS4_V4_STAGING_FAILED);
        report->code_rx = memory.translate(entry,sizeof(code),5) &&
            !memory.translate(entry,1,2);
        report->stack_rw_nx = memory.translate(entry+page,page,6) &&
            !memory.translate(entry+page,1,1);
        maxps4::OrbisV4ExecutionGate gate(memory);
        const uint8_t *executable = nullptr, *stack = nullptr;
        const int admitted = gate.preflight(entry,entry+page,page,executable,stack);
        // No safe translated-memory FEX dispatcher exists in the pinned core.
        // Even an unexpected admission must never be reported as CPU success.
        return fail(admitted ? admitted : -ENOTSUP,MAXPS4_V4_TRANSLATION_REQUIRED);
    } catch (const std::bad_alloc&) {
        return fail(-ENOMEM,MAXPS4_V4_STAGING_FAILED);
    }
}
extern "C" int32_t maxps4_fex_test_m2_low_va(maxps4_m2_low_va_report* report) {
    if (!report) return -EINVAL;
    *report = {};
    report->boundary_status = report->initialize_status = report->execute_status = -ECANCELED;
    report->scalar_status = report->scalar_execute_status = report->scalar_verify_status = -ECANCELED;
    report->scalar_rejection_status = -ECANCELED;
    report->scalar_expected = 11;
    maxps4_fex_engine* engine = nullptr;
    int status = maxps4_fex_engine_open(&engine);
    if (status) return report->status = status;
    report->initialize_status = maxps4_fex_engine_initialize(engine);
    status = report->initialize_status;
    if (!status) {
        std::lock_guard lock(runtime_lock);
        try {
            status = [&]() -> int {
                maxps4::OrbisM1 process(maxps4::GuestAddressSpace::Backend::translated);
                const size_t page = process.space.page_size();
                if (!page) return -ENOTSUP;
                // A real occupied host mapping is also used as a guest logical
                // operand. Translation must leave this host marker untouched.
                // Linux ARM64 may allocate anonymous storage above 47 bits.
                // A non-fixed hint keeps the collision operand in the PS4 VA
                // domain without overwriting any occupied host reservation.
                Mapping occupied(3*page,PROT_READ|PROT_WRITE,reinterpret_cast<void*>(0x800000000ULL));
                if (occupied.address == MAP_FAILED) return -errno;
                auto* marker = static_cast<uint8_t*>(occupied.address)+page;
                const uint64_t sentinel = 0x13579bdf2468ace0ULL;
                std::memcpy(marker,&sentinel,8);
                const uint64_t data = reinterpret_cast<uintptr_t>(marker);
                constexpr uint64_t entry = 0x400000;
                maxps4_ps4_synthetic::Program code;
                code.reg(3,data);
                code.bytes({0xb8,21,0,0,0,0x48,0x89,0x03});
                code.bytes({0xf0,0x48,0x0f,0xc1,0x03,0x48,0x8b,0x03,0x50,0x58});
                // Preserve RAX=42 while loading/updating private static TLS.
                code.bytes({0x64,0x48,0x8b,0x0c,0x25,0xf0,0xff,0xff,0xff});
                code.bytes({0x48,0x83,0xc1,1});
                code.bytes({0x64,0x48,0x89,0x0c,0x25,0xf0,0xff,0xff,0xff,0xf4});
                std::vector<uint8_t> file(256+code.code.size());
                std::memcpy(file.data(),"\177ELF\2\1\1\11",8);
                auto put = [&](uint64_t value,size_t at,size_t width=8) {
                    maxps4_ps4_synthetic::put(file,value,at,width);
                };
                put(0xfe00,16,2); put(62,18,2); put(1,20,4); put(entry,24);
                put(64,32); put(64,52,2); put(56,54,2); put(1,56,2);
                put(1,64,4); put(5,68,4); put(256,72); put(entry,80);
                put(code.code.size(),96); put(page,104); put(1,112);
                std::memcpy(file.data()+256,code.code.data(),code.code.size());
                int error = process.source.stage_ps4(file.data(),file.size());
                if (error) return error;
                if ((error=process.space.reserve(entry,page)) ||
                    (error=process.space.map(entry,page,5,code.code.data(),code.code.size())) ||
                    (error=process.space.reserve(data,page)) ||
                    (error=process.space.map(data,page,6))) return error;
                process.entry = entry;
                process.tls.memory_size = process.tls.block_size = 16;
                process.tls.initial = {7,0,0,0,0,0,0,0};
                maxps4::GuestThreadMemory thread(process.space);
                error = thread.initialize(process.tls,0x800000,entry);
                if (error) return error;
                report->entry = entry; report->rsp = thread.rsp;
                report->code_rx = process.space.translate(entry,1,5) && !process.space.translate(entry,1,2);
                report->stack_rw_nx = process.space.translate(thread.stack_begin,thread.stack_size,6) &&
                    !process.space.translate(thread.stack_begin,1,1);
                // Only the constant fixture above bypasses general ELF admission.
                report->execute_status = executeImage(engine,process.source,&report->rax,&report->backend,
                    nullptr,false,false,&process,&thread);
                if (report->execute_status) return report->execute_status;
                if ((error=process.space.read(data,&report->memory,8)) ||
                    (error=process.space.read(thread.tls_begin,&report->tls,8))) return error;
                uint64_t observed = 0; std::memcpy(&observed,marker,8);
                report->collision_preserved = observed == sentinel;
                if (report->rax != 42 || report->memory != 42 || report->tls != 8 ||
                    !report->collision_preserved || !report->code_rx || !report->stack_rw_nx) return -EIO;
                // Bounded scalar successes and negative fixtures. Each invocation
                // retires its JIT cache before RX or permission mutations.
                // No signal recovery, software atomic fallback or BRK is used.
                report->boundary_status = [&]() -> int {
                    const std::vector<uint8_t> sentinels(page,0x59);
                    std::vector<uint8_t> check(page);
                    const std::vector<std::vector<uint8_t>> operations={
                        {0xf3,0x0f,0x6f,0x03}, {0xf3,0x0f,0x7f,0x03},
                        {0xf0,0x48,0x0f,0xc1,0x03}, {0xf0,0x48,0x0f,0xb1,0x0b}};
                    for (int boundary=0; boundary<4; ++boundary) {
                        if ((error=process.space.release(data)) ||
                            (error=process.space.reserve(data,2*page)) ||
                            (error=process.space.map(data,page,6,sentinels.data(),page))) return error;
                        // 0=hole, 1=RW, 2=R, 3=PROT_NONE.
                        const uint32_t permission=boundary==2 ? 4 : boundary==3 ? 0 : 6;
                        if (boundary && ((error=process.space.map(data+page,page,6,sentinels.data(),page)) ||
                            (error=process.space.protect(data+page,page,permission)))) return error;
                        if (boundary==1) {
                            report->scalar_status = [&]() -> int {
                                for (unsigned width : {2U,4U,8U}) for (unsigned split=1;split<width;++split) {
                                    report->scalar_width=width; report->scalar_split=split;
                                    report->scalar_execute_status=report->scalar_verify_status=-ECANCELED;
                                    if ((error=process.space.write(data,sentinels.data(),page)) ||
                                        (error=process.space.write(data+page,sentinels.data(),page))) return error;
                                    maxps4_ps4_synthetic::Program scalar;
                                    scalar.reg(3,data+page-split);
                                    scalar.bytes({0xb8,42,0,0,0,0x31,0xc9,0x39,0xc9});
                                    if (width==2) scalar.bytes({0x66});
                                    if (width==8) scalar.bytes({0x48});
                                    scalar.bytes({0x89,0x03});
                                    if (width==2) scalar.bytes({0x66});
                                    if (width==8) scalar.bytes({0x48});
                                    scalar.bytes({0x8b,0x03,0x0f,0x95,0xc1,0x0f,0xb6,0xc9,
                                                  0x48,0xc1,0xe1,32,0x48,0x09,0xc8,0xf4});
                                    if ((error=process.space.release(entry)) ||
                                        (error=process.space.reserve(entry,page)) ||
                                        (error=process.space.map(entry,page,5,scalar.code.data(),scalar.code.size()))) return error;
                                    uint64_t result=0; uint32_t backend=0;
                                    ++report->scalar_executed;
                                    report->scalar_execute_status=executeImage(engine,process.source,&result,&backend,
                                            nullptr,false,false,&process,&thread);
                                    if (report->scalar_execute_status) return report->scalar_execute_status;
                                    report->scalar_verify_status=-EIO;
                                    if (result!=42 || backend!=report->backend) return -EIO;
                                    std::vector<uint8_t> expected(2*page,0x59), actual(2*page);
                                    for(unsigned i=0;i<width;++i) expected[page-split+i]=i?0:42;
                                    if ((error=process.space.read(data,actual.data(),actual.size()))) return error;
                                    if (actual!=expected) return -EIO;
                                    report->scalar_verify_status=0;
                                    ++report->scalar_passed;
                                }
                                return report->scalar_executed==report->scalar_expected &&
                                    report->scalar_passed==report->scalar_expected ? 0 : -EIO;
                            }();
                            if (report->scalar_status) return report->scalar_status;
                            if ((error=process.space.write(data,sentinels.data(),page)) ||
                                (error=process.space.write(data+page,sentinels.data(),page))) return error;
                        }
                        // Check every scalar cut against holes and permissions,
                        // including all bytes on both sides after rejected stores.
                        // Read-only second pages permit loads, so reject stores only.
                        if (boundary!=1) {
                            report->scalar_rejection_status = [&]() -> int {
                                for (unsigned width : {2U,4U,8U}) for (unsigned split=1;split<width;++split)
                                for (bool store : {false,true}) {
                                    if (!store && boundary==2) continue;
                                    maxps4_ps4_synthetic::Program negative;
                                    negative.reg(3,data+page-split);
                                    negative.bytes({0xb8,42,0,0,0});
                                    if (width==2) negative.bytes({0x66});
                                    if (width==8) negative.bytes({0x48});
                                    negative.bytes({static_cast<uint8_t>(store ? 0x89 : 0x8b),0x03,0xf4});
                                    if ((error=process.space.release(entry)) ||
                                        (error=process.space.reserve(entry,page)) ||
                                        (error=process.space.map(entry,page,5,negative.code.data(),negative.code.size()))) return error;
                                    uint64_t result=0xfeed; uint32_t backend=0;
                                    const int rejected=executeImage(engine,process.source,&result,&backend,
                                        nullptr,false,false,&process,&thread);
                                    if (rejected!=-EFAULT || result!=0xfeed || backend) return -EIO;
                                    if ((error=process.space.read(data,check.data(),page))) return error;
                                    if (check!=sentinels) return -EIO;
                                    if (boundary) {
                                        if ((error=process.space.protect(data+page,page,4)) ||
                                            (error=process.space.read(data+page,check.data(),page))) return error;
                                        if (check!=sentinels) return -EIO;
                                        if ((error=process.space.protect(data+page,page,permission))) return error;
                                    }
                                    std::memcpy(&observed,marker,8);
                                    if (observed!=sentinel) return -EIO;
                                    ++report->scalar_rejection_cases;
                                }
                                return 0;
                            }();
                            if (report->scalar_rejection_status) return report->scalar_rejection_status;
                        }
                        for (const auto& operation : operations) {
                            maxps4_ps4_synthetic::Program negative;
                            negative.reg(3,data+page-4);
                            negative.bytes({0xb8,21,0,0,0,0xb9,42,0,0,0});
                            negative.code.insert(negative.code.end(),operation.begin(),operation.end());
                            negative.bytes({0xf4});
                            if ((error=process.space.release(entry)) ||
                                (error=process.space.reserve(entry,page)) ||
                                (error=process.space.map(entry,page,5,negative.code.data(),negative.code.size()))) return error;
                            uint64_t ignored=0xfeed;
                            uint32_t ignored_backend=0;
                            const int rejected=executeImage(engine,process.source,&ignored,&ignored_backend,
                                nullptr,false,false,&process,&thread);
                            // Failed dispatch must not publish a CPU result.
                            if (rejected!=-EFAULT || ignored!=0xfeed || ignored_backend) return -EIO;
                            if ((error=process.space.read(data,check.data(),page))) return error;
                            if (check!=sentinels) return -EIO;
                            if (boundary) {
                                if ((error=process.space.protect(data+page,page,4)) ||
                                    (error=process.space.read(data+page,check.data(),page))) return error;
                                if (check!=sentinels) return -EIO;
                                if ((error=process.space.protect(data+page,page,permission))) return error;
                            }
                            std::memcpy(&observed,marker,8);
                            if (observed!=sentinel) return -EIO;
                            ++report->boundary_cases;
                        }
                    }
                    if (report->scalar_rejection_cases!=55) return -EIO;
                    report->scalar_rejection_preserved=1;
                    report->boundary_preserved=1;
                    return 0;
                }();
                return report->boundary_status;
            }();
            // All fixture mappings and CPU caches have been retired before this.
            report->cleanup = engine->probe.m1 == nullptr && engine->probe.ranges.empty();
        } catch (const std::bad_alloc&) { status = -ENOMEM; }
          catch (...) { status = -EIO; }
    }
    maxps4_fex_engine_close(engine);
    report->status = status;
    std::fprintf(stderr,"M2 low-VA diagnostic: status=%d backend=%u entry=0x%llx rax=%llu memory=%llu tls=%llu collision=%u cleanup=%u boundaries=%u boundary_status=%d preserved=%u\n",
        status,report->backend,static_cast<unsigned long long>(report->entry),
        static_cast<unsigned long long>(report->rax),static_cast<unsigned long long>(report->memory),
        static_cast<unsigned long long>(report->tls),report->collision_preserved,report->cleanup,report->boundary_cases,report->boundary_status,report->boundary_preserved);
    std::fprintf(stderr,"M2 scalar cross-mapping: executed=%u passed=%u expected=%u status=%d execute=%d verify=%d width=%u split=%u; scalar refusals=%u/55 status=%d preserved=%u\n",
        report->scalar_executed,report->scalar_passed,report->scalar_expected,report->scalar_status,
        report->scalar_execute_status,report->scalar_verify_status,report->scalar_width,report->scalar_split,
        report->scalar_rejection_cases,report->scalar_rejection_status,report->scalar_rejection_preserved);
    return status;
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
extern "C" int32_t maxps4_fex_engine_run_v4_prx(maxps4_fex_engine* engine,
    const maxps4_v4_prx_request* request, uint64_t* result, uint32_t* backend) {
    if (result) *result=0;
    if (backend) *backend=0;
    if (!engine || !request || !result || !backend) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    if (engine->stage!=MAXPS4_FEX_DISPATCHER_INITIALIZED) return -EAGAIN;
    try {
        Mapping callret(FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE,PROT_READ|PROT_WRITE);
        if (callret.address==MAP_FAILED) return -ENOMEM;
        std::array<FEXCore::Core::CPUState::gdt_segment,32> gdt{};
        FEXCore::Core::InternalThreadState* thread=nullptr;
        // Declared before owner: owner invalidates/unmaps while thread is live;
        // then restore the handler and destroy the thread before local buffers.
        struct Cleanup {
            maxps4_fex_engine* engine;
            FEXCore::Core::InternalThreadState*& thread;
            std::vector<FEXCore::HLE::GuestMemoryRange> old_ranges;
            bool old_mode;
            ~Cleanup() {
                if (thread) {
                    thread->CurrentFrame->State.GuestMemoryFaultRecord=nullptr;
                    engine->context->ClearCodeCache(thread,false);
                    engine->context->DestroyThread(thread);
                }
                engine->probe.ranges=std::move(old_ranges);
                engine->probe.v4_translation=old_mode;
            }
        } cleanup{engine,thread,std::move(engine->probe.ranges),engine->probe.v4_translation};
        engine->probe.ranges.clear();
        std::unique_ptr<maxps4::OrbisV4Translation> owner;
        const int prepared=maxps4::OrbisV4PRXLaunch::prepare(*request,[&] {
            if (thread) engine->context->ClearCodeCache(thread,false);
        },owner);
        if (prepared) return prepared;
        const uint64_t rsp=maxps4::OrbisV4PRXLaunch::stack+owner->page_size()-16;
        thread=engine->context->CreateThread(request->entry,rsp);
        if (!thread) return -ENOMEM;
        thread->CallRetStackBase=static_cast<uint8_t*>(callret.address);
        auto& state=thread->CurrentFrame->State;
        std::fill(std::begin(state.gregs),std::end(state.gregs),0);
        state.rip=request->entry; state.gregs[FEXCore::X86State::REG_RSP]=rsp;
        state.fs_cached=0; state.gs_cached=0;
        state.callret_sp=reinterpret_cast<uintptr_t>(callret.address)+callret.size/4;
        state.segment_arrays[FEXCore::Core::CPUState::SEGMENT_ARRAY_INDEX_GDT]=gdt.data();
        state.segment_arrays[FEXCore::Core::CPUState::SEGMENT_ARRAY_INDEX_LDT]=gdt.data();
        state.cs_idx=FEXCore::Core::CPUState::DEFAULT_USER_CS<<3;
        auto* segment=FEXCore::Core::CPUState::GetSegmentFromIndex(state,state.cs_idx);
        FEXCore::Core::CPUState::SetGDTBase(segment,0);
        FEXCore::Core::CPUState::SetGDTLimit(segment,0xF'FFFFU);
        segment->L=1; segment->D=0;
        state.cs_cached=FEXCore::Core::CPUState::CalculateGDTBase(*segment);
        // Clear shared translations left by another API using this context.
        engine->context->ClearCodeCache(thread,false);
        uint64_t value=0;
        {
            auto lease=owner->lease();
            engine->probe.v4_translation=true;
            for (const auto& r:lease.ranges())
                engine->probe.ranges.push_back({r.guest,r.host,r.size,r.permissions});
            if (!engine->probe.ValidateGuestMemoryRanges()) return -ENOTSUP;
            FEXCore::Core::GuestMemoryFaultData fault{};
            state.GuestMemoryFaultRecord=&fault;
            struct ClearV4Fault {
                FEXCore::Core::CPUState& state;
                ~ClearV4Fault() { state.GuestMemoryFaultRecord=nullptr; }
            } clear_fault{state};
            engine->context->ExecuteThread(thread);
            state.GuestMemoryFaultRecord=nullptr;
            if (fault.Bytes) return -EFAULT;
            value=state.gregs[FEXCore::X86State::REG_RAX];
            engine->probe.ranges.clear();
        }
        owner->close();
        *result=value;
#if defined(VIXL_SIMULATOR)
        *backend=MAXPS4_CPU_FEX_ARM64_SIMULATOR;
#else
        *backend=MAXPS4_CPU_FEX_ARM64_JIT;
#endif
        return 0;
    } catch (const std::bad_alloc&) { return -ENOMEM; }
      catch (const std::exception&) { return -ENOTSUP; }
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
            engine->staged_ps4.clear(); engine->cpu_report = {};
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

extern "C" int32_t maxps4_fex_engine_run_m1_threads(maxps4_fex_engine* engine,
    const uint8_t* file, size_t length, uint32_t budget, uint64_t snapshot,
    const maxps4_m1_tls_input* inputs, size_t module_count, size_t thread_count,
    maxps4_m1_thread_report* reports, size_t report_count) {
    if (!reports || !report_count || report_count > 4) return -EINVAL;
    std::fill(reports,reports+report_count,maxps4_m1_thread_report{});
    if (!engine || !file || !length || !inputs || !module_count || module_count > 16 ||
        !thread_count || thread_count > 4 || report_count != thread_count ||
        snapshot > UINT64_MAX-0x108) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    if (engine->stage != MAXPS4_FEX_DISPATCHER_INITIALIZED) return -EAGAIN;
    try {
        maxps4::OrbisM1 process(maxps4::GuestAddressSpace::Backend::translated);
        int status = process.load(file,length,budget);
        if (status) return status;
        auto mapping_error = [&](const char* phase, uint64_t id, int error) {
            const auto failure = process.space.reservation_failure();
            std::fprintf(stderr,
                "M1 %s thread=%llu error=%d guest=0x%llx guard_bytes=%zu host_status=%d reservation_error=%d\n",
                phase, static_cast<unsigned long long>(id), error,
                static_cast<unsigned long long>(failure.guest), failure.bytes,
                failure.host_status, failure.error);
            return error;
        };
        if (process.tls.memory_size != 16 || process.tls.block_size != 16 ||
            process.tls.initial.size() != 8) return -ENOTSUP;
        if (!process.space.translate(snapshot,0x108,6)) return -EFAULT;
        maxps4::GuestTLSModules modules;
        std::vector<uint64_t> ids;
        for (size_t i=0;i<module_count;++i) {
            maxps4::PS4ExecutableImage image;
            status = image.load(inputs[i].bytes,inputs[i].size);
            if (status) return status;
            if (image.isSelf) return -ENOTSUP;
            for (const auto& h : image.headers) if (h.type == 2) return -ENOTSUP;
            uint64_t id = 0;
            status = modules.add(image,id);
            if (status) return status;
            // This finite fixture ABI snapshots an initialized word and a
            // zero word; templates with another layout need another harness.
            const auto* tls = modules.find(id);
            if (tls->memory_size < 16 || tls->initial.size() != 8) return -ENOTSUP;
            ids.push_back(id);
        }
        maxps4::GuestELF empty;
        maxps4::PS4GuestMemory memory(empty,&process.space);
        maxps4::PS4Libkernel kernel(memory);
        const maxps4::PS4KernelCall mutex_call{snapshot+0x100,0,0,0,0,0,0};
        if (kernel.invoke(maxps4::PS4KernelService::mutex_init,mutex_call,1)) return -EFAULT;
        struct Job {
            std::unique_ptr<maxps4::GuestThreadMemory> memory;
            std::unique_ptr<maxps4::GuestDynamicTLS> tls;
            FEXCore::Core::InternalThreadState* cpu = nullptr;
            std::vector<maxps4::GuestAddressSpace::Region> payloads;
            uint64_t id = 0;
        };
        std::vector<Job> jobs(thread_count);
        struct Cleanup {
            FEXCore::Context::Context* context;
            std::vector<Job>& jobs;
            ~Cleanup() { for (auto& job : jobs) if (job.cpu) context->DestroyThread(job.cpu); }
        } cleanup{engine->context.get(),jobs};
        // Create every ready context and private mapping before any CPU runs.
        for (size_t i=0;i<thread_count;++i) {
            auto& job = jobs[i];
            job.id = i ? kernel.threads().attach() : 1;
            if (!job.id) return -E2BIG;
            const uint64_t base = maxps4::OrbisM1::thread_base+i*0x4000000ULL;
            job.memory = std::make_unique<maxps4::GuestThreadMemory>(process.space);
            status = job.memory->initialize(process.tls,base,process.entry);
            if (status) return mapping_error("stack/static-TLS",job.id,status);
            job.tls = std::make_unique<maxps4::GuestDynamicTLS>(process.space,modules,base+0x1000000);
            for (auto id : ids) {
                uint64_t address = 0;
                status = job.tls->resolve(id,0,modules.find(id)->memory_size,address);
                if (status) return mapping_error("dynamic-TLS",job.id,status);
                job.payloads.push_back({address,modules.find(id)->memory_size,6});
            }
            status = process.admit(*job.memory,budget,job.payloads);
            if (status) return status;
            job.cpu = engine->context->CreateThread(process.entry,job.memory->rsp);
            if (!job.cpu) return -ENOMEM;
        }
        std::vector<maxps4_m1_thread_report> completed(thread_count);
        for (size_t i=0;i<thread_count;++i) {
            auto& job = jobs[i];
            auto& report = completed[i];
            const uint64_t sentinel = 0xdeadbeef;
            for (uint64_t address : {snapshot+16,snapshot+0x80,snapshot+0x88}) {
                status = process.space.write(address,&sentinel,8);
                if (status) return status;
            }
            bool consumed = false;
            try {
                status = executeImage(engine,process.source,&report.cpu.rax,&report.cpu.backend,
                    nullptr,false,false,&process,job.memory.get(),job.cpu,job.id,&kernel,&consumed);
            } catch (...) {
                if (consumed) job.cpu = nullptr;
                throw;
            }
            // executeImage joins/destroys an owned CPU context while its GDT and
            // private call/return mapping are still alive. Cleanup owns contexts
            // rejected before that ownership transfer.
            if (consumed) job.cpu = nullptr;
            if (status) return status;
            auto& cpu = report.cpu;
            cpu.entry = process.entry; cpu.rsp = job.memory->rsp; cpu.fs_base = job.memory->fs_base;
            cpu.calls = engine->probe.calls;
            const std::pair<uint64_t,uint64_t*> reads[] = {
                {snapshot,&cpu.memory}, {snapshot+8,&cpu.kernel_return},
                {snapshot+16,&cpu.zero_memory}, {maxps4::OrbisM1::heap_base,&cpu.heap_word},
                {job.memory->tls_begin,&cpu.tls_word}, {snapshot+0x80,&report.lock_result},
                {snapshot+0x88,&report.unlock_result}
            };
            for (const auto& read : reads) {
                status = process.space.read(read.first,read.second,8);
                if (status) return status;
            }
            for (size_t j=0;j<module_count;++j) {
                status = process.space.read(job.payloads[j].address,&report.modules[j],8);
                if (status) return status;
                status = process.space.read(job.payloads[j].address+8,&report.zeros[j],8);
                if (status) return status;
            }
            report.id = job.id; report.joined = 1;
            if (job.id != 1 && kernel.threads().detach(job.id) != maxps4::ps4::ok) return -EBUSY;
        }
        std::copy(completed.begin(),completed.end(),reports);
        return 0;
    } catch (const std::bad_alloc&) { return -ENOMEM; }
      catch (...) { return -EIO; }
}

extern "C" int32_t maxps4_fex_engine_run_m1(maxps4_fex_engine* engine,
    const uint8_t* file, size_t length, uint32_t budget, uint64_t snapshot,
    maxps4_m1_report* report) {
    if (!report) return -EINVAL;
    *report = {};
    if (!engine || !file || !length) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    if (engine->stage != MAXPS4_FEX_DISPATCHER_INITIALIZED) return -EAGAIN;
    try {
        maxps4::OrbisM1 process(maxps4::GuestAddressSpace::Backend::translated);
        int status = process.load(file,length,budget);
        if (status) { std::fprintf(stderr,"M1 image mapping failed: %d\n",status); return status; }
        maxps4::GuestThreadMemory thread(process.space);
        status = thread.initialize(process.tls,maxps4::OrbisM1::thread_base,process.entry);
        if (status) { std::fprintf(stderr,"M1 thread/TLS mapping failed: %d\n",status); return status; }
        status = process.admit(thread,budget);
        if (status) return status;
        const auto* output = process.space.translate(snapshot,24,4);
        if (!output) return -EFAULT;
        uint64_t rax = 0; uint32_t backend = 0;
        status = executeImage(engine,process.source,&rax,&backend,nullptr,false,false,&process,&thread);
        if (status) return status;
        report->rax = rax; report->backend = backend; report->entry = process.entry;
        report->rsp = thread.rsp; report->fs_base = thread.fs_base;
        report->calls = engine->probe.calls;
        std::memcpy(&report->memory,output,8);
        std::memcpy(&report->kernel_return,output+8,8);
        std::memcpy(&report->zero_memory,output+16,8);
        std::memcpy(&report->heap_word,process.space.translate(maxps4::OrbisM1::heap_base,8,4),8);
        if (thread.tls_begin && process.tls.memory_size >= 8)
            std::memcpy(&report->tls_word,process.space.translate(thread.tls_begin,8,4),8);
        return 0;
    } catch (const std::bad_alloc&) { return -ENOMEM; }
}




