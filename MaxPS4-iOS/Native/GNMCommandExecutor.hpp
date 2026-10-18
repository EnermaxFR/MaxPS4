#pragma once
#include "PM4Type3Decoder.hpp"
#include "GCNCompute.hpp"
#include "GCNRenderer.hpp"
#include <cstdio>
#include <array>
#include <algorithm>
#include <atomic>
#include <functional>
#include <sstream>
#include <string>

namespace maxps4::gpu {
// Synchronous bounded PM4 engine. It performs register/memory/fence work and
// executes draws through GCNRenderer (guest VS/PS, rasteriser, colour/depth).
// Unsupported packets or draw state stop at their exact command address,
// never succeed.
struct GNMCommandExecutor {
    struct State { std::array<uint32_t,1024> context{},shader{},uconfig{};uint32_t instances=1,indexType=0; } state;
    struct Draw { uint8_t opcode=0;uint32_t count=0,initiator=0;uint64_t indexAddress=0; };
    GCNCompute compute;
    GCNRenderer renderer;
    uint64_t packets=0,draws=0,writes=0;size_t remaining=65536;
    // Optional override (tests); the built-in renderer runs when unset.
    std::function<bool(const State&,const Draw&,std::string&)> render;
    // CLEAR_STATE loads the hardware default context, not zeros. Only the
    // defaults consumed by the renderer are modelled (CIK clear-state values):
    // scissor rectangles open to 16384 and every colour channel enabled.
    static void clearContext(std::array<uint32_t,1024>& c){
        c.fill(0);
        c[0x0d]=0x40004000;                 // PA_SC_SCREEN_SCISSOR_BR
        c[0x100]=0xffffffff;                // VGT_MAX_VTX_INDX
        c[0x81]=0x80000000;c[0x82]=0x40004000; // PA_SC_WINDOW_SCISSOR_TL/BR
        c[0x83]=0x0000ffff;                 // PA_SC_CLIPRECT_RULE
        c[0x8e]=0xffffffff;c[0x8f]=0xffffffff; // CB_TARGET_MASK, CB_SHADER_MASK
        c[0x90]=0x80000000;c[0x91]=0x40004000; // PA_SC_GENERIC_SCISSOR_TL/BR
        for(unsigned v=0;v<16;++v){c[0x94+v*2]=0x80000000;c[0x95+v*2]=0x40004000;} // PA_SC_VPORT_SCISSOR_n
        for(unsigned v=0;v<16;++v){c[0xb4+v*2]=0;c[0xb5+v*2]=0x3f800000;}         // PA_SC_VPORT_ZMIN/ZMAX_n
    }
    std::function<void()> interrupt;
    // Owned uploads to the shader-prefetch register port. A renderer must still
    // execute GCN; recording an upload never completes a dispatch or draw.
    struct ShaderUpload { uint64_t source;std::vector<uint8_t> bytes; };
    std::vector<ShaderUpload> shaderUploads;
    template<class Memory>
    bool execute(Memory& memory,uint64_t address,const std::vector<uint32_t>& words,std::string& error,unsigned depth=0){
        if(depth>8 || words.size()>remaining){error="PM4 recursion/word budget exhausted";return false;}
        remaining-=words.size();const auto parsed=decodePM4Type3(words.data(),words.size());
        if(!parsed){error="PM4 framing error="+std::to_string(int(parsed.error));return false;}
        for(const auto& packet:parsed.packets){
            const uint32_t* p=words.data()+packet.payloadWord;const auto n=packet.payloadCount;
            auto fail=[&](const std::string& why){std::ostringstream s;s<<"PM4 pc=0x"<<std::hex<<(address+packet.headerWord*4)<<" opcode=0x"<<unsigned(packet.opcode)<<": "<<why;error=s.str();return false;};
            ++packets;
            if(words[packet.headerWord]&1)return fail("predication requires a predicate evaluator");
            switch(packet.opcode){
            case 0x10: break; // Explicit NOP, including SDK annotation payloads.
            case 0x12: if(n!=1||p[0])return fail("unsupported CLEAR_STATE mode");clearContext(state.context);break;
            case 0x28: if(n!=2||p[0]!=0x80000000||p[1]!=0x80000000)return fail("unsupported CONTEXT_CONTROL load/shadow mode");break; // One synchronous context.
            case 0x58: if(n!=6)return fail("invalid ACQUIRE_MEM");std::atomic_thread_fence(std::memory_order_seq_cst);break;
            case 0x3c:{ // WAIT_REG_MEM: synchronous memory polling, bounded to avoid hangs.
                if(n!=6 || p[0]!=0x13 || p[4]!=0xffffffffU || !p[5])
                    return fail("unsupported WAIT_REG_MEM control/mask/interval");
                const uint64_t at=uint64_t(p[1])|(uint64_t(p[2])<<32);
                if((at&3)||!memory.permitted(at,4,4)) // ELF PF_R, not PF_X.
                    return fail("WAIT_REG_MEM address invalid");
                uint32_t observed=0;
                if(memory.read_data(at,&observed,4))return fail("WAIT_REG_MEM read failed");
                if(observed!=p[3])return fail("WAIT_REG_MEM condition unsatisfied (no GPU scheduler)");
                break;
            }

            case 0x69: case 0x76: case 0x79:{
                if(n<2)return fail("register packet lacks values");uint32_t first=p[0];
                if(packet.opcode==0x69 && first==0x100002aa)first=0x2aa; // Base-PS4 SDK indexed VGT control.
                if(first>=1024 || n-1>1024-first)return fail("register range/index unsupported");
                auto& bank=packet.opcode==0x69?state.context:packet.opcode==0x76?state.shader:state.uconfig;
                std::copy(p+1,p+n,bank.begin()+first);writes+=n-1;break;
            }
            case 0x2f: if(n!=1)return fail("invalid NUM_INSTANCES");state.instances=p[0];break;
            case 0x2a: // Preserve the complete register, including SDK high bits.
                // DRAW_INDEX_AUTO uses generated indices. Validate fetch modes
                // only when an indexed draw actually consumes guest indices.
                if(n!=1)return fail("invalid INDEX_TYPE payload");state.indexType=p[0];break;
            case 0x15:{
                if(n!=4)return fail("invalid DISPATCH_DIRECT payload");
                const uint64_t program=(uint64_t(state.shader[0x20c])<<8)|(uint64_t(state.shader[0x20d])<<40);
                std::fprintf(stderr,"GCN.DISPATCH program=0x%llx groups=%u,%u,%u local=%u,%u,%u rsrc=%08x,%08x\n",
                    (unsigned long long)program,p[0],p[1],p[2],state.shader[0x207],state.shader[0x208],state.shader[0x209],state.shader[0x212],state.shader[0x213]);
                for(unsigned j=0;j<((state.shader[0x213]>>1)&31) && j<16;++j)
                    std::fprintf(stderr,"GCN.USER s%u=0x%08x\n",j,state.shader[0x240+j]);
                std::string detail;
                const bool success=compute.dispatch(memory,state.shader,{p[0],p[1],p[2]},p[3],detail);
                const auto& st=compute.stats;
                for(size_t j=0;j<st.scalarReads;++j)std::fprintf(stderr,"GCN.SCALAR address=0x%llx value=0x%08x\n",(unsigned long long)st.scalarAddress[j],st.scalarValue[j]);
                std::fprintf(stderr,"GCN.RESULT success=%u groups=%llu lanes=%llu instructions=%llu loads=%llu stores=%llu verified=%llu first=0x%llx:%08x last=0x%llx:%08x\n",
                    unsigned(success),(unsigned long long)st.groups,(unsigned long long)st.lanes,(unsigned long long)st.instructions,
                    (unsigned long long)st.loads,(unsigned long long)st.stores,(unsigned long long)st.verified,
                    (unsigned long long)st.firstWrite,st.firstValue,(unsigned long long)st.lastWrite,st.lastValue);
                if(!success)return fail(detail);
                break;
            }
            case 0x50:{ // DMA_DATA (base PS4): bounded memory transfers / shader prefetch.
                if(n!=6)return fail("invalid DMA_DATA payload");
                const auto count=p[5]&0x1fffffU;
                const uint64_t src=uint64_t(p[1])|(uint64_t(p[2])<<32);
                const uint64_t dst=uint64_t(p[3])|(uint64_t(p[4])<<32);
                if(!count)return fail("zero-length DMA_DATA unsupported");
                // Observed SDK shader upload: L2 memory -> fixed register port.
                // Keep the real source bytes, never dereference the MMIO port
                // as guest RAM. Unknown register transfers remain errors.
                if(p[0]==0x60000000 && dst==0x3022c && (p[5]&~0x1fffffU)==0x28200000){
                    if(count>65536||shaderUploads.size()>=256||!memory.permitted(src,count,4))
                        return fail("shader DMA source/budget invalid");
                    ShaderUpload upload{src,std::vector<uint8_t>(count)};
                    if(memory.read_data(src,upload.bytes.data(),count))return fail("shader DMA read failed");
                    shaderUploads.push_back(std::move(upload));break;
                }
                const unsigned source=(p[0]>>29)&3,destination=(p[0]>>20)&3;
                if((p[0]&~0xe0300000U)||(p[5]&~0x003fffffU)||
                   (source!=0&&source!=2&&source!=3)||(destination!=0&&destination!=3))
                    return fail("DMA_DATA register/GDS/address mode unsupported");
                if(!memory.permitted(dst,count,2))return fail("DMA destination unmapped/read-only");
                std::vector<uint8_t> bytes(count);
                if(source==2){for(size_t i=0;i<count;++i)bytes[i]=uint8_t(p[1]>>((i&3)*8));}
                else if(!memory.permitted(src,count,4)||memory.read_data(src,bytes.data(),count))
                    return fail("DMA source inaccessible");
                if(memory.write_data(dst,bytes.data(),count))return fail("DMA write failed");
                std::atomic_thread_fence(std::memory_order_seq_cst);break;
            }
            case 0x37:{
                if(n<4)return fail("WRITE_DATA too short");const uint32_t destination=(p[0]>>8)&15;
                if(destination!=1&&destination!=2&&destination!=5)return fail("WRITE_DATA destination is not supported memory");
                const uint64_t dst=uint64_t(p[1])|(uint64_t(p[2])<<32);const bool repeat=p[0]&0x10000;
                if(!memory.permitted(dst,repeat?4:(n-3)*4,2))return fail("WRITE_DATA destination unmapped/read-only");
                if(repeat){for(size_t k=3;k<n;++k)if(memory.write_data(dst,p+k,4))return fail("WRITE_DATA failed");}
                else if(memory.write_data(dst,p+3,(n-3)*4))return fail("WRITE_DATA failed");
                break;
            }
            case 0x48:{ // EVENT_WRITE_EOS: CS_DONE, immediate 32-bit fence.
                // All supported compute work completes synchronously before
                // this packet. GDS transfers and other event modes stay errors.
                if(n!=4 || p[0]!=0x62f || (p[2]&0xffff0000U)!=0x40000000U)
                    return fail("unsupported EVENT_WRITE_EOS event/command");
                const uint64_t dst=uint64_t(p[1])|(uint64_t(p[2]&0xffff)<<32);
                if((dst&3)||!memory.permitted(dst,4,2))return fail("EOS fence destination invalid");
                std::atomic_thread_fence(std::memory_order_seq_cst);
                if(memory.write_data(dst,p+3,4))return fail("EOS fence write failed");
                break;
            }
            case 0x47:{
                if(n!=5)return fail("invalid EVENT_WRITE_EOP");const uint32_t select=p[2]>>29;
                const uint64_t dst=uint64_t(p[1])|(uint64_t(p[2]&0xffff)<<32);
                if(select>2)return fail("EOP data source requires GPU clock/counter implementation");
                if(select && memory.write_data(dst,p+3,select==1?4:8))return fail("EOP fence destination invalid");
                std::atomic_thread_fence(std::memory_order_seq_cst);if(((p[2]>>24)&3)&&interrupt)interrupt();break;
            }
            case 0x3f:{
                if(n!=3||p[1]>0xffff||(p[2]>>24))return fail("indirect buffer VMID/address unsupported");
                const uint64_t at=uint64_t(p[0])|(uint64_t(p[1])<<32);const size_t count=p[2]&0xfffff;
                if(!count||count>remaining||count>65536||(at&3))return fail("indirect buffer bounds");
                std::vector<uint32_t> nested(count);if(memory.read_data(at,nested.data(),count*4))return fail("indirect buffer inaccessible");
                if(!execute(memory,at,nested,error,depth+1))return false;if(p[2]&0x100000)return true;break;
            }
            case 0x2d: case 0x27:{
                Draw draw;draw.opcode=packet.opcode;
                if(packet.opcode==0x2d){if(n!=2)return fail("invalid DRAW_INDEX_AUTO");draw.count=p[0];draw.initiator=p[1];}
                else{if(n!=5)return fail("invalid DRAW_INDEX_2");if(state.indexType>2)return fail("index type/endian mode unsupported for indexed fetch");draw.indexAddress=uint64_t(p[1])|(uint64_t(p[2])<<32);draw.count=p[3];draw.initiator=p[4];}
                if(!draw.count||!state.instances)break;
                std::string detail;
                bool rendered=false;
                if(render)rendered=render(state,draw,detail);
                else{
                    GCNRenderer::DrawCall call;call.opcode=draw.opcode;call.count=draw.count;call.initiator=draw.initiator;
                    call.indexAddress=draw.indexAddress;call.indexType=state.indexType;call.instances=state.instances;
                    rendered=renderer.draw(memory,state.context,state.shader,state.uconfig,call,detail);
                }
                if(!rendered){
                    std::ostringstream reason;reason<<"guest GCN draw rejected; count="<<std::dec<<draw.count<<" VS=0x"<<std::hex<<(uint64_t(state.shader[0x48])<<8)<<" PS=0x"<<(uint64_t(state.shader[8])<<8)<<" CB_COLOR0_BASE=0x"<<(uint64_t(state.context[0x318])<<8)<<": "<<detail;
                    return fail(reason.str());
                }
                ++draws;break;
            }
            default:return fail("GPU packet semantics not implemented");
            }
        }
        return true;
    }
};
}
