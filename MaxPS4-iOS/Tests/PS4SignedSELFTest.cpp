#include "../Native/OrbisV4Eboot.hpp"
#include "PS4ExecutableFixture.h"
#include <cassert>
#include <iostream>

static std::vector<uint8_t> signedFixture() {
    const auto plain=plainSELF();
    std::vector<uint8_t> b(444,0);
    std::copy(plain.begin(),plain.begin()+32,b.begin());
    guest_test::put(b,352,12,2); guest_test::put(b,b.size(),16,4); guest_test::put(b,4,24,2);
    auto block=[&](size_t i,uint64_t flags,uint64_t offset,uint64_t size) {
        const auto p=32+i*32;
        guest_test::put(b,flags,p,8); guest_test::put(b,offset,p+8,8);
        guest_test::put(b,size,p+16,8); guest_test::put(b,size,p+24,8);
    };
    block(0,0x110004,352,32); block(1,0x2804,416,11);
    block(2,0x310004,384,32); block(3,0x102804,432,12);
    std::copy(plain.begin()+96,plain.begin()+272,b.begin()+160);
    std::copy(plain.begin()+288,plain.begin()+299,b.begin()+416);
    std::copy(plain.begin()+299,plain.end(),b.begin()+432);
    return b;
}
int main() {
    using Image=maxps4::PS4ExecutableImage;
    const auto profile=Image::SelfPolicy::StageSignedClear;
    const auto original=signedFixture();
    Image image;
    for (unsigned i=0;i<10;++i) {
        assert(image.load(original.data(),original.size(),profile)==0);
        assert(image.isSelf && image.signedSelfUnverified && image.segments.size()==2);
        assert(image.translate(0x1000,11,5)[0]==0xb8);
        assert(image.translate(0x10000,64,6)[12]==0);
        assert(!image.translate(0x1000,1,2) && !image.translate(0x10000,1,1));
        assert(image.load(original.data(),original.size())==-ENOTSUP); // Strict stays strict.
        assert(image.segments.empty() && !image.signedSelfUnverified);
    }
    auto refuse=[&](size_t offset,uint64_t value,int status) {
        auto b=original; guest_test::put(b,value,offset,8);
        assert(image.load(original.data(),original.size(),profile)==0);
        assert(image.load(b.data(),b.size(),profile)==status);
        assert(image.segments.empty() && image.headers.empty() && !image.entry && !image.signedSelfUnverified);
    };
    for (size_t i=0;i<4;++i) {
        const auto p=32+i*32;
        const uint64_t flags=i==0 ? 0x110004 : i==1 ? 0x2804 : i==2 ? 0x310004 : 0x102804;
        refuse(p,flags|2,-ENOTSUP); refuse(p,flags|8,-ENOTSUP);
    }
    refuse(64,0x3804,-ENOTSUP); // Unsupported block-bits profile.
    refuse(64,0x100000002804ULL,-ENOTSUP); // Unknown high bits.
    refuse(72,417,-ENOEXEC); // Misaligned direct data without physical overlap.
    refuse(48,0,-ENOEXEC); // Digest length cannot be omitted.
    refuse(72,UINT64_MAX,-ENOEXEC); // Overflowing source span.
    refuse(136,416,-ENOEXEC); // Two physical sources overlap.
    refuse(32,0x10004,-ENOEXEC); // Digest points at itself rather than data descriptor.
    refuse(96,0x110004,-ENOEXEC); // Two digest tables for one data descriptor.
    refuse(32,0x100000,-ENOEXEC); // Missing digest association, not guessed from adjacency.
    refuse(88,12,-ENOEXEC); // Encoded/decoded size mismatch.
    refuse(128,0x2804,-ENOEXEC); // Two data descriptors for the same ELF PH.
    refuse(128,0x202804,-ENOEXEC); // PH index outside ELF table.
    auto wx=original; guest_test::put(wx,7,160+64+4,4);
    assert(image.load(wx.data(),wx.size(),profile)==-ENOEXEC && image.segments.empty());
    auto legacy=plainSELF(); guest_test::put(legacy,0x100804,32,8); guest_test::put(legacy,0x804,64,8);
    assert(image.load(legacy.data(),legacy.size(),profile)==0 && image.signedSelfUnverified);
    assert(image.load(legacy.data(),legacy.size())==-ENOTSUP);
    // Signature metadata is opaque, never treated as code or trust. This is not
    // hash/signature verification, whether the supplied digest bytes are zero or not.
    auto opaque=original; opaque[352]=0xff;
    assert(image.load(opaque.data(),opaque.size(),profile)==0 && image.signedSelfUnverified);
    maxps4_v4_prx_request request{}; std::string report;
    assert(maxps4::OrbisV4Eboot::request(original.data(),original.size(),request,report)==-ENOTSUP);
    assert(!request.file && report.find("signature_verified=false")!=std::string::npos);
    assert(report.find("EBOOT.SELF")!=std::string::npos);
    request={original.data(),original.size(),0,0x1000,MAXPS4_V4_ABI_STATIC_EXEC_HLT,64};
    std::unique_ptr<maxps4::OrbisV4Translation> owner;
    assert(maxps4::OrbisV4PRXLaunch::prepare(request,[]{},owner,&report)==-ENOTSUP && !owner);
    std::cout<<"PASS signed clear SELF inspection: strict policy preserved, digest/data IDs, span/size/overlap/flags rejection, BSS, RX/RW-NX, transactional cleanup; no trust, guest mapping or dispatch\n";
}
