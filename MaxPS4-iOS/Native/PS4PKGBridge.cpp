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
            // Explain common real-world descriptors without weakening the
            // extraction allowlist or treating protected metadata as plaintext.
            for (const auto& e : pkg.entries) {
                if (e.id == 0x100 && e.access == maxps4::PS4PKGInventory::Access::invalid &&
                    e.offset == pkg.tableOffset)
                    report << "Diagnostic metas : offset identique au début de la table PKG ; "
                              "chevauchement réel, aucune extraction.\n";
                if (e.id == 0x200 && e.access == maxps4::PS4PKGInventory::Access::unsupported &&
                    e.flags1 == 0x40000000U && !e.flags2 && !e.reserved)
                    report << "Diagnostic entry_names : descripteur présent, flag 0x40000000 "
                              "non interprété ; noms non lus et aucune élévation de privilège.\n";
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
                            if (e.id == 0x1000 && bytes.size() >= 4 &&
                                bytes[0] == 0 && bytes[1] == 'P' && bytes[2] == 'S' && bytes[3] == 'F') {
                                // Bounded PSF metadata preview: no path use, extraction or execution.
                                auto le16 = [&](size_t at) -> uint16_t {
                                    return uint16_t(bytes[at]) | (uint16_t(bytes[at + 1]) << 8);
                                };
                                auto le32 = [&](size_t at) -> uint32_t {
                                    return uint32_t(bytes[at]) | (uint32_t(bytes[at + 1]) << 8) |
                                        (uint32_t(bytes[at + 2]) << 16) | (uint32_t(bytes[at + 3]) << 24);
                                };
                                bool valid = bytes.size() >= 20;
                                uint32_t keyStart = 0, valueStart = 0, count = 0;
                                if (valid) {
                                    keyStart = le32(8); valueStart = le32(12); count = le32(16);
                                    valid = count <= 256 &&
                                        maxps4::PS4PKGInventory::range(20, uint64_t(count) * 16, bytes.size()) &&
                                        keyStart >= 20 + uint64_t(count) * 16 &&
                                        keyStart <= valueStart && valueStart <= bytes.size();
                                }
                                std::string metadata;
                                for (uint32_t j = 0; valid && j < count; ++j) {
                                    const size_t at = 20 + size_t(j) * 16;
                                    const uint16_t keyOffset = le16(at);
                                    const uint16_t format = le16(at + 2);
                                    const uint32_t length = le32(at + 4);
                                    const uint32_t maximum = le32(at + 8);
                                    const uint32_t valueOffset = le32(at + 12);
                                    const uint64_t keyAt = uint64_t(keyStart) + keyOffset;
                                    const uint64_t dataAt = uint64_t(valueStart) + valueOffset;
                                    valid = keyAt < valueStart && length <= maximum &&
                                        maxps4::PS4PKGInventory::range(dataAt, maximum, bytes.size());
                                    if (!valid) break;
                                    size_t keyEnd = static_cast<size_t>(keyAt);
                                    while (keyEnd < valueStart && bytes[keyEnd] &&
                                           keyEnd - keyAt <= 32) ++keyEnd;
                                    valid = keyEnd < valueStart && keyEnd > keyAt &&
                                        keyEnd - keyAt <= 32;
                                    for (size_t k = static_cast<size_t>(keyAt); valid && k < keyEnd; ++k)
                                        if (!((bytes[k] >= 'A' && bytes[k] <= 'Z') ||
                                              (bytes[k] >= '0' && bytes[k] <= '9') ||
                                              bytes[k] == '_')) valid = false;
                                    if (!valid) break;
                                    const std::string key(reinterpret_cast<const char*>(bytes.data() + keyAt),
                                                          keyEnd - static_cast<size_t>(keyAt));
                                    if (key != "TITLE" && key != "TITLE_ID" &&
                                        key != "CONTENT_ID" && key != "APP_VER") continue;
                                    if (format != 0x204 || !length || length > 256 ||
                                        bytes[static_cast<size_t>(dataAt + length - 1)] != 0) continue;
                                    // Strings must have exactly one trailing NUL: reject embedded
                                    // terminators rather than silently displaying a prefix.
                                    bool embeddedNul = false;
                                    for (size_t k = 0; k + 1 < length; ++k)
                                        if (bytes[static_cast<size_t>(dataAt) + k] == 0)
                                            embeddedNul = true;
                                    if (embeddedNul) continue;
                                    bool printable = true;
                                    for (size_t k = 0; k + 1 < length; ++k)
                                        if (bytes[dataAt + k] < 0x20 || bytes[dataAt + k] > 0x7e)
                                            printable = false;
                                    if (printable)
                                        metadata += " ; " + key + "=" +
                                            std::string(reinterpret_cast<const char*>(bytes.data() + dataAt),
                                                        length - 1);
                                }
                                if (valid) report << " ; PSF valide (" << count << " entrées)" << metadata;
                                else report << " ; PSF invalide : table ou chaînes hors limites";
                            }
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
                                    const uint16_t kind = uint16_t(bytes[16]) | (uint16_t(bytes[17]) << 8);
                                    const uint32_t version = uint32_t(bytes[20]) | (uint32_t(bytes[21]) << 8) |
                                        (uint32_t(bytes[22]) << 16) | (uint32_t(bytes[23]) << 24);
                                    if (machine != 62 || headerSize != 64 || version != 1) {
                                        report << " ; ELF incompatible : machine x86-64, version 1 et en-tête 64 octets requis";
                                    } else if (kind != 2 && kind != 3 && kind != 0xfe10 &&
                                               kind != 0xfe18) {
                                        report << " ; ELF incompatible : type exécutable/dynamique non reconnu";
                                    } else {
                                        const auto le16 = [&](size_t at) {
                                            return uint16_t(bytes[at]) | (uint16_t(bytes[at + 1]) << 8);
                                        };
                                        const auto le64 = [&](size_t at) {
                                            uint64_t value = 0;
                                            for (size_t n = 0; n < 8; ++n)
                                                value |= uint64_t(bytes[at + n]) << (8 * n);
                                            return value;
                                        };
                                        const uint64_t phoff = le64(32);
                                        const uint64_t phnum = le16(56);
                                        const uint64_t phentsize = le16(54);
                                        if (phnum && (phentsize != 56 ||
                                            !maxps4::PS4PKGInventory::range(
                                                phoff, phnum * phentsize, bytes.size()))) {
                                            report << " ; ELF invalide : table de segments absente ou hors ressource";
                                        } else {
                                            size_t loadCount = 0;
                                            bool invalidLoad = false;
                                            for (uint64_t n = 0; n < phnum; ++n) {
                                                const size_t at = static_cast<size_t>(phoff + n * phentsize);
                                                const uint32_t segmentType = uint32_t(bytes[at]) |
                                                    (uint32_t(bytes[at + 1]) << 8) |
                                                    (uint32_t(bytes[at + 2]) << 16) |
                                                    (uint32_t(bytes[at + 3]) << 24);
                                                if (segmentType != 1) continue; // PT_LOAD only
                                                ++loadCount;
                                                const uint64_t fileOffset = le64(at + 8);
                                                const uint64_t fileLength = le64(at + 32);
                                                const uint64_t memoryLength = le64(at + 40);
                                                if (memoryLength < fileLength ||
                                                    !maxps4::PS4PKGInventory::range(
                                                        fileOffset, fileLength, bytes.size())) {
                                                    invalidLoad = true;
                                                    break;
                                                }
                                            }
                                            if (invalidLoad)
                                                report << " ; ELF invalide : segment PT_LOAD hors ressource ou mémoire trop petite";
                                            else
                                                report << " ; ELF64 x86-64 : " << loadCount
                                                       << " segments PT_LOAD bornés (aucune admission ni exécution)";
                                        }
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
