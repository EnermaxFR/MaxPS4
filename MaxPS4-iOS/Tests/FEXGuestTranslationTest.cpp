#include <FEXCore/Utils/LogManager.h>
#include "maxps4_orbis_v4_translation.h"
#include "OrbisPRXFEXBridge.hpp"
#include "OrbisV4AppContent.hpp"
#include "OrbisV4AudioOut.hpp"
#include "OrbisV4ErrorDialog.hpp"
#include "OrbisV4GnmDriver.hpp"
#include "OrbisV4NpCommon.hpp"
#include "OrbisV4NpManager.hpp"
#include "OrbisV4NpTrophy.hpp"
#include "OrbisV4LocalServices.hpp"
#include "OrbisV4TLSRelocations.hpp"
#include "OrbisV4TLSRuntime.hpp"
#include "OrbisSymbolsFixture.h"
#include "OrbisTLSMission44Fixture.h"
#include "OrbisV4LibcInternal.hpp"
#include "PM4MetalDrawPlan.hpp"
#include "OrbisV4CommonDialog.hpp"
#include "OrbisV4CompatibilityFixture.h"
#include "OrbisFixture.h"
#include "Common/HostFeatures.h"
#include <FEXCore/Config/Config.h>
#include <FEXCore/Core/Context.h>
#include <FEXCore/Core/CoreState.h>
#include <FEXCore/Core/SignalDelegator.h>
#include <FEXCore/Core/X86Enums.h>
#include <FEXCore/Debug/InternalThreadState.h>
#include <FEXCore/HLE/SyscallHandler.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <csignal>
#include <sys/mman.h>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::abort(); } } while (0)
class CallRetMemory {
public:
    CallRetMemory() : ptr_(::mmap(nullptr,size(),PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0)) { CHECK(ptr_!=MAP_FAILED); }
    ~CallRetMemory() { ::munmap(ptr_,size()); }
    uint8_t* data() const { return static_cast<uint8_t*>(ptr_); }
    size_t size() const { return FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE; }
private:
    void* ptr_;
};
#if !defined(VIXL_SIMULATOR)
// Native preemption (FEXCore::MaxPS4::NativePreemptCheck): the armed entry
// load faults on the guard page and continues at PreemptTarget.
static std::atomic<FEXCore::Core::InternalThreadState*> preempt_running{nullptr};
static void preempt_fault(int, siginfo_t* info, void* context) {
    auto* thread=preempt_running.load();
    if (!thread || !FEXCore::MaxPS4::IsPreemptFault(info->si_addr)) std::abort();
    FEXCore::MaxPS4::ArmPreempt(thread,false);
    auto* uc=static_cast<ucontext_t*>(context);
#if defined(__APPLE__)
    auto& pc=uc->uc_mcontext->__ss.__pc; const uint64_t x28=uc->uc_mcontext->__ss.__x[28];
#else
    auto& pc=uc->uc_mcontext.pc; const uint64_t x28=uc->uc_mcontext.regs[28];
#endif
    pc-=4; // the entry's first load
    if (const uint64_t target=FEXCore::MaxPS4::PreemptTarget(pc,x28)) pc=target;
}
#endif
class Space final : public FEXCore::HLE::SyscallHandler {
public:
    Space() { OSABI = FEXCore::HLE::SyscallOSABI::OS_GENERIC; }
    std::vector<FEXCore::HLE::GuestMemoryRange> ranges;
    bool UsesGuestMemoryTranslation() const override { return true; }
    std::span<const FEXCore::HLE::GuestMemoryRange> GuestMemoryRanges() const override { return ranges; }
    uint64_t HandleSyscall(FEXCore::Core::CpuStateFrame*,FEXCore::HLE::SyscallArguments*) override { std::abort(); }
    FEXCore::HLE::ExecutableRangeInfo QueryGuestExecutableRange(FEXCore::Core::InternalThreadState*,uint64_t addr) override {
        for (auto r : ranges) if ((r.Permissions & 5)==5 && addr>=r.Guest && addr-r.Guest<r.Size)
            return {r.Guest,r.Size,false};
        return {};
    }
    std::optional<FEXCore::ExecutableFileSectionInfo> LookupExecutableFileSection(FEXCore::Core::InternalThreadState*,uint64_t) override { return {}; }
};
static void instruction_boundaries() {
    Space space;
    const uint8_t left[]={0xb8,42}, right[]={0,0,0,0xf4};
    constexpr uint64_t guest=0x400000;
    space.ranges={{guest,reinterpret_cast<uintptr_t>(left),sizeof(left),5},
                  {guest+sizeof(left),reinterpret_cast<uintptr_t>(right),sizeof(right),5}};
    std::array<uint8_t,6> output{};
    CHECK(space.ReadGuestInstruction(guest,output.data(),output.size()));
    CHECK((output==std::array<uint8_t,6>{0xb8,42,0,0,0,0xf4}));
    for (uint32_t permissions : {0U,4U,6U,7U,13U}) {
        space.ranges[1].Permissions=permissions;
        output.fill(0xa5);
        CHECK(!space.ReadGuestInstruction(guest,output.data(),output.size()));
        for (auto byte:output) CHECK(byte==0xa5);
    }
    space.ranges.pop_back();
    CHECK(!space.ReadGuestInstruction(guest,output.data(),output.size()));
    for (auto byte:output) CHECK(byte==0xa5);
    CHECK(!space.ReadGuestInstruction(UINT64_MAX-1,output.data(),4));
    CHECK(!space.ReadGuestInstruction(guest,output.data(),0));
    // Malformed host arithmetic is refused before dereferencing any byte.
    space.ranges={{guest,UINT64_MAX,4,5}};
    CHECK(!space.ReadGuestInstruction(guest,output.data(),4));
    CHECK(!space.ReadGuestInstruction(guest,output.data(),1)); // full range wraps
    for (auto byte:output) CHECK(byte==0xa5);
    std::puts("PASS instruction boundary preflight: split RX, hole, NX, overflow; output unchanged on failure");
}
int main() {
    instruction_boundaries();
    // Keep assertion text visible when the pinned emitter terminates a check.
    LogMan::Throw::InstallHandler([](const char* text) { std::fprintf(stderr,"FEX assertion: %s\n",text); });
    LogMan::Msg::InstallHandler([](LogMan::DebugLevels level,const char* text) {
        if (level<=LogMan::ERROR) std::fprintf(stderr,"FEX error: %s\n",text);
    });

    FEXCore::Config::Initialize(); FEXCore::Config::Load();
    FEXCore::Config::Set(FEXCore::Config::CONFIG_IS64BIT_MODE,"1");
    FEXCore::Config::Set(FEXCore::Config::CONFIG_DISABLETELEMETRY,"1");
    FEXCore::Config::Set(FEXCore::Config::CONFIG_ENABLECODECACHINGWIP,"0");
#if defined(VIXL_SIMULATOR)
    FEXCore::Config::Set(FEXCore::Config::CONFIG_DISABLE_VIXL_INDIRECT_RUNTIME_CALLS,"0");
#endif
    for (bool tso : {false,true}) for (bool lse : {false,true}) {
        FEXCore::Config::Set(FEXCore::Config::CONFIG_TSOENABLED,tso?"1":"0");
        std::fprintf(stderr,"BEGIN translated FEX tso=%d lse=%d\n",tso,lse);
        auto features=FEX::FetchHostFeatures();
#if !defined(VIXL_SIMULATOR)
        if (lse && !features.SupportsAtomics) continue;
#endif
        features.SupportsAtomics=lse;
#if defined(VIXL_SIMULATOR)
        if (std::getenv("MAXPS4_TEST_NO_SVE")) {
            features.SupportsAVX=false; features.SupportsSVE128=false; features.SupportsSVE256=false;
        }
#endif
        FEXCore::SignalDelegator signals;
        Space space;
        auto ctx=FEXCore::Context::Context::CreateNewContext(features); CHECK(ctx);
        ctx->SetSignalDelegator(&signals); ctx->SetSyscallHandler(&space); ctx->EnableExitOnHLT(); CHECK(ctx->InitCore());
        constexpr uint64_t code=0x400000ULL, data=0x400000000ULL;
        auto* thread=ctx->CreateThread(code, data+2048); CHECK(thread);
        std::array<FEXCore::Core::CPUState::gdt_segment,32> gdt{};
        CallRetMemory callret;
        thread->CallRetStackBase=callret.data();
        auto& state=thread->CurrentFrame->State;
        state.segment_arrays[FEXCore::Core::CPUState::SEGMENT_ARRAY_INDEX_GDT]=gdt.data();
        state.segment_arrays[FEXCore::Core::CPUState::SEGMENT_ARRAY_INDEX_LDT]=gdt.data();
        state.cs_idx=FEXCore::Core::CPUState::DEFAULT_USER_CS<<3;
        auto* segment=FEXCore::Core::CPUState::GetSegmentFromIndex(state,state.cs_idx);
        FEXCore::Core::CPUState::SetGDTBase(segment,0); FEXCore::Core::CPUState::SetGDTLimit(segment,0xFFFFF);
        segment->L=1; segment->D=0; state.cs_cached=FEXCore::Core::CPUState::CalculateGDTBase(*segment);
        auto* second=ctx->CreateThread(code,data+2048,&state); CHECK(second);
        second->CallRetStackBase=callret.data();
        // Prove malformed snapshots are rejected on the C++ compile stack,
        // before decoding bytes or emitting a native memory operand. No guest
        // dispatch is attempted for these cases, including inaccessible hosts.
        {
            std::array<uint8_t,16> source{0xb8,42,0,0,0,0xf4}, writable{};
            const std::vector<FEXCore::HLE::GuestMemoryRange> valid={
                {code,reinterpret_cast<uintptr_t>(source.data()),source.size(),5},
                {data,reinterpret_cast<uintptr_t>(writable.data()),writable.size(),6}};
            space.ranges=valid;
            CHECK(space.ValidateGuestMemoryRanges());
            size_t refusals=0;
            auto reject=[&] {
                CHECK(!space.ValidateGuestMemoryRanges());
                ctx->ClearCodeCache(thread,false);
                bool rejected=false;
                try { ctx->CompileRIP(thread,code); }
                catch (const std::runtime_error& error) {
                    rejected=std::string(error.what())=="Invalid translated guest memory snapshot";
                }
                CHECK(rejected && source[0]==0xb8 && writable[0]==0);
                ++refusals;
                space.ranges=valid;
            };
            space.ranges.clear(); reject();
            for (uint32_t permission : {7U,13U,1U}) {
                space.ranges[0].Permissions=permission; reject();
            }
            space.ranges[0].Host=0; reject();
            space.ranges[0].Host=UINT64_MAX-1; reject();
            space.ranges[0].Guest=UINT64_MAX-1; reject();
            space.ranges[0].Size=0; reject();
            space.ranges[1].Guest=code+1; reject();
            space.ranges[1].Host=valid[0].Host; reject(); // writable alias of RX
            // Read-only aliases preserve W^X; ordering is not prescribed.
            space.ranges[1].Host=valid[0].Host; space.ranges[1].Permissions=4;
            CHECK(space.ValidateGuestMemoryRanges());
            std::swap(space.ranges[0],space.ranges[1]);
            CHECK(space.ValidateGuestMemoryRanges());
            CHECK(refusals==10);
            space.ranges.clear();
            std::puts("PASS 10 malformed FEX snapshots refused before decode/JIT; bytes unchanged; no dispatch");
        }
        // Time-slice preemption of a guest spin wait. Both guest threads share
        // this host thread: A spins until B stores the flag, so A must be
        // stopped (State.rip kept) for B to run. Simulator: the dispatcher /
        // block-entry request; native: the read-only interrupt page.
        if (!tso && !lse) {
            // A: mov rdi,data; mov rbx,L; L: mov eax,[rdi]; test eax,eax; jnz out;
            //    jmp rbx; out: mov eax,42; hlt. The indirect jump leaves the
            //    block each turn (a tight loop inside one multiblock region never
            //    reaches an entry and is not preemptible, as in the simulator).
            // B: mov rdi,data; mov dword [rdi],1; hlt
            std::array<uint8_t,0x80> program{};
            const uint8_t spin[]={0x48,0xbf,0,0,0,0,4,0,0,0, 0x48,0xbb,0x14,0,0x40,0,0,0,0,0,
                                  0x8b,0x07, 0x85,0xc0, 0x75,0x02, 0xff,0xe3, 0xb8,42,0,0,0, 0xf4};
            const uint8_t store[]={0x48,0xbf,0,0,0,0,4,0,0,0, 0xc7,0x07,1,0,0,0, 0xf4};
            std::copy(std::begin(spin),std::end(spin),program.begin());
            std::copy(std::begin(store),std::end(store),program.begin()+0x40);
            std::array<uint8_t,64> flag{};
            space.ranges={{code,reinterpret_cast<uintptr_t>(program.data()),program.size(),5},
                          {data,reinterpret_cast<uintptr_t>(flag.data()),flag.size(),6}};
            CHECK(space.ValidateGuestMemoryRanges());
#if !defined(VIXL_SIMULATOR)
            FEXCore::MaxPS4::NativePreemptCheck=true;
            struct sigaction action{}, oldSegv{}, oldBus{};
            action.sa_sigaction=preempt_fault; action.sa_flags=SA_SIGINFO; sigemptyset(&action.sa_mask);
            CHECK(sigaction(SIGSEGV,&action,&oldSegv)==0 && sigaction(SIGBUS,&action,&oldBus)==0);
#endif
            ctx->ClearCodeCache(thread,false); ctx->ClearCodeCache(second,false);
            auto run=[&](FEXCore::Core::InternalThreadState* current, uint64_t rip) {
                auto& cpu=current->CurrentFrame->State;
                if (rip) cpu.rip=rip;
                cpu.callret_sp=reinterpret_cast<uintptr_t>(callret.data())+callret.size()/4;
                FEXCore::Core::GuestMemoryFaultData fault{}; cpu.GuestMemoryFaultRecord=&fault;
                std::atomic<bool> done{false};
#if !defined(VIXL_SIMULATOR)
                FEXCore::MaxPS4::ArmPreempt(current,false);
                preempt_running=current;
#endif
                std::thread ticker([&] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    if (done) return;
                    FEXCore::MaxPS4::PreemptRequest=1;
#if !defined(VIXL_SIMULATOR)
                    FEXCore::MaxPS4::ArmPreempt(current,true);
#endif
                });
                ctx->ExecuteThread(current); done=true; ticker.join();
#if !defined(VIXL_SIMULATOR)
                preempt_running=nullptr;
                FEXCore::MaxPS4::ArmPreempt(current,false);
#endif
                FEXCore::MaxPS4::PreemptRequest=0;
                cpu.GuestMemoryFaultRecord=nullptr;
                CHECK(!fault.Bytes);
                return cpu.rip;
            };
            thread->CurrentFrame->State.gregs[FEXCore::X86State::REG_RAX]=0;
            const uint64_t stopped=run(thread,code);
            CHECK(flag[0]==0 && thread->CurrentFrame->State.gregs[FEXCore::X86State::REG_RAX]==0);
            CHECK(stopped>=code && stopped<code+sizeof spin);
            run(second,code+0x40);
            CHECK(flag[0]==1);
            run(thread,0);
            CHECK(thread->CurrentFrame->State.gregs[FEXCore::X86State::REG_RAX]==42);
#if !defined(VIXL_SIMULATOR)
            FEXCore::MaxPS4::NativePreemptCheck=false;
            CHECK(sigaction(SIGSEGV,&oldSegv,nullptr)==0 && sigaction(SIGBUS,&oldBus,nullptr)==0);
#endif
            ctx->ClearCodeCache(thread,false); ctx->ClearCodeCache(second,false);
            space.ranges.clear();
            std::printf("PASS guest spin wait preempted at rip=0x%llx, second thread ran, first resumed to RAX=42\n",(unsigned long long)stopped);
        }
        // Real synthetic SCE PRX -> relocation -> validated staging -> owned
        // translation -> lease-pinned FEX snapshot. No test byte substitution
        // after loading, no direct guest==host dispatch and no application gate.
        {
            auto bytes=orbis_test::fixture(false);
            orbis_test::put(bytes,0x4000,64+16); orbis_test::put(bytes,0x8000,120+16);
            orbis_test::put(bytes,0x4000,0xb20);
            orbis_test::put(bytes,0x8000,0xc00); orbis_test::put(bytes,0x8008,0xc18);
            orbis_test::put(bytes,0x4000,0xc28);
            // mov rdi,0x48010; mov eax,[rdi]; hlt. Guest VA is never a host pointer.
            const uint8_t program[]={0x48,0xbf,0x10,0x80,0x04,0,0,0,0,0,0x8b,0x07,0xf4};
            std::copy(std::begin(program),std::end(program),bytes.begin()+0x400);
            orbis_test::put(bytes,42,0x610,4);
            maxps4::OrbisPRXPreparation prepared;
            CHECK(prepared.prepare({{"synthetic.prx",bytes,0x40000}}));
            maxps4::OrbisPRXFEXBridge bridge;
            CHECK(bridge.map(prepared));
            const uint8_t* denied=reinterpret_cast<const uint8_t*>(1);
            CHECK(bridge.admit_dispatch(0x44000,denied)==-ENOTSUP && !denied);
            auto invalidate=[&] { ctx->ClearCodeCache(thread,false); ctx->ClearCodeCache(second,false); };
            auto owner=bridge.make_translation(invalidate);
            const size_t page=owner->page_size();
            // Keep both reservation guards disjoint on 4 KiB AND 16 KiB
            // hosts. 0x50000 overlaps the PRX guard on Apple ARM64.
            const uint64_t private_stack=0x44000+16*page;
            CHECK(owner->reserve(private_stack,page)==0 && owner->map(private_stack,page,6)==0);
            prepared.unload(); bridge.unload();
            uint64_t relocated=0;
            CHECK(owner->read(0x48000,&relocated,8)==0 && relocated==0x44000);
            for (auto* current : {thread,second,thread}) {
                auto lease=owner->lease(); space.ranges.clear();
                for (const auto& r:lease.ranges()) {
                    CHECK(r.guest!=r.host);
                    space.ranges.push_back({r.guest,r.host,r.size,r.permissions});
                }
                CHECK(space.ValidateGuestMemoryRanges());
                auto& cpu=current->CurrentFrame->State;
                cpu.rip=0x44000; cpu.gregs[FEXCore::X86State::REG_RSP]=private_stack+page-16;
                cpu.gregs[FEXCore::X86State::REG_RAX]=0;
                cpu.callret_sp=reinterpret_cast<uintptr_t>(callret.data())+callret.size()/4;
                FEXCore::Core::GuestMemoryFaultData fault{};
                cpu.GuestMemoryFaultRecord=&fault;
                ctx->ExecuteThread(current);
                cpu.GuestMemoryFaultRecord=nullptr;
                CHECK(cpu.gregs[FEXCore::X86State::REG_RAX]==42 && !fault.Bytes);
                space.ranges.clear();
            }
            owner->close(); // Both threads/context still live; invalidate before unmap.
            CHECK(owner->reserve(0x44000,page)==-ESHUTDOWN);
            std::puts("PASS synthetic SCE PRX relocated, snapshotted and executed by real translated FEX on two threads; loader unloaded, private RW/NX stack, terminal cleanup; direct dispatch still refused");
        }
        // DTPMOD64 -> bounded synthetic module table -> per-thread guest TLS.
        // This proves lease lifetime through FEX, not the commercial Orbis DTV ABI.
        {
            using TLS=maxps4::OrbisV4TLSRelocations;
            const auto libc=mission44_fixture::module(416,1168,16,13);
            const auto fios=mission44_fixture::module(0,64,8,3);
            TLS::Plan plan;std::string reason;
            CHECK(TLS::prepare({{"libc.prx",libc.data(),libc.size(),2},
                {"libSceFios2.prx",fios.data(),fios.size(),3}},plan,reason)==0 && plan.relocated==16);
            auto invalidator=[&]{ctx->ClearCodeCache(thread,false);ctx->ClearCodeCache(second,false);};
            maxps4::OrbisV4Translation first(invalidator),other(invalidator);
            maxps4::OrbisV4TLSMulti::Thread firstTLS,otherTLS;
            CHECK(maxps4::OrbisV4TLSMulti::initialize(first,plan.templates,firstTLS)==0);
            CHECK(maxps4::OrbisV4TLSMulti::initialize(other,plan.templates,otherTLS)==0);
            const uint8_t program[]={0x48,0x8b,0x07,0x48,0x8b,0x04,0xc6,0x8b,0x00,0xf4};
            for(auto* owner:{&first,&other}) {
                const auto page=owner->page_size();
                CHECK(owner->reserve(code,page)==0 && owner->map(code,page,5,program,sizeof(program))==0);
                CHECK(owner->reserve(data,page)==0 && owner->map(data,page,6)==0);
                auto& tls=owner==&first ? firstTLS : otherTLS;
                uint64_t addresses[4]{};
                {auto lease=owner->lease();CHECK(TLS::resolve(lease,tls,2,0,4,addresses[2])==0);
                 CHECK(TLS::resolve(lease,tls,3,0,4,addresses[3])==0);}
                CHECK(owner->write_span(data+0x100,addresses,sizeof(addresses))==0);
            }
            uint64_t firstAddress=0;
            {auto lease=first.lease();CHECK(TLS::resolve(lease,firstTLS,2,0,4,firstAddress)==0);}
            const uint32_t changed=99;CHECK(first.write_span(firstAddress,&changed,4)==0);
            for(size_t index=0;index<2;++index) {
                auto& owner=index ? other : first;
                auto* current=index ? second : thread;
                for(size_t m=0;m<plan.modules.size();++m) {
                    const auto& patch=plan.modules[m].patches.front();
                    CHECK(owner.write_span(data,plan.modules[m].elf.data()+patch.fileOffset,8)==0);
                    invalidator();
                    auto lease=owner.lease();space.ranges.clear();
                    for(const auto& range:lease.ranges())space.ranges.push_back({range.guest,range.host,range.size,range.permissions});
                    CHECK(space.ValidateGuestMemoryRanges());
                    auto& cpu=current->CurrentFrame->State;cpu.rip=code;
                    cpu.gregs[FEXCore::X86State::REG_RSP]=data+owner.page_size()-16;
                    cpu.gregs[FEXCore::X86State::REG_RDI]=data;cpu.gregs[FEXCore::X86State::REG_RSI]=data+0x100;
                    cpu.callret_sp=reinterpret_cast<uintptr_t>(callret.data())+callret.size()/4;
                    FEXCore::Core::GuestMemoryFaultData fault{};cpu.GuestMemoryFaultRecord=&fault;
                    ctx->ExecuteThread(current);cpu.GuestMemoryFaultRecord=nullptr;
                    CHECK(!fault.Bytes && cpu.gregs[FEXCore::X86State::REG_RAX]==(m ? 0U : index ? 42U : 99U));
                    space.ranges.clear();
                }
            }
            first.close();other.close();
            std::puts("PASS FEX DTPMOD64 module IDs 2/3 -> bounded synthetic table -> isolated TLS across two threads, stable lease and invalidation before unmap; no Orbis process admission");
        }
        // Genuine linked TLS relocations and an imported guest __tls_get_addr,
        // following FS -> TCB -> DTV. No host-side synthetic module table.
        // Variant 1 (mission 47) adds the real program-header set: module
        // params, PT_GNU_EH_FRAME views and inert 0x6fffff0x entries.
        // Variant 2 (mission 48) adds the real dynamic-tag set (never executed).
        // Variant 3 (mission 49): the app IMPORTS a libc STT_TLS symbol and its
        // DTPMOD64/DTPOFF64 relocations produce the tls_index the guest passes.
        for(int variant=0;variant<4;++variant) {
            mission45_fixture::Bundle fixture;
            if(variant==1)mission47_fixture::apply(fixture);
            if(variant==2)mission48_fixture::apply(fixture);
            if(variant==3)mission49_fixture::apply(fixture);
            maxps4::OrbisLinker::TLSImage linked;maxps4::OrbisLinker::Report details;std::string error;
            CHECK(maxps4::OrbisLinker::linkTLS(variant==3 ? mission49_fixture::inputs(fixture) : fixture.inputs(),{1,2,3,4},{},linked,error,32*1024*1024,&details));
            CHECK(details.committed && details.tlsRelocations==(variant==3 ? 18U : 16U) && details.resolvedImports==(variant==3 ? 2U : 1U));
            CHECK(details.tlsOffsetRelocations==(variant==3 ? 2U : 0U) && details.tlsSymbols==(variant==3 ? 1U : 0U));
            CHECK(details.moduleParams==(variant ? 2U : 0U) && details.ehFrameHeaders==(variant ? 3U : 0U) && details.opaqueHeaders==(variant ? 5U : 0U));
            CHECK(details.recognisedDynamicTags==(variant>=2 ? 51U : 0U) && details.initArrayEntries==(variant>=2 ? 1U : 0U));
            std::unique_ptr<maxps4::OrbisV4TLSRuntime> runtime;
            CHECK(maxps4::OrbisV4TLSRuntime::load(linked,[&]{ctx->ClearCodeCache(thread,false);ctx->ClearCodeCache(second,false);},runtime,error)==0);
            auto primary=runtime->primary();maxps4::OrbisV4TLSRuntime::Thread secondary;
            CHECK(runtime->create_thread(secondary)==0);
            // Source loader state is dispensable after the runtime owns copies.
            linked=maxps4::OrbisLinker::TLSImage{};
            fixture.app.clear();fixture.libc.clear();fixture.fios.clear();fixture.provider.clear();
            uint64_t fs[2]{};
            for(size_t t=0;t<2;++t) {
                auto lease=runtime->lease(t ? secondary : primary);fs[t]=lease.fs_base();
                uint64_t address=0;CHECK(lease.resolve(2,0,4,address)==0);
                if(!t){const uint32_t change=99;CHECK(lease.memory().write_data(address,&change,4)==0);}
                const uint64_t slots[]={0x808000,0xc08000,0x808000};
                for(size_t m=0;m<3;++m) {
                    uint64_t id=0;CHECK(lease.memory().read_data(slots[m],&id,8)==0 && id==(m==1 ? 3U : 2U));
                    const uint64_t index[]={id,m==2 ? 512U : 0U};
                    if(variant==3 && !t && !m) { // Use the relocation-produced tls_index as is.
                        uint64_t produced[2]{};CHECK(lease.memory().read_data(0x408040,produced,sizeof(produced))==0);
                        CHECK(produced[0]==index[0] && produced[1]==index[1]);
                    } else CHECK(lease.memory().write_data(0x408040,index,sizeof(index))==0);
                    space.ranges.clear();for(const auto& r:lease.memory().ranges()) {
                        CHECK(r.guest!=r.host);space.ranges.push_back({r.guest,r.host,r.size,r.permissions});
                    }
                    CHECK(space.ValidateGuestMemoryRanges());
                    auto* current=t ? second : thread;auto& cpu=current->CurrentFrame->State;
                    cpu.rip=0x404000;cpu.fs_cached=lease.fs_base();cpu.gs_cached=0;
                    cpu.gregs[FEXCore::X86State::REG_RSP]=lease.rsp();
                    cpu.callret_sp=reinterpret_cast<uintptr_t>(callret.data())+callret.size()/4;
                    FEXCore::Core::GuestMemoryFaultData fault{};cpu.GuestMemoryFaultRecord=&fault;
                    ctx->ExecuteThread(current);cpu.GuestMemoryFaultRecord=nullptr;
                    CHECK(!fault.Bytes && cpu.gregs[FEXCore::X86State::REG_RAX]==(m==2 ? 0x804000U : m ? 0U : t ? 42U : 99U));
                    CHECK(cpu.gregs[FEXCore::X86State::REG_RSP]==lease.rsp());
                    space.ranges.clear();
                }
            }
            CHECK(fs[0]!=fs[1]);CHECK(runtime->release_thread(secondary)==0);
            runtime->close();
            std::puts(variant==3 ? "PASS mission49 FEX: guest __tls_get_addr consumes the tls_index produced by DTPMOD64/DTPOFF64 for an imported libc STT_TLS symbol (main thread), then main/secondary TLS as before; unbound weak/global plain names never addressed; no commercial CPU admission"
                    : variant==2 ? "PASS mission48 FEX: same guest TLS/GOT execution with 51 validated dynamic tags (SCE hash, init/fini, arrays, PLTGOT, attributes) recorded but never executed; no commercial CPU admission"
                    : variant ? "PASS mission47 FEX: same guest TLS/GOT execution with real-shaped module params, PT_GNU_EH_FRAME views and inert 0x6fffff0x headers; metadata unmapped beyond PT_LOAD, no unwinder; no commercial CPU admission"
                              : "PASS mission45 FEX: 16 linked DTPMOD64 + NID/GOT __tls_get_addr -> FS/TCB/DTV -> isolated main/secondary TLS, private stack, sealed RELRO, loader freed and invalidation before release; no commercial CPU admission");
        }
        size_t invalidations=0;
        maxps4::OrbisV4Translation memory([&] { ctx->ClearCodeCache(thread,false); ctx->ClearCodeCache(second,false); ++invalidations; });
        const size_t page=memory.page_size();
        // Link actual NIDs/GOT slots before executing imported guest functions.
        for(unsigned profile : {0U,1U,2U}) {
            const bool common=profile==1,errorDialog=profile==2;
            using Dialog=maxps4::OrbisV4ErrorDialog;
            using Audio=maxps4::OrbisV4AudioOut;
            const auto provider=errorDialog ? Dialog::provider() : common ? maxps4::OrbisV4CommonDialog::provider() : Audio::provider();
            const std::string path=errorDialog ? Dialog::path : common ? maxps4::OrbisV4CommonDialog::path : Audio::path;
            const uint64_t bias=errorDialog ? Dialog::bias : common ? maxps4::OrbisV4CommonDialog::bias : Audio::bias;
            const auto app=compat_fixture::consumer(errorDialog ? "libSceErrorDialog" : common ? "libSceCommonDialog" : "libSceAudioOut",path,
                errorDialog ? std::vector<std::string>{Dialog::nids.begin(),Dialog::nids.end()} : common ? std::vector<std::string>{"uoUpLGNkygk"} : std::vector<std::string>{Audio::nids.begin(),Audio::nids.end()});
            maxps4::OrbisLinker::Image image;maxps4::OrbisLinker::Report linked;std::string error;
            CHECK(maxps4::OrbisLinker::link({
                {{{"compat-app.prx",{path}},app.data(),app.size()},0x400000},
                {{{path,{}},provider.data(),provider.size()},bias}}, {},image,error,32*1024*1024,&linked));
            CHECK(linked.committed && linked.resolvedImports==(errorDialog ? 4U : common ? 1U : 5U));
            maxps4::OrbisPRXFEXBridge bridge;CHECK(bridge.map(image));
            auto owner=bridge.make_translation([&]{ctx->ClearCodeCache(thread,false);ctx->ClearCodeCache(second,false);});
            constexpr uint64_t stack=0x700000000ULL;
            CHECK(owner->reserve(stack,page)==0 && owner->map(stack,page,6)==0);
            const uint64_t marker=0x5959595959595959ULL;
            CHECK(owner->write(stack,&marker,8)==0);
            FEXCore::Core::GuestMemoryFaultData fault{};
            auto call=[&](size_t function,const std::array<uint64_t,6>& args,bool alternate=false) {
                auto lease=owner->lease();space.ranges.clear();
                for(const auto& r:lease.ranges()){
                    CHECK(r.guest!=r.host);space.ranges.push_back({r.guest,r.host,r.size,r.permissions});
                }
                auto* current=alternate ? second : thread;auto& cpu=current->CurrentFrame->State;
                const unsigned registers[]={FEXCore::X86State::REG_RDI,FEXCore::X86State::REG_RSI,FEXCore::X86State::REG_RDX,
                    FEXCore::X86State::REG_RCX,FEXCore::X86State::REG_R8,FEXCore::X86State::REG_R9};
                for(size_t i=0;i<6;++i)cpu.gregs[registers[i]]=args[i];
                cpu.rip=0x401000+function*32;cpu.gregs[FEXCore::X86State::REG_RSP]=stack+page-16;
                cpu.callret_sp=reinterpret_cast<uintptr_t>(callret.data())+callret.size()/4;
                fault={};cpu.GuestMemoryFaultRecord=&fault;ctx->ExecuteThread(current);cpu.GuestMemoryFaultRecord=nullptr;
                space.ranges.clear();return cpu.gregs[FEXCore::X86State::REG_RAX];
            };
            if(errorDialog) {
                auto status=[&](uint32_t expected){CHECK(call(3,{})==expected && !fault.Bytes);};
                status(0);
                CHECK(call(2,{})==Dialog::not_initialized && !fault.Bytes);
                CHECK(call(1,{UINT64_MAX})==Dialog::invalid_state && !fault.Bytes);
                CHECK(owner->protect(bias+0x8000,4,4)==0);
                call(0,{});CHECK(fault.Address==bias+0x8000 && fault.Permissions==6 && fault.Bytes==4);
                status(0);
                CHECK(owner->protect(bias+0x8000,4,6)==0);
                CHECK(call(0,{})==0 && !fault.Bytes);status(1);
                CHECK(call(0,{},true)==Dialog::already_initialized && !fault.Bytes);
                CHECK(call(1,{})==Dialog::arg_null && !fault.Bytes);status(1);
                const uint64_t param=stack+32;
                std::array<uint32_t,4> args{16,0x80000001,0xffffffff,0};
                CHECK(owner->write(param,args.data(),16)==0);
                for(unsigned repeat=0;repeat<3;++repeat){
                    CHECK(call(1,{param},repeat%2)==Dialog::not_supported && !fault.Bytes);status(1);
                }
                std::array<uint32_t,4> after{};
                CHECK(owner->read(param,after.data(),16)==0 && after==args);
                args[0]=15;CHECK(owner->write(param,args.data(),16)==0);
                CHECK(call(1,{param})==Dialog::param_invalid && !fault.Bytes);status(1);
                args[0]=16;args[3]=1;CHECK(owner->write(param,args.data(),16)==0);
                CHECK(call(1,{param})==Dialog::param_invalid && !fault.Bytes);status(1);
                args[3]=0;CHECK(owner->write(param,args.data(),16)==0);
                // Parameters isolated from CALL/RET stack: RO reads succeed,
                // NX is preserved; a hole/partial structure faults without state changes.
                const uint64_t ro=stack+16*page;
                CHECK(owner->reserve(ro,page)==0 && owner->map(ro,page,6)==0);
                CHECK(owner->write(ro+1,args.data(),16)==0 && owner->protect(ro,page,4)==0);
                CHECK(call(1,{ro+1})==Dialog::not_supported && !fault.Bytes);status(1);
                call(1,{ro+page-8});CHECK(fault.Bytes && fault.Permissions==4);status(1);
                call(1,{UINT64_MAX});CHECK(fault.Bytes && fault.Permissions==4);status(1);
                CHECK(owner->protect(bias+0x8000,4,4)==0);
                call(2,{});CHECK(fault.Address==bias+0x8000 && fault.Permissions==6);status(1);
                CHECK(owner->protect(bias+0x8000,4,6)==0);
                CHECK(call(2,{},true)==0 && !fault.Bytes);status(0);
                CHECK(call(2,{})==Dialog::not_initialized && !fault.Bytes);
                CHECK(call(0,{})==0 && !fault.Bytes);status(1);
                CHECK(call(2,{})==0 && !fault.Bytes);status(0);
                std::puts("PASS ErrorDialog four real GOT calls: atomic initialize/terminate, truthful NONE/INITIALIZED, Open validation/NOT_SUPPORTED, RO/unaligned/hole/overflow, no fake UI completion");
            } else {
            const size_t init=common ? 0 : 1;
            if(!common)for(size_t fn:{0U,2U,3U,4U})CHECK(call(fn,{})==Audio::not_init && !fault.Bytes);
            CHECK(owner->protect(bias+0x8000,4,4)==0);
            call(init,{});CHECK(fault.Address==bias+0x8000 && fault.Bytes==4 && fault.Permissions==6);
            uint32_t initialized=99;CHECK(owner->read(bias+0x8000,&initialized,4)==0 && !initialized);
            CHECK(owner->protect(bias+0x8000,4,6)==0);
            CHECK(call(init,{})==0 && !fault.Bytes);
            CHECK(call(init,{},true)==(common ? maxps4::OrbisV4CommonDialog::already_initialized : Audio::already_init));
            if(!common) {
                std::array<uint64_t,6> args{1,0,0,256,48000,1};
                for(unsigned repeat=0;repeat<3;++repeat)CHECK(call(2,args)==Audio::backend_unavailable && !fault.Bytes);
                for(uint64_t length:{0U,1U,255U,257U,4096U,0xffffffffU}){
                    args[3]=length;CHECK(call(2,args)==Audio::invalid_size && !fault.Bytes);
                }
                args={1,5,0,256,48000,1};CHECK(call(2,args)==Audio::invalid_type);
                args={1,0,0,256,44100,1};CHECK(call(2,args)==Audio::invalid_frequency);
                args={1,0,0,256,48000,8};CHECK(call(2,args)==Audio::invalid_format);
                args={1,0x100000000ULL,0,0x100000100ULL,0x10000bb80ULL,0x100000001ULL};
                CHECK(call(2,args)==Audio::backend_unavailable && !fault.Bytes);
                for(size_t fn:{0U,3U,4U}){
                    for(uint64_t handle:{0U,0xffffffffU,0x20000019U})
                        CHECK(call(fn,{handle,UINT64_MAX,UINT64_MAX,0,0,0})==Audio::invalid_port && !fault.Bytes);
                    CHECK(call(fn,{0x20050000,0,0,0,0,0})==Audio::invalid_type && !fault.Bytes);
                    CHECK(call(fn,{0x20000000,UINT64_MAX,UINT64_MAX,0,0,0})==Audio::not_opened && !fault.Bytes);
                    CHECK(call(fn,{0x20000000,stack,stack,0,0,0})==Audio::not_opened && !fault.Bytes);
                }
                uint64_t unchanged=0;CHECK(owner->read(stack,&unchanged,8)==0 && unchanged==marker);
            }
            }
            owner->close();owner.reset();bridge.unload();
            if(!errorDialog)std::puts(common ? "PASS CommonDialog imported Initialize: real GOT call, atomic state, duplicate/thread isolation and RO fault" :
                "PASS AudioOut five imported functions: real GOT calls, atomic init, ABI validation, unavailable backend, no fake handle/PCM success, buffers untouched");
        }
        // Offline NpManager and actual CPU LibcInternal byte operations.
        for(bool libc : {false,true}){
            using Manager=maxps4::OrbisV4NpManager;using Libc=maxps4::OrbisV4LibcInternal;
            const auto provider=libc ? Libc::provider() : Manager::provider();
            const std::string path=libc ? Libc::path : Manager::path;
            const uint64_t bias=libc ? Libc::bias : Manager::bias;
            const std::vector<std::string> nids=libc ? std::vector<std::string>{Libc::nids.begin(),Libc::nids.end()} :
                std::vector<std::string>{Manager::nids.begin(),Manager::nids.end()};
            const auto app=compat_fixture::consumer(libc ? "libSceLibcInternal" : "libSceNpManager",path,nids);
            maxps4::OrbisLinker::Image image;maxps4::OrbisLinker::Report linked;std::string error;
            CHECK(maxps4::OrbisLinker::link({
                {{{"np-app.prx",{path}},app.data(),app.size()},0x400000},
                {{{path,{}},provider.data(),provider.size()},bias}}, {},image,error,32*1024*1024,&linked));
            CHECK(linked.committed && linked.resolvedImports==nids.size());
            maxps4::OrbisPRXFEXBridge bridge;CHECK(bridge.map(image));
            auto owner=bridge.make_translation([&]{ctx->ClearCodeCache(thread,false);ctx->ClearCodeCache(second,false);});
            constexpr uint64_t stack=0x700000000ULL;
            const uint64_t output=stack+16*page;
            CHECK(owner->reserve(stack,page)==0 && owner->map(stack,page,6)==0);
            CHECK(owner->reserve(output,2*page)==0 && owner->map(output,page,6)==0 && owner->map(output+page,page,6)==0);
            FEXCore::Core::GuestMemoryFaultData fault{};
            auto call=[&](size_t fn,const std::array<uint64_t,6>& args,bool alternate=false){
                auto lease=owner->lease();space.ranges.clear();
                for(const auto& r:lease.ranges())space.ranges.push_back({r.guest,r.host,r.size,r.permissions});
                auto* current=alternate ? second : thread;auto& cpu=current->CurrentFrame->State;
                const unsigned regs[]={FEXCore::X86State::REG_RDI,FEXCore::X86State::REG_RSI,FEXCore::X86State::REG_RDX,
                    FEXCore::X86State::REG_RCX,FEXCore::X86State::REG_R8,FEXCore::X86State::REG_R9};
                for(size_t i=0;i<6;++i)cpu.gregs[regs[i]]=args[i];
                cpu.rip=0x401000+fn*32;cpu.gregs[FEXCore::X86State::REG_RSP]=stack+page-16;
                cpu.callret_sp=reinterpret_cast<uintptr_t>(callret.data())+callret.size()/4;
                fault={};cpu.GuestMemoryFaultRecord=&fault;ctx->ExecuteThread(current);cpu.GuestMemoryFaultRecord=nullptr;
                space.ranges.clear();return cpu.gregs[FEXCore::X86State::REG_RAX];
            };
            if(!libc){
                std::array<uint8_t,48> sentinel;sentinel.fill(0xa5);
                CHECK(owner->write(output,sentinel.data(),sentinel.size())==0);
                for(size_t fn=0;fn<3;++fn){
                    CHECK(call(fn,{0,0})==Manager::invalid_argument && !fault.Bytes);
                    CHECK(call(fn,{0xffffffffU,output})==Manager::invalid_argument && !fault.Bytes);
                    CHECK(call(fn,{0x1ffffffffULL,output})==Manager::invalid_argument && !fault.Bytes);
                }
                for(size_t fn:{1U,2U}){
                    CHECK(call(fn,{0,output},true)==Manager::signed_out && !fault.Bytes);
                    CHECK(call(fn,{0,UINT64_MAX})==Manager::signed_out && !fault.Bytes);
                }
                std::array<uint8_t,48> actual{};CHECK(owner->read(output,actual.data(),actual.size())==0 && actual==sentinel);
                CHECK(call(0,{0x100000000ULL,output+1})==0 && !fault.Bytes);
                CHECK(owner->read(output,actual.data(),actual.size())==0);
                CHECK(actual[0]==0xa5 && actual[1]==1 && actual[2]==0 && actual[3]==0 && actual[4]==0 && actual[5]==0xa5);
                CHECK(owner->protect(output,page,4)==0);
                call(0,{0,output+1});CHECK(fault.Address==output+1 && (fault.Permissions&2));
                std::array<uint8_t,48> retained{};CHECK(owner->read(output,retained.data(),retained.size())==0 && retained==actual);
                CHECK(call(1,{0,output})==Manager::signed_out && !fault.Bytes);
                call(0,{0,UINT64_MAX});CHECK(fault.Bytes && (fault.Permissions&2));
                call(0,{0,bias+0x4000});CHECK(fault.Bytes && (fault.Permissions&2));
                std::puts("PASS NpManager real GOT: offline SignedOut=1 query, scalar ABI, invalid arguments, ID failures preserve outputs, RO/RX/overflow; no initialization or fabricated account");
            }else{
                const uint64_t source=stack+32;
                std::array<uint8_t,64> pattern{},before{};before.fill(0x55);
                for(size_t i=0;i<pattern.size();++i)pattern[i]=uint8_t(i*3+1);
                CHECK(owner->write(source,pattern.data(),pattern.size())==0);
                CHECK(owner->write(output,before.data(),before.size())==0);
                CHECK(call(0,{output+3,0x1234ab,37})==output+3 && !fault.Bytes);
                std::array<uint8_t,64> actual{};CHECK(owner->read(output,actual.data(),actual.size())==0);
                for(size_t i=0;i<actual.size();++i)CHECK(actual[i]==(i>=3 && i<40 ? 0xab : 0x55));
                CHECK(call(1,{output+1,source+1,59},true)==output+1 && !fault.Bytes);
                CHECK(owner->read(output,actual.data(),actual.size())==0);
                CHECK(std::equal(actual.begin()+1,actual.begin()+60,pattern.begin()+1) && actual[0]==0x55 && actual[60]==0x55);
                CHECK(call(0,{UINT64_MAX,0,0})==UINT64_MAX && !fault.Bytes);
                CHECK(call(1,{0,UINT64_MAX,0})==0 && !fault.Bytes);
                const uint64_t ro=output+page+128;
                CHECK(owner->write(ro,pattern.data(),pattern.size())==0 && owner->protect(output+page,page,4)==0);
                CHECK(call(1,{output,ro,64})==output && !fault.Bytes);
                CHECK(owner->read(output,actual.data(),actual.size())==0 && actual==pattern);
                CHECK(owner->protect(output,page,4)==0);
                for(size_t fn=0;fn<2;++fn){call(fn,{output,fn ? source : 7,16});CHECK(fault.Address==output && fault.Permissions==6);}
                CHECK(owner->read(output,actual.data(),actual.size())==0 && actual==pattern);
                CHECK(owner->protect(output,page,6)==0 && owner->protect(output+page,page,6)==0);
                const uint64_t crossing=output+page-8;
                std::array<uint8_t,16> boundary;boundary.fill(0x59);
                CHECK(owner->write(crossing,boundary.data(),boundary.size())==0 && owner->protect(output+page,page,4)==0);
                for(size_t fn=0;fn<2;++fn){
                    call(fn,{crossing,fn ? source : 7,16});CHECK(fault.Address==output+page && fault.Permissions==6);
                    std::array<uint8_t,16> retained{};CHECK(owner->read(crossing,retained.data(),retained.size())==0 && retained==boundary);
                }
                // Late source hole: already-probed destination bytes stay intact.
                const uint64_t hole=output+2*page-1;
                call(1,{output,hole,0x100000000ULL});CHECK(fault.Bytes && fault.Permissions==4);
                CHECK(owner->read(output,actual.data(),actual.size())==0 && actual==pattern);
                call(0,{crossing,7,0x100000000ULL});CHECK(fault.Bytes && fault.Permissions==6);
                call(0,{UINT64_MAX,7,1});CHECK(fault.Bytes && fault.Permissions==6);
                call(1,{output,UINT64_MAX,1});CHECK(fault.Bytes && fault.Permissions==4);
                for(size_t fn=0;fn<2;++fn){call(fn,{bias+0x4000,fn ? source : 7,8});CHECK(fault.Bytes && fault.Permissions==6);}
                std::array<uint8_t,8> codeBytes{},copied{};CHECK(owner->read(bias+0x4000,codeBytes.data(),8)==0);
                CHECK(call(1,{output,bias+0x4000,8})==output && !fault.Bytes);
                CHECK(owner->read(output,copied.data(),8)==0 && copied==codeBytes);
                CHECK(owner->read(source,actual.data(),actual.size())==0 && actual==pattern);
                std::puts("PASS LibcInternal real GOT memcpy/memset: actual bytes/returns, truncation and 64-bit size, unaligned/RO source, guards, zero length, late RO/hole rollback, RX/overflow and no host pointers");
            }
            owner->close();owner.reset();bridge.unload();
        }
        // Local services: qualified linker imports and translated guest code.
        for(size_t mi=0;mi<maxps4::OrbisV4LocalServices::modules().size();++mi){
            const auto module=maxps4::OrbisV4LocalServices::modules()[mi];
            for(size_t first=0;first<module.functions.size();first+=4){
                const size_t count=std::min(size_t(4),module.functions.size()-first);
                std::vector<std::string> nids;
                for(size_t j=0;j<count;++j)nids.push_back(module.functions[first+j].nid);
                auto app=compat_fixture::consumer(module.name,module.path(),nids);
                // The fixture imports module version 0x101; use the provider's own
                // (VideoOut and NpToolkit2 are 0, ScreenShot 9).
                if(module.version!=0x101){
                    maxps4::OrbisMetadata m;CHECK(m.parse(app.data(),app.size()));
                    orbis_fixture::setTag(app,0x6100000f,1|(uint64_t(module.version)<<32)|(uint64_t{1}<<48));
                }
                const auto provider=maxps4::OrbisV4LocalServices::provider(module);
                maxps4::OrbisLinker::Image image;std::string error;
                CHECK(maxps4::OrbisLinker::link({
                    {{{"local-app.prx",{module.path()}},app.data(),app.size()},0x400000},
                    {{{module.path(),{}},provider.data(),provider.size()},maxps4::OrbisV4LocalServices::bias(mi)}}, {},image,error));
                maxps4::OrbisPRXFEXBridge bridge;CHECK(bridge.map(image));
                auto owner=bridge.make_translation([&]{ctx->ClearCodeCache(thread,false);ctx->ClearCodeCache(second,false);});
                constexpr uint64_t stack=0x700000000ULL;const uint64_t output=stack+16*page;
                CHECK(owner->reserve(stack,page)==0 && owner->map(stack,page,6)==0);
                CHECK(owner->reserve(output,page)==0 && owner->map(output,page,6)==0);
                FEXCore::Core::GuestMemoryFaultData fault{};
                auto call=[&](size_t fn,std::array<uint64_t,6> args,uint64_t pitch=0){
                    CHECK(owner->write(stack+page-16,&pitch,8)==0);
                    auto lease=owner->lease();space.ranges.clear();
                    for(const auto& r:lease.ranges())space.ranges.push_back({r.guest,r.host,r.size,r.permissions});
                    auto& cpu=thread->CurrentFrame->State;
                    const unsigned regs[]={FEXCore::X86State::REG_RDI,FEXCore::X86State::REG_RSI,FEXCore::X86State::REG_RDX,
                        FEXCore::X86State::REG_RCX,FEXCore::X86State::REG_R8,FEXCore::X86State::REG_R9};
                    for(size_t j=0;j<6;++j)cpu.gregs[regs[j]]=args[j];
                    cpu.rip=0x401000+fn*32;cpu.gregs[FEXCore::X86State::REG_RSP]=stack+page-16;
                    cpu.callret_sp=reinterpret_cast<uintptr_t>(callret.data())+callret.size()/4;
                    fault={};cpu.GuestMemoryFaultRecord=&fault;ctx->ExecuteThread(thread);cpu.GuestMemoryFaultRecord=nullptr;
                    space.ranges.clear();return cpu.gregs[FEXCore::X86State::REG_RAX];
                };
                std::array<uint8_t,64> sentinel;sentinel.fill(0xa5);
                CHECK(owner->write(output,sentinel.data(),sentinel.size())==0);
                for(size_t j=0;j<count;++j){const std::string name=module.functions[first+j].name;
                    if(name=="getpagesize"){CHECK(call(j,{})==16384 && !fault.Bytes);}
                    else if(name=="scePthreadEqual"){
                        CHECK(call(j,{0x123400000001ULL,0x123400000001ULL})==1 && !fault.Bytes);
                        CHECK(call(j,{0x123400000001ULL,0x223400000001ULL})==0 && !fault.Bytes);
                        CHECK(call(j,{UINT64_MAX,UINT64_MAX})==1 && !fault.Bytes);
                    }
                    else if(name=="scePadInit"){CHECK(call(j,{})==0 && !fault.Bytes);CHECK(call(j,{})==0 && !fault.Bytes);}
                    else if(name=="sceUserServiceInitialize"){
                        CHECK(call(j,{output})==0x80960006 && !fault.Bytes);
                        CHECK(call(j,{})==0 && !fault.Bytes);CHECK(call(j,{})==0x80960003 && !fault.Bytes);
                        CHECK(call(0,{output})==0x80960009 && !fault.Bytes);
                        CHECK(call(0,{0})==0x80960005 && !fault.Bytes);
                    }else if(name=="sceUserServiceTerminate"){
                        CHECK(call(j,{})==0 && !fault.Bytes);CHECK(call(j,{})==0x80960002 && !fault.Bytes);
                    }else if(name=="sceSaveDataDialogInitialize"){
                        CHECK(call(j,{})==0x80b8000f && !fault.Bytes);CHECK(call(j,{})==0x80b8000f && !fault.Bytes);
                    }else if(name=="sceSaveDataDialogTerminate"){
                        CHECK(call(j,{})==0x80b80003 && !fault.Bytes);CHECK(call(j,{})==0x80b80003 && !fault.Bytes);
                    }else if(name=="sceSaveDataDialogUpdateStatus"){CHECK(call(j,{})==0 && !fault.Bytes);}
                    else if(name=="scePngDecParseHeader"){
                        const uint8_t png[]={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,0,0,0,11,73,68,65,84,120,156,99,96,0,2,0,0,5,0,1,122,94,171,63,0,0,0,0,73,69,78,68,174,66,96,130};
                        const uint64_t input=output+128,param=output+256;
                        CHECK(owner->write(input,png,sizeof(png))==0);
                        std::array<uint64_t,2> fields{input,sizeof(png)};
                        CHECK(owner->write(param,fields.data(),16)==0);
                        CHECK(call(j,{param,output+1})==0 && !fault.Bytes);
                        std::array<uint32_t,4> info{};CHECK(owner->read(output+1,info.data(),16)==0);
                        CHECK(info[0]==1 && info[1]==1 && info[2]==(19U|(8U<<16)) && info[3]==0);
                        CHECK(owner->write(output,sentinel.data(),64)==0);
                        fields[1]=32;CHECK(owner->write(param,fields.data(),16)==0);
                        CHECK(call(j,{param,output})==0x80690002 && !fault.Bytes);
                        fields[1]=sizeof(png);CHECK(owner->write(param,fields.data(),16)==0);
                        uint8_t corrupt=0;CHECK(owner->write(input+32,&corrupt,1)==0);
                        CHECK(call(j,{param,output})==0x80690010 && !fault.Bytes);
                        CHECK(owner->write(input,png,sizeof(png))==0);
                        std::array<uint8_t,8> tail;tail.fill(0xa5);
                        CHECK(owner->write(output+page-8,tail.data(),tail.size())==0);
                        call(j,{param,output+page-8});CHECK(fault.Bytes);
                        std::array<uint8_t,8> after{};CHECK(owner->read(output+page-8,after.data(),after.size())==0 && after==tail);
                        CHECK(owner->protect(output,page,4)==0);
                        call(j,{param,output});CHECK(fault.Bytes);
                        CHECK(owner->protect(output,page,6)==0);
                        CHECK(call(j,{0,output})==0x80690001 && !fault.Bytes);
                    }else if(name=="sceVideoOutSetBufferAttribute"){
                        call(j,{output+1,0x80002200,1,0,320,240},336);CHECK(!fault.Bytes);
                        std::array<uint32_t,10> attr{};CHECK(owner->read(output+1,attr.data(),40)==0);
                        const std::array<uint32_t,10> expected{0x80002200,1,0,320,240,336,0,0,0,0};CHECK(attr==expected);
                        CHECK(owner->write(output,sentinel.data(),sentinel.size())==0);
                        std::array<uint8_t,20> tail;tail.fill(0xa5);
                        CHECK(owner->write(output+page-20,tail.data(),tail.size())==0);
                        call(j,{output+page-20,0,0,0,1,1},1);CHECK(fault.Bytes);
                        std::array<uint8_t,20> after{};CHECK(owner->read(output+page-20,after.data(),after.size())==0 && after==tail);
                        CHECK(owner->read(output,attr.data(),40)==0);
                        CHECK(owner->protect(output,page,4)==0);
                        call(j,{output,0,0,0,1,1},1);CHECK(fault.Bytes);
                        CHECK(owner->protect(output,page,6)==0);
                    }else if(name=="sceScreenShotSetOverlayImageWithOrigin" || name=="module_start"){
                        // Succeed without any effect (no screenshot feature; nothing to start).
                        CHECK(call(j,{output,output,output,output,output,output})==0 && !fault.Bytes);
                    }else{
                        const uint64_t r=call(j,{output,output,output,output,output,output});
                        CHECK(!fault.Bytes && (r&0x80000000));
                    }
                }
                std::array<uint8_t,64> actual{};CHECK(owner->read(output,actual.data(),64)==0 && actual==sentinel);
                owner->close();owner.reset();bridge.unload();
            }
        }
        std::puts("PASS ten local providers through FEX/VIXL: software lifetime, explicit refusals, VideoOut ABI v0/7th stack arg/unaligned writes/RO/hole faults, no fabricated devices or IO");

        // NpTrophy is an explicit refusal backend: no fake handles or results.
        for(size_t first=0;first<8;first+=4){
            using T=maxps4::OrbisV4NpTrophy;
            const auto provider=T::provider();
            const auto app=compat_fixture::consumer("libSceNpTrophy",T::path,{T::nids.begin()+first,T::nids.begin()+first+4});
            maxps4::OrbisLinker::Image image;maxps4::OrbisLinker::Report linked;std::string error;
            CHECK(maxps4::OrbisLinker::link({
                {{{"trophy-app.prx",{T::path}},app.data(),app.size()},0x400000},
                {{{T::path,{}},provider.data(),provider.size()},T::bias}}, {},image,error,32*1024*1024,&linked));
            CHECK(linked.committed && linked.resolvedImports==4);
            maxps4::OrbisPRXFEXBridge bridge;CHECK(bridge.map(image));
            auto owner=bridge.make_translation([&]{ctx->ClearCodeCache(thread,false);ctx->ClearCodeCache(second,false);});
            constexpr uint64_t stack=0x700000000ULL;
            const uint64_t output=stack+16*page;
            CHECK(owner->reserve(stack,page)==0 && owner->map(stack,page,6)==0);
            CHECK(owner->reserve(output,page)==0 && owner->map(output,page,6)==0);
            FEXCore::Core::GuestMemoryFaultData fault{};
            auto call=[&](size_t fn,const std::array<uint64_t,6>& args,bool alternate=false){
                auto lease=owner->lease();space.ranges.clear();
                for(const auto& r:lease.ranges())space.ranges.push_back({r.guest,r.host,r.size,r.permissions});
                auto* current=alternate ? second : thread;auto& cpu=current->CurrentFrame->State;
                const unsigned regs[]={FEXCore::X86State::REG_RDI,FEXCore::X86State::REG_RSI,FEXCore::X86State::REG_RDX,
                    FEXCore::X86State::REG_RCX,FEXCore::X86State::REG_R8,FEXCore::X86State::REG_R9};
                for(size_t i=0;i<6;++i)cpu.gregs[regs[i]]=args[i];
                cpu.rip=0x401000+fn*32;cpu.gregs[FEXCore::X86State::REG_RSP]=stack+page-16;
                cpu.callret_sp=reinterpret_cast<uintptr_t>(callret.data())+callret.size()/4;
                fault={};cpu.GuestMemoryFaultRecord=&fault;ctx->ExecuteThread(current);cpu.GuestMemoryFaultRecord=nullptr;
                space.ranges.clear();return cpu.gregs[FEXCore::X86State::REG_RAX];
            };
            std::array<uint8_t,64> sentinel;sentinel.fill(0xa5);
            CHECK(owner->write(output,sentinel.data(),sentinel.size())==0);
            auto expect=[&](size_t index,const std::array<uint64_t,6>& args,uint32_t error){
                CHECK(index>=first && index<first+4);
                for(bool alternate:{false,true})CHECK(call(index-first,args,alternate)==error && !fault.Bytes);
            };
            if(first==0){
                expect(0,{output,0,0,0},T::not_supported);
                expect(0,{0,0,0,0},T::invalid_argument);
                expect(0,{output,0,0,1},T::invalid_argument);
                expect(0,{output,0,0,0x100000000ULL},T::invalid_argument); // uint64 options
                expect(0,{UINT64_MAX,0,0,0},T::not_supported);
                expect(3,{output},T::not_supported);expect(3,{},T::invalid_argument);
                expect(3,{UINT64_MAX},T::not_supported);
                for(uint64_t id:{0ULL,1ULL,0xffffffffULL,0x100000001ULL}){
                    expect(1,{id},T::invalid_context);expect(2,{id},T::invalid_handle);
                }
            }else{
                for(uint64_t id:{0ULL,1ULL,0xffffffffULL,0x100000001ULL})expect(4,{id},T::invalid_handle);
                expect(5,{1,1,output,output+32},T::invalid_context);
                expect(5,{1,1,0,output},T::invalid_argument);
                expect(5,{1,1,output,0},T::invalid_argument);
                expect(5,{0xffffffffU,0xffffffffU,output,output},T::invalid_context);
                expect(5,{1,0xffffffffU,output,output},T::invalid_handle);
                expect(5,{1,1,UINT64_MAX,UINT64_MAX},T::invalid_context);
                expect(6,{1,1,0},T::invalid_context);
                expect(6,{1,1,1},T::invalid_argument);
                expect(6,{1,1,0x100000000ULL},T::invalid_argument);
                expect(6,{0xffffffffU,0xffffffffU,0},T::invalid_context);
                expect(6,{1,0xffffffffU,0},T::invalid_handle);
                expect(7,{1,1,0,output},T::invalid_context);
                expect(7,{1,1,0x100000000ULL,output},T::invalid_context); // int32 trophy id
                expect(7,{1,1,128,output},T::invalid_trophy_id);
                expect(7,{1,1,0xffffffffU,output},T::invalid_trophy_id);
                expect(7,{1,1,0,0},T::invalid_argument);
                expect(7,{0xffffffffU,1,0,output},T::invalid_context);
                expect(7,{1,0xffffffffU,0,output},T::invalid_handle);
                expect(7,{1,1,0,UINT64_MAX},T::invalid_context);
            }
            std::array<uint8_t,64> actual{};
            CHECK(owner->read(output,actual.data(),actual.size())==0 && actual==sentinel);
            CHECK(owner->protect(output,page,4)==0);
            if(first==0){expect(0,{output,0,0,0},T::not_supported);expect(3,{output},T::not_supported);}
            else {expect(5,{1,1,output,output+32},T::invalid_context);expect(7,{1,1,0,output},T::invalid_context);}
            CHECK(owner->read(output,actual.data(),actual.size())==0 && actual==sentinel);
            const uint8_t* code=nullptr;
            {auto lease=owner->lease();CHECK(lease.resolve_contiguous(T::bias+0x4000,1,5,code)==0 && !lease.permitted(T::bias+0x4000,1,2));}
            // RX, unaligned and crossing-hole outputs are never touched on refusal.
            if(first==0){expect(0,{T::bias+0x4000,0,0,0},T::not_supported);expect(3,{output+page-1},T::not_supported);}
            else {expect(5,{1,1,output+1,output+page-1},T::invalid_context);expect(7,{1,1,0,T::bias+0x4000},T::invalid_context);}
            owner->close();owner.reset();bridge.unload();
        }
        std::puts("PASS NpTrophy eight real GOT calls: truthful unsupported/invalid objects, null/scalar/64-bit options, RO/RX/hole outputs untouched, no success/handle/context/trophy/account/PSN state");
        // Offline NpCommon utility contracts: no account/session exists.
        {
            using NP=maxps4::OrbisV4NpCommon;
            const auto provider=NP::provider();
            const auto app=compat_fixture::consumer("libSceNpCommon",NP::path,
                {NP::nids.begin(),NP::nids.end()});
            maxps4::OrbisLinker::Image image;maxps4::OrbisLinker::Report linked;std::string error;
            CHECK(maxps4::OrbisLinker::link({
                {{{"np-app.prx",{NP::path}},app.data(),app.size()},0x400000},
                {{{NP::path,{}},provider.data(),provider.size()},NP::bias}}, {},image,error,32*1024*1024,&linked));
            CHECK(linked.committed && linked.resolvedImports==2);
            maxps4::OrbisPRXFEXBridge bridge;CHECK(bridge.map(image));
            auto owner=bridge.make_translation([&]{ctx->ClearCodeCache(thread,false);ctx->ClearCodeCache(second,false);});
            constexpr uint64_t stack=0x700000000ULL;
            const uint64_t output=stack+16*page;
            CHECK(owner->reserve(stack,page)==0 && owner->map(stack,page,6)==0);
            CHECK(owner->reserve(output,page)==0 && owner->map(output,page,6)==0);
            FEXCore::Core::GuestMemoryFaultData fault{};
            auto call=[&](size_t fn,const std::array<uint64_t,6>& args,bool alternate=false){
                auto lease=owner->lease();space.ranges.clear();
                for(const auto& r:lease.ranges())space.ranges.push_back({r.guest,r.host,r.size,r.permissions});
                auto* current=alternate ? second : thread;auto& cpu=current->CurrentFrame->State;
                const unsigned regs[]={FEXCore::X86State::REG_RDI,FEXCore::X86State::REG_RSI,FEXCore::X86State::REG_RDX,
                    FEXCore::X86State::REG_RCX,FEXCore::X86State::REG_R8,FEXCore::X86State::REG_R9};
                for(size_t i=0;i<6;++i)cpu.gregs[regs[i]]=args[i];
                cpu.rip=0x401000+fn*32;cpu.gregs[FEXCore::X86State::REG_RSP]=stack+page-16;
                cpu.callret_sp=reinterpret_cast<uintptr_t>(callret.data())+callret.size()/4;
                fault={};cpu.GuestMemoryFaultRecord=&fault;ctx->ExecuteThread(current);cpu.GuestMemoryFaultRecord=nullptr;
                space.ranges.clear();return cpu.gregs[FEXCore::X86State::REG_RAX];
            };
            std::array<uint8_t,36> left{},right{};
            left[0]=right[0]='a';left[1]=right[1]='b';left[2]=right[2]='c';
            auto store=[&]{CHECK(owner->write(output,left.data(),left.size())==0);
                CHECK(owner->write(output+64,right.data(),right.size())==0);};
            store();
            for(size_t fn=0;fn<2;++fn){
                CHECK(call(fn,{0,output+64})==NP::invalid_argument && !fault.Bytes);
                CHECK(call(fn,{output,0})==NP::invalid_argument && !fault.Bytes);
                CHECK(call(fn,{output,output+64})==0 && !fault.Bytes);
            }
            right[4]='x';right[16]=0x55;right[19]=0x77;store();
            CHECK(call(0,{output,output+64})==0 && !fault.Bytes);
            CHECK(call(1,{output,output+64},true)==0 && !fault.Bytes); // ignores bytes after NUL and term/dummy
            for(size_t off=20;off<36;++off){
                right[off]=1;store();CHECK(call(1,{output,output+64})==NP::not_match && !fault.Bytes);right[off]=0;
            }
            right[1]='z';store();
            CHECK(call(0,{output,output+64})==NP::not_match && !fault.Bytes);
            CHECK(call(1,{output,output+64})==NP::not_match && !fault.Bytes);
            // Deterministic cross-check against bounded string + byte equality.
            for(unsigned sample=0;sample<32;++sample){
                for(size_t i=0;i<36;++i)left[i]=right[i]=uint8_t(0x80+((sample+i)%0x70));
                if(sample%2)left[sample%16]=right[sample%16]=0;
                right[sample%36]^=1;store();
                bool same=true;
                for(size_t i=0;i<16;++i){if(left[i]!=right[i]){same=false;break;}if(!left[i])break;}
                CHECK(call(0,{output,output+64})==(same ? 0 : NP::not_match) && !fault.Bytes);
                same= same && std::equal(left.begin()+20,left.end(),right.begin()+20);
                CHECK(call(1,{output,output+64},sample%2)==(same ? 0 : NP::not_match) && !fault.Bytes);
            }
            left.fill('q');right=left;store();
            CHECK(owner->protect(output,page,4)==0);
            CHECK(call(0,{output,output+64})==0 && !fault.Bytes);
            CHECK(call(1,{output,output+64})==0 && !fault.Bytes);
            CHECK(call(0,{output+1,output+65})==0 && !fault.Bytes); // unaligned read-only input
            CHECK(owner->protect(output,page,6)==0);
            CHECK(owner->write(output+page-16,left.data(),16)==0);
            CHECK(call(0,{output+page-16,output+page-16})==0 && !fault.Bytes); // exactly 16 readable bytes
            call(1,{output+page-16,output+page-16});CHECK(fault.Bytes && fault.Permissions==4);
            call(0,{UINT64_MAX,output});CHECK(fault.Bytes && fault.Permissions==4);
            std::array<uint8_t,36> after{};CHECK(owner->read(output,after.data(),after.size())==0 && after==left);
            owner->close();owner.reset();bridge.unload();
            std::puts("PASS NpCommon two real GOT calls: offline bounded identifier comparisons, null/mismatch errors, NUL/tail/padding, non-ASCII, RO/unaligned/hole/overflow and no guest writes or PSN state");
        }
        // Partial GnmDriver: actual guest construction, never a GPU submit.
        for(size_t first=0;first<16;first+=4){
            using Gnm=maxps4::OrbisV4GnmDriver;
            const auto provider=Gnm::provider();
            const auto app=compat_fixture::consumer("libSceGnmDriver",Gnm::path,
                {Gnm::nids.begin()+first,Gnm::nids.begin()+first+4});
            maxps4::OrbisLinker::Image image;maxps4::OrbisLinker::Report linked;std::string error;
            CHECK(maxps4::OrbisLinker::link({
                {{{"gnm-app.prx",{Gnm::path}},app.data(),app.size()},0x400000},
                {{{Gnm::path,{}},provider.data(),provider.size()},Gnm::bias}}, {},image,error,32*1024*1024,&linked));
            CHECK(linked.committed && linked.resolvedImports==4);
            maxps4::OrbisPRXFEXBridge bridge;CHECK(bridge.map(image));
            auto owner=bridge.make_translation([&]{ctx->ClearCodeCache(thread,false);ctx->ClearCodeCache(second,false);});
            constexpr uint64_t stack=0x700000000ULL;
            const uint64_t output=stack+16*page;
            CHECK(owner->reserve(stack,page)==0 && owner->map(stack,page,6)==0);
            CHECK(owner->reserve(output,2*page)==0 && owner->map(output,page,6)==0 && owner->map(output+page,page,6)==0);
            FEXCore::Core::GuestMemoryFaultData fault{};
            auto call=[&](size_t fn,const std::array<uint64_t,6>& args,bool alternate=false){
                auto lease=owner->lease();space.ranges.clear();
                for(const auto& r:lease.ranges())space.ranges.push_back({r.guest,r.host,r.size,r.permissions});
                auto* current=alternate ? second : thread;auto& cpu=current->CurrentFrame->State;
                const unsigned regs[]={FEXCore::X86State::REG_RDI,FEXCore::X86State::REG_RSI,FEXCore::X86State::REG_RDX,
                    FEXCore::X86State::REG_RCX,FEXCore::X86State::REG_R8,FEXCore::X86State::REG_R9};
                for(size_t i=0;i<6;++i)cpu.gregs[regs[i]]=args[i];
                cpu.rip=0x401000+fn*32;cpu.gregs[FEXCore::X86State::REG_RSP]=stack+page-16;
                cpu.callret_sp=reinterpret_cast<uintptr_t>(callret.data())+callret.size()/4;
                fault={};cpu.GuestMemoryFaultRecord=&fault;ctx->ExecuteThread(current);cpu.GuestMemoryFaultRecord=nullptr;
                space.ranges.clear();return cpu.gregs[FEXCore::X86State::REG_RAX];
            };
            for(size_t fn=0;fn<4;++fn){
                const auto index=first+fn;if(index==0)continue;
                const uint32_t expected=index<=6 ? 0 : index==7 ? 0x80d15001 : index<=12 ? 0x80d110ff : 0x80d170ff;
                for(bool alternate:{false,true}){
                    CHECK(call(fn,{UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX},alternate)==expected && !fault.Bytes);
                    CHECK(call(fn,{})==expected && !fault.Bytes);
                }
            }
            if(first==0){
                std::array<uint32_t,9> sentinel;sentinel.fill(0x59595959);
                CHECK(owner->write(output,sentinel.data(),sizeof(sentinel))==0);
                CHECK(call(0,{output+4,7,3,0})==0 && !fault.Bytes);
                std::array<uint32_t,9> actual{};
                CHECK(owner->read(output,actual.data(),sizeof(actual))==0);
                const std::array<uint32_t,9> expected{0x59595959,0xc0012d00,3,2,0xc0021000,0,0,0,0x59595959};
                CHECK(actual==expected);
                const auto commands=maxps4::gpu::interpretPM4(actual.data()+1,7);
                CHECK(commands.valid && commands.draws.size()==1 && commands.draws[0].indexCount==3 && commands.writes.empty());
                CHECK(maxps4::gpu::planPM4Triangles(actual.data()+1,7).drawCount==1);
                // Upper argument bits are ignored for uint32 ABI arguments.
                CHECK(call(0,{output+4,0x100000007ULL,0x100000004ULL,0x100000000ULL},true)==0 && !fault.Bytes);
                CHECK(owner->read(output,actual.data(),sizeof(actual))==0 && actual[2]==4);
                CHECK(maxps4::gpu::planPM4Triangles(actual.data()+1,7).error==maxps4::gpu::PM4MetalError::unsupported_draw);
                for(const auto& args:std::array<std::array<uint64_t,6>,5>{{
                    {{0,7,3,0}},{{output+1,7,3,0}},{{output+4,8,3,0}},
                    {{output+4,7,3,1}},{{UINT64_MAX-3,7,3,0}}}}){
                    CHECK(call(0,args)==0xffffffffU && !fault.Bytes);
                }
                const auto retained=actual;CHECK(owner->read(output,actual.data(),sizeof(actual))==0 && actual==retained);
                CHECK(owner->protect(output,page,4)==0);
                call(0,{output+4,7,3,0});CHECK(fault.Address==output+4 && fault.Permissions==6);
                CHECK(owner->read(output,actual.data(),sizeof(actual))==0 && actual==retained);
                CHECK(owner->protect(output,page,6)==0);
                const uint64_t crossing=output+page-24;
                std::array<uint32_t,7> before;before.fill(0xa5a5a5a5);
                CHECK(owner->write(crossing,before.data(),sizeof(before))==0);
                CHECK(owner->protect(output+page,page,4)==0);
                call(0,{crossing,7,3,0});CHECK(fault.Address==output+page && fault.Permissions==6);
                std::array<uint32_t,7> after{};CHECK(owner->read(crossing,after.data(),sizeof(after))==0 && after==before);
                CHECK(owner->protect(output+page,page,6)==0);
                const uint64_t hole=output+2*page-24;
                CHECK(owner->write(hole,before.data(),24)==0);
                call(0,{hole,7,3,0});CHECK(fault.Address==output+2*page && fault.Permissions==6);
                CHECK(owner->read(hole,after.data(),24)==0 && std::equal(after.begin(),after.begin()+6,before.begin()));
                call(0,{Gnm::bias+0x4000,7,3,0});CHECK(fault.Address==Gnm::bias+0x4000 && fault.Permissions==6);
                CHECK(call(1,{})==0 && !fault.Bytes); // constructing PM4 did not enable submits
            }
            owner->close();owner.reset();bridge.unload();
        }
        std::puts("PASS GnmDriver 16 real GOT calls: seven-DWORD PM4 constructor/guards/preflight, CPU parser/Metal plan only, truthful disabled queries and explicit GPU/queue/capture failures");
        auto stage=[&](const std::vector<uint8_t>& bytes) {
            CHECK(memory.reserve(code,page)==0); CHECK(memory.map(code,page,5,bytes.data(),bytes.size())==0);
        };
        FEXCore::Core::GuestMemoryFaultData operand_fault {};
        auto run=[&](uint64_t entry, bool alias=false, bool alternate=false, bool data_alias=false, unsigned fragment=0) {
            auto* current=alternate?second:thread;
            auto& observed=current->CurrentFrame->State;
            auto lease=memory.lease(); space.ranges.clear();
            for (auto r:lease.ranges()) {
                CHECK(r.guest!=r.host);
                if (data_alias && r.guest==data+page) continue;
                if (fragment && r.guest==data) {
                    for (unsigned i=0;i<8;++i) space.ranges.push_back({data+i,r.host+i,1,fragment==2 && i==7 ? 0U : r.permissions});
                    space.ranges.push_back({data+8,r.host+8,r.size-8,r.permissions});
                } else space.ranges.push_back({data_alias && r.guest==data+16*page ? data+page :
                    alias && r.guest==code+3*page ? code+page : r.guest,r.host,r.size,r.permissions});
            }
            observed.fs_cached=data; observed.gs_cached=data;
            observed.rip=entry; observed.gregs[FEXCore::X86State::REG_RSP]=data+2048;
            observed.callret_sp=reinterpret_cast<uintptr_t>(callret.data())+callret.size()/4;
            operand_fault = {};
            observed.GuestMemoryFaultRecord = &operand_fault;
            ctx->ExecuteThread(current);
            observed.GuestMemoryFaultRecord = nullptr;
            return observed.gregs[FEXCore::X86State::REG_RAX];
        };
        callret.data()[callret.size()-1]=0xa5;
        CHECK(memory.reserve(data,page)==0); CHECK(memory.map(data,page,6)==0);
        CHECK(callret.data()[callret.size()-1]==0);
        // Execute the actual virtual AppContent guest functions, not host stubs.
        {
            const auto elf=maxps4::OrbisV4AppContent::provider({11,22,33,44});
            maxps4::PS4ExecutableImage provider;
            maxps4::OrbisMetadata metadata;
            CHECK(provider.load(elf.data(),elf.size())==0 && metadata.parse(elf.data(),elf.size()));
            auto body=provider.segments[0].bytes;
            body.resize(page,0x90);
            auto driver=[&](size_t offset,uint64_t function) {
                body[offset++]=0x48;body[offset++]=0xb8;
                for(unsigned i=0;i<8;++i)body[offset++]=uint8_t(function>>(8*i));
                body[offset++]=0xff;body[offset++]=0xd0;body[offset]=0xf4;
            };
            driver(0x300,code);driver(0x320,code+metadata.symbols[2].value-0x4000);
            CHECK(memory.reserve(code,0x4000+page)==0);
            CHECK(memory.map(code,page,5,body.data(),body.size())==0);
            CHECK(memory.map(code+0x4000,20,6,provider.segments[1].bytes.data(),20)==0);
            const uint64_t arguments=data+4*page; // Separate from the CALL/RET stack.
            CHECK(memory.reserve(arguments,page)==0 && memory.map(arguments,page,6)==0);
            const uint8_t marker=0xa5;
            std::array<uint8_t,40> boot;boot.fill(marker);
            CHECK(memory.write(arguments+128,boot.data(),boot.size())==0);
            auto call=[&](bool get,uint64_t arg0,uint64_t arg1) {
                state.gregs[FEXCore::X86State::REG_RDI]=arg0;
                state.gregs[FEXCore::X86State::REG_RSI]=arg1;
                return run(code+(get?0x320:0x300));
            };
            CHECK(call(false,0,arguments+128)==maxps4::OrbisV4AppContent::parameter && !operand_fault.Bytes);
            const uint8_t nonzero=1,zero=0;
            CHECK(memory.write(arguments+64,&nonzero,1)==0);
            CHECK(call(false,arguments+64,arguments+128)==maxps4::OrbisV4AppContent::parameter);
            CHECK(memory.write(arguments+64,&zero,1)==0);
            CHECK(memory.protect(arguments,page,4)==0);
            call(false,arguments+64,arguments+128);
            CHECK(operand_fault.Address==arguments+132 && operand_fault.Bytes==4 && operand_fault.Permissions==2);
            uint32_t initialized=99;
            CHECK(memory.read(code+0x4000,&initialized,4)==0 && !initialized);
            CHECK(memory.protect(arguments,page,6)==0);
            CHECK(call(false,arguments+64,arguments+128)==0 && !operand_fault.Bytes);
            CHECK(memory.read(arguments+128,boot.data(),boot.size())==0);
            for(size_t i=0;i<boot.size();++i) CHECK(boot[i]==(i>=4 && i<8 ? 0 : marker));
            CHECK(call(false,arguments+64,arguments+128)==maxps4::OrbisV4AppContent::busy);
            CHECK(call(true,2,arguments+256)==0 && !operand_fault.Bytes);
            int32_t parameter=0;CHECK(memory.read(arguments+256,&parameter,4)==0 && parameter==22);
            CHECK(call(true,0x100000002ULL,arguments+256)==0 && !operand_fault.Bytes);
            CHECK(memory.read(arguments+256,&parameter,4)==0 && parameter==22);
            for(uint64_t id:{0ULL,5ULL,0xffffffffULL}) {
                CHECK(call(true,id,arguments+256)==maxps4::OrbisV4AppContent::parameter && !operand_fault.Bytes);
                CHECK(memory.read(arguments+256,&parameter,4)==0 && parameter==22);
            }
            CHECK(call(true,1,0)==maxps4::OrbisV4AppContent::parameter);
            CHECK(memory.release(code)==0);
            CHECK(memory.release(arguments)==0);
            std::puts("PASS AppContent guest Initialize/GetInt: state, BUSY, parameters, SKU refusal, NX/RX, fault-before-state-write and reserved output preservation; real FEX");
        }
        // mov rdi,guest-data; mov qword [rdi+8],21; mov rax,[rdi+8];
        // lock xadd [rdi+8],rax; movdqu xmm0,[rdi+8]; movdqu [rdi+32],xmm0;
        // mov eax,[rdi+32]; hlt. Both offsets are translated, not added to host base.
        stage({0x48,0xbf,0,0,0,0,4,0,0,0, 0x48,0xc7,0x47,8,21,0,0,0,
               0x48,0x8b,0x47,8, 0xf0,0x48,0x0f,0xc1,0x47,8,
               0xf3,0x0f,0x6f,0x47,8, 0xf3,0x0f,0x7f,0x47,32, 0x8b,0x47,32,0xf4});
        std::fprintf(stderr,"BEGIN translated scalar/vector/atomic execution\n");
        CHECK(run(code)==42);
        CHECK(run(code,false,true)==42);
        CHECK(run(code)==42); // cached code, same immutable mapping snapshot
        auto replace=[&](const std::vector<uint8_t>& bytes) { CHECK(memory.release(code)==0); stage(bytes); };
        replace({0xb8,99,0,0,0,0xf4}); CHECK(run(code)==99); CHECK(run(code,false,true)==99);
        replace({0xb8,21,0,0,0,0xf4}); CHECK(run(code)==21); CHECK(run(code,false,true)==21);
        std::puts("PASS two-thread instruction cache remap before atomics"); std::fflush(stdout);
        const std::vector<uint8_t> address={0x48,0xbf,0,0,0,0,4,0,0,0};
        auto verify=[&](uint64_t expected) {
            uint64_t observed=0; auto lease=memory.lease();
            CHECK(lease.memory().read(data,&observed,8)==0); CHECK(observed==expected);
        };
        // Rejected native operands stop without a signal or a partial store.
        auto rejected = [&](uint64_t pointer, const std::vector<uint8_t>& operation, uint32_t permissions, uint32_t width=8) {
            std::vector<uint8_t> bytes={0x48,0xbf};
            for (unsigned i=0;i<8;++i) bytes.push_back(static_cast<uint8_t>(pointer>>(8*i)));
            bytes.insert(bytes.end(),{0x48,0xb8,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88});
            bytes.insert(bytes.end(),operation.begin(),operation.end()); bytes.push_back(0xf4);
            replace(bytes); run(code);
            CHECK(state.gregs[FEXCore::X86State::REG_RAX]==0x8877665544332211ULL);
            const auto& fault=operand_fault;
            if (fault.Bytes!=width || fault.Address!=pointer || fault.Permissions!=permissions)
                std::fprintf(stderr,"operand fault mismatch: guest=0x%llx width=%u permissions=%u; actual=0x%llx/%u/%u\n",
                    static_cast<unsigned long long>(pointer),width,permissions,
                    static_cast<unsigned long long>(fault.Address),fault.Bytes,fault.Permissions);
            CHECK(fault.Bytes==width && fault.Address==pointer && fault.Permissions==permissions);
        };
        rejected(data+page-4,{0x48,0x8b,0x07},4); // split operand
        rejected(data+page,{0x48,0x89,0x07},2);   // unmapped
        rejected(data+1,{0xf0,0x48,0x0f,0xc1,0x07},6); // misaligned atomic
        CHECK(memory.protect(data,page,4)==0);
        rejected(data,{0x48,0x89,0x07},2);
        CHECK(memory.protect(data,page,0)==0);
        rejected(data,{0x48,0x8b,0x07},4);
        CHECK(memory.protect(data,page,6)==0);
        replace({0xb8,42,0,0,0,0xf4}); CHECK(run(code)==42);
        CHECK(!operand_fault.Bytes);
        std::puts("PASS low RIP 0x400000; checked split/hole/permissions/atomic rejection and retry");
        // Scalar accesses may cross pinned mappings after complete preflight;
        // atomics must still fit a single mapping.
        CHECK(memory.release(data)==0);
        CHECK(memory.reserve(data,2*page)==0);
        CHECK(memory.map(data,page,6)==0);
        std::vector<uint8_t> marker(2*page,0x59), observed(marker.size());
        CHECK(memory.write(data,marker.data(),page)==0);
        for (int boundary : {0,1,2,3}) { // hole, RW, R, PROT_NONE
            if (boundary==1) CHECK(memory.map(data+page,page,6)==0);
            if (boundary==1) CHECK(memory.write(data+page,marker.data(),page)==0);
            if (boundary==2) CHECK(memory.protect(data+page,page,4)==0);
            if (boundary==3) CHECK(memory.protect(data+page,page,0)==0);
            for (uint32_t width : {1U,2U,4U,8U}) {
                // Holes/protections fault deterministically; split atomics
                // remain refused even across two RW mappings.
                const uint64_t pointer=data+page-(width>1?width/2:0);
                std::vector<uint8_t> load, store, atomic={0xf0};
                if (width==2) { load.push_back(0x66); store.push_back(0x66); atomic.push_back(0x66); }
                if (width==8) { load.push_back(0x48); store.push_back(0x48); atomic.push_back(0x48); }
                load.insert(load.end(),{static_cast<uint8_t>(width==1?0x8a:0x8b),0x07});
                store.insert(store.end(),{static_cast<uint8_t>(width==1?0x88:0x89),0x07});
                atomic.insert(atomic.end(),{0x0f,static_cast<uint8_t>(width==1?0xc0:0xc1),0x07});
                // A byte at the next mapping is valid only with permissions.
                if (boundary==0 || boundary==3) rejected(pointer,load,4,width);
                if (boundary==1 || boundary==2) {
                    std::vector<uint8_t> bytes={0x48,0xbf};
                    for(unsigned i=0;i<8;++i) bytes.push_back(pointer>>(8*i));
                    bytes.insert(bytes.end(),{0x48,0xb8,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88});
                    bytes.insert(bytes.end(),load.begin(),load.end()); bytes.push_back(0xf4);
                    replace(bytes);
                    const uint64_t expected=width==1 ? 0x8877665544332259ULL :
                        width==2 ? 0x8877665544335959ULL :
                        width==4 ? 0x59595959ULL : 0x5959595959595959ULL;
                    CHECK(run(code)==expected && !operand_fault.Bytes);
                }
                if (boundary!=1) rejected(pointer,store,2,width);
                if (width>1 || boundary!=1) rejected(pointer,atomic,6,width);
                atomic[atomic.size()-2]=static_cast<uint8_t>(width==1?0xb0:0xb1);
                if (width>1 || boundary!=1) rejected(pointer,atomic,6,width); // CAS
            }
            CHECK(memory.read(data,observed.data(),page)==0);
            for(size_t i=0;i<page;++i) CHECK(observed[i]==0x59);
            if (boundary==1 || boundary==2) {
                CHECK(memory.read(data+page,observed.data(),page)==0);
                for(size_t i=0;i<page;++i) CHECK(observed[i]==0x59);
            }
        }
        // Aligned atomics can also overrun a short logical region without
        // crossing a host page. This must fault on bounds, not alignment.
        CHECK(memory.release(data)==0);
        CHECK(memory.reserve(data,2*page)==0);
        CHECK(memory.map(data,page-1,6)==0);
        CHECK(memory.write(data,marker.data(),page-1)==0);
        for (uint32_t width : {1U,2U,4U,8U}) {
            std::vector<uint8_t> atomic={0xf0};
            if (width==2) atomic.push_back(0x66);
            if (width==8) atomic.push_back(0x48);
            atomic.insert(atomic.end(),{0x0f,static_cast<uint8_t>(width==1?0xc0:0xc1),0x07});
            rejected(data+page-width,atomic,6,width);
            atomic[atomic.size()-2]=static_cast<uint8_t>(width==1?0xb0:0xb1);
            rejected(data+page-width,atomic,6,width);
        }
        CHECK(memory.read(data,observed.data(),page-1)==0);
        for(size_t i=0;i<page-1;++i) CHECK(observed[i]==0x59);
        CHECK(memory.release(data)==0);
        CHECK(memory.reserve(data,2*page)==0);
        CHECK(memory.map(data,page,6)==0);
        CHECK(memory.map(data+page,page,6)==0);
        CHECK(memory.write(data,marker.data(),marker.size())==0);
        // Checked copy handles the same boundary.
        const uint64_t crossing=42;
        CHECK(memory.write_span(data+page-4,&crossing,sizeof(crossing))==0);
        uint64_t copied=0;
        CHECK(memory.read(data+page-4,&copied,sizeof(copied))==0 && copied==42);
        CHECK(memory.write(data,marker.data(),marker.size())==0);
        std::puts("PASS native mapping boundary matrix: 8/16/32/64-bit loads, stores, atomics; no partial write");
        // Every split position, scalar width and load/store direction. The
        // instruction following each access observes ZF, catching NZCV damage.
        for (uint32_t width : {2U,4U,8U}) {
            for (uint32_t split=1; split<width; ++split) {
                const uint64_t pointer=data+page-split;
                for (bool store : {false,true}) {
                    CHECK(memory.write(data,marker.data(),marker.size())==0);
                    const uint64_t value=0x8877665544332211ULL;
                    std::vector<uint8_t> bytes={0x48,0xbf};
                    for (unsigned i=0;i<8;++i) bytes.push_back(pointer>>(8*i));
                    bytes.insert(bytes.end(),{0x48,0xb8});
                    for (unsigned i=0;i<8;++i) bytes.push_back(value>>(8*i));
                    bytes.insert(bytes.end(),{0x31,0xc9,0x39,0xc9}); // ECX=0; ZF=1
                    if (width==2) bytes.push_back(0x66);
                    if (width==8) bytes.push_back(0x48);
                    bytes.insert(bytes.end(),{static_cast<uint8_t>(store?0x89:0x8b),0x07,
                                              0x0f,0x95,0xc1,0xf4}); // setnz cl; hlt
                    replace(bytes);
                    const uint64_t expected=width==2 ? (value & ~0xffffULL)|0x5959 :
                        width==4 ? 0x59595959 : 0x5959595959595959ULL;
                    CHECK(run(code)==(store?value:expected));
                    CHECK(!operand_fault.Bytes);
                    CHECK(state.gregs[FEXCore::X86State::REG_RCX]==0);
                    CHECK(state.gregs[FEXCore::X86State::REG_RDI]==pointer);
                    CHECK(memory.read(data,observed.data(),observed.size())==0);
                    auto expected_memory=marker;
                    if (store) for (uint32_t i=0;i<width;++i)
                        expected_memory[page-split+i]=static_cast<uint8_t>(value>>(8*i));
                    CHECK(observed==expected_memory);
                }
            }
        }
        // A destination sharing the address register must not destroy EA.
        replace({0x48,0xb8,0,0,0,0,4,0,0,0,0x48,0x05,
                 static_cast<uint8_t>((page-4)&255),static_cast<uint8_t>((page-4)>>8),0,0,
                 0x48,0x8b,0x00,0xf4});
        CHECK(memory.write(data,marker.data(),marker.size())==0);
        CHECK(run(code)==0x5959595959595959ULL && !operand_fault.Bytes);
        rejected(UINT64_MAX-3,{0x48,0x89,0x07},2);
        rejected(UINT64_MAX-3,{0x48,0x8b,0x07},4);
        std::puts("PASS scalar split loads/stores: every 16/32/64-bit split, exact bytes, registers, flags, EA alias and overflow");
        // Eight logical mappings, plus two noncontiguous guarded host allocations.
        auto bytes=address;
        bytes.insert(bytes.end(),{0x48,0xc7,0x07,42,0,0,0,0x48,0x8b,0x07,0xf4});
        replace(bytes); CHECK(run(code,false,false,false,true)==42 && !operand_fault.Bytes);
        for (bool store : {false,true}) {
            CHECK(memory.write(data,marker.data(),marker.size())==0);
            auto denied=address;
            denied.insert(denied.end(),{0x48,0xb8,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
                0x48,static_cast<uint8_t>(store?0x89:0x8b),0x07,0xf4});
            replace(denied);
            CHECK(run(code,false,false,false,2)==0x8877665544332211ULL);
            CHECK(operand_fault.Address==data && operand_fault.Bytes==8 &&
                  operand_fault.Permissions==(store?2U:4U));
            CHECK(memory.read(data,observed.data(),observed.size())==0 && observed==marker);
        }
        CHECK(memory.reserve(data+16*page,page)==0);
        CHECK(memory.map(data+16*page,page,6)==0);
        {
            auto lease=memory.lease();
            const auto* first=lease.memory().translate(data,1,4);
            const auto* last=lease.memory().translate(data+16*page,1,4);
            CHECK(first && last && first+page!=last);
        }
        bytes={0x48,0xbf};
        const uint64_t split_pointer=data+page-4;
        for(unsigned i=0;i<8;++i) bytes.push_back(split_pointer>>(8*i));
        bytes.insert(bytes.end(),{0x48,0xc7,0x07,42,0,0,0,0x48,0x8b,0x07,0xf4});
        replace(bytes); CHECK(run(code,false,false,true)==42 && !operand_fault.Bytes);
        uint32_t tail=0, head=1;
        CHECK(memory.read(data+page-4,&tail,4)==0 && tail==42);
        CHECK(memory.read(data+16*page,&head,4)==0 && head==0);
        CHECK(memory.release(data+16*page)==0);
        std::puts("PASS scalar split across eight logical ranges and disjoint guarded host mappings");
        // Native-width CAS and XADD: both mismatch and success, all widths.
        for (size_t width : {1U,2U,4U,8U}) {
            uint64_t initial=21; CHECK(memory.write(data,&initial,8)==0);
            auto bytes=address;
            bytes.insert(bytes.end(),{0xb8,21,0,0,0,0xb9,42,0,0,0,0xf0});
            if(width==2) bytes.push_back(0x66);
            if(width==8) bytes.push_back(0x48);
            bytes.insert(bytes.end(),{0x0f,static_cast<uint8_t>(width==1?0xb0:0xb1),0x0f,0xf4});
            replace(bytes);
            const auto actual=run(code);
            std::fprintf(stderr,"CAS lse=%d width=%zu RAX=%llu expected=21\n",lse,width,static_cast<unsigned long long>(actual));
            CHECK(actual==21); verify(42);
            CHECK(run(code)==42); verify(42); // mismatch updates accumulator, memory unchanged
            initial=21; CHECK(memory.write(data,&initial,8)==0);
            bytes=address; bytes.insert(bytes.end(),{0xb8,21,0,0,0,0xf0});
            if(width==2) bytes.push_back(0x66);
            if(width==8) bytes.push_back(0x48);
            bytes.insert(bytes.end(),{0x0f,static_cast<uint8_t>(width==1?0xc0:0xc1),0x07,0xf4});
            replace(bytes); CHECK(run(code)==21); verify(42);
        }
        // Paired CAS forces translation to coexist with all four JIT temporaries.
        for (bool wide : {false,true}) {
            uint64_t initial[2]={21,7};
            if (!wide) initial[0]=21ULL | (7ULL<<32);
            CHECK(memory.write(data,initial,16)==0);
            auto bytes=address;
            bytes.insert(bytes.end(),{0xb8,21,0,0,0,0xba,7,0,0,0,0xbb,42,0,0,0,0xb9,9,0,0,0,0xf0});
            if(wide) bytes.push_back(0x48);
            bytes.insert(bytes.end(),{0x0f,0xc7,0x0f,0xf4});
            replace(bytes); CHECK(run(code)==21);
            uint64_t observed[2]{};
            { auto lease=memory.lease(); CHECK(lease.memory().read(data,observed,16)==0); }
            CHECK(observed[0]==(wide?42ULL:(42ULL | (9ULL<<32))));
            if(wide) CHECK(observed[1]==9);
        }
        struct ALU { uint8_t modrm; uint32_t initial, immediate, expected; };
        for (const ALU op : {ALU{0x07,21,21,42}, ALU{0x2f,63,21,42},
             ALU{0x27,63,42,42}, ALU{0x0f,32,10,42}, ALU{0x37,63,21,42}}) {
            uint64_t initial=op.initial; CHECK(memory.write(data,&initial,8)==0);
            auto bytes=address; bytes.insert(bytes.end(),{0xf0,0x83,op.modrm,static_cast<uint8_t>(op.immediate),0x8b,0x07,0xf4});
            replace(bytes); CHECK(run(code)==op.expected); verify(op.expected);
        }
        {   // XCHG and atomic NEG have distinct backend handlers.
            uint64_t initial=21; CHECK(memory.write(data,&initial,8)==0);
            auto bytes=address; bytes.insert(bytes.end(),{0xb8,42,0,0,0,0x87,0x07,0xf4});
            replace(bytes); CHECK(run(code)==21); verify(42);
            bytes=address; bytes.insert(bytes.end(),{0xf0,0x48,0xf7,0x1f,0x48,0x8b,0x07,0xf4});
            replace(bytes); CHECK(run(code)==uint64_t(-42)); verify(uint64_t(-42));
        }
        {   // Effective addresses retain guest TLS bases and signed/scaled offsets.
            uint64_t value=42; CHECK(memory.write(data+8,&value,8)==0);
            replace({0x64,0x8b,0x04,0x25,8,0,0,0,0xf4}); CHECK(run(code)==42);
            replace({0x65,0x8b,0x04,0x25,8,0,0,0,0xf4}); CHECK(run(code)==42);
            auto bytes=address; bytes[2]=16;
            bytes.insert(bytes.end(),{0x8b,0x47,0xf8,0xf4}); replace(bytes); CHECK(run(code)==42);
            bytes=address; bytes.insert(bytes.end(),{0xb9,2,0,0,0,0x8b,0x04,0x8f,0xf4});
            replace(bytes); CHECK(run(code)==42);
        }
        replace({0x6a,42,0x58,0xf4}); CHECK(run(code)==42);
        CHECK(state.gregs[FEXCore::X86State::REG_RSP]==data+2048);
        replace({0x54,0x58,0xf4}); CHECK(run(code)==data+2048); // PUSH RSP stores guest RSP
        replace({0xe8,1,0,0,0,0xf4,0xb8,42,0,0,0,0xc3}); CHECK(run(code)==42);
        CHECK(state.gregs[FEXCore::X86State::REG_RSP]==data+2048);
        // A translated load between CMP and SETZ must preserve guest NZCV.
        auto flags=address;
        flags.insert(flags.end(),{0x31,0xc0,0x39,0xc0,0x8b,0x0f,0x0f,0x94,0xc0,0x0f,0xb6,0xc0,0xf4});
        replace(flags); CHECK(run(code)==1);
        // Cache invalidation on release + remap is observed at the same RIP.
        CHECK(memory.release(code)==0);
        // Same guest RIP, changed host bytes: stale native code would return 42.
        stage({0xb8,99,0,0,0,0xf4}); CHECK(run(code)==99); CHECK(run(code,false,true)==99);
        CHECK(memory.protect(code,page,4)==0);
        {
            auto lease=memory.lease(); space.ranges.clear();
            for(auto r:lease.ranges()) space.ranges.push_back({r.guest,r.host,r.size,r.permissions});
            uint8_t byte=0; CHECK(!space.ReadGuestInstruction(code,&byte,1));
        }
        CHECK(memory.protect(code,page,0)==0);
        {
            auto lease=memory.lease(); space.ranges.clear();
            for(auto r:lease.ranges()) space.ranges.push_back({r.guest,r.host,r.size,r.permissions});
            uint8_t byte=0x59; CHECK(!space.ReadGuestInstruction(code,&byte,1));
            CHECK(byte==0x59);
            const uint8_t* host=reinterpret_cast<const uint8_t*>(1);
            CHECK(lease.resolve_contiguous(code,1,5,host)==-EFAULT && !host);
        }
        CHECK(memory.protect(code,page,5)==0); CHECK(run(code)==99);
        CHECK(memory.release(code)==0);
        // Decoder crosses two adjacent guest pages backed by independent host reservations.
        // Guard pages force these host buffers to be noncontiguous.
        CHECK(memory.reserve(code,page)==0); CHECK(memory.reserve(code+3*page,page)==0);
        const uint8_t left[]={0xb8,77}; const uint8_t right[]={0,0,0,0xf4};
        std::vector<uint8_t> first(page,0x90); first[page-2]=left[0]; first[page-1]=left[1];
        CHECK(memory.map(code,page,5,first.data(),first.size())==0);
        CHECK(memory.map(code+3*page,page,5,right,sizeof(right))==0);
        CHECK(run(code+page-2,true)==77);
        CHECK(memory.release(code)==0); CHECK(memory.release(code+3*page)==0);
        CHECK(memory.reserve(code,page)==0); CHECK(memory.reserve(code+3*page,page)==0);
        std::vector<uint8_t> branch={0xe9};
        const uint32_t displacement=static_cast<uint32_t>(3*page-5);
        for(unsigned i=0;i<4;++i) branch.push_back(static_cast<uint8_t>(displacement>>(8*i)));
        const uint8_t target[]={0xb8,123,0,0,0,0xf4};
        CHECK(memory.map(code,page,5,branch.data(),branch.size())==0);
        CHECK(memory.map(code+3*page,page,5,target,sizeof(target))==0);
        CHECK(run(code)==123); // multiblock branch target has a different host mapping
        // A mutation cannot release storage while a decoder/JIT dispatch lease pins it.
        std::future<int> released;
        std::promise<void> started;
        {
            auto lease=memory.lease();
            released=std::async(std::launch::async,[&] { started.set_value(); return memory.release(data); });
            started.get_future().wait();
            CHECK(released.wait_for(std::chrono::milliseconds(30))==std::future_status::timeout);
            CHECK(lease.memory().translate(data,8,6));
        }
        CHECK(released.get()==0); CHECK(invalidations>10);
        // Terminal shutdown must finish while both cache-owning threads live.
        memory.close(); space.ranges.clear(); ctx->DestroyThread(second); ctx->DestroyThread(thread);
        // Cleanup after thread destruction must never re-enter the callback.
        memory.close(); memory.clear();
        CHECK(memory.reserve(code,page)==-ESHUTDOWN);
    }
    FEXCore::Config::Shutdown();
#if defined(VIXL_SIMULATOR)
    std::puts("PASS translated FEX decoder/scalar/vector/TSO/atomic/TLS/stack/remap/lease; backend=ARM64 simulator (not device)");
#else
    std::puts("PASS translated FEX decoder/scalar/vector/TSO/atomic/TLS/stack/remap/lease; backend=native ARM64 (not iPhone)");
#endif
}

