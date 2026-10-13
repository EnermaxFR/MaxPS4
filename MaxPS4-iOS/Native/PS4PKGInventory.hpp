#pragma once
// Public layout reference: LibOrbisPkg PKG/{PkgReader,Entry,Enums}.cs,
// 643477263b2644e0803e0f58b8726ea4e3f3b7d4. No crypto or upstream code copied.
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include <utility>
#include <new>

namespace maxps4 {
class PS4PKGInventory {
public:
    // Exact reads only. The caller owns a stable source; no whole-file allocation.
    using Reader = std::function<bool(uint64_t, uint8_t*, size_t)>;
    static constexpr size_t headerSize = 0x5a0, maxEntries = 4096;
    static constexpr size_t extractionBudget = 1024 * 1024;
    enum class Access { invalid, encrypted, unsupported, unknown, plaintext };
    struct Entry {
        uint32_t id, nameOffset, flags1, flags2, offset, size;
        uint64_t reserved;
        Access access = Access::unknown;
        std::string problem;
    };
    uint64_t fileSize = 0, packageSize = 0, bodyOffset = 0, bodySize = 0;
    uint64_t pfsOffset = 0, pfsSize = 0, pfsFlags = 0;
    uint32_t tableOffset = 0;
    uint32_t headerFlags = 0, contentType = 0, contentFlags = 0, drmType = 0;
    std::string contentID, titleID, metadataDiagnostic;
    std::vector<Entry> entries;
    std::string error;

    static bool range(uint64_t offset, uint64_t size, uint64_t total) {
        return offset <= total && size <= total - offset;
    }
    static const char* packageType(uint32_t type, uint32_t flags) {
        switch (type) {
        case 0x1a:
            if (flags & 0x40100000U) return "application / mise à jour (flags déclarés)";
            if (flags & 0x00400000U) return "application remaster (flag déclaré)";
            return "application";
        case 0x1b: return "contenu additionnel / thème";
        case 0x1c: return "contenu additionnel sans données";
        case 0x1e: return "mise à jour delta";
        default: return "type non pris en charge (valeur brute conservée)";
        }
    }
    static bool validTitleID(const std::string& id) {
        return id.size() == 9 && id.compare(0, 4, "CUSA") == 0 &&
            id.find_first_not_of("0123456789", 4) == std::string::npos;
    }
    static bool validContentID(const std::string& id) {
        if (id.size() != 36 || id[6] != '-' || id[16] != '_' || id[19] != '-') return false;
        for (size_t i = 0; i < id.size(); ++i) {
            if (i == 6 || i == 16 || i == 19) continue;
            if (!((id[i] >= 'A' && id[i] <= 'Z') || (id[i] >= '0' && id[i] <= '9'))) return false;
        }
        return validTitleID(id.substr(7, 9));
    }
    static const char* type(uint32_t id) {
        switch (id) {
        case 1: return "digests";
        case 0x10: return "entry_keys (protégé)";
        case 0x20: return "image_key (protégé)";
        case 0x80: return "general_digests";
        case 0x100: return "metas";
        case 0x200: return "entry_names";
        case 0x400: return "license.dat (non pris en charge)";
        case 0x1000: return "param.sfo";
        case 0x1006: return "pic1.png";
        case 0x1200: return "icon0.png";
        default: return "type inconnu";
        }
    }
    static const char* accessName(Access access) {
        switch (access) {
        case Access::invalid: return "invalide";
        case Access::encrypted: return "chiffré (flag déclaré)";
        case Access::unsupported: return "non pris en charge";
        case Access::unknown: return "inconnu";
        case Access::plaintext: return "non chiffré déclaré, copie bornée autorisée";
        }
        return "inconnu";
    }
    bool parse(uint64_t size, const Reader& read) {
        *this = PS4PKGInventory{};
        PS4PKGInventory pending;
        pending.fileSize = size;
        try {
            if (!pending.parseImpl(read)) { error = pending.error; return false; }
            *this = std::move(pending);
            return true;
        } catch (const std::bad_alloc&) { error = "Budget mémoire indisponible"; return false; }
    }
    // Only a narrow allowlist with zero flags and valid, unambiguous ranges.
    // Copies opaque bytes into memory; no filesystem paths, DRM, PFS or ELF execution.
    bool extract(size_t index, const Reader& read, std::vector<uint8_t>& output,
                 std::string& diagnostic) const {
        output.clear();
        diagnostic.clear();
        if (index >= entries.size() || entries[index].access != Access::plaintext) {
            diagnostic = "Extraction refusée : entrée non admissible"; return false;
        }
        const auto& e = entries[index];
        if (e.size > extractionBudget) {
            diagnostic = "Extraction refusée : limite de 1 Mio"; return false;
        }
        try {
            std::vector<uint8_t> pending(e.size);
            if (e.size && !read(e.offset, pending.data(), pending.size())) {
                diagnostic = "Lecture incomplète"; return false;
            }
            output = std::move(pending);
            return true;
        } catch (const std::bad_alloc&) { diagnostic = "Budget mémoire indisponible"; return false; }
    }
    // Name offsets refer to the plaintext entry_names resource, never PFS.
    // Names are display-only ASCII; no paths are opened or extraction enabled.
    bool resolveNames(const Reader& read, std::vector<std::string>& names,
                      std::string& diagnostic) const {
        names.clear(); diagnostic.clear();
        size_t index = entries.size();
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].id == 0x200) index = i;
        std::vector<uint8_t> bytes;
        if (index == entries.size() || !extract(index, read, bytes, diagnostic)) {
            diagnostic = "Noms indisponibles : table absente ou non admissible";
            return false;
        }
        std::vector<std::string> pending(entries.size());
        for (size_t i = 0; i < entries.size(); ++i) {
            const size_t start = entries[i].nameOffset;
            if (!start) continue; // Zero is the unnamed sentinel.
            if (start >= bytes.size() || bytes[start - 1] != 0) {
                diagnostic = "Offset de nom hors table ou au milieu d'une chaîne"; return false;
            }
            size_t end = start;
            while (end < bytes.size() && bytes[end] && end - start < 255) {
                if (bytes[end] < 0x20 || bytes[end] > 0x7e) {
                    diagnostic = "Nom non ASCII affichable"; return false;
                }
                ++end;
            }
            if (end == bytes.size() || bytes[end] != 0) {
                diagnostic = "Nom non terminé ou supérieur à 255 octets"; return false;
            }
            pending[i].assign(reinterpret_cast<const char*>(bytes.data() + start), end - start);
        }
        names = std::move(pending);
        return true;
    }
