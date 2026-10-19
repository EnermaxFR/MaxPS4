#include "../Native/OrbisV4DependencyDiagnostic.hpp"
#include "../Native/OrbisV4NpCommon.hpp"
#include "OrbisV4CompatibilityFixture.h"
#include <cassert>
#include <iostream>
int main(){
    using Audit=maxps4::OrbisV4DependencyDiagnostic;using NP=maxps4::OrbisV4NpCommon;
    auto provider=NP::provider();auto app=compat_fixture::consumer("libSceNpCommon",NP::path,{NP::nids.begin(),NP::nids.end()});
    std::vector<Audit::Input> inputs{{"app",app.data(),app.size()},
        {NP::path,provider.data(),provider.size(),Audit::Coverage::Partial}};
    auto inspect=[&]{inputs[0].bytes=app.data();return Audit::inspect(inputs);};
    auto result=inspect();assert(result.matched==2 && !result.unresolved && !result.missing && !result.descriptors);
    assert(result.text.find("coverage=partial")!=std::string::npos && result.text.find("coverage=unverified")!=std::string::npos);
    assert(result.text.find("admission=false")!=std::string::npos);
    inputs[1].coverage=Audit::Coverage::Complete;assert(inspect().text.find("complete-declared")!=std::string::npos);
    // Initializers are inventoried for every module independently of imports.
    orbis_fixture::appendTag(app,12,0x1000);
    result=inspect();assert(result.matched==2 && result.text.find("feature=module-initializers")!=std::string::npos);
    assert(result.text.find("init=4096 fini=0 admission=false")!=std::string::npos);
    orbis_fixture::setTag(app,12,0);
    // Both missing dependencies are listed despite an otherwise matchable import.
    for(size_t i=0;i<2;++i){const std::string name=i ? "missing-b.prx" : "missing-a.prx";
        std::copy(name.begin(),name.end(),app.begin()+0x780+i*32);orbis_fixture::appendTag(app,1,0x180+i*32);}
    orbis_fixture::setTag(app,0x61000037,0x1c0);
    result=inspect();assert(result.missing==2 && result.matched==2);
    assert(result.text.find("missing-a.prx")!=std::string::npos && result.text.find("missing-b.prx")!=std::string::npos);
    // Exact version, type and declaration checks, not bare-NID guesses.
    orbis_fixture::setTag(app,0x6100000f,1|(uint64_t{0x102}<<32)|(uint64_t{1}<<48));
    result=inspect();assert(result.unresolved==2 && result.descriptors && result.text.find("version-or-type-mismatch")!=std::string::npos);
    orbis_fixture::setTag(app,0x6100000f,1|(uint64_t{0x101}<<32)|(uint64_t{1}<<48));
    app[0x81c]=0x11;result=inspect();assert(result.unresolved==1);app[0x81c]=0x12;
    inputs.push_back(inputs[1]);result=inspect();assert(result.duplicates==1 && result.unresolved==2 && result.text.find("ambiguous-export")!=std::string::npos);
    inputs.pop_back();inputs[1].name="renamed.prx";result=inspect();assert(result.missing==3 && result.unresolved==2 && result.text.find("undeclared-provider")!=std::string::npos);
    inputs[1].name=NP::path;
    std::vector<uint8_t> bad(64);inputs.push_back({"invalid",bad.data(),bad.size()});
    result=inspect();assert(result.invalid==1 && result.missing==2 && result.matched==2);inputs.pop_back();
    // Cycle detection includes dependants without claiming to enumerate SCCs.
    auto a=provider,b=provider;
    auto need=[](std::vector<uint8_t>& bytes,const std::string& name){
        std::copy(name.begin(),name.end(),bytes.begin()+0xa00);
        for(size_t p=0x700;p<0x880;p+=16){const auto tag=maxps4::OrbisMetadata::read(bytes.data()+p,8);
            if(tag==0x61000037)orbis_fixture::put(bytes,p+8,0x120);
            if(!tag){orbis_fixture::put(bytes,p,1);orbis_fixture::put(bytes,p+8,0x100);break;}}
    };
    need(a,"b.prx");need(b,"a.prx");
    result=Audit::inspect({{"a.prx",a.data(),a.size()},{"b.prx",b.data(),b.size()}});
    assert(result.cycles==2 && !result.missing);
    assert(Audit::inspect({}).invalid==1);
    std::vector<Audit::Input> excessive(129);assert(Audit::inspect(excessive).invalid==1);
    std::cout<<"PASS global read-only diagnostic: all missing deps/imports, exact identities/versions/types/declarations, partial/complete-declared/unknown, invalid metadata, duplicates and cycles\n";
}
