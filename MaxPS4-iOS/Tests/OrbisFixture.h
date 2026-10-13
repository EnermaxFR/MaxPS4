#pragma once
#include <vector>
#include <string>
#include <cstdint>
namespace orbis_test {
inline void put(std::vector<uint8_t>& f, uint64_t n, size_t off, size_t width = 8) {
    for (size_t i = 0; i < width; ++i) f.at(off+i) = uint8_t(n>>(i*8));
}
inline std::vector<uint8_t> fixture(bool consumer = false, bool tls = false, bool kernel = false) {
    std::vector<uint8_t> f(0x1000);
    f[0]=0x7f; f[1]='E'; f[2]='L'; f[3]='F'; f[4]=2; f[5]=1; f[6]=1; f[7]=9;
    put(f,0xfe18,16,2); put(f,62,18,2); put(f,1,20,4); put(f,0,24);
    put(f,64,32); put(f,64,52,2); put(f,56,54,2); put(f,tls ? 5 : 4,56,2);
    auto ph = [&](size_t i, uint32_t type, uint32_t flags, uint64_t off, uint64_t addr, uint64_t fs, uint64_t ms) {
        size_t p = 64+i*56;
        put(f,type,p,4); put(f,flags,p+4,4); put(f,off,p+8); put(f,addr,p+16);
        put(f,fs,p+32); put(f,ms,p+40); put(f,1,p+48);
    };
    ph(0,1,5,0x400,0x1000,0x100,0x100); f[0x400]=0xc3;
    ph(1,1,6,0x600,0x2000,0x200,0x300);
    ph(2,2,4,0x800,0,0x100,0x100);
    ph(3,0x61000000,4,0xa00,0,0x500,0x500);
    if (tls) ph(4,7,4,0x700,0x2100,4,16);
    std::string strings(1,'\0');
    auto str = [&](const std::string& s) { auto off = strings.size(); strings += s; strings += '\0'; return off; };
    uint64_t ownMod = str(consumer ? "modB" : "modA");
    uint64_t ownLib = str(consumer ? "libB" : "libA");
    uint64_t depMod = str(kernel ? "libkernel" : "modA");
    uint64_t depLib = str(kernel ? "libkernel" : "libA");
    uint64_t symbol = str(consumer ? (kernel ? "aI+OeCz8xrQ#B#B" : "AAAAAAAAAAA#B#B") : "AAAAAAAAAAA#A#A");
    for (size_t i=0; i<strings.size(); ++i) f[0xa00+i]=uint8_t(strings[i]);
    size_t d=0x800;
    auto tag = [&](uint64_t t, uint64_t v) { put(f,t,d); put(f,v,d+8); d+=16; };
    auto identity = [](uint64_t off,uint16_t version,uint16_t id) { return off | (uint64_t(version)<<32) | (uint64_t(id)<<48); };
    tag(0x61000035,0); tag(0x61000037,strings.size());
    tag(0x61000039,0x100); tag(0x6100003b,24); tag(0x6100003f,48);
    tag(0x6100000d,identity(ownMod,0x100,0)); tag(0x61000013,identity(ownLib,1,0));
    if (consumer) { tag(0x6100000f,identity(depMod,0x100,1)); tag(0x61000015,identity(depLib,1,1)); }
    tag(0x6100002f,0x200); tag(0x61000031,48); tag(0x61000033,24); tag(0,0);
    put(f,d-0x800,64+2*56+32); put(f,d-0x800,64+2*56+40);
    put(f,symbol,0xb18,4); f[0xb1c]=0x12;
    put(f,consumer ? 0 : 1,0xb1e,2); put(f,consumer ? 0 : 0x1000,0xb20); put(f,consumer ? 0 : 1,0xb28);
    put(f,0x2000,0xc00); put(f,(1ULL<<32)|6,0xc08); put(f,0,0xc10);
    put(f,0x2008,0xc18); put(f,8,0xc20); put(f,0x1000,0xc28);
    return f;
}
}
