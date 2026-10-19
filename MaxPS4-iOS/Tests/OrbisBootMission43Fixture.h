#pragma once
#include "OrbisLinkerFixture.h"
#include <algorithm>
namespace mission43_fixture {
static std::vector<uint8_t> wrap(const std::vector<uint8_t>& elf) {
    auto read=[&](size_t p,size_t n) {uint64_t value=0;for(size_t i=0;i<n;++i)value|=uint64_t(elf.at(p+i))<<(8*i);return value;};
    const size_t count=read(56,2), ph=read(32,8), origin=32+count*32;
    const size_t header=(origin+64+count*56+15)&~size_t(15);
    std::vector<uint8_t> b(header,0);
    orbis_fixture::put(b,0,0x1d3d154f,4);
    b[5]=1;b[6]=1;b[7]=0x12;b[8]=1;b[9]=1;
    orbis_fixture::put(b,12,header,2);orbis_fixture::put(b,24,count,2);
    std::copy_n(elf.begin(),64,b.begin()+origin);
    orbis_fixture::put(b,origin+32,64);
    std::copy_n(elf.begin()+ph,count*56,b.begin()+origin+64);
    for (size_t i=0;i<count;++i) {
        const size_t offset=read(ph+i*56+8,8), size=read(ph+i*56+32,8);
        b.resize((b.size()+15)&~size_t(15),0);const size_t source=b.size();
        b.insert(b.end(),elf.begin()+offset,elf.begin()+offset+size);
        orbis_fixture::put(b,32+i*32,0x804|(i<<20));orbis_fixture::put(b,40+i*32,source);
        orbis_fixture::put(b,48+i*32,size);orbis_fixture::put(b,56+i*32,size);
    }
    orbis_fixture::put(b,16,b.size(),4);return b;
}
}
