#pragma once
// Host PNG decoder used by the libScePngDec service. Dependency-free (zlib
// inflate, filters, palette/tRNS), bounded, never reads outside its input.
// Non-interlaced colour types 0/2/3/4/6 at their legal bit depths are decoded;
// Adam7 interlacing is refused explicitly.
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace maxps4 {
struct PngDecoder {
    struct Header {
        uint32_t width = 0, height = 0; uint8_t depth = 0, color = 0, interlace = 0;
        bool transparency = false; // tRNS present
    };
    // Output RGBA8 (bgra swaps R/B). Images without alpha use 'alpha'.
    struct Output { std::vector<uint8_t> rgba; Header header; };

    static uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

    // --- inflate (RFC 1950/1951) ---------------------------------------
    struct Bits {
        const uint8_t* data; size_t size, pos = 0; uint32_t bit = 0, count = 0;
        bool need(unsigned n) {
            while (count < n) { if (pos >= size) return false; bit |= uint32_t(data[pos++]) << count; count += 8; }
            return true;
        }
        bool get(unsigned n, uint32_t& v) {
            if (n == 0) { v = 0; return true; }
            if (!need(n)) return false;
            v = bit & ((1u << n) - 1); bit >>= n; count -= n; return true;
        }
    };
    struct Huffman { // canonical code, decoded bit by bit (lengths <= 15)
        std::array<uint16_t, 16> counts{}; std::vector<uint16_t> symbols;
        bool build(const uint8_t* lengths, size_t n) {
            counts.fill(0); for (size_t i = 0; i < n; ++i) ++counts[lengths[i]];
            counts[0] = 0; int left = 1;
            for (int len = 1; len < 16; ++len) { left <<= 1; left -= counts[len]; if (left < 0) return false; }
            std::array<uint16_t, 16> offsets{}; for (int len = 1; len < 15; ++len) offsets[len + 1] = offsets[len] + counts[len];
            symbols.assign(n, 0); for (size_t i = 0; i < n; ++i) if (lengths[i]) symbols[offsets[lengths[i]]++] = uint16_t(i);
            return true;
        }
        bool decode(Bits& in, uint32_t& symbol) const {
            int code = 0, first = 0, index = 0;
            for (int len = 1; len < 16; ++len) {
                uint32_t b; if (!in.get(1, b)) return false;
                code |= int(b); const int count = counts[len];
                if (code - count < first) { symbol = symbols[size_t(index + (code - first))]; return true; }
                index += count; first += count; first <<= 1; code <<= 1;
            }
            return false;
        }
    };
    static bool inflate(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t limit, std::string& why) {
        if (size < 6) { why = "zlib stream too short"; return false; }
        if ((data[0] & 15) != 8 || (uint32_t(data[0]) << 8 | data[1]) % 31 || (data[1] & 0x20)) { why = "unsupported zlib header"; return false; }
        Bits in{data + 2, size - 2};
        static constexpr uint16_t lengthBase[] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
        static constexpr uint8_t lengthExtra[] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
        static constexpr uint16_t distBase[] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
        static constexpr uint8_t distExtra[] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
        uint32_t final = 0;
        do {
            uint32_t type; if (!in.get(1, final) || !in.get(2, type)) { why = "truncated deflate block header"; return false; }
            if (type == 0) {
                in.bit = 0; in.count = 0; // byte-align (whole bytes already consumed)
                if (in.size - in.pos < 4) { why = "truncated stored block"; return false; }
                const uint32_t len = in.data[in.pos] | uint32_t(in.data[in.pos + 1]) << 8, nlen = in.data[in.pos + 2] | uint32_t(in.data[in.pos + 3]) << 8;
                in.pos += 4;
                if ((len ^ 0xffff) != nlen || in.size - in.pos < len) { why = "invalid stored block"; return false; }
                if (out.size() + len > limit) { why = "decompressed data exceeds image size"; return false; }
                out.insert(out.end(), in.data + in.pos, in.data + in.pos + len); in.pos += len; continue;
            }
            if (type == 3) { why = "reserved deflate block type"; return false; }
            Huffman lit, dist;
            if (type == 1) {
                uint8_t l[288], d[30];
                for (int i = 0; i < 288; ++i) l[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
                std::memset(d, 5, sizeof(d)); lit.build(l, 288); dist.build(d, 30);
            } else {
                uint32_t hlit, hdist, hclen;
                if (!in.get(5, hlit) || !in.get(5, hdist) || !in.get(4, hclen)) { why = "truncated dynamic header"; return false; }
                hlit += 257; hdist += 1; hclen += 4;
                if (hlit > 286 || hdist > 30) { why = "invalid dynamic code counts"; return false; }
                static constexpr uint8_t order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
                uint8_t cl[19] = {}; for (uint32_t i = 0; i < hclen; ++i) { uint32_t v; if (!in.get(3, v)) { why = "truncated code lengths"; return false; } cl[order[i]] = uint8_t(v); }
                Huffman clh; if (!clh.build(cl, 19)) { why = "invalid code-length code"; return false; }
                uint8_t lengths[316] = {}; uint32_t n = 0;
                while (n < hlit + hdist) {
                    uint32_t s; if (!clh.decode(in, s)) { why = "invalid code-length symbol"; return false; }
                    if (s < 16) { lengths[n++] = uint8_t(s); continue; }
                    uint32_t repeat, value = 0;
                    if (s == 16) { if (!n || !in.get(2, repeat)) { why = "invalid length repeat"; return false; } repeat += 3; value = lengths[n - 1]; }
                    else if (s == 17) { if (!in.get(3, repeat)) { why = "truncated zero run"; return false; } repeat += 3; }
                    else { if (!in.get(7, repeat)) { why = "truncated zero run"; return false; } repeat += 11; }
                    if (n + repeat > hlit + hdist) { why = "code lengths overflow"; return false; }
                    while (repeat--) lengths[n++] = uint8_t(value);
                }
                if (!lengths[256]) { why = "missing end-of-block code"; return false; }
                if (!lit.build(lengths, hlit) || !dist.build(lengths + hlit, hdist)) { why = "over-subscribed Huffman code"; return false; }
            }
            for (;;) {
                uint32_t s; if (!lit.decode(in, s)) { why = "invalid literal/length code"; return false; }
                if (s < 256) { if (out.size() >= limit) { why = "decompressed data exceeds image size"; return false; } out.push_back(uint8_t(s)); continue; }
                if (s == 256) break;
                s -= 257; if (s >= 29) { why = "invalid length symbol"; return false; }
                uint32_t extra; if (!in.get(lengthExtra[s], extra)) { why = "truncated length"; return false; }
                const uint32_t length = lengthBase[s] + extra;
                uint32_t ds; if (!dist.decode(in, ds) || ds >= 30) { why = "invalid distance code"; return false; }
                if (!in.get(distExtra[ds], extra)) { why = "truncated distance"; return false; }
                const uint32_t distance = distBase[ds] + extra;
                if (distance > out.size()) { why = "distance before stream start"; return false; }
                if (out.size() + length > limit) { why = "decompressed data exceeds image size"; return false; }
                const size_t from = out.size() - distance; for (uint32_t k = 0; k < length; ++k) out.push_back(out[from + k]);
            }
        } while (!final);
        return true;
    }

    // --- container --------------------------------------------------------
    static bool parse(const uint8_t* p, size_t size, Header& h, std::vector<uint8_t>* idat,
                      std::array<uint8_t, 1024>* palette, uint32_t* paletteSize, std::array<uint16_t, 3>* key, std::string& why) {
        static constexpr uint8_t magic[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
        if (size < 33 || std::memcmp(p, magic, 8) || be32(p + 8) != 13 || be32(p + 12) != 0x49484452) { why = "not a PNG with IHDR"; return false; }
        h.width = be32(p + 16); h.height = be32(p + 20); h.depth = p[24]; h.color = p[25]; h.interlace = p[28];
        if (!h.width || !h.height || h.width > 0x7fffffff || h.height > 0x7fffffff || p[26] || p[27] || h.interlace > 1) { why = "invalid IHDR"; return false; }
        const uint8_t d = h.depth;
        const bool ok = (h.color == 0 && (d == 1 || d == 2 || d == 4 || d == 8 || d == 16)) || (h.color == 3 && (d == 1 || d == 2 || d == 4 || d == 8)) ||
                        ((h.color == 2 || h.color == 4 || h.color == 6) && (d == 8 || d == 16));
        if (!ok) { why = "invalid colour type/bit depth"; return false; }
        size_t offset = 33; bool sawPalette = false, sawData = false, end = false;
        if (paletteSize) *paletteSize = 0;
        if (palette) for (size_t i = 0; i < 256; ++i) (*palette)[i * 4 + 3] = 255;
        while (offset + 12 <= size) {
            const uint32_t length = be32(p + offset), kind = be32(p + offset + 4);
            if (length > size - offset - 12) { why = "chunk exceeds file"; return false; }
            const uint8_t* body = p + offset + 8;
            if (kind == 0x504c5445) { // PLTE
                if (!length || length % 3 || length > 768) { why = "invalid PLTE"; return false; }
                sawPalette = true; if (paletteSize) *paletteSize = length / 3;
                if (palette) for (uint32_t i = 0; i < length / 3; ++i) { (*palette)[i * 4] = body[i * 3]; (*palette)[i * 4 + 1] = body[i * 3 + 1]; (*palette)[i * 4 + 2] = body[i * 3 + 2]; }
            } else if (kind == 0x74524e53) { // tRNS
                h.transparency = true;
                if (h.color == 3) { if (!sawPalette || length > 256) { why = "invalid tRNS"; return false; } if (palette) for (uint32_t i = 0; i < length; ++i) (*palette)[i * 4 + 3] = body[i]; }
                else if (h.color == 0 && length == 2) { if (key) (*key)[0] = uint16_t(body[0] << 8 | body[1]); }
                else if (h.color == 2 && length == 6) { if (key) for (int c = 0; c < 3; ++c) (*key)[c] = uint16_t(body[2 * c] << 8 | body[2 * c + 1]); }
                else { why = "invalid tRNS"; return false; }
            } else if (kind == 0x49444154) { sawData = true; if (idat) idat->insert(idat->end(), body, body + length); else break; }
            else if (kind == 0x49454e44) { end = true; break; }
            else if (!(p[offset + 4] & 0x20)) { why = "unknown critical chunk"; return false; }
            offset += size_t(length) + 12;
        }
        if (h.color == 3 && !sawPalette) { why = "palette image without PLTE"; return false; }
        if (!sawData) { why = "no IDAT"; return false; }
        if (idat && !end) { why = "truncated PNG (no IEND)"; return false; }
        return true;
    }

    static bool header(const uint8_t* p, size_t size, Header& h, std::string& why) {
        return parse(p, size, h, nullptr, nullptr, nullptr, nullptr, why);
    }

    static bool decode(const uint8_t* p, size_t size, uint8_t alpha, bool bgra, Output& out, std::string& why, uint64_t maxPixels = 16384ull * 16384ull) {
        Header& h = out.header; std::vector<uint8_t> idat; std::array<uint8_t, 1024> palette{}; uint32_t entries = 0; std::array<uint16_t, 3> key{0xffff, 0xffff, 0xffff};
        if (!parse(p, size, h, &idat, &palette, &entries, &key, why)) return false;
        if (h.interlace) { why = "Adam7 interlaced PNG not implemented"; return false; }
        if (uint64_t(h.width) * h.height > maxPixels) { why = "image too large"; return false; }
        const unsigned channels = h.color == 0 ? 1 : h.color == 2 ? 3 : h.color == 3 ? 1 : h.color == 4 ? 2 : 4;
        const unsigned bitsPerPixel = channels * h.depth, bpp = (bitsPerPixel + 7) / 8;
        const size_t stride = (size_t(h.width) * bitsPerPixel + 7) / 8, expected = (stride + 1) * h.height;
        std::vector<uint8_t> raw; raw.reserve(expected);
        if (!inflate(idat.data(), idat.size(), raw, expected, why)) return false;
        if (raw.size() != expected) { why = "decompressed size mismatch"; return false; }
        std::vector<uint8_t> prior(stride, 0), line(stride);
        out.rgba.assign(size_t(h.width) * h.height * 4, 0);
        const bool hasKey = h.transparency && (h.color == 0 || h.color == 2);
        for (uint32_t y = 0; y < h.height; ++y) {
            const uint8_t filter = raw[y * (stride + 1)]; const uint8_t* src = &raw[y * (stride + 1) + 1];
            for (size_t i = 0; i < stride; ++i) {
                const int a = i >= bpp ? line[i - bpp] : 0, b = prior[i], c = i >= bpp ? prior[i - bpp] : 0;
                int v;
                switch (filter) {
                case 0: v = 0; break; case 1: v = a; break; case 2: v = b; break; case 3: v = (a + b) / 2; break;
                case 4: { const int pp = a + b - c, pa = pp > a ? pp - a : a - pp, pb = pp > b ? pp - b : b - pp, pc = pp > c ? pp - c : c - pp;
                          v = (pa <= pb && pa <= pc) ? a : pb <= pc ? b : c; break; }
                default: why = "invalid filter type"; return false;
                }
                line[i] = uint8_t(src[i] + v);
            }
            uint8_t* dst = &out.rgba[size_t(y) * h.width * 4];
            auto sample = [&](uint32_t x, unsigned channel) -> uint16_t { // native-depth sample
                if (h.depth == 16) { const size_t o = (size_t(x) * channels + channel) * 2; return uint16_t(line[o] << 8 | line[o + 1]); }
                if (h.depth == 8) return line[size_t(x) * channels + channel];
                const size_t bit = size_t(x) * h.depth; return uint16_t((line[bit / 8] >> (8 - h.depth - bit % 8)) & ((1u << h.depth) - 1));
            };
            auto to8 = [&](uint16_t v) -> uint8_t { return h.depth == 16 ? uint8_t(v >> 8) : h.depth == 8 ? uint8_t(v) : uint8_t(v * 255 / ((1u << h.depth) - 1)); };
            for (uint32_t x = 0; x < h.width; ++x) {
                uint8_t r, g, b, al = alpha;
                if (h.color == 3) {
                    const uint16_t index = sample(x, 0);
                    if (index >= entries) { why = "palette index out of range"; return false; }
                    r = palette[index * 4]; g = palette[index * 4 + 1]; b = palette[index * 4 + 2]; al = h.transparency ? palette[index * 4 + 3] : alpha;
                } else if (h.color == 0 || h.color == 4) {
                    const uint16_t s = sample(x, 0); r = g = b = to8(s);
                    if (h.color == 4) al = to8(sample(x, 1)); else if (hasKey && s == key[0]) al = 0;
                } else {
                    const uint16_t s0 = sample(x, 0), s1 = sample(x, 1), s2 = sample(x, 2);
                    r = to8(s0); g = to8(s1); b = to8(s2);
                    if (h.color == 6) al = to8(sample(x, 3)); else if (hasKey && s0 == key[0] && s1 == key[1] && s2 == key[2]) al = 0;
                }
                dst[x * 4] = bgra ? b : r; dst[x * 4 + 1] = g; dst[x * 4 + 2] = bgra ? r : b; dst[x * 4 + 3] = al;
            }
            std::swap(prior, line);
        }
        return true;
    }
};
}
