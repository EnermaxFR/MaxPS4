#pragma once
#include "PS4ExecutableImage.hpp"
#include <string>

namespace maxps4 {
// PT_GNU_EH_FRAME (0x6474e550): the .eh_frame_hdr binary-search table, bounded
// profile observed on the clear eboot.bin/libc.prx/libSceFios2.prx of CUSA06015
// (docs/mission47-keroblaster-elf-headers.md). Unwinding METADATA only: the
// header is a read-only VIEW inside one file-backed, non-writable PT_LOAD; it
// is never mapped, copied, relocated or registered with an unwinder here.
//   +0 u8 version=1  +1 eh_frame_ptr_enc=0x1b (pcrel|sdata4)
//   +2 fde_count_enc=0x03 (udata4)  +3 table_enc=0x3b (datarel|sdata4)
//   +4 eh_frame_ptr  +8 fde_count  +12 {initial_location, fde} * fde_count
// Only this encoding set is accepted. The table, FDE and CIE framing are
// bounds-checked; CIE augmentations/instructions are not interpreted.
struct OrbisEHFrameHeader {
    static constexpr uint32_t programType = 0x6474e550;
    static constexpr uint64_t maxEntries = uint64_t{1} << 20;
    struct Value {
        bool present = false;
        uint64_t address = 0, size = 0, ehFrame = 0; // Module-relative guest addresses.
        uint32_t fdeCount = 0;
        uint64_t firstLocation = 0, lastLocation = 0;
    };
    static bool validate(const PS4ExecutableImage& image, Value& output, std::string& error) {
        auto fail = [&](const std::string& reason) { error = "PT_GNU_EH_FRAME: " + reason; return false; };
        const PS4ExecutableImage::Header* header = nullptr;
        for (const auto& h : image.headers) if (h.type == programType) {
            if (header) return fail("duplicate header");
            header = &h;
        }
        Value pending;
        if (!header) { output = pending; return true; }
        const auto& h = *header;
        if (h.flags != 4) return fail("read-only flags required");
        if (h.fileSize != h.memorySize || h.fileSize < 12) return fail("file-backed table of at least 12 bytes required");
        if (h.alignment != 4 || h.address % 4 || h.offset % 4) return fail("4-byte alignment required");
        if (h.address > UINT64_MAX - h.fileSize) return fail("address overflow");
        const auto end = h.address + h.fileSize;
        const PS4ExecutableImage::Segment* owner = nullptr;
        for (const auto& s : image.segments)
            if (h.address >= s.header.address && end <= s.header.address + s.header.memorySize) owner = &s;
        if (!owner) return fail("outside loadable segments");
        if (owner->header.type != 1) return fail("PT_LOAD owner required");
        if ((owner->header.flags & 2) || !(owner->header.flags & 4)) return fail("readable non-writable owner required");
        const auto base = owner->header.address;
        if (end - base > owner->header.fileSize) return fail("view must be file-backed");
        if (h.offset < owner->header.offset || h.offset - owner->header.offset != h.address - base)
            return fail("file offset differs from owning PT_LOAD");
        // All reads below stay inside the owner's file-backed bytes.
        auto u32 = [&](uint64_t address) {
            const auto* p = owner->bytes.data() + (address - base);
            return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
        };
        auto relative = [&](uint64_t origin, uint64_t field, uint64_t& result) {
            const auto delta = int64_t(int32_t(u32(field)));
            if (delta < 0 ? uint64_t(-delta) > origin : uint64_t(delta) > UINT64_MAX - origin) return false;
            result = origin + uint64_t(delta);
            return true;
        };
        const auto* p = owner->bytes.data() + (h.address - base);
        if (p[0] != 1) return fail("unsupported version");
        if (p[1] != 0x1b || p[2] != 0x03 || p[3] != 0x3b) return fail("unsupported pointer encodings");
        uint64_t ehFrame = 0;
        // eh_frame_ptr is pc-relative to its own field; FDEs/CIEs lie in [eh_frame, header).
        if (!relative(h.address + 4, h.address + 4, ehFrame) || ehFrame < base || ehFrame >= h.address)
            return fail("eh_frame outside owner before header");
        const uint64_t count = u32(h.address + 8);
        if (!count || count > maxEntries || count > (h.fileSize - 12) / 8) return fail("FDE table count out of bounds");
        for (uint64_t a = h.address + 12 + count * 8; a < end; ++a)
            if (owner->bytes[size_t(a - base)]) return fail("non-zero bytes after table");
        uint64_t previous = 0;
        for (uint64_t i = 0; i < count; ++i) {
            const auto entry = h.address + 12 + i * 8;
            uint64_t location = 0, fde = 0;
            if (!relative(h.address, entry, location) || !relative(h.address, entry + 4, fde))
                return fail("table entry overflow");
            if (i && location <= previous) return fail("table not strictly sorted");
            previous = location;
            bool code = false;
            for (const auto& s : image.segments)
                code |= (s.header.flags & 1) && location >= s.header.address && location - s.header.address < s.header.fileSize;
            if (!code) return fail("initial location outside file-backed code");
            if (fde < ehFrame || fde > h.address - 8) return fail("FDE outside eh_frame");
            const uint64_t length = u32(fde);
            if (length < 8 || length == 0xffffffff || length > h.address - fde - 4) return fail("FDE length out of bounds");
            const uint64_t pointer = u32(fde + 4);
            if (!pointer || pointer > fde + 4 - ehFrame) return fail("CIE pointer out of bounds");
            const auto cie = fde + 4 - pointer;
            if (cie > fde - 8) return fail("CIE pointer out of bounds");
            const uint64_t cieLength = u32(cie);
            if (cieLength < 4 || cieLength == 0xffffffff || cieLength > fde - cie - 4) return fail("CIE length out of bounds");
            if (u32(cie + 4)) return fail("FDE does not reference a CIE");
            if (!i) pending.firstLocation = location;
        }
        pending.present = true; pending.address = h.address; pending.size = h.fileSize;
        pending.ehFrame = ehFrame; pending.fdeCount = uint32_t(count); pending.lastLocation = previous;
        output = pending;
        return true;
    }
};
}
