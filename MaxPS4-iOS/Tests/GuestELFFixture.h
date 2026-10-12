#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace guest_test {
inline void put(std::vector<uint8_t>& file, uint64_t value, size_t offset, size_t width) {
    for (size_t i = 0; i < width; ++i) file[offset + i] = value >> (8 * i);
}
inline std::vector<uint8_t> elf(uint32_t initial = 40, uint32_t add = 2, bool data = false) {
    std::vector<uint8_t> file(data ? 205 : 131, 0);
    const uint8_t ident[] = {0x7F, 'E', 'L', 'F', 2, 1, 1};
    for (size_t i = 0; i < sizeof(ident); ++i) file[i] = ident[i];
    put(file,2,16,2); put(file,62,18,2); put(file,1,20,4);
    put(file,0x1000,24,8); put(file,64,32,8); put(file,64,52,2);
    put(file,56,54,2); put(file,data ? 2 : 1,56,2);
    const size_t offset = data ? 184 : 120;
    put(file,1,64,4); put(file,5,68,4); put(file,offset,72,8);
    put(file,0x1000,80,8); put(file,11,96,8); put(file,32,104,8); put(file,1,112,8);
    file[offset] = 0xB8; put(file,initial,offset+1,4);
    file[offset+5] = 0x05; put(file,add,offset+6,4); file[offset+10] = 0xF4;
    if (data) {
        put(file,1,120,4); put(file,6,124,4); put(file,195,128,8);
        put(file,0x10000,136,8); put(file,10,152,8); put(file,64,160,8); put(file,1,168,8);
        file[195] = 0xAB;
    }
    return file;
}
// Actual x86 stores generate a DRAW_INDEX_AUTO packet in a zero-filled RW
// segment. The command buffer does not exist in the ELF file's data payload.
inline std::vector<uint8_t> generated_pm4(uint32_t vertexCount = 3) {
    auto file = elf(40, 2, true);
    file.resize(225, 0);
    put(file, 41, 96, 8); put(file, 64, 104, 8);
    put(file, 225, 128, 8); put(file, 0, 152, 8);
    const uint32_t words[] = {0xc0012d00, vertexCount, 2};
    for (size_t i = 0; i < 3; ++i) {
        const size_t offset = 10 + i * 10;
        file[184 + offset] = 0xC7; file[185 + offset] = 0x05;
        const uint64_t target = 0x10000 + i * 4;
        const uint64_t next = 0x1000 + offset + 10;
        put(file, target - next, 186 + offset, 4);
        put(file, words[i], 190 + offset, 4);
    }
    file[224] = 0xF4;
    return file;
}
}

