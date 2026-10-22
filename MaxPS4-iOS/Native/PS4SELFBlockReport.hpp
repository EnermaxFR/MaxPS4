#pragma once
// Read-only characterization of SELF segment blocks. Flag layout follows
// shadPS4 elf.h and the public fSELF tooling: ordered 0x1, encrypted 0x2,
// signed 0x4, deflated 0x8, window 0x700, blocked 0x800, block size 0xf000,
// digests 0x10000, extents 0x20000, program header ID bits 20..31.
// This report never decrypts, decompresses, verifies signatures or changes
// what PS4ExecutableImage admits: it only explains why a file is refused.
#include "PS4ExecutableImage.hpp"
#include <string>

namespace maxps4 {
struct PS4SELFBlockReport {
    struct Block {
        uint64_t flags = 0, offset = 0, size = 0, decodedSize = 0, unknownFlags = 0;
        uint32_t id = 0, blockSize = 0, programType = 0;
        int programHeader = -1; // ELF program header described by a blocked entry.
        bool blocked = false, ordered = false, encrypted = false, signedBlock = false,
            compressed = false, digests = false, extents = false, inBounds = false;
    };
    static constexpr uint64_t knownFlags = 0xfU | 0x700U | 0x800U | 0xf000U | 0x10000U | 0x20000U |
        uint64_t{0xfff} << 20;
    int error = 0, loadError = 0;
    std::string stage; // SELF.HEADER, ELF.HEADER or empty when every header is readable.
    bool isSelf = false, isElf = false;
    uint8_t category = 0, programType = 0;
    uint16_t headerSize = 0, metaSize = 0, programHeaders = 0;
    uint32_t fileSize = 0;
    std::vector<Block> blocks;
    unsigned encrypted = 0, compressed = 0, signedOnly = 0, unknown = 0, outOfBounds = 0;

    static PS4SELFBlockReport inspect(const uint8_t* file, size_t size) {
        PS4SELFBlockReport r;
        PS4ExecutableImage image;
        r.loadError = image.load(file, size);
        if (!file || size < 32) return r.fail(-EINVAL, "SELF.HEADER");
        r.isSelf = read(file, 4) == 0x1d3d154f;
        r.isElf = !std::memcmp(file, "\177ELF", 4);
        if (!r.isSelf) return r;
        r.category = file[8]; r.programType = file[9];
        r.headerSize = static_cast<uint16_t>(read(file + 12, 2));
        r.metaSize = static_cast<uint16_t>(read(file + 14, 2));
        r.fileSize = static_cast<uint32_t>(read(file + 16, 4));
        const size_t count = read(file + 24, 2);
        if (!count || count > 128 || count > (size - 32) / 32) return r.fail(-ENOEXEC, "SELF.HEADER");
        const size_t origin = 32 + count * 32;
        const uint8_t* phdrs = nullptr;
        if (origin <= size && size - origin >= 64 && !std::memcmp(file + origin, "\177ELF", 4)) {
            const uint64_t table = read(file + origin + 32, 8), phnum = read(file + origin + 56, 2);
            if (phnum <= 128 && table >= 64 && table <= size - origin &&
                phnum * 56 <= size - origin - table) {
                r.programHeaders = static_cast<uint16_t>(phnum);
                phdrs = file + origin + table;
            }
        }
        if (!phdrs) r.stage = "ELF.HEADER"; // Blocks are still listed without a program header map.
        for (size_t i = 0; i < count; ++i) {
            const auto* p = file + 32 + i * 32;
            Block b;
            b.flags = read(p, 8); b.offset = read(p + 8, 8); b.size = read(p + 16, 8); b.decodedSize = read(p + 24, 8);
            b.ordered = b.flags & 0x1; b.encrypted = b.flags & 0x2; b.signedBlock = b.flags & 0x4;
            b.compressed = b.flags & 0x8; b.blocked = b.flags & 0x800;
            b.digests = b.flags & 0x10000; b.extents = b.flags & 0x20000;
            b.id = static_cast<uint32_t>((b.flags >> 20) & 0xfff);
            if (b.flags & 0xf000) b.blockSize = 1U << (12 + ((b.flags >> 12) & 0xf));
            b.unknownFlags = b.flags & ~knownFlags;
            b.inBounds = b.offset <= size && b.size <= size - b.offset;
            if (b.blocked && phdrs && b.id < r.programHeaders) {
                b.programHeader = static_cast<int>(b.id);
                b.programType = static_cast<uint32_t>(read(phdrs + b.id * 56, 4));
            }
            r.encrypted += b.encrypted; r.compressed += b.compressed;
            r.signedOnly += b.signedBlock && !b.encrypted && !b.compressed;
            r.unknown += b.unknownFlags != 0; r.outOfBounds += !b.inBounds;
            r.blocks.push_back(b);
        }
        return r;
    }

