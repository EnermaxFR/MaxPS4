#include "../Native/maxps4_ps4_gnm.h"
#include "../Native/PM4MetalDrawPlan.hpp"
#include "GuestELFFixture.h"
#include <cassert>
#include <cstdio>
using namespace maxps4;
int main() {
    GuestELF image;
    auto file=guest_test::elf(40,2,true); guest_test::put(file,4096,160,8);
    assert(image.load(file.data(),file.size(),6)==0);
    PS4GuestMemory memory(image); PS4Gnm gnm(memory);
    constexpr uint64_t out=0x10000, regs=out+0x200;
    auto load = [&](size_t count) {
        std::vector<uint32_t> words(count); std::vector<uint8_t> bytes(count*4);
        assert(memory.read(out,bytes.data(),bytes.size()));
        for(size_t i=0;i<count;++i) words[i]=uint32_t(ps4::get_le(bytes.data()+4*i,4));
        return words;
    };
    uint8_t registers[48]{};
    for(size_t i=0;i<12;++i) ps4::put_le(registers,i*4,i==1 ? 0 : 0x120+i,4);
    assert(memory.write(regs,registers,sizeof(registers)));
    assert(gnm.draw_auto(out,7,3,0)==0);
    auto words=load(8);
    const uint32_t autoExpected[]={0xc0012d00,3,2,0xc0021000,0,0,0};
    for(size_t i=0;i<7;++i) assert(words[i]==autoExpected[i]);
    assert(words[7]==0 && gpu::planPM4Triangles(words.data(),7).drawCount==1);
    for(auto flags:{2U,0x10000000U}) assert(gnm.draw_auto(out,7,3,flags)==-1);
    assert(gnm.draw_auto(out,6,3,0)==-1 && gnm.draw_auto(out+1,7,3,0)==-1);
    assert(gnm.draw_auto(image.entry(),7,3,0)==-1 && gnm.draw_auto(UINT64_MAX-3,7,3,0)==-1);
    assert(gnm.draw_index(out,10,6,0x123456780ULL,0)==0);
    words=load(10); const uint32_t indexExpected[]={0xc0042700,6,0x23456780,1,6,0,0xc0021000,0,0,0};
    for(size_t i=0;i<10;++i) assert(words[i]==indexExpected[i]);
    auto commands=gpu::interpretPM4(words.data(),words.size());
    assert(commands.valid && commands.draws.size()==1 && commands.draws[0].indexed &&
        commands.draws[0].indexAddress==0x123456780ULL);
    assert(gnm.draw_index(out,10,6,0,0)==-1 && gnm.draw_index(out,10,6,3,0)==-1);
    assert(gnm.draw_index(out,9,6,2,0)==-1 && gnm.draw_index(out,10,6,2,2)==-1);
    assert(gnm.vgt(out,3,255,1,0)==0); words=load(3);
    assert(words[0]==0xc0016900 && words[1]==0x2aa && words[2]==0x100ff);
    assert(gnm.vgt(out,3,256,1,0)==-1 && gnm.vgt(out,3,1,2,0)==-1 && gnm.vgt(out,3,1,0,2)==-1);
    assert(gnm.vs(out,29,regs,0)==0); words=load(29);
    commands=gpu::interpretPM4(words.data(),words.size());
    assert(commands.valid && commands.writes.size()==7 && commands.draws.empty());
    assert(commands.writes[0].offset==0x48 && commands.writes[0].value==0x120);
    assert(commands.writes[2].offset==0x4a && commands.writes[2].value==0x122);
    assert(commands.writes[4].offset==0x207 && commands.writes[4].value==0x126);
    assert(words[17]==0xc00a1000 && words[28]==0);
    assert(gnm.vs(out,UINT32_MAX,regs,0x40)==0); words=load(29);
    assert(words[6]==((0x122 & 0xfcfffc3fU)|0x40));
    assert(gnm.vs(out,28,regs,0)==-1 && gnm.vs(out,29,0,0)==-1);
    assert(gnm.vs(out,29,regs,1)==-1 && gnm.vs(out,29,UINT64_MAX,0)==-1);
    assert(gnm.ps350(out,40,regs)==0); words=load(40);
    commands=gpu::interpretPM4(words.data(),words.size());
    assert(commands.valid && commands.writes.size()==12 && words[28]==0xc00a1000);
    assert(commands.writes[4].offset==0x1c4 && commands.writes[4].value==0x124);
    assert(commands.writes.back().offset==0x8f && commands.writes.back().value==0x12b);
    assert(gnm.ps350(out,40,0)==0); words=load(40);
    commands=gpu::interpretPM4(words.data(),words.size());
    assert(commands.valid && commands.writes.size()==4 && commands.writes.back().value==0xf);
    assert(words[10]==0xc01c1000 && gnm.ps350(out,39,regs)==-1);
    // A rejected input must preserve the complete existing command sequence.
    const auto before=load(40); ps4::put_le(registers,4,1,4);
    assert(memory.write(regs,registers,sizeof(registers)));
    assert(gnm.ps350(out,40,regs)==-1 && load(40)==before);
    assert(gnm.vs(out,29,regs,0)==-1 && load(40)==before);
    // Registers can alias output: read all of them before writing any packet.
    ps4::put_le(registers,4,0,4); assert(memory.write(out,registers,sizeof(registers)));
    assert(gnm.ps350(out,40,out)==0 && load(40)[2]==0x120);
    std::puts("PASS GNM guest command builders: indexed/auto draws, VS/PS350/VGT, exact PM4 and checked atomic copies");
}
