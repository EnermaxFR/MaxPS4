#include "../Native/OrbisV4GnmDriver.hpp"
#include "../Native/OrbisV4SystemInventory.hpp"
#include "../Native/OrbisLinker.hpp"
#include "OrbisV4CompatibilityFixture.h"
#include <cassert>
#include <iostream>
int main() {
    using Gnm=maxps4::OrbisV4GnmDriver;using Link=maxps4::OrbisLinker;
    const auto provider=Gnm::provider();maxps4::OrbisMetadata metadata;
    assert(metadata.parse(provider.data(),provider.size()) && metadata.symbols.size()==17);
    assert(metadata.modules.size()==1 && metadata.modules[0].exported && metadata.modules[0].version==0x101);
    assert(metadata.libraries.size()==1 && metadata.libraries[0].exported && metadata.libraries[0].version==1);
    for(size_t i=0;i<16;++i)assert(metadata.symbols[i+1].nid==Gnm::nids[i] && metadata.symbols[i+1].defined);
    assert(maxps4::OrbisV4SystemInventory::quote("a\n\"\\")=="\"a\\u000a\\\"\\\\\"");
    for(size_t first=0;first<16;first+=4){
        auto app=compat_fixture::consumer("libSceGnmDriver",Gnm::path,{Gnm::nids.begin()+first,Gnm::nids.begin()+first+4});
        std::vector<Link::Input> inputs{
            {{{"gnm-app.prx",{Gnm::path}},app.data(),app.size()},0x400000},
            {{{Gnm::path,{}},provider.data(),provider.size()},Gnm::bias}};
        Link::Image image;Link::Report report;std::string error;
        assert(Link::link(inputs,{},image,error,32*1024*1024,&report));
        assert(report.committed && report.resolvedImports==4 && report.validatedRelocations==4);
        const auto* got=image.translate(0x402000,32,6);assert(got);
        for(size_t i=0;i<4;++i)assert(maxps4::OrbisMetadata::read(got+8*i,8)==Gnm::bias+metadata.symbols[first+i+1].value);
        maxps4::OrbisMetadata caller;assert(caller.parse(app.data(),app.size()));
        const auto json=maxps4::OrbisV4SystemInventory::json(caller,"probe");
        assert(json.find("\"needed\":[\"libSceGnmDriver.prx\"]")!=std::string::npos);
        assert(json.find(Gnm::nids[first])!=std::string::npos && json.find("\"module_version\":257")!=std::string::npos);
        inputs.pop_back();assert(!Link::link(inputs,{},image,error) && error.find(Gnm::path)!=std::string::npos);
        inputs.push_back({{{Gnm::path,{}},provider.data(),provider.size()},Gnm::bias});
        orbis_fixture::setTag(app,0x61000015,1|(uint64_t{2}<<32)|(uint64_t{1}<<48));
        assert(!Link::link(inputs,{},image,error) && error.find("incompatible")!=std::string::npos);
    }
    for(const char* unsupported:{"bX5IbRvECXk","gAhCn6UiU4Y","nvEwfYAImTs"}){
        const auto app=compat_fixture::consumer("libSceGnmDriver",Gnm::path,{unsupported});
        Link::Image image;Link::Report report;std::string error;
        assert(!Link::link({{{{"gnm-app.prx",{Gnm::path}},app.data(),app.size()},0x400000},
                           {{{Gnm::path,{}},provider.data(),provider.size()},Gnm::bias}}, {},image,error,32*1024*1024,&report));
        assert(!report.committed && error.find("unresolved NID")!=std::string::npos);
    }
    std::cout<<"PASS partial GnmDriver: 16 exports/GOT relocations, missing/version refusals; doorbell/shader/resource imports unresolved; JSON inventory\n";
}
