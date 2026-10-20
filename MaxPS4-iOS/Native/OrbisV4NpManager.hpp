#pragma once
#include "OrbisV4VirtualPRX.hpp"
#include <array>
namespace maxps4 {
// Explicit offline profile: no init API, account database, request manager or
// PSN connection. A successful GetState reports SignedOut, never initialization.
struct OrbisV4NpManager {
    static constexpr const char* path="libSceNpManager.prx";
    static constexpr uint64_t bias=0x1b00000;
    static constexpr uint32_t invalid_argument=0x80550003,signed_out=0x80550006;
    static constexpr std::array<const char*,3> nids{"eQH7nWPcAgc","p-o74CnoNzY","XDncXQIJUSk"};
    static std::vector<uint8_t> provider(){
        std::vector<uint8_t> code;std::vector<OrbisV4VirtualPRX::Export> exports;
        for(size_t i=0;i<nids.size();++i){
            const size_t start=code.size();
            // uint32 userId=-1 or null output -> INVALID_ARGUMENT. Other IDs
            // select no account: the whole profile is SignedOut.
            const uint8_t check[]={0x83,0xff,0xff,0x74,0,0x48,0x85,0xf6,0x74,0};
            code.insert(code.end(),std::begin(check),std::end(check));
            if(i==0){const uint8_t body[]={0xc7,0x06,1,0,0,0,0x31,0xc0,0xc3};
                code.insert(code.end(),std::begin(body),std::end(body));}
            else {code.push_back(0xb8);for(unsigned b=0;b<4;++b)code.push_back(uint8_t(signed_out>>(8*b)));code.push_back(0xc3);}
            const size_t invalid=code.size();code.push_back(0xb8);
            for(unsigned b=0;b<4;++b)code.push_back(uint8_t(invalid_argument>>(8*b)));
            code.push_back(0xc3);code[start+4]=uint8_t(invalid-start-5);code[start+9]=uint8_t(invalid-start-10);
            exports.push_back({nids[i],start,code.size()-start});
        }
        return OrbisV4VirtualPRX::build("libSceNpManager",code,4,exports);
    }
};
}
