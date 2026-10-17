#pragma once
#include "PM4Type3Decoder.hpp"
#include <array>
#include <algorithm>
#include <atomic>
#include <functional>
#include <sstream>
#include <string>

namespace maxps4::gpu {
// Synchronous bounded PM4 engine. It performs register/memory/fence work;
// a draw requires a renderer that actually executes the guest shaders.
// Unsupported packets stop at their exact command address, never succeed.
struct GNMCommandExecutor {
    struct State { std::array<uint32_t,1024> context{},shader{},uconfig{};uint32_t instances=1,indexType=0; } state;
    struct Draw { uint8_t opcode=0;uint32_t count=0,initiator=0;uint64_t indexAddress=0; };
    uint64_t packets=0,draws=0,writes=0;size_t remaining=65536;
    std::function<bool(const State&,const Draw&,std::string&)> render;
    std::function<void()> interrupt;
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
            case 0x12: if(n!=1||p[0])return fail("unsupported CLEAR_STATE mode");state.context.fill(0);break;
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
            case 0x2a: if(n!=1||p[0]>2)return fail("index type/endian mode unsupported");state.indexType=p[0];break;
            case 0x37:{
                if(n<4)return fail("WRITE_DATA too short");const uint32_t destination=(p[0]>>8)&15;
                if(destination!=1&&destination!=2&&destination!=5)return fail("WRITE_DATA destination is not supported memory");
                const uint64_t dst=uint64_t(p[1])|(uint64_t(p[2])<<32);const bool repeat=p[0]&0x10000;
                if(!memory.permitted(dst,repeat?4:(n-3)*4,2))return fail("WRITE_DATA destination unmapped/read-only");
                if(repeat){for(size_t k=3;k<n;++k)if(memory.write_data(dst,p+k,4))return fail("WRITE_DATA failed");}
                else if(memory.write_data(dst,p+3,(n-3)*4))return fail("WRITE_DATA failed");
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
                else{if(n!=5)return fail("invalid DRAW_INDEX_2");draw.indexAddress=uint64_t(p[1])|(uint64_t(p[2])<<32);draw.count=p[3];draw.initiator=p[4];}
                if(!draw.count||!state.instances)break;
                std::string detail;
                if(!render || !render(state,draw,detail)){
                    std::ostringstream reason;reason<<"guest GCN draw needs shader execution/rasterizer; count="<<std::dec<<draw.count<<" VS=0x"<<std::hex<<(uint64_t(state.shader[0x48])<<8)<<" PS=0x"<<(uint64_t(state.shader[8])<<8)<<" CB_COLOR0_BASE=0x"<<(uint64_t(state.context[0x318])<<8)<<" "<<detail;
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
