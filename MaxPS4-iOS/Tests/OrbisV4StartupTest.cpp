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
    maxps4_v4_prx_request r{bytes.data(),bytes.size(),0,0x400000,MAXPS4_V4_ABI_STARTUP_RETURN,3};
    std::unique_ptr<maxps4::OrbisV4Translation> owner;
    std::string diagnostic;
    auto prepare=[&] { r.file=bytes.data(); return maxps4::OrbisV4PRXLaunch::prepare(r,[] {},owner,&diagnostic); };
    for (unsigned i=0;i<10;++i) {
        assert(prepare()==0 && owner && diagnostic.find("V4.READY")==0);
        const auto rsp=maxps4::OrbisV4Startup::rsp(owner->page_size());
        assert((rsp&15)==8);
        uint64_t args[6],target=0;
        assert(owner->read(maxps4::OrbisV4Startup::arguments,args,sizeof(args))==0);
        assert(args[0]==1 && args[1]==maxps4::OrbisV4Startup::name);
        for (unsigned j=2;j<6;++j) assert(!args[j]);
        char name[9];
        assert(owner->read(args[1],name,sizeof(name))==0 && !std::strcmp(name,"homebrew"));
        assert(owner->read(rsp,&target,8)==0 && target==maxps4::OrbisV4Startup::return_pc);
        uint8_t halt=0;
        assert(owner->read(target,&halt,1,true)==0 && halt==0xf4);
        {
            auto lease=owner->lease();
            assert(!lease.memory().translate(target,1,2));
            assert(!lease.memory().translate(rsp,1,1));
            assert(!lease.memory().translate(maxps4::OrbisV4Startup::arguments,1,1));
            assert(!lease.memory().translate(0x400000,1,2));
        }
        owner->close(); owner.reset();
    }
    // Collision at the return page must not leave a partially created stack or
    // release/mutate the pre-existing caller-owned mapping.
    {
        maxps4::OrbisV4Translation memory([]{});
        const auto page=memory.page_size();
        const uint64_t marker=0x123456789abcdef0;
        assert(memory.reserve(maxps4::OrbisV4Startup::return_pc,page)==0);
        assert(memory.map(maxps4::OrbisV4Startup::return_pc,page,6)==0);
        assert(memory.write_span(maxps4::OrbisV4Startup::return_pc,&marker,8)==0);
        assert(maxps4::OrbisV4Startup::initialize(memory)==-EEXIST);
        uint64_t preserved=0;
        assert(memory.read(maxps4::OrbisV4Startup::return_pc,&preserved,8)==0 && preserved==marker);
        assert(memory.reserve(maxps4::OrbisV4Startup::stack,page)==0);
        assert(memory.release(maxps4::OrbisV4Startup::stack)==0);
        assert(memory.release(maxps4::OrbisV4Startup::return_pc)==0);
        assert(maxps4::OrbisV4Startup::initialize(memory)==0);
        memory.close();
    }
    auto refuse=[&](const char* category) {
        assert(prepare()==-ENOTSUP && !owner && diagnostic.find(category)!=std::string::npos);
    };
    r.instruction_budget=2; refuse("V4.BUDGET"); r.instruction_budget=3;
    r.entry++; refuse("V4.ENTRY"); r.entry--;
    r.load_bias=0x4000; refuse("V4.BIAS"); r.load_bias=0;
    r.abi=99; refuse("V4.ABI"); r.abi=MAXPS4_V4_ABI_STATIC_EXEC_HLT;
    refuse("V4.OPERAND"); r.abi=MAXPS4_V4_ABI_STARTUP_RETURN;
    bytes[0x4000]=0x0f; bytes[0x4001]=5; refuse("V4.INSTRUCTION"); bytes=original;
    for (uint8_t opcode:{0xf4,0xeb,0x50,0xe8}) {
        bytes[0x4000]=opcode; refuse("V4.INSTRUCTION"); bytes=original;
    }
    bytes[0x4000]=0x64; refuse("V4.TLS"); bytes=original; // Unsupported FS form remains refused.
    bytes[64+56]=7; refuse("TLS"); bytes=original;
    bytes[64+4]=7; refuse("V4.LINK"); bytes=original;
    auto put64=[&](size_t offset,uint64_t value) {
        for (unsigned j=0;j<8;++j) bytes[offset+j]=static_cast<uint8_t>(value>>(8*j));
    };
    // A valid ELF mapped where the runtime needs its stack must fail closed.
    put64(24,maxps4::OrbisV4Startup::stack);
    put64(64+16,maxps4::OrbisV4Startup::stack);
    put64(64+56+16,maxps4::OrbisV4Startup::stack+0x10000);
    r.entry=maxps4::OrbisV4Startup::stack;
    assert(prepare()==-EEXIST && !owner && diagnostic.find("V4.STARTUP")==0);
    bytes=original; r.entry=0x400000;
    // Proven argv dereference, then a readable four-byte load of arg0.
    const uint8_t argvProgram[]={0x48,0x8b,0x7f,8,0x8b,7,0xc3};
    std::copy(std::begin(argvProgram),std::end(argvProgram),bytes.begin()+0x4000);
    assert(prepare()==0 && owner); owner->close(); owner.reset();
    // Overflow in the additional pointer load must never reach FEX.
    const uint8_t overflow[]={0x48,0xbf,255,255,255,255,255,255,255,255,0x48,0x8b,0x7f,8,0xc3};
    bytes[64+32]=sizeof(overflow); bytes[64+40]=sizeof(overflow);
    std::copy(std::begin(overflow),std::end(overflow),bytes.begin()+0x4000);
    refuse("V4.OPERAND");
    std::cout<<"PASS V4 startup: argc/argv/env/AT_NULL, function-entry alignment, controlled RX return, RW-NX stack, refusal diagnostics, pointer overflow and cleanup; no CPU dispatch\n";
}
