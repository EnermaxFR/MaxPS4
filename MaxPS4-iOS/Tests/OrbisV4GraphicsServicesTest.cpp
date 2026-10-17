#include "../Native/OrbisV4GraphicsServices.hpp"
#include "../Native/LinuxVideoPresenter.hpp"
#include <cassert>
#include <iostream>
using namespace maxps4;
int main(){
    constexpr uint64_t base=0x100000;
    OrbisV4Translation memory([]{});assert(!memory.reserve(base,0x10000));assert(!memory.map(base,0x10000,6));
    {
        auto lease=memory.lease();
        OrbisV4ProcessBatch batch;OrbisV4HostServices svc(batch);uint64_t arena=base+0x8000;
        auto g=OrbisV4GraphicsServices::install(svc,[&](auto&,size_t n){auto at=arena;arena+=(n+15)&~size_t(15);return at;});
        auto call=[&](const std::string& name,uint64_t a=0,uint64_t b=0,uint64_t c=0,uint64_t d=0,uint64_t e=0,uint64_t f=0){
            size_t i=0;for(;i<batch.services.size();++i)if(batch.services[i].name==name)break;
            if(i==batch.services.size()){OrbisV4ProcessBatch::Service s;s.name=name;s.id=batch.serviceBase+uint32_t(i);s.stub=0x400000+i*16;batch.services.push_back(s);}
            OrbisV4HostServices::Registers r;r.rax=batch.services[i].id;r.rip=batch.services[i].stub+8;r.rsp=base+0x7000;r.rdi=a;r.rsi=b;r.rdx=c;r.rcx=d;r.r8=e;r.r9=f;
            return svc.dispatch(r,lease);
        };
        auto put=[&](uint64_t at,const auto& value){assert(!lease.write_data(at,&value,sizeof(value)));};
        auto get64=[&](uint64_t at){uint64_t n=0;assert(!lease.read_data(at,&n,8));return n;};
        assert(call("sceVideoOutOpen").value==0x80290018);
        int opens=0,closes=0,presents=0;std::vector<uint8_t> captured;
        g->openDevice=[&](auto&){++opens;return true;};g->closeDevice=[&]{++closes;};
        g->present=[&](uint32_t w,uint32_t h,const auto& pixels,uint64_t address,uint64_t arg,auto&){assert(w==2&&h==2&&address==base+0x400&&arg==42);captured=pixels;++presents;return true;};
        assert(call("sceVideoOutOpen",0,1).value==0x80290001);
        auto h=call("sceVideoOutOpen").value;assert(h==1&&opens==1);
        assert(call("sceVideoOutOpen").value==0x80290009);
        assert(call("sceVideoOutSetFlipRate",99,0).value==0x8029000b);
        assert(call("sceVideoOutSetFlipRate",h,3).value==0x80290001);
        assert(call("sceVideoOutSetFlipRate",h,1).value==0);
        uint32_t pitch=3;put(base+0x7008,pitch);
        assert(call("sceVideoOutSetBufferAttribute",base,0x80000000,1,0,2,2).value==0);
        assert(uint32_t(get64(base+20))==3);
        uint64_t buffer=base+0x400;put(base+0x100,buffer);
        const uint8_t bgra[24]={3,2,1,255,6,5,4,255,99,99,99,99,9,8,7,255,12,11,10,255,88,88,88,88};assert(!lease.write_data(buffer,bgra,24));
        assert(call("sceVideoOutRegisterBuffers",h,0,base+0x100,1,base).value==0);
        assert(call("sceVideoOutRegisterBuffers",h,0,base+0x100,1,base).value==0x80290010);
        // Mission 54: compute shader binding is a real 25-DWORD PM4 stream.
        const std::array<uint32_t,7> csRegs{0x1234U,0,0xabc000U,0x42U,8,4,1};
        assert(!lease.write_data(base+0x1500,csRegs.data(),sizeof(csRegs)));
        assert(call("sceGnmSetCsShaderWithModifier",base+0x1600,24,base+0x1500,0).value==uint64_t(-1));
        assert(call("sceGnmSetCsShaderWithModifier",base+0x1600,25,base+0x1500,1).value==uint64_t(-1));
        assert(call("sceGnmSetCsShaderWithModifier",base+0x1600,25,base+0x1500,0x40).value==0);
        std::array<uint32_t,25> csPacket{};
        assert(!lease.read_data(base+0x1600,csPacket.data(),sizeof(csPacket)));
        assert(csPacket[0]==0xc0027602U && csPacket[1]==0x20c && csPacket[2]==0x1234);
        assert(csPacket[4]==0xc0027602U && csPacket[5]==0x212);
        assert(csPacket[6]==((csRegs[2]&0xfffffc3fU)|0x40U));
        assert(csPacket[8]==0xc0037602U && csPacket[9]==0x207);
        assert(csPacket[10]==8 && csPacket[11]==4 && csPacket[12]==1);
        assert(csPacket[13]==0xc00a1000U && csPacket[14]==0);
        gpu::GNMCommandExecutor csExecutor;std::string csError;
        assert(csExecutor.execute(lease,base+0x1600,std::vector<uint32_t>(csPacket.begin(),csPacket.end()),csError));
        assert(csExecutor.state.shader[0x20c]==0x1234);
        assert(csExecutor.state.shader[0x212]==csPacket[6]);
        assert(csExecutor.state.shader[0x207]==8 && csExecutor.state.shader[0x209]==1);
        assert(call("sceGnmSetCsShader",base+0x1600,25,base+0x1500).value==0);
        assert(!lease.read_data(base+0x1600,csPacket.data(),sizeof(csPacket)));
        assert(csPacket[6]==csRegs[2]);
        // Mission 55: encode compute dispatch, but never claim GPU execution.
        assert(call("sceGnmDispatchDirect",base+0x1700,8,1,1,1,0).value==uint64_t(-1));
        assert(call("sceGnmDispatchDirect",base+0x1700,9,0x80000000U,1,1,0).value==uint64_t(-1));
        assert(call("sceGnmDispatchDirect",base+0x1700,9,0x7f80,1,1,0).value==0);
        std::array<uint32_t,9> dispatchPacket{};
        assert(!lease.read_data(base+0x1700,dispatchPacket.data(),sizeof(dispatchPacket)));
        assert(dispatchPacket[0]==0xc0031502U && dispatchPacket[1]==0x7f80);
        assert(dispatchPacket[2]==1 && dispatchPacket[3]==1 && dispatchPacket[4]==1);
        assert(dispatchPacket[5]==0xc0021000U);
        gpu::GNMCommandExecutor dispatchExecutor;std::string dispatchError;
        assert(!dispatchExecutor.execute(lease,base+0x1700,
            std::vector<uint32_t>(dispatchPacket.begin(),dispatchPacket.end()),dispatchError));
        assert(dispatchError.find("GPU packet semantics not implemented")!=std::string::npos);
        // Mission 56: bounded debug marker is a NOP packet, not a GPU draw.
        const char pushMarker[]="hello!";
        assert(!lease.write_data(base+0x1800,pushMarker,sizeof(pushMarker)));
        assert(call("sceGnmInsertPushMarker",base+0x1900,8,base+0x1800).value==uint64_t(-1));
        assert(call("sceGnmInsertPushMarker",base+0x1900,9,base+0x1800).value==0);
        std::array<uint32_t,9> markerPacket{};
        assert(!lease.read_data(base+0x1900,markerPacket.data(),sizeof(markerPacket)));
        assert(markerPacket[0]==0xc0071000U && markerPacket[1]==0x68750001U);
        assert(std::memcmp(markerPacket.data()+2,pushMarker,sizeof(pushMarker))==0);
        gpu::GNMCommandExecutor markerExecutor;std::string markerError;
        assert(markerExecutor.execute(lease,base+0x1900,
            std::vector<uint32_t>(markerPacket.begin(),markerPacket.end()),markerError));
        assert(call("sceGnmInsertPopMarker",base+0x1b00,5).value==uint64_t(-1));
        assert(call("sceGnmInsertPopMarker",base+0x1b01,6).value==uint64_t(-1));
        assert(call("sceGnmInsertPopMarker",base+0x10000,6).outcome==OrbisV4HostServices::Outcome::Trap);
        assert(call("sceGnmInsertPopMarker",base+0x1b00,6).value==0);
        std::array<uint32_t,6> popPacket{};assert(!lease.read_data(base+0x1b00,popPacket.data(),sizeof(popPacket)));
        assert(popPacket[0]==0xc0041000U&&popPacket[1]==0x68750002U);
        gpu::GNMCommandExecutor popExecutor;std::string popError;
        assert(popExecutor.execute(lease,base+0x1b00,std::vector<uint32_t>(popPacket.begin(),popPacket.end()),popError));
        assert(popExecutor.draws==0&&popExecutor.writes==0);
        // Mission 57: embedded VS id 0 has real GCN bytes in aligned guest
        // storage and a complete register-binding stream. No draw is claimed.
        assert(call("sceGnmSetEmbeddedVsShader",base+0x2000,29,1).value==0x8eee00ffU);
        assert(call("sceGnmSetEmbeddedVsShader",base+0x2000,28,0).value==uint64_t(-1));
        assert(g->embeddedVs==0);
        assert(call("sceGnmSetEmbeddedVsShader",base+0x2000,29,0).value==0);
        assert(g->embeddedVs && !(g->embeddedVs&255));
        std::array<uint32_t,29> embeddedPacket{};
        assert(!lease.read_data(base+0x2000,embeddedPacket.data(),sizeof(embeddedPacket)));
        gpu::GNMCommandExecutor embeddedExecutor;std::string embeddedError;
        assert(embeddedExecutor.execute(lease,base+0x2000,
            std::vector<uint32_t>(embeddedPacket.begin(),embeddedPacket.end()),embeddedError));
        assert((uint64_t(embeddedExecutor.state.shader[0x48])<<8)==g->embeddedVs);
        assert(embeddedExecutor.state.shader[0x4a]==0xc0000);
        assert(embeddedExecutor.state.context[0x1c3]==4);
        const auto embeddedAddress=g->embeddedVs;
        uint32_t firstInstruction=0;assert(!lease.read_data(embeddedAddress,&firstInstruction,4));
        assert(firstInstruction==0xbeeb03ffU);
        assert(call("sceGnmSetEmbeddedVsShader",base+0x2000,29,0).value==0&&g->embeddedVs==embeddedAddress);
        // Real PM4 wait-for-flip packet: seven DWORDs and a valid label.
        assert(call("sceGnmInsertWaitFlipDone",base+0x1800,6,h,0).value==uint64_t(-1));
        assert(call("sceGnmInsertWaitFlipDone",base+0x1800,7,h,0).value==0);
        std::array<uint32_t,7> waitPacket{};
        assert(!lease.read_data(base+0x1800,waitPacket.data(),sizeof(waitPacket)));
        assert(waitPacket[0]==0xc0053c00U && waitPacket[1]==0x13U);
        assert(waitPacket[4]==0 && waitPacket[5]==0xffffffffU && waitPacket[6]==10);
        const uint64_t label=uint64_t(waitPacket[2])|(uint64_t(waitPacket[3])<<32);
        assert(label==g->ports.at(h).labels);
        uint32_t clearLabel=0;put(label,clearLabel);
        gpu::GNMCommandExecutor waitExecutor;std::string waitError;
        std::vector<uint32_t> waitWords(waitPacket.begin(),waitPacket.end());
        assert(waitExecutor.execute(lease,base+0x1800,waitWords,waitError));
        // Labels are RW/NX data. Polling must request PF_R, never execute
        // permission; an inaccessible label still fails before reading.
        auto invalidWait=waitWords;invalidWait[2]=uint32_t(base+0x10000);
        waitExecutor.remaining=65536;
        assert(!waitExecutor.execute(lease,base+0x1800,invalidWait,waitError));
        assert(waitError.find("address invalid")!=std::string::npos);
        uint32_t busyLabel=1;put(label,busyLabel);waitExecutor.remaining=65536;
        assert(!waitExecutor.execute(lease,base+0x1800,waitWords,waitError));
        assert(waitError.find("condition unsatisfied")!=std::string::npos);
        put(label,clearLabel);

        const char name[]="graphics-test";assert(!lease.write_data(base+0x200,name,sizeof(name)));
        assert(call("sceKernelCreateEqueue",base+0x208,base+0x200).value==0);auto q=get64(base+0x208);
        assert(call("sceGnmAddEqEvent",q,64,0xcafe).value==0);
        assert(call("sceGnmAddEqEvent",q,63).value==uint64_t(ps4::kernel_error(ps4::inval)));
        assert(call("sceGnmDeleteEqEvent",0,64).value==uint64_t(ps4::kernel_error(ps4::badf)));
        assert(call("sceVideoOutAddFlipEvent",q,h,0xbeef).value==0);
        auto blocked=call("sceKernelWaitEqueue",q,base+0x1000,2,base+0x1100);assert(blocked.outcome==OrbisV4HostServices::Outcome::Block&&!blocked.ready());
        assert(call("sceVideoOutSubmitFlip",h,0,1,42).value==0);assert(presents==1&&captured.size()==16);
        assert(captured[0]==1&&captured[1]==2&&captured[2]==3&&captured[8]==7&&captured[12]==10);
        assert(blocked.ready()&&blocked.wake(lease)==0);
        OrbisV4GraphicsServices::Event event;assert(!lease.read_data(base+0x1000,&event,32));assert(event.ident==6&&event.filter==-13&&event.udata==0xbeef);
        assert(call("sceVideoOutGetFlipStatus",h,base+0x1200).value==0&&get64(base+0x1200)==1&&get64(base+0x1218)==42);
        assert(call("sceVideoOutSubmitFlip",h,15,1,42).value==0x8029000a);
        uint32_t zero=0;put(base+0x1300,zero);
        assert(call("sceKernelWaitEqueue",q,base+0x1000,1,base+0x1100,base+0x1300).value==uint64_t(ps4::kernel_error(ps4::Error(60))));
        g->trigger(64,-14,0,64);assert(g->pending(q));
        assert(call("sceGnmDeleteEqEvent",q,64).value==0&&!g->pending(q));
        assert(call("sceVideoOutGetBufferLabelAddress",h,base+0x1400).value==16&&get64(base+0x1400)>=base+0x8000);
        assert(call("sceVideoOutClose",h).value==0&&closes==1);
        assert(call("sceVideoOutClose",h).value==0x8029000b);
        assert(call("sceKernelDeleteEqueue",q).value==0);
        assert(LinuxVideoPresenter::png("/tmp/maxps4-mission52-test.png",2,2,captured));
    }
    memory.close();std::cout<<"PASS VideoOut lifecycle, validation, pitch/BGRA snapshot, presentation callback, status, labels, equeue blocking/wakeup/timeout, GNM event add/delete, PNG (synthetic test only)\n";
}
