#pragma once
#include "OrbisV4VirtualPRX.hpp"
#include <array>
namespace maxps4 {
// Explicit unavailable backend: no contexts or handles are ever allocated, no
// outputs are written, and no registration/unlock/PSN action reports success.
struct OrbisV4NpTrophy {
    static constexpr const char* path="libSceNpTrophy.prx";
    static constexpr uint64_t bias=0x1f00000;
    static constexpr uint32_t invalid_argument=0x80551604,invalid_handle=0x80551608,
        invalid_context=0x80551609,invalid_trophy_id=0x8055160a,not_supported=0x80551622;
    static constexpr std::array<const char*,8> nids{
        "XbkjbobZlCY","E1Wrwd07Lr8","aTnHs7W-9Uk","q7U6tEAQf7c",
        "GNcF4oidY0Y","LHuSmO3SLd8","TJCAxto9SEU","28xmRUFao68"};
    static std::vector<uint8_t> provider(){
        std::vector<uint8_t> code;std::map<uint32_t,std::vector<size_t>> errors;
        auto emit=[&](std::initializer_list<uint8_t> v){code.insert(code.end(),v);};
        auto imm=[&](uint32_t v){for(unsigned b=0;b<4;++b)code.push_back(uint8_t(v>>(8*b)));};
        auto reject=[&](uint8_t cc,uint32_t error){emit({0x0f,cc});errors[error].push_back(code.size());imm(0);};
        auto result=[&](uint32_t value){emit({0xb8});imm(value);emit({0xc3});};
        std::vector<OrbisV4VirtualPRX::Export> exports;
        for(size_t i=0;i<nids.size();++i){
            const size_t start=code.size();
            switch(i){
            case 0: // CreateContext: output pointer and uint64 options.
                emit({0x48,0x85,0xff});reject(0x84,invalid_argument);
                emit({0x48,0x85,0xc9});reject(0x85,invalid_argument);
                result(not_supported);break;
            case 3: // CreateHandle: no fake token is produced.
                emit({0x48,0x85,0xff});reject(0x84,invalid_argument);
                result(not_supported);break;
            case 2:case 4:result(invalid_handle);break;
            case 5: // GetTrophyUnlockState: never clear flags/count as a fake result.
                emit({0x48,0x85,0xd2});reject(0x84,invalid_argument);
                emit({0x48,0x85,0xc9});reject(0x84,invalid_argument);
                emit({0x83,0xff,0xff});reject(0x84,invalid_context);
                emit({0x83,0xfe,0xff});reject(0x84,invalid_handle);
                result(invalid_context);break;
            case 6: // RegisterContext: options are uint64, not truncated.
                emit({0x48,0x85,0xd2});reject(0x85,invalid_argument);
                emit({0x83,0xff,0xff});reject(0x84,invalid_context);
                emit({0x83,0xfe,0xff});reject(0x84,invalid_handle);
                result(invalid_context);break;
            case 7: // UnlockTrophy: validate scalar ABI only; no trophy state exists.
                emit({0x83,0xff,0xff});reject(0x84,invalid_context);
                emit({0x83,0xfe,0xff});reject(0x84,invalid_handle);
                emit({0x81,0xfa});imm(128);reject(0x83,invalid_trophy_id);
                emit({0x48,0x85,0xc9});reject(0x84,invalid_argument);
                result(invalid_context);break;
            default:result(invalid_context);break;
            }
            exports.push_back({nids[i],start,code.size()-start});
        }
        for(const auto& error:errors){
            for(auto p:error.second){const auto offset=uint32_t(code.size()-p-4);
                for(unsigned b=0;b<4;++b)code[p+b]=uint8_t(offset>>(8*b));}
            result(error.first);
        }
        return OrbisV4VirtualPRX::build("libSceNpTrophy",code,4,exports);
    }
};
}
