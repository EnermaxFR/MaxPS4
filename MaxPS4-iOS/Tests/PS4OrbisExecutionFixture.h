#pragma once
#include "OrbisLinkerFixture.h"
#include "../Native/OrbisLinker.hpp"
namespace orbis_execution {
constexpr uint64_t app_bias=0x100000, provider_bias=0x200000;
constexpr uint64_t entry=app_bias+0x4000, data=app_bias+0x8000;
inline std::vector<uint8_t> make(bool provider, uint32_t value=40) {
    auto b=orbis_fixture::make(provider);
    using orbis_fixture::put;
    put(b,24,provider ? 0 : 0x4000);
    put(b,64+16,0x4000); put(b,64+56+16,0x8000);
    put(b,64+48,1); put(b,64+56+48,1);
    if (provider) {
        const uint8_t code[]={0xb8,0,0,0,0,0xc3};
        std::copy(std::begin(code),std::end(code),b.begin()+0x200);
        put(b,0x201,value,4); put(b,0x820,0x4000); put(b,0x828,6);
        put(b,64+32,6); put(b,64+40,6);
    } else {
        const uint8_t code[]={0xff,0x15,0,0,0,0,0x05,2,0,0,0,0xc7,5,0,0,0,0,0x34,0x12,0,0,0xf4};
        std::copy(std::begin(code),std::end(code),b.begin()+0x200);
        put(b,0x202,0x8000-(0x4000+6),4);
        put(b,0x20d,0x8008-(0x4000+21),4);
        put(b,0x900,0x8000);
        put(b,64+32,sizeof(code)); put(b,64+40,sizeof(code));
        put(b,64+56+40,0x200); // BSS after relocation slot.
    }
    return b;
}
// App with a PT_TLS block (4 template bytes = `initial`, 8 bytes per thread,
// 4-byte aligned): MOV EAX,FS:[disp]; ADD EAX,2; HLT. Its only TLS block sits
// at TP-8, so disp=-8 reads the variable and the program returns initial+2.
constexpr uint64_t tls_template=app_bias+0x8080;
inline std::vector<uint8_t> make_tls_app(int32_t disp=-8, uint32_t initial=0x1234) {
    auto b=make(false);
    using orbis_fixture::put;
    const uint8_t code[]={0x64,0x8b,0x04,0x25,0,0,0,0,0x05,2,0,0,0,0xf4};
    std::fill(b.begin()+0x200,b.begin()+0x300,0);
    std::copy(std::begin(code),std::end(code),b.begin()+0x200);
    put(b,0x204,uint32_t(disp),4);
    put(b,64+32,sizeof(code)); put(b,64+40,sizeof(code));
    put(b,56,5,2);
    const size_t p=64+4*56;
    put(b,p,7,4); put(b,p+4,4,4); put(b,p+8,0x380); put(b,p+16,0x8080);
    put(b,p+32,4); put(b,p+40,8); put(b,p+48,4);
    put(b,0x380,initial,4);
    return b;
}
inline maxps4::OrbisLinker::Input input(const char* path,const std::vector<uint8_t>& b,bool provider) {
    return {{{path,provider ? std::vector<std::string>{} : std::vector<std::string>{"provider.prx"}},b.data(),b.size()},
            provider ? provider_bias : app_bias};
}
}
