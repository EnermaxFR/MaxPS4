#include "../Native/OrbisV4ProviderFunctions.hpp"
#include "../Native/OrbisLinker.hpp"
#include "OrbisV4CompatibilityFixture.h"
#include <cassert>
#include <iostream>
int main(){
    using Link=maxps4::OrbisLinker;using Audit=maxps4::OrbisV4DependencyDiagnostic;
    for(bool libc:{false,true}){
        const auto provider=libc ? maxps4::OrbisV4LibcInternal::provider() : maxps4::OrbisV4NpManager::provider();
        const std::string path=libc ? maxps4::OrbisV4LibcInternal::path : maxps4::OrbisV4NpManager::path;
        const std::string library=libc ? "libSceLibcInternal" : "libSceNpManager";
        const uint64_t bias=libc ? maxps4::OrbisV4LibcInternal::bias : maxps4::OrbisV4NpManager::bias;
        const std::vector<std::string> nids=libc ? std::vector<std::string>{maxps4::OrbisV4LibcInternal::nids.begin(),maxps4::OrbisV4LibcInternal::nids.end()} :
            std::vector<std::string>{maxps4::OrbisV4NpManager::nids.begin(),maxps4::OrbisV4NpManager::nids.end()};
        maxps4::OrbisMetadata m;assert(m.parse(provider.data(),provider.size()) && m.symbols.size()==nids.size()+1);
        assert(m.modules[0].exported && m.modules[0].name==library && m.modules[0].version==0x101);
        assert(m.libraries[0].exported && m.libraries[0].name==library && m.libraries[0].version==1);
        auto app=compat_fixture::consumer(library,path,nids);
        std::vector<Link::Input> inputs{
            {{{"app.prx",{path}},app.data(),app.size()},0x400000},
            {{{path,{}},provider.data(),provider.size()},bias}};
        Link::Image image;Link::Report report;std::string error;
        assert(Link::link(inputs,{},image,error,32*1024*1024,&report));
        assert(report.committed && report.resolvedImports==nids.size() && report.validatedRelocations==nids.size());
        const auto* got=image.translate(0x402000,8*nids.size(),6);assert(got);
        for(size_t i=0;i<nids.size();++i)assert(maxps4::OrbisMetadata::read(got+8*i,8)==bias+m.symbols[i+1].value);
        std::vector<Audit::Input> audit{{"app.prx",app.data(),app.size()},
            {path,provider.data(),provider.size(),Audit::Coverage::Partial,maxps4::OrbisV4ProviderFunctions::get(path)}};
        auto result=Audit::inspect(audit);
        assert(!result.unresolved && result.matched==nids.size());
        assert(result.implemented==(libc ? 2U : 1U) && result.refused==(libc ? 0U : 2U));
        audit[1].functions.clear();result=Audit::inspect(audit);
        assert(result.unverified==nids.size() && !result.implemented && !result.refused);
        inputs.pop_back();assert(!Link::link(inputs,{},image,error) && error.find(path)!=std::string::npos);
        inputs.push_back({{{path,{}},provider.data(),provider.size()},bias});
        orbis_fixture::setTag(app,0x61000015,1|(uint64_t{2}<<32)|(uint64_t{1}<<48));
        assert(!Link::link(inputs,{},image,error) && error.find("incompatible")!=std::string::npos);
        const std::string unsupported=libc ? "gQX+4GDQjpM" : "GpLQDNKICac"; // malloc / CreateRequest
        auto unknown=compat_fixture::consumer(library,path,{unsupported});
        audit[0]={"app.prx",unknown.data(),unknown.size()};
        // Merely declaring a behavioral contract must not synthesize an export.
        audit[1].functions={{unsupported,library,library,"not-implemented",1,0x101,2,Audit::Behavior::ExplicitRefusal}};
        result=Audit::inspect(audit);assert(result.unresolved==1 && !result.matched);
        assert(!Link::link({{{{"app.prx",{path}},unknown.data(),unknown.size()},0x400000},
                           {{{path,{}},provider.data(),provider.size()},bias}}, {},image,error));
        assert(error.find("unresolved NID")!=std::string::npos);
    }
    std::cout<<"PASS NpManager/LibcInternal: exact exports/GOT, version/missing/unsupported refusals, implemented vs refusal vs unknown, contracts cannot resolve symbols\n";
}
