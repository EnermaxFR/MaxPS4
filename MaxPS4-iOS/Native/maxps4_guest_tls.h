#pragma once
#include "maxps4_guest_address_space.h"
#include "PS4ExecutableImage.hpp"
#include "maxps4_guest_stack.h"

namespace maxps4 {
// Single-module x86-64 variant-II static TLS. Dynamic TLS relocations and
// __tls_get_addr are deliberately not admitted by the initial CPU path.
class GuestTLSTemplate final {
public:
    size_t alignment = 1, memory_size = 0, block_size = 0;
    std::vector<uint8_t> initial;
    int load(const PS4ExecutableImage& image) {
        alignment = 1; memory_size = block_size = 0; initial.clear();
        if (std::count_if(image.headers.begin(),image.headers.end(),[](const auto& h) { return h.type == 7; }) > 1)
            return -ENOEXEC;
        bool seen = false;
        for (const auto& h : image.headers) if (h.type == 7) {
            if (seen) return -ENOEXEC;
            seen = true;
            const uint64_t align = std::max(uint64_t{1},h.alignment);
            if (!h.memorySize || h.memorySize > 1024*1024 || h.fileSize > h.memorySize ||
                (align & (align-1)) || align > 16384 || h.address > UINT64_MAX-h.memorySize)
                return -ENOEXEC;
            // Nonzero first-byte alignment and TLS outside a readable PT_LOAD
            // require another layout model; do not silently misinitialize them.
            if (h.address % align || (h.flags & ~7U) || !(h.flags & 4U) || (h.flags & 1U))
                return -ENOTSUP;
            const auto* data = image.translate(h.address,h.memorySize,4);
            if (!data) return -ENOEXEC;
            bool file_backed = h.fileSize == 0;
            for (const auto& s : image.segments) if (h.address >= s.header.address &&
                h.address-s.header.address <= s.header.fileSize &&
                h.fileSize <= s.header.fileSize-(h.address-s.header.address) &&
                h.offset == s.header.offset+h.address-s.header.address) file_backed = true;
            if (!file_backed) return -ENOEXEC;
            alignment = std::max(size_t{16},static_cast<size_t>(align));
            memory_size = static_cast<size_t>(h.memorySize);
            block_size = (memory_size+alignment-1)/alignment*alignment;
            initial.assign(data,data+h.fileSize);
        }
        return 0;
    }
};

class GuestThreadMemory final {
public:
    explicit GuestThreadMemory(GuestAddressSpace& space) : space_(space) {}
    ~GuestThreadMemory() { clear(); }
    GuestThreadMemory(const GuestThreadMemory&) = delete;
    GuestThreadMemory& operator=(const GuestThreadMemory&) = delete;
    uint64_t rsp = 0, fs_base = 0, tls_begin = 0, stack_begin = 0;
    size_t tls_size = 0, stack_size = 0;
    int initialize(const GuestTLSTemplate& tls, uint64_t base, uint64_t entry) {
        clear();
        const size_t page = space_.page_size();
        if (!page || base % page || base > UINT64_MAX-2*1024*1024) return -EINVAL;
        stack_begin = base; stack_size = 0x10000;
        int error = space_.reserve(stack_begin,stack_size);
        if (error) { stack_begin = 0; return error; }
        error = space_.map(stack_begin,stack_size,6);
        if (error) { clear(); return error; }
        auto* stack = const_cast<uint8_t*>(space_.translate(stack_begin,stack_size,6));
        uint64_t host_rsp = 0;
        error = initialize_guest_stack(stack,stack_size,entry,page,host_rsp);
        if (error) { clear(); return error; }
        rsp = stack_begin + host_rsp-reinterpret_cast<uintptr_t>(stack);
        if (!tls.memory_size) return 0;
        tls_begin = base+0x20000;
        tls_size = (tls.block_size+48+page-1)/page*page;
        error = space_.reserve(tls_begin,tls_size);
        if (error) { tls_begin = 0; clear(); return error; }
        error = space_.map(tls_begin,tls_size,6,tls.initial.data(),tls.initial.size());
        if (error) { clear(); return error; }
        fs_base = tls_begin+tls.block_size;
        auto* tcb = const_cast<uint8_t*>(space_.translate(fs_base,48,6));
        // TCB: self, DTV pointer. DTV: generation, module-1 static pointer,
        // plus reserved zeros. No host pointer is stored in guest memory.
        const uint64_t words[] = {fs_base,fs_base+16,1,tls_begin,0,0};
        std::memcpy(tcb,words,sizeof(words));
        return 0;
    }
    void clear() {
        if (tls_begin) space_.release(tls_begin);
        if (stack_begin) space_.release(stack_begin);
        rsp = fs_base = tls_begin = stack_begin = 0; tls_size = stack_size = 0;
    }
private:
    GuestAddressSpace& space_;
};
}
