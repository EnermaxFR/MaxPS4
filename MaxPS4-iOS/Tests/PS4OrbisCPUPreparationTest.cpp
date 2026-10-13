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
    std::puts("PASS SCE NID/RELA -> owned page mapping -> guest CALL/RET admission; no CPU execution claimed");
}
