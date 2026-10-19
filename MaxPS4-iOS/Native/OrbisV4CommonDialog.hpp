#pragma once
#include "OrbisV4VirtualPRX.hpp"
namespace maxps4 {
// Only the software initialization required by the real eboot. No dialog is
// opened, completed or answered on behalf of the user; UI remains unsupported.
struct OrbisV4CommonDialog {
    static constexpr const char* path="libSceCommonDialog.prx";
    static constexpr uint64_t bias=0x1c00000;
    static constexpr uint32_t already_initialized=0x80b80002;
    static std::vector<uint8_t> provider() {
        // XOR EAX,EAX; MOV EDX,1; LOCK CMPXCHG [RIP+state],EDX;
        // JNZ duplicate; RET; duplicate: MOV EAX,ALREADY_SYSTEM_INITIALIZED; RET.
        std::vector<uint8_t> code{0x31,0xc0,0xba,1,0,0,0,0xf0,0x0f,0xb1,0x15};
        const uint32_t displacement=0x8000-(0x4000+uint32_t(code.size())+4);
        for(unsigned i=0;i<4;++i)code.push_back(uint8_t(displacement>>(8*i)));
        code.insert(code.end(),{0x75,1,0xc3,0xb8});
        for(unsigned i=0;i<4;++i)code.push_back(uint8_t(already_initialized>>(8*i)));
        code.push_back(0xc3);
        return OrbisV4VirtualPRX::build("libSceCommonDialog",code,4,{{"uoUpLGNkygk",0,code.size()}});
    }
};
}
