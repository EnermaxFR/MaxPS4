#pragma once
#include "maxps4_ps4_guest_memory.h"
#include "maxps4_ps4_abi.h"
#include <array>
#include <initializer_list>

namespace maxps4 {
// Base-PS4 GNM packet generation. These calls WRITE commands; they do not
// submit, execute a shader, signal completion or present a frame.
class PS4Gnm final {
public:
    explicit PS4Gnm(PS4GuestMemory& memory) : memory_(memory) {}
    int64_t draw_auto(uint64_t output, uint32_t size, uint32_t count, uint32_t flags) {
        if (size != 7 || (flags & 0x1ffffffeU)) return -1;
        Packet p; p.add(0x2d, {count, 2}); p.nop(3); return store(output,p);
    }
    int64_t draw_index(uint64_t output, uint32_t size, uint32_t count, uint64_t indices, uint32_t flags) {
        if (size != 10 || !indices || (indices & 1) || (flags & 0x1ffffffeU)) return -1;
        Packet p; p.add(0x27, {count,uint32_t(indices),uint32_t(indices>>32),count,0});
        p.nop(3); return store(output,p);
    }
    int64_t vgt(uint64_t output, uint32_t size, uint32_t group, uint32_t partial, uint32_t eop) {
        if (size != 3 || group >= 256 || (partial | eop) >= 2) return -1;
        Packet p; p.add(0x69,{0x2aa,group | (partial << 16)}); return store(output,p);
    }
    int64_t vs(uint64_t output, uint32_t size, uint64_t registers, uint32_t modifier) {
        if (size < 29 || !registers || (modifier & 0xfcfffc3fU)) return -1;
        std::array<uint32_t,7> r{};
        if (!load(registers,r) || r[1]) return -1;
        Packet p;
        p.add(0x76,{0x48,r[0],0});
        p.add(0x76,{0x4a,modifier ? (r[2] & 0xfcfffc3fU) | modifier : r[2],r[3]});
        p.add(0x69,{0x207,r[6]}); p.add(0x69,{0x1b1,r[4]}); p.add(0x69,{0x1c3,r[5]});
        p.nop(11); return store(output,p);
    }
    int64_t ps350(uint64_t output, uint32_t size, uint64_t registers) {
        if (size < 40) return -1;
        std::array<uint32_t,12> r{};
        if (registers && (!load(registers,r) || r[1])) return -1;
        Packet p; p.add(0x76,{8,r[0],0});
        if (!registers) {
            p.add(0x69,{0x203,0}); p.add(0x69,{0x8f,0xf}); p.nop(29);
        } else {
            p.add(0x76,{10,r[2],r[3]}); p.add(0x69,{0x1c4,r[4],r[5]});
            p.add(0x69,{0x1b3,r[6],r[7]}); p.add(0x69,{0x1b6,r[8]});
            p.add(0x69,{0x1b8,r[9]}); p.add(0x69,{0x203,r[10]}); p.add(0x69,{0x8f,r[11]}); p.nop(11);
        }
        return store(output,p);
    }
private:
    // Largest implemented packet sequence is 40 dwords, irrespective of the
    // caller's advertised capacity. Build completely before touching output.
    struct Packet {
        std::array<uint32_t,40> words{};
        size_t used=0;
        void add(uint32_t opcode, std::initializer_list<uint32_t> data) {
            words[used++]=0xc0000000U | (uint32_t(data.size()-1)<<16) | (opcode<<8);
            for (auto value:data) words[used++]=value;
        }
        void nop(size_t payload) {
            words[used++]=0xc0001000U | (uint32_t(payload-1)<<16);
            used+=payload;
        }
    };
    template<size_t N> bool load(uint64_t address, std::array<uint32_t,N>& values) {
        std::array<uint8_t,N*4> bytes{};
        if ((address & 3) || !memory_.read(address,bytes.data(),bytes.size())) return false;
        for (size_t i=0;i<N;++i) values[i]=uint32_t(ps4::get_le(bytes.data()+4*i,4));
        return true;
    }
    int64_t store(uint64_t address, const Packet& packet) {
        if (!address || (address & 3)) return -1;
        std::array<uint8_t,160> wire{};
        for(size_t i=0;i<packet.used;++i) ps4::put_le(wire.data(),4*i,packet.words[i],4);
        return memory_.write(address,wire.data(),packet.used*4) ? 0 : -1;
    }
    PS4GuestMemory& memory_;
};
}
