#pragma once
#include "OrbisMetadata.hpp"
#include "PS4SFO.hpp"
#include <array>

namespace maxps4 {
// Explicit virtual PRX, built from our guest x86 code; not Sony firmware.
// A bounded AppContent profile: zero-reserved init, single initialization,
// boot attr=0 (no addons), configured user parameters 1..4. SKU/DRM is absent
// and parameter 0 is refused rather than claiming a trial/full entitlement.
struct OrbisV4AppContent {
    static constexpr const char* path="libSceAppContent.prx";
    static constexpr uint64_t bias=0x2000000;
    static constexpr uint32_t parameter=0x80d90002U, busy=0x80d90003U;
    static bool parameters_from_sfo(const std::vector<uint8_t>& bytes,std::array<int32_t,4>& output,std::string& error) {
        PS4SFO checked;
        if (!checked.parse(bytes)) {error=checked.error;return false;}
        std::array<int32_t,4> pending{};
        const auto keys=OrbisMetadata::read(bytes.data()+8,4),data=OrbisMetadata::read(bytes.data()+12,4);
        const auto count=OrbisMetadata::read(bytes.data()+16,4);
        for(size_t i=0;i<count;++i) {
            const auto* entry=bytes.data()+20+i*16;
            const auto name=std::string(reinterpret_cast<const char*>(bytes.data()+keys+OrbisMetadata::read(entry,2)));
            for(size_t id=0;id<4;++id) if(name=="USER_DEFINED_PARAM_"+std::to_string(id+1)) {
                if(OrbisMetadata::read(entry+2,2)!=0x404) {error="AppContent user parameter must be int32";return false;}
                const auto bits=uint32_t(OrbisMetadata::read(bytes.data()+data+OrbisMetadata::read(entry+12,4),4));
                std::memcpy(&pending[id],&bits,4);
            }
        }
        output=pending;error.clear();return true;
    }
    static std::vector<uint8_t> provider(const std::array<int32_t,4>& parameters={}) {
        std::vector<uint8_t> code;
        auto emit=[&](std::initializer_list<uint8_t> bytes) {code.insert(code.end(),bytes);};
        auto imm=[&](uint32_t value) {for(unsigned i=0;i<4;++i) code.push_back(uint8_t(value>>(8*i)));};
        auto rip=[&](std::initializer_list<uint8_t> op,uint64_t target,size_t tail=0) {
            emit(op); imm(uint32_t(target-(0x4000+code.size()+4+tail)));
        };
        std::vector<size_t> errors;
        auto branch=[&](uint8_t condition) {emit({0x0f,condition});const auto p=code.size();imm(0);return p;};
        auto patch=[&](size_t p,size_t target) {
            const auto displacement=uint32_t(target-(p+4));
            for(unsigned i=0;i<4;++i) code[p+i]=uint8_t(displacement>>(8*i));
        };
        emit({0x48,0x85,0xff});errors.push_back(branch(0x84)); // initParam != NULL
        emit({0x48,0x85,0xf6});errors.push_back(branch(0x84)); // bootParam != NULL
        for(uint8_t offset:{0,8,16,24}) {
            emit({0x48,0x83,0x7f,offset,0});errors.push_back(branch(0x85));
        }
        rip({0x83,0x3d},0x8000,1);emit({0});const auto duplicate=branch(0x85);
        emit({0xc7,0x46,4});imm(0); // Write only boot.attr, preserve reserved bytes.
        rip({0xc7,0x05},0x8000,4);imm(1); // Commit initialized after output write.
        emit({0x31,0xc0,0xc3});
        const auto error=code.size();emit({0xb8});imm(parameter);emit({0xc3});
        const auto busyLabel=code.size();emit({0xb8});imm(busy);emit({0xc3});
        for(auto p:errors) patch(p,error);
        patch(duplicate,busyLabel);
        const auto getInt=code.size();errors.clear();
        emit({0x89,0xff}); // uint32 ABI argument: ignore unspecified upper RDI bits.
        emit({0x48,0x85,0xf6});errors.push_back(branch(0x84));
        emit({0x83,0xff,1});errors.push_back(branch(0x82)); // unsigned id >= 1
        emit({0x83,0xff,4});errors.push_back(branch(0x87));
        rip({0x48,0x8d,0x05},0x8000); // state followed by four int32 parameters
        emit({0x8b,0x04,0xb8,0x89,0x06,0x31,0xc0,0xc3});
        for(auto p:errors) patch(p,error);

        std::vector<uint8_t> b(0xc00,0);
        auto put=[&](size_t p,uint64_t v,size_t n=8) {for(size_t i=0;i<n;++i)b.at(p+i)=uint8_t(v>>(8*i));};
        const uint8_t magic[]={0x7f,'E','L','F',2,1,1,9,0};
        std::copy(std::begin(magic),std::end(magic),b.begin());
        put(16,0xfe18,2);put(18,62,2);put(20,1,4);put(32,64);
        put(52,64,2);put(54,56,2);put(56,4,2);
        auto ph=[&](size_t i,uint32_t type,uint32_t flags,uint64_t offset,uint64_t address,uint64_t size) {
            const auto p=64+i*56;put(p,type,4);put(p+4,flags,4);put(p+8,offset);
            put(p+16,address);put(p+32,size);put(p+40,size);put(p+48,1);
        };
        ph(0,1,5,0x200,0x4000,code.size());ph(1,1,6,0x400,0x8000,20);
        ph(2,2,4,0x500,0,0x180);ph(3,0x61000000,4,0x700,0,0x500);
        std::copy(code.begin(),code.end(),b.begin()+0x200);
        for(size_t i=0;i<4;++i)put(0x404+i*4,uint32_t(parameters[i]),4);
        size_t cursor=0x701;
        auto text=[&](const std::string& value) {const auto p=cursor-0x700;
            for(char c:value)b.at(cursor++)=uint8_t(c);
            b.at(cursor++)=0;return p;};
        const auto library=text("libSceAppContent"),module=text("libSceAppContentUtil");
        const auto initName=text("R9lA82OraNs#B#B"),getName=text("99b82IKXpH4#B#B");
        size_t d=0x500;
        auto tag=[&](uint64_t t,uint64_t v) {put(d,t);put(d+8,v);d+=16;};
        tag(0x61000035,0);tag(0x61000037,cursor-0x700);tag(0x61000039,0x200);
        tag(0x6100003b,24);tag(0x6100003f,72);
        tag(0x61000013,library|(uint64_t{1}<<32)|(uint64_t{1}<<48));
        tag(0x6100000d,module|(uint64_t{0x101}<<32)|(uint64_t{1}<<48));
        auto symbol=[&](size_t i,size_t name,uint64_t offset,uint64_t size) {
            const auto p=0x900+i*24;put(p,name,4);b[p+4]=0x12;put(p+6,1,2);
            put(p+8,0x4000+offset);put(p+16,size);
        };
        symbol(1,initName,0,getInt);symbol(2,getName,getInt,code.size()-getInt);
        return b;
    }
};
}
