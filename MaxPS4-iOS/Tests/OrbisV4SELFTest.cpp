#include "../Native/OrbisV4Eboot.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
static std::vector<uint8_t> readFile(const char* path) {
    std::ifstream f(path,std::ios::binary);
    return {(std::istreambuf_iterator<char>(f)),{}};
}
int main(int argc,char** argv) {
    assert(argc==3);
    const auto elf=readFile(argv[1]), original=readFile(argv[2]);
    auto bytes=original;
    std::vector<uint8_t> canonical; std::string reason;
    assert(maxps4::OrbisV4SELF::adapt(bytes.data(),bytes.size(),canonical,reason)==0);
    maxps4::PS4ExecutableImage expected, actual;
    assert(expected.load(elf.data(),elf.size())==0 && actual.load(canonical.data(),canonical.size())==0);
    assert(expected.entry==actual.entry && expected.segments.size()==actual.segments.size());
    for (size_t i=0;i<expected.segments.size();++i) {
        const auto& a=expected.segments[i]; const auto& b=actual.segments[i];
        assert(a.bytes==b.bytes && a.header.address==b.header.address && a.header.flags==b.header.flags);
        assert(a.header.fileSize==b.header.fileSize && a.header.memorySize==b.header.memorySize);
    }
    maxps4_v4_prx_request request{bytes.data(),bytes.size(),0,0x400000,MAXPS4_V4_ABI_STARTUP_RETURN,64};
    std::unique_ptr<maxps4::OrbisV4Translation> owner; unsigned invalidations=0;
    auto prepare=[&] {request.file=bytes.data(); request.length=bytes.size();
        return maxps4::OrbisV4PRXLaunch::prepare(request,[&]{++invalidations;},owner,&reason);};
    assert(prepare()==0 && owner);
    bytes.clear(); canonical.clear(); // Mapping cannot borrow either source buffer.
    uint32_t marker=0; assert(owner->read(0x410000,&marker,4)==0 && marker==0x1234);
    {auto lease=owner->lease();
     for (const auto& r:lease.ranges()) assert(r.guest!=r.host);
     assert(lease.permitted(0x400000,15,5) && !lease.permitted(0x400000,1,2));
     assert(lease.permitted(0x410000,8,6) && !lease.permitted(0x410000,1,1));
     assert(!lease.permitted(maxps4::OrbisV4Startup::stack,1,1));}
    owner->close(); assert(owner->read(0x410000,&marker,4)==-ESHUTDOWN); owner.reset();
    assert(invalidations);
    bytes=original;
    auto signedBytes=original; signedBytes[32]|=4;
    canonical={1,2,3};
    assert(maxps4::OrbisV4SELF::adapt(signedBytes.data(),signedBytes.size(),canonical,reason)==-ENOTSUP);
    assert(canonical.empty());
    const size_t origin=32+size_t(bytes[24])*32;
    auto refuse=[&](size_t offset,uint64_t value,size_t width=8) {
        for (size_t j=0;j<width;++j) bytes.at(offset+j)=uint8_t(value>>(8*j));
        assert(prepare()==-ENOTSUP && !owner);
        bytes=original;
    };
    refuse(32,0x804); // Signed even if not encrypted: strict dispatch path stays closed.
    refuse(32,0x802); refuse(32,0x808); // Encryption/compression.
    refuse(origin+16,0xfe10,2); // Dynamic executable needs a different startup contract.
    refuse(origin+64+56,7,4); // Actual TLS declaration.
    refuse(origin+64+4,7,4); // W+X.
    refuse(origin+64+56+16,0x400000); // Guest segment collision.
    refuse(origin+64+56,4,4); // Metadata cannot be discarded by normalizing headers.
    const size_t code=maxps4::OrbisMetadata::read(bytes.data()+40,8);
    refuse(code,0xeb,1); // Unknown branch remains rejected after SELF adaptation.
    request.entry=0x400001; assert(prepare()==-ENOTSUP && !owner); request.entry=0x400000;
    assert(prepare()==0 && owner); owner->close();
    std::cout<<"PASS unsigned static SELF -> existing linker -> translated V4: exact segment bytes/BSS/VA/perms, independent lifetime, W^X/NX and fail-closed signed/dynamic/TLS/collision/opcode paths; no CPU execution\n";
}
