#pragma once
#include "OrbisFixture.h"
#include <algorithm>
#include <iterator>
namespace v4_prx_fixture {
inline std::vector<uint8_t> make(uint32_t value=42) {
    auto bytes=orbis_test::fixture(false);
    orbis_test::put(bytes,0x4000,64+16); orbis_test::put(bytes,0x8000,120+16);
    orbis_test::put(bytes,0x4000,0xb20);
    orbis_test::put(bytes,0x8000,0xc00); orbis_test::put(bytes,0x8008,0xc18);
    orbis_test::put(bytes,0x4000,0xc28);
    const uint8_t program[]={0x48,0xbf,0x10,0x80,0x04,0,0,0,0,0,0x8b,0x07,0xf4};
    std::copy(std::begin(program),std::end(program),bytes.begin()+0x400);
    orbis_test::put(bytes,value,0x610,4);
    return bytes;
}
}
