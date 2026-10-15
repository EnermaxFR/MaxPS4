#pragma once
#include "../Native/maxps4_ps4_synthetic_elf.h"
#include "../Native/maxps4_orbis_m1.h"

namespace orbis_m1_fixture {
// Finite direct fixture at 8 GiB, below the macOS GPU carve-out and 48 TiB.
// Device VM/JIT admission is still required; this is not general VA translation.
// The direct backend must reject a collision, never move an ELF behind its pointers.
constexpr uint64_t bias = maxps4::OrbisM1::heap_base-0x800000;
constexpr uint64_t entry = bias+maxps4_ps4_synthetic::entry;
constexpr uint64_t data = bias+maxps4_ps4_synthetic::data;
constexpr uint64_t snapshot = data+0x100;
inline uint64_t number(const std::vector<uint8_t>& b,size_t at,size_t n = 8) {
    uint64_t v = 0; for (size_t i=0;i<n;++i) v |= uint64_t(b.at(at+i))<<(8*i); return v;
}
inline std::vector<uint8_t> make(uint32_t initial = 17, bool calls = true, bool dynamic = false) {
    using namespace maxps4_ps4_synthetic;
    Program p;
    // Local-exec static TLS at FS-16, initialized 8 bytes plus 8 zero bytes.
    p.bytes({0x64,0x48,0x8b,0x04,0x25}); p.immediate(0xfffffff0,4);
    p.bytes({0x05,5,0,0,0});
    p.bytes({0x64,0x48,0x89,0x04,0x25}); p.immediate(0xfffffff0,4);
    p.bytes({0x50,0x58});
    p.bytes({0x48,0xa3}); p.immediate(snapshot,8);
    p.reg(3,maxps4::OrbisM1::heap_base);
    p.bytes({0x48,0x89,0x03,0x48,0x8b,0x03,0x05,7,0,0,0,0x48,0xa3}); p.immediate(snapshot,8);
    if (calls) {
        p.call("aI+OeCz8xrQ"); // Existing verified scePthreadSelf NID.
        p.bytes({0x48,0xa3}); p.immediate(data+0x108,8);
        p.reg(7,4); p.reg(6,data+0x300); p.call("sceKernelClockGettime");
    }
    p.bytes({0x64,0x48,0x8b,0x04,0x25}); p.immediate(0xfffffff8,4);
    p.bytes({0x48,0xa3}); p.immediate(data+0x110,8);
    if (dynamic) {
        // Fixture-only module payload convention. These are x86 FS-relative
        // accesses, not an implementation of SCE DTV/__tls_get_addr.
        for (size_t module=0;module<2;++module) {
            const uint32_t offset = 0x1000000+module*0x200000-0x20010;
            p.bytes({0x64,0x48,0x8b,0x04,0x25}); p.immediate(offset,4);
            p.bytes({0x05}); p.immediate(module+1,4);
            p.bytes({0x64,0x48,0x89,0x04,0x25}); p.immediate(offset,4);
        }
        p.bytes({0x48,0xa3}); p.immediate(snapshot,8);
        p.bytes({0x64,0x48,0x8b,0x04,0x25}); p.immediate(0x1000000-0x20010+8,4);
        p.bytes({0x48,0xa3}); p.immediate(snapshot+16,8);
        p.reg(7,snapshot+0x100); p.call("upoVrzMHFeE");
        p.bytes({0x48,0xa3}); p.immediate(snapshot+0x80,8);
        p.reg(7,snapshot+0x100); p.call("tn3VlD0hG60");
        p.bytes({0x48,0xa3}); p.immediate(snapshot+0x88,8);
    }
    p.bytes({0x48,0xa1}); p.immediate(snapshot,8); p.bytes({0xf4});
    auto b = p.elf();
    put(b,entry,24);
    // Rebase metadata only. x86 absolute operands already contain guest VAs;
    // relative GOT displacements retain their original layout.
    for (size_t at : {64U,120U,176U}) put(b,number(b,at+16)+bias,at+16);
    for (size_t at=dynamic_file;at<dynamic_file+160;at+=16) {
        const uint64_t tag = number(b,at);
        if (tag == 4 || tag == 5 || tag == 6 || tag == 23) put(b,number(b,at+8)+bias,at+8);
    }
    for (size_t i=0;i<p.symbols.size();++i) put(b,number(b,data_file+0xc00+i*24)+bias,data_file+0xc00+i*24);
    put(b,calls ? 4 : 3,56,2);
    if (!calls) put(b,0xfe00,16,2);
    const size_t at = calls ? 232 : 176;
    put(b,7,at,4); put(b,4,at+4,4); put(b,data_file+0x2000,at+8);
    put(b,data+0x2000,at+16); put(b,8,at+32); put(b,16,at+40); put(b,16,at+48);
    put(b,initial,data_file+0x2000);
    put(b,0xdeadbeef,data_file+0x110); // CPU must overwrite this using the zero TLS tail.
    return b;
}
}

