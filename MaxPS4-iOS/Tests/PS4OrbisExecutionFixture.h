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
// Same module, compiler-style access: MOV RAX,FS:[0]; MOV EAX,[RAX+disp]
// (disp8 or disp32 encoding); ADD EAX,2; HLT. `gap` inserts a NOP between the
// two loads, which must invalidate RAX as the thread pointer.
inline std::vector<uint8_t> make_tls_register_app(int32_t disp=-8, bool disp32=false,
                                                  uint32_t initial=0x1234, bool gap=false) {
    auto b=make_tls_app(-8,initial);
    std::vector<uint8_t> code={0x64,0x48,0x8b,0x04,0x25,0,0,0,0};
    if (gap) code.push_back(0x90);
    if (disp32) { code.insert(code.end(),{0x8b,0x80}); for (int i=0;i<4;++i) code.push_back(uint8_t(uint32_t(disp)>>(8*i))); }
    else code.insert(code.end(),{0x8b,0x40,uint8_t(int8_t(disp))});
    code.insert(code.end(),{0x05,2,0,0,0,0xf4});
    std::fill(b.begin()+0x200,b.begin()+0x300,0);
    std::copy(code.begin(),code.end(),b.begin()+0x200);
    orbis_fixture::put(b,64+32,code.size()); orbis_fixture::put(b,64+40,code.size());
    return b;
}
// Initial-exec model: a GOT slot at 0xC000 receives TPOFF64 (sym 0, A=0 -> -8)
// from the linker; MOV RAX,[RIP+slot]; MOV EAX,FS:[RAX]; ADD EAX,2; HLT.
// With `relro`, the slot lives in a PT_SCE_RELRO segment that becomes read-only
// after linking; otherwise it is an ordinary RW PT_LOAD and must be refused.
constexpr uint64_t tls_got=app_bias+0xc000; // Own 16 KiB page (iOS/macOS arm64).

inline std::vector<uint8_t> make_tls_initial_exec_app(bool relro=true, uint32_t initial=0x1234, bool gap=false) {
    auto b=make_tls_app(-8,initial);
    using orbis_fixture::put;
    std::vector<uint8_t> code={0x48,0x8b,0x05,0,0,0,0};
    if (gap) code.push_back(0x90);
    code.insert(code.end(),{0x64,0x8b,0x00,0x05,2,0,0,0,0xf4});
    std::fill(b.begin()+0x200,b.begin()+0x300,0);
    std::copy(code.begin(),code.end(),b.begin()+0x200);
    put(b,0x203,0xc000-(0x4000+7),4);
    put(b,64+32,code.size()); put(b,64+40,code.size());
    put(b,56,6,2);
    const size_t p=64+5*56;
    put(b,p,relro ? 0x61000010 : 1,4); put(b,p+4,6,4); put(b,p+8,0x3c0); put(b,p+16,0xc000);
    put(b,p+32,8); put(b,p+40,8); put(b,p+48,1);
    put(b,0x918,0xc000); put(b,0x920,18); put(b,0x928,0); // TPOFF64, symbol 0, A=0.
    orbis_fixture::setTag(b,0x61000031,2*24);
    return b;
}
// The app alone, importing scePthreadSelf (public NID aI+OeCz8xrQ) from the
// system libkernel instead of the fixture PRX: CALL [GOT]; ADD EAX,2; store; HLT.
// Thread 1 is the only PS4 thread, so a real HLE call returns 1 + 2 = 3.
inline std::vector<uint8_t> make_kernel_app() {
    auto b=make(false);
    using orbis_fixture::put;
    std::fill(b.begin()+0x601,b.begin()+0x640,0);
    const char strings[]="libkernel\0libkernel\0aI+OeCz8xrQ#B#B";
    std::copy(std::begin(strings),std::end(strings),b.begin()+0x601);
    orbis_fixture::setTag(b,0x61000037,1+sizeof(strings));
    orbis_fixture::setTag(b,0x61000015,1|(uint64_t{1}<<32)|(uint64_t{1}<<48));
    orbis_fixture::setTag(b,0x6100000f,11|(uint64_t{1}<<32)|(uint64_t{1}<<48));
    put(b,0x818,21,4);
    return b;
}
inline maxps4::OrbisLinker::Identity kernel_identity(const char* nid="aI+OeCz8xrQ") {
    return {nid,"libkernel","libkernel",1,1,2};
}
inline maxps4::OrbisLinker::Input input(const char* path,const std::vector<uint8_t>& b,bool provider) {
    return {{{path,provider ? std::vector<std::string>{} : std::vector<std::string>{"provider.prx"}},b.data(),b.size()},
            provider ? provider_bias : app_bias};
}
}
