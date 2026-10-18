// PngDecoder against PIL references (manifest from scripts/make-png-fixtures.py)
// and the libScePngDec host service end to end in translated guest memory.
#include "../Native/OrbisV4PngDecService.hpp"
#include "../Native/maxps4_orbis_v4_translation.h"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace maxps4;
static std::vector<uint8_t> load(const std::string& path) {
    std::ifstream f(path, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}};
}

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: PngDecoderTest MANIFEST\n"; return 2; }
    std::ifstream manifest(argv[1]); std::string line; size_t checked = 0, game = 0; std::vector<uint8_t> sample;
    while (std::getline(manifest, line)) {
        std::istringstream in(line); std::string png, ref, flag; unsigned alpha = 255; in >> png >> ref >> alpha >> flag;
        const auto bytes = load(png); assert(!bytes.empty());
        PngDecoder::Output out; std::string why;
        if (flag == "interlaced") {
            assert(!PngDecoder::decode(bytes.data(), bytes.size(), 255, false, out, why) && why.find("Adam7") != std::string::npos);
            continue;
        }
        if (!PngDecoder::decode(bytes.data(), bytes.size(), uint8_t(alpha), false, out, why)) { std::cerr << png << ": " << why << '\n'; return 1; }
        const auto expected = load(ref);
        if (out.rgba != expected) {
            size_t k = 0; while (k < std::min(out.rgba.size(), expected.size()) && out.rgba[k] == expected[k]) ++k;
            std::cerr << png << ": mismatch at byte " << k << " of " << expected.size() << '\n'; return 1;
        }
        // BGRA swaps R and B only.
        PngDecoder::Output swapped; assert(PngDecoder::decode(bytes.data(), bytes.size(), uint8_t(alpha), true, swapped, why));
        for (size_t i = 0; i < out.rgba.size(); i += 4)
            assert(swapped.rgba[i] == out.rgba[i + 2] && swapped.rgba[i + 2] == out.rgba[i] && swapped.rgba[i + 1] == out.rgba[i + 1] && swapped.rgba[i + 3] == out.rgba[i + 3]);
        // Every truncation is refused, never read past the end.
        for (size_t cut : {size_t(8), size_t(40), bytes.size() / 2, bytes.size() - 13}) {
            PngDecoder::Output partial; assert(!PngDecoder::decode(bytes.data(), cut, 255, false, partial, why));
        }
        ++checked; if (png.find("rsc_k") != std::string::npos) ++game;
        if (png.find("palette8_trns") != std::string::npos) sample = bytes;
    }
    assert(checked >= 15 && !sample.empty());

    // Corrupted compressed data: refused (any byte flip inside IDAT).
    {
        auto bad = sample; size_t idat = 8; while (PngDecoder::be32(&bad[idat + 4]) != 0x49444154) idat += PngDecoder::be32(&bad[idat]) + 12;
        bool anyRefused = false;
        for (size_t k = 0; k < 64; ++k) {
            auto c = bad; c[idat + 10 + k * 7 % PngDecoder::be32(&bad[idat])] ^= 0x5a;
            PngDecoder::Output o; std::string why; anyRefused |= !PngDecoder::decode(c.data(), c.size(), 255, false, o, why);
        }
        assert(anyRefused);
    }

    // Service: parse/query/create/decode through guest memory, title's layouts.
    constexpr uint64_t base = 0x400000;
    OrbisV4Translation owner([]{}); const auto page = owner.page_size(); const uint64_t pages = (2u << 20) / page;
    assert(!owner.reserve(base, page * pages) && !owner.map(base, page * pages, 6));
    {
        auto memory = owner.lease();
        struct Registry { std::map<std::string, OrbisV4HostServices::Handler> h; void implement(const std::string& n, OrbisV4HostServices::Handler f) { h[n] = std::move(f); } } services;
        auto st = OrbisV4PngDecService::install(services);
        auto call = [&](const std::string& name, uint64_t a = 0, uint64_t b = 0, uint64_t c = 0, uint64_t d = 0) {
            OrbisV4HostServices::Registers r; r.rdi = a; r.rsi = b; r.rdx = c; r.rcx = d;
            return services.h.at(name)(r, memory).value;
        };
        const uint64_t png = base, param = base + 0x40000, info = param + 0x100, handle = param + 0x200, work = param + 0x1000, image = base + 0x80000;
        assert(!memory.write_data(png, sample.data(), sample.size()));
        struct { uint64_t p; uint32_t size, reserved; } parse{png, uint32_t(sample.size()), 0};
        assert(!memory.write_data(param, &parse, 16));
        assert(call("scePngDecParseHeader", param, info) == 0);
        uint32_t w = 0, h = 0, flags = 0; uint16_t space = 0, depth = 0;
        memory.read_data(info, &w, 4); memory.read_data(info + 4, &h, 4); memory.read_data(info + 8, &space, 2); memory.read_data(info + 10, &depth, 2); memory.read_data(info + 12, &flags, 4);
        assert(w == 37 && h == 23 && space == 4 && depth == 8 && flags == 2);
        parse.reserved = 1; assert(!memory.write_data(param, &parse, 16) && call("scePngDecParseHeader", param, info) == OrbisV4PngDecService::invalidParam);
        uint32_t create[3] = {12, 0, w}; assert(!memory.write_data(param, create, 12));
        const uint64_t need = call("scePngDecQueryMemorySize", param); assert(int32_t(need) > 0);
        assert(call("scePngDecCreate", param, work, need - 1, handle) == OrbisV4PngDecService::invalidWorkMemory);
        assert(call("scePngDecCreate", param, work, need, handle) == 0);
        uint64_t decoder = 0; memory.read_data(handle, &decoder, 8); assert(decoder == work);
        struct { uint64_t png, image; uint32_t pngSize, imageSize; uint16_t format, alpha; uint32_t pitch; } decode{png, image, uint32_t(sample.size()), w * 4 * h, 0, 255, w * 4};
        assert(!memory.write_data(param, &decode, 32));
        std::vector<uint8_t> guard(w * 4 * h + 64, 0xcd); assert(!memory.write_data(image, guard.data(), guard.size()));
        assert(call("scePngDecDecode", decoder + 16, param, 0) == OrbisV4PngDecService::invalidHandle);
        decode.imageSize -= 1; assert(!memory.write_data(param, &decode, 32) && call("scePngDecDecode", decoder, param, 0) == OrbisV4PngDecService::invalidSize);
        std::vector<uint8_t> back(guard.size()); memory.read_data(image, back.data(), back.size()); assert(back == guard); // untouched on refusal
        decode.imageSize += 1; assert(!memory.write_data(param, &decode, 32) && call("scePngDecDecode", decoder, param, 0) == 0);
        PngDecoder::Output ref; std::string why; assert(PngDecoder::decode(sample.data(), sample.size(), 255, false, ref, why));
        memory.read_data(image, back.data(), back.size());
        assert(std::equal(ref.rgba.begin(), ref.rgba.end(), back.begin()));
        assert(std::all_of(back.begin() + ref.rgba.size(), back.end(), [](uint8_t b) { return b == 0xcd; })); // no overrun
        assert(st->decoded == 1 && call("scePngDecDelete", decoder) == 0 && call("scePngDecDelete", decoder) == OrbisV4PngDecService::invalidHandle);
        // The title frees work memory without Delete and creates again at the same address.
        assert(!memory.write_data(param + 0x40, create, 12));
        assert(call("scePngDecCreate", param + 0x40, work, need, handle) == 0 && call("scePngDecCreate", param + 0x40, work, need, handle) == 0);
        assert(call("scePngDecDecode", work, param, 0) == 0);
        create[2] = w - 1; assert(!memory.write_data(param, create, 12) && call("scePngDecCreate", param, work, need, handle) == 0);
        assert(!memory.write_data(param + 0x80, &decode, 32) && call("scePngDecDecode", work, param + 0x80, 0) == OrbisV4PngDecService::unsupportedData);
    }
    owner.close();
    std::cout << "PASS PNG decoder: " << checked << " images equal to PIL (" << game << " from the title), BGRA, truncation/corruption/Adam7 refusals; "
                 "libScePngDec service parse/query/create/decode/delete in guest memory, refusals leave output untouched\n";
}
