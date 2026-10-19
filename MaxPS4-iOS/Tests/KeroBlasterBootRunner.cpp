// Mission 50: real-title boot runner (host development tool, FEX JIT).
// Reads clear SELF/PRX files read-only, links the complete batch, maps it
// through the TLS runtime and executes, under FEX, the rtld sequence observed
// in the binaries: libc DT_INIT(0,0,0), Fios2 DT_INIT(0,0,0), then the eboot
// entry with {argc,argv} and an exit handler. Every service call is logged;
// an unimplemented service stops execution (TRAP), never returns a value.
#include <FEXCore/Utils/LogManager.h>
#include "OrbisV4HostServices.hpp"
#include "OrbisV4TLSRuntime.hpp"
#include "OrbisV4SystemServices.hpp"
#include "LinuxVideoPresenter.hpp"
#include "Common/HostFeatures.h"
#include <FEXCore/Config/Config.h>
#include <FEXCore/Core/Context.h>
#include <FEXCore/Core/CoreState.h>
#include <FEXCore/Core/SignalDelegator.h>
#include <FEXCore/Core/X86Enums.h>
#include <FEXCore/Debug/InternalThreadState.h>
#include <FEXCore/HLE/SyscallHandler.h>
#include <sys/mman.h>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <cstdlib>
#include <algorithm>
#include <atomic>
#include <thread>
#include <csignal>
#include <filesystem>
#include <cmath>
#include "LinuxAudioSink.hpp"
#include "PadInputScript.hpp"
#include <execinfo.h>
#include <pthread.h>

namespace {
using namespace maxps4;
namespace x86 = FEXCore::X86State;
std::vector<uint8_t> slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}};
}
struct Stop { bool trap = false, exit = false, refused = false, deferred = false, block = false, yield = false, threadExit = false;
              std::string detail; uint64_t rip = 0, rsp = 0, value = 0; };
class Space final : public FEXCore::HLE::SyscallHandler {
public:
    Space() { OSABI = FEXCore::HLE::SyscallOSABI::OS_GENERIC; }
    std::vector<FEXCore::HLE::GuestMemoryRange> ranges;
    OrbisV4HostServices* services = nullptr;
    OrbisV4Translation::Lease* memory = nullptr;
    const OrbisV4ProcessBatch* batch = nullptr;
    Stop stop;
    std::function<void()> onCall;
    std::atomic<uint64_t> lastSyscall{0}, callCount{0};
    mutable std::atomic<uint64_t> lastFetch{0}, fetchCount{0};
    uint64_t trapSlot = 0; // Guest address holding the HLT address (see below).
    OrbisV4HostServices::Result pending; // Deferred service awaiting the runner.
    bool preemptible = false; uint64_t sliceStartNs = 0; static constexpr uint64_t sliceNs = 2000000;
    bool UsesGuestMemoryTranslation() const override { return true; }
    std::span<const FEXCore::HLE::GuestMemoryRange> GuestMemoryRanges() const override { return ranges; }
    uint64_t HandleSyscall(FEXCore::Core::CpuStateFrame* frame, FEXCore::HLE::SyscallArguments*) override {
        auto& state = frame->State; auto& r = state.gregs;
        OrbisV4HostServices::Registers regs{r[x86::REG_RAX], r[x86::REG_RDI], r[x86::REG_RSI], r[x86::REG_RDX],
            r[x86::REG_R10], r[x86::REG_R8], r[x86::REG_R9], r[x86::REG_RSP], state.rip, state.fs_cached};
        // The stub moved the 4th SysV argument (RCX) into R10 before SYSCALL.
        auto result = services->dispatch(regs, *memory);
        lastSyscall = state.rip; ++callCount;
        if (onCall) onCall();
        if (result.outcome == OrbisV4HostServices::Outcome::Returned) {
            // Preemption at service-call boundaries: once the time slice is used
            // and other threads exist, the call completes but the thread yields
            // (it resumes at the stub's RET with this value).
            if (!(preemptible && OrbisV4SystemServices::nowNs() - sliceStartNs > sliceNs)) {
                r[x86::REG_RAX] = result.value; state.rip += 2; return result.value;
            }
            result.outcome = OrbisV4HostServices::Outcome::Yield;
        }
        using O = OrbisV4HostServices::Outcome;
        stop.deferred = result.outcome == O::Deferred; stop.block = result.outcome == O::Block;
        stop.yield = result.outcome == O::Yield; stop.threadExit = result.outcome == O::ThreadExit; stop.value = result.value;
        if (stop.deferred || stop.block) pending = result;
        stop.trap = result.outcome == OrbisV4HostServices::Outcome::Trap;
        stop.exit = result.outcome == OrbisV4HostServices::Outcome::Exit;
        stop.refused = result.outcome == OrbisV4HostServices::Outcome::Refused;
        stop.detail = result.detail; stop.rip = state.rip; stop.rsp = r[x86::REG_RSP];
        // FEX does not re-read State.rip after an OS_GENERIC SYSCALL inside a
        // block: the stub's RET always executes. Registers ARE refilled, so the
        // stop is a register-only change: RSP -> a slot holding the HLT
        // address. The RET lands on HLT; the caller never sees a value.
        r[x86::REG_RSP] = trapSlot; r[x86::REG_RAX] = 0;
        return 0;
    }
    bool ReadGuestInstruction(uint64_t addr, void* output, size_t size) const override {
        const bool valid = FEXCore::HLE::SyscallHandler::ReadGuestInstruction(addr, output, size);
        lastFetch = addr; ++fetchCount;
        if (valid && fetchCount <= 65536 && std::getenv("MAXPS4_FETCH_TRACE")) {
            std::fprintf(stderr, "FETCH rip=0x%llx bytes=", (unsigned long long)addr);
            for (size_t i = 0; i < size; ++i) std::fprintf(stderr, "%02x", static_cast<uint8_t*>(output)[i]);
            std::fprintf(stderr, "\n");
        }
        return valid;
    }
    FEXCore::HLE::ExecutableRangeInfo QueryGuestExecutableRange(FEXCore::Core::InternalThreadState*, uint64_t addr) override {
        for (auto r : ranges) if ((r.Permissions & 5) == 5 && addr >= r.Guest && addr - r.Guest < r.Size) return {r.Guest, r.Size, false};
        return {};
    }
    std::optional<FEXCore::ExecutableFileSectionInfo> LookupExecutableFileSection(FEXCore::Core::InternalThreadState*, uint64_t) override { return {}; }
};
std::string where(const OrbisLinker::Image& image, uint64_t address) {
    for (const auto& m : image.modules) for (const auto& s : m.image.segments)
        if (address >= m.bias + s.header.address && address - m.bias - s.header.address < s.header.memorySize) {
            std::ostringstream o; o << m.path << "+0x" << std::hex << (address - m.bias); return o.str();
        }
    std::ostringstream o; o << "0x" << std::hex << address; return o.str();
}
}

