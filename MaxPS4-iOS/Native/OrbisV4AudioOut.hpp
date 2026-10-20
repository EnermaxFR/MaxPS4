#pragma once
#include "OrbisV4VirtualPRX.hpp"
#include <array>

namespace maxps4 {
// Explicit virtual PRX with an unavailable device backend. Software init is
// real state; Open fails instead of issuing a fake handle. No PCM is consumed,
// queued, timed or played, and Output never reports a successful transfer.
struct OrbisV4AudioOut {
    static constexpr const char* path="libSceAudioOut.prx";
    static constexpr uint64_t bias=0x1800000;
    static constexpr uint32_t not_opened=0x80260001,invalid_port=0x80260003,
        invalid_size=0x80260006,invalid_format=0x80260007,invalid_frequency=0x80260008,
        invalid_type=0x8026000a,already_init=0x8026000e,not_init=0x8026000f,
        backend_unavailable=0x80260012;
    static constexpr std::array<const char*,5> nids{
        "s1--uE9mBFw","JfEPXVxhFqA","ekNvsT22rsY","QOQtbeDqsT4","b+uAV89IlxE"};
    static std::vector<uint8_t> provider() {
        std::vector<uint8_t> code;
        std::map<uint32_t,std::vector<size_t>> errors;
        auto emit=[&](std::initializer_list<uint8_t> bytes){code.insert(code.end(),bytes);};
        auto imm=[&](uint32_t v){for(unsigned i=0;i<4;++i)code.push_back(uint8_t(v>>(8*i)));};
        auto rip=[&](std::initializer_list<uint8_t> op,size_t tail=0){
            emit(op);imm(uint32_t(0x8000-(0x4000+code.size()+4+tail)));
        };
        auto branch=[&](uint8_t cc){emit({0x0f,cc});const auto p=code.size();imm(0);return p;};
        auto error=[&](uint8_t cc,uint32_t value){errors[value].push_back(branch(cc));};
        auto patch=[&](size_t p,size_t target){const auto v=uint32_t(target-p-4);
            for(unsigned i=0;i<4;++i)code[p+i]=uint8_t(v>>(8*i));};
        auto result=[&](uint32_t value){emit({0xb8});imm(value);emit({0xc3});};
        auto initialized=[&]{rip({0x83,0x3d},1);emit({0});error(0x84,not_init);};
        auto type=[&](bool handle){
            // EAX is the uint32 port type, stripped from a handle if needed.
            emit({0x83,0xf8,4});const auto standard=branch(0x86);
            size_t personal=0;
            if(handle){emit({0x83,0xf8,14});personal=branch(0x84);}
            emit({0x83,0xf8,126});const auto audio3d=branch(0x84);
            emit({0x83,0xf8,127});error(0x85,invalid_type);
            patch(standard,code.size());patch(audio3d,code.size());
            if(handle)patch(personal,code.size());
        };
        // Init atomically claims the software manager, independent of devices.
        const size_t init=code.size();
        emit({0x31,0xc0,0xba,1,0,0,0});rip({0xf0,0x0f,0xb1,0x15});
        error(0x85,already_init);emit({0x31,0xc0,0xc3});
        const size_t open=code.size();initialized();
        emit({0x85,0xc9});error(0x84,invalid_size);
        emit({0x81,0xf9});imm(2048);error(0x87,invalid_size);
        emit({0xf7,0xc1});imm(255);error(0x85,invalid_size);
        emit({0x89,0xf0});type(false);
        emit({0x41,0x81,0xf8});imm(48000);error(0x85,invalid_frequency);
        emit({0x44,0x89,0xc8,0x25});imm(255);emit({0x83,0xf8,7});error(0x87,invalid_format);
        result(backend_unavailable); // No device/port allocated, even for valid input.
        const size_t closedPort=code.size();initialized();
        emit({0x89,0xf8,0x25});imm(0x3f000000);emit({0x3d});imm(0x20000000);error(0x85,invalid_port);
        emit({0x89,0xf8,0x25});imm(255);emit({0x83,0xf8,25});error(0x83,invalid_port);
        emit({0x89,0xf8,0xc1,0xe8,16,0x25});imm(255);type(true);
        result(not_opened); // Close/Output/Volume fail before touching caller buffers.
        const size_t bodyEnd=code.size();
        for(const auto& entry:errors){for(auto p:entry.second)patch(p,code.size());result(entry.first);}

        const std::array<size_t,5> starts{closedPort,init,open,closedPort,closedPort};
        const std::array<size_t,5> sizes{bodyEnd-closedPort,open-init,closedPort-open,bodyEnd-closedPort,bodyEnd-closedPort};
        std::vector<OrbisV4VirtualPRX::Export> exports;
        for(size_t i=0;i<nids.size();++i)exports.push_back({nids[i],starts[i],sizes[i]});
        return OrbisV4VirtualPRX::build("libSceAudioOut",code,4,exports);
    }
};
}
