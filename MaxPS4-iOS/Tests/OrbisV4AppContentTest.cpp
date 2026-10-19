#include "../Native/OrbisV4AppContent.hpp"
#include "../Native/OrbisLinker.hpp"
#include "OrbisLinkerFixture.h"
#include <cassert>
#include <iostream>
int main() {
    // One checked integer SFO entry, including a negative bit pattern.
    std::vector<uint8_t> sfo(64,0);
    orbis_fixture::put(sfo,0,0x46535000,4);orbis_fixture::put(sfo,4,0x101,4);
    orbis_fixture::put(sfo,8,36,4);orbis_fixture::put(sfo,12,60,4);orbis_fixture::put(sfo,16,1,4);
    orbis_fixture::put(sfo,22,0x404,2);orbis_fixture::put(sfo,24,4,4);orbis_fixture::put(sfo,28,4,4);
    const std::string key="USER_DEFINED_PARAM_2";std::copy(key.begin(),key.end(),sfo.begin()+36);
    orbis_fixture::put(sfo,60,0xfffffffeU,4);
    std::array<int32_t,4> parameters{99,99,99,99};std::string sfoError;
    assert(maxps4::OrbisV4AppContent::parameters_from_sfo(sfo,parameters,sfoError));
    assert((parameters==std::array<int32_t,4>{0,-2,0,0}));
    auto bad=sfo;orbis_fixture::put(bad,32,0xffffffffU,4);
    assert(!maxps4::OrbisV4AppContent::parameters_from_sfo(bad,parameters,sfoError));
    assert((parameters==std::array<int32_t,4>{0,-2,0,0}));
    bad=sfo;orbis_fixture::put(bad,22,4,2);
    assert(!maxps4::OrbisV4AppContent::parameters_from_sfo(bad,parameters,sfoError));
    auto provider=maxps4::OrbisV4AppContent::provider({11,22,33,44});
    maxps4::OrbisMetadata metadata;
    assert(metadata.parse(provider.data(),provider.size()) && metadata.symbols.size()==3);
    assert(metadata.modules.size()==1 && metadata.modules[0].exported && metadata.modules[0].version==0x101);
    assert(metadata.symbols[1].nid=="R9lA82OraNs" && metadata.symbols[2].nid=="99b82IKXpH4");
    auto app=orbis_fixture::make(false);
    std::fill(app.begin()+0x600,app.begin()+0x800,0);
    size_t cursor=0x601;
    auto text=[&](const std::string& value) {const auto p=cursor-0x600;
        for(char c:value)app.at(cursor++)=uint8_t(c);
        app.at(cursor++)=0;return p;};
    const auto lib=text("libSceAppContent"),mod=text("libSceAppContentUtil");
    const auto get=text("99b82IKXpH4#B#B"),init=text("R9lA82OraNs#B#B"),dep=text("libSceAppContent.prx");
    orbis_fixture::setTag(app,0x61000037,cursor-0x600);
    orbis_fixture::setTag(app,0x61000015,lib|(uint64_t{1}<<32)|(uint64_t{1}<<48));
    orbis_fixture::setTag(app,0x6100000f,mod|(uint64_t{0x101}<<32)|(uint64_t{1}<<48));
    orbis_fixture::setTag(app,0x6100003f,72);orbis_fixture::setTag(app,0x61000031,48);
    orbis_fixture::appendTag(app,1,dep);
    orbis_fixture::put(app,0x818,get,4);
    orbis_fixture::put(app,0x830,init,4);app[0x834]=0x12;
    orbis_fixture::put(app,0x918,0x2008);orbis_fixture::put(app,0x920,(uint64_t{2}<<32)|7);
    using Link=maxps4::OrbisLinker;
    auto inputs=std::vector<Link::Input>{
        {{{"eboot.bin",{maxps4::OrbisV4AppContent::path}},app.data(),app.size()},0x400000},
        {{{maxps4::OrbisV4AppContent::path,{}},provider.data(),provider.size()},maxps4::OrbisV4AppContent::bias}};
    Link::Image image;Link::Report report;std::string error;
    assert(Link::link(inputs,{},image,error,32*1024*1024,&report));
    assert(report.committed && report.resolvedImports==2 && report.validatedRelocations==2);
    const auto* slots=image.translate(0x402000,16,6);assert(slots);
    assert(maxps4::OrbisMetadata::read(slots,8)==maxps4::OrbisV4AppContent::bias+metadata.symbols[2].value);
    assert(maxps4::OrbisMetadata::read(slots+8,8)==maxps4::OrbisV4AppContent::bias+metadata.symbols[1].value);
    inputs.pop_back();assert(!Link::link(inputs,{},image,error,32*1024*1024,&report));
    assert(error.find("libSceAppContent.prx")!=std::string::npos && !report.committed);
    inputs.push_back({{{maxps4::OrbisV4AppContent::path,{}},provider.data(),provider.size()},maxps4::OrbisV4AppContent::bias});
    app[0x600+get]='X';
    assert(!Link::link(inputs,{},image,error,32*1024*1024,&report) && error.find("unresolved NID")!=std::string::npos);
    std::cout<<"PASS AppContent virtual PRX: exact module/library versions, both real NIDs, two GOT relocations, missing/unknown-provider refusals\n";
}
