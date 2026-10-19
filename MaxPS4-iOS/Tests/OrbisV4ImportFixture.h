#pragma once
#include "PS4OrbisExecutionFixture.h"
namespace v4_import_fixture {
inline std::vector<uint8_t> make(uint32_t addend=41) {
    auto b=orbis_execution::make(false);
    orbis_fixture::put(b,0x300,UINT64_MAX); // Linker must overwrite this with a guest address.
    auto replace=[&](const std::string& old,const std::string& name) {
        const auto at=std::search(b.begin(),b.end(),old.begin(),old.end());
        if (at==b.end() || name.size()>old.size()) throw std::runtime_error("fixture string");
        std::fill(at,at+old.size(),0); std::copy(name.begin(),name.end(),at);
    };
    replace("FixtureLibrary","libkernel"); replace("FixtureModule","libkernel");
    replace("AAAAAAAAAAA#B#B","aI+OeCz8xrQ#B#B");
    const uint8_t code[]={0x48,0x83,0xec,8,0xff,0x15,0,0,0,0,0x48,0x83,0xc4,8,0x05,0,0,0,0,0xc3};
    std::copy(std::begin(code),std::end(code),b.begin()+0x200);
    orbis_fixture::put(b,0x206,0x8000-(0x4000+10),4);
    orbis_fixture::put(b,0x20f,addend,4);
    orbis_fixture::put(b,64+32,sizeof(code)); orbis_fixture::put(b,64+40,sizeof(code));
    return b;
}
inline std::vector<uint8_t> make_clock(uint64_t output=orbis_execution::data+0x20) {
    auto b=make();
    const std::string old="aI+OeCz8xrQ", name="wRYVA5Zolso";
    const auto at=std::search(b.begin(),b.end(),old.begin(),old.end());
    if (at==b.end()) throw std::runtime_error("clock fixture symbol");
    std::copy(name.begin(),name.end(),at);
    const uint8_t code[]={0xbf,4,0,0,0,0x48,0xbe,0,0,0,0,0,0,0,0,
        0x48,0x83,0xec,8,0xff,0x15,0,0,0,0,0x48,0x83,0xc4,8,0x8b,0x46,8,0xc3};
    std::copy(std::begin(code),std::end(code),b.begin()+0x200);
    orbis_fixture::put(b,0x207,output);
    orbis_fixture::put(b,0x215,0x8000-(0x4000+25),4);
    orbis_fixture::put(b,64+32,sizeof(code)); orbis_fixture::put(b,64+40,sizeof(code));
    std::fill(b.begin()+0x310,b.begin()+0x360,0xa5);
    return b;
}
}
