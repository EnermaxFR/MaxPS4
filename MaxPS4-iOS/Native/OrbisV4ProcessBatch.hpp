#pragma once
#include "OrbisV4ExperimentalSELF.hpp"
#include "OrbisV4NpManager.hpp"
#include "OrbisV4NpTrophy.hpp"
#include "OrbisV4ServicePRX.hpp"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <set>

namespace maxps4 {
// Mission 50: the complete link batch for a real title (clear SELF/PRX files
// read-only) plus MaxPS4 providers. Every import with no existing export is
// bound to a host-service stub in the provider of its exact module; the host
// dispatcher decides per service whether it is IMPLEMENTED or a diagnostic
// TRAP (execution stops, never a fabricated success). OBJECT imports are only
// exported when their content is established (see objectFor); otherwise they
// stay unresolved and linking fails explicitly.
struct OrbisV4ProcessBatch {
    struct File { std::string name; std::vector<uint8_t> bytes; };
    struct Service {
        uint32_t id = 0; std::string nid, name, library, module, provider;
        uint16_t libraryVersion = 0, moduleVersion = 0; uint64_t stub = 0; // Absolute guest address.
        std::set<std::string> consumers;
    };
    struct Object { std::string name, nid, provider, meaning; uint64_t address = 0, size = 0; };
    static constexpr uint32_t serviceBase = 0x4d500000;
    static constexpr size_t providerState = 0x2000;
    // libkernel's state also hosts the guest kernel-object arena (pthread
    // objects whose fields libc accesses directly, see OrbisV4SystemServices).
    static constexpr size_t kernelState = 0x200000, kernelArena = 0x4000;
    static size_t stateFor(const std::string& path) { return path == "libkernel.prx" ? kernelState : providerState; }
    static constexpr const char* hleMemcpyNid = "Q3VBxCXhUHs"; // memcpy
    static std::string hexText(uint64_t v) { char b[24]; std::snprintf(b, sizeof b, "%llx", (unsigned long long)v); return b; }
    std::vector<std::unique_ptr<std::vector<uint8_t>>> storage;
    std::vector<OrbisLinker::Input> inputs;
    std::vector<Service> services;
    std::vector<Object> objects;
    std::map<std::string, uint64_t> bias, halt, stateBase; // provider path -> absolute addresses
    std::vector<std::string> unresolved;
    std::string report;
    uint64_t stackGuard = 0;

