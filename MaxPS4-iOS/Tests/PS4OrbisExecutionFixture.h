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
inline void use_libkernel(std::vector<uint8_t>& b, const char* nid);
inline std::vector<uint8_t> make_kernel_app(const char* nid="aI+OeCz8xrQ") {
    auto b=make(false);
    use_libkernel(b,nid);
    return b;
}
// Rewrites the fixture's single import to come from the system libkernel.
inline void use_libkernel(std::vector<uint8_t>& b, const char* nid) {
    using orbis_fixture::put;
    std::fill(b.begin()+0x601,b.begin()+0x640,0);
    char strings[]="libkernel\0libkernel\0aI+OeCz8xrQ#B#B";
    std::copy(nid,nid+11,strings+20);
    std::copy(std::begin(strings),std::end(strings),b.begin()+0x601);
    orbis_fixture::setTag(b,0x61000037,1+sizeof(strings));
    orbis_fixture::setTag(b,0x61000015,1|(uint64_t{1}<<32)|(uint64_t{1}<<48));
    orbis_fixture::setTag(b,0x6100000f,11|(uint64_t{1}<<32)|(uint64_t{1}<<48));
    put(b,0x818,21,4);
}
// General-dynamic TLS: tls_index {module, offset} at 0x8020 filled by DTPMOD64
// and DTPOFF64 (symbol 0, A = `offset`); LEA RDI,[RIP+tls_index];
// CALL [GOT] -> __tls_get_addr (NID vNe1w4diLCs); MOV EAX,[RAX]; ADD EAX,2; HLT.
constexpr uint64_t tls_index_address=data+0x20;
inline std::vector<uint8_t> make_tls_get_addr_app(uint32_t initial=0x4242, uint64_t offset=0) {
    auto b=make_tls_app(-8,initial);
    use_libkernel(b,"vNe1w4diLCs");
    using orbis_fixture::put;
    const uint8_t code[]={0x48,0x8d,0x3d,0,0,0,0, 0xff,0x15,0,0,0,0, 0x8b,0x00, 0x05,2,0,0,0, 0xf4};
    std::fill(b.begin()+0x200,b.begin()+0x300,0);
    std::copy(std::begin(code),std::end(code),b.begin()+0x200);
    put(b,0x203,0x8020-(0x4000+7),4);
    put(b,0x209,0x8000-(0x4000+13),4);
    put(b,64+32,sizeof(code)); put(b,64+40,sizeof(code));
    put(b,0x918,0x8020); put(b,0x920,16); put(b,0x928,0);      // DTPMOD64 -> module ID
    put(b,0x930,0x8028); put(b,0x938,17); put(b,0x940,offset); // DTPOFF64 -> offset
    orbis_fixture::setTag(b,0x61000031,3*24);
    return b;
}
// An executable with DT_INIT (0x400C, stores 40 at 0x8010) and a DT_INIT_ARRAY
// entry (0x8030, filled by RELATIVE, adds 2). The engine validates these but
// never runs them: an executable's own startup code does, from its entry.
inline std::vector<uint8_t> make_init_app() {
    auto b=make_kernel_app();
    using orbis_fixture::put;
    const uint8_t code[]={
        0x8b,0x05,0,0,0,0, 0x05,2,0,0,0, 0xf4,                       // entry  0x4000
        0xb8,40,0,0,0, 0x89,0x05,0,0,0,0, 0xc3,                      // init_1 0x400C
        0x8b,0x05,0,0,0,0, 0x05,2,0,0,0, 0x89,0x05,0,0,0,0, 0xc3};   // init_2 0x4018
    std::fill(b.begin()+0x200,b.begin()+0x300,0);
    std::copy(std::begin(code),std::end(code),b.begin()+0x200);
    put(b,0x202,0x8010-0x4006,4);
    put(b,0x213,0x8010-0x4017,4);
    put(b,0x21a,0x8010-0x401e,4);
    put(b,0x225,0x8010-0x4029,4);
    put(b,64+32,sizeof(code)); put(b,64+40,sizeof(code));
    orbis_fixture::appendTag(b,12,0x400c);
    orbis_fixture::appendTag(b,25,0x8030);
    orbis_fixture::appendTag(b,27,8);
    put(b,0x918,0x8030); put(b,0x920,8); put(b,0x928,0x4018); // RELATIVE -> init_2
    orbis_fixture::setTag(b,0x61000031,2*24);
    return b;
}
// module_start: provider.prx DT_INIT (0x4006) stores 40 at its data 0x8010; its
// DT_INIT_ARRAY entry (0x4012) would add 1000. app.elf DT_INIT (0x400C) would
// store 99 there; app entry loads it and adds 2. Only the PRX DT_INIT may run
// (module_start(0, NULL, NULL)), so the result must be exactly 42.
constexpr uint64_t provider_value=provider_bias+0x8010;
inline std::vector<uint8_t> make_start_provider() {
    auto b=make(true);
    using orbis_fixture::put;
    const uint8_t code[]={
        0xb8,40,0,0,0, 0x89,0x05,0,0,0,0, 0xc3,                          // DT_INIT 0x4006
        0x8b,0x05,0,0,0,0, 0x05,0xe8,0x03,0,0, 0x89,0x05,0,0,0,0, 0xc3}; // array  0x4012
    std::copy(std::begin(code),std::end(code),b.begin()+0x206);
    put(b,0x20d,0x8010-0x4011,4);
    put(b,0x214,0x8010-0x4018,4);
    put(b,0x21f,0x8010-0x4023,4);
    put(b,64+32,6+sizeof(code)); put(b,64+40,6+sizeof(code));
    orbis_fixture::appendTag(b,12,0x4006);
    orbis_fixture::appendTag(b,25,0x8030);
    orbis_fixture::appendTag(b,27,8);
    orbis_fixture::appendTag(b,0x6100002f,0x300);
    orbis_fixture::appendTag(b,0x61000031,24);
    orbis_fixture::appendTag(b,0x61000033,24);
    put(b,0x900,0x8030); put(b,0x908,8); put(b,0x910,0x4012); // RELATIVE -> array function
    return b;
}
inline std::vector<uint8_t> make_start_app() {
    auto b=make(false);
    using orbis_fixture::put;
    const uint8_t code[]={
        0x8b,0x05,0,0,0,0, 0x05,2,0,0,0, 0xf4,      // entry   0x4000
        0xb8,99,0,0,0, 0x89,0x05,0,0,0,0, 0xc3};    // DT_INIT 0x400C (must not run)
    std::fill(b.begin()+0x200,b.begin()+0x300,0);
    std::copy(std::begin(code),std::end(code),b.begin()+0x200);
    put(b,0x202,uint32_t(provider_value-(app_bias+0x4006)),4);
    put(b,0x213,uint32_t(provider_value-(app_bias+0x4017)),4);
    put(b,64+32,sizeof(code)); put(b,64+40,sizeof(code));
    orbis_fixture::appendTag(b,12,0x400c);
    return b;
}
// sceKernelClockGettime (public NID QBi7HCK03hw) with a pointer argument:
// MOV EDI,clock; LEA RSI,[RIP+timespec]; CALL [GOT]; ADD EAX,2; HLT.
// The 16-byte timespec lands in app data at 0x8010; success returns 0 + 2.
constexpr uint64_t timespec_address=data+0x10;
inline std::vector<uint8_t> make_clock_app(uint32_t clock=4, int32_t lea_target=0x8010) {
    auto b=make_kernel_app("QBi7HCK03hw");
    using orbis_fixture::put;
    const uint8_t code[]={0xbf,0,0,0,0, 0x48,0x8d,0x35,0,0,0,0, 0xff,0x15,0,0,0,0, 0x05,2,0,0,0, 0xf4};
    std::fill(b.begin()+0x200,b.begin()+0x300,0);
    std::copy(std::begin(code),std::end(code),b.begin()+0x200);
    put(b,0x201,clock,4);
    put(b,0x208,uint32_t(lea_target-(0x4000+12)),4);
    put(b,0x20e,0x8000-(0x4000+18),4);
    put(b,64+32,sizeof(code)); put(b,64+40,sizeof(code));
    return b;
}
// General execution (identity mapping, no instruction list). The bounded
// admission refuses this code (JNZ, PUSH/POP, local CALL, absolute loads):
//   EDX = 1+..+10 (loop); EAX = twice(EDX) via CALL rel32 with a frame;
//   EDX += *[slot], slot filled by RELATIVE with the address of `cell`;
//   PUSH RDX; CALL [GOT] -> provider.prx (unpatched); POP RDX; EAX += EDX;
//   store EAX in BSS (0x8100); HLT. Result: 110 + cell + provider value.
constexpr uint64_t general_slot=data+0x10, general_cell=data+0x20, general_store=data+0x100;
inline std::vector<uint8_t> make_general_app(uint32_t cell=7) {
    auto b=make(false);
    using orbis_fixture::put;
    const uint8_t code[]={
        0x31,0xd2, 0xb9,10,0,0,0,                 // 0x00 XOR EDX,EDX; MOV ECX,10
        0x01,0xca, 0xff,0xc9, 0x75,0xfa,          // 0x07 loop: ADD EDX,ECX; DEC ECX; JNZ loop
        0x89,0xd7, 0xe8,0x1c,0,0,0,               // 0x0d MOV EDI,EDX; CALL twice (0x30)
        0x89,0xc2, 0x48,0x8b,0x05,0,0,0,0,        // 0x14 MOV EDX,EAX; MOV RAX,[RIP+slot]
        0x03,0x10, 0x52, 0xff,0x15,0,0,0,0,       // 0x1d ADD EDX,[RAX]; PUSH RDX; CALL [GOT]
        0x5a, 0x01,0xd0, 0x89,0x05,0,0,0,0, 0xf4, // 0x26 POP RDX; ADD EAX,EDX; MOV [store],EAX; HLT
        0x55, 0x48,0x89,0xe5, 0x89,0xf8,          // 0x30 twice: PUSH RBP; MOV RBP,RSP; MOV EAX,EDI
        0x01,0xc0, 0x5d, 0xc3};                   // 0x36 ADD EAX,EAX; POP RBP; RET
    std::fill(b.begin()+0x200,b.begin()+0x300,0);
    std::copy(std::begin(code),std::end(code),b.begin()+0x200);
    put(b,0x219,0x8010-0x401d,4);
    put(b,0x222,0x8000-0x4026,4);
    put(b,0x22b,0x8100-0x402f,4);
    put(b,64+32,sizeof(code)); put(b,64+40,sizeof(code));
    put(b,0x320,cell,4);
    put(b,0x918,0x8010); put(b,0x920,8); put(b,0x928,0x8020); // RELATIVE -> cell
    orbis_fixture::setTag(b,0x61000031,2*24);
    return b;
}
// sceKernelClockGettime(clock, RSP buffer): the timespec lives on the guest
// stack, then is copied to app data at 0x8010. Returns the HLE result.
inline std::vector<uint8_t> make_general_clock_app(uint32_t clock=4) {
    auto b=make_kernel_app("QBi7HCK03hw");
    using orbis_fixture::put;
    const uint8_t code[]={
        0xbf,0,0,0,0, 0x48,0x83,0xec,0x10, 0x48,0x89,0xe6, // MOV EDI,clock; SUB RSP,16; MOV RSI,RSP
        0xff,0x15,0,0,0,0,                                  // 0x0c CALL [GOT]
        0x48,0x8b,0x14,0x24, 0x48,0x89,0x15,0,0,0,0,        // 0x12 MOV RDX,[RSP]; MOV [0x8010],RDX
        0x48,0x8b,0x54,0x24,0x08, 0x48,0x89,0x15,0,0,0,0,   // 0x1d MOV RDX,[RSP+8]; MOV [0x8018],RDX
        0x48,0x83,0xc4,0x10, 0xf4};                         // 0x29 ADD RSP,16; HLT
    std::fill(b.begin()+0x200,b.begin()+0x300,0);
    std::copy(std::begin(code),std::end(code),b.begin()+0x200);
    put(b,0x201,clock,4);
    put(b,0x20e,0x8000-0x4012,4);
    put(b,0x219,0x8010-0x401d,4);
    put(b,0x225,0x8018-0x4029,4);
    put(b,64+32,sizeof(code)); put(b,64+40,sizeof(code));
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
