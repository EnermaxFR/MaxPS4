#include "../Native/OrbisV4Eboot.hpp"
#include "PS4ExecutableFixture.h"
#include "OrbisV4ImportFixture.h"
#include <cassert>
#include <fstream>
#include <iostream>

int main(int argc,char** argv) {
    assert(argc==2);
    std::ifstream file(argv[1],std::ios::binary);
    const std::vector<uint8_t> original((std::istreambuf_iterator<char>(file)),{});
    assert(original.size()>0x8004);
    std::string report;
    maxps4_v4_prx_request request{};
    for (unsigned i=0;i<5;++i) {
        assert(maxps4::OrbisV4Eboot::preflight(original.data(),original.size(),report)==0);
        assert(report.find("guest_mapping_verified=true")!=std::string::npos);
        assert(report.find("CPU_executed=false")!=std::string::npos);
        assert(maxps4::OrbisV4Eboot::request(original.data(),original.size(),request,report)==0);
        assert(request.file==original.data() && request.entry==0x400000 && request.instruction_budget==64);
    }
    auto reject=[&](const std::vector<uint8_t>& bytes,const char* reason) {
        assert(maxps4::OrbisV4Eboot::preflight(bytes.data(),bytes.size(),report)!=0);
        if (report.find(reason)==std::string::npos) std::cerr<<report;
        assert(report.find(reason)!=std::string::npos);
        assert(report.find("guest_mapping_verified=true")==std::string::npos);
    };
    auto bad=original;
    auto put=[&](size_t offset,uint64_t value,size_t width=8) {
        guest_test::put(bad,value,offset,width);
    };
    std::memcpy(bad.data(),"\177CNT",4); reject(bad,"format=PKG/CNT"); bad=original;
    put(64+4,7,4); reject(bad,"V4.LINK"); bad=original; // W+X.
    put(24,0x410000); reject(bad,"EBOOT.IMAGE"); bad=original; // NX entry.
    put(64+56+16,0x400000); reject(bad,"EBOOT.IMAGE"); bad=original; // Overlap.
    put(64+16,UINT64_MAX-1); reject(bad,"EBOOT.IMAGE"); bad=original;
    put(56,3,2);
    const size_t tls=64+2*56;
    put(tls,7,4); put(tls+4,4,4); put(tls+8,0x8000); put(tls+16,0x410000);
    put(tls+32,4); put(tls+40,8); put(tls+48,4);
    reject(bad,"EBOOT.ABI"); bad=original; // No real PT_TLS contract.
    bad[0x4000]=0xeb; reject(bad,"V4.INSTRUCTION"); bad=original;
    // Valid file with instructions exceeding the finite 64-instruction budget.
    put(64+32,65); put(64+40,65);
    std::fill(bad.begin()+0x4000,bad.begin()+0x4040,0x90); bad[0x4040]=0xc3;
    reject(bad,"V4.BUDGET");
    auto self=plainSELF();
    // Reach the intended unsupported-instruction check on 16 KiB Apple hosts:
    // the generic staging fixture's 0x1000 entry is below one host page.
    guest_test::put(self,0x4000,96+24,8);
    guest_test::put(self,0x4000,96+64+16,8);
    reject(self,"V4.INSTRUCTION");
    self.push_back(0); reject(self,"SELF.TRAILER"); self=plainSELF();
    guest_test::put(self,0x100802,32,8); reject(self,"encrypted SELF block refused");
    self=plainSELF(); guest_test::put(self,0x100808,32,8); reject(self,"compressed SELF block unsupported");
    auto imported=v4_import_fixture::make();
    reject(imported,"EBOOT.ABI");
    assert(report.find("Import ")!=std::string::npos && report.find("RELA type=7")!=std::string::npos);
    for (size_t length=0;length<256;++length) {
        assert(maxps4::OrbisV4Eboot::preflight(original.data(),length,report)!=0);
        assert(report.find("guest_mapping_verified=true")==std::string::npos);
    }
    assert(maxps4::OrbisV4Eboot::request(nullptr,0,request,report)==-EINVAL && !request.file);
    // Limit checked before inspecting the supplied memory, no giant allocation.
    assert(maxps4::OrbisV4Eboot::request(original.data(),maxps4::OrbisV4Eboot::file_limit+1,request,report)==-EFBIG);
    assert(!request.file);
    assert(maxps4::OrbisV4Eboot::preflight(original.data(),original.size(),report)==0);
    std::cout<<"PASS separate eboot: ELF mapping/preflight, inventory, SELF/encrypted/PKG refusals, bounds, W^X/NX, collisions, cleanup/recreation; no CPU execution\n";
}
