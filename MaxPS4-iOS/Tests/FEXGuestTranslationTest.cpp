#include <FEXCore/Utils/LogManager.h>
#include "maxps4_orbis_v4_translation.h"
#include "OrbisPRXFEXBridge.hpp"
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
            CHECK(owner->reserve(0x50000,page)==0 && owner->map(0x50000,page,6)==0);
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
                cpu.rip=0x44000; cpu.gregs[FEXCore::X86State::REG_RSP]=0x50000+page-16;
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
        size_t invalidations=0;
        maxps4::OrbisV4Translation memory([&] { ctx->ClearCodeCache(thread,false); ctx->ClearCodeCache(second,false); ++invalidations; });
        const size_t page=memory.page_size();
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

