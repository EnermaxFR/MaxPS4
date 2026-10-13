#include "../Native/PS4PFSReadOnly.hpp"
#include "../Native/PS4PKGBridge.h"
#include "PS4PFSFixture.hpp"
#include <cassert>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>
using PFS=maxps4::PS4PFSReadOnly;
int main(int argc,char** argv) {
    constexpr size_t bs=65536;
    auto good=pfsFixture::image(),b=good;
    size_t reads=0,bytes=0;
    const auto read=[&](uint64_t at,uint8_t* out,size_t count) {
        ++reads; bytes+=count;
        if (!maxps4::PS4PKGInventory::range(at,count,b.size())) return false;
        if (count) std::memcpy(out,b.data()+at,count);
        return true;
    };
    PFS pfs;
    assert(pfs.inspect(b.size(),read));
    assert(pfs.status==PFS::Status::accessible && pfs.files.size()==1 && pfs.files[0].path=="eboot.bin");
    std::vector<uint8_t> output; std::string why;
    assert(pfs.extract("eboot.bin",read,output,why) && std::string(output.begin(),output.end())=="hello");
    for (const auto* path:{"../eboot.bin","/eboot.bin","uroot/../eboot.bin","eboot.bin/","missing","keystone","sce_sys/license.dat"}) {
        const auto before=reads;
        assert(!pfs.extract(path,read,output,why) && output.empty() && reads==before);
    }
    const auto reject=[&](PFS::Status status) { assert(!pfs.inspect(b.size(),read) && pfs.status==status && pfs.files.empty()); };
    for (size_t n:{size_t(1),size_t(71),bs,2*bs,4*bs}) { b=good; b.resize(n); reject(PFS::Status::malformed); }
    b=good; pfsFixture::le(b,0x1c,0xc,2); reads=bytes=0; reject(PFS::Status::encrypted); assert(reads==1 && bytes==0x48);
    assert(!pfs.extract("eboot.bin",read,output,why));
    b=good; pfsFixture::le(b,8,20130305,8); reject(PFS::Status::unknown); // template typo is not accepted
    for (uint64_t mode:{uint64_t(9),uint64_t(10),uint64_t(0x108)}) { b=good; pfsFixture::le(b,0x1c,mode,2); reject(PFS::Status::unsupported); }
    for (size_t at:{size_t(0x20),size_t(0x30),size_t(0x38),size_t(0x40)}) { b=good; pfsFixture::le(b,at,UINT32_MAX,4); reject(PFS::Status::malformed); }
    b=good; pfsFixture::le(b,bs+0x64,1,4); reject(PFS::Status::malformed); // table alias
    b=good; pfsFixture::le(b,bs+2*0xa8+0x64,3,4); reject(PFS::Status::malformed); // overlap
    b=good; pfsFixture::le(b,bs+0x68,3,4); reject(PFS::Status::unsupported);
    b=good; pfsFixture::le(b,bs+0x94,4,4); reject(PFS::Status::unsupported);
    b=good; pfsFixture::le(b,bs+2*0xa8+4,1,4); reject(PFS::Status::unsupported);
    b=good; pfsFixture::le(b,bs+2*0xa8+8,UINT64_MAX,8); reject(PFS::Status::unsupported);
    b=good; pfsFixture::le(b,3*bs+12,16,4); reject(PFS::Status::malformed);
    b=good; pfsFixture::le(b,3*bs+8,UINT32_MAX,4); reject(PFS::Status::malformed);
    b=good; pfsFixture::le(b,3*bs,99,4); reject(PFS::Status::malformed);
    b=good; pfsFixture::le(b,2*bs,0,4); reject(PFS::Status::malformed); // cycle
    b=good; pfsFixture::le(b,3*bs+4,3,4); reject(PFS::Status::malformed); // inode kind mismatch
    for(const char* name:{"../escape","a/bescape","a\\bescape","/absolute","a:bescape"}) {
        b=good; std::memcpy(b.data()+3*bs+16,name,9); reject(PFS::Status::malformed);
    }
    b=good; b[3*bs+25]=1; reject(PFS::Status::malformed);
    b=good; b[3*bs+32]=1; reject(PFS::Status::malformed);
    // Empty uroot is valid; no executable invented.
    b=good; std::fill(b.begin()+3*bs,b.begin()+4*bs,0); assert(pfs.inspect(b.size(),read) && pfs.files.empty());
    b=good; std::memcpy(b.data()+2*bs+16,"other",5); reject(PFS::Status::malformed);
    b=good; std::memcpy(b.data()+3*bs+32,b.data()+3*bs,32); reject(PFS::Status::malformed); // duplicate
    b=good; b[3*bs+16]='\n'; reject(PFS::Status::malformed); // log injection
    // Large virtual images test budgets before any large payload allocation/read.
    b=good; pfsFixture::le(b,0x38,1030,8);
    pfsFixture::le(b,bs+2*0xa8+8,64*1024*1024+1,8);
    pfsFixture::le(b,bs+2*0xa8+0x10,64*1024*1024+1,8);
    pfsFixture::le(b,bs+2*0xa8+0x60,1025,4);
    assert(pfs.inspect(1030*bs,read));
    const auto beforeLarge=reads;
    assert(!pfs.extract("eboot.bin",read,output,why) && reads==beforeLarge && output.empty());
    b=good; pfsFixture::le(b,0x38,32,8);
    pfsFixture::le(b,bs+8,17*bs,8); pfsFixture::le(b,bs+0x10,17*bs,8); pfsFixture::le(b,bs+0x60,17,4);
    pfsFixture::le(b,bs+0xa8+0x64,19,4); pfsFixture::le(b,bs+2*0xa8+0x64,20,4);
    assert(!pfs.inspect(32*bs,read) && pfs.status==PFS::Status::unsupported && pfs.files.empty());
    // Exact I/O failure, including selected-file payload, stays atomic.
    b=good; assert(!pfs.inspect(b.size(),[](uint64_t,uint8_t*,size_t){return false;}));
    assert(pfs.inspect(b.size(),read)); output={1};
    assert(!pfs.extract("eboot.bin",[](uint64_t,uint8_t*,size_t){return false;},output,why) && output.empty());
    // Deterministic mutation sweep; refusal never leaves an extractable partial tree.
    for(size_t at=0;at<0xa8;++at) {
        b=good; b[bs+at]^=0xff;
        if(!pfs.inspect(b.size(),read)) assert(pfs.files.empty());
    }
    b=good;
    const auto temp=std::filesystem::temp_directory_path()/ ("maxps4-pfs-"+std::to_string(getpid()));
    std::filesystem::create_directory(temp);
    const auto source=temp/"synthetic.pkg";
    auto pkg=pfsFixture::package(good);
    const auto write=[&] { std::ofstream f(source,std::ios::binary); f.write(reinterpret_cast<const char*>(pkg.data()),std::streamsize(pkg.size())); assert(f.good()); };
    write();
    std::vector<char> report(4*1024*1024);
    assert(maxps4_pkg_inventory_report(source.c_str(),report.data(),report.size())==0);
    assert(std::string(report.data()).find("PFS non chiffré accessible")!=std::string::npos);
    assert(std::string(report.data()).find("eboot.bin accessible, non validé")!=std::string::npos);
    size_t length=0; uint8_t out[8]={0xaa};
    assert(maxps4_pkg_pfs_read_file(source.c_str(),"eboot.bin",nullptr,0,&length)==-ENOSPC && length==5);
    assert(maxps4_pkg_pfs_read_file(source.c_str(),"eboot.bin",out,4,&length)==-ENOSPC && out[0]==0xaa);
    assert(maxps4_pkg_pfs_read_file(source.c_str(),"eboot.bin",out,8,&length)==0 && length==5 && !std::memcmp(out,"hello",5));
    assert(maxps4_pkg_pfs_read_file(source.c_str(),"../eboot.bin",out,8,&length)==-EACCES && length==0);
    assert(maxps4_pkg_pfs_read_file(source.c_str(),"missing",out,8,&length)==-ENOENT && length==0);
    assert(maxps4_pkg_pfs_read_file(source.c_str(),"keystone",out,8,&length)==-EACCES && length==0);
    const auto link=temp/"link.pkg"; std::filesystem::create_symlink(source,link);
    assert(maxps4_pkg_pfs_read_file(link.c_str(),"eboot.bin",out,8,&length)<0);
    pfsFixture::le(pkg,bs+0x1c,0xc,2); write();
    assert(maxps4_pkg_pfs_read_file(source.c_str(),"eboot.bin",out,8,&length)==-EACCES);
    assert(maxps4_pkg_inventory_report(source.c_str(),report.data(),report.size())==0);
    assert(std::string(report.data()).find("PFS chiffré déclaré : inaccessible")!=std::string::npos);
    if(argc>1) {
        const std::filesystem::path artifacts(argv[1]); std::filesystem::create_directories(artifacts);
        for(const auto* name:{"pfs-plain.pkg","pfs-encrypted.pkg","pfs-unknown.pkg"}) {
            auto data=good;
            if(std::string(name)=="pfs-encrypted.pkg") pfsFixture::le(data,0x1c,0xc,2);
            if(std::string(name)=="pfs-unknown.pkg") pfsFixture::le(data,8,0,8);
            data=pfsFixture::package(data);
            std::ofstream f(artifacts/name,std::ios::binary); f.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size())); assert(f.good());
        }
    }
    std::filesystem::remove_all(temp);
    std::cout << "PASS PFS read-only D32: real synthetic uroot read, selected extraction, encrypted header-only refusal, unknown/unsupported/malformed, paths/cycles/overlaps/budgets/mutations, CNT/C ABI; no execution\n";
}
