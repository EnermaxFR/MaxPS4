#include "../Native/OrbisV4ProviderFunctions.hpp"
#include "../Native/OrbisLinker.hpp"
#include "OrbisV4CompatibilityFixture.h"
#include <cassert>
#include <iostream>
int main(){
    using T=maxps4::OrbisV4NpTrophy;using Link=maxps4::OrbisLinker;using Audit=maxps4::OrbisV4DependencyDiagnostic;
    const auto provider=T::provider();maxps4::OrbisMetadata metadata;
    assert(metadata.parse(provider.data(),provider.size()) && metadata.symbols.size()==9);
    assert(metadata.modules[0].exported && metadata.modules[0].name=="libSceNpTrophy" && metadata.modules[0].version==0x101);
    assert(metadata.libraries[0].exported && metadata.libraries[0].name=="libSceNpTrophy" && metadata.libraries[0].version==1);
    for(size_t first=0;first<8;first+=4){
        auto app=compat_fixture::consumer("libSceNpTrophy",T::path,{T::nids.begin()+first,T::nids.begin()+first+4});
        std::vector<Link::Input> inputs{
            {{{"trophy-app.prx",{T::path}},app.data(),app.size()},0x400000},
            {{{T::path,{}},provider.data(),provider.size()},T::bias}};
        Link::Image image;Link::Report report;std::string error;
        assert(Link::link(inputs,{},image,error,32*1024*1024,&report));
        assert(report.committed && report.resolvedImports==4 && report.validatedRelocations==4);
        const auto* got=image.translate(0x402000,32,6);assert(got);
        for(size_t i=0;i<4;++i)assert(maxps4::OrbisMetadata::read(got+8*i,8)==T::bias+metadata.symbols[first+i+1].value);
        auto audit=Audit::inspect({{"trophy-app.prx",app.data(),app.size()},
            {T::path,provider.data(),provider.size(),Audit::Coverage::Partial,maxps4::OrbisV4ProviderFunctions::get(T::path)}});
        assert(audit.matched==4 && audit.refused==4 && !audit.implemented && !audit.partial && !audit.unresolved);
        inputs.pop_back();assert(!Link::link(inputs,{},image,error) && error.find(T::path)!=std::string::npos);
        inputs.push_back({{{T::path,{}},provider.data(),provider.size()},T::bias});
        orbis_fixture::setTag(app,0x61000015,1|(uint64_t{2}<<32)|(uint64_t{1}<<48));
        assert(!Link::link(inputs,{},image,error) && error.find("incompatible")!=std::string::npos);
    }
    const auto app=compat_fixture::consumer("libSceNpTrophy",T::path,{"cqGkYAN-gRw"}); // CaptureScreenshot, not exported.
    Link::Image image;std::string error;
    assert(!Link::link({{{{"trophy-app.prx",{T::path}},app.data(),app.size()},0x400000},
                       {{{T::path,{}},provider.data(),provider.size()},T::bias}}, {},image,error));
    assert(error.find("unresolved NID")!=std::string::npos);
    std::cout<<"PASS NpTrophy eight imports/GOT: exact versions, explicit refusals distinct from functionality, missing/version/unsupported-export refusals\n";
}
