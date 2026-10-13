#include "PS4PlayableFixture.h"
#include "../Native/maxps4_guest_frame.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
int main(int argc,char** argv) {
    auto app=playable_fixture::app(), provider=orbis_execution::make(true);
    if (argc==2) {
        auto save=[&](const char* name,const std::vector<uint8_t>& bytes) {
            std::string path=std::string(argv[1])+"/"+name;
            FILE* f=std::fopen(path.c_str(),"wb"); CHECK(f); CHECK(std::fwrite(bytes.data(),1,bytes.size(),f)==bytes.size()); CHECK(!std::fclose(f));
        };
        save("app.elf",app); save("provider.prx",provider); return 0;
    }
    maxps4::OrbisLinker::Image linked; std::string reason;
    CHECK(maxps4::OrbisLinker::link({orbis_execution::input("app.elf",app,false),orbis_execution::input("provider.prx",provider,true)}, {},linked,reason));
    maxps4::GuestELF image;
    CHECK(image.prepare_orbis(linked,"app.elf",65536,reason)==0);
    maxps4::GuestFrame frame{playable_fixture::pixels,playable_fixture::input,32,24};
    CHECK(frame.validate(image)==0);
    CHECK(frame.setInput(image,31)==0);
    CHECK(*image.translate(playable_fixture::input,4,4)==31);
    CHECK(frame.setInput(image,32)==-EINVAL);
    std::vector<uint8_t> out(frame.bytes(),0xa5);
    CHECK(frame.snapshot(image,out.data(),out.size())==0 && out[0]==0);
    CHECK(frame.snapshot(image,out.data(),out.size()-1)==-EINVAL);
    auto bad=frame; bad.width=0; CHECK(bad.validate(image)==-EINVAL);
    bad=frame; bad.height=241; CHECK(bad.validate(image)==-EINVAL);
    bad=frame; bad.width=UINT32_MAX; CHECK(bad.validate(image)==-EINVAL);
    bad=frame; bad.pixels=UINT64_MAX-3; CHECK(bad.validate(image)==-EFAULT);
    bad=frame; bad.pixels=orbis_execution::entry; CHECK(bad.validate(image)!=0);
    bad=frame; bad.input=bad.pixels; CHECK(bad.validate(image)==-EINVAL);
    image.clear(); CHECK(frame.validate(image)==-EFAULT);
    CHECK(image.prepare_orbis(linked,"app.elf",65536,reason)==0 && frame.validate(image)==0);
    CHECK(image.prepare_orbis(linked,"app.elf",10,reason)!=0);
    // Missing initialization and unknown pointer-based store are refused.
    auto corrupted=app; corrupted[0x1006]=0x89; corrupted[0x1007]=0x00;
    CHECK(maxps4::OrbisLinker::link({orbis_execution::input("app.elf",corrupted,false),orbis_execution::input("provider.prx",provider,true)}, {},linked,reason));
    CHECK(image.prepare_orbis(linked,"app.elf",65536,reason)!=0);
    std::puts("PASS framebuffer dimensions, range, isolation, input bytes, stop/reprepare and finite CPU admission (no execution claimed)");
}
