#pragma once
#include "OrbisV4VirtualPRX.hpp"
#include <array>
namespace maxps4 {
// Offline byte comparisons only, not authentication or PSN initialization.
// The real files currently import no NpCommon functions. These two exports
// implement verifiable utility contracts instead of an empty placeholder PRX.
struct OrbisV4NpCommon {
    static constexpr const char* path="libSceNpCommon.prx";
    static constexpr uint64_t bias=0x1d00000;
    static constexpr uint32_t invalid_argument=0x80550003,not_match=0x80550609;
    static constexpr std::array<const char*,2> nids{"dj+O5aD2a0Q","i8UmXTSq7N4"};
    static std::vector<uint8_t> provider() {
        std::vector<uint8_t> code;
        auto emit=[&](std::initializer_list<uint8_t> v){code.insert(code.end(),v);};
        auto imm=[&](uint32_t v){for(unsigned i=0;i<4;++i)code.push_back(uint8_t(v>>(8*i)));};
        auto branch=[&](uint8_t cc){emit({0x0f,cc});const auto p=code.size();imm(0);return p;};
        auto patch=[&](size_t p,size_t target){const auto v=uint32_t(target-p-4);
            for(unsigned i=0;i<4;++i)code[p+i]=uint8_t(v>>(8*i));};
        auto result=[&](uint32_t v){const auto p=code.size();emit({0xb8});imm(v);emit({0xc3});return p;};
        std::vector<OrbisV4VirtualPRX::Export> exports;
        for(size_t fn=0;fn<2;++fn){
            const auto start=code.size();
            emit({0x48,0x85,0xff});const auto null1=branch(0x84);
            emit({0x48,0x85,0xf6});const auto null2=branch(0x84);
            emit({0x31,0xc9});const auto loop=code.size();
            emit({0x0f,0xb6,0x04,0x0f,0x3a,0x04,0x0e});const auto mismatch=branch(0x85);
            emit({0x84,0xc0});const auto nul=branch(0x84);
            emit({0xff,0xc1,0x83,0xf9,16});patch(branch(0x85),loop);
            patch(nul,code.size());
            size_t tailMismatch=0;
            if(fn){
                // OrbisNpOnlineId is 20 bytes; opt[8] and reserved[8] follow.
                // term/dummy are not part of the comparison contract.
                emit({0xb9,20,0,0,0});const auto tail=code.size();
                emit({0x0f,0xb6,0x04,0x0f,0x3a,0x04,0x0e});tailMismatch=branch(0x85);
                emit({0xff,0xc1,0x83,0xf9,36});patch(branch(0x85),tail);
            }
            result(0);const auto different=result(not_match),invalid=result(invalid_argument);
            patch(null1,invalid);patch(null2,invalid);patch(mismatch,different);
            if(fn)patch(tailMismatch,different);
            exports.push_back({nids[fn],start,code.size()-start});
        }
        return OrbisV4VirtualPRX::build("libSceNpCommon",code,4,exports);
    }
};
}
