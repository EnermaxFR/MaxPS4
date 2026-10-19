#pragma once
#include "OrbisV4VirtualPRX.hpp"
#include <array>

namespace maxps4 {
// Software lifetime only. Without a dialog UI backend, Open cannot publish
// RUNNING/FINISHED or acknowledge a user action. Only the four actual imports
// are exported; Close/GetStatus and extended Open variants remain unresolved.
struct OrbisV4ErrorDialog {
    static constexpr const char* path="libSceErrorDialog.prx";
    static constexpr uint64_t bias=0x1e00000;
    static constexpr uint32_t not_initialized=0x80b80003,already_initialized=0x80b80004,
        invalid_state=0x80b80006,param_invalid=0x80b8000a,arg_null=0x80b8000d,
        not_supported=0x80b8000f;
    static constexpr std::array<const char*,4> nids{
        "I88KChlynSs","M2ZF-ClLhgY","9XAxK2PMwk8","WWiGuh9XfgQ"};
    static std::vector<uint8_t> provider() {
        std::vector<uint8_t> code;
        std::map<uint32_t,std::vector<size_t>> errors;
        auto emit=[&](std::initializer_list<uint8_t> bytes){code.insert(code.end(),bytes);};
        auto imm=[&](uint32_t v){for(unsigned i=0;i<4;++i)code.push_back(uint8_t(v>>(8*i)));};
        auto rip=[&](std::initializer_list<uint8_t> op){
            emit(op);imm(uint32_t(0x8000-(0x4000+code.size()+4)));
        };
        auto error=[&](uint8_t cc,uint32_t value){
            emit({0x0f,cc});errors[value].push_back(code.size());imm(0);
        };
        auto result=[&](uint32_t value){emit({0xb8});imm(value);emit({0xc3});};
        const size_t init=code.size();
        emit({0x31,0xc0,0xba,1,0,0,0});rip({0xf0,0x0f,0xb1,0x15});
        error(0x85,already_initialized);emit({0x31,0xc0,0xc3});
        const size_t open=code.size();
        rip({0x8b,0x05});emit({0x83,0xf8,1});error(0x85,invalid_state);
        emit({0x48,0x85,0xff});error(0x84,arg_null);
        // Read all 16 ABI bytes through translated guest memory. Parameters
        // may be unaligned/read-only. No host pointer or guest write is used.
        emit({0x48,0x8b,0x07,0x48,0x8b,0x57,8});
        emit({0x83,0xf8,16});error(0x85,param_invalid);
        emit({0x48,0xc1,0xea,32,0x85,0xd2});error(0x85,param_invalid);
        result(not_supported);
        const size_t terminate=code.size();
        emit({0xb8,1,0,0,0,0x31,0xd2});rip({0xf0,0x0f,0xb1,0x15});
        error(0x85,not_initialized);emit({0x31,0xc0,0xc3});
        const size_t status=code.size();rip({0x8b,0x05});emit({0xc3});
        const size_t bodyEnd=code.size();
        for(const auto& entry:errors){
            for(auto p:entry.second){const auto v=uint32_t(code.size()-p-4);
                for(unsigned i=0;i<4;++i)code[p+i]=uint8_t(v>>(8*i));}
            result(entry.first);
        }
        const std::array<size_t,4> starts{init,open,terminate,status};
        const std::array<size_t,4> ends{open,terminate,status,bodyEnd};
        std::vector<OrbisV4VirtualPRX::Export> exports;
        for(size_t i=0;i<nids.size();++i)exports.push_back({nids[i],starts[i],ends[i]-starts[i]});
        return OrbisV4VirtualPRX::build("libSceErrorDialog",code,4,exports);
    }
};
}
