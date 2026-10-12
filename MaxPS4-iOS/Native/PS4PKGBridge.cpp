#include "PS4PKGBridge.h"
#include "PS4PKGInventory.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <memory>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

int maxps4_pkg_inventory_report(const char* path, char* output, size_t capacity) {
    if (!output || !capacity) return -EINVAL;
    output[0] = 0;
    if (!path) return -EINVAL;
    try {
        const auto closeFile = [](FILE* f) { std::fclose(f); };
        std::unique_ptr<FILE, decltype(closeFile)> file(std::fopen(path, "rb"), closeFile);
        if (!file) return -errno;
        struct stat info{};
        if (fstat(fileno(file.get()), &info) || !S_ISREG(info.st_mode) || info.st_size < 0) return -EINVAL;
        const uint64_t size = static_cast<uint64_t>(info.st_size);
        auto read = [&](uint64_t offset, uint8_t* bytes, size_t count) {
            if (!maxps4::PS4PKGInventory::range(offset, count, size)) return false;
            return !fseeko(file.get(), static_cast<off_t>(offset), SEEK_SET) &&
                std::fread(bytes, 1, count, file.get()) == count;
        };
        maxps4::PS4PKGInventory pkg;
        std::ostringstream report;
        report << "MaxPS4 • inventaire PKG générique (lecture seule)\n";
        if (!pkg.parse(size, read)) {
            report << "Rejet : " << pkg.error << '\n';
        } else {
            report << "Taille vérifiée : " << size << " octets ; entrées : " << pkg.entries.size()
                   << "\nCorps : " << pkg.bodyOffset << " + " << pkg.bodySize
                   << "\nPFS : " << pkg.pfsOffset << " + " << pkg.pfsSize << " (opaque, non monté)\n";
            std::vector<std::string> names;
            std::string nameDiagnostic;
            const bool namesResolved = pkg.resolveNames(read, names, nameDiagnostic);
            report << (namesResolved ? "Noms de descripteurs résolus (affichage seulement)" : nameDiagnostic) << '\n';
            // Count the names read against the same total inspection budget.
            size_t copied = 0;
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
                        if (pkg.extract(i, read, bytes, why)) {
                            copied += bytes.size();
                            report << " ; copie mémoire=" << bytes.size() << " octets";
                            const char name[] = "eboot.bin";
                            if (std::search(bytes.begin(), bytes.end(), name, name + 9,
                                [](uint8_t a, char b) { return (a | 0x20U) == static_cast<uint8_t>(b); }) != bytes.end())
                                report << " ; indice texte eboot.bin (ne prouve pas un fichier accessible)";
                            if (bytes.size() >= 4 && !std::memcmp(bytes.data(), "\177ELF", 4))
                                report << " ; signature ELF candidate (non validée)";
                            if (bytes.size() >= 4 && !std::memcmp(bytes.data(), "\117\025\075\035", 4))
                                report << " ; signature SELF candidate (non validée)";
                        } else report << " ; " << why;
                    } else report << " ; copie non réalisée : budget total de 1 Mio";
                }
                report << '\n';
            }
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
    } catch (const std::bad_alloc&) { return -ENOMEM; }
}
