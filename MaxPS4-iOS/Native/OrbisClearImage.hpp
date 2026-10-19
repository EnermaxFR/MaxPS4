#pragma once
#include "PS4ExecutableImage.hpp"
#include <algorithm>
#include <string>

namespace maxps4 {
// Canonical bytes in ordinary vectors only. Explicit unauthenticated inspection
// policy, shared with the existing experimental preparation; never CPU admission.
// Refuses encryption/compression and unknown or unbacked SELF layouts.
struct OrbisClearImage {
    enum class Purpose { Preparation, Inspection };
    static int normalize(const uint8_t* bytes,size_t size,std::vector<uint8_t>& output,std::string& reason,
                         Purpose purpose = Purpose::Preparation) {
        output.clear();
        auto fail=[&](const std::string& text) {reason="SELF.EXPERIMENTAL: "+text;return -ENOTSUP;};
        if (!bytes || size<64 || size>32*1024*1024) return fail("input size outside budget");
        PS4ExecutableImage image;
        const int status=image.load(bytes,size,PS4ExecutableImage::SelfPolicy::StageSignedClear);
        if (status) return fail(std::string(image.stage)+" index="+std::to_string(image.diagnosticIndex)+" status="+std::to_string(status));
        if (!image.isSelf) {
            output.assign(bytes,bytes+size);
            reason="ELF.CLEAR: original bytes preserved; metadata/linker/CPU admission still required";
            return 0;
        }
        auto read=[](const uint8_t* p,size_t n) {uint64_t v=0; for(size_t i=0;i<n;++i)v|=uint64_t(p[i])<<(8*i); return v;};
        const size_t blocks=read(bytes+24,2), origin=32+blocks*32;
        const auto* elf=bytes+origin;
        const size_t phoff=read(elf+32,8), count=read(elf+56,2);
        if (read(elf+40,8) || read(elf+60,2) || read(elf+62,2))
            return fail("runtime SELF section tables unsupported; no invented section bytes");
        size_t extent=phoff+count*56;
        for (const auto& h:image.headers) {
            if (h.offset>32*1024*1024 || h.fileSize>32*1024*1024-h.offset)
                return fail("logical ELF range exceeds 32 MiB");
            if (h.fileSize) extent=std::max(extent,size_t(h.offset+h.fileSize));
        }
        std::vector<uint8_t> pending(extent,0), covered(extent,0);
        auto copy=[&](size_t offset,const uint8_t* source,size_t length) {
            if (offset>extent || length>extent-offset) return false;
            for (size_t i=0;i<length;++i) {
                if (covered[offset+i] && pending[offset+i]!=source[i]) return false;
                pending[offset+i]=source[i]; covered[offset+i]=1;
            }
            return true;
        };
        if (!copy(0,elf,64) || !copy(phoff,elf+phoff,count*56)) return fail("header overlap");
        for (size_t i=0;i<blocks;++i) {
            const auto* block=bytes+32+i*32; const auto flags=read(block,8);
            if (!(flags&0x800)) continue; // Opaque digest bytes are never code/data.
            const auto& h=image.headers[(flags>>20)&0xfff];
            if (!copy(size_t(h.offset),bytes+read(block+8,8),size_t(h.fileSize)))
                return fail("conflicting logical ELF ranges");
        }
        const size_t declared=read(bytes+16,4);
        if (declared<size) {
            for (const auto& h:image.headers) if (h.type==0x6fffff01)
                if (!copy(size_t(h.offset),bytes+declared,size-declared)) return fail("version trailer conflict");
        }
        for (const auto& h:image.headers)
            for (size_t j=0;j<h.fileSize;++j)
                if (!covered[size_t(h.offset)+j]) return fail("program header has unbacked file bytes");
        // RELRO relocation writes occur only in offline vectors. The existing
        // linker seals SCE RELRO read-only before any V4 mapping is published.
        for (size_t i=0;i<image.headers.size();++i) {
            const auto& h=image.headers[i];
            if (h.type!=0x61000010) continue;
            if (h.flags!=4 && h.flags!=6) return fail("RELRO must be readable and non-executable");
            const size_t flagOffset=phoff+i*56+4;
            if (h.flags!=6) for (const auto& segment:image.segments)
                if (flagOffset>=segment.header.offset && flagOffset-segment.header.offset<segment.header.fileSize)
                    return fail("RELRO header flag aliases loadable bytes; cannot alter code/data");
            if (purpose==Purpose::Preparation) pending[flagOffset]=6;
        }
        output=std::move(pending);
        reason="SELF.EXPERIMENTAL: clear bytes structurally checked; signature unverified and not required; no decryption";
        return 0;
    }
};
}
