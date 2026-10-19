#include "../Native/OrbisV4ExperimentalSELF.hpp"
#include "OrbisLinkerFixture.h"
#include <cassert>
#include <iostream>
#include "OrbisBootMission43Fixture.h"
using mission43_fixture::wrap;
int main() {
    auto elf=orbis_fixture::make(true);
    // Distinct 16 KiB RX/RELRO pages also work on 4 KiB Linux hosts.
    orbis_fixture::put(elf,24,0x4000);
    orbis_fixture::put(elf,64+16,0x4000);
    orbis_fixture::put(elf,64+56+16,0x8000);
    orbis_fixture::put(elf,0x820,0x4000);
    orbis_fixture::put(elf,64+56,0x61000010,4);orbis_fixture::put(elf,64+56+4,4,4);
    orbis_fixture::appendTag(elf,0x6100002f,0x300);
    orbis_fixture::appendTag(elf,0x61000031,24);orbis_fixture::appendTag(elf,0x61000033,24);
    orbis_fixture::put(elf,0x900,0x8000);orbis_fixture::put(elf,0x908,8);orbis_fixture::put(elf,0x910,0x123);
    const auto original=wrap(elf);auto bytes=original;
    maxps4::OrbisMetadata inspection;
    assert(inspection.parse(bytes.data(),bytes.size(),{},maxps4::OrbisMetadata::SelfInspection::ClearUnauthenticated));
    assert(inspection.segments[1].type==0x61000010 && inspection.segments[1].flags==4);
    auto ordinaryReadOnly=elf;
    orbis_fixture::put(ordinaryReadOnly,64+56,1,4);
    const auto ordinaryReadOnlySELF=wrap(ordinaryReadOnly);
    assert(!inspection.parse(ordinaryReadOnlySELF.data(),ordinaryReadOnlySELF.size(),{},
        maxps4::OrbisMetadata::SelfInspection::ClearUnauthenticated));
    assert(inspection.error.find("Cible RELA hors segment writable")!=std::string::npos);
    // Read-only inspection preserves source RELRO flags; preparation may use
    // temporary writable vectors, then seals the published guest region below.
    std::unique_ptr<maxps4::OrbisV4Translation> owner;std::string report;
    auto prepare=[&] {return maxps4::OrbisV4ExperimentalSELF::prepare({{"probe.prx",bytes.data(),bytes.size(),0x400000}},[]{},owner,report);};
    assert(prepare()==0 && owner);
    uint64_t relocated=0;assert(owner->read(0x408000,&relocated,8)==0 && relocated==0x400123);
    {auto lease=owner->lease();assert(!lease.permitted(0x408000,8,2));assert(!lease.permitted(0x408000,8,1));
     for (const auto& r:lease.ranges()) assert(r.host!=r.guest);}
    owner->close();owner.reset();
    std::vector<uint8_t> canonical;maxps4_v4_prx_request request{};
    assert(maxps4::OrbisV4ExperimentalSELF::admit(bytes.data(),bytes.size(),canonical,request,report)==-ENOTSUP);
    assert(!request.file && report.find("dynamic process startup")!=std::string::npos);
    // Source bytes and original RELRO flags remain untouched.
    assert(bytes==original);
    for (uint8_t bit:{uint8_t(2),uint8_t(8)}) {
        bytes=original;bytes[32]|=bit;
        assert(prepare()==-ENOTSUP && !owner);
    }
    auto missing=elf;
    const char dependency[]="missing.prx";
    std::copy(std::begin(dependency),std::end(dependency),missing.begin()+0x6a0);
    orbis_fixture::setTag(missing,0x61000037,0xb0);orbis_fixture::appendTag(missing,1,0xa0);
    bytes=wrap(missing);assert(prepare()==-ENOTSUP && !owner && report.find("missing.prx")!=std::string::npos);
    auto wx=elf;orbis_fixture::put(wx,64+56+4,5,4);bytes=wrap(wx);
    assert(prepare()==-ENOTSUP && !owner); // RELRO cannot become executable.
    bytes=original; orbis_fixture::put(bytes,32+2*32,0x200000);
    assert(prepare()==-ENOTSUP && !owner && report.find("unbacked file bytes")!=std::string::npos);
    bytes=original; const size_t origin=32+4*32;
    orbis_fixture::put(bytes,origin+64+3*56+8,0x200);
    assert(prepare()==-ENOTSUP && !owner && report.find("conflicting logical")!=std::string::npos);
    auto alias=elf; orbis_fixture::put(alias,64+8,0); bytes=wrap(alias);
    assert(prepare()==-ENOTSUP && !owner && report.find("aliases loadable bytes")!=std::string::npos);
    bytes=original;bytes.pop_back();assert(prepare()==-ENOTSUP && !owner);
    bytes=original;
    assert(maxps4::OrbisV4ExperimentalSELF::prepare({{"probe.prx",bytes.data(),bytes.size(),0x400000}},[]{},owner,report,true,{0,102,0,0})==0);
    assert(report.find("USER_DEFINED_PARAM_2=102")!=std::string::npos);
    {auto lease=owner->lease();const uint8_t* code=nullptr;
     assert(lease.resolve_contiguous(maxps4::OrbisV4AppContent::bias+0x4000,1,5,code)==0);
     assert(!lease.permitted(maxps4::OrbisV4AppContent::bias+0x4000,1,2));}
    owner->close();owner.reset();
    assert(maxps4::OrbisV4ExperimentalSELF::prepare({{maxps4::OrbisV4AppContent::path,bytes.data(),bytes.size(),0x400000}},[]{},owner,report,true)==-EINVAL);
    assert(!owner && report.find("conflicts")!=std::string::npos);
    assert(maxps4::OrbisV4ExperimentalSELF::prepare({{"probe.prx",bytes.data(),bytes.size(),0x400000}},[]{},owner,report,true,{0,102,0,0},true,true,true,true,true,true,true,true)==0);
    {auto lease=owner->lease();
     for(uint64_t bias:{maxps4::OrbisV4AudioOut::bias,maxps4::OrbisV4CommonDialog::bias,maxps4::OrbisV4ErrorDialog::bias,maxps4::OrbisV4GnmDriver::bias,maxps4::OrbisV4NpCommon::bias,maxps4::OrbisV4NpManager::bias,maxps4::OrbisV4LibcInternal::bias,maxps4::OrbisV4NpTrophy::bias}){
        const uint8_t* code=nullptr;assert(lease.resolve_contiguous(bias+0x4000,1,5,code)==0);
        assert(!lease.permitted(bias+0x4000,1,2));
        assert(lease.permitted(bias+0x8000,4,6) && !lease.permitted(bias+0x8000,4,1));}}
    owner->close();owner.reset();
    for(const char* path:{maxps4::OrbisV4AudioOut::path,maxps4::OrbisV4CommonDialog::path,maxps4::OrbisV4ErrorDialog::path,maxps4::OrbisV4GnmDriver::path,maxps4::OrbisV4NpCommon::path,maxps4::OrbisV4NpManager::path,maxps4::OrbisV4LibcInternal::path,maxps4::OrbisV4NpTrophy::path}){
        assert(maxps4::OrbisV4ExperimentalSELF::prepare({{path,bytes.data(),bytes.size(),0x400000}},[]{},owner,report,false,{},true,true,true,true,true,true,true,true)==-EINVAL);
        assert(!owner && report.find("conflicts")!=std::string::npos);
    }
    assert(maxps4::OrbisV4ExperimentalSELF::prepare({{"probe.prx",bytes.data(),bytes.size(),0x400000}},[]{},owner,report,false,{},false,false,false,false,false,false,false,false,true)==0);
    {auto lease=owner->lease();for(size_t i=0;i<maxps4::OrbisV4LocalServices::modules().size();++i){
        const uint64_t address=maxps4::OrbisV4LocalServices::bias(i)+0x4000;
        assert(lease.permitted(address,1,5) && !lease.permitted(address,1,2));}}
    owner->close();owner.reset();
    for(const auto& module:maxps4::OrbisV4LocalServices::modules()){
        assert(maxps4::OrbisV4ExperimentalSELF::prepare({{module.path(),bytes.data(),bytes.size(),0x400000}},[]{},owner,report,false,{},false,false,false,false,false,false,false,false,true)==-EINVAL && !owner);
    }
    assert(maxps4::OrbisV4ExperimentalSELF::prepare({{"probe.prx",bytes.data(),bytes.size(),maxps4::OrbisV4ErrorDialog::bias}},[]{},owner,report,false,{},false,false,true)==-ENOTSUP && !owner);
    assert(maxps4::OrbisV4ExperimentalSELF::prepare({{"probe.prx",bytes.data(),bytes.size(),maxps4::OrbisV4GnmDriver::bias}},[]{},owner,report,false,{},false,false,false,true)==-ENOTSUP && !owner);
    std::vector<maxps4::OrbisV4ExperimentalSELF::Input> tooMany(120,{"probe.prx",bytes.data(),bytes.size(),0x400000});
    assert(maxps4::OrbisV4ExperimentalSELF::prepare(tooMany,[]{},owner,report,true,{},true,true,true,true,true,true,true,true)==-EINVAL && !owner);
    std::cout<<"PASS experimental signed clear SELF: actual RELATIVE relocation, final read-only NX RELRO, guest/host separation, no signature gate, missing-provider/crypto/malformed refusals and no dynamic CPU ABI\n";
}
