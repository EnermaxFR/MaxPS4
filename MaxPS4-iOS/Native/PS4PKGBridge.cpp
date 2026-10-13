#include "PS4PKGBridge.h"
#include "PS4PKGInventory.hpp"
#include "PS4SFO.hpp"
#include "PS4PFSReadOnly.hpp"
#include <fcntl.h>
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
        const int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) { const int code = errno; return sourceError(code, std::strerror(code)); }
        FILE* stream = fdopen(fd, "rb");
        if (!stream) { const int code = errno; close(fd); return sourceError(code, std::strerror(code)); }
        std::unique_ptr<FILE, decltype(closeFile)> file(stream, closeFile);
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
                   << "\nPFS : " << pkg.pfsOffset << " + " << pkg.pfsSize << " (plage déclarée ; statut ci-dessous)\n";
            maxps4::PS4PFSReadOnly pfs;
            auto pfsRead = [&](uint64_t offset, uint8_t* bytes, size_t count) {
                return pkg.range(offset,count,pkg.pfsSize) && read(pkg.pfsOffset+offset,bytes,count);
            };
            const uint64_t tableEnd = uint64_t(pkg.tableOffset)+pkg.entries.size()*32;
            bool overlap = pkg.pfsSize && pkg.pfsOffset < tableEnd && pkg.tableOffset < pkg.pfsOffset+pkg.pfsSize;
            for (const auto& e : pkg.entries) overlap |= pkg.pfsSize && e.size &&
                e.offset < pkg.pfsOffset+pkg.pfsSize && pkg.pfsOffset < uint64_t(e.offset)+e.size;
            if (overlap) {
                pfs.status = maxps4::PS4PFSReadOnly::Status::malformed;
                pfs.diagnostic = "PFS : plage recouvre table/descripteurs, lecture refusée";
            } else pfs.inspect(pkg.pfsSize,pfsRead);
            report << "Diagnostic PKG/PFS : " << pfs.diagnostic << " ; mode=0x" << std::hex << pfs.mode
                   << std::dec << " ; bloc=" << pfs.blockSize << '\n';
            for (const auto& f : pfs.files)
                report << "Fichier PFS uroot : " << f.path << " ; octets=" << f.size << '\n';
            const bool pfsAccessible = pfs.status == maxps4::PS4PFSReadOnly::Status::accessible;
            const bool pfsEboot = std::any_of(pfs.files.begin(),pfs.files.end(),[](const auto& f) { return f.path=="eboot.bin"; });
            if (pfsAccessible)
                report << "Contenu PFS : fichiers uroot accessibles ; eboot.bin " << (pfsEboot ? "présent" : "absent")
                       << "; présence distincte de validation ELF et de démarrage.\n"
                          "Précontrôle #185 : exporter les fichiers sélectionnés accessibles vers un dossier choisi, "
                          "puis Import → Dossier de jeu accessible ; aucun manifeste/graphe/biais inventé.\n";
            // Descriptor counts are informational; no access to the opaque PFS is inferred.
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
            if (pkg.pfsSize && !pfsAccessible)
                report << "PFS présent : contenu opaque ; aucun exécutable déduit de cet inventaire.\n"
                          "Contenu du jeu : non accessible par ce lecteur (PFS opaque, aucun déchiffrement).\n";
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
            // A readiness verdict is deliberately separate from descriptor names:
            // PKG metadata and plaintext artwork are never executable admission.
            // The standalone ELF path is a distinct, explicit user-supplied workflow.
            report << "Diagnostic démarrage : NON PRÊT (inventaire PKG uniquement).\n";
            report << (pfsAccessible ? "Accès PFS : " : "Blocage 1/3 : PFS ")
                   << (pfsAccessible ? "lu en lecture seule (D32 contigu)" : (pkg.pfsSize ? "présent mais opaque/non monté" : "absent ou non exposé"))
                   << (pfsAccessible && pfsEboot ? " ; eboot.bin accessible, non validé comme exécutable.\n" : " ; aucun eboot.bin accessible par ce lecteur.\n");
            report << "Blocage 2/3 : aucun binaire Orbis ELF autonome validé depuis ce PKG ; "
                      "noms, images et param.sfo ne sont pas exécutables.\n";
            report << "Blocage 3/3 : imports NID, modules PRX et relocations non résolus "
                      "pour ce titre ; exécution non autorisée.\n";
            report << "Étape sûre : fournir séparément un ELF Orbis non chiffré et "
                      "autorisé, puis tester son staging et ses dépendances ; "
                      "aucun contournement DRM/PFS.\n";
            report << "Obstacles : " << (pfsAccessible ? "PFS lisible limité au sous-ensemble D32 ; " : "PFS non monté ; ")
                   << "entrées chiffrées/non interprétées exclues ; "
                   << "eboot.bin et dépendances PRX accessibles requis.\n"
                   << "Contrat runtime : fichiers ELF Orbis autonomes non chiffrés fournis explicitement ; "
                   << "staging seulement, imports/dépendances et relocations à résoudre, aucun lancement automatique.\n";
            report << "Octets inspectés (budget partagé) : " << inspected << "\n"
                   << "Ce budget concerne les ressources CNT ; PFS : budget distinct de 8 Mio, aucune lecture de payload chiffré.\n";
            report << "Octets copiés en mémoire : " << copied << "; aucun fichier exporté.\n"
                   << "Les flags déclarés ne constituent pas une authentification.\n"
                   << "Les noms de descripteurs ne constituent pas un système de fichiers ; aucune admission ELF/SELF ni exécution.\n"
                   << "Absence de signature dans ces ressources ≠ absence d'exécutable dans le PKG.\n"
                   << "Aucun déchiffrement ni recherche de clés/licences ; inspection PFS non chiffré documenté uniquement.\n";
        }
        const auto text = report.str();
        const auto count = text.size() < capacity - 1 ? text.size() : capacity - 1;
        std::memcpy(output, text.data(), count);
        output[count] = 0;
        return count == text.size() ? 0 : -ENOSPC;
    } catch (const std::bad_alloc&) { return sourceError(ENOMEM, "budget mémoire indisponible"); }
}

