#pragma once
#include "PS4PKGRuntimePlan.hpp"
#include "OrbisTLSPlan.hpp"
#include "OrbisModuleParam.hpp"
#include "OrbisEHFrameHeader.hpp"
#include "OrbisDynamicInfo.hpp"
#include "OrbisProcessParam.hpp"
#include <map>
#include <set>
#include <tuple>
#include <stdexcept>
#include <sstream>

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
    struct Report {
        std::string phase = "inputs", failure;
        size_t validatedModules = 0, segments = 0, memoryBytes = 0;
        size_t dependencies = 0, imports = 0, resolvedImports = 0;
        size_t relocations = 0, validatedRelocations = 0, ignoredRelocations = 0, tlsRelocations = 0, moduleParams = 0, ehFrameHeaders = 0, opaqueHeaders = 0;
        size_t recognisedDynamicTags = 0, initFunctions = 0, finiFunctions = 0, initArrayEntries = 0;
        size_t processParams = 0, interpreters = 0;
        size_t tlsOffsetRelocations = 0, tlsSymbols = 0, protectedSymbols = 0, unboundWeakSymbols = 0, unboundGlobalSymbols = 0;
        bool committed = false;
        const char* stageCode() const {
            if (phase == "ELF/segments") return "ORBIS.ELF";
            if (phase == "dynamic metadata") return "ORBIS.SCE";
            if (phase == "PRX dependencies") return "ORBIS.PRX";
            if (phase == "NID imports") return "ORBIS.NID";
            if (phase == "relocations") return "ORBIS.RELOCATION";
            if (phase == "memory permissions") return "ORBIS.PERMISSIONS";
            if (phase == "complete") return "ORBIS.COMPLETE";
            return "ORBIS.INPUT";
        }
        std::string text() const {
            std::ostringstream out;
            out << "Préparation runtime Orbis (aucune exécution)\n"
                << '[' << stageCode() << "] committed=" << committed << "\n"
                << "Étape : " << phase << "\n"
                << "ELF validés : " << validatedModules << " ; segments validés : " << segments << "\n"
                << "Dépendances PRX vérifiées : " << dependencies << "\n"
                << "Imports NID examinés : " << imports << " ; résolus : " << resolvedImports << " ; non résolus : " << imports - resolvedImports << "\n"
                << "Relocations examinées : " << relocations << " ; validées : " << validatedRelocations
                << " ; appliquées : " << (committed ? validatedRelocations : 0)
                << " ; différées : " << (committed ? 0 : validatedRelocations)
                << " ; refusées : " << (relocations - validatedRelocations) << "\n"
                << "Relocations TLS DTPMOD64 validées : " << tlsRelocations << " ; DTPOFF64 : " << tlsOffsetRelocations << "\n"
                << "Symboles : TLS exportés " << tlsSymbols << " ; protégés " << protectedSymbols
                << " ; indéfinis sans NID non liés (faibles/globaux) " << unboundWeakSymbols << '/' << unboundGlobalSymbols << "\n"
                << "Relocations NONE ignorées (aucune écriture) : " << ignoredRelocations << "\n"
                << "Paramètres module SCE validés (vue PT_LOAD, sans écriture) : " << moduleParams << "\n"
                << "En-têtes EH_FRAME validés (métadonnées, aucun unwinder) : " << ehFrameHeaders << "\n"
                << "En-têtes SCE opaques sans mémoire (non lus) : " << opaqueHeaders << "\n"
                << "PROCPARAM validés (vue, pointeurs non interprétés) : " << processParams
                << " ; PT_INTERP enregistrés (jamais chargés) : " << interpreters << "\n"
                << "Tags dynamiques validés (métadonnées) : " << recognisedDynamicTags
                << " ; init/fini déclarés : " << initFunctions << '/' << finiFunctions
                << " ; entrées de tableaux init/fini : " << initArrayEntries << " ; exécutés : 0\n"
                << "Mémoire : " << memoryBytes << " octets validés ; "
                << (committed ? "image engagée" : "image non engagée, allocations provisoires libérées") << "\n";
            if (!failure.empty()) out << "Refus : " << failure << "\n";
            out << "Préparation : " << (committed ? "réussie" : "refusée")
                << " ; compteurs partiels, étapes suivantes non évaluées en cas de refus\n"
                << "Exécution : impossible — backend d'espace d'adresses FEX Orbis, pile/TLS et ABI requis";
            return out.str();
        }
    };
    struct HLE { Identity identity; uint64_t guestAddress; }; // Guest trampoline, never host function pointer.
    struct Input { PS4PKGRuntimePlan::ModuleBytes file; uint64_t bias = 0; };
    struct Module {
        std::string path; uint64_t bias = 0, entry = 0; PS4ExecutableImage image;
        OrbisModuleParam::Value moduleParam; // Module-relative; add bias for the guest address.
        OrbisEHFrameHeader::Value ehFrameHeader; // Module-relative; metadata only.
        OrbisDynamicInfo dynamic; // Validated dynamic-table metadata; never executed.
        OrbisProcessParam::Value processParam; // PROCPARAM/INTERP views; never interpreted further.
    };
    struct Mapping { uint64_t address, size; uint32_t permissions; const uint8_t* data; };
    struct Image {
        std::vector<Module> modules;
        // A backend must copy/map these bytes at their guest addresses and apply
        // the advertised permissions. No identity mapping is implied.
        std::vector<Mapping> mappings() const {
            std::vector<Mapping> result;
            for (const auto& m : modules) {
                if (m.image.requiresTLS) throw std::runtime_error("Linker mapping requires unsupported TLS");
                for (const auto& s : m.image.segments) {
                    // Image is public/mutable. Revalidate the copy contract at
                    // export, before any backend sees a pointer and byte count.
                    if (!s.header.memorySize || s.header.memorySize != s.bytes.size())
                        throw std::runtime_error("Linker mapping byte extent mismatch");
                    if (s.header.address > UINT64_MAX - m.bias)
                        throw std::runtime_error("Linker mapping bias overflow");
                    result.push_back({m.bias + s.header.address, s.header.memorySize, s.header.flags, s.bytes.data()});
                }
            }
            return result;
        }
        const uint8_t* translate(uint64_t address, size_t count, uint32_t permissions) const {
            for (const auto& m : modules) if (address >= m.bias)
                if (const auto* p = m.image.translate(address - m.bias, count, permissions)) return p;
            return nullptr;
        }
    };
    // Separate sealed staging product. Ordinary Image::mappings() continues to
    // refuse TLS; only this validated product can be mapped by the TLS runtime.
    class TLSImage {
        Image image_;
        OrbisTLSPlan::Plan plan_;
        friend class OrbisLinker;
    public:
        const Image& image() const { return image_; }
        const OrbisTLSPlan::Plan& plan() const { return plan_; }
        std::vector<Mapping> mappings() const {
            if(image_.modules.empty())throw std::runtime_error("TLS image not linked");
            std::vector<Mapping> result;
            for(const auto& m:image_.modules)for(const auto& s:m.image.segments) {
                if(!s.header.memorySize || s.header.memorySize!=s.bytes.size() || s.header.address>UINT64_MAX-m.bias)
                    throw std::runtime_error("TLS image mapping extent");
                result.push_back({m.bias+s.header.address,s.header.memorySize,s.header.flags,s.bytes.data()});
            }
            return result;
        }
    };
    static bool linkTLS(const std::vector<Input>& inputs,const std::vector<uint32_t>& ids,
                        const std::vector<HLE>& hle,TLSImage& output,std::string& diagnostic,
                        size_t budget=PS4PKGRuntimePlan::batchMemoryBudget,Report* report=nullptr) {
        Report progress;
        try {
            require(inputs.size()==ids.size(),"TLS registry cardinality");
            std::vector<OrbisTLSPlan::Input> sources;
            for(size_t i=0;i<inputs.size();++i)sources.push_back({inputs[i].file.request.logicalPath,inputs[i].file.bytes,inputs[i].file.size,ids[i]});
            TLSImage pending;
            const int status=OrbisTLSPlan::prepare(sources,pending.plan_,diagnostic);
            require(!status,diagnostic);
            std::vector<Input> normalized;
            std::map<std::string,uint32_t> registry;
            for(size_t i=0;i<inputs.size();++i) {
                const auto& m=pending.plan_.modules[i];
                normalized.push_back({{inputs[i].file.request,m.elf.data(),m.elf.size()},inputs[i].bias});
                registry.emplace(m.name,m.id);
            }
            build(normalized,hle,pending.image_,budget,progress,&registry);
            // Thread templates must reflect ALL committed relocations, including
            // ordinary pointer relocations into TLS initial bytes or TLS BSS.
            for(auto& value:pending.plan_.templates) {
                const auto source=std::find_if(pending.plan_.modules.begin(),pending.plan_.modules.end(),[&](const auto& m){return m.id==value.id;});
                require(source!=pending.plan_.modules.end(),"TLS template registry mismatch");
                const auto module=std::find_if(pending.image_.modules.begin(),pending.image_.modules.end(),[&](const auto& m){return m.path==source->name;});
                require(module!=pending.image_.modules.end(),"TLS template module absent");
                const auto tls=std::find_if(module->image.headers.begin(),module->image.headers.end(),[](const auto& h){return h.type==7;});
                require(tls!=module->image.headers.end() && tls->memorySize==value.memory_size,"TLS template extent mismatch");
                const auto* bytes=module->image.translate(tls->address,value.memory_size,4);
                require(bytes,"TLS template outside linked readable storage");
                value.initial.assign(bytes,bytes+value.memory_size);
            }
            progress.phase="complete";progress.committed=true;
            if(report)*report=progress;
            output=std::move(pending);diagnostic.clear();return true;
        } catch(const std::bad_alloc&) {diagnostic="TLS linker allocation failed";}
          catch(const std::runtime_error& e) {diagnostic=e.what();}
        progress.committed=false;
        diagnostic="["+std::string(progress.stageCode())+"] "+diagnostic;
        progress.failure=diagnostic;if(report)*report=progress;return false;
    }
    // Strong transaction: output is unchanged on every failure, including OOM.
    static bool link(const std::vector<Input>& inputs, const std::vector<HLE>& hle,
                     Image& output, std::string& diagnostic,
                     size_t budget = PS4PKGRuntimePlan::batchMemoryBudget, Report* report = nullptr) {
        Report progress;
        try {
            Image pending;
            build(inputs, hle, pending, budget, progress);
            progress.phase = "complete"; progress.committed = true;
            if (report) *report = progress;
            output = std::move(pending);
            diagnostic.clear();
            return true;
        } catch (const std::bad_alloc&) { diagnostic = "linker: allocation failed"; }
          catch (const std::runtime_error& e) { diagnostic = e.what(); }
        progress.committed = false;
        diagnostic = "[" + std::string(progress.stageCode()) + "] " + diagnostic;
        progress.failure = diagnostic;
        if (report) *report = std::move(progress);
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
    struct Symbol {
        Identity identity; uint64_t value = 0, size = 0; bool defined = false, local = false, tlsMarker = false;
        // Mission 49. unbound: undefined plain-name (non-NID) symbol, never given
        // an address; any relocation using it is refused. tls: STT_TLS, value is
        // an offset in the defining module's TLS block, never a guest address.
        bool unbound = false, weak = false, tls = false, protectedVisibility = false;
        std::string name;
    };
    struct Rela { uint64_t offset, info, addend; };
    static bool plainName(const std::string& name) {
        if (name.empty() || name.size() > 255 || (name[0] >= '0' && name[0] <= '9')) return false;
        for (char c : name) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.'))
            return false;
        return true;
    }
    struct Dynamic {
        std::vector<Symbol> symbols; std::vector<Rela> relas;
        std::vector<Descriptor> imports, exports, importLibraries, exportLibraries;
        std::vector<std::string> needed;
        OrbisDynamicInfo info;
        size_t jmprelBegin = 0, jmprelEnd = 0;
    };
    static Dynamic parse(const Input& input, const Module& m, size_t& symbolBudget, size_t& relocationBudget, bool tlsEnabled, Report& report, bool tlsLink = false) {
        const auto* bytes = input.file.bytes; const auto size = input.file.size;
        const PS4ExecutableImage::Header *dyn = nullptr, *data = nullptr;
        for (const auto& h : m.image.headers) {
            if (h.type == 2) { require(!dyn, "duplicate PT_DYNAMIC"); dyn = &h; }
            if (h.type == 0x61000000) { require(!data, "duplicate PT_SCE_DYNLIBDATA"); data = &h; }
        }
        if (!dyn && !data && m.image.type == 0xfe00 && !m.image.requiresLinker) return {};
        require(dyn && data, "SCE PT_DYNAMIC and PT_SCE_DYNLIBDATA required");
        require(dyn->fileSize && dyn->fileSize % 16 == 0 && dyn->fileSize / 16 <= 4096 &&
                range(dyn->offset, dyn->fileSize, size) && range(data->offset, data->fileSize, size),
                "dynamic metadata out of bounds");
        std::map<uint64_t, uint64_t> tags;
        std::vector<std::pair<uint64_t, uint64_t>> descriptors;
        std::vector<uint64_t> needed;
        std::vector<std::pair<uint64_t, uint64_t>> attributes; // DT_SCE_*_LIB_ATTR, one per library.
        bool terminated = false;
        for (size_t i = 0; i < dyn->fileSize; i += 16) {
            const auto* p = bytes + dyn->offset + i;
            const auto tag = read(p, 8), value = read(p + 8, 8);
            if (!tag) { terminated = true; break; }
            if (tag == 1) { needed.push_back(value); continue; } // DT_NEEDED, SCE string table offset
            if (tag == 0x6100000d || tag == 0x6100000f || tag == 0x61000013 || tag == 0x61000015) {
                descriptors.push_back({tag, value}); continue;
            }
            if (tag == 0x61000017 || tag == 0x61000019) { attributes.push_back({tag, value}); continue; }
            // Only interpreted, verified SCE table tags. Unknown semantics fail closed.
            switch (tag) {
            case 0x61000035: case 0x61000037: case 0x61000039: case 0x6100003b: case 0x6100003f:
            case 0x6100002f: case 0x61000031: case 0x61000033:
            case 0x61000029: case 0x6100002b: case 0x6100002d: break;
            // Mission 48: validated metadata only (see validation below).
            case 12: case 13: case 14: case 21: case 22: case 25: case 26: case 27: case 28:
            case 30: case 32: case 33: case 0x61000007: case 0x61000009: case 0x61000011:
            case 0x61000025: case 0x61000027: case 0x6100003d: break;
            default: throw Failure("unsupported dynamic tag " + std::to_string(tag) + " module=" + m.path);
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
        for (const auto& d : importLib) out.importLibraries.push_back(d.second);
        for (const auto& d : exportLib) out.exportLibraries.push_back(d.second);
        for (const auto& d : importMod) out.imports.push_back(d.second);
        for (const auto& d : exportMod) out.exports.push_back(d.second);
        auto relocations = [&](uint64_t offsetTag, uint64_t sizeTag, uint64_t kindTag, uint64_t expected) {
            if (!tags.count(offsetTag) && !tags.count(sizeTag) && !tags.count(kindTag)) return;
            require(get(kindTag) == expected, "unsupported relocation table encoding");
            const auto n = get(sizeTag); require(n % 24 == 0 && n / 24 <= relocationBudget, "relocation table budget or size");
            relocationBudget -= static_cast<size_t>(n / 24);
            const auto* p = table(get(offsetTag), n);
            for (size_t i = 0; i < n; i += 24) out.relas.push_back({read(p + i, 8), read(p + i + 8, 8), read(p + i + 16, 8)});
        };
        // Dynamic-table metadata is validated before the symbol entries.
        relocations(0x6100002f, 0x61000031, 0x61000033, 24);
        out.jmprelBegin = out.relas.size();
        relocations(0x61000029, 0x6100002d, 0x6100002b, 7); // DT_RELA
        out.jmprelEnd = out.relas.size();
        validateDynamic(out, m, tags, descriptors, attributes, symbolSize / 24,
                        [&](uint64_t offset, uint64_t length) { return table(offset, length); },
                        [&](uint64_t offset) { return stringAt(offset); }, strings, stringSize);
        report.recognisedDynamicTags += out.info.recognisedTags; // Counted even if a later symbol is refused.
        for (auto offset : needed) {
            const auto name = stringAt(offset);
            require(!name.empty() && std::find(out.needed.begin(), out.needed.end(), name) == out.needed.end(),
                    "empty or duplicate DT_NEEDED");
            out.needed.push_back(name);
        }
        for (size_t i = 0; i < symbolSize; i += 24) {
            const auto* p = symbols + i;
            Symbol s; const auto name = stringAt(read(p, 4));
            const uint8_t bind = p[4] >> 4, type = p[4] & 15;
            const auto section = read(p + 6, 2);
            s.value = read(p + 8, 8); s.size = read(p + 16, 8); s.defined = section != 0; s.local = bind == 0;
            if (!i) { require(read(p, 8) == 0 && s.value == 0 && s.size == 0, "invalid null symbol"); out.symbols.push_back(s); continue; }
            if(tlsEnabled && type==3 && !bind && !section && !p[5] && name.empty() && !s.value && !s.size) {
                s.tlsMarker=true;out.symbols.push_back(s);continue;
            }
            s.name = name; s.weak = bind == 2;
            // Undefined plain-name symbols (e.g. weak module_start/module_stop,
            // __start__Ztext): ELF lookups happen only through relocations, and
            // none is accepted for them, so no address is ever invented.
            if (!section && type == 0 && (bind == 1 || bind == 2) && !p[5] && !s.value && !s.size && plainName(name)) {
                s.unbound = true; out.symbols.push_back(std::move(s)); continue;
            }
            if (type == 6) {
                // A general-dynamic consumer may import TLS without owning PT_TLS.
                require(tlsLink && (tlsEnabled || !section), "STT_TLS symbol requires the TLS linking path: " + name + " module=" + m.path);
                require(bind == 1 || bind == 2, "local STT_TLS symbol unsupported: " + name);
                if (section) {
                    const PS4ExecutableImage::Header* tlsHeader = nullptr;
                    for (const auto& h : m.image.headers) if (h.type == 7) tlsHeader = &h;
                    require(tlsHeader && s.value <= tlsHeader->memorySize && s.size <= tlsHeader->memorySize - s.value,
                            "STT_TLS symbol outside PT_TLS: " + name + " module=" + m.path);
                }
                s.tls = true;
            }
            // STV_PROTECTED: exported but bound locally. Defined symbols are never
            // preempted by this linker, which is exactly that guarantee.
            s.protectedVisibility = p[5] == 3 && section;
            require(bind <= 2 && (type == 0 || type == 1 || type == 2 || type == 6) && (p[5] == 0 || s.protectedVisibility) &&
                    section < 0xff00, "unsupported symbol binding/type/visibility/section: " + name + " module=" + m.path);
            if (s.defined) require(s.tls || m.image.translate(s.value, s.size ? s.size : 1, type == 2 ? 5 : 4), "export outside valid segment");
            else require(s.value == 0 && !s.local, "invalid undefined symbol: " + name + " module=" + m.path);
            if (s.defined && type == 2) {
                bool backed = false;
                for (const auto& region : m.image.segments) {
                    if ((region.header.flags & 5) != 5 || s.value < region.header.address) continue;
                    const auto offset = s.value - region.header.address;
                    backed |= offset < region.header.fileSize &&
                        (s.size ? s.size : 1) <= region.header.fileSize - offset;
                }
                require(backed, "function export outside file-backed code");
            }
            if (!s.local) {
                const auto a = name.find('#'), b = a == std::string::npos ? a : name.find('#', a + 1);
                require(a == 11 && b != std::string::npos && name.find('#', b + 1) == std::string::npos, "invalid NID identity: " + name + " module=" + m.path);
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
        return out;
    }
    // Mission 48. Each accepted tag has a proven layout; none is executed,
    // used for lookup or written. Unlisted values/bits fail closed.
    template <class Table, class String>
    static void validateDynamic(Dynamic& out, const Module& m, const std::map<uint64_t, uint64_t>& tags,
                                const std::vector<std::pair<uint64_t, uint64_t>>& descriptors,
                                const std::vector<std::pair<uint64_t, uint64_t>>& attributes, uint64_t symbolCount,
                                Table table, String stringAt, const uint8_t* strings, uint64_t stringSize) {
        auto& info = out.info;
        auto has = [&](uint64_t tag) { return tags.count(tag) != 0; };
        auto get = [&](uint64_t tag) { return tags.at(tag); };
        auto backed = [&](uint64_t address, uint64_t bytes, bool code) {
            for (const auto& s : m.image.segments) {
                const bool kind = code ? (s.header.type == 1 && (s.header.flags & 5) == 5) : !(s.header.flags & 1);
                if (kind && address >= s.header.address && range(address - s.header.address, bytes, s.header.fileSize)) return true;
            }
            return false;
        };
        static const uint64_t known[] = {12, 13, 14, 21, 22, 25, 26, 27, 28, 30, 32, 33,
            0x61000007, 0x61000009, 0x61000011, 0x61000025, 0x61000027, 0x6100003d};
        for (auto tag : known) info.recognisedTags += has(tag);
        info.recognisedTags += attributes.size();
        if (has(0x61000025) || has(0x6100003d)) {
            require(has(0x61000025) && has(0x6100003d), "DT_SCE_HASH/DT_SCE_HASHSZ pair incomplete");
            const auto offset = get(0x61000025), bytes = get(0x6100003d);
            require(offset % 4 == 0 && bytes <= 4 * 1024 * 1024, "DT_SCE_HASH alignment or budget");
            const char* problem = OrbisDynamicInfo::validateHashStructure(table(offset, bytes), bytes, symbolCount,
                                                                          info.buckets, info.chains);
            require(!problem, std::string("DT_SCE_HASH: ") + (problem ? problem : ""));
            info.hash = true;
        }
        if (has(0x61000007)) {
            const auto* p = table(get(0x61000007), 20);
            std::copy(p, p + 20, info.fingerprintBytes); info.fingerprint = true;
        }
        if (has(14)) {
            info.soname = stringAt(get(14));
            const auto slash = m.path.find_last_of('/');
            require(info.soname == (slash == std::string::npos ? m.path : m.path.substr(slash + 1)),
                    "DT_SONAME differs from module file name");
        }
        if (has(0x61000009)) {
            // Informational build path; may contain spaces, never used to open files.
            const auto offset = get(0x61000009);
            require(offset < stringSize, "string offset out of bounds");
            const auto available = std::min<uint64_t>(stringSize - offset, 1024);
            const auto* end = static_cast<const uint8_t*>(std::memchr(strings + offset, 0, size_t(available)));
            require(end, "unterminated or oversized string");
            for (auto* p = strings + offset; p != end; ++p) require(*p >= 0x20 && *p <= 0x7e, "invalid original filename");
            info.originalFilename.assign(reinterpret_cast<const char*>(strings + offset), end - (strings + offset));
        }
        require(!has(0x61000011) || get(0x61000011) == 0, "DT_SCE_MODULE_ATTR value unsupported");
        require(!has(21) || get(21) == 0, "DT_DEBUG value unsupported");
        require(!has(22) || get(22) == 0, "DT_TEXTREL value unsupported");
        require(!has(30) || (get(30) & ~uint64_t{4}) == 0, "DT_FLAGS bits unsupported");
        info.textrel = has(22) || (has(30) && (get(30) & 4));
        std::map<uint16_t, bool> exportIds, importIds;
        for (const auto& d : descriptors) {
            if (d.first == 0x61000013) exportIds[uint16_t(d.second >> 48)] = true;
            if (d.first == 0x61000015) importIds[uint16_t(d.second >> 48)] = true;
        }
        for (const auto& a : attributes) {
            const bool exported = a.first == 0x61000017;
            const auto id = uint16_t(a.second >> 48);
            const uint64_t value = a.second & 0xffffffffffffULL;
            require((exported ? exportIds : importIds).count(id), "library attribute without library descriptor");
            require(value == (exported ? 1U : 9U), "library attribute value unsupported");
            require((exported ? info.exportLibraryAttributes : info.importLibraryAttributes).emplace(id, value).second,
                    "duplicate library attribute");
        }
        for (auto tag : {uint64_t{12}, uint64_t{13}}) if (has(tag)) {
            require(backed(get(tag), 1, true), tag == 12 ? "DT_INIT outside file-backed code" : "DT_FINI outside file-backed code");
            (tag == 12 ? info.initAddress : info.finiAddress) = get(tag);
            (tag == 12 ? info.init : info.fini) = true;
        }
        const struct { uint64_t address, size; OrbisDynamicInfo::Array* array; const char* name; } arrays[] = {
            {32, 33, &info.preinitArray, "DT_PREINIT_ARRAY"}, {25, 27, &info.initArray, "DT_INIT_ARRAY"},
            {26, 28, &info.finiArray, "DT_FINI_ARRAY"}};
        for (const auto& a : arrays) {
            if (!has(a.address) && !has(a.size)) continue;
            require(has(a.address) && has(a.size), std::string(a.name) + " pair incomplete");
            const auto address = get(a.address), bytes = get(a.size);
            require(address % 8 == 0 && bytes % 8 == 0 && bytes <= 64 * 1024 && backed(address, bytes, false),
                    std::string(a.name) + " outside file-backed non-executable data");
            *a.array = {true, address, bytes};
        }
        if (has(0x61000027)) {
            const auto got = get(0x61000027);
            require(got % 8 == 0 && backed(got, 24, false), "DT_SCE_PLTGOT outside file-backed non-executable data");
            for (size_t i = out.jmprelBegin; i < out.jmprelEnd; ++i)
                require(out.relas[i].offset >= got + 24 || out.relas[i].offset + 8 <= got, "JMPREL writes reserved PLTGOT entries");
            info.pltgot = true; info.pltgotAddress = got;
        }
    }
    static void build(const std::vector<Input>& inputs, const std::vector<HLE>& hle, Image& out, size_t budget, Report& report,const std::map<std::string,uint32_t>* tlsRegistry=nullptr) {
        require(!inputs.empty() && inputs.size() <= PS4PKGRuntimePlan::maxDependencies, "invalid module count (1..128)");
        for (const auto& input : inputs) {
            require(input.file.request.logicalPath.size() <= 255 && input.file.request.dependencies.size() <= 128,
                    "module request budget exceeded");
            for (const auto& dependency : input.file.request.dependencies)
                require(dependency.size() <= 255, "dependency path budget exceeded");
        }
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
        report.phase = "ELF/segments";
        // Check the complete segment budget before allocating any staged image.
        // The existing loader still validates every field independently below.
        size_t planned = 0;
        for (auto index : sorted) {
            const auto& input = inputs[index];
            const auto* b = input.file.bytes; const auto n = input.file.size;
            require(b && n >= 64 && n <= 64 * 1024 * 1024, "chargeur: invalid ELF file size");
            require(!std::memcmp(b,"\177ELF",4), "[ELF.CONTAINER] ELF Orbis autonome non chiffré requis ; CNT/SELF refusé");
            const auto phoff = read(b + 32, 8), phnum = read(b + 56, 2);
            require(phoff >= 64 && phnum && phnum <= 128 && range(phoff, phnum * 56, n), "chargeur: invalid program table");
            for (size_t i = 0; i < phnum; ++i) {
                const auto* p = b + phoff + i * 56; const auto type = read(p, 4);
                if (type != 1 && type != 0x61000010) continue;
                const auto size = read(p + 40, 8);
                require(size <= budget - planned, "batch memory budget exceeded"); planned += static_cast<size_t>(size);
            }
        }
        size_t allocated = 0, symbolBudget = 65536, relocationBudget = 65536;
        for (auto i : order) {
            report.phase = "ELF/segments";
            const auto& in = inputs[sorted[i]];
            PS4PKGRuntimePlan::PreparedModule staged;
            require(PS4PKGRuntimePlan::prepareStandalone(in.file.request.logicalPath, in.file.bytes, in.file.size,
                    in.file.request.dependencies, staged, error), error);
            ++report.validatedModules;
            require(!staged.image.requiresTLS || (tlsRegistry && tlsRegistry->count(staged.logicalPath)), "TLS not supported: module="+staged.logicalPath);
            Module m {staged.logicalPath, in.bias, 0, std::move(staged.image), {}, {}, {}, {}};
            for (const auto& h : m.image.headers) {
                require(h.type == 0 || h.type == 1 || h.type == 2 || h.type == 4 || h.type == 6 ||
                        h.type == 7 || h.type == 0x61000000 || h.type == 0x61000010 ||
                        h.type == OrbisModuleParam::programType || h.type == OrbisEHFrameHeader::programType ||
                        h.type == 0x6fffff00 || h.type == 0x6fffff01 || h.type == OrbisProcessParam::programType || h.type == 3,
                        "unsupported runtime program header " + std::to_string(h.type)+" module="+m.path);
                // 0x6fffff00/0x6fffff01: no semantics assumed. Accepted only as
                // inert entries with no address, protection or memory footprint;
                // their bytes are never read (0x6fffff01 has none in the SELF).
                if (h.type == 0x6fffff00 || h.type == 0x6fffff01) {
                    require(!h.memorySize && !h.address && !h.flags,
                            "SCE header " + std::to_string(h.type) + " with memory footprint refused module=" + m.path);
                    ++report.opaqueHeaders;
                }
            }
            const bool paramValid = OrbisModuleParam::validate(m.image, m.moduleParam, error);
            require(paramValid, error + " module=" + m.path);
            if (m.moduleParam.present) ++report.moduleParams;
            const bool processValid = OrbisProcessParam::validate(m.image, m.processParam, error);
            require(processValid, error + " module=" + m.path);
            report.processParams += m.processParam.present; report.interpreters += !m.processParam.interpreter.empty();
            const bool ehValid = OrbisEHFrameHeader::validate(m.image, m.ehFrameHeader, error);
            require(ehValid, error + " module=" + m.path);
            if (m.ehFrameHeader.present) ++report.ehFrameHeaders;
            for (const auto& s : m.image.segments) {
                require(s.bytes.size() <= budget - allocated, "batch memory budget exceeded"); allocated += s.bytes.size();
                require((s.header.flags & 3) != 3, "writable executable guest segment refused");
                require(s.header.alignment <= 1 || m.bias % s.header.alignment == 0, "misaligned load bias");
                const auto start = add(m.bias, s.header.address), end = add(start, s.header.memorySize);
                require(end <= (uint64_t{1} << 47), "non-canonical user guest address");
                for (const auto& old : out.modules) for (const auto& region : old.image.segments) {
                    const auto a = add(old.bias, region.header.address), b = add(a, region.header.memorySize);
                    require(start >= b || a >= end, "overlapping guest modules");
                }
                ++report.segments; report.memoryBytes += s.bytes.size();
            }
            if (m.image.entry) {
                bool valid = false;
                for (const auto& segment : m.image.segments)
                    valid |= (segment.header.flags & 1) && m.image.entry >= segment.header.address &&
                             m.image.entry - segment.header.address < segment.header.fileSize;
                require(valid, "invalid module entry"); m.entry = add(m.bias, m.image.entry);
            }
            report.phase = "dynamic metadata";
            auto parsed = parse(in, m, symbolBudget, relocationBudget,tlsRegistry!=nullptr && m.image.requiresTLS, report, tlsRegistry!=nullptr);
            m.dynamic = parsed.info;
            dynamic.push_back(std::move(parsed)); out.modules.push_back(std::move(m));
        }
        report.phase = "PRX dependencies";
        // Validate even unused module descriptors: symbols alone cannot prove a
        // closed dependency graph. Only explicit supplied files can be providers.
        for (size_t i = 0; i < dynamic.size(); ++i) {
            const auto& deps = requests[order[i]].dependencies;
            auto validateDescriptor = [&](const Descriptor& required, bool library) {
                size_t named = 0, compatible = 0, declared = 0;
                for (size_t j = 0; j < dynamic.size(); ++j) {
                    const auto& offeredDescriptors = library ? dynamic[j].exportLibraries : dynamic[j].exports;
                    for (const auto& offered : offeredDescriptors) if (offered.name == required.name) {
                        ++named;
                        if (offered.version != required.version) continue;
                        ++compatible;
                        if (std::find(deps.begin(), deps.end(), out.modules[j].path) != deps.end()) ++declared;
                    }
                }
                const auto kind = library ? " PRX library: " : " PRX module: ";
                require(named != 0, "missing" + std::string(kind) + required.name);
                require(compatible != 0, "incompatible" + std::string(kind) + required.name);
                require(declared != 0, "missing explicit dependency:" + std::string(kind) + required.name);
                require(declared == 1, "ambiguous" + std::string(kind) + required.name);
            };
            for (const auto& required : dynamic[i].imports) {
                validateDescriptor(required, false); ++report.dependencies;
            }
            for (const auto& required : dynamic[i].importLibraries) validateDescriptor(required, true);
            for (const auto& required : dynamic[i].needed) {
                require(std::find(deps.begin(), deps.end(), required) != deps.end(),
                        "DT_NEEDED missing explicit dependency: " + required);
                ++report.dependencies;
            }
        }
        report.phase = "NID imports";
        struct Export { uint64_t address; size_t owner; };
        std::map<Identity, Export> exports;
        struct TLSExport { size_t owner; uint64_t offset, size; };
        std::map<Identity, TLSExport> tlsExports; // Offsets in the owner's TLS block (DTV module id).
        for (size_t i = 0; i < dynamic.size(); ++i) for (const auto& s : dynamic[i].symbols) if (s.defined && !s.local) {
            if (s.tls) {
                require(tlsExports.emplace(s.identity, TLSExport{i, s.value, s.size}).second, "duplicate or ambiguous TLS export: " + s.identity.nid);
                ++report.tlsSymbols; continue;
            }
            require(exports.emplace(s.identity, Export{add(out.modules[i].bias, s.value), i}).second,
                    "duplicate or ambiguous export: " + s.identity.nid);
            report.protectedSymbols += s.protectedVisibility;
        }
        for (const auto& h : hle) {
            require(h.identity.type == 2 && out.translate(h.guestAddress, 1, 5), "HLE requires mapped executable guest trampoline");
            require(exports.emplace(h.identity, Export{h.guestAddress, SIZE_MAX}).second, "ambiguous HLE/export");
        }
        // Resolve every import (including unreferenced/weak imports) before writes.
        std::vector<std::vector<uint64_t>> resolved(dynamic.size());
        std::vector<std::map<size_t, TLSExport>> tlsResolved(dynamic.size());
        for (size_t i = 0; i < dynamic.size(); ++i) {
            for (size_t j = 0; j < dynamic[i].symbols.size(); ++j) {
                const auto& s = dynamic[i].symbols[j]; uint64_t address = 0;
                if(s.tlsMarker) {resolved[i].push_back(0);continue;}
                if (s.unbound) { ++(s.weak ? report.unboundWeakSymbols : report.unboundGlobalSymbols); resolved[i].push_back(0); continue; }
                if (s.tls) {
                    if (s.defined) tlsResolved[i][j] = {i, s.value, s.size};
                    else {
                        ++report.imports;
                        const auto it = tlsExports.find(s.identity);
                        require(it != tlsExports.end(), out.modules[i].path + ": unresolved TLS NID " + s.identity.nid +
                                " library=" + s.identity.library + " module=" + s.identity.module);
                        const auto owner = it->second.owner;
                        if (owner != i) {
                            const auto& deps = requests[order[i]].dependencies;
                            require(std::find(deps.begin(), deps.end(), out.modules[owner].path) != deps.end(), "import provider missing explicit dependency");
                            require(owner < i, "import provider not loaded first");
                        }
                        tlsResolved[i][j] = it->second; ++report.resolvedImports;
                    }
                    resolved[i].push_back(0); continue;
                }
                if (s.defined) address = add(out.modules[i].bias, s.value);
                else if (j) {
                    ++report.imports;
                    const auto it = exports.find(s.identity);
                    require(it != exports.end(), out.modules[i].path + ": unresolved NID " + s.identity.nid +
                            " library=" + s.identity.library + " module=" + s.identity.module);
                    const auto owner = it->second.owner;
                    if (owner != SIZE_MAX && owner != i) {
                        const auto& deps = requests[order[i]].dependencies;
                        require(std::find(deps.begin(), deps.end(), out.modules[owner].path) != deps.end(), "import provider missing explicit dependency");
                        require(owner < i, "import provider not loaded first");
                    }
                    address = it->second.address; ++report.resolvedImports;
                }
                resolved[i].push_back(address);
            }
        }
        report.phase = "relocations";
        struct Write { uint8_t* target; uint64_t value; };
        std::vector<Write> writes;
        std::vector<std::set<uint64_t>> pointerTargets(dynamic.size());
        for (size_t i = 0; i < dynamic.size(); ++i) {
            std::set<uint64_t> targets;
            auto& m = out.modules[i];
            for (const auto& r : dynamic[i].relas) {
                const auto type = uint32_t(r.info); const auto symbol = r.info >> 32;
                // AMD64 R_X86_64_NONE has no effect: neither the target nor the
                // symbol/addend is used, and it cannot overlap a real write.
                if (type == 0) { ++report.ignoredRelocations; continue; }
                ++report.relocations;
                require(type == 1 || type == 6 || type == 7 || type == 8 || ((type==16 || type==17) && tlsRegistry), "unsupported x86-64 relocation " + std::to_string(type));
                require(symbol < resolved[i].size() && (type != 8 || symbol == 0) && (type == 8 || type==16 || symbol != 0), "invalid relocation symbol");
                const auto& used = dynamic[i].symbols[symbol];
                require(!used.unbound, "relocation uses unbound " + std::string(used.weak ? "weak" : "global") +
                        " undefined symbol " + used.name + " (no NID provider, no invented address) module=" + m.path);
                uint64_t tlsValue = 0;
                if(type==16 && (symbol==0 || used.tlsMarker)) {
                    require(!r.addend && tlsRegistry->count(m.path),"TLS local module relocation required");
                    tlsValue = tlsRegistry->at(m.path);
                } else if(type==16 || type==17) {
                    // General-dynamic TLS: tls_index {module id, offset} for the existing DTV.
                    require(used.tls && tlsResolved[i].count(symbol), "TLS relocation requires an STT_TLS symbol module=" + m.path);
                    const auto& t = tlsResolved[i].at(symbol);
                    require(tlsRegistry->count(out.modules[t.owner].path), "TLS symbol owner absent from TLS registry");
                    if (type == 16) { require(!r.addend, "DTPMOD64 addend must be zero"); tlsValue = tlsRegistry->at(out.modules[t.owner].path); }
                    else {
                        require(!(r.addend >> 63) && r.addend < (t.size ? t.size : 1), "DTPOFF64 outside TLS symbol module=" + m.path);
                        tlsValue = t.offset + r.addend; ++report.tlsOffsetRelocations;
                    }
                } else require(!used.tlsMarker && !used.tls,"TLS symbol used by non-TLS relocation module=" + m.path);
                auto* target = const_cast<uint8_t*>(m.image.translate(r.offset, 8, 6));
                // DT_TEXTREL never makes code writable: W^X staging is unchanged.
                require(!m.dynamic.textrel || !m.image.translate(r.offset, 8, 1),
                        "DT_TEXTREL relocation into executable segment refused (W^X) module=" + m.path);
                require(target && !m.image.translate(r.offset, 8, 1), "relocation target outside RW/non-X guest segment");
                // PROCPARAM: only its two pointer fields, only by RELATIVE into module data.
                if (m.processParam.present && r.offset < m.processParam.address + OrbisProcessParam::size &&
                    m.processParam.address < r.offset + 8) {
                    const auto field = r.offset - m.processParam.address;
                    require(r.offset >= m.processParam.address && field >= OrbisProcessParam::firstPointer && field % 8 == 0 && type == 8,
                            "PT_SCE_PROCPARAM write outside its RELATIVE pointer fields module=" + m.path);
                    bool data = false;
                    for (const auto& seg : m.image.segments)
                        data |= !(seg.header.flags & 1) && !(r.addend >> 63) && r.addend >= seg.header.address &&
                                r.addend - seg.header.address + 8 <= seg.header.fileSize;
                    require(data, "PT_SCE_PROCPARAM pointer outside file-backed module data module=" + m.path);
                }
                // The verified module-param profile has no pointer field.
                require(!m.moduleParam.present || r.offset >= m.moduleParam.address + OrbisModuleParam::size ||
                        m.moduleParam.address >= r.offset + 8, "relocation into PT_SCE_MODULE_PARAM refused");
                auto next = targets.lower_bound(r.offset);
                require((next == targets.end() || *next - r.offset >= 8) &&
                        (next == targets.begin() || r.offset - *std::prev(next) >= 8), "overlapping relocation writes");
                targets.insert(r.offset);
                // GLOB_DAT/JUMP_SLOT = S; 64 = S+A; RELATIVE = B+A (AMD64 ABI).
                const auto value = (type==16 || type==17) ? tlsValue : type == 8 ? signedAdd(m.bias, r.addend) : type == 1 ? signedAdd(resolved[i][symbol], r.addend) : resolved[i][symbol];
                writes.push_back({target, value}); ++report.validatedRelocations;
                if(type==16)++report.tlsRelocations;
                if(type==1 || type==8) pointerTargets[i].insert(r.offset);
            }
            if (m.processParam.present) for (uint64_t f = 0; f < OrbisProcessParam::pointerCount; ++f) {
                const auto address = m.processParam.address + OrbisProcessParam::firstPointer + 8 * f;
                const auto* raw = m.image.translate(address, 8, 4);
                require(raw && (!read(raw, 8) || pointerTargets[i].count(address)),
                        "PT_SCE_PROCPARAM pointer field without RELATIVE relocation module=" + m.path);
            }
            // Init/fini array entries must each be produced by a pointer
            // relocation; their final values are checked after the writes.
            for (const auto* a : {&m.dynamic.preinitArray, &m.dynamic.initArray, &m.dynamic.finiArray})
                for (uint64_t e = 0; a->present && e < a->size; e += 8)
                    require(pointerTargets[i].count(a->address + e), "init/fini array entry without pointer relocation module=" + m.path);
        }
        // Values are final only once every write is known; check before applying.
        for (size_t i = 0; i < dynamic.size(); ++i) {
            const auto& m = out.modules[i];
            for (const auto* a : {&m.dynamic.preinitArray, &m.dynamic.initArray, &m.dynamic.finiArray})
                for (uint64_t e = 0; a->present && e < a->size; e += 8) {
                    const auto* slot = m.image.translate(a->address + e, 8, 4);
                    const auto w = std::find_if(writes.begin(), writes.end(), [&](const Write& x) { return x.target == slot; });
                    require(w != writes.end() && w->value >= m.bias, "init/fini array entry unresolved module=" + m.path);
                    bool code = false;
                    for (const auto& s : m.image.segments)
                        code |= s.header.type == 1 && (s.header.flags & 5) == 5 && w->value - m.bias >= s.header.address &&
                                w->value - m.bias - s.header.address < s.header.fileSize;
                    require(code, "init/fini array entry outside module code module=" + m.path);
                    ++report.initArrayEntries;
                }
            report.initFunctions += m.dynamic.init; report.finiFunctions += m.dynamic.fini;
        }
        report.phase = "memory permissions";
        for (const auto& w : writes) for (size_t i = 0; i < 8; ++i) w.target[i] = uint8_t(w.value >> (i * 8));
        // RELRO may be writable while linking, then becomes read-only for the backend.
        for (auto& m : out.modules) for (auto& s : m.image.segments)
            if (s.header.type == 0x61000010) s.header.flags &= ~2U;
    }
};
}
