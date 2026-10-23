// Host command: the app's Orbis analysis on a PC, for an eboot.bin and the
// game's own modules (sce_module/*.prx).
// Usage: ps4-orbis-analyze <eboot.bin|module.prx> [module.prx ...]
// Reads at most 64 MiB per file; never writes, never executes. Prints the
// metadata report of the first file (a summary for the others), the imports
// per library that no given module exports (what HLE must provide), then a
// link of all the files together where each of those imports gets a
// placeholder trampoline or data object: it checks the whole loader
// (segments, relocations, TLS, dependencies), not whether the functions are
// implemented. A plaintext SELF is read as its ELF. Dependencies between the
// given files come from their DT_NEEDED entries.
#include "../Native/OrbisLinker.hpp"
#include "../Native/OrbisMetadata.hpp"
#include <cstdio>
#include <map>
#include <set>

namespace {
struct File {
    std::string name;
    std::vector<uint8_t> bytes; // Plain ELF once unwrapped.
    maxps4::OrbisMetadata metadata;
};
bool load(const char* path, File& file) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) { std::perror(path); return false; }
    file.bytes.resize(64 * 1024 * 1024 + 1);
    const size_t size = std::fread(file.bytes.data(), 1, file.bytes.size(), f);
    const bool failed = std::ferror(f);
    std::fclose(f);
    if (failed || size == file.bytes.size()) {
        std::fprintf(stderr, "%s : lecture incomplète ou fichier supérieur à 64 Mio\n", path);
        return false;
    }
    file.bytes.resize(size);
    const std::string full(path);
    file.name = full.substr(full.find_last_of("/\\") == std::string::npos ? 0 : full.find_last_of("/\\") + 1);
    return true;
}
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fputs("usage: ps4-orbis-analyze <eboot.bin|module.prx> [module.prx ...]\n", stderr); return 2; }
    std::vector<File> files(size_t(argc - 1));
    for (size_t i = 0; i < files.size(); ++i) {
        auto& file = files[i];
        if (!load(argv[i + 1], file)) return 1;
        const bool parsed = file.metadata.parse(file.bytes.data(), file.bytes.size());
        if (!i) std::fputs(file.metadata.report().c_str(), stdout);
        else std::printf("\n%s : %s\n", file.name.c_str(), parsed ? "métadonnées lues" : file.metadata.error.c_str());
        if (!parsed) return 1;
        std::vector<uint8_t> plain;
        std::string why;
        const int unwrapped = maxps4::SelfImage::unwrap(file.bytes.data(), file.bytes.size(), plain, why);
        if (unwrapped < 0) { std::printf("%s : SELF refusé : %s\n", file.name.c_str(), why.c_str()); return 1; }
        if (unwrapped) file.bytes.swap(plain);
    }
    // Libraries the given modules export; every other import is HLE's job.
    std::set<std::string> exported;
    for (const auto& file : files) for (const auto& l : file.metadata.libraries) if (l.exported) exported.insert(l.name);
    std::map<std::string, std::set<std::string>> hleByLibrary;
    std::set<maxps4::OrbisLinker::Identity> imports;
    for (const auto& file : files) {
        for (size_t i = 1; i < file.metadata.symbols.size(); ++i) {
            const auto& s = file.metadata.symbols[i];
            if (s.defined || s.encoded.empty() || exported.count(s.library)) continue;
            if (s.kind != 2 && s.kind != 1) {
                std::printf("%s : import d'un autre type : %s (type %u)\n", file.name.c_str(), s.encoded.c_str(), unsigned(s.kind));
                continue;
            }
            hleByLibrary[s.library].insert(s.kind == 1 ? s.nid + " (données)" : s.nid);
            imports.insert({s.nid, s.library, s.module, s.libraryVersion, s.moduleVersion, s.kind});
        }
    }
    std::printf("\nImports à fournir par la HLE (%zu, %zu bibliothèques) :\n", imports.size(), hleByLibrary.size());
    for (const auto& [library, nids] : hleByLibrary) {
        std::printf("  %s (%zu) :", library.c_str(), nids.size());
        for (const auto& nid : nids) std::printf(" %s", nid.c_str());
        std::printf("\n");
    }
    // Functions get numbered trampolines, data objects 64 placeholder bytes.
    std::vector<maxps4::OrbisLinker::HLE> hle;
    uint32_t token = 1;
    for (const auto& id : imports) hle.push_back({id, 0, id.type == 1 ? 64u : token++});
    // Each file after the previous one, 1 MiB aligned with a 1 MiB gap.
    std::vector<maxps4::OrbisLinker::Input> inputs;
    uint64_t bias = 0x400000;
    for (const auto& file : files) {
        std::vector<std::string> dependencies;
        for (const auto& other : files)
            for (const auto& needed : file.metadata.needed) if (needed == other.name && &other != &file) dependencies.push_back(other.name);
        inputs.push_back({{{file.name, dependencies}, file.bytes.data(), file.bytes.size()}, bias});
        uint64_t end = 0;
        for (const auto& s : file.metadata.segments) if (s.type == 1 || s.type == 0x61000010) end = std::max(end, s.address + s.memsz);
        bias += ((end + 0xfffff) & ~uint64_t{0xfffff}) + 0x100000;
    }
    maxps4::OrbisLinker::Image image;
    maxps4::OrbisLinker::Report report;
    std::string diagnostic;
    const bool linked = maxps4::OrbisLinker::link(inputs, hle, image, diagnostic,
        maxps4::PS4PKGRuntimePlan::batchMemoryBudget, &report);
    std::printf("\nLiaison de %zu fichier(s) avec %zu fonctions et données provisoires :\n%s\n",
        files.size(), hle.size(), report.text().c_str());
    if (!linked) std::printf("Diagnostic : %s\n", diagnostic.c_str());
    return linked ? 0 : 1;
}
