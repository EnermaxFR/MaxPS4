#pragma once
#include "OrbisLinkerFixture.h"
#include <algorithm>
namespace mission44_fixture {
// Technical shapes only; all bytes and symbol identities are synthetic.
inline std::vector<uint8_t> module(size_t initial,size_t total,size_t align,size_t relocations) {
    auto b=orbis_fixture::make(true);
    using orbis_fixture::put;
    put(b,56,6,2);const size_t ph=64+4*56;
    put(b,ph,7,4);put(b,ph+4,4,4);put(b,ph+8,0x1000);
    put(b,ph+16,0x4000);put(b,ph+32,initial);put(b,ph+40,total);put(b,ph+48,align);
    const size_t data=64+5*56;
    put(b,data,1,4);put(b,data+4,6,4);put(b,data+8,0x1000);put(b,data+16,0x4000);
    put(b,data+32,initial);put(b,data+40,std::max<size_t>(64,total));put(b,data+48,align);
    b.resize(0x1000+initial,0);
    // Orbis local STT_SECTION marker at index 1, not an unresolved NID.
    put(b,0x818,0,4);b[0x81c]=3;put(b,0x81e,0,2);put(b,0x820,0);put(b,0x828,0);
    if(initial)b[0x1000]=42;
    if(relocations) {
        orbis_fixture::appendTag(b,0x6100002f,0x300);
        orbis_fixture::appendTag(b,0x61000031,relocations*24);
        orbis_fixture::appendTag(b,0x61000033,24);
        for(size_t i=0;i<relocations;++i){put(b,0x900+i*24,0x2000+i*16);put(b,0x908+i*24,(uint64_t{1}<<32)|16);}
    }
    return b;
}
}
