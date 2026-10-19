#include "../Native/OrbisV4PRXLaunch.hpp"
#include <cassert>
#include <fstream>
#include <iostream>

int main(int argc,char** argv) {
    assert(argc==2);
    std::ifstream file(argv[1],std::ios::binary);
    const std::vector<uint8_t> original((std::istreambuf_iterator<char>(file)),{});
    assert(original.size()>0x8004);
    auto bytes=original;
    maxps4_v4_prx_request request{bytes.data(),bytes.size(),0,0x400000,MAXPS4_V4_ABI_STARTUP_RETURN,4};
    std::unique_ptr<maxps4::OrbisV4Translation> owner;
    std::string reason; unsigned invalidations=0;
    auto prepare=[&] {
        request.file=bytes.data();
        return maxps4::OrbisV4PRXLaunch::prepare(request,[&]{++invalidations;},owner,&reason);
    };
    auto put=[&](size_t offset,uint64_t value,size_t length) {
        for (size_t i=0;i<length;++i) bytes.at(offset+i)=uint8_t(value>>(8*i));
    };
    // Clang's exact source-built stream reads .data then argc then .bss.
    assert(bytes[0x4000]==0x8b && bytes[0x4001]==0x05);
    assert(bytes[0x4008]==0x03 && bytes[0x4009]==0x05 && bytes[0x400e]==0xc3);
    for (unsigned run=0;run<10;++run) {
        assert(prepare()==0 && owner);
        uint32_t initialized=0,zero=1;
        assert(owner->read(0x410000,&initialized,4)==0 && initialized==0x1234);
        assert(owner->read(0x410004,&zero,4)==0 && zero==0);
        auto lease=owner->lease();
        assert(!lease.permitted(0x400000,1,2)); // Code never writable.
        assert(!lease.permitted(0x410000,1,1)); // Globals never executable.
    }
    owner.reset();
    assert(invalidations>=10);
    auto refuse=[&](const char* diagnostic) {
        assert(prepare()==-ENOTSUP && !owner);
        if (reason.find(diagnostic)==std::string::npos) std::cerr<<"Expected "<<diagnostic<<" got "<<reason<<"\n";
        assert(reason.find(diagnostic)!=std::string::npos);
        bytes=original;
    };
    put(0x4002,0x80000000U,4); refuse("V4.OPERAND"); // Negative displacement underflows.
    put(0x4002,0x7fffffffU,4); refuse("V4.OPERAND"); // Positive unmapped address.
    put(0x4002,0U-6U,4); // Negative displacement to readable code itself.
    assert(prepare()==0 && owner); owner.reset(); bytes=original;
    put(64+56+4,4,4); // Read-only globals are sufficient for this program.
    assert(prepare()==0 && owner); owner.reset(); bytes=original;
    put(64+56+4,2,4); refuse("V4.LINK"); // Write-only globals cannot be read.
    put(0x4002,0x420000-0x400006,4); refuse("V4.OPERAND"); // Unmapped page.
    put(0x4002,0x413fff-0x400006,4); refuse("V4.OPERAND"); // Four-byte span crosses mapping.
    put(0x4001,0x0d,1); refuse("V4.OPERAND"); // ECX addressing is not silently admitted.
    put(64+32,5,8); put(64+40,5,8); refuse("V4.FETCH"); // Truncated disp32.
    request.instruction_budget=3; refuse("V4.BUDGET"); request.instruction_budget=4;
    put(64+56,7,4); refuse("V4.LINK"); // ELF PT_TLS remains unsupported.
    put(64+56,2,4); refuse("V4.LINK"); // No dynamic CRT by implication.
    assert(prepare()==0 && owner); owner->close();
    uint32_t value=0;
    assert(owner->read(0x410000,&value,4)==-ESHUTDOWN);
    std::cout<<"PASS RIP-relative globals: data/BSS, signed addressing, complete spans, permissions, bounded termination, failure cleanup and recreation\n";
}
