#pragma once
#include "OrbisV4VirtualPRX.hpp"
#include <array>
namespace maxps4 {
// Two real byte operations. No heap, stdout, teardown or clock implementation.
// Invalid buffers fault in guest translation; memcpy overlap remains undefined.
struct OrbisV4LibcInternal {
    static constexpr const char* path="libSceLibcInternal.prx";
    static constexpr uint64_t bias=0x1900000;
    static constexpr std::array<const char*,2> nids{"8zTFvBIAIN8","Q3VBxCXhUHs"};
    static std::vector<uint8_t> provider(){
        std::vector<uint8_t> code;std::vector<OrbisV4VirtualPRX::Export> exports;
        auto emit=[&](std::initializer_list<uint8_t> v){code.insert(code.end(),v);};
        auto branch=[&](uint8_t cc){emit({0x0f,cc});const auto p=code.size();emit({0,0,0,0});return p;};
        auto patch=[&](size_t p,size_t target){const auto v=uint32_t(target-p-4);
            for(unsigned b=0;b<4;++b)code[p+b]=uint8_t(v>>(8*b));};
        for(size_t fn=0;fn<2;++fn){
            const size_t start=code.size();emit({0x49,0x89,0xf8,0x48,0x85,0xd2}); // R8=dest; TEST size
            const auto empty=branch(0x84);emit({0x31,0xc9});const auto probe=code.size();
            if(fn)emit({0x0f,0xb6,0x04,0x0e}); // source read before any data changes
            emit({0xf0,0x80,0x0c,0x0f,0,0x48,0xff,0xc1,0x48,0x39,0xd1}); // LOCK OR byte [dest+RCX],0
            patch(branch(0x85),probe);
            emit({0x31,0xc9});const auto copy=code.size();
            if(fn)emit({0x0f,0xb6,0x04,0x0e,0x88,0x04,0x0f});
            else emit({0x40,0x88,0x34,0x0f}); // SIL truncated to uint8
            emit({0x48,0xff,0xc1,0x48,0x39,0xd1});patch(branch(0x85),copy);
            patch(empty,code.size());emit({0x4c,0x89,0xc0,0xc3}); // original dest, including size zero
            exports.push_back({nids[fn],start,code.size()-start});
        }
        return OrbisV4VirtualPRX::build("libSceLibcInternal",code,4,exports);
    }
};
}
