#include "../Native/OrbisV4NpCommon.hpp"
#include "../Native/OrbisLinker.hpp"
#include "OrbisV4CompatibilityFixture.h"
#include <cassert>
#include <iostream>
int main(){
    using NP=maxps4::OrbisV4NpCommon;using Link=maxps4::OrbisLinker;
    const auto provider=NP::provider();maxps4::OrbisMetadata metadata;
    assert(metadata.parse(provider.data(),provider.size()) && metadata.symbols.size()==3);
    assert(metadata.modules[0].exported && metadata.modules[0].name=="libSceNpCommon" && metadata.modules[0].version==0x101);
    assert(metadata.libraries[0].exported && metadata.libraries[0].name=="libSceNpCommon" && metadata.libraries[0].version==1);
    const auto app=compat_fixture::consumer("libSceNpCommon",NP::path,{NP::nids.begin(),NP::nids.end()});
    std::vector<Link::Input> inputs{
        {{{"np-app.prx",{NP::path}},app.data(),app.size()},0x400000},
        {{{NP::path,{}},provider.data(),provider.size()},NP::bias}};
    Link::Image image;Link::Report report;std::string error;
    assert(Link::link(inputs,{},image,error,32*1024*1024,&report));
    assert(report.committed && report.resolvedImports==2 && report.validatedRelocations==2);
    const auto* got=image.translate(0x402000,16,6);assert(got);
    for(size_t i=0;i<2;++i)assert(maxps4::OrbisMetadata::read(got+8*i,8)==NP::bias+metadata.symbols[i+1].value);
    inputs.pop_back();assert(!Link::link(inputs,{},image,error) && error.find(NP::path)!=std::string::npos);
    const auto unsupported=compat_fixture::consumer("libSceNpCommon",NP::path,{"fhJ5uKzcn0w"});
    assert(!Link::link({{{{"np-app.prx",{NP::path}},unsupported.data(),unsupported.size()},0x400000},
                       {{{NP::path,{}},provider.data(),provider.size()},NP::bias}}, {},image,error));
    assert(error.find("unresolved NID")!=std::string::npos);
    std::cout<<"PASS NpCommon two offline comparator imports/GOT; missing provider and unsupported thread import refused\n";
}
