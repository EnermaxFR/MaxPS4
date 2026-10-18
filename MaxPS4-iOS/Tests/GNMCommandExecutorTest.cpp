#include "../Native/GNMCommandExecutor.hpp"
#include "../Native/maxps4_orbis_v4_translation.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace maxps4;
    constexpr uint64_t base=0x400000;
    OrbisV4Translation owner([]{});const auto page=owner.page_size();
    assert(!owner.reserve(base,page*2));
    assert(!owner.map(base,page,6)&&!owner.map(base+page,page,6));
    {
        auto memory=owner.lease();gpu::GNMCommandExecutor engine;
        auto packet=[](uint32_t op,std::initializer_list<uint32_t> data) {
            std::vector<uint32_t> words{0xc0000000U|(uint32_t(data.size()-1)<<16)|(op<<8)};
            words.insert(words.end(),data);return words;
        };
        auto run=[&](const std::vector<uint32_t>& words,std::string& error){engine.remaining=65536;return engine.execute(memory,base,words,error);};
        auto read64=[&](uint64_t address){uint64_t value=0;assert(!memory.read_data(address,&value,8));return value;};
        const auto destination=base+page-4,fence=base+page+64;
        int interrupts=0;
        engine.interrupt=[&]{assert(read64(fence)==0xeeff0011aabbccddULL);++interrupts;};
        std::vector<uint32_t> stream;
        for(const auto& part:{packet(0x69,{8,0x1234}),packet(0x76,{0x20c,0x5678}),
            packet(0x37,{0x500,uint32_t(destination),0,0x11223344,0x55667788}),
            packet(0x47,{0x514,uint32_t(fence),0x42000000,0xaabbccdd,0xeeff0011}),
            packet(0x3c,{0x13,uint32_t(fence),0,0xaabbccdd,0xffffffff,10})})
            stream.insert(stream.end(),part.begin(),part.end());
        std::string error;
        assert(run(stream,error));
        assert(engine.state.context[8]==0x1234&&engine.state.shader[0x20c]==0x5678);
        assert(read64(destination)==0x5566778811223344ULL&&interrupts==1);
        // A later unmapped/overflowing write never alters a valid destination.
        assert(!run(packet(0x37,{0x500,0xfffffffc,0xffffffff,1,2}),error));
        assert(read64(destination)==0x5566778811223344ULL&&interrupts==1);
        auto invalidRegister=packet(0x69,{1023,1,2});
        assert(!run(invalidRegister,error)&&engine.state.context[1023]==0);
        auto predicate=packet(0x37,{0x500,uint32_t(destination),0,0,0});predicate[0]|=1;
        assert(!run(predicate,error)&&read64(destination)==0x5566778811223344ULL);
        // Owned nested commands are actually fetched from guest memory.
        const auto nestedAddress=base+256;auto nested=packet(0x79,{0x20,0xdeadbeef});
        assert(!memory.write_data(nestedAddress,nested.data(),nested.size()*4));
        assert(run(packet(0x3f,{uint32_t(nestedAddress),0,uint32_t(nested.size())}),error));
        assert(engine.state.uconfig[0x20]==0xdeadbeef);
        // Real memory copy/fill and an owned shader-port upload; no MMIO pointer.
        assert(run(packet(0x50,{0x60000000,uint32_t(destination),0,uint32_t(fence+16),0,8}),error));
        assert(read64(fence+16)==read64(destination));
        assert(run(packet(0x50,{0x40000000,0x12345678,0,uint32_t(fence+16),0,8}),error));
        assert(read64(fence+16)==0x1234567812345678ULL);
        assert(run(packet(0x50,{0x60000000,uint32_t(destination),0,0x3022c,0,0x28200008}),error));
        assert(engine.shaderUploads.size()==1&&engine.shaderUploads[0].source==destination);
        assert(engine.shaderUploads[0].bytes[0]==0x44&&engine.shaderUploads[0].bytes[7]==0x55);
        assert(!run(packet(0x50,{0x60000000,0,0,0x3022c,0,0x28200008}),error));
        assert(!run(packet(0x50,{0x60000000,uint32_t(destination),0,0x30230,0,0x28200008}),error));
        assert(engine.shaderUploads.size()==1&&read64(fence+16)==0x1234567812345678ULL);
        // Per-frame shader uploads (Kero Blaster: 4 per frame) never exhaust a
        // budget; only the latest 256 are kept for diagnostics.
        for(int k=0;k<300;++k)assert(run(packet(0x50,{0x60000000,uint32_t(destination),0,0x3022c,0,0x28200008}),error));
        assert(engine.shaderUploads.size()==256&&engine.shaderUploads.back().source==destination);
        engine.shaderUploads.resize(1);
        // End-of-compute fence writes exactly one DWORD; unknown event/GDS,
        // malformed/alignment/unmapped destinations never alter the label.
        assert(run(packet(0x48,{0x62f,uint32_t(fence+16),0x40000000,0x99}),error));
        assert(read64(fence+16)==0x1234567800000099ULL);
        assert(!run(packet(0x48,{0x62f,uint32_t(fence+16),0x20000000,0}),error));
        assert(!run(packet(0x48,{0x630,uint32_t(fence+16),0x40000000,0}),error));
        assert(!run(packet(0x48,{0x62f,uint32_t(fence+17),0x40000000,0}),error));
        assert(!run(packet(0x48,{0x62f,0,0x40000000,0}),error));
        assert(!run(packet(0x48,{0x62f,uint32_t(fence+16),0x40000000}),error));
        assert(read64(fence+16)==0x1234567800000099ULL&&interrupts==1);
        auto cycle=packet(0x3f,{uint32_t(nestedAddress),0,4});
        assert(!memory.write_data(nestedAddress,cycle.data(),cycle.size()*4));
        assert(!run(cycle,error)&&error.find("recursion/word budget")!=std::string::npos);
        // INDEX_TYPE is raw state until a draw needs a memory index fetch.
        // The SDK 0x400 value must not block generated-index draws, and must
        // never be silently stripped for a later real indexed fetch.
        assert(run(packet(0x2a,{0x400}),error)&&engine.state.indexType==0x400);
        assert(!run(packet(0x2d,{3,2}),error));
        assert(error.find("guest GCN draw rejected")!=std::string::npos); // no shaders/primitive bound
        assert(!run(packet(0x27,{3,uint32_t(destination),0,3,0}),error));
        assert(error.find("unsupported for indexed fetch")!=std::string::npos);
        assert(!run(packet(0x2a,{0,0}),error));
        assert(run(packet(0x2a,{0}),error));
        // Nonempty GCN work cannot complete through a command-only engine.
        assert(!run(packet(0x15,{1,1,1,1}),error));
        assert(error.find("GCN compute program=")!=std::string::npos);
        assert(!run(packet(0x15,{1,1,1}),error)&&error.find("invalid DISPATCH_DIRECT")!=std::string::npos);
        assert(!run(packet(0x2d,{3,2}),error)&&engine.draws==0&&interrupts==1);
        // Gnmx prepareFlip(label, value) as Kero Blaster emits it: a 64-dword
        // NOP whose label the driver writes at end of pipe (frame-reuse fence).
        // Annotations stay inert; unknown flip variants are refused.
        {
            std::vector<uint32_t> flip(64,0);flip[0]=0xc03e1000U;flip[1]=0x68750778U;
            flip[2]=uint32_t(fence+24);flip[3]=0;flip[4]=0x00000000U;
            const uint64_t busy=0x1111111122222222ULL;assert(!memory.write_data(fence+24,&busy,8));
            assert(run(flip,error)&&engine.flipLabels==1);
            assert(read64(fence+24)==0x1111111100000000ULL);
            flip[4]=7;flip[2]=uint32_t(fence+25);
            assert(!run(flip,error)&&error.find("prepareFlip label destination invalid")!=std::string::npos);
            flip[2]=0xfffffff0U;flip[3]=0xff;
            assert(!run(flip,error)&&engine.flipLabels==1);
            assert(run(packet(0x10,{0x68750001U,0x6c6f6f54U}),error)&&read64(fence+24)==0x1111111100000000ULL);
            assert(run(packet(0x10,{0x68750777U,0,0}),error));
            assert(!run(packet(0x10,{0x68750781U,0,0,0}),error)&&error.find("variant not implemented")!=std::string::npos);
            assert(run(packet(0x10,{0x68750780U,0,0,0,0,0}),error)&&interrupts==2);
        }
    }
    owner.close();
    std::cout<<"PASS PM4 register effects, cross-region WRITE_DATA, EOP store-before-IRQ, bounded label read, invalid-write isolation, owned indirect buffers, recursion bound and explicit shader refusal\n";
}
