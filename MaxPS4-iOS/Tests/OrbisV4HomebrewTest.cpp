#include "../Native/OrbisV4PRXLaunch.hpp"
#include <cassert>
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    assert(argc==2);
    std::ifstream file(argv[1],std::ios::binary);
    const std::vector<uint8_t> original((std::istreambuf_iterator<char>(file)),{});
    assert(original.size()>0x8004);
    auto bytes=original;
    maxps4_v4_prx_request r{bytes.data(),bytes.size(),0,0x400000,MAXPS4_V4_ABI_STATIC_EXEC_HLT,4};
    std::unique_ptr<maxps4::OrbisV4Translation> owner;
    unsigned invalidations=0;
    auto prepare=[&] { r.file=bytes.data(); return maxps4::OrbisV4PRXLaunch::prepare(r,[&] { ++invalidations; },owner); };
    for (unsigned i=0;i<10;++i) {
        assert(prepare()==0 && owner);
        uint32_t data=0;
        assert(owner->read(0x410000,&data,4)==0 && data==40);
        uint8_t bss[64];
        assert(owner->read(0x410004,bss,sizeof(bss))==0);
        for (auto b:bss) assert(b==0);
        {
            auto lease=owner->lease();
            for (const auto& range:lease.ranges()) assert(range.guest!=range.host);
            assert(!lease.memory().translate(0x400000,1,2));
            assert(!lease.memory().translate(0x410000,1,1));
            assert(!lease.memory().translate(maxps4::OrbisV4PRXLaunch::stack,1,1));
        }
        owner->close(); owner.reset();
    }
    assert(invalidations>=10);
    auto refuse=[&] { assert(prepare()==-ENOTSUP && !owner); };
    r.abi=MAXPS4_V4_ABI_SYNTHETIC_HLT; refuse(); // Old PRX ABI stays restricted.
    r.abi=MAXPS4_V4_ABI_STATIC_EXEC_HLT;
    r.instruction_budget=3; refuse(); r.instruction_budget=4;
    r.load_bias=0x4000; refuse(); r.load_bias=0;
    r.entry=0x40000a; refuse(); r.entry=0x400000;
    bytes[7]=0; refuse(); bytes=original; // Ordinary Linux ELF is not Orbis.
    bytes[16]=0x18; refuse(); bytes=original; // Dynamic PRX cannot use static ABI.
    bytes[64+4]=7; refuse(); bytes=original; // W+X.
    bytes[64+56+4]=7; refuse(); bytes=original;
    bytes[64+56+17]=0x40; bytes[64+56+18]=0x40; refuse(); bytes=original; // Guard collision.
    bytes[64+56]=7; refuse(); bytes=original; // TLS.
    bytes[64+56]=2; refuse(); bytes=original; // Dynamic startup unsupported.
    bytes[0x4000]=0x0f; bytes[0x4001]=0x05; refuse(); bytes=original; // SYSCALL.
    bytes[0x4000]=0xeb; refuse(); bytes=original; // Unbounded branch.
    bytes[0x4000]=0xc3; refuse(); bytes=original; // No startup return ABI.
    bytes[0x4002]=0; bytes[0x4003]=0; bytes[0x4004]=0; refuse(); bytes=original; // Bad operand.
    bytes[64+32]=1; refuse(); bytes=original; // Truncated file-backed instruction.
    assert(prepare()==0 && owner); owner->close();
    std::cout << "PASS source-built Orbis static homebrew: admission, BSS, translated addresses, RX/RW-NX, cleanup and unsupported ABI/TLS/dynamic/syscall/branch refusals; no native CPU dispatch\n";
}
