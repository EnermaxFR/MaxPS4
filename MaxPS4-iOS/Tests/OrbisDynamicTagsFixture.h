#pragma once
#include "OrbisLinkerFixture.h"
#include <string>
// Mission 48: the dynamic tags observed in CUSA06015 (hash, fingerprint,
// SONAME, original filename, init/fini, arrays, PLTGOT, library attributes,
// DEBUG/FLAGS/TEXTREL), applied to the synthetic linker fixtures. All bytes
// are fabricated. DYNLIBDATA is file 0x600..0xc00 in these fixtures.
namespace dyn_fixture {
using orbis_fixture::put;
// Fingerprint first, then the hash table: room for up to 29 symbols before 0xc00.
constexpr size_t fingerprintFile = 0xb60, hashFile = 0xb80; // DYNLIBDATA offsets 0x560 / 0x580.
inline uint64_t getTag(const std::vector<uint8_t>& b, uint64_t tag) {
    for (size_t p = 0x400; p + 16 <= 0x600; p += 16) {
        uint64_t t = 0, v = 0;
        for (size_t i = 0; i < 8; ++i) { t |= uint64_t(b[p + i]) << (8 * i); v |= uint64_t(b[p + 8 + i]) << (8 * i); }
        if (t == tag) return v;
        if (!t) break;
    }
    throw std::runtime_error("fixture tag missing");
}
inline uint64_t appendString(std::vector<uint8_t>& b, const std::string& text) {
    const auto offset = getTag(b, 0x61000037);
    if (0x600 + offset + text.size() + 1 > 0x800) throw std::runtime_error("fixture string table full");
    for (size_t i = 0; i < text.size(); ++i) b[0x600 + offset + i] = uint8_t(text[i]);
    b[0x600 + offset + text.size()] = 0;
    orbis_fixture::setTag(b, 0x61000037, offset + text.size() + 1);
    return offset;
}
inline uint64_t hashSize(uint64_t symbols) { return 4 * (3 + symbols); }
// One bucket; chain 1 -> 2 -> ... -> n-1 -> 0 covers every symbol exactly once.
inline void writeHash(std::vector<uint8_t>& b, uint64_t symbols) {
    if (hashFile + hashSize(symbols) > 0xc00) throw std::runtime_error("fixture hash table too large");
    put(b, hashFile, 1, 4); put(b, hashFile + 4, symbols, 4); put(b, hashFile + 8, symbols > 1 ? 1 : 0, 4);
    for (uint64_t i = 0; i < symbols; ++i) put(b, hashFile + 12 + i * 4, i && i + 1 < symbols ? i + 1 : 0, 4);
}
struct Options {
    std::string soname; // Empty: no DT_SONAME (executables).
    uint64_t code = 0x1000, arrays = 0x2100, pltgot = 0;
    uint16_t exportId = 0, importId = 0; // 0: no attribute tag.
    bool textrel = false;
};
inline void add(std::vector<uint8_t>& b, const Options& o) {
    using orbis_fixture::appendTag;
    const auto symbols = getTag(b, 0x6100003f) / 24;
    writeHash(b, symbols);
    for (size_t i = 0; i < 20; ++i) b[fingerprintFile + i] = uint8_t(0xa0 + i);
    appendTag(b, 0x61000025, hashFile - 0x600); appendTag(b, 0x6100003d, hashSize(symbols));
    appendTag(b, 0x61000007, fingerprintFile - 0x600);
    appendTag(b, 0x61000009, appendString(b, "W:/Build/fixture path/" + (o.soname.empty() ? std::string("app.elf") : o.soname)));
    if (!o.soname.empty()) appendTag(b, 14, appendString(b, o.soname));
    appendTag(b, 0x61000011, 0); appendTag(b, 21, 0); appendTag(b, 30, o.textrel ? 4 : 0);
    if (o.textrel) appendTag(b, 22, 0);
    appendTag(b, 12, o.code); appendTag(b, 13, o.code);
    for (uint64_t tag : {25, 27, 26, 28, 32, 33}) appendTag(b, tag, (tag == 27 || tag == 28 || tag == 33) ? 0 : o.arrays);
    if (o.pltgot) appendTag(b, 0x61000027, o.pltgot);
    if (o.exportId) appendTag(b, 0x61000017, (uint64_t(o.exportId) << 48) | 1);
    if (o.importId) appendTag(b, 0x61000019, (uint64_t(o.importId) << 48) | 9);
}
}
