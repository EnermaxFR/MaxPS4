#pragma once
#include "OrbisV4VirtualPRX.hpp"
#include <algorithm>
#include <map>
#include <stdexcept>

namespace maxps4 {
// Mission 50. Extends a provider produced by OrbisV4VirtualPRX::build with
// extra export libraries and host-service stubs, keeping the original guest
// code at the same guest addresses (text 0x4000, state 0x8000) so its
// RIP-relative state accesses stay valid.
//
// Stub ABI (16 bytes): 49 89 CA (mov r10,rcx) | B8 id32 (mov eax,id) |
// 0F 05 (syscall) | C3 (ret) | CC*5. SYSCALL overwrites RCX (the 4th SysV
// argument), so it is preserved in R10 first. The syscall is the only way
// into the host; a dispatcher must check that RIP is exactly the stub's
// syscall and EAX its id before any effect.
// A final F4 (HLT) in the text is the diagnostic stop target.
struct OrbisV4ServicePRX {
    struct Library { std::string name; uint16_t version = 1; };
    struct Stub { std::string nid; size_t library = 0; uint32_t service = 0; }; // library: 0 = base library.
    // OBJECT export in the RW state segment, after the base provider's state.
    struct Data { std::string nid; size_t library = 0; uint64_t offset = 0; std::vector<uint8_t> bytes; };
    struct Layout {
        uint64_t stubs = 0; // Module-relative address of stub 0.
        uint64_t halt = 0;  // Module-relative address of the HLT byte.
        size_t stubCount = 0;
        static constexpr uint64_t stubSize = 16, syscallOffset = 8;
    };
    static constexpr uint64_t text = 0x4000, state = 0x8000;
    static std::vector<uint8_t> extend(const std::vector<uint8_t>& base, const std::vector<Library>& extra,
                                       const std::vector<Stub>& stubs, size_t stateSize, Layout& layout,
                                       const std::vector<Data>& objects = {},
                                       const std::vector<std::string>& dropped = {}) {
        OrbisMetadata m;
        if (!m.parse(base.data(), base.size())) throw std::invalid_argument("service PRX base: " + m.error);
        const OrbisMetadata::Segment *code = nullptr, *data = nullptr;
        for (const auto& s : m.segments) {
            if (s.type == 1 && s.address == text && (s.flags & 7) == 5) code = &s;
            if (s.type == 1 && s.address == state && (s.flags & 7) == 6) data = &s;
        }
        if (!code || !data || code->filesz != code->memsz || m.modules.size() != 1 || m.libraries.size() != 1)
            throw std::invalid_argument("service PRX base must be an OrbisV4VirtualPRX provider");
        if (stateSize < data->memsz || stateSize > 0x400000) throw std::length_error("service PRX state");
        struct Export { std::string nid; uint16_t library; uint64_t offset, size; bool object = false; };
        std::vector<Export> exports;
        // `dropped`: base exports withdrawn (their code stays, unexported) so the
        // same identities can be re-exported as host-service stubs.
        for (const auto& s : m.symbols) if (s.defined && !s.encoded.empty() &&
                                             std::find(dropped.begin(), dropped.end(), s.nid) == dropped.end())
            exports.push_back({s.nid, 1, s.value - text, s.size});
        std::vector<uint8_t> bytes(base.begin() + code->offset, base.begin() + code->offset + code->filesz);
        while (bytes.size() % 16) bytes.push_back(0xcc);
        layout.stubs = text + bytes.size(); layout.stubCount = stubs.size();
        std::map<std::string, bool> seen;
        for (const auto& e : exports) seen[e.nid + "#" + std::to_string(e.library)] = true;
        for (const auto& s : stubs) {
            if (s.nid.size() != 11 || s.library > extra.size()) throw std::invalid_argument("service stub identity");
            const uint16_t library = uint16_t(s.library + 1);
            if (!seen.emplace(s.nid + "#" + std::to_string(library), true).second) throw std::invalid_argument("duplicate service export");
            exports.push_back({s.nid, library, bytes.size(), Layout::stubSize});
            const uint8_t stub[16] = {0x49, 0x89, 0xca, 0xb8, uint8_t(s.service), uint8_t(s.service >> 8), uint8_t(s.service >> 16),
                                      uint8_t(s.service >> 24), 0x0f, 0x05, 0xc3, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc};
            bytes.insert(bytes.end(), stub, stub + 16);
        }
        layout.halt = text + bytes.size(); bytes.push_back(0xf4);
        std::vector<uint8_t> stateBytes(size_t(stateSize), 0);
        std::copy(base.begin() + data->offset, base.begin() + data->offset + data->filesz, stateBytes.begin());
        for (const auto& d : objects) {
            if (d.nid.size() != 11 || d.library > extra.size() || d.bytes.empty() || d.offset < ((data->memsz + 15) & ~uint64_t{15}) ||
                d.offset > stateSize || d.bytes.size() > stateSize - d.offset) throw std::invalid_argument("service data export");
            const uint16_t library = uint16_t(d.library + 1);
            if (!seen.emplace(d.nid + "#" + std::to_string(library), true).second) throw std::invalid_argument("duplicate service export");
            for (const auto& other : exports) if (other.object && d.offset < other.offset + other.size && other.offset < d.offset + d.bytes.size())
                throw std::invalid_argument("overlapping service data");
            std::copy(d.bytes.begin(), d.bytes.end(), stateBytes.begin() + d.offset);
            exports.push_back({d.nid, library, d.offset, d.bytes.size(), true});
        }
        if (bytes.size() > state - text) throw std::length_error("service PRX text");
        // Strings, descriptors and symbols.
        std::vector<uint8_t> strings(1, 0);
        auto addString = [&](const std::string& t) {
            const auto offset = strings.size(); strings.insert(strings.end(), t.begin(), t.end()); strings.push_back(0); return uint64_t(offset);
        };
        auto encode = [](uint16_t id) {
            const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";
            std::string out; do { out.insert(out.begin(), a[id & 63]); id >>= 6; } while (id); return out;
        };
        std::vector<std::pair<uint64_t, uint64_t>> tags;
        const auto moduleName = addString(m.modules[0].name);
        tags.push_back({0x61000013, addString(m.libraries[0].name) | (uint64_t{m.libraries[0].version} << 32) | (uint64_t{1} << 48)});
        for (size_t i = 0; i < extra.size(); ++i) {
            if (extra[i].name.empty() || extra[i].name.size() > 64) throw std::invalid_argument("service library name");
            tags.push_back({0x61000013, addString(extra[i].name) | (uint64_t{extra[i].version} << 32) | (uint64_t(i + 2) << 48)});
        }
        tags.push_back({0x6100000d, moduleName | (uint64_t{m.modules[0].version} << 32) | (uint64_t{1} << 48)});
        std::vector<uint8_t> symbols(24, 0);
        auto put = [](std::vector<uint8_t>& b, size_t p, uint64_t v, size_t n) { for (size_t i = 0; i < n; ++i) b[p + i] = uint8_t(v >> (8 * i)); };
        for (const auto& e : exports) {
            const auto name = addString(e.nid + "#" + encode(e.library) + "#B");
            const size_t p = symbols.size(); symbols.resize(p + 24, 0);
            put(symbols, p, name, 4); symbols[p + 4] = e.object ? 0x11 : 0x12; put(symbols, p + 6, e.object ? 2 : 1, 2);
            put(symbols, p + 8, (e.object ? state : text) + e.offset, 8); put(symbols, p + 16, e.size, 8);
        }
        while (strings.size() % 8) strings.push_back(0);
        const uint64_t symbolOffset = strings.size();
        std::vector<uint8_t> dynlib = strings; dynlib.insert(dynlib.end(), symbols.begin(), symbols.end());
        tags.insert(tags.begin(), {{0x61000035, 0}, {0x61000037, strings.size()}, {0x61000039, symbolOffset},
                                   {0x6100003b, 24}, {0x6100003f, symbols.size()}});
        tags.push_back({0, 0});
        // File layout: ELF header + 4 PH, text @0x1000, state @aligned, dynamic, dynlibdata.
        auto align = [](uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); };
        const uint64_t textFile = 0x1000, stateFile = align(textFile + bytes.size(), 0x1000);
        const uint64_t dynFile = align(stateFile + stateSize, 16), dynSize = tags.size() * 16;
        const uint64_t libFile = align(dynFile + dynSize, 16);
        std::vector<uint8_t> out(size_t(libFile + dynlib.size()), 0);
        const uint8_t magic[] = {0x7f, 'E', 'L', 'F', 2, 1, 1, 9, 0};
        std::copy(std::begin(magic), std::end(magic), out.begin());
        put(out, 16, 0xfe18, 2); put(out, 18, 62, 2); put(out, 20, 1, 4); put(out, 32, 64, 8);
        put(out, 52, 64, 2); put(out, 54, 56, 2); put(out, 56, 4, 2);
        auto ph = [&](size_t i, uint32_t t, uint32_t f, uint64_t off, uint64_t addr, uint64_t size) {
            const size_t p = 64 + i * 56; put(out, p, t, 4); put(out, p + 4, f, 4); put(out, p + 8, off, 8);
            put(out, p + 16, addr, 8); put(out, p + 32, size, 8); put(out, p + 40, size, 8); put(out, p + 48, 1, 8);
        };
        ph(0, 1, 5, textFile, text, bytes.size()); ph(1, 1, 6, stateFile, state, stateSize);
        ph(2, 2, 4, dynFile, 0, dynSize); ph(3, 0x61000000, 4, libFile, 0, dynlib.size());
        std::copy(bytes.begin(), bytes.end(), out.begin() + textFile);
        std::copy(stateBytes.begin(), stateBytes.end(), out.begin() + stateFile);
        for (size_t i = 0; i < tags.size(); ++i) { put(out, dynFile + i * 16, tags[i].first, 8); put(out, dynFile + i * 16 + 8, tags[i].second, 8); }
        std::copy(dynlib.begin(), dynlib.end(), out.begin() + libFile);
        return out;
    }
};
}
