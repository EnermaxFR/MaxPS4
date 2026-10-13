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
            // An explicit inventory summary distinguishes inaccessible payloads from
            // missing executables. No signature scan of the opaque PFS is attempted.
            size_t readable = 0, encrypted = 0, unsupported = 0, unknown = 0, invalid = 0;
            for (const auto& entry : pkg.entries) {
                switch (entry.access) {
                case maxps4::PS4PKGInventory::Access::plaintext: ++readable; break;
                case maxps4::PS4PKGInventory::Access::encrypted: ++encrypted; break;
                case maxps4::PS4PKGInventory::Access::unsupported: ++unsupported; break;
                case maxps4::PS4PKGInventory::Access::unknown: ++unknown; break;
                case maxps4::PS4PKGInventory::Access::invalid: ++invalid; break;
                }
            }
            report << "Bilan des descripteurs : lisibles déclarés=" << readable
                   << " ; chiffrés déclarés=" << encrypted
                   << " ; non pris en charge=" << unsupported
                   << " ; inconnus=" << unknown << " ; invalides=" << invalid << '\n';
            if (pkg.pfsSize) report << "PFS présent : contenu opaque ; aucun exécutable déduit de cet inventaire.\n";
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
                            const char name[] = "eboot.bin";
                            if (std::search(bytes.begin(), bytes.end(), name, name + 9,
                                [](uint8_t a, char b) { return (a | 0x20U) == static_cast<uint8_t>(b); }) != bytes.end())
                                report << " ; indice texte eboot.bin (ne prouve pas un fichier accessible)";
                            if (bytes.size() >= 4 && !std::memcmp(bytes.data(), "\177ELF", 4)) {
                                // Header-only admission probe. No ELF segments are mapped or executed.
                                if (bytes.size() < 64) {
                                    report << " ; ELF tronqué : en-tête ELF64 incomplet";
                                } else if (bytes[4] != 2 || bytes[5] != 1 || bytes[6] != 1) {
                                    report << " ; ELF incompatible : ELF64 little-endian version 1 requis";
                                } else {
                                    const uint16_t machine = uint16_t(bytes[18]) | (uint16_t(bytes[19]) << 8);
                                    const uint16_t headerSize = uint16_t(bytes[52]) | (uint16_t(bytes[53]) << 8);
                                    if (machine != 62 || headerSize != 64) {
                                        report << " ; ELF incompatible : machine x86-64 et en-tête 64 octets requis";
                                    } else {
                                        report << " ; en-tête ELF64 x86-64 reconnu (segments non validés, aucune admission)";
                                    }
                                }
                            }
                            if (bytes.size() >= 4 && !std::memcmp(bytes.data(), "\117\025\075\035", 4))
                                report << " ; signature SELF candidate (non validée)";
                        } else report << " ; " << why;
                    } else report << " ; copie non réalisée : budget total de 1 Mio";
                }
                report << '\n';
            }
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
    } catch (const std::bad_alloc&) { return -ENOMEM; }
}
