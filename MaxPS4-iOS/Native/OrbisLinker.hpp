#pragma once
#include "PS4PKGRuntimePlan.hpp"
#include <map>
#include <set>
#include <tuple>
#include <stdexcept>

namespace maxps4 {
// Staging linker only. All addresses are guest addresses; vectors are ordinary
// host data, never executable mappings. See docs/ORBIS-LINKER.md for the subset.
class OrbisLinker {
public:
    struct Identity {
        std::string nid, library, module;
        uint16_t libraryVersion = 0, moduleVersion = 0;
        uint8_t type = 0;
        auto key() const { return std::tie(nid, library, module, libraryVersion, moduleVersion, type); }
        bool operator<(const Identity& b) const { return key() < b.key(); }
    };
    struct HLE { Identity identity; uint64_t guestAddress; }; // Guest trampoline, never host function pointer.
    struct Input { PS4PKGRuntimePlan::ModuleBytes file; uint64_t bias = 0; };
    struct Module { std::string path; uint64_t bias = 0, entry = 0; PS4ExecutableImage image; };
    struct Mapping { uint64_t address, size; uint32_t permissions; const uint8_t* data; };
    struct Image {
        std::vector<Module> modules;
        // A backend must copy/map these bytes at their guest addresses and apply
        // the advertised permissions. No identity mapping is implied.
        std::vector<Mapping> mappings() const {
            std::vector<Mapping> result;
            for (const auto& m : modules) for (const auto& s : m.image.segments)
                result.push_back({m.bias + s.header.address, s.header.memorySize, s.header.flags, s.bytes.data()});
            return result;
        }
        const uint8_t* translate(uint64_t address, size_t count, uint32_t permissions) const {
            for (const auto& m : modules) if (address >= m.bias)
                if (const auto* p = m.image.translate(address - m.bias, count, permissions)) return p;
            return nullptr;
        }
    };
    // Strong transaction: output is unchanged on every failure, including OOM.
    static bool link(const std::vector<Input>& inputs, const std::vector<HLE>& hle,
                     Image& output, std::string& diagnostic,
                     size_t budget = PS4PKGRuntimePlan::batchMemoryBudget) {
        try {
            Image pending;
            build(inputs, hle, pending, budget);
            output = std::move(pending);
            diagnostic.clear();
            return true;
        } catch (const std::bad_alloc&) { diagnostic = "linker: allocation failed"; }
          catch (const std::runtime_error& e) { diagnostic = e.what(); }
        return false;
    }
private:
    struct Failure : std::runtime_error { using std::runtime_error::runtime_error; };
    static void require(bool ok, const std::string& message) { if (!ok) throw Failure(message); }
    static uint64_t read(const uint8_t* p, size_t width) {
        uint64_t v = 0; for (size_t i = 0; i < width; ++i) v |= uint64_t(p[i]) << (i * 8); return v;
    }
    static bool range(uint64_t p, uint64_t n, uint64_t size) { return p <= size && n <= size - p; }
    static uint64_t add(uint64_t a, uint64_t b) {
        require(b <= UINT64_MAX - a, "guest address overflow"); return a + b;
    }
    static uint64_t signedAdd(uint64_t a, uint64_t bits) {
        if (!(bits >> 63)) return add(a, bits);
        const uint64_t magnitude = (~bits) + 1;
        require(a >= magnitude, "relocation underflow"); return a - magnitude;
    }
    static std::string encoded(uint16_t id) {
        const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";
        std::string out; do { out.insert(out.begin(), alphabet[id & 63]); id >>= 6; } while (id); return out;
    }
    struct Descriptor { std::string name; uint16_t version; };
    struct Symbol { Identity identity; uint64_t value = 0, size = 0; bool defined = false, local = false; };
    struct Rela { uint64_t offset, info, addend; };
    struct Dynamic { std::vector<Symbol> symbols; std::vector<Rela> relas; };
    static Dynamic parse(const Input& input, const Module& m, size_t& symbolBudget, size_t& relocationBudget) {
        const auto* bytes = input.file.bytes; const auto size = input.file.size;
        const PS4ExecutableImage::Header *dyn = nullptr, *data = nullptr;
        for (const auto& h : m.image.headers) {
            if (h.type == 2) { require(!dyn, "duplicate PT_DYNAMIC"); dyn = &h; }
            if (h.type == 0x61000000) { require(!data, "duplicate PT_SCE_DYNLIBDATA"); data = &h; }
        }
        require(dyn && data, "SCE PT_DYNAMIC and PT_SCE_DYNLIBDATA required");
        require(dyn->fileSize && dyn->fileSize % 16 == 0 && dyn->fileSize / 16 <= 4096 &&
                range(dyn->offset, dyn->fileSize, size) && range(data->offset, data->fileSize, size),
                "dynamic metadata out of bounds");
        std::map<uint64_t, uint64_t> tags;
        std::vector<std::pair<uint64_t, uint64_t>> descriptors;
        bool terminated = false;
        for (size_t i = 0; i < dyn->fileSize; i += 16) {
            const auto* p = bytes + dyn->offset + i;
            const auto tag = read(p, 8), value = read(p + 8, 8);
            if (!tag) { terminated = true; break; }
            if (tag == 0x6100000d || tag == 0x6100000f || tag == 0x61000013 || tag == 0x61000015) {
                descriptors.push_back({tag, value}); continue;
            }
            // Only interpreted, verified SCE table tags. Unknown semantics fail closed.
            switch (tag) {
            case 0x61000035: case 0x61000037: case 0x61000039: case 0x6100003b: case 0x6100003f:
            case 0x6100002f: case 0x61000031: case 0x61000033:
            case 0x61000029: case 0x6100002b: case 0x6100002d: break;
            default: throw Failure("unsupported dynamic tag " + std::to_string(tag));
            }
            require(tags.emplace(tag, value).second, "duplicate dynamic tag");
        }
        require(terminated, "unterminated dynamic table");
        auto get = [&](uint64_t tag) {
            auto it = tags.find(tag); require(it != tags.end(), "missing dynamic table tag"); return it->second;
        };
        auto table = [&](uint64_t offset, uint64_t length) {
            require(range(offset, length, data->fileSize), "SCE table out of bounds");
            return bytes + data->offset + offset;
        };
        const auto stringSize = get(0x61000037);
        require(stringSize && stringSize <= 1024 * 1024, "string table budget");
        const auto* strings = table(get(0x61000035), stringSize);
        require(strings[0] == 0, "missing null string");
        auto stringAt = [&](uint64_t offset) {
            require(offset < stringSize, "string offset out of bounds");
            const auto available = std::min<uint64_t>(stringSize - offset, 256);
            const auto* end = static_cast<const uint8_t*>(std::memchr(strings + offset, 0, available));
            require(end, "unterminated or oversized string");
            for (auto* p = strings + offset; p != end; ++p) require(*p >= 0x21 && *p <= 0x7e, "invalid identity string");
            return std::string(reinterpret_cast<const char*>(strings + offset), end - (strings + offset));
        };
        std::map<std::string, Descriptor> importLib, exportLib, importMod, exportMod;
        for (const auto& d : descriptors) {
            auto& dest = d.first == 0x6100000d ? exportMod : d.first == 0x6100000f ? importMod :
                         d.first == 0x61000013 ? exportLib : importLib;
            const auto name = stringAt(uint32_t(d.second));
            require(!name.empty(), "empty descriptor");
            require(dest.emplace(encoded(uint16_t(d.second >> 48)), Descriptor{name, uint16_t(d.second >> 32)}).second,
                    "duplicate descriptor ID");
        }
        require(get(0x6100003b) == 24, "unsupported symbol entry size");
        const auto symbolSize = get(0x6100003f);
        require(symbolSize >= 24 && symbolSize % 24 == 0 && symbolSize / 24 <= symbolBudget, "symbol table budget or size");
        symbolBudget -= static_cast<size_t>(symbolSize / 24);
        const auto* symbols = table(get(0x61000039), symbolSize);
        Dynamic out;
        for (size_t i = 0; i < symbolSize; i += 24) {
            const auto* p = symbols + i;
            Symbol s; const auto name = stringAt(read(p, 4));
            const uint8_t bind = p[4] >> 4, type = p[4] & 15;
            const auto section = read(p + 6, 2);
            s.value = read(p + 8, 8); s.size = read(p + 16, 8); s.defined = section != 0; s.local = bind == 0;
            if (!i) { require(read(p, 8) == 0 && s.value == 0 && s.size == 0, "invalid null symbol"); out.symbols.push_back(s); continue; }
            require(bind <= 2 && (type == 0 || type == 1 || type == 2) && p[5] == 0 && section < 0xff00,
                    "unsupported symbol binding/type/visibility/section");
            if (s.defined) require(m.image.translate(s.value, s.size ? s.size : 1, type == 2 ? 5 : 4), "export outside valid segment");
            else require(s.value == 0 && !s.local, "invalid undefined symbol");
            if (!s.local) {
                const auto a = name.find('#'), b = a == std::string::npos ? a : name.find('#', a + 1);
                require(a == 11 && b != std::string::npos && name.find('#', b + 1) == std::string::npos, "invalid NID identity");
                s.identity.nid = name.substr(0, a);
                require(s.identity.nid.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-") == std::string::npos,
                        "invalid NID alphabet");
                const auto& libs = s.defined ? exportLib : importLib; const auto& mods = s.defined ? exportMod : importMod;
                auto l = libs.find(name.substr(a + 1, b - a - 1)), mod = mods.find(name.substr(b + 1));
                require(l != libs.end() && mod != mods.end(), "symbol references missing library/module ID");
                s.identity.library = l->second.name; s.identity.libraryVersion = l->second.version;
                s.identity.module = mod->second.name; s.identity.moduleVersion = mod->second.version; s.identity.type = type;
            }
            out.symbols.push_back(std::move(s));
        }
        auto relocations = [&](uint64_t offsetTag, uint64_t sizeTag, uint64_t kindTag, uint64_t expected) {
            if (!tags.count(offsetTag) && !tags.count(sizeTag) && !tags.count(kindTag)) return;
            require(get(kindTag) == expected, "unsupported relocation table encoding");
            const auto n = get(sizeTag); require(n % 24 == 0 && n / 24 <= relocationBudget, "relocation table budget or size");
            relocationBudget -= static_cast<size_t>(n / 24);
            const auto* p = table(get(offsetTag), n);
            for (size_t i = 0; i < n; i += 24) out.relas.push_back({read(p + i, 8), read(p + i + 8, 8), read(p + i + 16, 8)});
        };
        relocations(0x6100002f, 0x61000031, 0x61000033, 24);
        relocations(0x61000029, 0x6100002d, 0x6100002b, 7); // DT_RELA
        return out;
    }
    static void build(const std::vector<Input>& inputs, const std::vector<HLE>& hle, Image& out, size_t budget) {
        require(budget <= PS4PKGRuntimePlan::batchMemoryBudget, "budget exceeds existing limit");
        require(hle.size() <= 65536, "HLE registry budget");
        std::vector<size_t> sorted(inputs.size());
        for (size_t i = 0; i < inputs.size(); ++i) sorted[i] = i;
        std::sort(sorted.begin(), sorted.end(), [&](size_t a, size_t b) { return inputs[a].file.request.logicalPath < inputs[b].file.request.logicalPath; });
        std::vector<PS4PKGRuntimePlan::ModuleRequest> requests;
        for (auto i : sorted) {
            auto r = inputs[i].file.request; std::sort(r.dependencies.begin(), r.dependencies.end()); requests.push_back(std::move(r));
        }
        std::vector<size_t> order; std::string error;
        require(PS4PKGRuntimePlan::orderModules(requests, order, error), error);
        std::vector<Dynamic> dynamic;
        // Check the complete segment budget before allocating any staged image.
        // The existing loader still validates every field independently below.
        size_t planned = 0;
        for (const auto& input : inputs) {
            const auto* b = input.file.bytes; const auto n = input.file.size;
            require(b && n >= 64, "chargeur: truncated ELF header");
            const auto phoff = read(b + 32, 8), phnum = read(b + 56, 2);
            require(phnum && phnum <= 128 && range(phoff, phnum * 56, n), "chargeur: invalid program table");
            for (size_t i = 0; i < phnum; ++i) {
                const auto* p = b + phoff + i * 56; const auto type = read(p, 4);
                if (type != 1 && type != 0x61000010) continue;
                const auto size = read(p + 40, 8);
                require(size <= budget - planned, "batch memory budget exceeded"); planned += static_cast<size_t>(size);
            }
        }
        size_t allocated = 0, symbolBudget = 65536, relocationBudget = 65536;
        for (auto i : order) {
            const auto& in = inputs[sorted[i]];
            PS4PKGRuntimePlan::PreparedModule staged;
            require(PS4PKGRuntimePlan::prepareStandalone(in.file.request.logicalPath, in.file.bytes, in.file.size,
                    in.file.request.dependencies, staged, error), error);
            require(!staged.image.requiresTLS, "TLS not supported");
            Module m {staged.logicalPath, in.bias, 0, std::move(staged.image)};
            for (const auto& s : m.image.segments) {
                require(s.bytes.size() <= budget - allocated, "batch memory budget exceeded"); allocated += s.bytes.size();
                require((s.header.flags & 3) != 3, "writable executable guest segment refused");
                require(s.header.alignment <= 1 || m.bias % s.header.alignment == 0, "misaligned load bias");
                const auto start = add(m.bias, s.header.address), end = add(start, s.header.memorySize);
                for (const auto& old : out.modules) for (const auto& region : old.image.segments) {
                    const auto a = add(old.bias, region.header.address), b = add(a, region.header.memorySize);
                    require(start >= b || a >= end, "overlapping guest modules");
                }
            }
            if (m.image.entry) {
                bool valid = false;
                for (const auto& segment : m.image.segments)
                    valid |= (segment.header.flags & 1) && m.image.entry >= segment.header.address &&
                             m.image.entry - segment.header.address < segment.header.fileSize;
                require(valid, "invalid module entry"); m.entry = add(m.bias, m.image.entry);
            }
            dynamic.push_back(parse(in, m, symbolBudget, relocationBudget)); out.modules.push_back(std::move(m));
        }
        struct Export { uint64_t address; size_t owner; };
        std::map<Identity, Export> exports;
        for (size_t i = 0; i < dynamic.size(); ++i) for (const auto& s : dynamic[i].symbols) if (s.defined && !s.local)
            require(exports.emplace(s.identity, Export{add(out.modules[i].bias, s.value), i}).second,
                    "duplicate or ambiguous export: " + s.identity.nid);
        for (const auto& h : hle) {
            require(h.identity.type == 2 && out.translate(h.guestAddress, 1, 5), "HLE requires mapped executable guest trampoline");
            require(exports.emplace(h.identity, Export{h.guestAddress, SIZE_MAX}).second, "ambiguous HLE/export");
        }
        // Resolve every import (including unreferenced/weak imports) before writes.
        std::vector<std::vector<uint64_t>> resolved(dynamic.size());
        for (size_t i = 0; i < dynamic.size(); ++i) {
            for (size_t j = 0; j < dynamic[i].symbols.size(); ++j) {
                const auto& s = dynamic[i].symbols[j]; uint64_t address = 0;
                if (s.defined) address = add(out.modules[i].bias, s.value);
                else if (j) {
                    const auto it = exports.find(s.identity);
                    require(it != exports.end(), out.modules[i].path + ": unresolved NID " + s.identity.nid +
                            " library=" + s.identity.library + " module=" + s.identity.module);
                    const auto owner = it->second.owner;
                    if (owner != SIZE_MAX && owner != i) {
                        const auto& deps = requests[order[i]].dependencies;
                        require(std::find(deps.begin(), deps.end(), out.modules[owner].path) != deps.end(), "import provider missing explicit dependency");
                        require(owner < i, "import provider not loaded first");
                    }
                    address = it->second.address;
                }
                resolved[i].push_back(address);
            }
        }
        struct Write { uint8_t* target; uint64_t value; };
        std::vector<Write> writes;
        for (size_t i = 0; i < dynamic.size(); ++i) {
            std::set<uint64_t> targets;
            auto& m = out.modules[i];
            for (const auto& r : dynamic[i].relas) {
                const auto type = uint32_t(r.info); const auto symbol = r.info >> 32;
                require(type == 1 || type == 6 || type == 7 || type == 8, "unsupported x86-64 relocation " + std::to_string(type));
                require(symbol < resolved[i].size() && (type != 8 || symbol == 0) && (type == 8 || symbol != 0), "invalid relocation symbol");
                auto* target = const_cast<uint8_t*>(m.image.translate(r.offset, 8, 6));
                require(target && !m.image.translate(r.offset, 8, 1), "relocation target outside RW/non-X guest segment");
                auto next = targets.lower_bound(r.offset);
                require((next == targets.end() || *next - r.offset >= 8) &&
                        (next == targets.begin() || r.offset - *std::prev(next) >= 8), "overlapping relocation writes");
                targets.insert(r.offset);
                // GLOB_DAT/JUMP_SLOT = S; 64 = S+A; RELATIVE = B+A (AMD64 ABI).
                const auto value = type == 8 ? signedAdd(m.bias, r.addend) : type == 1 ? signedAdd(resolved[i][symbol], r.addend) : resolved[i][symbol];
                writes.push_back({target, value});
            }
        }
        for (const auto& w : writes) for (size_t i = 0; i < 8; ++i) w.target[i] = uint8_t(w.value >> (i * 8));
        // RELRO may be writable while linking, then becomes read-only for the backend.
        for (auto& m : out.modules) for (auto& s : m.image.segments)
            if (s.header.type == 0x61000010) s.header.flags &= ~2U;
    }
};
}
