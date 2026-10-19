#include "../Native/OrbisV4TLSRelocations.hpp"
#include "OrbisTLSMission44Fixture.h"
#include "OrbisBootMission43Fixture.h"
#include <cassert>
#include <iostream>
int main() {
    using namespace maxps4;using TLS=OrbisV4TLSRelocations;using orbis_fixture::put;
    const auto main=mission44_fixture::module(0,0,1,0);
    auto libc=mission44_fixture::module(416,1168,16,13),fios=mission44_fixture::module(0,64,8,3);
    const auto original=libc;
    TLS::Plan plan;std::string reason;
    auto prepare=[&]{return TLS::prepare({{"eboot.bin",main.data(),main.size(),1},
        {"libc.prx",libc.data(),libc.size(),2},{"libSceFios2.prx",fios.data(),fios.size(),3}},plan,reason);};
    assert(prepare()==0 && plan.relocated==16 && plan.templates.size()==2 && plan.total==1232);
    assert(plan.placements[0].id==2 && plan.placements[0].offset==0);
    assert(plan.placements[1].id==3 && plan.placements[1].offset==1168);
    assert(libc==original);
    for(size_t i=1;i<3;++i)for(const auto& patch:plan.modules[i].patches)
        assert(OrbisMetadata::read(plan.modules[i].elf.data()+patch.fileOffset,8)==i+1);
    // Original relocation records remain present; this is not complete linking.
    assert(OrbisMetadata::read(plan.modules[1].elf.data()+0x908,8)==((uint64_t{1}<<32)|16));
    const auto saved=plan.modules[1].elf;
    auto reject=[&](int expected){assert(prepare()==expected);assert(plan.modules[1].elf==saved && plan.relocated==16);};
    put(libc,0x910,1);reject(-ENOTSUP);libc=original;
    libc[0x81c]=0x13;reject(-ENOTSUP);libc=original; // nonlocal marker
    put(libc,0x908,(uint64_t{1}<<32)|17);reject(-ENOTSUP);libc=original;
    put(libc,0x900,0x2001);reject(-ENOEXEC);libc=original;
    put(libc,0x900,0x1000);reject(-ENOEXEC);libc=original; // RX
    put(libc,64+4*56+48,24);reject(-ENOEXEC);libc=original;
    assert(TLS::prepare({{"libc",libc.data(),libc.size(),2},{"fios",fios.data(),fios.size(),2}},plan,reason)==-EINVAL);
    assert(plan.modules[1].elf==saved);
    auto signedClear=mission43_fixture::wrap(libc);
    TLS::Plan selfPlan;
    assert(TLS::prepare({{"libc.prx",signedClear.data(),signedClear.size(),2}},selfPlan,reason)==0 && selfPlan.relocated==13);
    const auto preservedSELF=selfPlan.modules[0].elf;
    signedClear[32]|=2;
    assert(TLS::prepare({{"libc.prx",signedClear.data(),signedClear.size(),2}},selfPlan,reason)==-ENOTSUP);
    assert(selfPlan.modules[0].elf==preservedSELF && reason.find("SELF.ENCRYPTED")!=std::string::npos);
    put(libc,0x908,(uint64_t{1}<<32)|36);reject(-ENOTSUP);libc=original;
    put(libc,0x900,0x4000);reject(-ENOTSUP);libc=original; // template must not precede its own relocation
    OrbisV4Translation first([]{}),second([]{});
    OrbisV4TLSMulti::Thread a,b;
    assert(OrbisV4TLSMulti::initialize(first,plan.templates,a)==0);
    assert(OrbisV4TLSMulti::initialize(second,plan.templates,b)==0);
    uint64_t address=0;
    {
        auto lease=first.lease();
        assert(TLS::resolve(lease,a,2,0,4,address)==0 && address%16==0);
        uint32_t initial=0;assert(lease.read_data(address,&initial,4)==0 && initial==42);
        uint64_t preserve=123;
        assert(TLS::resolve(lease,a,3,63,2,preserve)==-EFAULT && preserve==123);
        assert(TLS::resolve(lease,a,4,0,1,preserve)==-ENOENT && preserve==123);
        assert(TLS::resolve(lease,a,2,SIZE_MAX,1,preserve)==-EFAULT);
        auto forged=a;forged.placements[0].offset=SIZE_MAX;
        assert(TLS::resolve(lease,forged,2,0,1,preserve)==-EFAULT);
    }
    const uint32_t mutation=99;assert(first.write_span(address,&mutation,4)==0);
    {auto lease=second.lease();uint64_t other=0;assert(TLS::resolve(lease,b,2,0,4,other)==0);
     uint32_t preserved=0;assert(lease.read_data(other,&preserved,4)==0 && preserved==42);}
    first.close();second.close();
    bool closed=false;
    try { auto lease=first.lease(); (void)lease; }
    catch(const std::logic_error&) {closed=true;}
    assert(closed);
    std::cout<<"PASS 16 local DTPMOD64 patches, exact TLS sizes/align 8/16, retained RELA, transactional refusals, per-thread guest resolution, bounds and closed storage\n";
}
