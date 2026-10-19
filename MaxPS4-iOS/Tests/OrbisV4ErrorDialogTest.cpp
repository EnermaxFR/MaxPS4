#include "../Native/OrbisV4ErrorDialog.hpp"
#include "../Native/OrbisLinker.hpp"
#include "OrbisV4CompatibilityFixture.h"
#include <cassert>
#include <iostream>
int main() {
    using Dialog=maxps4::OrbisV4ErrorDialog;
    const auto provider=Dialog::provider();maxps4::OrbisMetadata metadata;
    assert(metadata.parse(provider.data(),provider.size()) && metadata.symbols.size()==5);
    assert(metadata.modules.size()==1 && metadata.modules[0].exported && metadata.modules[0].version==0x101);
    assert(metadata.libraries.size()==1 && metadata.libraries[0].exported && metadata.libraries[0].version==1);
    for(size_t i=0;i<4;++i)assert(metadata.symbols[i+1].nid==Dialog::nids[i] && metadata.symbols[i+1].defined);
    auto app=compat_fixture::consumer("libSceErrorDialog",Dialog::path,{Dialog::nids.begin(),Dialog::nids.end()});
    using Link=maxps4::OrbisLinker;
    std::vector<Link::Input> inputs{
        {{{"error-app.prx",{Dialog::path}},app.data(),app.size()},0x400000},
        {{{Dialog::path,{}},provider.data(),provider.size()},Dialog::bias}};
    Link::Image image;Link::Report report;std::string error;
    assert(Link::link(inputs,{},image,error,32*1024*1024,&report));
    assert(report.committed && report.resolvedImports==4 && report.validatedRelocations==4);
    const auto* got=image.translate(0x402000,32,6);assert(got);
    for(size_t i=0;i<4;++i)assert(maxps4::OrbisMetadata::read(got+8*i,8)==Dialog::bias+metadata.symbols[i+1].value);
    inputs.pop_back();assert(!Link::link(inputs,{},image,error) && error.find(Dialog::path)!=std::string::npos);
    inputs.push_back({{{Dialog::path,{}},provider.data(),provider.size()},Dialog::bias});
    orbis_fixture::setTag(app,0x61000015,1|(uint64_t{2}<<32)|(uint64_t{1}<<48));
    assert(!Link::link(inputs,{},image,error) && error.find("incompatible")!=std::string::npos);
    orbis_fixture::setTag(app,0x61000015,1|(uint64_t{1}<<32)|(uint64_t{1}<<48));
    const auto firstName=maxps4::OrbisMetadata::read(app.data()+0x818,4);
    app[0x600+firstName]='X';assert(!Link::link(inputs,{},image,error) && error.find("unresolved NID")!=std::string::npos);
    std::cout<<"PASS ErrorDialog four imports/GOT relocations; version/unknown-NID/missing-provider refusals\n";
}
