#pragma once
#include "OrbisTLSMission44Fixture.h"
#include "../Native/OrbisLinker.hpp"
#include "OrbisV4CompatibilityFixture.h"
#include "../Native/OrbisV4TLSGetAddr.hpp"
namespace mission45_fixture {
inline std::vector<uint8_t> module(size_t initial,size_t total,size_t align,size_t count) {
    auto bytes=mission44_fixture::module(initial,total,align,count);
    using orbis_fixture::put;
    put(bytes,64+16,0x4000);put(bytes,64+56+16,0x8000);
    put(bytes,64+56,0x61000010,4); // link RW, seal SCE RELRO after relocation
    put(bytes,64+4*56+16,0xc000);put(bytes,64+5*56+16,0xc000);
    for(size_t i=0;i<count;++i)put(bytes,0x900+i*24,0x8000+i*16);
    if(initial>=16 && total>=520) {
        // Ordinary RELATIVE writes into TLS initial data AND TLS BSS must be
        // applied before per-thread templates are instantiated.
        orbis_fixture::setTag(bytes,0x61000031,(count+2)*24);
        for(size_t i=0;i<2;++i) {
            const size_t p=0x900+(count+i)*24;
            put(bytes,p,0xc000+(i ? 512 : 8));put(bytes,p+8,8);put(bytes,p+16,0x4000);
        }
    }
    return bytes;
}
struct Bundle {
    std::vector<uint8_t> app=compat_fixture::consumer("libkernel","libkernel.prx",{maxps4::OrbisV4TLSGetAddr::nid});
    std::vector<uint8_t> libc=module(416,1168,16,13),fios=module(0,64,8,3);
    std::vector<uint8_t> provider=maxps4::OrbisV4TLSGetAddr::provider({{2,1168,16,{}},{3,64,8,{}}});
    Bundle() {
        using orbis_fixture::put;
        put(app,64+16,0x4000);put(app,64+56+16,0x8000);put(app,24,0x4000);
        put(app,0x900,0x8000);
        // Exact SysV call through a genuinely relocated GOT; TLS index is data.
        const uint8_t program[]={0x48,0xbf,0x40,0x80,0x40,0,0,0,0,0,
            0x48,0x83,0xec,8,0xff,0x15,0xec,0x3f,0,0,0x48,0x83,0xc4,8,0x8b,0,0xf4};
        std::copy(std::begin(program),std::end(program),app.begin()+0x200);
        put(app,0x340,2);put(app,0x348,0);
    }
    std::vector<maxps4::OrbisLinker::Input> inputs() const {
        return {{{{"app.prx",{"libkernel.prx"}},app.data(),app.size()},0x400000},
            {{{"libc.prx",{}},libc.data(),libc.size()},0x800000},
            {{{"libSceFios2.prx",{}},fios.data(),fios.size()},0xc00000},
            {{{"libkernel.prx",{}},provider.data(),provider.size()},0x1000000}};
    }
};
}
