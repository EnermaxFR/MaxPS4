#pragma once
#include "OrbisLinkerFixture.h"
// Mission 47: synthetic .eh_frame + .eh_frame_hdr placed in a file-backed text
// PT_LOAD, with the encodings observed on CUSA06015 (0x1b/0x03/0x3b). Bytes are
// fabricated; CIE bodies are opaque filler, never interpreted.
namespace eh_fixture {
using orbis_fixture::put;
// Text-relative layout: code +0x00/+0x10, CIE +0xa0, FDEs +0xb0/+0xc0,
// header +0xe0 (28 bytes: 12-byte prologue + two table entries), ending at
// +0xfc so a program table of up to 10 entries never overlaps it.
constexpr uint64_t cie = 0xa0, fde0 = 0xb0, fde1 = 0xc0, header = 0xe0, headerSize = 28;
inline void setPhnum(std::vector<uint8_t>& b, size_t count) {
    const size_t current = size_t(b[56]) | size_t(b[57]) << 8;
    if (count > current) put(b, 56, count, 2);
}
inline void program(std::vector<uint8_t>& b, size_t index, uint32_t type, uint32_t flags, uint64_t offset,
                    uint64_t address, uint64_t fileSize, uint64_t memorySize, uint64_t alignment) {
    const size_t p = 64 + index * 56;
    put(b, p, type, 4); put(b, p + 4, flags, 4); put(b, p + 8, offset); put(b, p + 16, address);
    put(b, p + 24, 0); put(b, p + 32, fileSize); put(b, p + 40, memorySize); put(b, p + 48, alignment);
    setPhnum(b, index + 1);
}
inline void addEHFrame(std::vector<uint8_t>& b, size_t index, uint64_t textAddress, size_t textFile) {
    auto at = [&](uint64_t relative) { return textFile + size_t(relative); };
    auto rel = [](uint64_t target, uint64_t origin) { return uint64_t(uint32_t(int32_t(int64_t(target) - int64_t(origin)))); };
    put(b, at(cie), 12, 4); put(b, at(cie) + 4, 0, 4); put(b, at(cie) + 8, 0x01527a01, 4); put(b, at(cie) + 12, 0x0c, 4);
    for (uint64_t f : {fde0, fde1}) {
        put(b, at(f), 12, 4); put(b, at(f) + 4, (f + 4) - cie, 4);
        put(b, at(f) + 8, rel(f == fde0 ? 0 : 0x10, f + 8), 4); put(b, at(f) + 12, 0x10, 4);
    }
    const auto h = at(header);
    b[h] = 1; b[h + 1] = 0x1b; b[h + 2] = 0x03; b[h + 3] = 0x3b;
    put(b, h + 4, rel(cie, header + 4), 4); put(b, h + 8, 2, 4);
    put(b, h + 12, rel(0x00, header), 4); put(b, h + 16, rel(fde0, header), 4);
    put(b, h + 20, rel(0x10, header), 4); put(b, h + 24, rel(fde1, header), 4);
    program(b, index, 0x6474e550, 4, textFile + header, textAddress + header, headerSize, headerSize, 4);
}
inline size_t headerFile(size_t textFile) { return textFile + size_t(header); }
}
