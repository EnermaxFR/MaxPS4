#pragma once
#include "OrbisV4VirtualPRX.hpp"
#include "OrbisV4TLSLayout.hpp"
namespace maxps4 {
// Pure guest SysV __tls_get_addr for a static eager registry. TCB/DTV metadata
// must be created read-only by OrbisV4TLSRuntime; unsupported inputs trap (UD2).
struct OrbisV4TLSGetAddr {
    static constexpr const char* nid="vNe1w4diLCs";
    static std::vector<uint8_t> provider(const std::vector<OrbisV4TLSModule>& modules) {
        if(modules.empty() || modules.size()>16)throw std::invalid_argument("TLS resolver profile: 1..16 templates");
        for(const auto& module:modules)if(module.id>128)throw std::invalid_argument("TLS resolver module ID");
        std::vector<OrbisV4TLSPlacement> placements;size_t total=0;
        if(OrbisV4TLSLayout::plan(modules,placements,total))throw std::invalid_argument("TLS resolver layout");
        std::vector<uint8_t> code;std::vector<size_t> errors;
        auto emit=[&](std::initializer_list<uint8_t> bytes){code.insert(code.end(),bytes);};
        auto imm=[&](uint32_t value){for(size_t i=0;i<4;++i)code.push_back(uint8_t(value>>(i*8)));};
        auto jump=[&](uint8_t cc){emit({0x0f,cc});const auto p=code.size();imm(0);return p;};
        auto patch=[&](size_t p,size_t target){const auto d=uint32_t(target-p-4);for(size_t i=0;i<4;++i)code[p+i]=uint8_t(d>>(i*8));};
        emit({0x48,0x8b,0x0f,0x48,0x8b,0x57,8}); // RCX=module, RDX=offset
        emit({0x64,0x4c,0x8b,0x04,0x25,8,0,0,0}); // R8=FS:[8] (guest DTV)
        emit({0x49,0x83,0x38,1});errors.push_back(jump(0x85));
        emit({0x48,0x85,0xc9});errors.push_back(jump(0x84));
        emit({0x49,0x3b,0x48,8});errors.push_back(jump(0x87));
        emit({0x49,0x8b,0x44,0xc8,8,0x48,0x85,0xc0});errors.push_back(jump(0x84));
        for(const auto& module:modules) {
            emit({0x48,0x81,0xf9});imm(module.id);const auto next=jump(0x85);
            emit({0x48,0x81,0xfa});imm(uint32_t(module.memory_size));errors.push_back(jump(0x83));
            emit({0x48,0x01,0xd0,0xc3});patch(next,code.size());
        }
        for(auto p:errors)patch(p,code.size());
        emit({0x0f,0x0b});
        return OrbisV4VirtualPRX::build("libkernel",code,4,{{nid,0,code.size()}});
    }
};
}
