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
// General-mode guest faults (same provider dependency as make()):
//   0: MOV EAX,[RSP+0x1000] - past the stack top, in its guard page (SIGSEGV)
//   1: MOV DWORD [RIP-10],0 - a store into its own read-only code (SIGSEGV)
//   2: UD2                  - invalid opcode (SIGILL)
//   3: JMP $                - endless loop (needs a time limit)
// Each is followed by HLT, which must never be reached.
inline std::vector<uint8_t> make_general_fault_app(int kind) {
    auto b=make(false);
    using orbis_fixture::put;
    std::vector<uint8_t> code;
    if (kind == 0) code={0x8b,0x84,0x24,0x00,0x10,0,0, 0xf4};
    else if (kind == 1) code={0xc7,0x05,0xf6,0xff,0xff,0xff,0,0,0,0, 0xf4};
    else if (kind == 2) code={0x0f,0x0b, 0xf4};
    else code={0xeb,0xfe, 0xf4};
    std::fill(b.begin()+0x200,b.begin()+0x300,0);
    std::copy(code.begin(),code.end(),b.begin()+0x200);
    put(b,64+32,code.size()); put(b,64+40,code.size());
    return b;
}
// sceKernelMmap (public NID PGhQHd-dzv8) from general code, seventh argument
// on the stack: mmap(0, 0x4000, READ|WRITE, ANON|PRIVATE, -1, 0, &out) with
// `out` at app data 0x8010. On success the guest stores 0x1234 at out+0x100
// through the returned pointer, loads it back and returns 0x1236; on failure
// it returns the libkernel error.
constexpr uint64_t mmap_out=data+0x10;
inline std::vector<uint8_t> make_general_mmap_app() {
    auto b=make_kernel_app("PGhQHd-dzv8");
    using orbis_fixture::put;
    const uint8_t code[]={
        0x31,0xff, 0xbe,0,0x40,0,0, 0xba,3,0,0,0,            // 0x00 XOR EDI,EDI; MOV ESI,0x4000; MOV EDX,3
        0xb9,0x02,0x10,0,0, 0x41,0xb8,0xff,0xff,0xff,0xff,  // 0x0c MOV ECX,0x1002; MOV R8D,-1
        0x45,0x31,0xc9, 0x48,0x8d,0x05,0,0,0,0, 0x50,        // 0x17 XOR R9D,R9D; LEA RAX,[out]; PUSH RAX
        0xff,0x15,0,0,0,0, 0x59, 0x85,0xc0, 0x75,0x1c,       // 0x22 CALL [GOT]; POP RCX; TEST EAX,EAX; JNZ end
        0x48,0x8b,0x05,0,0,0,0,                              // 0x2d MOV RAX,[out]
        0xc7,0x80,0,1,0,0,0x34,0x12,0,0,                     // 0x34 MOV DWORD [RAX+0x100],0x1234
        0x8b,0x80,0,1,0,0, 0x05,2,0,0,0, 0xf4};              // 0x3e MOV EAX,[RAX+0x100]; ADD EAX,2; end: HLT
    std::fill(b.begin()+0x200,b.begin()+0x300,0);
    std::copy(std::begin(code),std::end(code),b.begin()+0x200);
    put(b,0x21d,0x8010-0x4021,4);
    put(b,0x224,0x8000-0x4028,4);
    put(b,0x230,0x8010-0x4034,4);
    put(b,64+32,sizeof(code)); put(b,64+40,sizeof(code));
    return b;
}
// PS4 process entry (general mode): checks RDI -> EntryParams {argc 1,
// argv[0] "eboot.bin"}, its first 8 bytes copied at [RSP], RSP = 8 (mod 16),
// then calls the exit function in RSI with argc+41 = 42 (7 if a check fails).
inline std::vector<uint8_t> make_general_entry_app() {
    auto b=make_kernel_app();
    using orbis_fixture::put;
    const uint8_t code[]={
        0x8b,0x07, 0x48,0x8b,0x57,0x08, 0x48,0x8b,0x12,            // 0x00 EAX=argc; RDX=*argv[0]
        0x48,0xb9,'e','b','o','o','t','.','b','i',                 // 0x09 MOV RCX,"eboot.bi"
        0x48,0x39,0xca, 0x75,0x1a,                                 // 0x13 CMP RDX,RCX; JNZ bad
        0x48,0x8b,0x0c,0x24, 0x48,0x3b,0x0f, 0x75,0x11,            // 0x18 CMP [RSP],[RDI]; JNZ bad
        0x48,0x89,0xe1, 0x83,0xe1,0x0f, 0x83,0xf9,0x08, 0x75,0x06, // 0x21 RSP%16==8 or bad
        0x8d,0x78,0x29, 0xff,0xd6, 0xf4,                           // 0x2c EDI=argc+41; CALL RSI; HLT
        0xbf,7,0,0,0, 0xff,0xd6, 0xf4};                            // 0x32 bad: EDI=7; CALL RSI; HLT
    std::fill(b.begin()+0x200,b.begin()+0x300,0);
    std::copy(std::begin(code),std::end(code),b.begin()+0x200);
    put(b,64+32,sizeof(code)); put(b,64+40,sizeof(code));
    return b;
}
// Several system libkernel imports: symbol i+1 is `nids[i]`, bound by a
// JUMP_SLOT to GOT slot data+8*i.
inline void use_libkernel_imports(std::vector<uint8_t>& b, std::initializer_list<const char*> nids) {
    using orbis_fixture::put;
    use_libkernel(b,*nids.begin());
    std::fill(b.begin()+0x615,b.begin()+0x700,0);
    std::fill(b.begin()+0x818,b.begin()+0x900,0);
    std::fill(b.begin()+0x900,b.begin()+0xa00,0);
    size_t i=0;
    for (const char* nid : nids) {
        const size_t name=21+16*i;
        std::copy(nid,nid+11,b.begin()+0x600+name);
        std::copy_n("#B#B",4,b.begin()+0x600+name+11);
        put(b,0x818+24*i,name,4); b[0x81c+24*i]=0x12;
        put(b,0x900+24*i,0x8000+8*i); put(b,0x908+24*i,((i+1)<<32)|7);
        ++i;
    }
    orbis_fixture::setTag(b,0x61000037,21+16*i);
    orbis_fixture::setTag(b,0x6100003f,24*(i+1));
    orbis_fixture::setTag(b,0x61000031,24*i);
}
// Two guest threads (general mode). The entry creates thread A (entry A, arg
// &40) and thread B (entry B, arg 5) with scePthreadCreate, joins both with
// scePthreadJoin and exits with A's value + B's value = 100 (or the failing
// call's error). A sets flag 0x8068 then spins until B sets 0x806c, and B
// waits for A first: only truly concurrent threads finish. A also checks its
// scePthreadSelf is not the main thread's (1) and that RSP = 8 (mod 16), then
// returns [arg]+2 = 42 (7 on failure); B ends with scePthreadExit(arg+53 = 58).
constexpr uint64_t threads_values=data+0x50;
inline std::vector<uint8_t> make_general_threads_app() {
    auto b=make_kernel_app();
    use_libkernel_imports(b,{"6UgtwV+0zb4","onNY9Byn-W8","3kg7rT0NQIs","aI+OeCz8xrQ"});
    using orbis_fixture::put;
    constexpr uint64_t create=data, join=data+8, quit=data+0x10, self=data+0x18;
    constexpr uint64_t handleA=data+0x40, handleB=data+0x48, valueA=data+0x50, valueB=data+0x58;
    constexpr uint64_t arg=data+0x60, flagA=data+0x68, flagB=data+0x6c;
    constexpr size_t A=0x8e, B=0xc4;
    const uint8_t code[]={
        0x53, 0x48,0x89,0xf3,                                  // 0x00 PUSH RBX; MOV RBX,RSI (exit)
        0x48,0x8d,0x3d,0,0,0,0, 0x31,0xf6,                     // 0x04 LEA RDI,[handleA]; XOR ESI,ESI
        0x48,0x8d,0x15,0,0,0,0, 0x48,0x8d,0x0d,0,0,0,0,        // 0x0d LEA RDX,[A]; LEA RCX,[arg]
        0x45,0x31,0xc0, 0xff,0x15,0,0,0,0, 0x85,0xc0,0x75,0x61, // 0x1b XOR R8D,R8D; CALL [create]; JNZ fail
        0x48,0x8d,0x3d,0,0,0,0, 0x31,0xf6,                     // 0x28 LEA RDI,[handleB]; XOR ESI,ESI
        0x48,0x8d,0x15,0,0,0,0, 0xb9,5,0,0,0,                  // 0x31 LEA RDX,[B]; MOV ECX,5
        0x45,0x31,0xc0, 0xff,0x15,0,0,0,0, 0x85,0xc0,0x75,0x3f, // 0x3d XOR R8D,R8D; CALL [create]; JNZ fail
        0x48,0x8b,0x3d,0,0,0,0, 0x48,0x8d,0x35,0,0,0,0,        // 0x4a MOV RDI,[handleA]; LEA RSI,[valueA]
        0xff,0x15,0,0,0,0, 0x85,0xc0,0x75,0x27,                // 0x58 CALL [join]; JNZ fail
        0x48,0x8b,0x3d,0,0,0,0, 0x48,0x8d,0x35,0,0,0,0,        // 0x62 MOV RDI,[handleB]; LEA RSI,[valueB]
        0xff,0x15,0,0,0,0, 0x85,0xc0,0x75,0x0f,                // 0x70 CALL [join]; JNZ fail
        0x8b,0x3d,0,0,0,0, 0x03,0x3d,0,0,0,0,                  // 0x7a EDI=[valueA]+[valueB]
        0xff,0xd3, 0xf4,                                       // 0x86 CALL RBX; HLT
        0x89,0xc7, 0xff,0xd3, 0xf4,                            // 0x89 fail: EDI=EAX; CALL RBX; HLT
        // A (0x8e)
        0x8b,0x1f, 0xc7,0x05,0,0,0,0,1,0,0,0,                  // +00 EBX=[arg]; [flagA]=1
        0x83,0x3d,0,0,0,0,0, 0x74,0xf7,                        // +0c spin while [flagB]==0
        0xff,0x15,0,0,0,0, 0x48,0x83,0xf8,0x01, 0x74,0x0f,     // +15 CALL [self]; CMP RAX,1; JE bad
        0x48,0x89,0xe1, 0x83,0xe1,0x0f, 0x83,0xf9,0x08, 0x75,0x04, // +21 RSP%16==8 or bad
        0x8d,0x43,0x02, 0xc3,                                  // +2c EAX=EBX+2; RET
        0xb8,7,0,0,0, 0xc3,                                    // +30 bad: EAX=7; RET
        // B (0xc4)
        0x83,0x3d,0,0,0,0,0, 0x74,0xf7,                        // +00 spin while [flagA]==0
        0xc7,0x05,0,0,0,0,1,0,0,0,                             // +09 [flagB]=1
        0x8d,0x7f,0x35, 0xff,0x15,0,0,0,0,                     // +13 EDI=arg+53; CALL [exit]
        0xb8,9,0,0,0, 0xc3};                                   // +1c never reached: EAX=9; RET
    static_assert(sizeof(code)==B+0x22, "thread fixture layout");
    std::fill(b.begin()+0x200,b.begin()+0x300,0);
    std::copy(std::begin(code),std::end(code),b.begin()+0x200);
    // RIP-relative operand at `at` of an instruction ending at `end`.
    auto rip=[&](size_t at, size_t end, uint64_t target) { put(b,0x200+at,target-(entry+end),4); };
    rip(0x07,0x0b,handleA); rip(0x10,0x14,entry+A); rip(0x17,0x1b,arg); rip(0x20,0x24,create);
    rip(0x2b,0x2f,handleB); rip(0x34,0x38,entry+B); rip(0x42,0x46,create);
    rip(0x4d,0x51,handleA); rip(0x54,0x58,valueA); rip(0x5a,0x5e,join);
    rip(0x65,0x69,handleB); rip(0x6c,0x70,valueB); rip(0x72,0x76,join);
    rip(0x7c,0x80,valueA); rip(0x82,0x86,valueB);
    rip(A+0x04,A+0x0c,flagA); rip(A+0x0e,A+0x13,flagB); rip(A+0x17,A+0x1b,self);
    rip(B+0x02,B+0x07,flagA); rip(B+0x0b,B+0x13,flagB); rip(B+0x18,B+0x1c,quit);
    put(b,0x300+(arg-data),40,4);
    put(b,64+32,sizeof(code)); put(b,64+40,sizeof(code));
    return b;
}
// One child thread that never returns. With `fault` it loads from address 0
// while the entry waits in scePthreadJoin; otherwise it spins forever and the
// entry exits at once with scePthreadCreate's result (0).
inline std::vector<uint8_t> make_general_stuck_thread_app(bool fault) {
    auto b=make_kernel_app();
    use_libkernel_imports(b,{"6UgtwV+0zb4","onNY9Byn-W8"});
    using orbis_fixture::put;
    constexpr uint64_t create=data, join=data+8, handle=data+0x40;
    constexpr size_t child=0x37;
    const uint8_t code[]={
        0x53, 0x48,0x89,0xf3,                                  // 0x00 PUSH RBX; MOV RBX,RSI (exit)
        0x48,0x8d,0x3d,0,0,0,0, 0x31,0xf6,                     // 0x04 LEA RDI,[handle]; XOR ESI,ESI
        0x48,0x8d,0x15,0,0,0,0, 0x31,0xc9, 0x45,0x31,0xc0,     // 0x0d LEA RDX,[child]; ECX=R8D=0
        0xff,0x15,0,0,0,0, 0x85,0xc0,                          // 0x19 CALL [create]; TEST EAX,EAX
        uint8_t(fault ? 0x75 : 0xeb),0x0f,                     // 0x21 JNZ / JMP leave
        0x48,0x8b,0x3d,0,0,0,0, 0x31,0xf6, 0xff,0x15,0,0,0,0,  // 0x23 join(handle, NULL)
        0x89,0xc7, 0xff,0xd3, 0xf4,                            // 0x32 leave: EDI=EAX; CALL RBX; HLT
        // child (0x37)
        uint8_t(fault ? 0x8b : 0xeb), uint8_t(fault ? 0x04 : 0xfe), 0x25,0,0,0,0, 0xc3}; // MOV EAX,[0] / JMP $
    static_assert(sizeof(code)==child+8, "stuck thread fixture layout");
    std::fill(b.begin()+0x200,b.begin()+0x300,0);
    std::copy(std::begin(code),std::end(code),b.begin()+0x200);
    auto rip=[&](size_t at, size_t end, uint64_t target) { put(b,0x200+at,target-(entry+end),4); };
    rip(0x07,0x0b,handle); rip(0x10,0x14,entry+child); rip(0x1b,0x1f,create);
    rip(0x26,0x2a,handle); rip(0x2e,0x32,join);
    put(b,64+32,sizeof(code)); put(b,64+40,sizeof(code));
    return b;
}
// Producer/consumer on statically initialized objects (slots holding 0). The
// entry creates a worker, locks the mutex and waits on the condition while
// the flag is 0; the worker locks, stores 77 and the flag, signals and unlocks.
// The entry then unlocks, joins and exits with the stored value (or the
// failing call's error).
constexpr uint64_t cond_mutex=data+0x48, cond_var=data+0x50;
inline std::vector<uint8_t> make_general_cond_app() {
    auto b=make_kernel_app();
    use_libkernel_imports(b,{"6UgtwV+0zb4","onNY9Byn-W8","9UK1vLZQft4","tn3VlD0hG60","WKAXJ4XBPQ4","kDh-NfxgMtE"});
    using orbis_fixture::put;
    constexpr uint64_t create=data, join=data+8, lock=data+0x10, unlock=data+0x18, wait=data+0x20, signal=data+0x28;
    constexpr uint64_t handle=data+0x40, m=cond_mutex, cv=cond_var, flag=data+0x58, value=data+0x5c;
    constexpr size_t W=0x85;
    const uint8_t code[]={
        0x53, 0x48,0x89,0xf3,                                  // 0x00 PUSH RBX; MOV RBX,RSI (exit)
        0x48,0x8d,0x3d,0,0,0,0, 0x31,0xf6,                     // 0x04 LEA RDI,[handle]; XOR ESI,ESI
        0x48,0x8d,0x15,0,0,0,0, 0x31,0xc9, 0x45,0x31,0xc0,     // 0x0d LEA RDX,[worker]; ECX=R8D=0
        0xff,0x15,0,0,0,0, 0x85,0xc0,0x75,0x5d,                // 0x19 CALL [create]; JNZ fail
        0x48,0x8d,0x3d,0,0,0,0, 0xff,0x15,0,0,0,0,             // 0x23 lock(m)
        0x85,0xc0,0x75,0x4c,                                   // 0x30 JNZ fail
        0x83,0x3d,0,0,0,0,0, 0x75,0x1a,                        // 0x34 loop: [flag]!=0 -> done
        0x48,0x8d,0x3d,0,0,0,0, 0x48,0x8d,0x35,0,0,0,0,        // 0x3d LEA RDI,[cv]; LEA RSI,[m]
        0xff,0x15,0,0,0,0, 0x85,0xc0,0x75,0x2b, 0xeb,0xdd,     // 0x4b wait; JNZ fail; JMP loop
        0x48,0x8d,0x3d,0,0,0,0, 0xff,0x15,0,0,0,0,             // 0x57 done: unlock(m)
        0x48,0x8b,0x3d,0,0,0,0, 0x31,0xf6,                     // 0x64 MOV RDI,[handle]; XOR ESI,ESI
        0xff,0x15,0,0,0,0, 0x85,0xc0,0x75,0x09,                // 0x6d join; JNZ fail
        0x8b,0x3d,0,0,0,0, 0xff,0xd3, 0xf4,                    // 0x77 exit([value]); HLT
        0x89,0xc7, 0xff,0xd3, 0xf4,                            // 0x80 fail: exit(EAX); HLT
        // worker (0x85)
        0x48,0x8d,0x3d,0,0,0,0, 0xff,0x15,0,0,0,0,             // +00 lock(m)
        0xc7,0x05,0,0,0,0,77,0,0,0,                            // +0d [value]=77
        0xc7,0x05,0,0,0,0,1,0,0,0,                             // +17 [flag]=1
        0x48,0x8d,0x3d,0,0,0,0, 0xff,0x15,0,0,0,0,             // +21 signal(cv)
        0x48,0x8d,0x3d,0,0,0,0, 0xff,0x15,0,0,0,0,             // +2e unlock(m)
        0xc3};                                                 // +3b RET
    static_assert(sizeof(code)==W+0x3c, "condition fixture layout");
    std::fill(b.begin()+0x200,b.begin()+0x300,0);
    std::copy(std::begin(code),std::end(code),b.begin()+0x200);
    auto rip=[&](size_t at, size_t end, uint64_t target) { put(b,0x200+at,target-(entry+end),4); };
    rip(0x07,0x0b,handle); rip(0x10,0x14,entry+W); rip(0x1b,0x1f,create);
    rip(0x26,0x2a,m); rip(0x2c,0x30,lock); rip(0x36,0x3b,flag);
    rip(0x40,0x44,cv); rip(0x47,0x4b,m); rip(0x4d,0x51,wait);
    rip(0x5a,0x5e,m); rip(0x60,0x64,unlock); rip(0x67,0x6b,handle); rip(0x6f,0x73,join); rip(0x79,0x7d,value);
    rip(W+0x03,W+0x07,m); rip(W+0x09,W+0x0d,lock); rip(W+0x0f,W+0x17,value); rip(W+0x19,W+0x21,flag);
    rip(W+0x24,W+0x28,cv); rip(W+0x2a,W+0x2e,signal); rip(W+0x31,W+0x35,m); rip(W+0x37,W+0x3b,unlock);
    for (uint64_t slot : {handle, m, cv, flag}) put(b,0x300+(slot-data),0,8);
    put(b,64+32,sizeof(code)); put(b,64+40,sizeof(code));
    return b;
}
// One thread: lock a statically initialized mutex, then
// scePthreadCondTimedwait(cv, m, 1000 us) with nobody to signal: exits with
// its result, ETIMEDOUT.
inline std::vector<uint8_t> make_general_timedwait_app() {
    auto b=make_kernel_app();
    use_libkernel_imports(b,{"9UK1vLZQft4","BmMjYxmew1w"});
    using orbis_fixture::put;
    constexpr uint64_t lock=data, timedwait=data+8, m=cond_mutex, cv=cond_var;
    const uint8_t code[]={
        0x53, 0x48,0x89,0xf3,                                  // 0x00 PUSH RBX; MOV RBX,RSI (exit)
        0x48,0x8d,0x3d,0,0,0,0, 0xff,0x15,0,0,0,0,             // 0x04 lock(m)
        0x48,0x8d,0x3d,0,0,0,0, 0x48,0x8d,0x35,0,0,0,0,        // 0x11 LEA RDI,[cv]; LEA RSI,[m]
        0xba,0xe8,0x03,0,0, 0xff,0x15,0,0,0,0,                 // 0x1f timedwait(cv, m, 1000)
        0x89,0xc7, 0xff,0xd3, 0xf4};                           // 0x2a exit(EAX); HLT
    static_assert(sizeof(code)==0x2f, "timed wait fixture layout");
    std::fill(b.begin()+0x200,b.begin()+0x300,0);
    std::copy(std::begin(code),std::end(code),b.begin()+0x200);
    auto rip=[&](size_t at, size_t end, uint64_t target) { put(b,0x200+at,target-(entry+end),4); };
    rip(0x07,0x0b,m); rip(0x0d,0x11,lock); rip(0x14,0x18,cv); rip(0x1b,0x1f,m); rip(0x26,0x2a,timedwait);
    for (uint64_t slot : {m, cv}) put(b,0x300+(slot-data),0,8);
    put(b,64+32,sizeof(code)); put(b,64+40,sizeof(code));
    return b;
}
// The entry keeps its exit function, creates a worker, then either sets a flag
// and calls scePthreadExit(0) (`main_spins` false) or spins forever. The worker
// waits for the flag (preset when the entry spins), counts down 2^20, then
// calls the entry's exit function with 77: either way the run exits with 77.
inline std::vector<uint8_t> make_general_handoff_app(bool main_spins) {
    auto b=make_kernel_app();
    use_libkernel_imports(b,{"6UgtwV+0zb4","3kg7rT0NQIs"});
    using orbis_fixture::put;
    constexpr uint64_t create=data, quit=data+8, exitfn=data+0x40, handle=data+0x48, flag=data+0x50;
    constexpr size_t W=0x43;
    const uint8_t code[]={
        0x53, 0x48,0x89,0x35,0,0,0,0,                          // 0x00 PUSH RBX; MOV [exitfn],RSI
        0x48,0x8d,0x3d,0,0,0,0, 0x31,0xf6,                     // 0x08 LEA RDI,[handle]; XOR ESI,ESI
        0x48,0x8d,0x15,0,0,0,0, 0x31,0xc9, 0x45,0x31,0xc0,     // 0x11 LEA RDX,[worker]; ECX=R8D=0
        0xff,0x15,0,0,0,0, 0x85,0xc0, 0x75,0x13,               // 0x1d CALL [create]; JNZ fail
        0xc7,0x05,0,0,0,0,1,0,0,0,                             // 0x27 [flag]=1 (or JMP $)
        0x31,0xff, 0xff,0x15,0,0,0,0, 0xf4,                    // 0x31 scePthreadExit(0); HLT
        0x89,0xc7, 0xff,0x15,0,0,0,0, 0xf4,                    // 0x3a fail: exit(EAX); HLT
        // worker (0x43)
        0x83,0x3d,0,0,0,0,0, 0x74,0xf7,                        // +00 spin while [flag]==0
        0xb9,0,0,0x10,0, 0xff,0xc9, 0x75,0xfc,                 // +09 ECX=2^20; DEC ECX; JNZ
        0xbf,77,0,0,0, 0xff,0x15,0,0,0,0, 0xf4};               // +12 exit(77); HLT
    static_assert(sizeof(code)==W+0x1e, "handoff fixture layout");
    std::fill(b.begin()+0x200,b.begin()+0x300,0);
    std::copy(std::begin(code),std::end(code),b.begin()+0x200);
    auto rip=[&](size_t at, size_t end, uint64_t target) { put(b,0x200+at,target-(entry+end),4); };
    rip(0x04,0x08,exitfn); rip(0x0b,0x0f,handle); rip(0x14,0x18,entry+W); rip(0x1f,0x23,create);
    rip(0x3e,0x42,exitfn); rip(W+0x02,W+0x07,flag); rip(W+0x19,W+0x1d,exitfn);
    if (main_spins) {
        b[0x200+0x27]=0xeb; b[0x200+0x28]=0xfe;
        std::fill(b.begin()+0x200+0x29,b.begin()+0x200+0x3a,0x90);
        put(b,0x300+(flag-data),1,4);
    } else {
        rip(0x29,0x31,flag); rip(0x35,0x39,quit);
    }
    put(b,64+32,sizeof(code)); put(b,64+40,sizeof(code));
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
