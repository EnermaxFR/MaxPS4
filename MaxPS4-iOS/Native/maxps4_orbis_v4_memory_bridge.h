#pragma once
#include "maxps4_guest_address_space.h"
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <type_traits>

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
    // Resolve a single guest memory operand for a future FEX JIT slow path.
    // The caller must hold the guest mapping stable until the access completes.
    // Refuse cross-page operands: they require an explicit split-access path,
    // not a raw native load/store that could overrun a mapped host region.
    int resolve_operand(uint64_t guest, size_t bytes, uint32_t permissions,
                        const uint8_t*& host) const {
        host = nullptr;
        if (!bytes || !permissions || (permissions & ~7U) ||
            (permissions & 3U) == 3U) return -EINVAL;
        const size_t page = memory_.page_size();
        if (!page || bytes > page || (guest % page) > page - bytes) return -EFAULT;
        host = memory_.translate(guest,bytes,permissions);
        return host ? 0 : -EFAULT;
    }
    // Single-mapping atomic primitive for a future translated JIT slow path.
    // The caller must serialize map/protect/release against this operation.
    // Never emulate a split-page atomic with separate reads and writes.
    template<class T>
    int compare_exchange_guest(uint64_t guest, T& expected, T desired,
                               bool& exchanged) {
        static_assert(std::is_integral_v<T> && !std::is_same_v<T,bool> &&
                      (sizeof(T) == 1 || sizeof(T) == 2 ||
                       sizeof(T) == 4 || sizeof(T) == 8),
                      "Only native 8/16/32/64-bit integer atomics supported");
        exchanged = false;
        if (guest % alignof(T)) return -EINVAL;
        // A translated native atomic must not straddle guest pages, even if
        // two adjacent guest regions happen to share one host reservation.
        const size_t page = memory_.page_size();
        if (!page || (guest % page) > page - sizeof(T)) return -EFAULT;
        // Both permissions are mandatory; no read-only or execute-only CAS.
        const uint8_t* ptr = nullptr;
        const int status = resolve_operand(guest,sizeof(T),6,ptr);
        if (status) return status;
        // Clang/GCC atomics operate on the mapped integer storage without
        // requiring C++20 atomic_ref or constructing an atomic<T> over guest
        // bytes. Reject targets that would call a locking runtime fallback.
        static_assert(__atomic_always_lock_free(sizeof(T), nullptr),
                      "Guest CAS requires native lock-free atomics");
        if (reinterpret_cast<uintptr_t>(ptr) % sizeof(T))
            return -EINVAL;
        auto* typed = reinterpret_cast<T*>(const_cast<uint8_t*>(ptr));
        // No software load/store fallback: preserve hardware atomicity.
        exchanged = __atomic_compare_exchange_n(typed,&expected,desired,false,
                                                __ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
        return 0;
    }
private:
    GuestAddressSpace& memory_;
};
}
