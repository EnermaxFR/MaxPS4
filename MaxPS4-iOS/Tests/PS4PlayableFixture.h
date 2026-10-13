#pragma once
#include "PS4OrbisExecutionFixture.h"
namespace playable_fixture {
constexpr uint64_t data = orbis_execution::app_bias + 0x10000;
constexpr uint64_t input = data + 0x10, x = data + 0x14, y = data + 0x18;
constexpr uint64_t counter = data + 0x1c, pixels = data + 0x100;
constexpr uint32_t width = 32, height = 24;
// Original, redistributable scene. All movement, color selection and pixel stores
// are x86 instructions in the SCE ELF; provider.prx is resolved by NID/RELA.
inline std::vector<uint8_t> app(bool animated = false) {
    auto file = orbis_execution::make(false);
    file.resize(0xb000);
    std::vector<uint8_t> code;
    auto emit = [&](std::initializer_list<uint8_t> bytes) { code.insert(code.end(),bytes); };
    auto imm = [&](uint32_t value) { for (int i=0;i<4;++i) code.push_back(uint8_t(value>>(8*i))); };
    auto rip = [&](uint8_t op,uint8_t mod,uint64_t target) {
        emit({op,mod}); imm(uint32_t(target-(orbis_execution::entry+code.size()+4)));
    };
    auto immediate = [&](uint8_t op,uint32_t value) { emit({op}); imm(value); };
    rip(0xff,0x15,data); // Guest CALL through relocated NID slot.
    auto move = [&](uint64_t state,uint32_t negative,uint32_t positive,uint8_t shift,uint32_t mask) {
        rip(0x8b,0x15,state);
        rip(0x8b,0x05,input); immediate(0x25,negative); emit({0xc1,0xe8,shift}); emit({0x29,0xc2});
        rip(0x8b,0x05,input); immediate(0x25,positive); emit({0xc1,0xe8,uint8_t(shift+1)}); emit({0x01,0xc2});
        emit({0x81,0xe2}); imm(mask); rip(0x89,0x15,state);
    };
    move(x,1,2,0,15); move(y,4,8,2,7);
    // EDX becomes y*16+x. The sprite covers one 2x2 cell.
    emit({0xc1,0xe2,4}); rip(0x8b,0x05,x); emit({0x01,0xc2});
    // Action selects a different color in guest code.
    rip(0x8b,0x05,input); immediate(0x25,16); emit({0xc1,0xe0,16});
    immediate(0x0d,0xff2080e0); emit({0x89,0xc3});
    // Optional second sprite: its x position is derived entirely from the
    // guest frame counter. On collision the guest changes its color.
    // The original app() remains byte-identical to the shipped demo ELF.
    if (animated) {
        rip(0x8b,0x05,counter); immediate(0x25,15); immediate(0x05,80);
        emit({0x89,0xc6}); // ESI = cell 80 + (frame & 15), row 5.
        immediate(0xb9,0xff40d030); // ECX = second sprite RGBA.
        emit({0x39,0xd6,0x0f,0x44,0xcb}); // Collision: if ESI == EDX, ECX = player color.
    }
    for (uint32_t py=0;py<height;++py) for (uint32_t px=0;px<width;++px) {
        immediate(0xb8,0xff302018); // RGBA little endian: dark background.
        emit({0x81,0xfa}); imm(py < 16 ? (py/2)*16+px/2 : UINT32_MAX);
        if (animated) {
            emit({0x81,0xfe}); imm(py < 16 ? (py/2)*16+px/2 : UINT32_MAX);
            emit({0x0f,0x44,0xc1}); // CMOVE EAX, ECX: autonomous sprite.
            emit({0x81,0xfa}); imm(py < 16 ? (py/2)*16+px/2 : UINT32_MAX);
        }
        emit({0x0f,0x44,0xc3}); rip(0x89,0x05,pixels+(py*width+px)*4);
    }
    rip(0x8b,0x05,counter); immediate(0x05,1); rip(0x89,0x05,counter); emit({0xf4});
    std::copy(code.begin(),code.end(),file.begin()+0x1000);
    using orbis_fixture::put;
    put(file,64+8,0x1000); put(file,64+32,code.size()); put(file,64+40,code.size());
    put(file,64+56+8,0xa000); put(file,64+56+16,0x10000);
    put(file,64+56+32,0x1000); put(file,64+56+40,0x1000);
    put(file,0x900,0x10000); // RELA target updated, provider stays unchanged.
    put(file,0xa014,7,4); put(file,0xa018,3,4);
    return file;
}
}
