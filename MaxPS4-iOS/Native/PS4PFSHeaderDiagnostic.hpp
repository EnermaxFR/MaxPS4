// SPDX-License-Identifier: GPL-2.0-only
#pragma once
// Independent header diagnostic, not an AetherPS4 port. Layout reference:
// LibOrbisPkg/LibOrbisPkg/PFS/PfsStructs.cs at
// 643477263b2644e0803e0f58b8726ea4e3f3b7d4. No upstream code or crypto copied.
#include "PS4PKGInventory.hpp"

namespace maxps4 {
struct PS4PFSHeaderDiagnostic {
    enum class Status { absent, malformed, unknown, encrypted, unsupported };
    Status status = Status::absent;
    uint16_t mode = 0;
    size_t bytesRead = 0;
    const char* message = "PFS absent ; aucun eboot.bin accessible";

    void inspect(const PS4PKGInventory& pkg, const PS4PKGInventory::Reader& read) {
        *this = {};
        if (!pkg.pfsSize) return;
        const auto reject = [&](const char* why) { status = Status::malformed; message = why; };
        if (!pkg.error.empty() || pkg.entries.empty() ||
            pkg.pfsOffset < PS4PKGInventory::headerSize ||
            !pkg.range(pkg.pfsOffset,pkg.pfsSize,pkg.fileSize) ||
            !pkg.range(pkg.tableOffset,pkg.entries.size()*32,pkg.fileSize)) {
            reject("PFS : plage ou inventaire invalide, lecture refusée"); return;
        }
        const auto overlaps = [&](uint64_t at,uint64_t size) {
            // Both ranges have been validated, so their ends cannot overflow.
            return size && at < pkg.pfsOffset+pkg.pfsSize && pkg.pfsOffset < at+size;
        };
        if (overlaps(pkg.tableOffset,pkg.entries.size()*32)) {
            reject("PFS : recouvre la table CNT, lecture refusée"); return;
        }
        for (const auto& e : pkg.entries) {
            if (!pkg.range(e.offset,e.size,pkg.fileSize) || overlaps(e.offset,e.size)) {
                reject("PFS : descripteur invalide ou recouvrement CNT, lecture refusée"); return;
            }
        }
        std::array<uint8_t,0x48> header{};
        if (pkg.pfsSize < header.size()) {
            reject("PFS : en-tête tronqué, lecture refusée"); return;
        }
        bytesRead = header.size(); // Budget attempted, including failed exact I/O.
        if (!read(pkg.pfsOffset,header.data(),header.size())) {
            reject("PFS : lecture de l'en-tête incomplète"); return;
        }
        const auto le = [&](size_t at,size_t width) {
            uint64_t value = 0;
            for (size_t i=0;i<width;++i) value |= uint64_t(header[at+i]) << (8*i);
            return value;
        };
        if (le(0,8)!=1 || le(8,8)!=20130315) {
            status = Status::unknown;
            message = "PFS : version/magic inconnus, chiffrement indéterminé ; aucun eboot.bin accessible";
            return;
        }
        mode = uint16_t(le(0x1c,2));
        if (mode & 4) {
            status = Status::encrypted;
            message = "PFS chiffré déclaré : inaccessible ; eboot.bin demeure inaccessible, aucun déchiffrement";
        } else {
            status = Status::unsupported;
            message = "PFS non chiffré déclaré : contenu non pris en charge par ce diagnostic d'en-tête ; eboot.bin demeure inaccessible";
        }
        // No seed, inode, directory, license, payload, output path or execution.
    }
};
}
