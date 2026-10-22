#include "../Native/maxps4_guest_elf.h"
#include "PS4OrbisExecutionFixture.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
int main() {
    using namespace orbis_execution;
    auto app=make(false), provider=make(true);
    maxps4::OrbisLinker::Image linked;
    std::string reason;
    CHECK(maxps4::OrbisLinker::link({input("app.elf",app,false),input("provider.prx",provider,true)}, {},linked,reason));
    maxps4::GuestELF image;
    CHECK(image.prepare_orbis(linked,"app.elf",6,reason)==0);
    CHECK(image.entry()==entry && image.ps4_blockers()==4);
    const auto* code=image.translate(entry,6,5);
    CHECK(code && code[0]==0xe8 && code[5]==0x90);
    CHECK(linked.translate(entry,6,5)[0]==0xff && app[0x200]==0xff);
    const auto* slot=image.translate(data,8,4);
    CHECK(slot && slot[1]==0x40 && slot[2]==0x20);
    CHECK(image.translate(data+0x100,0x100,2));
    CHECK(!image.translate(entry,1,2) && !image.translate(data,1,1));
    CHECK(image.prepare_orbis(linked,"missing",10,reason)!=0 && image.segments().empty());
    CHECK(image.prepare_orbis(linked,"app.elf",5,reason)!=0 && image.segments().empty());
    // Invalid guest store fails before execution and publishes no image.
    app[0x20d]=0; app[0x20e]=0; app[0x20f]=0; app[0x210]=0;
    CHECK(maxps4::OrbisLinker::link({input("app.elf",app,false),input("provider.prx",provider,true)}, {},linked,reason));
    CHECK(image.prepare_orbis(linked,"app.elf",100,reason)!=0 && image.segments().empty());
    app=make(false); provider[0x200]=0xeb;
    CHECK(maxps4::OrbisLinker::link({input("app.elf",app,false),input("provider.prx",provider,true)}, {},linked,reason));
    CHECK(image.prepare_orbis(linked,"app.elf",100,reason)!=0);
    provider=make(true); provider[0x205]=0xf4;
    CHECK(maxps4::OrbisLinker::link({input("app.elf",app,false),input("provider.prx",provider,true)}, {},linked,reason));
    CHECK(image.prepare_orbis(linked,"app.elf",100,reason)!=0);
    provider=make(true);
    // The relocation lands in BSS, which must survive the page-copy boundary.
    app=make(false); orbis_fixture::put(app,0x900,0x8100);
    orbis_fixture::put(app,0x202,0x8100-(0x4000+6),4);
    CHECK(maxps4::OrbisLinker::link({input("app.elf",app,false),input("provider.prx",provider,true)}, {},linked,reason));
    CHECK(image.prepare_orbis(linked,"app.elf",6,reason)==0);
    CHECK(image.translate(data+0x100,8,4)[2]==0x20);
    provider=make(true); app=make(false);
    const uint8_t cycle[]={0xff,0x15,0xfa,0x3f,0,0,0xc3};
    std::copy(std::begin(cycle),std::end(cycle),provider.begin()+0x200);
    orbis_fixture::put(provider,64+32,7); orbis_fixture::put(provider,64+40,7);
    orbis_fixture::put(provider,0x828,7); orbis_fixture::put(provider,0x300,provider_bias+0x4000);
    CHECK(maxps4::OrbisLinker::link({input("app.elf",app,false),input("provider.prx",provider,true)}, {},linked,reason));
    CHECK(image.prepare_orbis(linked,"app.elf",100,reason)==-ELOOP && image.segments().empty());
    CHECK(reason.find("recursive")!=std::string::npos);
    provider=make(true); app=make(false);
    // A raw readable pointer targets the operand of the currently admitted CALL.
    // Reject overlap before rewriting any stream, even if its bytes decode.
    orbis_fixture::put(app,0x310,entry+1);
    orbis_fixture::put(app,0x202,0x8010-(0x4000+6),4);
    CHECK(maxps4::OrbisLinker::link({input("app.elf",app,false),input("provider.prx",provider,true)}, {},linked,reason));
    CHECK(image.prepare_orbis(linked,"app.elf",100,reason)!=0 && image.segments().empty());
    // Static TLS: region after a guard page, template copied below the TCB,
    // TCB self-pointer, and FS reads admitted only inside blocks/TCB.
    provider=make(true);
    auto tls=make_tls_app();
    CHECK(maxps4::OrbisLinker::link({input("app.elf",tls,false),input("provider.prx",provider,true)}, {},linked,reason));
    CHECK(linked.tls.size()==1 && linked.staticTLSSize==8);
    CHECK(image.prepare_orbis(linked,"app.elf",10,reason)==0);
    const auto* tcb=image.thread_pointer_host();
    CHECK(tcb!=nullptr);
    uint64_t self=0; std::memcpy(&self,tcb,8);
    CHECK(self==reinterpret_cast<uintptr_t>(tcb));
    uint32_t variable=0; std::memcpy(&variable,tcb-16,4); // 16-byte aligned blocks: TP-16..TP.
    uint32_t below=0; std::memcpy(&below,tcb-8,4);
    CHECK(variable==0 && below==0x1234);
    CHECK(image.segments().back().permissions==6 && image.segments().back().address>data+0x300);
    for (int32_t outside : {-24, 16, 13}) {
        tls=make_tls_app(outside);
        CHECK(maxps4::OrbisLinker::link({input("app.elf",tls,false),input("provider.prx",provider,true)}, {},linked,reason));
        CHECK(image.prepare_orbis(linked,"app.elf",10,reason)==-EFAULT && image.segments().empty() && !image.thread_pointer_host());
    }
    // Compiler-style access through RAX = FS:[0], disp8 and disp32 encodings.
    for (bool disp32 : {false, true}) {
        tls=make_tls_register_app(-8,disp32);
        CHECK(maxps4::OrbisLinker::link({input("app.elf",tls,false),input("provider.prx",provider,true)}, {},linked,reason));
        CHECK(image.prepare_orbis(linked,"app.elf",10,reason)==0 && image.thread_pointer_host());
        tls=make_tls_register_app(16,disp32); // Past the TCB.
        CHECK(maxps4::OrbisLinker::link({input("app.elf",tls,false),input("provider.prx",provider,true)}, {},linked,reason));
        CHECK(image.prepare_orbis(linked,"app.elf",10,reason)==-EFAULT && image.segments().empty());
    }
    tls=make_tls_register_app(-8,false,0x1234,true); // NOP between: RAX no longer trusted.
    CHECK(maxps4::OrbisLinker::link({input("app.elf",tls,false),input("provider.prx",provider,true)}, {},linked,reason));
    CHECK(image.prepare_orbis(linked,"app.elf",10,reason)==-ENOTSUP && image.segments().empty());
    tls=make_tls_register_app(); tls[0x205]=1; // FS:[1] is not the self-pointer load.
    CHECK(maxps4::OrbisLinker::link({input("app.elf",tls,false),input("provider.prx",provider,true)}, {},linked,reason));
    CHECK(image.prepare_orbis(linked,"app.elf",10,reason)==-ENOTSUP);
    tls=make_tls_app(0); // Reading the TCB self-pointer is allowed.
    CHECK(maxps4::OrbisLinker::link({input("app.elf",tls,false),input("provider.prx",provider,true)}, {},linked,reason));
    CHECK(image.prepare_orbis(linked,"app.elf",10,reason)==0);
    // Without PT_TLS, any FS read is refused and no TCB exists.
    app=make(false);
    const uint8_t fsRead[]={0x64,0x8b,0x04,0x25,0,0,0,0,0xf4};
    std::copy(std::begin(fsRead),std::end(fsRead),app.begin()+0x200);
    CHECK(maxps4::OrbisLinker::link({input("app.elf",app,false),input("provider.prx",provider,true)}, {},linked,reason));
    CHECK(image.prepare_orbis(linked,"app.elf",10,reason)!=0 && !image.thread_pointer_host());
    std::puts("PASS SCE NID/RELA -> owned page mapping -> guest CALL/RET admission; no CPU execution claimed");
}
