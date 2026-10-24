#pragma once
#include "maxps4_ps4_guest_memory.h"
#include "maxps4_ps4_abi.h"
#include <array>
#include <cstring>
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
    int64_t cs(uint64_t output, uint32_t size, uint64_t registers, uint32_t modifier) {
        if (size < 25 || !registers || (modifier & 0xfffffc3fU)) return -1;
        std::array<uint32_t,7> r{};
        if (!load(registers,r) || r[1]) return -1;
        Packet p;
        p.add(0x76,{0x20c,r[0],0},compute);
        p.add(0x76,{0x212,modifier ? (r[2] & 0xfffffc3fU) | modifier : r[2],r[3]},compute);
        p.add(0x76,{0x207,r[4],r[5],r[6]},compute); p.nop(11); return store(output,p);
    }
    int64_t dispatch(uint64_t output, uint32_t size, uint32_t x, uint32_t y, uint32_t z, uint32_t flags) {
        if (size != 9 || int32_t(x | y | z) < 0) return -1;
        Packet p; p.add(0x15,{x,y,z,(flags & 0x18) + 1},compute | (flags & 1)); p.nop(3); return store(output,p);
    }
    // The embedded full-screen VS (id 0) sets the registers of shadPS4's copy
    // with a null program address: its code is not placed in guest memory yet,
    // so a drawing backend must recognize it from these registers.
    int64_t embedded_vs(uint64_t output, uint32_t size, uint32_t id) {
        if (id) return ps4::signed32(0x8eee00ffU);
        if (size < 29) return -1;
        Packet p;
        p.add(0x76,{0x48,0,0}); p.add(0x76,{0x4a,0xc0000,4});
        p.add(0x69,{0x207,0}); p.add(0x69,{0x1b1,0}); p.add(0x69,{0x1c3,4});
        p.nop(11); return store(output,p);
    }
    // No flip is ever pending once the next frame's commands are built (the
    // display completes flips on its own), so the wait is a same-size NOP.
    int64_t wait_flip_done(uint64_t output, uint32_t size) {
        if (size != 7) return -1;
        Packet p; p.nop(6); return store(output,p);
    }
    int64_t push_marker(uint64_t output, uint32_t size, uint64_t marker) {
        std::array<char,512> text{};
        if (!marker || !memory_.read_cstring(marker,text.data(),text.size())) return -1;
        const size_t length=std::strlen(text.data());
        const size_t count=((length+8)>>2) + ((length+0xc)>>3)*2;
        if (count+2 != size || size > words_limit) return -1;
        Packet p; p.nop(count+1); p.words[1]=0x68750001U;
        for (size_t i=0;i<=length;++i) p.words[2+i/4] |= uint32_t(uint8_t(text[i])) << (8*(i%4));
        return store(output,p);
    }
    int64_t pop_marker(uint64_t output, uint32_t size) {
        if (size != 6) return -1;
        Packet p; p.nop(5); p.words[1]=0x68750002U; return store(output,p);
    }
    // Only the context clear (CONTEXT_CONTROL, CLEAR_STATE, EOS wait) of the
    // 3.50 default state: its register defaults are not written yet.
    int64_t init_default_state350(uint64_t output, uint32_t size) {
        if (size < words_limit) return 0;
        Packet p;
        for (auto value : {0xc0012800U,0x80000000U,0x80000000U,0xc0001200U,0U,0xc0055800U,
                           0x2ec47fc0U,0xffffffffU,0U,0U,0U,10U}) p.words[p.used++]=value;
        p.nop(words_limit-p.used-1);
        return store(output,p) ? -1 : int64_t(words_limit);
    }
private:
    static constexpr uint32_t compute=2;
    // Largest implemented packet sequence (the default state) is 256 dwords,
    // irrespective of the caller's advertised capacity. Build completely
    // before touching output.
    static constexpr size_t words_limit=256;
    struct Packet {
        std::array<uint32_t,words_limit> words{};
        size_t used=0;
        // `flags`: shader type (bit 1, compute) and predicate (bit 0).
        void add(uint32_t opcode, std::initializer_list<uint32_t> data, uint32_t flags=0) {
            words[used++]=0xc0000000U | (uint32_t(data.size()-1)<<16) | (opcode<<8) | flags;
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
        std::array<uint8_t,words_limit*4> wire{};
        for(size_t i=0;i<packet.used;++i) ps4::put_le(wire.data(),4*i,packet.words[i],4);
        return memory_.write(address,wire.data(),packet.used*4) ? 0 : -1;
    }
    PS4GuestMemory& memory_;
};
}
