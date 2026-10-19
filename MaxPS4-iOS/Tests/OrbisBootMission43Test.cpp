#include "../Native/OrbisV4ExperimentalSELF.hpp"
#include "OrbisBootMission43Fixture.h"
#include <cassert>
#include <iostream>
using namespace maxps4;
using orbis_fixture::put;
static void tls(std::vector<uint8_t>& elf,size_t initial,size_t total,size_t alignment) {
    put(elf,56,5,2);
    const size_t p=64+4*56;
    put(elf,p,7,4);put(elf,p+4,4,4);put(elf,p+8,initial ? 0x300 : 0);
    put(elf,p+16,0x2000);put(elf,p+32,initial);
    put(elf,p+40,total);put(elf,p+48,alignment);
    put(elf,64+56+32,std::max<size_t>(0x100,initial));
    put(elf,64+56+40,std::max<size_t>(0x100,total));
}
static void tlsRelocations(std::vector<uint8_t>& elf,size_t count) {
    orbis_fixture::appendTag(elf,0x6100002f,0x300);
    orbis_fixture::appendTag(elf,0x61000031,count*24);
    orbis_fixture::appendTag(elf,0x61000033,24);
    for(size_t i=0;i<count;++i) {put(elf,0x900+i*24,0x2000+i*8);put(elf,0x908+i*24,16);}
}
int main() {
    using Policy=OrbisMetadata::SelfInspection;
    auto app=orbis_fixture::make(false),libc=orbis_fixture::make(true),fios=libc;
    // Shape observed in the supplied files; all payloads/NIDs remain fabricated.
    tls(app,0,0,1);tls(libc,416,1168,16);tls(fios,0,64,8);
    orbis_fixture::appendTag(app,12,0x1000);orbis_fixture::appendTag(app,13,0x1001);
    tlsRelocations(libc,13);tlsRelocations(fios,3);
    std::vector<std::vector<uint8_t>> images{app,libc,fios},canonical(3);
    const std::vector<std::string> names{"eboot.bin","libc.prx","libSceFios2.prx"};
    std::vector<OrbisV4DependencyDiagnostic::Input> audit;
    for(size_t i=0;i<images.size();++i) {
        const auto self=mission43_fixture::wrap(images[i]);
        OrbisMetadata metadata;
        assert(!metadata.parse(self.data(),self.size())); // strict unchanged
        const bool parsed=metadata.parse(self.data(),self.size(),{},Policy::ClearUnauthenticated);
        if (!parsed) std::cerr<<names[i]<<": "<<metadata.error<<'\n';
        assert(parsed);
        assert(metadata.inspectedClearSelf && !metadata.clearSelf);
        assert(metadata.report().find("signature non vérifiée")!=std::string::npos);
        std::string reason;
        assert(OrbisClearImage::normalize(self.data(),self.size(),canonical[i],reason)==0);
        assert(self==mission43_fixture::wrap(images[i]));
        audit.push_back({names[i],canonical[i].data(),canonical[i].size()});
        for(const uint8_t protection:{uint8_t(2),uint8_t(8)}) {
            auto bad=self;bad[32]|=protection;
            assert(!metadata.parse(bad.data(),bad.size(),{},Policy::ClearUnauthenticated));
            assert(metadata.segments.empty() && metadata.symbols.empty());
            assert(OrbisClearImage::normalize(bad.data(),bad.size(),canonical[i],reason)==-ENOTSUP && canonical[i].empty());
        }
        assert(OrbisClearImage::normalize(self.data(),self.size(),canonical[i],reason)==0);
        audit.back().bytes=canonical[i].data();
    }
    const auto diagnostic=OrbisV4DependencyDiagnostic::inspect(audit).text;
    assert(diagnostic.find("initial_bytes=416 total_bytes=1168")!=std::string::npos);
    assert(diagnostic.find("initial_bytes=0 total_bytes=64")!=std::string::npos);
    assert(diagnostic.find("type=16 count=13")!=std::string::npos);
    assert(diagnostic.find("type=16 count=3")!=std::string::npos);
    assert(diagnostic.find("feature=module-initializers")!=std::string::npos);
    assert(diagnostic.find("admission=false")!=std::string::npos);
    // A compatible explicit mixed SELF/ELF graph used to fail "SELF required".
    auto consumer=orbis_fixture::make(false),provider=orbis_fixture::make(true);
    const std::string dependency="libc.prx";
    std::copy(dependency.begin(),dependency.end(),consumer.begin()+0x6a0);
    orbis_fixture::setTag(consumer,0x61000037,0xb0);
    orbis_fixture::appendTag(consumer,1,0xa0);
    const auto wrapped=mission43_fixture::wrap(consumer);
    std::unique_ptr<OrbisV4Translation> owner;std::string report;
    auto prepare=[&](const std::vector<uint8_t>& module) {
        return OrbisV4ExperimentalSELF::prepare({{"eboot.bin",wrapped.data(),wrapped.size(),0x400000},
            {dependency,module.data(),module.size(),0x800000}},[]{},owner,report);
    };
    assert(prepare(provider)==0 && owner);
    uint64_t got=0;
    assert(owner->read(0x402000,&got,8)==0 && got==0x801000);
    {auto lease=owner->lease();assert(lease.permitted(0x401000,1,5));assert(!lease.permitted(0x401000,1,2));}
    owner->close();owner.reset();
    // Real TLS remains a transaction refusal; no guessed module IDs or offsets.
    assert(prepare(libc)==-ENOTSUP && !owner && report.find("TLS not supported")!=std::string::npos);
    provider[9]=1;
    assert(prepare(provider)==-ENOTSUP && !owner); // reserved ELF identity byte
    std::cout<<"PASS mission43 boot: dynamic signed-clear SELF, TLS 416/1168 and 0/64, 13+3 type16, init/fini; compatible mixed graph relocated; protected/malformed/TLS refused; no CPU dispatch\n";
}
