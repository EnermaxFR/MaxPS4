#include "../Native/OrbisMetadata.hpp"
#include <cassert>
#include <iostream>
using namespace maxps4;
#include "OrbisLoaderMission42Fixture.h"
using namespace mission42;
int main() {
    OrbisMetadata metadata;
    auto e=elf(); assert(metadata.parse(e.data(),e.size()) && !metadata.clearSelf);
    auto s=self(); assert(metadata.parse(s.data(),s.size()) && metadata.clearSelf);
    assert(metadata.entry==0x1000 && metadata.segments.size()==1);
    for (uint64_t flags:{0x802,0x808,0x804}) {
        auto bad=s;put(bad,32,flags);
        assert(!metadata.parse(bad.data(),bad.size()) && metadata.segments.empty());
        assert(metadata.error.find("aucun déchiffrement")!=std::string::npos);
    }
    for(size_t p:{size_t(40),size_t(64+32)}) {
        auto bad=s;put(bad,p,UINT64_MAX);
        assert(!metadata.parse(bad.data(),bad.size()));
    }
    // Header extent must be bounded before a trailer reads its PH table.
    auto bad=s;put(bad,12,65535,2);put(bad,16,191,4);
    assert(!metadata.parse(bad.data(),bad.size()));
    for (size_t p:{size_t(9),size_t(48),size_t(40),size_t(62)}) {
        auto badELF=e; put(badELF,p,1,1);
        assert(!metadata.parse(badELF.data(),badELF.size()));
    }
    put(e,32,UINT64_MAX);assert(!metadata.parse(e.data(),e.size()));
    for(size_t n=0;n<s.size();++n) assert(!metadata.parse(s.data(),n));
    std::cout<<"PASS mission42: ELF, clear SELF, protected flags, offsets, extents, truncation; no execution\n";
}
