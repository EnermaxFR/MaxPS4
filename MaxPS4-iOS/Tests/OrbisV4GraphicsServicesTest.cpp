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