int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    if (argc < 5) { std::cerr << "usage: boot <names.tsv> <eboot.bin> <libc.prx> <libSceFios2.prx> [app0-dir]\n"; return 2; }
    std::map<std::string, std::string> names;
    {
        std::ifstream t(argv[1]); std::string line; std::getline(t, line);
        while (std::getline(t, line)) {
            std::vector<std::string> f; std::stringstream ss(line); std::string x;
            while (std::getline(ss, x, '\t')) f.push_back(x);
            if (f.size() > 8 && !f[8].empty() && f[8] != "UNKNOWN") names[f[7]] = f[8];
        }
    }
    std::vector<OrbisV4ProcessBatch::File> files;
    for (int i = 2; i < 5; ++i) { std::string n = argv[i]; files.push_back({n.substr(n.find_last_of('/') + 1), slurp(n)}); }
    OrbisV4ProcessBatch batch; std::string error;
    if (const int status = batch.build(files, {0, 0, 0, 0}, names, error)) { std::cerr << error << '\n'; return status; }
    std::cout << batch.report;
    for (const auto& u : batch.unresolved) std::cout << "UNRESOLVED " << u << '\n';
    std::vector<uint32_t> ids; for (size_t i = 0; i < batch.inputs.size(); ++i) ids.push_back(uint32_t(i + 1));
    OrbisLinker::TLSImage linked; OrbisLinker::Report report; std::string reason;
    if (!OrbisLinker::linkTLS(batch.inputs, ids, {}, linked, reason, 32 * 1024 * 1024, &report)) {
        std::cout << "LINK.REFUSED " << reason << '\n' << report.text() << '\n'; return 1;
    }
    std::cout << "LINK.COMMITTED imports=" << report.resolvedImports << " relocations=" << report.validatedRelocations
              << " tls=" << report.tlsRelocations << " service_stubs=" << batch.services.size() << '\n';
    // Addresses needed by the rtld sequence, from the committed image.
    uint64_t libcInit = 0, fiosInit = 0, entry = 0; bool haveLibc = false, haveFios = false;
    for (const auto& m : linked.image().modules) {
        if (m.path == "libc.prx" && m.dynamic.init) { libcInit = m.bias + m.dynamic.initAddress; haveLibc = true; }
        if (m.path == "libSceFios2.prx" && m.dynamic.init) { fiosInit = m.bias + m.dynamic.initAddress; haveFios = true; }
        if (m.path == "eboot.bin") entry = m.entry;
    }
    if (!haveLibc || !haveFios || !entry) { std::cerr << "missing init/entry\n"; return 1; }

    LogMan::Throw::InstallHandler([](const char* text) { std::fprintf(stderr, "FEX assertion: %s\n", text); });
    LogMan::Msg::InstallHandler([](LogMan::DebugLevels level, const char* text) {
        if (level <= LogMan::ERROR) std::fprintf(stderr, "FEX error: %s\n", text);
    });
    FEXCore::Config::Initialize(); FEXCore::Config::Load();
    FEXCore::Config::Set(FEXCore::Config::CONFIG_IS64BIT_MODE, "1");
    FEXCore::Config::Set(FEXCore::Config::CONFIG_DISABLETELEMETRY, "1");
    FEXCore::Config::Set(FEXCore::Config::CONFIG_ENABLECODECACHINGWIP, "0");
    // Keep x86 memory ordering enabled until an explicit off-mode is validated.
    FEXCore::Config::Set(FEXCore::Config::CONFIG_TSOENABLED, "1");
    std::cout << "FEX.CONFIG TSO=1\n";
    // Each translated access emits a per-range check ladder (bounded at 16 MiB
    // per block by the guest-memory patch). Smaller blocks keep that bound.
    // MAXPS4_JIT_MAP=1: FEX writes /tmp/perf-<pid>.map (host code -> guest RIP per block),
    // which resolves the EXEC.A64 samples of a guest loop that makes no service call.
    if (std::getenv("MAXPS4_JIT_MAP")) FEXCore::Config::Set(FEXCore::Config::CONFIG_BLOCKJITNAMING, "1");
    FEXCore::Config::Set(FEXCore::Config::CONFIG_MAXINST, std::getenv("MAXPS4_MAXINST") ? std::getenv("MAXPS4_MAXINST") : "16");
