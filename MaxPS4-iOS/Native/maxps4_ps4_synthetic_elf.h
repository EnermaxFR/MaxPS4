#pragma once
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace maxps4_ps4_synthetic {
constexpr uint64_t entry = 0x4000, data = 0x10000;
constexpr size_t data_file = 0x8000, dynamic_file = data_file + 0x1000;
inline void put(std::vector<uint8_t>& file, uint64_t value, size_t offset, size_t width = 8) {
    for (size_t i = 0; i < width; ++i) file.at(offset+i) = static_cast<uint8_t>(value >> (8*i));
}
class Program {
public:
    std::vector<uint8_t> code;
    std::vector<std::string> symbols;
    void bytes(std::initializer_list<uint8_t> values) { code.insert(code.end(),values); }
    void immediate(uint64_t value, size_t width) {
        for (size_t i = 0; i < width; ++i) code.push_back(static_cast<uint8_t>(value >> (8*i)));
    }
    void reg(uint8_t index, uint64_t value) {
        bytes({static_cast<uint8_t>(index >= 8 ? 0x49 : 0x48),static_cast<uint8_t>(0xb8+(index&7))});
        immediate(value,8);
    }
    void call(const char* symbol) {
        size_t slot = 0;
        while (slot < symbols.size() && symbols[slot] != symbol) ++slot;
        if (slot == symbols.size()) symbols.emplace_back(symbol);
        bytes({0xff,0x15});
        immediate(data+slot*8-(entry+code.size()+4),4);
    }
    void store(uint64_t address, uint8_t source = 0) {
        bytes({static_cast<uint8_t>(source >= 8 ? 0x4c : 0x48),0x89,static_cast<uint8_t>(0x05|((source&7)<<3))});
        immediate(address-(entry+code.size()+4),4);
    }
    std::vector<uint8_t> elf() const {
        std::vector<uint8_t> file(0xc000);
        std::memcpy(file.data(),"\177ELF\2\1\1\11",8);
        put(file,0xfe10,16,2); put(file,62,18,2); put(file,1,20,4);
        put(file,entry,24); put(file,64,32); put(file,64,52,2); put(file,56,54,2); put(file,3,56,2);
        auto ph = [&](size_t at,uint32_t type,uint32_t flags,uint64_t offset,uint64_t address,uint64_t files,uint64_t memory,uint64_t align) {
            put(file,type,at,4); put(file,flags,at+4,4); put(file,offset,at+8); put(file,address,at+16);
            put(file,files,at+32); put(file,memory,at+40); put(file,align,at+48);
        };
        ph(64,1,5,entry,entry,code.size(),0x4000,0x4000);
        ph(120,1,6,data_file,data,0x4000,0x8000,0x4000);
        constexpr size_t tag_count = 10;
        ph(176,2,6,dynamic_file,data+0x1000,tag_count*16,tag_count*16,8);
        std::memcpy(file.data()+entry,code.data(),code.size());
        std::string names(1,'\0'); names += "libkernel"; names.push_back('\0');
        for (size_t i = 0; i < symbols.size(); ++i) {
            const size_t sym = data_file+0x400+(i+1)*24;
            put(file,names.size(),sym,4); file.at(sym+4) = 0x12;
            names += symbols[i]; names.push_back('\0');
            const size_t rela = data_file+0xc00+i*24;
            put(file,data+i*8,rela); put(file,((i+1)<<32)|7,rela+8);
        }
        std::memcpy(file.data()+data_file+0x200,names.data(),names.size());
        put(file,1,data_file+0x800,4); put(file,symbols.size()+1,data_file+0x804,4);
        put(file,symbols.empty() ? 0 : 1,data_file+0x808,4);
        const uint64_t tags[tag_count][2] = {{1,1},{4,data+0x800},{5,data+0x200},{10,names.size()},
            {6,data+0x400},{11,24},{23,data+0xc00},{2,symbols.size()*24},{20,7},{0,0}};
        for (size_t i = 0; i < tag_count; ++i) { put(file,tags[i][0],dynamic_file+i*16); put(file,tags[i][1],dynamic_file+i*16+8); }
        return file;
    }
};
inline std::vector<uint8_t> minimal() {
    Program p;
    p.call("aI+OeCz8xrQ"); // scePthreadSelf returns bootstrap guest identity 1.
    p.store(data+0x108); // Capture the actual libkernel return before modifying RAX.
    p.bytes({0x05,1,0,0,0}); // Must resume after the actual kernel return.
    p.store(data+0x100); p.bytes({0xf4});
    return p.elf();
}
inline std::vector<uint8_t> combined() {
    Program p;
    p.reg(7,data+0x180); p.reg(6,0); p.reg(2,0); p.call("sceKernelOpen");
    p.store(data+0x100);
    p.bytes({0x48,0x89,0xc7}); p.reg(6,data+0x2000); p.reg(2,4); p.call("sceKernelRead");
    p.store(data+0x108);
    p.reg(7,3); p.call("sceKernelClose"); p.store(data+0x110);
    p.reg(7,4); p.reg(6,data+0x2100); p.call("sceKernelClockGettime"); p.store(data+0x118);
    p.reg(7,0); p.reg(6,0x4000); p.reg(2,3); p.reg(1,0x1002); p.reg(8,UINT64_MAX); p.reg(9,0);
    // Seventh SysV argument resides at caller RSP, callee RSP+8. Preserve
    // 16-byte call alignment; the synthetic private CPU stack never escapes.
    p.bytes({0x48,0x83,0xec,0x10,0x48,0xc7,0x04,0x24}); p.immediate(data+0x120,4);
    p.call("sceKernelMmap"); p.bytes({0x48,0x83,0xc4,0x10}); p.store(data+0x128);
    p.bytes({0xf4});
    auto file = p.elf();
    std::memcpy(file.data()+data_file+0x180,"/app0/fixture",14);
    return file;
}
}
