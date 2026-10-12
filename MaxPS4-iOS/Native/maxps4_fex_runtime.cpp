#include "maxps4_fex_runtime.h"
#include "maxps4_jit_memory.h"
#include "maxps4_guest_elf.h"
#include "maxps4_ps4_guest_memory.h"
#include "PM4GuestSubmission.hpp"
#include "Common/HostFeatures.h"
#include <FEXCore/Config/Config.h>
#include <FEXCore/Core/Context.h>
#include <FEXCore/Core/SignalDelegator.h>
#include <TargetConditionals.h>
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

extern "C" int maxps4_native_guest_x86_run(const uint8_t*, size_t, uint32_t,
                                           uint64_t*) noexcept;
namespace {
// Anonymous immutable guest memory, not a PS4 or Linux syscall ABI.
// A syscall has no permitted host effect and returns the real ENOSYS error.
class ProbeAddressSpace final : public FEXCore::HLE::SyscallHandler {
public:
    ProbeAddressSpace() { OSABI = FEXCore::HLE::SyscallOSABI::OS_GENERIC; }
    maxps4::GuestELF* image = nullptr;
    uint64_t HandleSyscall(FEXCore::Core::CpuStateFrame*, FEXCore::HLE::SyscallArguments*) override {
        return static_cast<uint64_t>(-ENOSYS);
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
            const auto* pointer = image->translate(segment.address, executable_bytes, 5);
            if (!pointer) continue;
            const auto begin = reinterpret_cast<uintptr_t>(pointer);
            if (address >= begin && address - begin < executable_bytes)
                return {begin, executable_bytes, false};
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
}
struct maxps4_fex_engine {
    // This real FEX configuration holder installs no process signal handlers.
    // It outlives the context. Only admitted no-fault straight-line code executes.
    FEXCore::SignalDelegator signals;
    ProbeAddressSpace probe;
    maxps4::GuestELF staged_ps4;
    fextl::unique_ptr<FEXCore::Context::Context> context;
    int32_t stage = MAXPS4_FEX_CONTEXT_CREATED;
};
namespace {
// FEX configuration is process-global. This adapter owns it until the last
// engine closes. Future independent FEX consumers must share this ownership.
std::mutex runtime_lock;
size_t engines = 0;
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
    delete engine; // Context must die before global configuration shutdown.
    --engines;
    release_config_if_unused();
}
extern "C" int32_t maxps4_fex_engine_initialize(maxps4_fex_engine *engine) {
    if (!engine) return -EINVAL;
    std::lock_guard lock(runtime_lock);
#if TARGET_OS_IPHONE
    // FEXCore is built with patch-fexcore-jit-provider.py: its iOS JIT
    // allocations use the prepared MaxPS4 RX/RW pool, never MAP_JIT fallback.
    // Fail closed until StikDebug regions pass alias/protection/canary checks.
    if (engine->stage == MAXPS4_FEX_DISPATCHER_INITIALIZED) return 0;
    const int32_t prepared = maxps4_jit_prepare();
    if (prepared != 0) {
        std::fprintf(stderr, "MaxPS4 FEX JIT: RX/RW preparation failed (%d).\n", prepared);
        return prepared;
    }
    engine->context->SetSignalDelegator(&engine->signals);
    engine->context->SetSyscallHandler(&engine->probe);
    engine->context->EnableExitOnHLT();
    errno = 0;
    try {
        if (!engine->context->InitCore()) return -(errno ? errno : ENOMEM);
    } catch (const std::bad_alloc&) { return -ENOMEM; }
    engine->stage = MAXPS4_FEX_DISPATCHER_INITIALIZED;
    return 0;
#else
    if (engine->stage == MAXPS4_FEX_DISPATCHER_INITIALIZED) return 0;
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
#endif
}
extern "C" int32_t maxps4_fex_engine_stage(const maxps4_fex_engine *engine) {
    std::lock_guard lock(runtime_lock);
    return engine ? engine->stage : 0;
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
    uint64_t* result, uint32_t* backend, const PM4Request* submission) {
    if (engine->stage != MAXPS4_FEX_DISPATCHER_INITIALIZED) return -EAGAIN;
    // FEX still uses rebased host addresses for the admitted instruction set.
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
    const auto* entry_pointer = image.entry_pointer();
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
    engine->probe.image = &image;
    struct ClearRange {
        ProbeAddressSpace& space;
        ~ClearRange() { space.image = nullptr; }
    } clear_range {engine->probe};
    auto* thread = engine->context->CreateThread(reinterpret_cast<uintptr_t>(entry_pointer),
        reinterpret_cast<uintptr_t>(stack_base) + page - 16);
    if (!thread) return -ENOMEM;
    struct DestroyThread {
        FEXCore::Context::Context* context;
        FEXCore::Core::InternalThreadState* thread;
        ~DestroyThread() { context->DestroyThread(thread); }
    } destroy {engine->context.get(), thread};
    auto& state = thread->CurrentFrame->State;
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
    engine->context->ClearCodeCache(thread, false);
    engine->context->ExecuteThread(thread);
    if (submission) {
        const auto snapshot = maxps4::gpu::snapshotPM4(image, submission->address, submission->count);
        if (snapshot.error) return snapshot.error;
        std::memcpy(submission->words, snapshot.words.data(), snapshot.words.size() * sizeof(uint32_t));
        *submission->written = snapshot.words.size();
    }
    *result = state.gregs[FEXCore::X86State::REG_RAX];
    *backend = MAXPS4_CPU_FEX_ARM64_JIT;
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
    engine->staged_ps4.clear();
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
extern "C" int32_t maxps4_fex_engine_read_ps4(maxps4_fex_engine* engine,
    uint64_t address, uint8_t* output, size_t length) {
    if (!engine || !output || !length || length > 1024 * 1024) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    maxps4::PS4GuestMemory memory(engine->staged_ps4);
    return memory.read(address, output, length) ? 0 : -EFAULT;
}
extern "C" int32_t maxps4_fex_engine_fetch_ps4(maxps4_fex_engine* engine,
    uint64_t address, uint8_t* output, size_t length) {
    if (!engine || !output || !length || length > 1024 * 1024) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    maxps4::PS4GuestMemory memory(engine->staged_ps4);
    return memory.fetch(address, output, length) ? 0 : -EFAULT;
}
extern "C" int32_t maxps4_fex_engine_write_ps4(maxps4_fex_engine* engine,
    uint64_t address, const uint8_t* input, size_t length) {
    if (!engine || !input || !length || length > 1024 * 1024) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    maxps4::PS4GuestMemory memory(engine->staged_ps4);
    return memory.write(address, input, length) ? 0 : -EFAULT;
}
extern "C" int32_t maxps4_fex_engine_query_ps4(maxps4_fex_engine* engine,
    uint64_t address, size_t length, uint32_t permissions, uint32_t* permitted) {
    if (permitted) *permitted = 0;
    if (!engine || !permitted || !length || length > 1024 * 1024 ||
        !permissions || (permissions & ~7U)) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    maxps4::PS4GuestMemory memory(engine->staged_ps4);
    *permitted = memory.permitted(address, length, permissions) ? 1U : 0U;
    return 0;
}
extern "C" int32_t maxps4_fex_engine_contiguous_ps4(maxps4_fex_engine* engine,
    uint64_t address, size_t limit, uint32_t permissions, size_t* available) {
    if (available) *available = 0;
    if (!engine || !available || !limit || limit > 1024 * 1024 ||
        !permissions || (permissions & ~7U)) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    maxps4::PS4GuestMemory memory(engine->staged_ps4);
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