#if defined(VIXL_SIMULATOR)
    FEXCore::Config::Set(FEXCore::Config::CONFIG_DISABLE_VIXL_INDIRECT_RUNTIME_CALLS, "0");
#endif
    auto features = FEX::FetchHostFeatures();
    // Note: disabling SVE here changes FEX's AVX-256 lowering (ASIMD pairs), whose
    // translated path mis-addressed a 32-byte store (+4 GiB) in libc; SVE stays on.
    FEXCore::SignalDelegator signals;
    Space space;
    auto ctx = FEXCore::Context::Context::CreateNewContext(features);
    ctx->SetSignalDelegator(&signals); ctx->SetSyscallHandler(&space); ctx->EnableExitOnHLT();
    if (!ctx->InitCore()) { std::cerr << "FEX init failed\n"; return 1; }
    FEXCore::Core::InternalThreadState* thread = nullptr;
    std::unique_ptr<OrbisV4TLSRuntime> runtime;
    // Every guest thread has its own FEX code cache: invalidation clears all.
    std::vector<FEXCore::Core::InternalThreadState*> fexThreads;
    if (OrbisV4TLSRuntime::load(linked, [&] { for (auto* t : fexThreads) ctx->ClearCodeCache(t, false); }, runtime, reason)) {
        std::cerr << "TLS runtime: " << reason << '\n'; return 1;
    }
    int code = 0;
    OrbisV4HostServices services(batch);
    // A lease pins guest memory while any guest CPU runs. It is released only
    // while no guest code executes (deferred mapping services), then re-taken.
    struct Held { OrbisV4TLSRuntime::Lease lease; explicit Held(OrbisV4TLSRuntime& r) : lease(r.lease(r.primary())) {} };
    if (const int limit = runtime->set_memory_limit(size_t(3) << 30)) { std::cerr << "memory limit " << limit << '\n'; return 1; }
    auto st = OrbisV4SystemServices::install(services, batch, linked.image(), runtime.get(), argc > 5 ? argv[5] : "");
    // Audio: MAXPS4_AUDIO_WAV=path captures the title's output; MAXPS4_AUDIO_PULSE=1 plays it.
    auto speaker=std::make_shared<LinuxAudioSink>();
    if(const char* wav=std::getenv("MAXPS4_AUDIO_WAV"); wav && !speaker->openWav(wav,48000))std::cout<<"AUDIO.WAV open failed: "<<wav<<'\n';
    if(std::getenv("MAXPS4_AUDIO_PULSE")){std::string why;std::cout<<(speaker->openPulse(48000,why)?"AUDIO.PULSE playing\n":"AUDIO.PULSE unavailable: "+why+"\n");}
    st->audio->sink=[speaker](int32_t,uint32_t,const int16_t* pcm,size_t frames){speaker->write(pcm,frames);};
    auto presenter=std::make_shared<LinuxVideoPresenter>();
    // MAXPS4_HEADLESS=1: no X11 window (long unattended sessions survive a lost
    // display); VideoOut presentation, hashing and PNG captures are unchanged.
    const bool headless=std::getenv("MAXPS4_HEADLESS")!=nullptr;
    st->graphics->openDevice=[presenter,headless](std::string& error){return headless || presenter->open(error);};
    st->graphics->closeDevice=[presenter]{presenter->close();};
    // Controller: X11 keyboard plus MAXPS4_INPUT_SCRIPT (buttons by presented frame).
    auto script=std::make_shared<PadInputScript>();
    if(const char* text=std::getenv("MAXPS4_INPUT_SCRIPT")){std::string why;if(!script->parse(text,why)){std::cerr<<"MAXPS4_INPUT_SCRIPT: "<<why<<'\n';return 1;}}
    st->graphics->present=[presenter,script,st,headless,captured=false,frame=uint64_t(0),held=uint32_t(0)](uint32_t w,uint32_t h,const std::vector<uint8_t>& rgba,uint64_t guest,uint64_t arg,std::string& error) mutable {
        if(!headless && !presenter->present(w,h,rgba,error))return false;
        std::cout<<"VIDEO.PRESENT width="<<w<<" height="<<h<<" guest=0x"<<std::hex<<guest<<std::dec<<" flip="<<arg<<'\n';
        // MAXPS4_FRAME_EVERY=N: also keep every Nth presented frame as PNG.
        ++frame;
        // MAXPS4_INPUT_FILE: same syntax as MAXPS4_INPUT_SCRIPT, re-read whenever the file
        // changes, so one running session can be steered frame by frame.
        if(const char* file=std::getenv("MAXPS4_INPUT_FILE")){
            static std::filesystem::file_time_type seen{};
            std::error_code ec; const auto stamp=std::filesystem::last_write_time(file,ec);
            if(!ec && stamp!=seen){
                seen=stamp; std::ifstream in(file); std::string text((std::istreambuf_iterator<char>(in)),{}),why;
                for(auto& c:text) if(c=='\n'||c=='\r') c=';';
                PadInputScript next; if(next.parse(text,why)){*script=next;std::cout<<"PAD.SCRIPT reloaded at frame "<<frame<<" steps="<<script->steps.size()<<'\n';}
                else std::cout<<"PAD.SCRIPT rejected at frame "<<frame<<": "<<why<<'\n';
            }
        }
        std::cout<<"PAD.FRAME "<<frame<<'\n';
        const uint32_t buttons=(headless?0u:presenter->pollButtons())|script->buttonsAt(frame+1); // state for the next frame
        if(buttons!=held){std::cout<<"PAD.INPUT frame="<<frame+1<<" buttons=0x"<<std::hex<<buttons<<std::dec<<'\n';held=buttons;}
        st->pad.buttons=buttons&~PadInputScript::stickMask;
        PadInputScript::stickAxes(buttons,st->pad.lx,st->pad.ly,st->pad.rx,st->pad.ry);
        if(const char* every=std::getenv("MAXPS4_FRAME_EVERY"); every && std::strtoull(every,nullptr,10) && frame%std::strtoull(every,nullptr,10)==0){
            const std::string dir=std::getenv("MAXPS4_FRAME_DIR")?std::getenv("MAXPS4_FRAME_DIR"):"docs/mission52-frames";
            char name[64];std::snprintf(name,sizeof(name),"/frame-%05llu.png",(unsigned long long)frame);
            if(!LinuxVideoPresenter::png(dir+name,w,h,rgba)){error="PNG capture failed";return false;}
        }
        if(!captured){
            const std::string dir=std::getenv("MAXPS4_FRAME_DIR")?std::getenv("MAXPS4_FRAME_DIR"):"docs/mission52-frames";
            if(!LinuxVideoPresenter::png(dir+"/first-guest-frame.png",w,h,rgba)){error="PNG capture failed";return false;}
            std::ofstream raw(dir+"/first-guest-frame.rgba",std::ios::binary);raw.write(reinterpret_cast<const char*>(rgba.data()),std::streamsize(rgba.size()));
            std::ofstream meta(dir+"/first-guest-frame.json");meta<<"{\"origin\":\"sceVideoOutSubmitFlip guest framebuffer snapshot\",\"guest_address\":"<<guest<<",\"width\":"<<w<<",\"height\":"<<h<<",\"format\":\"RGBA8\",\"flip_arg\":"<<arg<<"}\n";
            captured=true;
        }
        return true;
    };
    if (!OrbisV4SystemServices::prepareMainStack(*st, *runtime)) { std::cerr << "main stack mapping failed\n"; return 1; }
    auto held = std::make_unique<Held>(*runtime);
    auto refresh = [&] {
        space.ranges.clear();
        for (const auto& r : held->lease.memory().ranges()) space.ranges.push_back({r.guest, r.host, r.size, r.permissions});
        // The translated-access ladder is sequential with first-hit exit; order
        // is not semantic. Hot ranges first: process memory (stacks, heap), TLS,
        // the title's own modules, providers last.
        auto rank = [](const FEXCore::HLE::GuestMemoryRange& r) {
            if (r.Guest >= 0x200000000ULL && r.Guest < 0x700000000000ULL) return 0;
            if (r.Guest >= 0x700000000000ULL) return 1;
            if (r.Guest < 0x1000000ULL) return (r.Permissions & 2) ? 2 : 3;
            return 4;
        };
        std::stable_sort(space.ranges.begin(), space.ranges.end(), [&](const auto& x, const auto& y) { return rank(x) < rank(y); });
        space.memory = &held->lease.memory();
        return space.ValidateGuestMemoryRanges();
    };
    if (!refresh()) { std::cerr << "invalid FEX ranges\n"; return 1; }
    std::cout << "FEX.RANGES " << space.ranges.size() << '\n';
    space.services = &services; space.batch = &batch;
    uint64_t gate = 0; held->lease.memory().read_data(held->lease.rsp(), &gate, 8);
    uint64_t mainRsp = 0;
    if (!OrbisV4SystemServices::attachMain(*st, held->lease.memory(), held->lease.fs_base(), gate, mainRsp)) { std::cerr << "main thread attach failed\n"; return 1; }
    // Process parameters for the eboot entry, in the libkernel provider state.
    const uint64_t params = batch.stateBase.at("libkernel.prx") + 0x1000;
    {
        auto& mem = held->lease.memory();
        const uint64_t argvArray[2] = {params + 0x40, 0}; const uint32_t argcValue = 1; const char program[] = "eboot.bin";
        uint8_t zero[0x80] = {}; const uint64_t argvPointer = params + 0x20;
        if (mem.write_data(params, zero, sizeof(zero)) || mem.write_data(params, &argcValue, 4) ||
            mem.write_data(params + 8, &argvPointer, 8) || mem.write_data(params + 0x20, argvArray, 16) ||
            mem.write_data(params + 0x40, program, sizeof(program))) { std::cerr << "params write failed\n"; return 1; }
    }
    const uint64_t exitHandler = batch.halt.at("libkernel.prx");
    space.trapSlot = batch.stateBase.at("libkernel.prx") + 0x1800;
    if (held->lease.memory().write_data(space.trapSlot, &exitHandler, 8)) { std::cerr << "trap slot write failed\n"; return 1; }
    struct Step { const char* name; uint64_t rip, rdi, rsi, rdx; };
    std::vector<Step> mainSteps = {{"libc.prx DT_INIT", libcInit, 0, 0, 0}, {"libSceFios2.prx DT_INIT", fiosInit, 0, 0, 0},
                                   {"eboot.bin entry", entry, params, exitHandler, 0}};
    // --- guest threads ---
    struct Guest {
        uint64_t handle = 0; std::string name;
        FEXCore::Core::InternalThreadState* fex = nullptr; void* callret = nullptr;
        std::array<FEXCore::Core::CPUState::gdt_segment, 32> gdt{};
        enum class Status { Runnable, Blocked, Exited } status = Status::Runnable;
        OrbisV4HostServices::Result wait; bool resume = false; uint64_t resumeValue = 0, resumeRsp = 0, resumeRip = 0;
        size_t step = 0; bool fresh = true;
    };
    std::vector<std::unique_ptr<Guest>> guests;
    auto attach = [&](uint64_t handle, const std::string& name, uint64_t rip, uint64_t rsp, uint64_t fs, uint64_t rdi) {
        auto g = std::make_unique<Guest>(); g->handle = handle; g->name = name;
        g->callret = ::mmap(nullptr, FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        g->fex = ctx->CreateThread(rip, rsp);
        g->fex->CallRetStackBase = static_cast<uint8_t*>(g->callret);
        auto& state = g->fex->CurrentFrame->State;
        state.segment_arrays[FEXCore::Core::CPUState::SEGMENT_ARRAY_INDEX_GDT] = g->gdt.data();
        state.segment_arrays[FEXCore::Core::CPUState::SEGMENT_ARRAY_INDEX_LDT] = g->gdt.data();
        state.cs_idx = FEXCore::Core::CPUState::DEFAULT_USER_CS << 3;
        auto* segment = FEXCore::Core::CPUState::GetSegmentFromIndex(state, state.cs_idx);
        FEXCore::Core::CPUState::SetGDTBase(segment, 0); FEXCore::Core::CPUState::SetGDTLimit(segment, 0xFFFFF);
        segment->L = 1; segment->D = 0; state.cs_cached = FEXCore::Core::CPUState::CalculateGDTBase(*segment);
        for (auto& v : state.gregs) v = 0;
        state.rip = rip; state.gregs[x86::REG_RSP] = rsp; state.gregs[x86::REG_RDI] = rdi; state.fs_cached = fs; state.gs_cached = 0;
        if (!thread) thread = g->fex;
        fexThreads.push_back(g->fex);
        guests.push_back(std::move(g));
        return guests.back().get();
    };
    auto* mainThread = attach(st->mainThread, "main", mainSteps[0].rip, mainRsp, st->guests[st->mainThread].fs, 0);
    auto startStep = [&](Guest& g) {
        const auto& step = mainSteps[g.step];
        auto& state = g.fex->CurrentFrame->State; auto& r = state.gregs;
        for (auto& v : r) v = 0;
        state.rip = step.rip; r[x86::REG_RSP] = mainRsp; r[x86::REG_RDI] = step.rdi; r[x86::REG_RSI] = step.rsi; r[x86::REG_RDX] = step.rdx;
        std::cout << "STEP " << step.name << " rip=" << where(linked.image(), step.rip) << " t=" << OrbisV4SystemServices::nowNs() / 1000000 % 1000000 << "ms\n";
    };
    startStep(*mainThread);
    size_t printed = 0;
    auto printCalls = [&] {
        if (printed < services.droppedCalls()) printed = services.droppedCalls();
        for (; printed < services.totalCalls(); ++printed) {
            const auto& c = services.calls()[printed - services.droppedCalls()];
            if (std::getenv("MAXPS4_QUIET_LOCKS") && (c.name == "scePthreadMutexLock" || c.name == "scePthreadMutexUnlock") &&
                c.outcome == OrbisV4HostServices::Outcome::Returned && !c.value) continue;
            std::cout << "  [" << c.thread << "] CALL " << c.name << " from " << where(linked.image(), c.caller) << std::hex << " rdi=0x" << c.registers.rdi
                      << " rsi=0x" << c.registers.rsi << " rdx=0x" << c.registers.rdx << " rcx=0x" << c.registers.rcx << " r8=0x" << c.registers.r8
                      << " r9=0x" << c.registers.r9;
            if (c.outcome == OrbisV4HostServices::Outcome::Returned || c.outcome == OrbisV4HostServices::Outcome::Deferred) std::cout << " -> 0x" << c.value;
            std::cout << std::dec;
            switch (c.outcome) {
            case OrbisV4HostServices::Outcome::Trap: std::cout << " TRAP"; break;
            case OrbisV4HostServices::Outcome::Exit: std::cout << " EXIT"; break;
            case OrbisV4HostServices::Outcome::Refused: std::cout << " REFUSED"; break;
            case OrbisV4HostServices::Outcome::Deferred: std::cout << " (deferred)"; break;
            case OrbisV4HostServices::Outcome::Block: std::cout << " (blocks)"; break;
            case OrbisV4HostServices::Outcome::Yield: std::cout << " (yield)"; break;
            case OrbisV4HostServices::Outcome::ThreadExit: std::cout << " (thread exit)"; break;
            default: break;
            }
            std::cout << '\n';
        }
    };
    auto backtrace = [&](uint64_t rsp, uint64_t rbp) {
        uint64_t ret = 0, frame = rbp;
        if (!held->lease.memory().read_data(rsp, &ret, 8)) std::cout << "    #0 " << where(linked.image(), ret) << '\n';
        for (int depth = 1; depth < 24 && frame; ++depth) {
            uint64_t next = 0;
            if (held->lease.memory().read_data(frame, &next, 8) || held->lease.memory().read_data(frame + 8, &ret, 8)) break;
            std::cout << "    #" << depth << ' ' << where(linked.image(), ret) << '\n';
            if (next <= frame) break; frame = next;
        }
    };
    space.onCall = printCalls;
    const auto executionThread = pthread_self();
    const bool profile = std::getenv("MAXPS4_PROFILE") != nullptr;
    if (profile) std::signal(SIGPROF, +[](int) {
        void* frames[32]; int count = ::backtrace(frames, 32);
        ::backtrace_symbols_fd(frames, count, 2);
    });
    // Diagnostic watchdog: samples the running guest thread's RIP (read-only;
    // the value is updated by FEX at block boundaries) every few seconds.
    std::atomic<FEXCore::Core::InternalThreadState*> running{nullptr}; std::atomic<bool> stopWatch{false};
    const uint64_t runStart = OrbisV4SystemServices::nowNs();
    // Time-slice preemption of guest code that makes no service call (spin
    // waits on another guest thread): block linking off, and every 10 ms the
    // dispatcher is asked to return at its next loop top (FEXCore::MaxPS4).
    const bool preemption = !std::getenv("MAXPS4_NO_PREEMPT");
    // Linked loops are interrupted at block entries (simulator registry); MAXPS4_NO_BLOCK_LINKS
    // forces every exit through the dispatcher instead.
    FEXCore::MaxPS4::BlockLinking = !std::getenv("MAXPS4_NO_BLOCK_LINKS");
    std::atomic<size_t> guestCount{0}; uint64_t preemptions = 0;
    std::thread ticker([&] {
        while (preemption && !stopWatch) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (running.load() && guestCount.load() > 1) FEXCore::MaxPS4::PreemptRequest = 1;
        }
    });
    std::thread watchdog([&] {
        while (!stopWatch) {
            for (int i = 0; i < 50 && !stopWatch; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            auto* t = running.load();
            if (t && !stopWatch) {
                std::cout << "  WATCH t=" << (OrbisV4SystemServices::nowNs() - runStart) / 1000000
                          << "ms last_syscall=" << where(linked.image(), space.lastSyscall.load())
                          << " last_fetch=" << where(linked.image(), space.lastFetch.load())
                          << " calls=" << space.callCount.load() << " fetches=" << space.fetchCount.load() << '\n';
                if (profile) pthread_kill(executionThread, SIGPROF);
            }
        }
    });
    const uint64_t budget = std::getenv("MAXPS4_SLICES") ? std::strtoull(std::getenv("MAXPS4_SLICES"), nullptr, 0) : 2000000;
    size_t current = 0; uint64_t slices = 0; bool done = false;
    while (!done && slices++ < budget) {
        // Choose the next runnable thread (round robin from the current one).
        Guest* chosen = nullptr; uint64_t soonest = UINT64_MAX;
        for (size_t k = 0; k < guests.size() && !chosen; ++k) {
            auto& g = *guests[(current + k) % guests.size()];
            if (g.status == Guest::Status::Runnable) { chosen = &g; current = (current + k) % guests.size(); }
            else if (g.status == Guest::Status::Blocked) {
                if (g.wait.ready && g.wait.ready()) {
                    st->thread = g.handle;
                    g.resumeValue = g.wait.wake ? g.wait.wake(held->lease.memory()) : 0;
                    services.note(g.handle, "wake");
                    std::cout << "SCHED.WAKE thread=" << g.handle << " wait=" << g.wait.detail
                              << " value=0x" << std::hex << g.resumeValue << std::dec << '\n';
                    g.status = Guest::Status::Runnable; chosen = &g; current = (current + k) % guests.size();
                } else if (g.wait.deadlineNs) soonest = std::min(soonest, g.wait.deadlineNs);
            }
        }
        if (!chosen) {
            if (soonest != UINT64_MAX) {
                const uint64_t now = OrbisV4SystemServices::nowNs();
                if (soonest > now) std::this_thread::sleep_for(std::chrono::nanoseconds(soonest - now));
                continue;
            }
            std::cout << "DEADLOCK: no runnable guest thread\n";
            for (auto& g : guests) std::cout << "  thread " << g->name << " status=" << int(g->status) << " waiting=" << g->wait.detail << '\n';
            code = 7; break;
        }
        auto& g = *chosen; auto& state = g.fex->CurrentFrame->State; auto& r = state.gregs;
        st->thread = g.handle;
        if (g.resume) { r[x86::REG_RAX] = g.resumeValue; r[x86::REG_RSP] = g.resumeRsp; state.rip = g.resumeRip; g.resume = false; }
        space.stop = {};
        state.callret_sp = reinterpret_cast<uintptr_t>(g.callret) + FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE / 4;
        FEXCore::Core::GuestMemoryFaultData fault{}; state.GuestMemoryFaultRecord = &fault;
        services.currentThread = g.handle;
        space.preemptible = guests.size() > 1; space.sliceStartNs = OrbisV4SystemServices::nowNs();
        running = g.fex;
        guestCount = guests.size();
        ctx->ExecuteThread(g.fex);
        const bool preempted = FEXCore::MaxPS4::PreemptRequest.exchange(0) != 0;
        running = nullptr;
        state.GuestMemoryFaultRecord = nullptr;
        auto resumeAfterStub = [&](uint64_t value) { g.resume = true; g.resumeValue = value; g.resumeRsp = space.stop.rsp; g.resumeRip = space.stop.rip + 2; };
        if (space.stop.deferred) {
            auto pending = std::move(space.pending);
            held.reset();
            const int status = pending.outside ? pending.outside() : -EINVAL;
            held = std::make_unique<Held>(*runtime);
            if (!refresh()) { std::cout << "  INVALID RANGES after mapping\n"; code = 6; break; }
            const uint64_t value = pending.complete(held->lease.memory(), status);
            services.settle(value);
            std::cout << "DEFERRED.COMPLETE thread=" << g.handle << " value=0x" << std::hex << value << std::dec << '\n';
            resumeAfterStub(value);
            for (const auto object : st->spawned) {
                const auto& rec = st->guests[object];
                attach(object, rec.name, rec.entry, rec.rsp, rec.fs, rec.arg);
                std::cout << "  THREAD.START " << rec.name << " entry=" << where(linked.image(), rec.entry) << '\n';
            }
            st->spawned.clear();
            printCalls();
            continue;
        }
        printCalls();
        if (fault.Bytes) {
            std::cout << "  FAULT [" << g.name << "] guest=0x" << std::hex << fault.Address << std::dec << " bytes=" << fault.Bytes
                      << " rip=" << where(linked.image(), state.rip) << '\n';
            backtrace(r[x86::REG_RSP], r[x86::REG_RBP]);
            code = 3; break;
        }
        if (space.stop.block) { g.status = Guest::Status::Blocked; g.wait = std::move(space.pending); resumeAfterStub(0); continue; }
        if (space.stop.yield) { resumeAfterStub(space.stop.value); current = (current + 1) % guests.size(); continue; }
        auto finishThread = [&](uint64_t value) {
            st->guests[g.handle].exited = true; st->guests[g.handle].exitValue = value; g.status = Guest::Status::Exited;
            std::cout << "  THREAD.EXIT " << g.name << " value=0x" << std::hex << value << std::dec << '\n';
        };
        if (space.stop.threadExit) { finishThread(space.stop.value); if (&g == mainThread) done = true; continue; }
        if (space.stop.trap || space.stop.refused || space.stop.exit) {
            std::cout << "  STOP [" << g.name << "] " << space.stop.detail << " at " << where(linked.image(), space.stop.rip) << '\n';
            backtrace(space.stop.rsp, r[x86::REG_RBP]);
            code = space.stop.exit ? 0 : 4; break;
        }
        if (state.rip == gate || state.rip == gate + 1) {
            if (&g == mainThread) {
                std::cout << "  RETURNED rax=0x" << std::hex << r[x86::REG_RAX] << std::dec << '\n';
                if (++g.step < mainSteps.size()) { startStep(g); continue; }
                done = true; continue;
            }
            finishThread(r[x86::REG_RAX]); continue;
        }
        if (state.rip == exitHandler || state.rip == exitHandler + 1) {
            std::cout << "  EXIT-HANDLER reached (process exit path) rdi=0x" << std::hex << r[x86::REG_RDI] << std::dec << '\n';
            done = true; continue;
        }
        if (preempted) { // stopped at the dispatcher loop top: resume later at State.rip
            ++preemptions;
            // Where preempted threads were (State.rip is exact here): sampled for diagnostics.
            if (preemptions % 50 == 0) std::cout << "SCHED.PREEMPT thread=" << g.handle << " name=" << g.name << " rip=" << where(linked.image(), state.rip) << std::hex
                                                    << " rax=" << r[x86::REG_RAX] << " rcx=" << r[x86::REG_RCX] << " rdx=" << r[x86::REG_RDX]
                                                    << " rdi=" << r[x86::REG_RDI] << " r8=" << r[x86::REG_R8] << " r9=" << r[x86::REG_R9]
                                                    << " r10=" << r[x86::REG_R10] << " r13=" << r[x86::REG_R13] << " r14=" << r[x86::REG_R14]
                                                    << " r15=" << r[x86::REG_R15] << " rbp=" << r[x86::REG_RBP] << std::dec << '\n';
            current = (current + 1) % guests.size(); continue;
        }
        std::cout << "  HALT/UNKNOWN [" << g.name << "] rip=" << where(linked.image(), state.rip) << " rax=0x" << std::hex << r[x86::REG_RAX] << std::dec << '\n';
        code = 5; break;
    }
    stopWatch = true; watchdog.join(); ticker.join();
    std::cout << "SCHED.PREEMPT count=" << preemptions << " block_linking=" << int(FEXCore::MaxPS4::BlockLinking.load()) << '\n';
    if (!done && slices > budget) {
        std::cout << "SLICE BUDGET EXHAUSTED (" << budget << ")\n";
        code = 8;
    }
    std::cout << "ELAPSED " << (OrbisV4SystemServices::nowNs() - runStart) / 1000000 << " ms\n";
    std::cout << "SERVICE.CALLS total=" << services.totalCalls() << " threads=" << guests.size() << '\n';
    for (const auto& [handle, port] : st->audio->ports)
        std::cout << "AUDIO.PORT handle=" << handle << " buffers=" << port.buffers << " frames=" << port.frames << " non_silent_frames=" << port.nonSilentFrames
                  << " rms=" << (port.frames ? std::sqrt(port.sumSquares / double(port.frames * 2)) : 0.0) << " captured_frames=" << speaker->frames()
                  << " pulse_errors=" << speaker->pulseErrors() << '\n';
    speaker->close();
    space.memory = nullptr;
    held.reset();
    // Ownership order: retire mappings (which invalidates the code cache of
    // the still-alive thread) before destroying the thread.
    runtime->close(); runtime.reset();
    fexThreads.clear();
    for (auto& g : guests) { ctx->DestroyThread(g->fex); ::munmap(g->callret, FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE); }
    thread = nullptr;
    return code;
}
