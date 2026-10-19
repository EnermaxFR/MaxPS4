#pragma once
#include "OrbisLinkerFixture.h"
namespace compat_fixture {
// Fabricated app with real import identities, actual GOT calls and RELA.
inline std::vector<uint8_t> consumer(const std::string& module,const std::string& dependency,
                                     const std::vector<std::string>& nids) {
    if(nids.empty() || nids.size()>5)throw std::length_error("compat fixture imports");
    auto app=orbis_fixture::make(false);
    std::fill(app.begin()+0x600,app.begin()+0x800,0);
    size_t cursor=0x601;
    auto text=[&](const std::string& s){const auto off=cursor-0x600;
        for(char c:s)app.at(cursor++)=uint8_t(c);
        app.at(cursor++)=0;return off;};
    const auto library=text(module),needed=text(dependency);
    for(size_t i=0;i<nids.size();++i){
        const auto name=text(nids[i]+"#B#B"),sym=0x800+(i+1)*24;
        orbis_fixture::put(app,sym,name,4);app[sym+4]=0x12;
        orbis_fixture::put(app,0x900+i*24,0x2000+i*8);
        orbis_fixture::put(app,0x908+i*24,(uint64_t(i+1)<<32)|7);
        const auto entry=0x200+i*32;
        app[entry]=0xff;app[entry+1]=0x15;
        orbis_fixture::put(app,entry+2,0x2000+i*8-(0x1000+i*32+6),4);
        app[entry+6]=0xf4;
    }
    orbis_fixture::setTag(app,0x61000037,cursor-0x600);
    orbis_fixture::setTag(app,0x61000015,library|(uint64_t{1}<<32)|(uint64_t{1}<<48));
    orbis_fixture::setTag(app,0x6100000f,library|(uint64_t{0x101}<<32)|(uint64_t{1}<<48));
    orbis_fixture::setTag(app,0x6100003f,(nids.size()+1)*24);
    orbis_fixture::setTag(app,0x61000031,nids.size()*24);
    orbis_fixture::appendTag(app,1,needed);
    return app;
}
// Runtime-mapped variant: RX and RW must occupy distinct 16 KiB host pages.
// Keep consumer() unchanged for metadata/linker tests using its 4 KiB addresses.
inline std::vector<uint8_t> pagedConsumer(const std::string& module,const std::string& dependency,
                                        const std::vector<std::string>& nids) {
    auto app=consumer(module,dependency,nids);
    orbis_fixture::put(app,24,0x4000);
    orbis_fixture::put(app,64+16,0x4000);
    orbis_fixture::put(app,64+56+16,0x8000);
    for(size_t i=0;i<nids.size();++i) {
        orbis_fixture::put(app,0x900+i*24,0x8000+i*8);
        orbis_fixture::put(app,0x202+i*32,0x8000+i*8-(0x4000+i*32+6),4);
    }
    return app;
}
}
