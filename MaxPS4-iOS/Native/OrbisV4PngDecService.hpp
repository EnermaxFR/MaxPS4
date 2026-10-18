#pragma once
// libScePngDec on the host: real PNG decoding (PngDecoder) into guest memory.
// Structure layouts follow the title's usage (Kero Blaster eboot+0xc6c30):
//   ParseParam  {const void* png; u32 size; u32 reserved}
//   ImageInfo   {u32 width, height; u16 colorSpace, bitDepth; u32 imageFlag}
//   CreateParam {u32 thisSize(12); u32 attribute(0: 8-bit, 1: 16-bit); u32 maxImageWidth}
//   DecodeParam {const void* png; void* image; u32 pngSize, imageSize; u16 pixelFormat, alpha; u32 pitch}
// Decoder work memory is owned by the title and only validated: decoding runs
// on the host. Every rejected call leaves guest output untouched.
#include "OrbisV4HostServices.hpp"
#include "PngDecoder.hpp"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>

namespace maxps4 {
struct OrbisV4PngDecService {
    using R = OrbisV4HostServices::Registers; using M = OrbisV4HostServices::Memory;
    using Result = OrbisV4HostServices::Result; using O = OrbisV4HostServices::Outcome;
    static constexpr uint32_t invalidAddr = 0x80690001, invalidSize = 0x80690002, invalidParam = 0x80690003,
        invalidHandle = 0x80690004, invalidWorkMemory = 0x80690005, invalidData = 0x80690010,
        unsupportedData = 0x80690011, decodeError = 0x80690012;
    static constexpr uint32_t maxPng = 16u << 20, maxWidth = 16384;
    struct Decoder { uint32_t attribute = 0, maxImageWidth = 0; };
    struct State {
        std::map<uint64_t, Decoder> decoders; // keyed by the title's work memory
        uint64_t decoded = 0, refused = 0; std::string lastError;
    };
    static Result value(uint64_t v) { return {O::Returned, v}; }
    static uint16_t colorSpace(const PngDecoder::Header& h) {
        switch (h.color) { case 0: return 2; case 2: return 3; case 3: return 4; case 4: return 18; default: return 19; }
    }
    static bool info(M& m, uint64_t at, const PngDecoder::Header& h) {
        uint8_t out[16] = {};
        const uint16_t space = colorSpace(h), depth = h.depth; const uint32_t flags = (h.interlace ? 1u : 0u) | (h.transparency ? 2u : 0u);
        std::memcpy(out, &h.width, 4); std::memcpy(out + 4, &h.height, 4); std::memcpy(out + 8, &space, 2);
        std::memcpy(out + 10, &depth, 2); std::memcpy(out + 12, &flags, 4);
        return !m.write_data(at, out, sizeof(out));
    }
    static bool readPng(M& m, uint64_t at, uint32_t size, std::vector<uint8_t>& bytes, uint32_t& error) {
        if (!at) { error = invalidAddr; return false; }
        if (size < 33 || size > maxPng) { error = invalidSize; return false; }
        bytes.resize(size);
        if (m.read_data(at, bytes.data(), size)) { error = invalidAddr; return false; }
        return true;
    }
    template <class Services> // anything with implement(name, handler)
    static std::shared_ptr<State> install(Services& services) {
        auto st = std::make_shared<State>();
        auto refuse = [st](uint32_t code, std::string why) {
            std::fprintf(stderr, "PNG.REFUSE code=0x%x %s\n", code, why.c_str());
            ++st->refused; st->lastError = std::move(why); return value(code); };
        // scePngDecParseHeader(const ParseParam*, ImageInfo*)
        services.implement("scePngDecParseHeader", [refuse](const R& r, M& m) {
            uint8_t p[16]; if (!r.rdi || !r.rsi || m.read_data(r.rdi, p, sizeof(p))) return refuse(invalidAddr, "parse parameters unreadable");
            uint64_t png; uint32_t size, reserved; std::memcpy(&png, p, 8); std::memcpy(&size, p + 8, 4); std::memcpy(&reserved, p + 12, 4);
            if (reserved) return refuse(invalidParam, "reserved field set");
            std::vector<uint8_t> bytes; uint32_t error = 0;
            if (!readPng(m, png, size, bytes, error)) return refuse(error, "PNG bytes unreadable");
            PngDecoder::Header h; std::string why;
            if (!PngDecoder::header(bytes.data(), bytes.size(), h, why)) return refuse(invalidData, why);
            if (!info(m, r.rsi, h)) return refuse(invalidAddr, "image info unwritable");
            return value(0); });
        auto createParam = [](M& m, uint64_t at, Decoder& d, uint32_t& error) {
            uint32_t p[3]; if (!at || m.read_data(at, p, sizeof(p))) { error = invalidAddr; return false; }
            if (p[0] != 12 || p[1] > 1 || !p[2] || p[2] > maxWidth) { error = invalidParam; return false; }
            d.attribute = p[1]; d.maxImageWidth = p[2]; return true;
        };
        auto workSize = [](const Decoder& d) { return uint32_t(0x10000 + d.maxImageWidth * (d.attribute ? 16 : 8)); };
        // scePngDecQueryMemorySize(const CreateParam*): bytes of work memory.
        services.implement("scePngDecQueryMemorySize", [refuse, createParam, workSize](const R& r, M& m) {
            Decoder d; uint32_t error = 0;
            if (!createParam(m, r.rdi, d, error)) return refuse(error, "invalid create parameters");
            return value(workSize(d)); });
        // scePngDecCreate(const CreateParam*, void* memory, u32 size, ScePngDecHandle* handle)
        services.implement("scePngDecCreate", [st, refuse, createParam, workSize](const R& r, M& m) {
            Decoder d; uint32_t error = 0;
            if (!createParam(m, r.rdi, d, error)) return refuse(error, "invalid create parameters");
            if (!r.rsi || !r.rcx) return refuse(invalidAddr, "null memory/handle");
            if (uint32_t(r.rdx) < workSize(d) || !m.permitted(r.rsi, uint32_t(r.rdx), 2)) return refuse(invalidWorkMemory, "work memory too small or unwritable");
            if (st->decoders.count(r.rsi)) return refuse(invalidWorkMemory, "work memory already holds a decoder");
            const uint64_t handle = r.rsi;
            if (m.write_data(r.rcx, &handle, 8)) return refuse(invalidAddr, "handle unwritable");
            st->decoders[handle] = d; return value(0); });
        services.implement("scePngDecDelete", [st, refuse](const R& r, M&) {
            if (!st->decoders.erase(r.rdi)) return refuse(invalidHandle, "unknown decoder");
            return value(0); });
        // scePngDecDecode(handle, const DecodeParam*, ImageInfo* optional)
        services.implement("scePngDecDecode", [st, refuse](const R& r, M& m) {
            auto it = st->decoders.find(r.rdi); if (it == st->decoders.end()) return refuse(invalidHandle, "unknown decoder");
            uint8_t p[32]; if (!r.rsi || m.read_data(r.rsi, p, sizeof(p))) return refuse(invalidAddr, "decode parameters unreadable");
            uint64_t png, image; uint32_t pngSize, imageSize, pitch; uint16_t format, alpha;
            std::memcpy(&png, p, 8); std::memcpy(&image, p + 8, 8); std::memcpy(&pngSize, p + 16, 4); std::memcpy(&imageSize, p + 20, 4);
            std::memcpy(&format, p + 24, 2); std::memcpy(&alpha, p + 26, 2); std::memcpy(&pitch, p + 28, 4);
            if (!image) return refuse(invalidAddr, "null image buffer");
            if (format > 1 || alpha > 255) return refuse(invalidParam, "pixel format/alpha");
            std::vector<uint8_t> bytes; uint32_t error = 0;
            if (!readPng(m, png, pngSize, bytes, error)) return refuse(error, "PNG bytes unreadable");
            PngDecoder::Header h; std::string why;
            if (!PngDecoder::header(bytes.data(), bytes.size(), h, why)) return refuse(invalidData, why);
            if (h.width > it->second.maxImageWidth) return refuse(unsupportedData, "image wider than the decoder's maximum");
            if (h.depth == 16 && !it->second.attribute) return refuse(unsupportedData, "16-bit image on an 8-bit decoder");
            const uint64_t row = uint64_t(h.width) * 4, stride = pitch ? pitch : row;
            if (stride < row || stride * (h.height - 1) + row > imageSize || !m.permitted(image, imageSize, 2))
                return refuse(invalidSize, "image buffer too small or unwritable");
            PngDecoder::Output out;
            if (!PngDecoder::decode(bytes.data(), bytes.size(), uint8_t(alpha), format == 1, out, why))
                return refuse(why.find("not implemented") != std::string::npos ? unsupportedData : decodeError, why);
            for (uint32_t y = 0; y < h.height; ++y)
                if (m.write_data(image + y * stride, out.rgba.data() + y * row, row)) return refuse(invalidAddr, "image row unwritable");
            if (r.rdx && !info(m, r.rdx, h)) return refuse(invalidAddr, "image info unwritable");
            ++st->decoded;
            if (std::getenv("MAXPS4_PNG_TRACE"))
                std::fprintf(stderr, "PNG.DECODE %ux%u color=%u depth=%u trns=%d format=%u alpha=%u pitch=%llu image=0x%llx\n", h.width, h.height,
                             h.color, h.depth, int(h.transparency), format, alpha, (unsigned long long)stride, (unsigned long long)image);
            return value(0); });
        return st;
    }
};
}
