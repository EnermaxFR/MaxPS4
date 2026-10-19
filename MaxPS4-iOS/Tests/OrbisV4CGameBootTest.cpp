#include "../Native/OrbisV4PRXLaunch.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
    assert(argc==3);
    for (int i=1;i<=2;++i) {
        std::ifstream file(argv[i],std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
        assert(bytes.size()>=64 && bytes.size()<=1024*1024);
        maxps4_v4_prx_request r{bytes.data(),bytes.size(),0,0x400000,MAXPS4_V4_ABI_STARTUP_RETURN,3};
        std::unique_ptr<maxps4::OrbisV4Translation> owner; std::string reason;
        const int status=maxps4::OrbisV4PRXLaunch::prepare(r,[]{},owner,&reason);
        if (i==2) { assert(status==-ENOTSUP && !owner && reason.find("V4.INSTRUCTION")!=std::string::npos); continue; }
        assert(status==0 && owner);
        maxps4::PS4ExecutableImage image;
        assert(image.load(bytes.data(),bytes.size())==0 && image.type==0xfe00);
        bool bssChecked=false;
        for (const auto& segment:image.segments) {
            const auto& h=segment.header;
            if (h.flags!=6) continue;
            uint32_t marker=0; assert(owner->read(h.address,&marker,4)==0 && marker==0xc0de);
            assert(h.memorySize>h.fileSize && h.memorySize-h.fileSize<=4096);
            std::vector<uint8_t> bss(h.memorySize-h.fileSize,255);
            assert(owner->read(h.address+h.fileSize,bss.data(),bss.size())==0);
            for (auto byte:bss) assert(!byte);
            bssChecked=true;
        }
        assert(bssChecked);
        {auto lease=owner->lease();
         assert(!lease.permitted(r.entry,1,2));
         assert(!lease.permitted(0x410000,1,1));
         assert(!lease.permitted(maxps4::OrbisV4Startup::stack,1,1));}
        owner->close();
    }
    std::cout<<"PASS C homebrew: optimized exact-file admission, data/BSS, RX/RW-NX; unoptimized prologue refused before CPU dispatch\n";
}
