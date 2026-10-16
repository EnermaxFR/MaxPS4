#pragma once
#include "OrbisMetadata.hpp"

namespace maxps4 {
// Transactional preparation in ordinary byte vectors only. No host executable
// memory, JIT-provider access, guest dispatch, constructor calls or DRM handling.
class OrbisPRXPreparation {
public:
    struct Input { std::string name; std::vector<uint8_t> bytes; uint64_t base; };
    struct Region { uint64_t address; uint32_t flags; std::vector<uint8_t> bytes; };
    struct Module { std::string name; OrbisMetadata metadata; uint64_t base; std::vector<Region> regions; };
    std::vector<Module> modules;
    std::vector<size_t> initializationOrder;
    std::string error;
    void unload() { std::vector<Module>().swap(modules); std::vector<size_t>().swap(initializationOrder); }
    bool prepare(const std::vector<Input>& inputs) {
        unload(); error.clear();
        OrbisPRXPreparation pending;
        try { pending.build(inputs); modules = std::move(pending.modules);
            initializationOrder = std::move(pending.initializationOrder); return true;
        } catch (const std::exception& e) { error = e.what(); return false; }
    }
private:
    static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
    static uint64_t sum(uint64_t a, uint64_t b) { check(a <= UINT64_MAX-b,"Débordement base PRX"); return a+b; }
    static uint64_t addSigned(uint64_t value, int64_t delta) {
        if (delta >= 0) return sum(value,uint64_t(delta));
        uint64_t magnitude = uint64_t(-(delta+1))+1;
        check(value >= magnitude,"Sous-débordement RELA PRX"); return value-magnitude;
    }
    static uint8_t* target(Module& m, uint64_t address, uint32_t flags) {
        for (auto& r : m.regions) if ((r.flags & flags) == flags && address >= r.address &&
            OrbisMetadata::range(address-r.address,8,r.bytes.size())) return r.bytes.data()+address-r.address;
        return nullptr;
    }
    void build(const std::vector<Input>& inputs) {
        check(!inputs.empty() && inputs.size() <= 32,"Nombre de PRX invalide (1..32)");
        size_t allocated = 0, fileBytes = 0;
        for (const auto& input : inputs) {
            check(input.bytes.size() <= OrbisMetadata::fileLimit-fileBytes,"Budget total de fichiers PRX dépassé");
            fileBytes += input.bytes.size();
            Module m; m.name = input.name; m.base = input.base;
            check(!m.name.empty() && !(m.base & 0x3fff),"Nom/base PRX invalide (base alignée 16 Kio requise)");
            for (const auto& old : modules) check(old.name != m.name,"Nom de fichier PRX dupliqué");
            if (!m.metadata.parse(input.bytes.data(),input.bytes.size())) throw std::runtime_error(m.metadata.error);
            check(m.metadata.type == 0xfe18,"Seul ET_SCE_DYNAMIC est préparé comme PRX");
            // Unknown metadata cannot silently acquire executable semantics.
            for (const auto& b : m.metadata.blockers) check(b == "Imports non résolus ; aucune réussite simulée" ||
                b == "Dépendances PRX externes : fichiers non sélectionnés par cette analyse" ||
                b == "Initialisation/finalisation PRX déclarée mais non exécutée", b.c_str());
            check(m.metadata.modules.size() <= 128,"Trop d'identités PRX");
            size_t identities = 0;
            for (const auto& id : m.metadata.modules) identities += id.exported;
            check(identities == 1,"Une identité module exportée requise pour un PRX");
            for (const auto& s : m.metadata.segments) if (s.type == 1 || s.type == 0x61000010) {
                check((s.flags & 3) != 3,"PRX W+X refusé");
                check(s.memsz <= OrbisMetadata::fileLimit-allocated,"Budget mémoire PRX dépassé");
                check(s.align <= 1 || m.base%s.align == 0,"Biais PRX incompatible avec p_align");
                uint64_t address = sum(m.base,s.address);
                check(sum(address,s.memsz) <= (uint64_t(1)<<47),"Adresse PRX non canonique utilisateur");
                for (const auto& old : modules) for (const auto& r : old.regions)
                    check(!s.memsz || r.bytes.empty() || address >= r.address+r.bytes.size() ||
                        r.address >= address+s.memsz,"Modules PRX se chevauchant");
                // Reject overlapping PT_LOAD/SCE_RELRO regions within the same
                // module as well as between modules. Otherwise target() could
                // silently choose the first writable alias for a relocation.
                for (const auto& r : m.regions)
                    check(!s.memsz || r.bytes.empty() ||
                        address >= r.address+r.bytes.size() ||
                        r.address >= address+s.memsz,
                        "Segments PRX du même module se chevauchant");
                Region region{address,s.flags,std::vector<uint8_t>(size_t(s.memsz),0)};
                if (s.filesz) std::memcpy(region.bytes.data(),input.bytes.data()+s.offset,size_t(s.filesz));
                allocated += size_t(s.memsz); m.regions.push_back(std::move(region));
            }
            for (const auto& old : modules) for (const auto& a : old.metadata.modules) if (a.exported)
                for (const auto& b : m.metadata.modules) if (b.exported)
                    check(a.name != b.name,"Identité module PRX ambiguë");
            modules.push_back(std::move(m));
        }
        auto provider = [&](const OrbisMetadata::Identity& identity) {
            size_t result = modules.size();
            for (size_t i = 0; i < modules.size(); ++i) for (const auto& offered : modules[i].metadata.modules)
                if (offered.exported && offered.name == identity.name && offered.version == identity.version) {
                    check(result == modules.size(),"Dépendance module ambiguë"); result = i;
                }
            check(result != modules.size(),"Dépendance/version PRX absente"); return result;
        };
        std::vector<std::vector<size_t>> edges(modules.size());
        for (size_t i = 0; i < modules.size(); ++i) {
            for (const auto& id : modules[i].metadata.modules) if (!id.exported) edges[i].push_back(provider(id));
            for (const auto& name : modules[i].metadata.needed) {
                auto found = std::find_if(modules.begin(),modules.end(),[&](const Module& m){return m.name == name;});
                check(found != modules.end(),"DT_NEEDED sans fichier PRX fourni");
                edges[i].push_back(size_t(found-modules.begin()));
            }
        }
        std::vector<uint8_t> marks(modules.size());
        std::function<void(size_t)> visit = [&](size_t i) {
            check(marks[i] != 1,"Cycle de dépendances PRX non pris en charge");
            if (marks[i] == 2) return;
            marks[i] = 1;
            for (size_t dep : edges[i]) visit(dep);
            marks[i] = 2; initializationOrder.push_back(i);
        };
        for (size_t i = 0; i < modules.size(); ++i) visit(i);
        for (auto& m : modules) {
            std::vector<uint64_t> addresses(m.metadata.symbols.size());
            for (size_t i = 1; i < m.metadata.symbols.size(); ++i) {
                auto& s = m.metadata.symbols[i];
                check(s.kind == 0 || s.kind == 1 || s.kind == 2,"Type de symbole PRX non pris en charge");
                check(s.binding == 0 || s.binding == 1,"Symboles weak/GNU non pris en charge");
                if (s.defined) addresses[i] = sum(m.base,s.value);
                else if (!s.encoded.empty()) {
                    check(s.qualified && s.binding == 1,"Import PRX non qualifié/global");
                    size_t hits = 0;
                    size_t current = size_t(&m-modules.data());
                    std::set<size_t> dependencies(edges[current].begin(),edges[current].end());
                    for (size_t dep : dependencies) for (const auto& exported : modules[dep].metadata.symbols)
                        if (exported.defined && exported.qualified && exported.binding == 1 &&
                            exported.nid == s.nid && exported.library == s.library && exported.module == s.module &&
                            exported.libraryVersion == s.libraryVersion && exported.moduleVersion == s.moduleVersion) {
                            addresses[i] = sum(modules[dep].base,exported.value); ++hits;
                        }
                    check(hits == 1,"Import PRX inconnu ou ambigu ; aucun stub de réussite");
                    s.resolved = true;
                }
            }
            for (const auto& r : m.metadata.relocations) {
                check(r.supported,"Relocation PRX non prise en charge");
                if (r.type == 0) continue;
                uint64_t value = 0;
                if (r.type == 8) value = addSigned(m.base,r.addend);
                else {
                    check(r.symbol && r.symbol < addresses.size(),"Relocation PRX sans symbole");
                    check(m.metadata.symbols[r.symbol].defined || m.metadata.symbols[r.symbol].resolved,"Relocation PRX référence un symbole non résolu");
                    value = (r.type == 6 || r.type == 7) ? addresses[r.symbol] : addSigned(addresses[r.symbol],r.addend);
                    check((r.type != 6 && r.type != 7) || r.addend == 0,"GLOB_DAT/JUMP_SLOT avec addend non nul");
                }
                auto* p = target(m,sum(m.base,r.target),2);
                check(p,"Cible PRX hors mémoire writable");
                for (size_t j = 0; j < 8; ++j) p[j] = uint8_t(value>>(j*8));
            }
        }
    }
};
}
