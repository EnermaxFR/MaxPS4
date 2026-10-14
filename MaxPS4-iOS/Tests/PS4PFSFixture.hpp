#pragma once
// Original freely redistributable synthetic data; no game, key or license bytes.
#include <vector>
#include <cstdint>
#include <cstring>
namespace pfsFixture {
inline void le(std::vector<uint8_t>& b,size_t at,uint64_t n,size_t w) {
    for(size_t i=0;i<w;++i) b[at+i]=uint8_t(n>>(8*i));
}
inline void be(std::vector<uint8_t>& b,size_t at,uint64_t n,size_t w) {
    for(size_t i=0;i<w;++i) b[at+w-1-i]=uint8_t(n>>(8*i));
}
inline std::vector<uint8_t> image(size_t bs=65536) {
    std::vector<uint8_t> b(5*bs);
    le(b,0,1,8); le(b,8,20130315,8); le(b,0x1c,8,2); le(b,0x20,bs,4);
    le(b,0x28,1,8); le(b,0x30,3,8); le(b,0x38,5,8); le(b,0x40,1,8);
    for(size_t i=0;i<3;++i) {
        const size_t at=bs+i*0xa8,sz=i==2?5:bs;
        le(b,at,i==2?0x816d:0x416d,2); le(b,at+2,1,2); le(b,at+4,0x10,4);
        le(b,at+8,sz,8); le(b,at+0x10,sz,8); le(b,at+0x60,1,4); le(b,at+0x64,i+2,4);
        for(size_t j=1;j<12;++j) le(b,at+0x64+j*4,0xffffffff,4);
    }
    le(b,2*bs,1,4); le(b,2*bs+4,3,4); le(b,2*bs+8,5,4); le(b,2*bs+12,24,4);
    std::memcpy(b.data()+2*bs+16,"uroot",5);
    le(b,3*bs,2,4); le(b,3*bs+4,2,4); le(b,3*bs+8,9,4); le(b,3*bs+12,32,4);
    std::memcpy(b.data()+3*bs+16,"eboot.bin",9);
    std::memcpy(b.data()+4*bs,"hello",5);
    return b;
}
inline std::vector<uint8_t> package(const std::vector<uint8_t>& pfs) {
    const size_t at=65536;
    std::vector<uint8_t> b(at+pfs.size());
    std::memcpy(b.data(),"\177CNT",4);
    be(b,0x10,1,4); be(b,0x16,1,2); be(b,0x18,0x5a0,4);
    be(b,0x430,b.size(),8); be(b,0x410,at,8); be(b,0x418,pfs.size(),8);
    be(b,0x5a0,0x409,4); // unknown empty descriptor: not a PFS file
    std::memcpy(b.data()+at,pfs.data(),pfs.size());
    return b;
}
}
