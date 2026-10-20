#pragma once
#include "PS4ExecutableImage.hpp"
#include <string>
#include <algorithm>

namespace maxps4 {
// Adapter to the existing linker, not a linker or decryption path. Only strict,
// unsigned static SELF is admitted. Canonical ELF exists in memory for one
// preparation transaction; code bytes/VA/permissions/BSS remain identical.
struct OrbisV4SELF {
    static int adapt(const uint8_t* bytes,size_t size,std::vector<uint8_t>& output,std::string& reason) {
        output.clear();
        auto refuse=[&](const std::string& text) {reason="V4.SELF: "+text; return -ENOTSUP;};
        PS4ExecutableImage image;
        const int status=image.load(bytes,size); // Strict: no signed/opaque profile.
        if (status) return refuse(std::string(image.stage)+" strict clear SELF required");
        if (!image.isSelf || image.type!=0xfe00 || image.requiresTLS || image.requiresLinker)
            return refuse("unsigned static ET_SCE_EXEC only; no dynamic linkage/TLS");
        for (const auto& h:image.headers)
            if (h.type!=1) return refuse("only PT_LOAD headers supported by static SELF adapter");
        const size_t origin=32+size_t(bytes[24]|unsigned(bytes[25])<<8)*32;
        std::vector<uint8_t> pending(64+image.segments.size()*56,0);
        std::copy(bytes+origin,bytes+origin+64,pending.begin());
        auto put=[&](size_t offset,uint64_t value,size_t width=8) {
            for (size_t j=0;j<width;++j) pending.at(offset+j)=uint8_t(value>>(8*j));
        };
        put(32,64); put(40,0); put(58,0,2); put(60,0,2); put(62,0,2);
        put(56,image.segments.size(),2);
        for (size_t i=0;i<image.segments.size();++i) {
            const auto& segment=image.segments[i]; const auto& h=segment.header;
            if ((h.flags&3)==3 || h.alignment>0x4000) return refuse("W+X or alignment beyond 16 KiB");
            const uint64_t align=std::max<uint64_t>(h.alignment,1);
            const uint64_t padding=(h.address%align+align-pending.size()%align)%align;
            const uint64_t offset=pending.size()+padding;
            if (offset>32*1024*1024 || h.fileSize>32*1024*1024-offset)
                return refuse("canonical ELF exceeds 32 MiB budget");
            pending.resize(size_t(offset+h.fileSize),0);
            std::copy_n(segment.bytes.begin(),size_t(h.fileSize),pending.begin()+size_t(offset));
            const auto p=64+i*56;
            put(p,1,4); put(p+4,h.flags,4); put(p+8,offset); put(p+16,h.address);
            put(p+24,0); put(p+32,h.fileSize); put(p+40,h.memorySize); put(p+48,h.alignment);
        }
        output=std::move(pending);
        reason="V4.SELF: strict static adapter; bytes/VA/permissions preserved; existing linker required";
        return 0;
    }
};
}
