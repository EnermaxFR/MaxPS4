#pragma once
#include "PS4ExecutableImage.hpp"
#include <string>

namespace maxps4 {
// PT_SCE_PROCPARAM (0x61000001) and PT_INTERP (3), bounded profiles observed
// on the clear eboot.bin of CUSA06015 (docs/mission49-keroblaster-symbols.md).
// Both are VIEWS of bytes already owned by one file-backed PT_LOAD: never
// mapped separately, never executed, and the interpreter is never opened.
// PROCPARAM, 72 bytes:
//   +0 u64 size (=72)  +8 u32 magic "ORBI"  +12 u32 version (=2)
//   +16 u32 SDK version (recorded only)  +20..+55 zero
//   +56 / +64 u64 pointers: zero, or written by a RELATIVE relocation whose
//   target lies in file-backed, non-executable module data (checked by the
//   linker). Their pointees are NOT interpreted here.
struct OrbisProcessParam {
    static constexpr uint32_t programType = 0x61000001, magic = 0x4942524f, version = 2;
    static constexpr uint64_t size = 72, firstPointer = 56, pointerCount = 2;
    struct Value {
        bool present = false;
        uint64_t address = 0; // Module-relative.
        uint32_t sdkVersion = 0;
        std::string interpreter; // PT_INTERP path, informational only.
    };
    static const PS4ExecutableImage::Segment* owner(const PS4ExecutableImage& image, const PS4ExecutableImage::Header& h) {
        for (const auto& s : image.segments)
            if (s.header.type == 1 && h.address >= s.header.address && h.fileSize <= s.header.fileSize &&
                h.address - s.header.address <= s.header.fileSize - h.fileSize &&
                h.offset >= s.header.offset && h.offset - s.header.offset == h.address - s.header.address) return &s;
        return nullptr;
    }
    static bool validate(const PS4ExecutableImage& image, Value& output, std::string& error) {
        auto fail = [&](const std::string& reason) { error = reason; return false; };
        const PS4ExecutableImage::Header *param = nullptr, *interp = nullptr;
        for (const auto& h : image.headers) {
            if (h.type == programType) { if (param) return fail("PT_SCE_PROCPARAM: duplicate header"); param = &h; }
            if (h.type == 3) { if (interp) return fail("PT_INTERP: duplicate header"); interp = &h; }
        }
        Value pending;
        if ((param || interp) && image.type != 0xfe10 && image.type != 0xfe00)
            return fail("PT_SCE_PROCPARAM/PT_INTERP: executable module required");
        if (param) {
            const auto& h = *param;
            if (h.flags != 4) return fail("PT_SCE_PROCPARAM: read-only flags required");
            if (h.fileSize != size || h.memorySize != size) return fail("PT_SCE_PROCPARAM: 72-byte file-backed profile required");
            if (h.alignment != 8 || h.address % 8 || h.offset % 8) return fail("PT_SCE_PROCPARAM: 8-byte alignment required");
            const auto* s = owner(image, h);
            if (!s) return fail("PT_SCE_PROCPARAM: not a file-backed view of one PT_LOAD");
            if (s->header.flags & 1) return fail("PT_SCE_PROCPARAM: executable owner refused");
            const auto* p = s->bytes.data() + (h.address - s->header.address);
            auto read = [&](size_t offset, size_t width) {
                uint64_t v = 0; for (size_t i = 0; i < width; ++i) v |= uint64_t(p[offset + i]) << (8 * i); return v;
            };
            if (read(0, 8) != size) return fail("PT_SCE_PROCPARAM: size field mismatch");
            if (read(8, 4) != magic) return fail("PT_SCE_PROCPARAM: magic mismatch");
            if (read(12, 4) != version) return fail("PT_SCE_PROCPARAM: unsupported version");
            for (size_t i = 20; i < firstPointer; ++i) if (p[i]) return fail("PT_SCE_PROCPARAM: reserved bytes not zero");
            for (const auto& t : image.headers) if (t.type == 7 && t.memorySize &&
                h.address < t.address + t.memorySize && t.address < h.address + size) return fail("PT_SCE_PROCPARAM: overlaps PT_TLS");
            pending.present = true; pending.address = h.address; pending.sdkVersion = uint32_t(read(16, 4));
        }
        if (interp) {
            const auto& h = *interp;
            if (h.flags != 4 || h.fileSize != h.memorySize || h.fileSize < 2 || h.fileSize > 256)
                return fail("PT_INTERP: read-only 2..256 byte string required");
            const auto* s = owner(image, h);
            if (!s) return fail("PT_INTERP: not a file-backed view of one PT_LOAD");
            const auto* p = s->bytes.data() + (h.address - s->header.address);
            if (p[h.fileSize - 1] || p[0] != '/') return fail("PT_INTERP: absolute NUL-terminated path required");
            for (size_t i = 0; i + 1 < h.fileSize; ++i)
                if (p[i] < 0x21 || p[i] > 0x7e) return fail("PT_INTERP: printable path required");
            pending.interpreter.assign(reinterpret_cast<const char*>(p), size_t(h.fileSize - 1));
        }
        output = std::move(pending);
        return true;
    }
};
}
