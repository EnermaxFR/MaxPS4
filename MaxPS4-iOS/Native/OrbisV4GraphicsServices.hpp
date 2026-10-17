#pragma once
#include "OrbisV4HostServices.hpp"
#include "maxps4_ps4_abi.h"
#include "GuestProviders/GnmInitState.hpp"
#include "GuestProviders/GnmEmbeddedVS.hpp"
#include "GNMCommandExecutor.hpp"
#include <array>
#include <chrono>
#include <deque>
#include <cstring>
#include <cstdio>
#include <memory>
#include <algorithm>
#include <fstream>
#include <cstdlib>

namespace maxps4 {
// Portable VideoOut/event state. Guest addresses stay logical; only owned
// pixel snapshots cross the presenter callback. GPU drawing is a separate
// executor and must never be replaced by a successful no-op submission.
struct OrbisV4GraphicsServices {
    using R=OrbisV4HostServices::Registers; using M=OrbisV4HostServices::Memory;
    using Result=OrbisV4HostServices::Result; using O=OrbisV4HostServices::Outcome;
    struct Event { uint64_t ident=0; int16_t filter=0; uint16_t flags=1; uint32_t fflags=0; uint64_t data=0, udata=0; };
    static_assert(sizeof(Event)==32);
    struct Subscription { Event event; bool pending=false; uint64_t port=0; };
    struct Queue { std::map<std::pair<uint64_t,int16_t>,Subscription> events; std::string name; };
    struct Buffer { uint64_t address=0; std::array<uint32_t,10> attr{}; int group=-1; };
    struct Port { std::array<Buffer,16> buffers{}; uint32_t groups=0; int rate=0,current=-1; uint64_t flips=0,arg=uint64_t(-1),last=0,labels=0; };
    std::map<uint64_t,Queue> queues; std::map<uint64_t,Port> ports;
    uint64_t nextQueue=0x52000000, nextPort=1, start=now();
    gpu::GNMCommandExecutor engine;
    uint64_t submitted=0,completed=0;
    uint64_t embeddedVs=0;
    std::function<bool(std::string&)> openDevice;
    std::function<void()> closeDevice;
    std::function<bool(uint32_t,uint32_t,const std::vector<uint8_t>&,uint64_t,uint64_t,std::string&)> present;
    std::function<uint64_t(M&,size_t)> allocate;
    static uint64_t now(){return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
    static Result ok(uint64_t n=0){return {O::Returned,n,{}};}
    static Result video(uint32_t n){return ok(0x80290000U|n);}
    static Result kernel(ps4::Error e){return ok(uint64_t(ps4::kernel_error(e)));}
    static Result trap(const std::string& s){return {O::Trap,0,s};}
    static bool permittedSpan(M& m,uint64_t address,size_t size,uint32_t permissions){
        if(!size||address>UINT64_MAX-size)return false;
        while(size){const auto n=std::min(size,M::copy_limit);if(!m.permitted(address,n,permissions))return false;address+=n;size-=n;}return true;
    }
    void trigger(uint64_t id,int16_t filter,uint64_t port,uint64_t data){
        for(auto& [q,queue]:queues)for(auto& [key,s]:queue.events)
            if(key.first==id && key.second==filter && (!port || s.port==port)){s.pending=true;s.event.data=data;}
    }
    bool pending(uint64_t q) const {
        auto it=queues.find(q);if(it==queues.end())return true;
        for(const auto& [key,s]:it->second.events)if(s.pending)return true;
        return false;
    }
    uint64_t consume(uint64_t q,uint64_t out,uint32_t max,uint64_t count,M& m){
        auto it=queues.find(q);if(it==queues.end())return kernel(ps4::badf).value;
        std::vector<Event> events;
        for(auto& [key,s]:it->second.events)if(s.pending && events.size()<max)events.push_back(s.event);
        const uint32_t n=uint32_t(events.size());
        if((n && m.write_data(out,events.data(),events.size()*sizeof(Event))) || m.write_data(count,&n,4))return kernel(ps4::fault).value;
        for(const auto& e:events)it->second.events.at({e.ident,e.filter}).pending=false;
        return n?0:kernel(ps4::Error(60)).value;
    }
    Result flip(const R& r,M& m){
        auto p=ports.find(r.rdi);if(p==ports.end())return video(11);
        if(r.rsi>=16 || p->second.buffers[r.rsi].group<0)return video(10);
        if(r.rdx>2)return video(6);
        const auto& b=p->second.buffers[r.rsi];const auto& a=b.attr;
        if(a[1]!=1)return trap("VideoOut tiled framebuffer requires display-macro detiling; no linear reinterpretation permitted");
        const size_t bytes=size_t(a[3])*a[4]*4;
        std::vector<uint8_t> rgba(bytes);
        for(uint32_t y=0;y<a[4];++y)if(m.read_data(b.address+uint64_t(y)*a[5]*4,rgba.data()+size_t(y)*a[3]*4,size_t(a[3])*4))return video(19);
        if(a[0]==0x80000000U)for(size_t i=0;i<rgba.size();i+=4)std::swap(rgba[i],rgba[i+2]);
        std::string error;
        if(!present || !present(a[3],a[4],rgba,b.address,r.rcx,error))return trap("VideoOut presentation failed: "+error);
        auto& port=p->second;++port.flips;port.current=int(r.rsi);port.arg=r.rcx;port.last=now();
        trigger(6,-13,r.rdi,(r.rcx<<16)|0x1000);
        return ok();
    }
    Result submit(M& m,uint64_t count,uint64_t dcb,uint64_t sizes,uint64_t ccb,uint64_t ccbSizes){
        if(count>16 || (count && (!dcb||!sizes)))return ok(0x80d11000);
        std::array<uint64_t,16> drawAddresses{},computeAddresses{};std::array<uint32_t,16> drawSizes{},computeSizes{};
        if(count && (m.read_data(dcb,drawAddresses.data(),size_t(count)*8)||m.read_data(sizes,drawSizes.data(),size_t(count)*4)))return ok(0x80d11000);
        if(count && (ccb||ccbSizes) && (!ccb||!ccbSizes||m.read_data(ccb,computeAddresses.data(),size_t(count)*8)||m.read_data(ccbSizes,computeSizes.data(),size_t(count)*4)))return ok(0x80d11000);
        struct Stream{uint64_t address;std::vector<uint32_t> words;std::string kind;};std::vector<Stream> streams;
        // Preflight/snapshot every stream before performing any GPU-side write.
        for(size_t i=0;i<count;++i)for(bool compute:{true,false}){
            auto at=compute?computeAddresses[i]:drawAddresses[i];auto size=compute?computeSizes[i]:drawSizes[i];if(!size)continue;
            if((at&3)||(size&3)||size>65536*4)return trap("GNM submission alignment/size outside bounded PM4 profile");
            Stream stream{at,std::vector<uint32_t>(size/4),compute?"ccb":"dcb"};
            if(m.read_data(at,stream.words.data(),size))return trap("GNM command buffer unreadable");streams.push_back(std::move(stream));
        }
        ++submitted;engine.remaining=65536;
        const std::string dir=std::getenv("MAXPS4_GPU_DUMP_DIR")?std::getenv("MAXPS4_GPU_DUMP_DIR"):"docs/mission52-logs";
        size_t index=0;
        for(const auto& stream:streams){
            const std::string prefix=dir+"/gpu-submit-"+std::to_string(submitted)+"-"+std::to_string(index++);
            std::ofstream dump(prefix+"-"+stream.kind+".bin",std::ios::binary);dump.write(reinterpret_cast<const char*>(stream.words.data()),std::streamsize(stream.words.size()*4));dump.close();
            std::fprintf(stderr,"GPU.SUBMIT sequence=%llu kind=%s guest=0x%llx bytes=%zu dump=%s\n",(unsigned long long)submitted,stream.kind.c_str(),(unsigned long long)stream.address,stream.words.size()*4,prefix.c_str());
            std::string error;
            if(!engine.execute(m,stream.address,stream.words,error)){
                // Replay inputs for the actual failed job, after all preceding
                // register packets have executed. No memory writes or GPU
                // completion are synthesized by this diagnostic snapshot.
                std::ofstream state(prefix+"-registers.bin",std::ios::binary);
                for(const auto* bank:{&engine.state.context,&engine.state.shader,&engine.state.uconfig})
                    state.write(reinterpret_cast<const char*>(bank->data()),std::streamsize(bank->size()*4));
                for(const auto& stage:{std::make_pair("vs",0x48),std::make_pair("ps",8)}){
                    const uint64_t program=uint64_t(engine.state.shader[stage.second])<<8;std::array<uint8_t,256> bytes{};
                    if(program && !m.read_data(program,bytes.data(),bytes.size())){std::ofstream shader(prefix+"-"+stage.first+"-prefix.bin",std::ios::binary);shader.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
                }
                return trap(error);
            }
        }
        ++completed;return ok();
    }
    static std::shared_ptr<OrbisV4GraphicsServices> install(OrbisV4HostServices& svc,std::function<uint64_t(M&,size_t)> alloc){
        auto g=std::make_shared<OrbisV4GraphicsServices>();g->allocate=std::move(alloc);
        svc.implement("sceGnmDrawIndex",[](const R& r,M& m){
            if(!r.rdi || (r.rdi&3) || r.rsi!=10 || !r.rcx || (r.rcx&1) || (uint32_t(r.r8)&0x1ffffffeU))return ok(uint64_t(-1));
            // Base PS4 DRAW_INDEX_2 followed by a three-DWORD NOP payload.
            // This is command construction only; the executor must perform
            // the indexed draw with the bound guest shaders before success.
            const std::array<uint32_t,10> packet{0xc0042700U,uint32_t(r.rdx),uint32_t(r.rcx),uint32_t(r.rcx>>32),uint32_t(r.rdx),0,0xc0021000U,0,0,0};
            return m.write_data(r.rdi,packet.data(),sizeof(packet))?trap("GNM indexed draw command destination unwritable"):ok();
        });
        svc.implement("sceGnmSetEmbeddedVsShader",[g](const R& r,M& m){
            if(r.rdx!=0)return ok(0x8eee00ffU);
            if(!r.rdi || (r.rdi&3) || r.rsi<29)return ok(uint64_t(-1));
            if(r.rcx)return trap("GNM embedded VS shader modifier is not supported by this profile");
            if(!m.permitted(r.rdi,29*4,2))return trap("GNM embedded VS command destination unwritable");
            if(!g->embeddedVs){
                const auto& code=gpu::gnm_embedded::FullscreenVS;
                const uint64_t allocation=g->allocate(m,sizeof(code)+255);
                if(!allocation || allocation>UINT64_MAX-255)return trap("GNM embedded VS guest storage allocation failed");
                const uint64_t address=(allocation+255)&~uint64_t(255);
                if(m.write_data(address,code.data(),sizeof(code)))return trap("GNM embedded VS code storage unwritable");
                g->embeddedVs=address;
            }
            // Bind the actual GCN bytes through a logical guest GPU address,
            // never a host pointer. These are register packets, not a draw.
            const std::array<uint32_t,29> packet{
                0xc0027600U,0x48U,uint32_t(g->embeddedVs>>8),uint32_t(g->embeddedVs>>40),
                0xc0027600U,0x4aU,0xc0000U,4,
                0xc0016900U,0x207U,0,
                0xc0016900U,0x1b1U,0,
                0xc0016900U,0x1c3U,4,
                0xc00a1000U,0
            };
            return m.write_data(r.rdi,packet.data(),sizeof(packet))?trap("GNM embedded VS packet write failed"):ok();
        });
        g->engine.interrupt=[weak=std::weak_ptr<OrbisV4GraphicsServices>(g)]{if(auto state=weak.lock())state->trigger(64,-14,0,64);};
        svc.implement("sceGnmAreSubmitsAllowed",[](const R&,M&){return ok(1);}); // Synchronous queue, no outstanding lock.
        svc.implement("sceGnmSubmitDone",[g](const R&,M&){return g->submitted==g->completed?ok():ok(0x80d110ff);});
        for(const auto& name:{"sceGnmSubmitCommandBuffers","sceGnmSubmitCommandBuffersForWorkload","sceGnmSubmitAndFlipCommandBuffers","sceGnmSubmitAndFlipCommandBuffersForWorkload"})svc.implement(name,[g,name=std::string(name)](const R& r,M& m){
            const bool workload=name.find("ForWorkload")!=std::string::npos,flip=name.find("AndFlip")!=std::string::npos;
            R vo;
            if(flip){uint64_t args[4]{};if(m.read_data(r.rsp+8,args,(workload?4:3)*8))return trap("GNM flip stack arguments unreadable");
                vo.rdi=workload?uint32_t(args[0]):uint32_t(r.r9);vo.rsi=uint32_t(args[workload?1:0]);vo.rdx=uint32_t(args[workload?2:1]);vo.rcx=args[workload?3:2];
                if(!g->ports.count(vo.rdi))return video(11);
            }
            auto result=workload?g->submit(m,uint32_t(r.rsi),r.rdx,r.rcx,r.r8,r.r9):g->submit(m,uint32_t(r.rdi),r.rsi,r.rdx,r.rcx,r.r8);
            if(result.outcome!=O::Returned||result.value||!flip)return result;return g->flip(vo,m);
        });
        // PS4 compute shader binding emits three SET_SH_REG packets and a trailing NOP.
        // This binds registers only: actual GCN dispatch remains unsupported by the executor.
        for(const auto& name:{"sceGnmSetCsShader","sceGnmSetCsShaderWithModifier"})svc.implement(name,[name=std::string(name)](const R& r,M& m){
            constexpr uint32_t count=25;
            const uint32_t modifier=name=="sceGnmSetCsShaderWithModifier"?uint32_t(r.rcx):0;
            if(!r.rdi || (r.rdi&3) || r.rsi<count || !r.rdx || (modifier&0xfffffc3fU))
                return ok(uint64_t(-1));
            std::array<uint32_t,7> regs{};
            if(m.read_data(r.rdx,regs.data(),sizeof(regs)))return trap("GNM compute shader register array unreadable");
            if(regs[1])return ok(uint64_t(-1));
            if(!m.permitted(r.rdi,count*4,2))return trap("GNM compute shader destination unwritable");
            const uint32_t resource=modifier?(regs[2]&0xfffffc3fU)|modifier:regs[2];
            std::array<uint32_t,count> packet{
                0xc0027602U,0x20cU,regs[0],0,
                0xc0027602U,0x212U,resource,regs[3],
                0xc0037602U,0x207U,regs[4],regs[5],regs[6],
                0xc00a1000U,0
            };
            // Remaining NOP payload is explicitly zeroed by value initialization.
            if(m.write_data(r.rdi,packet.data(),sizeof(packet)))
                return trap("GNM compute shader packet write failed");
            std::fprintf(stderr,"GNM.SHADER stage=CS program=0x%llx resource1=%08x modifier=%08x\n",
                (unsigned long long)(uint64_t(regs[0])<<8),resource,modifier);
            // Capture only an owned, bounded prefix from the actual bound
            // guest program. This is input for GCN execution work, not a claim
            // that the shader has run or that the prefix is a complete binary.
            if(const char* dir=std::getenv("MAXPS4_GPU_DUMP_DIR")){
                const uint64_t program=uint64_t(regs[0])<<8;
                std::array<uint8_t,512> code{};
                if(program && !m.read_data(program,code.data(),code.size())){
                    std::ostringstream stem;stem<<dir<<"/gcn-cs-0x"<<std::hex<<program;
                    std::ofstream file(stem.str()+"-prefix.bin",std::ios::binary);
                    file.write(reinterpret_cast<const char*>(code.data()),code.size());
                    std::ofstream meta(stem.str()+"-registers.json");
                    meta<<"{\"program\":"<<program<<",\"prefix_bytes\":"<<code.size()<<",\"resource1\":"<<resource<<",\"resource2\":"<<regs[3]<<",\"local_size\":["<<regs[4]<<','<<regs[5]<<','<<regs[6]<<"]}\n";
                }
            }
            return ok();
        });
        // GPU debug marker: PM4 NOP payload with a bounded guest C string.
        // The marker annotates command streams; it does not execute GPU work.
        svc.implement("sceGnmInsertPopMarker",[](const R& r,M& m){
            if(!r.rdi || (r.rdi&3) || r.rsi!=6)return ok(uint64_t(-1));
            const std::array<uint32_t,6> packet{0xc0041000U,0x68750002U,0,0,0,0};
            return m.write_data(r.rdi,packet.data(),sizeof(packet))?trap("GNM pop marker packet destination unwritable"):ok();
        });
        svc.implement("sceGnmInsertPushMarker",[](const R& r,M& m){
            if(!r.rdi || (r.rdi&3) || !r.rdx || r.rsi<3 || r.rsi>4096)
                return ok(uint64_t(-1));
            if(!m.permitted(r.rdi,size_t(r.rsi)*4,2))
                return trap("GNM push marker destination unwritable");
            std::string marker;
            bool terminated=false;
            for(size_t i=0;i<4096;++i){
                char c=0;
                if(m.read_data(r.rdx+i,&c,1))return trap("GNM push marker string unreadable");
                if(!c){terminated=true;break;}
                marker.push_back(c);
            }
            if(!terminated)return ok(uint64_t(-1));
            const size_t len=marker.size();
            const size_t payload=((len+8)>>2)+(((len+12)>>3)*2);
            if(payload+2!=r.rsi)return ok(uint64_t(-1));
            std::vector<uint32_t> packet(size_t(r.rsi),0);
            packet[0]=0xc0001000U|(uint32_t(payload)<<16);
            packet[1]=0x68750001U;
            std::memcpy(reinterpret_cast<uint8_t*>(packet.data()+2),marker.c_str(),len+1);
            return m.write_data(r.rdi,packet.data(),packet.size()*4)
                ?trap("GNM push marker packet write failed"):ok();
        });
        // DISPATCH_DIRECT is a 9-DWORD PM4 compute dispatch command.
        // Encoding is not execution: the PM4 executor must reject GCN dispatch
        // until a real compute backend is installed.
        svc.implement("sceGnmDispatchDirect",[](const R& r,M& m){
            if(!r.rdi || (r.rdi&3) || r.rsi!=9 ||
               (uint32_t(r.rdx|r.rcx|r.r8)&0x80000000U))return ok(uint64_t(-1));
            if(!m.permitted(r.rdi,9*4,2))return trap("GNM dispatch destination unwritable");
            const std::array<uint32_t,9> packet{
                0xc0031502U|uint32_t(r.r9&1),uint32_t(r.rdx),uint32_t(r.rcx),uint32_t(r.r8),
                uint32_t(r.r9&0x18)+1U,0xc0021000U,0,0,0
            };
            return m.write_data(r.rdi,packet.data(),sizeof(packet))
                ?trap("GNM dispatch packet write failed"):ok();
        });
        for(const auto& name:{"sceGnmSetVsShader","sceGnmSetPsShader","sceGnmSetPsShader350","sceGnmUpdatePsShader","sceGnmUpdatePsShader350","sceGnmUpdateVsShader"})svc.implement(name,[name=std::string(name)](const R& r,M& m){
            const bool vs=name.find("VsShader")!=std::string::npos,update=name.find("Update")!=std::string::npos;
            const size_t count=vs?29:40;
            if(!r.rdi || (r.rdi&3) || r.rsi<count || (vs && (!r.rdx || (r.rcx&0xfcfffc3f))))return ok(uint64_t(-1));
            std::array<uint32_t,12> regs{};
            if(r.rdx && m.read_data(r.rdx,regs.data(),(vs?7:12)*4))return trap("GNM shader register array unreadable");
            if(regs[1])return ok(uint64_t(-1));
            std::vector<uint32_t> words;
            auto set=[&](uint32_t opcode,uint32_t offset,std::initializer_list<uint32_t> data){words.push_back(0xc0000000U|(uint32_t(data.size())<<16)|(opcode<<8));words.push_back(offset);words.insert(words.end(),data);};
            // UPDATE keeps context writes as SDK metadata in NOP payloads;
            // actual shader register writes still execute. Do not silently
            // replace this ABI with the different SET context semantics.
            auto context=[&](uint32_t offset,std::initializer_list<uint32_t> data){set(update?0x10:0x69,update?(0xc01e0000U|offset):offset,data);};
            if(vs){
                const uint32_t resource=r.rcx?(regs[2]&0xfcfffc3f)|uint32_t(r.rcx):regs[2];
                set(0x76,0x48,{regs[0],0});set(0x76,0x4a,{resource,regs[3]});context(0x207,{regs[6]});context(0x1b1,{regs[4]});context(0x1c3,{regs[5]});
            }else if(!r.rdx){
                set(0x76,8,{0,0});context(0x203,{0});if(name.find("350")!=std::string::npos)set(0x69,0x8f,{15});
            }else{
                set(0x76,8,{regs[0],0});set(0x76,10,{regs[2],regs[3]});context(0x1c4,{regs[4],regs[5]});context(0x1b3,{regs[6],regs[7]});context(0x1b6,{regs[8]});context(0x1b8,{regs[9]});context(0x203,{regs[10]});context(0x8f,{regs[11]});
            }
            if(words.size()+2>count)return trap("GNM shader packet size mismatch");
            words.push_back(0xc0001000U|(uint32_t(count-words.size()-2)<<16));words.resize(count,0);
            std::fprintf(stderr,"GNM.SHADER stage=%s program=0x%llx resource1=%08x resource2=%08x\n",vs?"VS":"PS",(unsigned long long)(uint64_t(regs[0])<<8),regs[2],regs[3]);
            return m.write_data(r.rdi,words.data(),words.size()*4)?trap("GNM shader packet destination unwritable"):ok();
        });
        for(const auto& name:{"sceGnmDrawInitDefaultHardwareState350","sceGnmDrawInitToDefaultContextState400"})svc.implement(name,[name=std::string(name)](const R& r,M& m){
            constexpr size_t size=256;
            if(!r.rdi || (r.rdi&3) || r.rsi<size || (name=="sceGnmDrawInitToDefaultContextState400" && r.rsi!=size))return ok(0);
            std::array<uint32_t,size> packet{};
            if(name=="sceGnmDrawInitDefaultHardwareState350"){
                const uint32_t clear[]={0xc0012800,0x80000000,0x80000000,0xc0001200,0,0xc0055800,0x2ec47fc0,0xffffffff,0,0,0,10};
                std::copy(std::begin(clear),std::end(clear),packet.begin());
                const auto& init=gpu::gnm_init::InitSequence350;
                auto end=std::copy(init.begin()+2,init.end(),packet.begin()+12);
                const auto remaining=packet.end()-end;
                *end=0xc0001000U|(uint32_t(remaining-2)<<16);
            }else{
                const auto& init=gpu::gnm_init::CtxInitSequence400;std::copy(init.begin(),init.end(),packet.begin());
            }
            return m.write_data(r.rdi,packet.data(),sizeof(packet))?trap("GNM init command destination not writable"):ok(size);
        });
        // PS4 ABI: (uint32_t* cmdbuf, uint32_t size_dwords, int vo_handle, uint32_t index).
        // Emits a real seven-DWORD WAIT_REG_MEM packet. Never report success
        // without writing the GPU synchronization command.
        svc.implement("sceGnmInsertWaitFlipDone",[g](const R& r,M& m){
            if(r.rsi!=7)return ok(uint64_t(-1));
            if(!r.rdi || (r.rdi&3) || !m.permitted(r.rdi,7*4,2))
                return trap("GNM wait-flip command destination invalid");
            auto port=g->ports.find(uint32_t(r.rdx));
            if(port==g->ports.end() || r.rcx>=16 || port->second.buffers[r.rcx].group<0)
                return trap("GNM wait-flip requires a registered VideoOut buffer");
            if(!port->second.labels)port->second.labels=g->allocate(m,128);
            if(!port->second.labels)return trap("GNM wait-flip label allocation failed");
            const uint64_t label=port->second.labels+uint64_t(r.rcx)*8;
            if((label&3) || !m.permitted(label,8,2))
                return trap("GNM wait-flip label address invalid");
            // PM4 WAIT_REG_MEM: memory poll, compare equal to zero.
            const std::array<uint32_t,7> packet{
                0xc0053c00U,0x13U,uint32_t(label),uint32_t(label>>32),0,0xffffffffU,10
            };
            return m.write_data(r.rdi,packet.data(),sizeof(packet))
                ?trap("GNM wait-flip packet write failed"):ok();
        });
        svc.implement("sceVideoOutOpen",[g](const R& r,M&){
            if(r.rsi || r.rdx || r.rcx)return video(1);
            if(!g->ports.empty())return video(9);
            std::string error;if(!g->openDevice || !g->openDevice(error)){std::fprintf(stderr,"VIDEO.DEVICE %s\n",error.c_str());return video(24);}
            const uint64_t h=g->nextPort++;g->ports.emplace(h,Port{});std::fprintf(stderr,"VIDEO.OPEN handle=%llu (real presenter)\n",(unsigned long long)h);return ok(h);
        });
        svc.implement("sceVideoOutClose",[g](const R& r,M&){
            if(!g->ports.erase(r.rdi))return video(11);
            for(auto& [id,q]:g->queues)for(auto it=q.events.begin();it!=q.events.end();)if(it->second.port==r.rdi)it=q.events.erase(it);else ++it;
            if(g->ports.empty() && g->closeDevice)g->closeDevice();return ok();
        });
        svc.implement("sceVideoOutSetFlipRate",[g](const R& r,M&){auto p=g->ports.find(r.rdi);if(p==g->ports.end())return video(11);if(r.rsi>2)return video(1);p->second.rate=int(r.rsi);return ok();});
        svc.implement("sceVideoOutSetBufferAttribute",[](const R& r,M& m){
            uint32_t pitch=0;if(m.read_data(r.rsp+8,&pitch,4))return trap("VideoOut attribute pitch stack argument unreadable");
            const std::array<uint32_t,10> a{uint32_t(r.rsi),uint32_t(r.rdx),uint32_t(r.rcx),uint32_t(r.r8),uint32_t(r.r9),pitch,0,0,0,0};
            if(m.write_data(r.rdi,a.data(),40))return trap("VideoOut attribute destination unwritable");return ok();
        });
        svc.implement("sceVideoOutRegisterBuffers",[g](const R& r,M& m){
            auto p=g->ports.find(r.rdi);if(p==g->ports.end())return video(11);
            if(r.rsi>=16 || !r.rcx || r.rcx>16-r.rsi)return video(10);
            std::array<uint32_t,10> a{};std::array<uint64_t,16> addresses{};
            if(m.read_data(r.r8,a.data(),40)||m.read_data(r.rdx,addresses.data(),size_t(r.rcx)*8))return video(2);
            if(a[0]!=0x80000000U && a[0]!=0x80002200U)return video(3);
            if(a[1]>1)return video(7);if(a[2])return video(8);
            if(!a[3]||!a[4]||a[3]>4096||a[4]>2160)return video(5);
            if(a[5]<a[3]||a[5]>8192)return video(4);if(a[6])return video(26);
            if(p->second.groups==4)return video(15);
            const size_t span=size_t(a[5])*a[4]*4;
            for(size_t i=0;i<r.rcx;++i){if(p->second.buffers[r.rsi+i].group>=0)return video(16);if(!addresses[i]||!permittedSpan(m,addresses[i],span,2))return video(19);}
            const int group=int(p->second.groups++);
            for(size_t i=0;i<r.rcx;++i)p->second.buffers[r.rsi+i]={addresses[i],a,group};
            std::fprintf(stderr,"VIDEO.BUFFERS handle=%llu group=%d count=%llu width=%u height=%u pitch=%u format=%08x tiling=%u first_guest=0x%llx\n",(unsigned long long)r.rdi,group,(unsigned long long)r.rcx,a[3],a[4],a[5],a[0],a[1],(unsigned long long)addresses[0]);
            return ok(uint64_t(group));
        });
        svc.implement("sceVideoOutSubmitFlip",[g](const R& r,M& m){return g->flip(r,m);});
        svc.implement("sceVideoOutGetResolutionStatus",[g](const R& r,M& m){
            if(!g->ports.count(r.rdi))return video(11);std::array<uint8_t,48> b{};const uint32_t dims[]={1280,720,1280,720};std::memcpy(b.data(),dims,16);uint64_t refresh=3;std::memcpy(b.data()+16,&refresh,8);float inches=24;std::memcpy(b.data()+24,&inches,4);return m.write_data(r.rsi,b.data(),b.size())?video(2):ok();
        });
        svc.implement("sceVideoOutGetFlipStatus",[g](const R& r,M& m){
            auto p=g->ports.find(r.rdi);if(p==g->ports.end())return video(11);
            std::array<uint64_t,8> b{p->second.flips,(p->second.last-g->start)/1000,p->second.last,p->second.arg,p->second.last,0,0,uint32_t(p->second.current)};
            if(!p->second.last)b[1]=0;return m.write_data(r.rsi,b.data(),64)?video(2):ok();
        });
        svc.implement("sceVideoOutIsFlipPending",[g](const R& r,M&){return g->ports.count(r.rdi)?ok(0):video(11);});
        svc.implement("sceVideoOutGetBufferLabelAddress",[g](const R& r,M& m){
            auto p=g->ports.find(r.rdi);if(p==g->ports.end())return video(11);if(!m.permitted(r.rsi,8,2))return video(2);
            if(!p->second.labels)p->second.labels=g->allocate(m,128);if(!p->second.labels)return video(0x100c);
            return m.write_data(r.rsi,&p->second.labels,8)?video(2):ok(16);
        });
        svc.implement("sceVideoOutWaitVblank",[g](const R& r,M&){
            if(!g->ports.count(r.rdi))return video(11);const uint64_t deadline=now()+16666667;Result b{O::Block,0,"VideoOut vblank"};b.deadlineNs=deadline;b.ready=[deadline]{return now()>=deadline;};b.wake=[g,h=r.rdi](M&){g->trigger(7,-13,h,1);return 0;};return b;
        });
        svc.implement("sceKernelCreateEqueue",[g](const R& r,M& m){
            if(!m.permitted(r.rdi,8,2))return kernel(ps4::fault);if(!r.rsi)return kernel(ps4::fault);
            std::string name;for(size_t i=0;i<32;++i){char c=0;if(m.read_data(r.rsi+i,&c,1))return kernel(ps4::fault);if(!c)break;name+=c;}
            if(name.size()==32)return kernel(ps4::inval);uint64_t id=g->nextQueue++;if(m.write_data(r.rdi,&id,8))return kernel(ps4::fault);g->queues[id].name=name;return ok();
        });
        svc.implement("sceKernelDeleteEqueue",[g](const R& r,M&){return g->queues.erase(r.rdi)?ok():kernel(ps4::badf);});
        for(const auto& name:{"sceGnmAddEqEvent","sceGnmDeleteEqEvent"})svc.implement(name,[g,add=std::string(name)=="sceGnmAddEqEvent"](const R& r,M&){
            auto q=g->queues.find(r.rdi);if(q==g->queues.end())return kernel(ps4::badf);
            if(!(r.rsi<=6 || r.rsi==64))return kernel(ps4::inval);
            const auto key=std::make_pair(r.rsi,int16_t(-14));if(add)q->second.events[key]={{r.rsi,-14,1,0,r.rsi,r.rdx},false,0};else q->second.events.erase(key);return ok();
        });
        for(const auto& name:{"sceVideoOutAddFlipEvent","sceVideoOutAddVblankEvent","sceVideoOutDeleteFlipEvent","sceVideoOutDeleteVblankEvent"})svc.implement(name,[g,name=std::string(name)](const R& r,M&){
            if(!g->ports.count(r.rsi))return video(11);auto q=g->queues.find(r.rdi);if(q==g->queues.end())return video(12);
            uint64_t id=name.find("Vblank")!=std::string::npos?7:6;const auto key=std::make_pair(id,int16_t(-13));
            if(name.find("Delete")!=std::string::npos)q->second.events.erase(key);else q->second.events[key]={{id,-13,1,0,0,r.rdx},false,r.rsi};return ok();
        });
        svc.implement("sceKernelWaitEqueue",[g](const R& r,M& m){
            if(!g->queues.count(r.rdi))return kernel(ps4::badf);if(!r.rdx||r.rdx>64)return kernel(ps4::inval);
            if(!m.permitted(r.rsi,size_t(r.rdx)*32,2)||!m.permitted(r.rcx,4,2))return kernel(ps4::fault);
            uint32_t us=0;if(r.r8 && m.read_data(r.r8,&us,4))return kernel(ps4::fault);
            if(g->pending(r.rdi)|| (r.r8&&!us))return ok(g->consume(r.rdi,r.rsi,uint32_t(r.rdx),r.rcx,m));
            const uint64_t deadline=r.r8?now()+uint64_t(us)*1000:0;Result b{O::Block,0,"kernel event queue"};b.deadlineNs=deadline;
            b.ready=[g,id=r.rdi,deadline]{return g->pending(id)||(deadline&&now()>=deadline);};
            b.wake=[g,r](M& m){return g->consume(r.rdi,r.rsi,uint32_t(r.rdx),r.rcx,m);};return b;
        });
        return g;
    }
};
}
