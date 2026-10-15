#pragma once
#include "maxps4_guest_tls.h"
#include "maxps4_ps4_linker.h"
#include <array>
#include <optional>
#include <cstdio>

namespace maxps4 {
// Initial M1 process: plaintext fixed-address ELF, one static TLS module,
// guarded stack/heap and the existing standard-table libkernel linker.
// General instruction admission/scheduling is separate from address mapping.
class OrbisM1 final {
public:
    // ASan reserves the low profile as shadow memory on Linux. Only the
    // sanitizer memory suite opts into the legacy high profile; CPU/IPA use 16 GiB.
#if defined(MAXPS4_M1_HIGH_VA_SANITIZER)
    static constexpr uint64_t thread_base = 0x300000400000ULL, heap_base = 0x300000800000ULL;
#else
    static constexpr uint64_t thread_base = 0x400400000ULL, heap_base = 0x400800000ULL;
#endif
    GuestAddressSpace space;
    GuestELF source;
    PS4DynamicLinker linker;
    GuestTLSTemplate tls;
    uint64_t entry = 0;
    explicit OrbisM1(GuestAddressSpace::Backend backend) : space(backend) {}
    int load(const uint8_t* file, size_t length, uint32_t budget) {
        space.clear(); source.clear(); linker.reset(); entry = 0;
        PS4ExecutableImage parsed;
        int error = parsed.load(file,length);
        if (error) return error;
        if (parsed.isSelf || parsed.type == 0xfe18) return -ENOTSUP;
        for (const auto& h : parsed.headers) if (h.type != 1 && h.type != 2 && h.type != 7) return -ENOTSUP;
        error = tls.load(parsed);
        if (error) return error;
        error = parsed.requiresLinker ? linker.load_m1(source,file,length,budget) : source.stage_ps4(file,length);
        if (error) return error;
        const size_t page = space.page_size();
        if (!page) return -ENOTSUP;
        uint64_t low = UINT64_MAX, high = 0;
        for (const auto& s : source.segments()) {
            if (s.address > UINT64_MAX-s.memory_size-page) return -EOVERFLOW;
            low = std::min(low,s.address/page*page);
            high = std::max(high,(s.address+s.memory_size+page-1)/page*page);
        }
        auto fail = [&](int code) { space.clear(); source.clear(); linker.reset(); return code; };
        if (high-low > GuestAddressSpace::budget) return fail(-E2BIG);
        error = space.reserve(low,high-low);
        if (error) {
            std::fprintf(stderr,"M1 guest image reservation 0x%llx..0x%llx declined: %d\n",
                (unsigned long long)low,(unsigned long long)high,error);
            return fail(error);
        }
        for (const auto& s : source.segments()) {
            const auto* bytes = source.translate(s.address,s.memory_size,4);
            if (!bytes) return fail(-EFAULT);
            error = space.map(s.address,s.memory_size,s.permissions,bytes,s.memory_size);
            if (error) return fail(error);
        }
        error = space.reserve(heap_base,0x10000);
        if (error) std::fprintf(stderr,"M1 guest heap reservation 0x%llx declined: %d\n",
            (unsigned long long)heap_base,error);
        if (!error) error = space.map(heap_base,0x10000,6);
        if (error) return fail(error);
        entry = parsed.entry;
        return 0;
    }
    // Sound, finite admission envelope: absolute/RIP/register loads and stores,
    // static FS TLS, balanced PUSH/POP and known GOT calls. No branches or raw
    // syscalls, arbitrary RSP writes, unknown address registers or code writes.
    int admit(const GuestThreadMemory& thread, uint32_t budget,
              const std::vector<GuestAddressSpace::Region>& dynamic_tls = {}) const {
        if (!entry || !budget || budget > 65536) return -EINVAL;
        size_t length = 0;
        for (const auto& s : source.segments()) if ((s.permissions & 1) && entry >= s.address &&
            entry-s.address < s.file_size) length = s.file_size-(entry-s.address);
        if (!length || length > 65536) return -ENOEXEC;
        const auto* code = space.translate(entry,length,5);
        if (!code) return -EFAULT;
        std::array<std::optional<uint64_t>,16> known{};
        for (auto& k : known) k = 0;
        size_t depth = 0;
        uint32_t instructions = 0;
        auto number = [](const uint8_t* p, size_t n) {
            uint64_t value = 0;
            for (size_t i=0;i<n;++i) value |= uint64_t(p[i])<<(8*i);
            return value;
        };
        auto target = [](uint64_t base, uint32_t raw, uint64_t& out) {
            const int64_t offset = raw & 0x80000000U ? int64_t(raw)-0x100000000LL : raw;
            if (offset < 0) { if (base < uint64_t(-offset)) return false; out = base-uint64_t(-offset); }
            else { if (base > UINT64_MAX-uint64_t(offset)) return false; out = base+uint64_t(offset); }
            return true;
        };
        auto access = [&](uint64_t address, size_t bytes, bool write) {
            if (!space.translate(address,bytes,write ? 2 : 4) ||
                (write && space.translate(address,bytes,1))) return false;
            if (write) for (const auto& i : linker.imports())
                if (address < i.slot+8 && i.slot < address+bytes) return false;
            // Keep stack return addresses/entry words and TCB/DTV immutable to
            // guest memory operands. PUSH/POP and generated CALL own that stack.
            if (write && address < thread.stack_begin+thread.stack_size &&
                thread.stack_begin < address+bytes) return false;
            if (write && thread.fs_base && address < thread.tls_begin+thread.tls_size &&
                thread.fs_base < address+bytes) return false;
            return true;
        };
        for (size_t at=0;at<length;) {
            if (++instructions > budget) return -E2BIG;
            const auto* p = code+at;
            const size_t left = length-at;
            size_t width = 0;
            if (p[0] == 0xf4) return left == 1 && !depth ? 0 : -ENOTSUP;
            if (p[0] == 0x90) width = 1;
            else if (p[0] >= 0xb8 && p[0] <= 0xbf && p[0] != 0xbc && left >= 5) {
                width = 5; known[p[0]-0xb8] = number(p+1,4);
            } else if ((p[0] == 0x48 || p[0] == 0x49) && left >= 10 && p[1] >= 0xb8 && p[1] <= 0xbf &&
                       !(p[0] == 0x48 && p[1] == 0xbc)) {
                width = 10; known[p[1]-0xb8+(p[0] == 0x49 ? 8 : 0)] = number(p+2,8);
            } else if (p[0] == 0x05 && left >= 5) { width = 5; known[0].reset(); }
            else if (p[0] == 0x50) { if (++depth > 128) return -E2BIG; width = 1; }
            else if (p[0] == 0x58) { if (!depth) return -ENOTSUP; --depth; width = 1; known[0].reset(); }
            else if (left >= 10 && p[0] == 0x48 && (p[1] == 0xa1 || p[1] == 0xa3)) {
                width = 10;
                if (!access(number(p+2,8),8,p[1] == 0xa3)) return -EFAULT;
                if (p[1] == 0xa1) known[0].reset();
            } else if (left >= 2 && (p[0] == 0x8b || p[0] == 0x89) && p[1] == 3) {
                width = 2;
                if (!known[3] || !access(*known[3],4,p[0] == 0x89)) return -EFAULT;
                if (p[0] == 0x8b) known[0].reset();
            } else if (left >= 3 && p[0] == 0x48 && (p[1] == 0x8b || p[1] == 0x89) && p[2] == 3) {
                width = 3;
                if (!known[3] || !access(*known[3],8,p[1] == 0x89)) return -EFAULT;
                if (p[1] == 0x8b) known[0].reset();
            } else if (p[0] == 0x64) {
                const bool wide = left >= 2 && p[1] == 0x48;
                const size_t k = wide ? 2 : 1;
                width = k+7;
                if (left < width || (p[k] != 0x8b && p[k] != 0x89) || p[k+1] != 4 || p[k+2] != 0x25 ||
                    !thread.fs_base) return -ENOTSUP;
                uint64_t address = 0;
                if (!target(thread.fs_base,number(p+k+3,4),address)) return -EFAULT;
                const size_t bytes = wide ? 8 : 4;
                bool payload = address >= thread.tls_begin && address-thread.tls_begin < tls.memory_size &&
                    bytes <= tls.memory_size-(address-thread.tls_begin);
                for (const auto& r : dynamic_tls) if (address >= r.address && address-r.address < r.size &&
                    bytes <= r.size-(address-r.address)) payload = true;
                if (!payload || !access(address,bytes,p[k] == 0x89)) return -EFAULT;
                if (p[k] == 0x8b) known[0].reset();
            } else if (left >= 7 && p[0] == 0x48 && (p[1] == 0x8b || p[1] == 0x89) && p[2] == 5) {
                width = 7; uint64_t address = 0;
                if (!target(entry+at+width,number(p+3,4),address) || !access(address,8,p[1] == 0x89)) return -EFAULT;
                if (p[1] == 0x8b) known[0].reset();
            } else if (left >= 6 && p[0] == 0xff && p[1] == 0x15) {
                width = 6; uint64_t slot = 0;
                if (depth || !target(entry+at+width,number(p+2,4),slot)) return -ENOTSUP;
                const PS4DynamicLinker::Import* import = nullptr;
                for (const auto& i : linker.imports()) if (i.slot == slot) import = &i;
                if (!import) return -ENOENT;
                switch (import->service) {
                case PS4KernelService::thread_self: break;
                case PS4KernelService::mutex_trylock: case PS4KernelService::mutex_unlock:
                    if (!known[7] || !access(*known[7],8,true)) return -EFAULT;
                    break;
                case PS4KernelService::clock_gettime: case PS4KernelService::clock_getres:
                    if (!known[6] || !access(*known[6],16,true)) return -EFAULT;
                    break;
                default: return -ENOTSUP;
                }
                if (budget-instructions < 4) return -E2BIG;
                instructions += 4;
                known[0].reset(); known[1].reset(); known[11].reset();
            } else return -ENOTSUP;
            at += width;
        }
        return -ENOTSUP;
    }
};
}

