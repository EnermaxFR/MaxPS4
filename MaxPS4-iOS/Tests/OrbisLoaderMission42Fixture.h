#pragma once
#include <vector>
#include <cstdint>
#include <cstring>
namespace mission42 {
inline void put(std::vector<uint8_t>& b,size_t p,uint64_t n,size_t w=8) {
    for(size_t i=0;i<w;++i) b.at(p+i)=uint8_t(n>>(8*i));
}
inline std::vector<uint8_t> elf() {
    std::vector<uint8_t> b(128);
    std::memcpy(b.data(),"\177ELF\2\1\1\11",8);
    put(b,16,0xfe00,2); put(b,18,62,2); put(b,20,1,4);
    put(b,24,0x1000); put(b,32,64); put(b,52,64,2);
    put(b,54,56,2); put(b,56,1,2);
    put(b,64,1,4); put(b,68,5,4); put(b,72,120);
    put(b,80,0x1000); put(b,96,8); put(b,104,16); put(b,112,1);
    b[120]=0xc3; return b;
}
inline std::vector<uint8_t> self() {
    auto e=elf(); std::vector<uint8_t> b(192);
    put(b,0,0x1d3d154f,4); b[5]=1;b[6]=1;b[7]=0x12;b[8]=1;b[9]=1;
    put(b,12,184,2); put(b,16,b.size(),4); put(b,24,1,2);
    put(b,32,0x800);put(b,40,184);put(b,48,8);put(b,56,8);
    std::copy(e.begin(),e.begin()+120,b.begin()+64);
    std::copy(e.begin()+120,e.end(),b.begin()+184); return b;
}
}
