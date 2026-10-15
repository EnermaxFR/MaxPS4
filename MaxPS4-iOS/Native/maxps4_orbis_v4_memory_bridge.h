#pragma once
#include "maxps4_guest_address_space.h"
#include <cerrno>
#include <cstddef>
#include <cstdint>

namespace maxps4 {
// Experimental FEX-facing admission boundary. No translated guest address
// may be handed to FEX's direct-memory JIT as a host pointer.
class OrbisV4MemoryBridge final {
public:
    explicit OrbisV4MemoryBridge(GuestAddressSpace& memory) : memory_(memory) {}
    int admit_fex_direct(uint64_t guest, size_t bytes, uint32_t permissions,
                         const uint8_t*& host) const {
        host = nullptr;
        if (!memory_.fex_direct()) return -ENOTSUP;
        if (!bytes || (permissions & ~7U) || !permissions) return -EINVAL;
        const auto* translated = memory_.translate(guest,bytes,permissions);
        if (!translated) return -EFAULT;
        // FEX currently uses the numeric guest VA for native JIT accesses.
        // Refuse any accidental translated-address dispatch.
        if (reinterpret_cast<uintptr_t>(translated) != guest) return -ENOTSUP;
        host = translated;
        return 0;
    }
    // Decoder-facing bounded copy: guest RX pages can have discontiguous
    // host backing. Never expose a raw translated pointer to FEX's JIT.
    // The destination is unchanged if any part of the fetch is inaccessible.
    int fetch_instruction(uint64_t guest, void* dst, size_t bytes) const {
        if (!bytes || bytes > 15) return -EINVAL; // x86 instruction limit
        return memory_.read(guest,dst,bytes,true);
    }
    // Decoder-facing fetch copies x86 instruction bytes from translated guest
    // storage. It never exposes a guest VA as a host pointer. This is not a
    // JIT load/store or atomic translation implementation.
    int fetch_instruction(uint64_t guest, void* dst, size_t bytes) const {
        if (!dst || !bytes || bytes > 15) return -EINVAL;
        return memory_.read(guest,dst,bytes,true);
    }
    int read_guest(uint64_t guest, void* dst, size_t bytes, bool instruction=false) const {
        return memory_.read(guest,dst,bytes,instruction);
    }
    int write_guest(uint64_t guest, const void* src, size_t bytes) {
        return memory_.write(guest,src,bytes);
    }
private:
    GuestAddressSpace& memory_;
};
}
