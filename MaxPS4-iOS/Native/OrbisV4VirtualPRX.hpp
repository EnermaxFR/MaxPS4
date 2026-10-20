#pragma once
#include "OrbisMetadata.hpp"
namespace maxps4 {
// Small internal ELF/SCE writer for explicit compatibility providers. It does
// not parse user input, remove dependencies, resolve imports or execute code.
struct OrbisV4VirtualPRX {
    struct Export {std::string nid;size_t offset,size;};
    static std::vector<uint8_t> build(const std::string& name,const std::vector<uint8_t>& code,
                                      size_t stateSize,const std::vector<Export>& exports,uint16_t moduleVersion=0x101) {
        if(name.empty() || name.size()>64 || code.empty() || code.size()>0x400 ||
           !stateSize || stateSize>0x100 || exports.empty() || exports.size()>16)
            throw std::length_error("Virtual PRX budget");
        std::vector<uint8_t> b(0x1400,0);
        auto put=[&](size_t p,uint64_t v,size_t n=8){for(size_t i=0;i<n;++i)b.at(p+i)=uint8_t(v>>(8*i));};
        const uint8_t magic[]={0x7f,'E','L','F',2,1,1,9,0};
        std::copy(std::begin(magic),std::end(magic),b.begin());
        put(16,0xfe18,2);put(18,62,2);put(20,1,4);put(32,64);
        put(52,64,2);put(54,56,2);put(56,4,2);
        auto ph=[&](size_t i,uint32_t t,uint32_t f,uint64_t off,uint64_t addr,uint64_t size){
            const auto p=64+i*56;put(p,t,4);put(p+4,f,4);put(p+8,off);put(p+16,addr);
            put(p+32,size);put(p+40,size);put(p+48,1);
        };
        ph(0,1,5,0x200,0x4000,code.size());ph(1,1,6,0x600,0x8000,stateSize);
        ph(2,2,4,0x700,0,0x180);ph(3,0x61000000,4,0x900,0,0xb00);
        std::copy(code.begin(),code.end(),b.begin()+0x200);
        size_t cursor=0x901;
        auto text=[&](const std::string& s){if(s.size()+1>0xb00-cursor)throw std::length_error("Virtual PRX strings");
            const auto p=cursor-0x900;for(char c:s)b.at(cursor++)=uint8_t(c);b.at(cursor++)=0;return p;};
        const auto descriptor=text(name);
        for(size_t i=0;i<exports.size();++i){const auto& e=exports[i];
            if(e.nid.size()!=11 || e.offset>=code.size() || !e.size || e.size>code.size()-e.offset)
                throw std::invalid_argument("Virtual PRX export extent");
            const auto n=text(e.nid+"#B#B"),p=0xb00+(i+1)*24;
            put(p,n,4);b[p+4]=0x12;put(p+6,1,2);put(p+8,0x4000+e.offset);put(p+16,e.size);
        }
        size_t d=0x700;
        auto tag=[&](uint64_t t,uint64_t v){put(d,t);put(d+8,v);d+=16;};
        tag(0x61000035,0);tag(0x61000037,cursor-0x900);tag(0x61000039,0x200);
        tag(0x6100003b,24);tag(0x6100003f,(exports.size()+1)*24);
        tag(0x61000013,descriptor|(uint64_t{1}<<32)|(uint64_t{1}<<48));
        tag(0x6100000d,descriptor|(uint64_t{moduleVersion}<<32)|(uint64_t{1}<<48));
        return b;
    }
};
}