    // Single most relevant explanation; the order puts hard blockers first.
    std::string verdict() const {
        if (error) return "En-tête SELF illisible : aucun bloc caractérisé";
        if (!isSelf && !isElf) return "Ni ELF ni SELF : ce n'est pas un exécutable PS4 (eboot.bin/PRX attendu)";
        if (!isSelf) return loadError ? "ELF brut refusé par le staging actuel (pas de blocs SELF)" :
            "ELF brut accepté par le staging actuel";
        if (encrypted) return "Blocs chiffrés : un exécutable déjà déchiffré (ELF/fSELF) obtenu légalement depuis "
            "votre console est nécessaire ; MaxPS4 ne déchiffre pas";
        if (outOfBounds) return "Blocs hors des limites du fichier : fichier tronqué ou en-tête incohérent";
        if (unknown) return "Drapeaux de bloc inconnus : format non caractérisé, aucun mappage supposé";
        if (compressed) return "Blocs compressés en clair : décompression non implémentée par le staging";
        if (signedOnly) return "Blocs signés en clair (type fSELF) : signature non vérifiable, "
            "chargement non encore admis par le staging";
        return loadError ? "Blocs en clair, mais le staging refuse le fichier pour une autre raison (voir code)" :
            "Blocs en clair acceptés par le staging actuel";
    }

    std::string text() const {
        std::string out = "MaxPS4 caractérisation SELF v1\nLecture seule ; aucun déchiffrement, "
            "aucune décompression, aucune vérification de signature\n";
        out += "Format : " + std::string(isSelf ? "SELF" : isElf ? "ELF" : "inconnu") +
            " ; staging=" + std::to_string(loadError) + "\n";
        if (error || !stage.empty())
            out += "Étape bloquante : " + stage + " ; code=" + std::to_string(error) + "\n";
        if (isSelf) {
            out += "En-tête : catégorie=" + hex(category) + " type=" + hex(programType) +
                " taille_entête=" + std::to_string(headerSize) + " taille_méta=" + std::to_string(metaSize) +
                " taille_fichier=" + std::to_string(fileSize) + " blocs=" + std::to_string(blocks.size()) +
                " program_headers=" + std::to_string(programHeaders) + "\n";
            for (size_t i = 0; i < blocks.size(); ++i) {
                const auto& b = blocks[i];
                out += "Bloc " + std::to_string(i) + " : flags=" + hex(b.flags) + " offset=" + hex(b.offset) +
                    " taille=" + std::to_string(b.size) + " décodé=" + std::to_string(b.decodedSize);
                if (b.blocked) out += " id=" + std::to_string(b.id);
                if (b.programHeader >= 0) out += " phdr_type=" + hex(b.programType);
                if (b.blockSize) out += " taille_bloc=" + std::to_string(b.blockSize);
                out += " [";
                const std::pair<bool, const char*> names[] = {{b.ordered, " ordonné"}, {b.encrypted, " chiffré"},
                    {b.signedBlock, " signé"}, {b.compressed, " compressé"}, {b.blocked, " segment"},
                    {b.digests, " empreintes"}, {b.extents, " étendues"}, {!b.inBounds, " HORS_LIMITES"},
                    {b.unknownFlags != 0, " INCONNU"}};
                for (const auto& n : names) if (n.first) out += n.second;
                out += " ]\n";
            }
            out += "Totaux : chiffrés=" + std::to_string(encrypted) + " compressés=" + std::to_string(compressed) +
                " signés_en_clair=" + std::to_string(signedOnly) + " inconnus=" + std::to_string(unknown) +
                " hors_limites=" + std::to_string(outOfBounds) + "\n";
        }
        return out + "Verdict : " + verdict() + "\n";
    }
private:
    PS4SELFBlockReport& fail(int code, const char* where) { error = code; stage = where; return *this; }
    static uint64_t read(const uint8_t* p, size_t width) {
        uint64_t value = 0;
        for (size_t i = 0; i < width; ++i) value |= uint64_t(p[i]) << (8 * i);
        return value;
    }
    static std::string hex(uint64_t value) {
        const char* digits = "0123456789abcdef";
        std::string out;
        do { out.insert(out.begin(), digits[value & 15]); value >>= 4; } while (value);
        return "0x" + out;
    }
};
}
