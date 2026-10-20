#pragma once
// PS4 ELF/SELF layouts follow shadPS4 elf.h/elf.cpp at
// 443f022450f6857256e780daa3d5a14029a748f7 (GPL-2.0-or-later).
// Plain file-backed segment staging only: no decryption, relocations, HLE,
// executable host memory or integration with the restricted FEX loader.
#include <cstdint>
#include <cstddef>
#include <vector>
#include <cstring>
#include <cerrno>
#include <new>
#include <utility>

namespace maxps4 {
class PS4ExecutableImage {
public:
    enum class SelfPolicy { Strict, StageSignedClear };
    struct Header {
        uint32_t type, flags;
        uint64_t offset, address, fileSize, memorySize, alignment;
    };
    struct Segment { Header header; std::vector<uint8_t> bytes; };
    static constexpr size_t memoryBudget = 32 * 1024 * 1024;
    // Retained on failure without retaining any staged bytes or metadata.
    const char* stage = "ELF.INPUT";
    size_t diagnosticIndex = 0;
    uint64_t entry = 0;
    uint16_t type = 0;
    bool isSelf = false, requiresLinker = false, requiresTLS = false;
    bool signedSelfUnverified = false; // Inspection only, never signature trust.
    std::vector<Header> headers; // Retain metadata; staging never applies it.
    std::vector<Segment> segments;

