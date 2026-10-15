#include "maxps4_orbis_v4_translation.h"
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
int main() {
    FEXCore::Config::Initialize(); FEXCore::Config::Load();
    FEXCore::Config::Set(FEXCore::Config::CONFIG_IS64BIT_MODE,"1");
    FEXCore::Config::Set(FEXCore::Config::CONFIG_DISABLETELEMETRY,"1");
    FEXCore::Config::Set(FEXCore::Config::CONFIG_ENABLECODECACHINGWIP,"0");
#if defined(VIXL_SIMULATOR)
    FEXCore::Config::Set(FEXCore::Config::CONFIG_DISABLE_VIXL_INDIRECT_RUNTIME_CALLS,"0");
#endif
    for (bool lse : {false,true}) {
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
        constexpr uint64_t code=0x300000000ULL, data=0x400000000ULL;
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
        size_t invalidations=0;
        maxps4::OrbisV4Translation memory([&] { ctx->ClearCodeCache(thread,false); ctx->ClearCodeCache(second,false); ++invalidations; });
        const size_t page=memory.page_size();
        auto stage=[&](const std::vector<uint8_t>& bytes) {
            CHECK(memory.reserve(code,page)==0); CHECK(memory.map(code,page,5,bytes.data(),bytes.size())==0);
        };
        auto run=[&](uint64_t entry, bool alias=false, bool alternate=false) {
            auto* current=alternate?second:thread;
            auto& observed=current->CurrentFrame->State;
            auto lease=memory.lease(); space.ranges.clear();
            for (auto r:lease.ranges()) {
                CHECK(r.guest!=r.host);
                space.ranges.push_back({alias && r.guest==code+3*page ? code+page : r.guest,r.host,r.size,r.permissions});
            }
            observed.fs_cached=data; observed.gs_cached=data;
            observed.rip=entry; observed.gregs[FEXCore::X86State::REG_RSP]=data+2048;
            observed.callret_sp=reinterpret_cast<uintptr_t>(callret.data())+callret.size()/4;
            ctx->ExecuteThread(current);
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
        memory.clear(); space.ranges.clear(); ctx->DestroyThread(second); ctx->DestroyThread(thread);
    }
    FEXCore::Config::Shutdown();
#if defined(VIXL_SIMULATOR)
    std::puts("PASS translated FEX decoder/scalar/vector/TSO/atomic/TLS/stack/remap/lease; backend=ARM64 simulator (not device)");
#else
    std::puts("PASS translated FEX decoder/scalar/vector/TSO/atomic/TLS/stack/remap/lease; backend=native ARM64 (not iPhone)");
#endif
}
