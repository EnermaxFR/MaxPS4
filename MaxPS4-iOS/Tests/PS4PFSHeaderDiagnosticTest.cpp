// SPDX-License-Identifier: GPL-2.0-only
#include "../Native/PS4PFSHeaderDiagnostic.hpp"
#include "../Native/PS4PKGBridge.h"
#include <cassert>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
using Diagnostic = maxps4::PS4PFSHeaderDiagnostic;
static void put(std::vector<uint8_t>& b,size_t at,uint64_t v,size_t width,bool little=false) {
    for (size_t i=0;i<width;++i) b[at+(little?i:width-1-i)] = uint8_t(v>>(8*i));
}
static std::vector<uint8_t> fixture(uint16_t mode) {
    // Original header-only synthetic fixture. Not a mountable filesystem.
    std::vector<uint8_t> b(0x700,0);
    std::memcpy(b.data(),"\177CNT",4);
    put(b,0x10,1,4); put(b,0x16,1,2); put(b,0x18,0x5a0,4);
    put(b,0x410,0x600,8); put(b,0x418,0x100,8); put(b,0x430,b.size(),8);
    put(b,0x5a0,0x409,4); // Unknown empty descriptor, never a filesystem file.
    put(b,0x600,1,8,true); put(b,0x608,20130315,8,true); put(b,0x61c,mode,2,true);
    std::fill(b.begin()+0x648,b.end(),0xa5); // Guard payload: must never be read.
    return b;
}
int main(int argc,char** argv) {
    auto b=fixture(12);
    size_t calls=0,bytes=0;
    bool guard=false;
    const auto read=[&](uint64_t at,uint8_t* out,size_t count) {
        ++calls; bytes+=count;
        assert(maxps4::PS4PKGInventory::range(at,count,b.size()));
        if (guard) assert(at==0x600 && count==72);
        std::memcpy(out,b.data()+at,count); return true;
    };
    maxps4::PS4PKGInventory pkg;
    assert(pkg.parse(b.size(),read));
    Diagnostic pfs;
    const auto inspect=[&]() { calls=bytes=0; guard=true; pfs.inspect(pkg,read); };
    inspect(); assert(pfs.status==Diagnostic::Status::encrypted && calls==1 && bytes==72);
    for (uint16_t mode : {uint16_t(8),uint16_t(9),uint16_t(10),uint16_t(0x108),uint16_t(0)}) {
        put(b,0x61c,mode,2,true); inspect();
        assert(pfs.status==Diagnostic::Status::unsupported && calls==1 && bytes==72);
    }
    put(b,0x608,20130305,8,true); inspect();
    assert(pfs.status==Diagnostic::Status::unknown && pfs.mode==0 && bytes==72);
    b=fixture(12); put(b,0x600,2,8,true); inspect(); assert(pfs.status==Diagnostic::Status::unknown);
    b=fixture(12);
    const auto original=pkg;
    for (uint64_t size : {uint64_t(1),uint64_t(71),UINT64_MAX}) {
        pkg=original; pkg.pfsSize=size; inspect(); assert(pfs.status==Diagnostic::Status::malformed && calls==0);
    }
    for (uint64_t offset : {uint64_t(0),uint64_t(0x5a0),UINT64_MAX}) {
        pkg=original; pkg.pfsOffset=offset; inspect(); assert(pfs.status==Diagnostic::Status::malformed && calls==0);
    }
    pkg=original; pkg.entries[0].offset=0x600; pkg.entries[0].size=1;
    inspect(); assert(pfs.status==Diagnostic::Status::malformed && calls==0);
    pkg=original; pkg.entries[0].offset=0xffffffffU; pkg.entries[0].size=1;
    inspect(); assert(pfs.status==Diagnostic::Status::malformed && calls==0);
    pkg=original; pkg.pfsSize=0; inspect(); assert(pfs.status==Diagnostic::Status::absent && calls==0);
    pkg=original; pkg.error="invalid"; inspect(); assert(pfs.status==Diagnostic::Status::malformed && calls==0);
    pkg=original;
    pfs.inspect(pkg,[](uint64_t,uint8_t*,size_t){return false;});
    assert(pfs.status==Diagnostic::Status::malformed && pfs.bytesRead==72);
    // Real report ABI + output truncation + source symlink/FIFO refusal.
    const auto temp=std::filesystem::temp_directory_path()/("maxps4-header-"+std::to_string(getpid()));
    std::filesystem::create_directory(temp);
    const auto source=temp/"header.pkg";
    const auto write=[&]() { std::ofstream f(source,std::ios::binary); f.write(reinterpret_cast<const char*>(b.data()),b.size()); assert(f.good()); };
    b=fixture(12); write();
    std::vector<char> report(4*1024*1024);
    assert(maxps4_pkg_inventory_report(source.c_str(),report.data(),report.size())==0);
    assert(std::strstr(report.data(),"PFS chiffré déclaré : inaccessible"));
    assert(std::strstr(report.data(),"eboot.bin demeure inaccessible"));
    char tiny[8]{}; assert(maxps4_pkg_inventory_report(source.c_str(),tiny,sizeof(tiny))==-ENOSPC && tiny[7]==0);
    const auto link=temp/"link.pkg"; std::filesystem::create_symlink(source,link);
    assert(maxps4_pkg_inventory_report(link.c_str(),report.data(),report.size())<0);
    const auto fifo=temp/"fifo.pkg"; assert(mkfifo(fifo.c_str(),0600)==0);
    assert(maxps4_pkg_inventory_report(fifo.c_str(),report.data(),report.size())==-EINVAL);
    if (argc>1) {
        const std::filesystem::path artifacts(argv[1]); std::filesystem::create_directories(artifacts);
        for (const auto* name : {"pfs-header-encrypted.pkg","pfs-header-plain.pkg","pfs-header-unknown.pkg"}) {
            b=fixture(std::string(name)=="pfs-header-encrypted.pkg"?12:8);
            if (std::string(name)=="pfs-header-unknown.pkg") put(b,0x608,0,8,true);
            std::ofstream f(artifacts/name,std::ios::binary); f.write(reinterpret_cast<const char*>(b.data()),b.size()); assert(f.good());
        }
    }
    std::filesystem::remove_all(temp);
    std::cout << "PASS PFS header diagnostic: 72-byte maximum, encrypted/plain/unknown/absent, bounds/overlaps/I/O, C report, symlink/FIFO refusal; no payload or execution\n";
}
