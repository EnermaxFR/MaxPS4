#include "../Native/maxps4_ps4_pad.h"
#include "GuestELFFixture.h"
#include <cassert>
#include <cstdio>
#include <thread>
using namespace maxps4;
int main() {
    GuestELF image;
    auto file = guest_test::elf(40, 2, true);
    guest_test::put(file, 4096, 160, 8);
    assert(image.load(file.data(), file.size(), 6) == 0);
    PS4GuestMemory memory(image);
    PS4Pad pad(memory), other(memory);
    constexpr uint64_t output = 0x10000;
    assert(pad.open(1,0,0,0) == PS4Pad::error(5));
    assert(pad.initialize() == 0 && pad.initialize() == 0);
    assert(pad.open(-1,0,0,0) == ps4::signed32(0x809b0001U));
    assert(pad.open(2,0,0,0) == ps4::signed32(0x809b0081U));
    assert(pad.open(1,2,0,0) == PS4Pad::error(2));
    assert(pad.open(1,0,1,0) == PS4Pad::error(2));
    assert(pad.open(1,0,0,UINT64_MAX) == PS4Pad::error(1));
    const int32_t handle = int32_t(pad.open(1,0,0,0)); assert(handle > 0);
    assert(pad.open(1,0,0,0) == PS4Pad::error(4));
    assert(pad.get_handle(1,0,0) == handle && pad.get_handle(2,0,0) == PS4Pad::error(8));
    assert(pad.read(handle,output,1,true) == 0);
    uint8_t bytes[128]{};
    assert(memory.read(output,bytes,120));
    assert(bytes[4] == 128 && bytes[7] == 128 && bytes[76] == 0);
    assert(ps4::get_le(bytes+24,4) == 0x3f800000);
    assert(pad.update(0x4080,{{0,255,127,129}},12,244,true) == 0);
    assert(pad.update(0x4000,{{0,255,127,129}},12,244,true) == 0); // 0x80 tapped before any read.
    assert(pad.update(1,{{128,128,128,128}},0,0,true) == -22);
    assert(pad.read(handle,0,1,true) == PS4Pad::error(1));
    assert(pad.read(handle,output,0,false) == PS4Pad::error(1));
    assert(pad.read(handle,output,65,false) == PS4Pad::error(1));
    assert(pad.read(handle,UINT64_MAX,1,true) == PS4Pad::error(1));
    assert(pad.read(handle,image.entry(),1,true) == PS4Pad::error(1));
    assert(pad.read(handle,output,64,false) == 1);
    assert(memory.read(output,bytes,128));
    assert(ps4::get_le(bytes,4) == 0x4080 && bytes[4] == 0 && bytes[5] == 255);
    assert(bytes[8] == 12 && bytes[9] == 244 && bytes[76] == 1 && bytes[104] == 1);
    assert(ps4::get_le(bytes+80,8) > 0 && ps4::get_le(bytes+120,8) == 0);
    assert(pad.read(handle,output,1,true) == 0 && memory.read(output,bytes,4) && ps4::get_le(bytes,4) == 0x4000);
    assert(other.read(handle,output,1,true) == PS4Pad::error(3));
    assert(pad.information(handle,output) == 0 && memory.read(output,bytes,28));
    assert(ps4::get_le(bytes+4,2) == 1920 && ps4::get_le(bytes+6,2) == 950);
    assert(bytes[11] == 1 && bytes[12] == 1);
    std::thread producer([&] { for (int i=0;i<1000;++i) assert(pad.update(0x4000,{{1,2,3,4}},5,6,true) == 0); });
    for (int i=0;i<1000;++i) assert(pad.read(handle,output,1,true) == 0);
    producer.join();
    assert(pad.update(0xffff,{{1,2,3,4}},255,255,false) == -22);
    assert(pad.update(0,{{1,2,3,4}},255,255,false) == 0);
    assert(pad.read(handle,output,1,true) == 0 && memory.read(output,bytes,120));
    assert(!ps4::get_le(bytes,4) && bytes[4] == 128 && bytes[8] == 0 && bytes[76] == 0);
    assert(pad.close(handle) == 0 && pad.close(handle) == PS4Pad::error(3));
    const auto second = pad.open(1,0,0,0); assert(second > handle);
    pad.reset(); assert(pad.initialize() == 0);
    assert(pad.open(1,0,0,0) > second);
    assert(pad.read(int32_t(second),output,1,true) == PS4Pad::error(3));
    std::puts("PASS PS4 Pad lifecycle, wire layout, host input, disconnect, concurrency and stale handles");
}