// Host output remains a caller-owned byte buffer; no source-derived disk path.
int maxps4_pkg_pfs_read_file(const char* path, const char* selected, unsigned char* output,
                            size_t capacity, size_t* length) {
    if (!length) return -EINVAL;
    *length = 0;
    if (!path || !selected || (!output && capacity)) return -EINVAL;
    try {
        const int fd = open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
        if (fd<0) return -errno;
        struct Close { int fd; ~Close() { close(fd); } } closeSource{fd};
        struct stat st{};
        if (fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size<0) return -EINVAL;
        const auto size=uint64_t(st.st_size);
        auto read=[&](uint64_t at,uint8_t* bytes,size_t n) {
            if (!maxps4::PS4PKGInventory::range(at,n,size) || at>uint64_t(std::numeric_limits<off_t>::max())) return false;
            size_t done=0;
            while (done<n) {
                const auto r=pread(fd,bytes+done,n-done,off_t(at+done));
                if (r<0 && errno==EINTR) continue;
                if (r<=0) return false;
                done+=size_t(r);
            }
            return true;
        };
        maxps4::PS4PKGInventory pkg;
        if (!pkg.parse(size,read)) return -EINVAL;
        const uint64_t end=pkg.pfsOffset+pkg.pfsSize;
        if (pkg.pfsSize && pkg.pfsOffset<uint64_t(pkg.tableOffset)+pkg.entries.size()*32 && pkg.tableOffset<end) return -EINVAL;
        for (const auto& e:pkg.entries) if (e.size && pkg.pfsSize && e.offset<end && pkg.pfsOffset<uint64_t(e.offset)+e.size) return -EINVAL;
        auto slice=[&](uint64_t at,uint8_t* bytes,size_t n) {
            return pkg.range(at,n,pkg.pfsSize) && read(pkg.pfsOffset+at,bytes,n);
        };
        maxps4::PS4PFSReadOnly pfs;
        if (!pfs.inspect(pkg.pfsSize,slice)) return -EACCES;
        const std::string name(selected);
        if (!pfs.allowedSelection(name)) return -EACCES;
        const auto it=std::find_if(pfs.files.begin(),pfs.files.end(),[&](const auto& f){return f.path==name;});
        if (it==pfs.files.end()) return -ENOENT;
        if (it->size>64*1024*1024) return -E2BIG;
        *length=size_t(it->size);
        if (capacity<*length) return -ENOSPC;
        std::vector<uint8_t> bytes; std::string why;
        if (!pfs.extract(name,slice,bytes,why)) { *length=0; return -EIO; }
        struct stat after{};
        if (fstat(fd,&after) || after.st_size!=st.st_size || after.st_mtime!=st.st_mtime || after.st_ctime!=st.st_ctime) {
            *length=0; return -EIO;
        }
        if (!bytes.empty()) std::memcpy(output,bytes.data(),bytes.size());
        return 0;
    } catch (const std::bad_alloc&) { *length=0; return -ENOMEM; }
}
