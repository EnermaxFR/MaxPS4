// Host command: the app's Orbis analysis for one eboot.bin/PRX, on a PC.
// Usage: ps4-orbis-analyze <file>. Reads at most 64 MiB; never writes, never
// executes. Prints the metadata report, the imports per library, then a link
// attempt where every imported function gets a placeholder HLE trampoline:
// it checks the whole loader (segments, relocations, TLS), not whether the
// functions are implemented. A plaintext SELF is read as its ELF.
#include "../Native/OrbisLinker.hpp"
#include "../Native/OrbisMetadata.hpp"
#include <cstdio>
#include <map>
#include <set>

int main(int argc, char** argv) {
    if (argc != 2) { std::fputs("usage: ps4-orbis-analyze <eboot.bin|module.prx>\n", stderr); return 2; }
    std::FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::perror(argv[1]); return 1; }
    std::vector<uint8_t> bytes(64 * 1024 * 1024 + 1);
    const size_t size = std::fread(bytes.data(), 1, bytes.size(), f);
    const bool failed = std::ferror(f);
    std::fclose(f);
    if (failed) { std::fputs("lecture incomplète\n", stderr); return 1; }
    if (size == bytes.size()) { std::fputs("fichier supérieur à 64 Mio refusé\n", stderr); return 1; }
    bytes.resize(size);
    maxps4::OrbisMetadata metadata;
    const bool parsed = metadata.parse(bytes.data(), bytes.size());
    std::fputs(metadata.report().c_str(), stdout);
    if (!parsed) return 1;
    // Imports per library, and placeholder HLE entries (any module version).

    std::map<std::string, std::vector<std::string>> byLibrary;
    std::set<maxps4::OrbisLinker::Identity> functions;
    size_t objects = 0;
    for (size_t i = 1; i < metadata.symbols.size(); ++i) {
        const auto& s = metadata.symbols[i];
        if (s.defined || s.encoded.empty()) continue;
        byLibrary[s.library].push_back(s.nid);
        if (s.kind != 2 && s.kind != 1) {
            std::printf("Import d'un autre type : %s (type %u, %s)\n", s.encoded.c_str(), unsigned(s.kind), s.library.c_str());
            continue;
        }
        if (s.kind == 1) {
            ++objects;
            std::printf("Import de données : %s (%s)\n", s.nid.c_str(), s.library.c_str());
        }
        functions.insert({s.nid, s.library, s.module, s.libraryVersion, s.moduleVersion, s.kind});
    }
    std::printf("\nImports par bibliothèque (%zu bibliothèques) :\n", byLibrary.size());
    for (const auto& [library, nids] : byLibrary) std::printf("  %s : %zu\n", library.c_str(), nids.size());
    std::printf("Imports de données (non fonctions) : %zu\n", objects);
    std::vector<uint8_t> plain;
    std::string why;
    const int unwrapped = maxps4::SelfImage::unwrap(bytes.data(), bytes.size(), plain, why);
    if (unwrapped < 0) { std::printf("SELF refusé : %s\n", why.c_str()); return 1; }
    if (unwrapped) bytes.swap(plain);
    for (const auto& s : metadata.segments) {
        if (s.type != 2 || !maxps4::OrbisMetadata::range(s.offset, s.filesz, bytes.size())) continue;
        std::printf("Tags dynamiques :");
        for (uint64_t at = s.offset; at + 16 <= s.offset + s.filesz; at += 16) {
            const uint64_t tag = maxps4::OrbisMetadata::read(bytes.data() + at, 8);
            if (!tag) break;
            std::printf(" 0x%llx", static_cast<unsigned long long>(tag));
        }
        std::printf("\n");
    }
    std::vector<maxps4::OrbisLinker::HLE> hle;
    uint32_t token = 1;
    // Functions get numbered trampolines, data objects 64 placeholder bytes.
    for (const auto& id : functions) hle.push_back({id, 0, id.type == 1 ? 64u : token++});
    maxps4::OrbisLinker::Input input{{{"eboot.bin", {}}, bytes.data(), bytes.size()}, 0x400000};
    maxps4::OrbisLinker::Image image;
    maxps4::OrbisLinker::Report report;
    std::string diagnostic;
    const bool linked = maxps4::OrbisLinker::link({input}, hle, image, diagnostic,
        maxps4::PS4PKGRuntimePlan::batchMemoryBudget, &report);
    std::printf("\nLiaison avec %zu fonctions et données provisoires :\n%s\n", hle.size(), report.text().c_str());
    if (!linked) std::printf("Diagnostic : %s\n", diagnostic.c_str());
    return linked ? 0 : 1;
}
