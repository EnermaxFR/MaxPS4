#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <stdexcept>
namespace orbis_fixture {
inline void put(std::vector<uint8_t>& bytes, size_t offset, uint64_t v, size_t n = 8) {
    for (size_t i = 0; i < n; ++i) bytes.at(offset + i) = uint8_t(v >> (8 * i));
}
// These are fabricated identities for format testing only, never Sony NIDs.
inline std::vector<uint8_t> make(bool provider) {
    std::vector<uint8_t> b(0xc00, 0);
    const uint8_t magic[] = {0x7f,'E','L','F',2,1,1,9,0};
    for (size_t i = 0; i < sizeof(magic); ++i) b[i] = magic[i];
    put(b,16,provider ? 0xfe18 : 0xfe10,2); put(b,18,62,2); put(b,20,1,4);
    put(b,24,provider ? 0 : 0x1000); put(b,32,64); put(b,52,64,2); put(b,54,56,2); put(b,56,4,2);
    auto ph = [&](size_t i, uint32_t type, uint32_t flags, uint64_t offset, uint64_t addr, uint64_t size) {
        const auto p = 64 + i * 56; put(b,p,type,4); put(b,p+4,flags,4); put(b,p+8,offset);
        put(b,p+16,addr); put(b,p+32,size); put(b,p+40,size); put(b,p+48,1);
    };
    ph(0,1,5,0x200,0x1000,0x100); ph(1,1,6,0x300,0x2000,0x100);
    ph(2,2,4,0x400,0,0x200); ph(3,0x61000000,4,0x600,0,0x600);
    b[0x200] = 0xc3; // Data only; never dispatched by these tests.
    size_t cursor = 0x601;
    auto text = [&](const std::string& s) { const auto off = cursor - 0x600; for (char c : s) b[cursor++] = uint8_t(c); b[cursor++] = 0; return off; };
    const auto library = text("FixtureLibrary"), module = text("FixtureModule"), name = text("AAAAAAAAAAA#B#B");
    size_t d = 0x400;
    auto tag = [&](uint64_t t, uint64_t v) { put(b,d,t); put(b,d+8,v); d += 16; };
    tag(0x61000035,0); tag(0x61000037,cursor-0x600); tag(0x61000039,0x200);
    tag(0x6100003b,24); tag(0x6100003f,48);
    tag(provider ? 0x61000013 : 0x61000015, library | (uint64_t{1}<<32) | (uint64_t{1}<<48));
    tag(provider ? 0x6100000d : 0x6100000f, module | (uint64_t{0x100}<<32) | (uint64_t{1}<<48));
    if (!provider) { tag(0x6100002f,0x300); tag(0x61000031,24); tag(0x61000033,24); }
    tag(0,0);
    put(b,0x818,name,4); b[0x81c] = 0x12; put(b,0x81e,provider ? 1 : 0,2);
    put(b,0x820,provider ? 0x1000 : 0); put(b,0x828,provider ? 1 : 0);
    if (!provider) { put(b,0x900,0x2000); put(b,0x908,(uint64_t{1}<<32)|7); }
    return b;
}
inline void setTag(std::vector<uint8_t>& b, uint64_t tag, uint64_t value) {
    for (size_t p = 0x400; p < 0x600; p += 16) {
        uint64_t t = 0; for (size_t i=0;i<8;++i) t |= uint64_t(b[p+i]) << (i*8);
        if (t == tag) { put(b,p+8,value); return; }
    }
    throw std::runtime_error("fixture tag missing");
}
}