    const Service* service(uint32_t id) const {
        return id >= serviceBase && id - serviceBase < services.size() ? &services[id - serviceBase] : nullptr;
    }
    int build(const std::vector<File>& files, const std::array<int32_t, 4>& appParameters,
              const std::map<std::string, std::string>& names, std::string& error) {
        try { return buildImpl(files, appParameters, names, error); }
        catch (const std::exception& e) { error = std::string("PROCESS.BATCH: ") + e.what(); return -EINVAL; }
    }
private:
    struct Key {
        std::string nid, library, module; uint16_t libraryVersion, moduleVersion;
        auto tie() const { return std::tie(nid, library, module, libraryVersion, moduleVersion); }
        bool operator<(const Key& o) const { return tie() < o.tie(); }
    };
    std::vector<uint8_t>& own(std::vector<uint8_t> bytes) {
        storage.push_back(std::make_unique<std::vector<uint8_t>>(std::move(bytes))); return *storage.back();
    }
    int buildImpl(const std::vector<File>& files, const std::array<int32_t, 4>& appParameters,
                  const std::map<std::string, std::string>& names, std::string& error) {
        *this = OrbisV4ProcessBatch{};
        struct Pending { std::string path; std::vector<uint8_t> bytes; uint64_t bias; std::vector<std::string> deps; OrbisMetadata meta; };
        std::vector<Pending> modules;
        for (size_t i = 0; i < files.size(); ++i) {
            Pending p{files[i].name, {}, uint64_t(i + 1) * 0x400000, {}, {}};
            std::string reason;
            if (const int status = OrbisClearImage::normalize(files[i].bytes.data(), files[i].bytes.size(), p.bytes, reason)) {
                error = files[i].name + ": " + reason; return status;
            }
            if (!p.meta.parse(p.bytes.data(), p.bytes.size())) { error = files[i].name + ": " + p.meta.error; return -ENOEXEC; }
            p.deps = p.meta.needed;
            modules.push_back(std::move(p));
        }
        // Load bases: one 4 MiB slot per file from 0x400000 while every image
        // fits (the layout Kero Blaster's logs refer to); otherwise the files
        // follow one another from 0x4000000, above the providers (0x1000000-
        // 0x2100000), each rounded up to 4 MiB (Sonic Mania's eboot is 14 MiB).
        {
            auto span = [](const OrbisMetadata& meta) {
                uint64_t end = 0;
                for (const auto& s : meta.segments) if (s.type == 1 || s.type == 0x61000010) end = std::max(end, s.address + s.memsz);
                return (end + 0x3fffff) & ~uint64_t(0x3fffff);
            };
            bool fits = true;
            for (const auto& p : modules) fits &= span(p.meta) <= 0x400000;
            if (!fits) {
                uint64_t at = 0x4000000;
                for (auto& p : modules) { p.bias = at; at += std::max<uint64_t>(span(p.meta), 0x400000); }
            }
        }
        const size_t real = modules.size();
        auto provider = [&](const std::string& path, std::vector<uint8_t> bytes, uint64_t at) {
            Pending p{path, std::move(bytes), at, {}, {}};
            if (!p.meta.parse(p.bytes.data(), p.bytes.size())) throw std::runtime_error(path + ": " + p.meta.error);
            modules.push_back(std::move(p));
        };
        provider(OrbisV4AppContent::path, OrbisV4AppContent::provider(appParameters), OrbisV4AppContent::bias);
        provider(OrbisV4AudioOut::path, OrbisV4AudioOut::provider(), OrbisV4AudioOut::bias);
        provider(OrbisV4CommonDialog::path, OrbisV4CommonDialog::provider(), OrbisV4CommonDialog::bias);
        provider(OrbisV4ErrorDialog::path, OrbisV4ErrorDialog::provider(), OrbisV4ErrorDialog::bias);
        provider(OrbisV4GnmDriver::path, OrbisV4GnmDriver::provider(), OrbisV4GnmDriver::bias);
        provider(OrbisV4NpCommon::path, OrbisV4NpCommon::provider(), OrbisV4NpCommon::bias);
        provider(OrbisV4NpManager::path, OrbisV4NpManager::provider(), OrbisV4NpManager::bias);
        provider(OrbisV4LibcInternal::path, OrbisV4LibcInternal::provider(), OrbisV4LibcInternal::bias);
        provider(OrbisV4NpTrophy::path, OrbisV4NpTrophy::provider(), OrbisV4NpTrophy::bias);
        const auto local = OrbisV4LocalServices::modules();
        for (size_t i = 0; i < local.size(); ++i) provider(local[i].path(), OrbisV4LocalServices::provider(local[i], true), OrbisV4LocalServices::bias(i));
        for (size_t i = real; i < modules.size(); ++i)
            for (size_t j = 0; j < real; ++j) if (modules[j].path == modules[i].path) { error = "provider conflicts with " + modules[j].path; return -EINVAL; }
        // Provider functions re-routed to host services in the process profile:
        // their guest-code refusals are replaced by real implementations
        // (OrbisV4SystemServices). Identities are withdrawn, then stubbed below.
        std::map<std::string, std::vector<std::string>> withdrawn; // provider path -> nids
        for (size_t p = real; p < modules.size(); ++p) for (const auto& s : modules[p].meta.symbols)
            if (s.defined && s.qualified && names.count(s.nid) && overridden(names.at(s.nid))) withdrawn[modules[p].path].push_back(s.nid);
        // Exact export identities offered by every module of the batch.
        std::set<Key> exported;
        for (const auto& m : modules) for (const auto& s : m.meta.symbols)
            if (s.defined && s.qualified && !(withdrawn.count(m.path) &&
                std::count(withdrawn[m.path].begin(), withdrawn[m.path].end(), s.nid)))
                exported.insert({s.nid, s.library, s.module, s.libraryVersion, s.moduleVersion});
        // Missing imports grouped by the provider owning their exact module.
        struct Need { Key key; uint8_t kind; std::set<std::string> consumers; };
        std::map<std::string, std::map<Key, Need>> needs; // provider path -> needs
        for (size_t i = 0; i < real; ++i) for (const auto& s : modules[i].meta.symbols) {
            if (s.defined || !s.qualified) continue;
            const Key key{s.nid, s.library, s.module, s.libraryVersion, s.moduleVersion};
            if (exported.count(key)) continue;
            size_t owner = SIZE_MAX;
            for (size_t p = real; p < modules.size(); ++p)
                if (modules[p].meta.modules.size() == 1 && modules[p].meta.modules[0].name == s.module &&
                    modules[p].meta.modules[0].version == s.moduleVersion) owner = p;
            const auto name = names.count(s.nid) ? names.at(s.nid) : std::string("?");
            if (owner == SIZE_MAX) { unresolved.push_back(modules[i].path + ": " + s.nid + " (" + name + ") module " + s.module + " has no provider"); continue; }
            auto& need = needs[modules[owner].path][key];
            need.key = key; need.kind = s.kind; need.consumers.insert(modules[i].path);
        }
        // HLE: the title's own libc memcpy is redirected (in memory, never on
        // disk) to a host service, maxps4_memcpy, served by a libkernel stub:
        // simulated byte copies dominate loading (Sonic Mania copies 183 MB).
        // MAXPS4_HLE_MEMCPY=0 keeps the guest implementation.
        size_t hleModule = SIZE_MAX; uint64_t hleEntry = 0;
        {
            const char* e = std::getenv("MAXPS4_HLE_MEMCPY");
            const bool enabled = !(e && std::string(e) == "0");
            for (size_t i = 0; enabled && i < real && hleModule == SIZE_MAX; ++i)
                for (const auto& s : modules[i].meta.symbols)
                    if (s.defined && s.nid == hleMemcpyNid && s.library == "libc" && s.kind == 2) { hleModule = i; hleEntry = s.value; break; }
            if (hleModule != SIZE_MAX) {
                for (size_t p = real; p < modules.size(); ++p) if (modules[p].path == "libkernel.prx") {
                    const auto& lib = modules[p].meta.libraries[0];
                    const Key key{hleMemcpyNid, lib.name, modules[p].meta.modules[0].name, lib.version, modules[p].meta.modules[0].version};
                    auto& need = needs[modules[p].path][key];
                    need.key = key; need.kind = 2; need.consumers.insert(modules[hleModule].path + " (HLE memcpy)");
                }
            }
        }
        std::random_device entropy;
        stackGuard = (uint64_t(entropy()) << 32 | entropy()) & ~uint64_t{0xff}; // Low zero byte, as usual for canaries.
        if (!stackGuard) stackGuard = 0x2f8b5c3a9d1e4700ULL;
        for (size_t p = real; p < modules.size(); ++p) {
            auto& m = modules[p];
            const auto it = needs.find(m.path);
            if (it == needs.end()) continue;
            std::vector<OrbisV4ServicePRX::Library> extra;
            auto libraryIndex = [&](const Key& k) -> size_t {
                if (k.library == m.meta.libraries[0].name && k.libraryVersion == m.meta.libraries[0].version) return 0;
                for (size_t e = 0; e < extra.size(); ++e) if (extra[e].name == k.library && extra[e].version == k.libraryVersion) return e + 1;
                extra.push_back({k.library, k.libraryVersion}); return extra.size();
            };
            std::vector<OrbisV4ServicePRX::Stub> stubs; std::vector<OrbisV4ServicePRX::Data> data;
            std::vector<size_t> serviceIndex;
            uint64_t dataCursor = 0x100;
            for (const auto& entry : it->second) {
                const auto& need = entry.second; const auto& k = need.key;
                const auto name = k.nid == hleMemcpyNid ? std::string("maxps4_memcpy") : names.count(k.nid) ? names.at(k.nid) : std::string("?");
                const size_t library = libraryIndex(k);
                if (need.kind == 1) {
                    std::vector<uint8_t> bytes; std::string meaning;
                    if (!objectFor(name, m.bias + OrbisV4ServicePRX::state, dataCursor, bytes, meaning)) {
                        for (const auto& c : need.consumers) unresolved.push_back(c + ": OBJECT " + k.nid + " (" + name + ") content not established");
                        continue;
                    }
                    data.push_back({k.nid, library, dataCursor, bytes});
                    objects.push_back({name, k.nid, m.path, meaning, m.bias + OrbisV4ServicePRX::state + dataCursor, bytes.size()});
                    dataCursor = (dataCursor + bytes.size() + 15) & ~uint64_t{15};
                    if (dataCursor > providerState) throw std::length_error("provider object state");
                    continue;
                }
                if (need.kind != 2 && need.kind != 0) {
                    for (const auto& c : need.consumers) unresolved.push_back(c + ": " + k.nid + " (" + name + ") symbol type " + std::to_string(need.kind));
                    continue;
                }
                Service s; s.id = serviceBase + uint32_t(services.size()); s.nid = k.nid; s.name = name;
                s.library = k.library; s.module = k.module; s.libraryVersion = k.libraryVersion; s.moduleVersion = k.moduleVersion;
                s.provider = m.path; s.consumers = need.consumers;
                stubs.push_back({k.nid, library, s.id}); serviceIndex.push_back(services.size()); services.push_back(std::move(s));
            }
            OrbisV4ServicePRX::Layout layout;
            m.bytes = OrbisV4ServicePRX::extend(m.bytes, extra, stubs, stateFor(m.path), layout, data,
                                                withdrawn.count(m.path) ? withdrawn[m.path] : std::vector<std::string>{});
            if (!m.meta.parse(m.bytes.data(), m.bytes.size())) throw std::runtime_error(m.path + ": " + m.meta.error);
            for (size_t s = 0; s < serviceIndex.size(); ++s)
                services[serviceIndex[s]].stub = m.bias + layout.stubs + s * OrbisV4ServicePRX::Layout::stubSize;
            halt[m.path] = m.bias + layout.halt;
            stateBase[m.path] = m.bias + OrbisV4ServicePRX::state;
            for (const auto& e : extra) report += "PROVIDER.LIBRARY: " + m.path + " adds " + e.name + " v" + std::to_string(e.version) + "\n";
        }
        // Redirect the guest memcpy entry: jmp rel32 to the maxps4_memcpy stub
        // (the stub's ret returns to memcpy's caller).
        if (hleModule != SIZE_MAX) {
            uint64_t stub = 0;
            for (const auto& s : services) if (s.name == "maxps4_memcpy") stub = s.stub;
            auto& m = modules[hleModule];
            bool patched = false;
            for (const auto& seg : m.meta.segments) {
                if (seg.type != 1 || !(seg.flags & 1) || hleEntry < seg.address || hleEntry + 5 > seg.address + seg.filesz) continue;
                const int64_t rel = int64_t(stub) - int64_t(m.bias + hleEntry + 5);
                if (!stub || rel < INT32_MIN || rel > INT32_MAX) break;
                uint8_t* at = m.bytes.data() + seg.offset + (hleEntry - seg.address);
                at[0] = 0xe9; for (int k = 0; k < 4; ++k) at[1 + k] = uint8_t(uint32_t(int32_t(rel)) >> (8 * k));
                patched = true; break;
            }
            report += patched ? "HLE.MEMCPY " + m.path + "+0x" + hexText(hleEntry) + " -> host\n" : "HLE.MEMCPY not applied\n";
        }
        for (auto& m : modules) {
            bias[m.path] = m.bias;
            auto& bytes = own(std::move(m.bytes));
            inputs.push_back({{{m.path, m.deps}, bytes.data(), bytes.size()}, m.bias});
        }
        report += "PROCESS.BATCH: modules=" + std::to_string(inputs.size()) + " real=" + std::to_string(real) +
                  " service_stubs=" + std::to_string(services.size()) + " objects=" + std::to_string(objects.size()) +
                  " unresolved=" + std::to_string(unresolved.size()) + "\n";
        return 0;
    }
public:
    // Guest-code provider functions whose refusals block a real title and that
    // MaxPS4 handles on the host in the process profile. VideoOutOpen keeps
    // its existing no-device error, now visible in the syscall diagnostics.
    static bool overridden(const std::string& name) {
        return name.rfind("sceNpTrophy", 0) == 0 || name.rfind("scePad", 0) == 0 || name.rfind("sceAudioOut", 0) == 0 || name.rfind("sceVideoOut",0)==0 || name=="sceGnmAreSubmitsAllowed" || name.rfind("sceGnmSubmit",0)==0 ||
               name=="sceSystemServiceHideSplashScreen" || name.rfind("scePngDec", 0) == 0 || name=="sceRtcGetCurrentTick";
    }
private:
    // OBJECT imports whose content is established. Anything else is refused.
    bool objectFor(const std::string& name, uint64_t stateAddress, uint64_t offset, std::vector<uint8_t>& bytes, std::string& meaning) const {
        auto u64 = [&](uint64_t v) { for (int i = 0; i < 8; ++i) bytes.push_back(uint8_t(v >> (8 * i))); };
        if (name == "__stack_chk_guard") { u64(stackGuard); meaning = "stack-protector canary (random, low byte zero)"; return true; }
        if (name == "__progname") {
            // char* to a NUL-terminated program name stored right after the pointer.
            u64(stateAddress + offset + 8); const std::string program = "eboot.bin";
            bytes.insert(bytes.end(), program.begin(), program.end()); bytes.push_back(0);
            meaning = "char* program name \"eboot.bin\""; return true;
        }
        if (name == "_Stdout") {
            // Fios2 only passes &_Stdout to fflush (verified call site 0x3866d),
            // a function of this same provider: an opaque token, never parsed.
            bytes.assign(16, 0); meaning = "opaque stdout FILE token owned by the provider (address only)"; return true;
        }
        if (name == "Need_sceLibcInternal") {
            // Link marker: Fios2 stores its address with R_X86_64_64; no code reads the slot.
            bytes.assign(8, 0); meaning = "link marker (address stored, content never read)"; return true;
        }
        return false;
    }
};
}
