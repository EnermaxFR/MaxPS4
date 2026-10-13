#include "PS4PKGBridge.h"
#include "PS4PKGInventory.hpp"
#include "PS4SFO.hpp"
#include "PS4PKGRuntimePlan.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <memory>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <limits>

int maxps4_pkg_inventory_report(const char* path, char* output, size_t capacity) {
    if (!output || !capacity) return -EINVAL;
    output[0] = 0;
    auto sourceError = [&](int code, const char* message) {
        // Error handling must not allocate, especially after bad_alloc.
        std::snprintf(output, capacity, "Source PKG : %s", message);
        return -code;
    };
    if (!path) return sourceError(EINVAL, "chemin absent");
    try {
        const auto closeFile = [](FILE* f) { std::fclose(f); };
        std::unique_ptr<FILE, decltype(closeFile)> file(std::fopen(path, "rb"), closeFile);
        if (!file) { const int code = errno; return sourceError(code, std::strerror(code)); }
        struct stat info{};
        if (fstat(fileno(file.get()), &info)) { const int code = errno; return sourceError(code, std::strerror(code)); }
        if (!S_ISREG(info.st_mode) || info.st_size < 0) return sourceError(EINVAL, "fichier régulier requis");
        const uint64_t size = static_cast<uint64_t>(info.st_size);
        auto read = [&](uint64_t offset, uint8_t* bytes, size_t count) {
            if (!maxps4::PS4PKGInventory::range(offset, count, size) ||
                offset > static_cast<uint64_t>(std::numeric_limits<off_t>::max())) return false;
            return !fseeko(file.get(), static_cast<off_t>(offset), SEEK_SET) &&
                std::fread(bytes, 1, count, file.get()) == count;
        };
        maxps4::PS4PKGInventory pkg;
        std::ostringstream report;
        report << "MaxPS4 • inventaire PKG générique (lecture seule)\n";
        if (!pkg.parse(size, read)) {
            report << "Rejet : " << pkg.error << '\n';
        } else {
            report << "Format : PKG PS4 / CNT (signature reconnue, authenticité non vérifiée)\n"
                   << "Content ID (en-tête) : " << (pkg.contentID.empty() ? "indisponible" : pkg.contentID)
                   << "\nTitle ID (en-tête Content ID) : " << (pkg.titleID.empty() ? "indisponible" : pkg.titleID)
                   << "\nType : " << pkg.packageType(pkg.contentType, pkg.contentFlags)
                   << " ; content_type=0x" << std::hex << pkg.contentType
                   << " ; content_flags=0x" << pkg.contentFlags << " ; header_flags=0x" << pkg.headerFlags
                   << " ; drm_type=0x" << pkg.drmType << std::dec << '\n';
            if (!pkg.metadataDiagnostic.empty()) report << pkg.metadataDiagnostic << '\n';
            report << "Taille vérifiée : " << size << " octets ; entrées : " << pkg.entries.size()
                   << "\nCorps : " << pkg.bodyOffset << " + " << pkg.bodySize
                   << "\nPFS : " << pkg.pfsOffset << " + " << pkg.pfsSize << " (opaque, non monté)\n";
            // Kero Blaster field diagnostics: keep the extraction policy unchanged.
            // These are descriptive only; neither flags nor names grant access.
            for (const auto& entry : pkg.entries) {
                if (entry.id == 0x100 && entry.access == maxps4::PS4PKGInventory::Access::invalid &&
                    entry.size && entry.offset == pkg.tableOffset)
                    report << "Diagnostic metas : début à l'offset de la table PKG ; "
                              "alias possible de la table des descripteurs ; non admis comme données à extraire.\n";
                if (entry.id == 0x200 &&
                    entry.access == maxps4::PS4PKGInventory::Access::unsupported &&
                    entry.flags1 == 0x40000000U && entry.flags2 == 0 && entry.reserved == 0)
                    report << "Diagnostic entry_names : table déclarée, flag 0x40000000 "
                              "non interprété ; noms non lus.\n";
            }
            size_t inspected = 0;
            auto boundedRead = [&](uint64_t offset, uint8_t* bytes, size_t count) {
                if (count > pkg.extractionBudget - inspected) return false;
                inspected += count; // Failed/invalid name parsing also consumes budget.
                return read(offset, bytes, count);
            };
            std::vector<std::string> names;
            std::string nameDiagnostic;
            const bool namesResolved = pkg.resolveNames(boundedRead, names, nameDiagnostic);
            report << (namesResolved ? "Noms de descripteurs résolus (affichage seulement)" : nameDiagnostic) << '\n';
            // Count the names read against the same total inspection budget.
            size_t copied = 0;
            maxps4::PS4SFO sfo;
            bool sfoRead = false;
            std::string sfoDiagnostic = "param.sfo absent ou non lisible (voir l'inventaire)";
            if (namesResolved) for (const auto& e : pkg.entries)
                if (e.id == 0x200) copied += e.size;
            for (size_t i = 0; i < pkg.entries.size(); ++i) {
                const auto& e = pkg.entries[i];
                report << '#' << i + 1 << " id=0x" << std::hex << e.id << std::dec
                       << " [" << pkg.type(e.id) << "] offset=" << e.offset << " taille=" << e.size
                       << " nom@=" << e.nameOffset << " flags=0x" << std::hex << e.flags1 << "/0x" << e.flags2
                       << std::dec << " : " << pkg.accessName(e.access);
                if (namesResolved && !names[i].empty()) report << " nom=" << names[i];
                if (!e.problem.empty()) report << " (" << e.problem << ')';
                // Total payload inspection budget, not per-entry budget.
                if (e.access == maxps4::PS4PKGInventory::Access::plaintext && !(namesResolved && e.id == 0x200)) {
                    if (e.size <= pkg.extractionBudget - copied) {
                        std::vector<uint8_t> bytes;
                        std::string why;
                        if (pkg.extract(i, boundedRead, bytes, why)) {
                            copied += bytes.size();
                            report << " ; copie mémoire=" << bytes.size() << " octets";
                            if (e.id == 0x1000) {
                                sfoRead = sfo.parse(bytes);
                                sfoDiagnostic = sfoRead ? "param.sfo lisible, tables vérifiées" : sfo.error;
                            }
                            const char name[] = "eboot.bin";
                            if (std::search(bytes.begin(), bytes.end(), name, name + 9,
                                [](uint8_t a, char b) { return (a | 0x20U) == static_cast<uint8_t>(b); }) != bytes.end())
                                report << " ; indice texte eboot.bin (ne prouve pas un fichier accessible)";
                            if (bytes.size() >= 4 && !std::memcmp(bytes.data(), "\177ELF", 4))
                                report << " ; signature ELF candidate (non validée)";
                            if (bytes.size() >= 4 && !std::memcmp(bytes.data(), "\117\025\075\035", 4))
                                report << " ; signature SELF candidate (non validée)";
                        } else {
                            report << " ; " << why;
                            if (e.id == 0x1000) sfoDiagnostic = "SFO non lu : " + why;
                        }
                    } else {
                        report << " ; copie non réalisée : budget total de 1 Mio";
                        if (e.id == 0x1000) sfoDiagnostic = "SFO non lu : budget total de 1 Mio";
                    }
                }
                report << '\n';
            }
            report << "Métadonnées SFO : " << sfoDiagnostic << '\n';
            for (const auto* key : {"TITLE", "TITLE_ID", "CONTENT_ID", "APP_VER", "VERSION", "CATEGORY"}) {
                const auto value = sfo.strings.find(key);
                report << key << " (param.sfo) : ";
                if (!sfoRead || value == sfo.strings.end()) report << "indisponible";
                else if ((std::strcmp(key, "TITLE_ID") == 0 && !pkg.validTitleID(value->second)) ||
                         (std::strcmp(key, "CONTENT_ID") == 0 && !pkg.validContentID(value->second)))
                    report << "invalide";
                else report << value->second;
                report << '\n';
                if (sfoRead && value != sfo.strings.end() &&
                    ((std::strcmp(key, "TITLE_ID") == 0 && !pkg.titleID.empty() && value->second != pkg.titleID) ||
                     (std::strcmp(key, "CONTENT_ID") == 0 && !pkg.contentID.empty() && value->second != pkg.contentID)))
                    report << "Avertissement : identifiant SFO différent de l'en-tête PKG\n";
            }
            maxps4::PS4PKGRuntimePlan plan;
            plan.inventory(pkg, names);
            report << "Préparation runtime ELF/PRX : " << plan.references.size() << " références de descripteurs\n";
            report << "Une référence absente de ces descripteurs peut exister dans le PFS opaque.\n";
            for (const auto& reference : plan.references)
                report << "Référence : " << reference.path << " ; " << pkg.accessName(reference.descriptorAccess)
                       << " ; référence seule, données exécutables non admises\n";
            report << "Obstacles : PFS non monté, entrées chiffrées/non interprétées exclues ; "
                   << "eboot.bin et dépendances PRX accessibles requis.\n"
                   << "Contrat runtime : fichiers ELF Orbis autonomes non chiffrés fournis explicitement ; "
                   << "staging seulement, imports/dépendances et relocations à résoudre, aucun lancement automatique.\n";
            report << "Octets inspectés (budget partagé) : " << inspected << "\n";
            report << "Octets copiés en mémoire : " << copied << "; aucun fichier exporté.\n"
                   << "Les flags déclarés ne constituent pas une authentification.\n"
                   << "Les noms de descripteurs ne constituent pas un système de fichiers ; aucune admission ELF/SELF ni exécution.\n"
                   << "Absence de signature dans ces ressources ≠ absence d'exécutable dans le PKG.\n"
                   << "Aucun déchiffrement, aucune recherche dans les clés/licences ou le PFS.\n";
        }
        const auto text = report.str();
        const auto count = text.size() < capacity - 1 ? text.size() : capacity - 1;
        std::memcpy(output, text.data(), count);
        output[count] = 0;
        return count == text.size() ? 0 : -ENOSPC;
    } catch (const std::bad_alloc&) { return sourceError(ENOMEM, "budget mémoire indisponible"); }
}