    const uint8_t* translate(uint64_t address, size_t count, uint32_t permissions) const {
        if (!count || (permissions & ~7U)) return nullptr;
        for (const auto& s : segments) {
            if (address < s.header.address || (s.header.flags & permissions) != permissions) continue;
            const uint64_t offset = address - s.header.address;
            if (offset <= s.bytes.size() && count <= s.bytes.size() - offset)
                return s.bytes.data() + offset;
        }
        return nullptr;
    }
    int load(const uint8_t* file, size_t size, SelfPolicy policy=SelfPolicy::Strict) {
        *this = PS4ExecutableImage{};
        if (!file || size < 64 || size > 64 * 1024 * 1024) return -EINVAL;
        try {
            PS4ExecutableImage pending;
            const int error = pending.parse(file, size, policy);
            if (error) { stage = pending.stage; diagnosticIndex = pending.diagnosticIndex; return error; }
            *this = std::move(pending); // No partial image escapes a rejected file.
            return 0;
        } catch (const std::bad_alloc&) { stage = "ELF.MEMORY"; return -ENOMEM; }
    }
private:
    static uint64_t read(const uint8_t* p, size_t width) {
        uint64_t value = 0;
        for (size_t i = 0; i < width; ++i) value |= uint64_t(p[i]) << (8 * i);
        return value;
    }
    static bool range(uint64_t offset, uint64_t count, size_t size) {
        return offset <= size && count <= size - offset;
    }
    // Some SELF producers append PT_SCE_LIBVERSION records after file_size.
    // Accept only the explicitly described, bounded non-loadable trailer. It
    // never extends a block's source range or enters staged guest memory.
    static bool selfTrailer(const uint8_t* file,size_t size,size_t declared,
                            size_t origin,size_t headerEnd) {
        if (size-declared>1024*1024) return false;
        const auto* elf=file+origin;
        if (std::memcmp(elf,"\177ELF\2\1\1\11\0",9) || read(elf+54,2)!=56) return false;
        const uint64_t table=read(elf+32,8), count=read(elf+56,2);
        if (table<64 || !count || count>128 ||
            !range(table,count*56,headerEnd-origin)) return false;
        unsigned found=0;
        uint64_t versionOffset=0, logicalEnd=0;
        for (size_t i=0;i<count;++i) {
            const auto* h=elf+table+i*56;
            const uint64_t offset=read(h+8,8), bytes=read(h+32,8);
            if (offset>UINT64_MAX-bytes) return false;
            if (read(h,4)!=0x6fffff01) {
                if (bytes && offset+bytes>logicalEnd) logicalEnd=offset+bytes;
                continue;
            }
            versionOffset=offset;
            if (++found!=1 || read(h+4,4)!=0 || read(h+32,8)!=size-declared ||
                read(h+40,8)!=0) return false;
        }
        if (found!=1 || versionOffset<logicalEnd) return false;
        for (size_t cursor=declared;cursor<size;) {
            const size_t length=file[cursor++];
            if (length<6 || length>size-cursor || file[cursor+length-5]!=':') return false;
            for (size_t j=0;j<length-5;++j)
                if (file[cursor+j]<0x21 || file[cursor+j]>0x7e) return false;
            cursor+=length; // Length-prefixed name ':' and four version bytes.
        }
        return true;
    }
    struct SelfSegment { uint64_t flags, offset, size, decodedSize; };
    int parse(const uint8_t* file, size_t size, SelfPolicy policy) {
        size_t origin = 0, payloadStart = 0, selfSize = size, selfHeaderEnd = 0;
        std::vector<SelfSegment> selfSegments;
        isSelf = read(file, 4) == 0x1d3d154f;
        stage = "ELF.CONTAINER";
        if (!isSelf && std::memcmp(file, "\177ELF", 4)) return -ENOEXEC;
        if (isSelf) {
            stage = "SELF.HEADER";
            if (file[4] != 0 || file[5] != 1 || file[6] != 1 || file[7] != 0x12 ||
                file[8] != 1 || file[9] != 1) return -ENOEXEC;
            const size_t count = read(file + 24, 2);
            if (!count || count > 128 || count > (size - 32) / 32) return -ENOEXEC;
            origin = 32 + count * 32;
            if (!range(origin, 64, size)) return -ENOEXEC;
            selfHeaderEnd = read(file + 12, 2);
            payloadStart = selfHeaderEnd + read(file + 14, 2);
            selfSize = read(file + 16, 4); // u32 SELF extent, not necessarily host EOF.
            if (selfHeaderEnd < origin + 64 || selfHeaderEnd > size || payloadStart > selfSize || selfSize > size)
                return -ENOEXEC;
            if (selfSize != size) {
                stage = "SELF.TRAILER";
                if (!selfTrailer(file,size,selfSize,origin,selfHeaderEnd)) return -ENOEXEC;
            }
            stage = "SELF.BLOCK";
            for (size_t i = 0; i < count; ++i) {
                diagnosticIndex = i;
                const auto* p = file + 32 + i * 32;
                SelfSegment s {read(p, 8), read(p + 8, 8), read(p + 16, 8), read(p + 24, 8)};
                // Signature is not encryption. The opt-in policy stages only
                // untrusted clear bytes for inspection, never dispatch/trust.
                if (s.flags & 0x2U) { stage="SELF.ENCRYPTED"; return -ENOTSUP; }
                if (s.flags & 0x8U) { stage="SELF.COMPRESSED_UNSUPPORTED"; return -ENOTSUP; }
                const uint64_t properties=s.flags & 0xfffff;
                if (s.flags >> 32) return -ENOTSUP;
                if (policy==SelfPolicy::Strict) {
                    if (s.flags & ~(uint64_t{0xfff} << 20 | uint64_t{0x801})) return -ENOTSUP;
                } else {
                    const bool legacy=(properties & ~uint64_t{0x805})==0 &&
                        (!(properties&4) || (properties&0x800));
                    if (!legacy && properties!=0x2804 && properties!=0x10004) return -ENOTSUP;
                    signedSelfUnverified |= (s.flags&4)!=0;
                }
                if (!range(s.offset, s.size, selfSize) || (s.size && s.offset < payloadStart))
                    return -ENOEXEC;
                for (const auto& old:selfSegments)
                    if (s.size && old.size && s.offset<old.offset+old.size && old.offset<s.offset+s.size)
                        return -ENOEXEC;
                selfSegments.push_back(s);
            }
        }
        stage = "ELF.IDENTITY"; diagnosticIndex = 0;
        const auto* elf = file + origin;
        if (std::memcmp(elf, "\177ELF\2\1\1\11\0", 9) ||
            read(elf + 18, 2) != 62 || read(elf + 20, 4) != 1 ||
            read(elf + 52, 2) != 64 || read(elf + 54, 2) != 56) return -ENOEXEC;
        for (size_t i = 9; i < 16; ++i) if (elf[i]) return -ENOEXEC;
        if (read(elf + 48, 4)) return -ENOEXEC; // No x86-64 processor flags supported.
        stage = "ELF.TYPE";
        type = static_cast<uint16_t>(read(elf + 16, 2));
        if (type != 0xfe00 && type != 0xfe10 && type != 0xfe18) return -ENOEXEC;
        entry = read(elf + 24, 8);
        stage = "ELF.PROGRAM_TABLE";
        const uint64_t table = read(elf + 32, 8), count = read(elf + 56, 2);
        if (table < 64 || !count || count > 128 ||
            !range(table, count * 56, size - origin)) return -ENOEXEC;
        if (isSelf && origin + table + count * 56 > selfHeaderEnd) return -ENOEXEC;
        stage = "ELF.SECTION_TABLE";
        const uint64_t shoff = read(elf + 40, 8), shnum = read(elf + 60, 2);
        if (shnum && (read(elf + 58, 2) != 64 || shnum > 4096 ||
            !range(shoff, shnum * 64, size - origin))) return -ENOEXEC;
        const uint64_t shstr = read(elf + 62, 2);
        if ((!shnum && (shoff || (read(elf + 58, 2) != 0 && read(elf + 58, 2) != 64) || shstr)) ||
            (shnum && (shoff < 64 || shstr >= shnum))) return -ENOEXEC;
        stage = "SELF.BLOCK_ID";
        for (const auto& s : selfSegments)
            if ((s.flags & 0x800) && ((s.flags >> 20) & 0xfff) >= count) return -ENOEXEC;
        if (isSelf && policy==SelfPolicy::StageSignedClear) {
            // Two distinct IDs: data blocks reference ELF PH indices; digest
            // tables reference SELF descriptor indices. Never map digest bytes.
            std::vector<unsigned> digests(selfSegments.size(),0);
            std::vector<bool> usedPH(count,false);
            for (size_t i=0;i<selfSegments.size();++i) {
                diagnosticIndex=i; stage="SELF.ASSOCIATION";
                const auto& block=selfSegments[i];
                const auto properties=block.flags&0xfffff;
                const size_t id=(block.flags>>20)&0xfff;
                if (properties==0x10004) {
                    if (id>=selfSegments.size() || ++digests[id]!=1) return -ENOEXEC;
                    const auto& data=selfSegments[id];
                    if ((data.flags&0xfffff)!=0x2804 || !data.size ||
                        block.size!=block.decodedSize ||
                        block.size!=((data.size+0x3fff)/0x4000)*32 || (block.offset&15))
                        return -ENOEXEC;
                } else if (block.flags&0x800) {
                    if (usedPH[id]) return -ENOEXEC;
                    usedPH[id]=true;
                    const auto* h=elf+table+id*56;
                    if (block.size!=block.decodedSize || block.size!=read(h+32,8)) return -ENOEXEC;
                    if (properties==0x2804 && (block.offset&15)) return -ENOEXEC;
                }
            }
            for (size_t i=0;i<selfSegments.size();++i) {
                diagnosticIndex=i;
                if ((selfSegments[i].flags&0xfffff)==0x2804 && digests[i]!=1) return -ENOEXEC;
            }
        }
        size_t allocated = 0;
        for (size_t i = 0; i < count; ++i) {
            stage = "ELF.SEGMENT"; diagnosticIndex = i;
            const auto* p = elf + table + i * 56;
            Header h {static_cast<uint32_t>(read(p, 4)), static_cast<uint32_t>(read(p + 4, 4)),
                read(p + 8, 8), read(p + 16, 8), read(p + 32, 8), read(p + 40, 8), read(p + 48, 8)};
            headers.push_back(h);
            requiresLinker |= h.type == 2 || h.type == 0x60000000 || h.type == 0x61000000;
            requiresTLS |= h.type == 7;
            if (!isSelf && h.fileSize && !range(h.offset, h.fileSize, size)) return -ENOEXEC;
            if (h.type != 1 && h.type != 0x61000010) continue; // Metadata retained, not executed.
            if (!h.memorySize || h.fileSize > h.memorySize || h.memorySize > memoryBudget - allocated ||
                h.address > UINT64_MAX - h.memorySize || (h.flags & ~7U) || !(h.flags & 4U) ||
                (isSelf && policy==SelfPolicy::StageSignedClear && (h.flags&3U)==3U) ||
                (h.alignment > 1 && ((h.alignment & (h.alignment - 1)) ||
                    h.address % h.alignment != h.offset % h.alignment))) return -ENOEXEC;
            for (const auto& old : segments)
                if (h.address < old.header.address + old.header.memorySize &&
                    old.header.address < h.address + h.memorySize) return -ENOEXEC;
            uint64_t source = h.offset;
            if (isSelf && h.fileSize) {
                stage = "SELF.SEGMENT_SOURCE";
                const SelfSegment* match = nullptr;
                for (const auto& s : selfSegments)
                    if ((s.flags & 0x800) && ((s.flags >> 20) & 0xfff) == i) {
                        if (match) return -ENOEXEC;
                        match = &s;
                    }
                if (!match || match->size != h.fileSize || match->decodedSize != h.fileSize)
                    return -ENOEXEC;
                source = match->offset; // SELF block ID identifies the ELF program header.
            }
            if (h.fileSize && !range(source, h.fileSize, size)) return -ENOEXEC;
            Segment segment {h, std::vector<uint8_t>(static_cast<size_t>(h.memorySize), 0)};
            if (h.fileSize) std::memcpy(segment.bytes.data(), file + source, static_cast<size_t>(h.fileSize));
            allocated += static_cast<size_t>(h.memorySize);
            segments.push_back(std::move(segment));
        }
        stage = "ELF.LOADABLE"; diagnosticIndex = 0;
        if (segments.empty()) return -ENOEXEC;
        // Shared libraries may have no entry. Executables require file-backed code.
        stage = "ELF.ENTRY";
        if (type != 0xfe18) {
            bool executableEntry = false;
            for (const auto& s : segments)
                executableEntry |= (s.header.flags & 1) && entry >= s.header.address &&
                    entry - s.header.address < s.header.fileSize;
            if (!executableEntry) return -ENOEXEC;
        }
        requiresLinker |= type != 0xfe00;
        stage = "ELF.COMPLETE";
        return 0;
    }
};
}
