#pragma once
#include "PS4ExecutableImage.hpp"
#include <string>

namespace maxps4 {
// PT_SCE_MODULE_PARAM (0x61000002), bounded profile observed on the clear
// libc.prx/libSceFios2.prx of CUSA06015 (docs/mission46-keroblaster-module-param.md).
// The header is a VIEW of bytes already owned by one file-backed, non-executable
// PT_LOAD: same file offset/address delta, never mapped, copied or zero-filled
// separately. Only the 24-byte layout without pointers is accepted:
//   +0 u64 size (=24)  +8 u32 magic 0x3C13F4BF  +12 u32 version (=1)
//   +16 u32 SDK version (recorded, no behaviour derived)  +20 u32 reserved (=0)
// Any other shape (pointers, larger structures, RELRO/text placement, writes by
// relocations, overlap with PT_TLS) is refused rather than guessed.
struct OrbisModuleParam {
    static constexpr uint32_t programType = 0x61000002, magic = 0x3C13F4BF, version = 1;
    static constexpr uint64_t size = 24;
    struct Value {
        bool present = false;
        uint64_t address = 0; // Module-relative guest address of the view.
        uint32_t version = 0, sdkVersion = 0;
    };
    static bool validate(const PS4ExecutableImage& image, Value& output, std::string& error) {
        auto fail = [&](const std::string& reason) { error = "PT_SCE_MODULE_PARAM: " + reason; return false; };
        const PS4ExecutableImage::Header* header = nullptr;
        for (const auto& h : image.headers) if (h.type == programType) {
            if (header) return fail("duplicate header");
            header = &h;
        }
        Value pending;
        if (!header) { output = pending; return true; }
        const auto& h = *header;
        if (h.flags != 4) return fail("read-only flags required");
        if (h.fileSize != size || h.memorySize != size) return fail("24-byte file-backed profile required");
        if (h.alignment != 8 || h.address % 8 || h.offset % 8) return fail("8-byte alignment required");
        if (h.address > UINT64_MAX - size) return fail("address overflow");
        const PS4ExecutableImage::Segment* owner = nullptr;
        for (const auto& s : image.segments) {
            if (h.address < s.header.address || h.address - s.header.address > s.header.memorySize) continue;
            if (h.address - s.header.address + size > s.header.memorySize) continue;
            if (owner) return fail("ambiguous containing segment");
            owner = &s;
        }
        if (!owner) return fail("outside loadable segments");
        const auto delta = h.address - owner->header.address;
        if (owner->header.type != 1) return fail("PT_LOAD owner required");
        if (owner->header.flags & 1) return fail("executable owner refused");
        if (delta + size > owner->header.fileSize) return fail("view must be file-backed");
        if (h.offset < owner->header.offset || h.offset - owner->header.offset != delta)
            return fail("file offset differs from owning PT_LOAD");
        for (const auto& t : image.headers) if (t.type == 7 && t.memorySize &&
            h.address < t.address + t.memorySize && t.address < h.address + size)
            return fail("overlaps PT_TLS");
        const auto* p = owner->bytes.data() + delta;
        auto read = [&](size_t offset, size_t width) {
            uint64_t value = 0;
            for (size_t i = 0; i < width; ++i) value |= uint64_t(p[offset + i]) << (8 * i);
            return value;
        };
        if (read(0, 8) != size) return fail("size field mismatch");
        if (read(8, 4) != magic) return fail("magic mismatch");
        if (read(12, 4) != version) return fail("unsupported version");
        if (read(20, 4)) return fail("reserved field not zero");
        pending.present = true; pending.address = h.address;
        pending.version = version; pending.sdkVersion = static_cast<uint32_t>(read(16, 4));
        output = pending;
        return true;
    }
};
}