private:
    static uint64_t be(const uint8_t* p, size_t n) {
        uint64_t v = 0;
        for (size_t i = 0; i < n; ++i) v = (v << 8) | p[i];
        return v;
    }
    bool fail(const char* why) { error = why; return false; }
    bool parseImpl(const Reader& read) {
        std::array<uint8_t, headerSize> h{};
        if (fileSize < h.size() || !read(0, h.data(), h.size())) return fail("En-tête PKG tronqué / lecture incomplète");
        if (std::memcmp(h.data(), "\177CNT", 4)) {
            if (!std::memcmp(h.data(), "\177ELF", 4)) return fail("Format ELF détecté : ce fichier n'est pas un PKG PS4");
            if (!std::memcmp(h.data(), "\177PKG", 4)) return fail("Format PKG PS3/PSP détecté : non pris en charge");
            return fail("Signature PKG PS4 invalide (0x7f434e54 attendue)");
        }
        headerFlags = static_cast<uint32_t>(be(h.data() + 4, 4));
        drmType = static_cast<uint32_t>(be(h.data() + 0x70, 4));
        contentType = static_cast<uint32_t>(be(h.data() + 0x74, 4));
        contentFlags = static_cast<uint32_t>(be(h.data() + 0x78, 4));
        size_t idLength = 0;
        while (idLength < 48 && h[0x40 + idLength]) ++idLength;
        const std::string id(reinterpret_cast<const char*>(h.data() + 0x40), idLength);
        if (validContentID(id)) { contentID = id; titleID = id.substr(7, 9); }
        else metadataDiagnostic = "Content ID absent ou invalide dans l'en-tête ; aucune déduction du nom du fichier";
        const uint64_t count = be(h.data() + 0x10, 4);
        const uint64_t secureCount = be(h.data() + 0x14, 2), secondary = be(h.data() + 0x16, 2);
        tableOffset = static_cast<uint32_t>(be(h.data() + 0x18, 4));
        bodyOffset = be(h.data() + 0x20, 8); bodySize = be(h.data() + 0x28, 8);
        pfsFlags = be(h.data() + 0x408, 8);
        pfsOffset = be(h.data() + 0x410, 8); pfsSize = be(h.data() + 0x418, 8);
        packageSize = be(h.data() + 0x430, 8);
        if (packageSize != fileSize) return fail("Taille PKG déclarée différente de la taille réelle");
        if (!count || count > maxEntries) return fail("Nombre d'entrées nul ou supérieur à 4096");
        if (secondary != count || secureCount > count) return fail("Compteurs PKG incohérents");
        if (tableOffset < headerSize || !range(tableOffset, count * 32, fileSize)) return fail("Table PKG hors limites / dans l'en-tête");
        if (!range(bodyOffset, bodySize, fileSize) || (bodySize && bodyOffset < headerSize)) return fail("Corps PKG hors limites");
        if (!range(pfsOffset, pfsSize, fileSize) || (pfsSize && pfsOffset < headerSize)) return fail("Image PFS hors limites");
        entries.reserve(static_cast<size_t>(count));
        for (size_t i = 0; i < count; ++i) {
            std::array<uint8_t, 32> item{};
            if (!read(tableOffset + i * 32, item.data(), item.size())) return fail("Table PKG : lecture incomplète");
            Entry e {static_cast<uint32_t>(be(item.data(), 4)), static_cast<uint32_t>(be(item.data()+4, 4)),
                static_cast<uint32_t>(be(item.data()+8, 4)), static_cast<uint32_t>(be(item.data()+12, 4)),
                static_cast<uint32_t>(be(item.data()+16, 4)), static_cast<uint32_t>(be(item.data()+20, 4)),
                be(item.data()+24, 8), Access::unknown, {}};
            const uint64_t end = uint64_t(e.offset) + e.size;
            if (!range(e.offset, e.size, fileSize) || (e.size && (e.offset < headerSize ||
                (e.offset < uint64_t(tableOffset) + count * 32 && tableOffset < end)))) {
                e.access = Access::invalid; e.problem = "plage hors fichier ou recouvre en-tête/table";
            } else if (e.flags1 & 0x80000000U) {
                e.access = Access::encrypted;
            } else if (e.flags1 || e.flags2 || e.reserved) {
                e.access = Access::unsupported; e.problem = "flags/réservés non interprétés";
            } else if (e.id == 0x200 || e.id == 0x1000 || e.id == 0x1006 || e.id == 0x1200) {
                e.access = Access::plaintext;
            } else if (e.id == 0x10 || e.id == 0x20 || e.id == 0x400) {
                e.access = Access::unsupported; e.problem = "clés/licences exclues";
            }
            // PFS contents remain opaque even if an entry descriptor says plaintext.
            if (e.access == Access::plaintext && e.size && pfsSize &&
                e.offset < pfsOffset + pfsSize && pfsOffset < end) {
                e.access = Access::unsupported; e.problem = "recouvre une image PFS opaque";
            }
            entries.push_back(std::move(e));
        }
        // Ambiguous IDs and overlapping payloads cannot be extraction sources.
        // Bounded O(n²), at most 4096 descriptors; no allocations based on payload sizes.
        for (size_t i = 0; i < entries.size(); ++i) {
            for (size_t j = 0; j < i; ++j) {
                auto& a = entries[i]; auto& b = entries[j];
                const bool overlap = a.size && b.size && uint64_t(a.offset) < uint64_t(b.offset) + b.size &&
                    uint64_t(b.offset) < uint64_t(a.offset) + a.size;
                if (a.id == b.id || overlap) {
                    a.access = b.access = Access::invalid;
                    a.problem = b.problem = "identifiant dupliqué ou plages ambiguës";
                }
            }
        }
        return true;
    }
};
}
