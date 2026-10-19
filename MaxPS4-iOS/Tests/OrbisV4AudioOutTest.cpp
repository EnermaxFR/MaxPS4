#include "../Native/OrbisV4AudioOut.hpp"
#include "../Native/OrbisLinker.hpp"
#include "OrbisV4CompatibilityFixture.h"
#include "../Native/OrbisV4CommonDialog.hpp"
#include <cassert>
#include <iostream>
int main() {
    using Audio=maxps4::OrbisV4AudioOut;
    const auto provider=Audio::provider();maxps4::OrbisMetadata metadata;
    assert(metadata.parse(provider.data(),provider.size()) && metadata.symbols.size()==6);
    assert(metadata.modules.size()==1 && metadata.modules[0].exported && metadata.modules[0].version==0x101);
    assert(metadata.libraries.size()==1 && metadata.libraries[0].exported && metadata.libraries[0].version==1);
    for(size_t i=0;i<5;++i)assert(metadata.symbols[i+1].nid==Audio::nids[i] && metadata.symbols[i+1].defined);
    auto app=compat_fixture::consumer("libSceAudioOut",Audio::path,{Audio::nids.begin(),Audio::nids.end()});
    constexpr size_t library=1;
    using Link=maxps4::OrbisLinker;
    std::vector<Link::Input> inputs{
        {{{"audio-app.prx",{Audio::path}},app.data(),app.size()},0x400000},
        {{{Audio::path,{}},provider.data(),provider.size()},Audio::bias}};
    Link::Image image;Link::Report report;std::string error;
    assert(Link::link(inputs,{},image,error,32*1024*1024,&report));
    assert(report.committed && report.resolvedImports==5 && report.validatedRelocations==5);
    const auto* got=image.translate(0x402000,40,6);assert(got);
    for(size_t i=0;i<5;++i)assert(maxps4::OrbisMetadata::read(got+8*i,8)==Audio::bias+metadata.symbols[i+1].value);
    inputs.pop_back();assert(!Link::link(inputs,{},image,error) && error.find(Audio::path)!=std::string::npos);
    inputs.push_back({{{Audio::path,{}},provider.data(),provider.size()},Audio::bias});
    orbis_fixture::setTag(app,0x61000015,library|(uint64_t{2}<<32)|(uint64_t{1}<<48));
    assert(!Link::link(inputs,{},image,error) && error.find("incompatible")!=std::string::npos);
    orbis_fixture::setTag(app,0x61000015,library|(uint64_t{1}<<32)|(uint64_t{1}<<48));
    const auto firstName=maxps4::OrbisMetadata::read(app.data()+0x818,4);
    app[0x600+firstName]='X';assert(!Link::link(inputs,{},image,error) && error.find("unresolved NID")!=std::string::npos);
    const auto common=maxps4::OrbisV4CommonDialog::provider();
    auto caller=compat_fixture::consumer("libSceCommonDialog",maxps4::OrbisV4CommonDialog::path,{"uoUpLGNkygk"});
    assert(metadata.parse(common.data(),common.size()) && metadata.symbols.size()==2);
    inputs={{{{"common-app.prx",{maxps4::OrbisV4CommonDialog::path}},caller.data(),caller.size()},0x400000},
            {{{maxps4::OrbisV4CommonDialog::path,{}},common.data(),common.size()},maxps4::OrbisV4CommonDialog::bias}};
    assert(Link::link(inputs,{},image,error,32*1024*1024,&report));
    assert(report.resolvedImports==1 && report.validatedRelocations==1);
    std::cout<<"PASS AudioOut five real imports/GOT relocations and CommonDialog Initialize; version/unknown-NID/missing-provider refusals\n";
}
