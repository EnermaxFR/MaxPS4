// SPDX-License-Identifier: GPL-2.0-or-later
#include "maxps4_ps4_loader.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace {

constexpr uint32_t kSelfMagic = 0x1D3D154Fu;
constexpr uint32_t kPkgMagic = 0x7F434E54u;
constexpr unsigned long long kMaxPlainPkgExecutableSize =
    256ULL * 1024ULL * 1024ULL;

static uint16_t u16le(const unsigned char *p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

static uint32_t u32le(const unsigned char *p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

static uint64_t u64le(const unsigned char *p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) {
        v = (v << 8) | p[i];
    }
    return v;
}

static uint16_t u16be(const unsigned char *p) {
    return (static_cast<uint16_t>(p[0]) << 8) |
           static_cast<uint16_t>(p[1]);
}

static uint32_t u32be(const unsigned char *p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

static uint64_t u64be(const unsigned char *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | p[i];
    }
    return v;
}

static void diag(char *dst, unsigned long size, const char *fmt,
                 unsigned long long a = 0, unsigned long long b = 0) {
    if (!dst || size == 0) return;
    std::snprintf(dst, size, fmt, a, b);
}

static bool get_file_size(FILE *f, unsigned long long *out_size) {
    if (!f || !out_size) return false;
    const long current = std::ftell(f);
    if (current < 0) return false;
    if (std::fseek(f, 0, SEEK_END) != 0) return false;
    const long end = std::ftell(f);
    if (end < 0 || std::fseek(f, current, SEEK_SET) != 0) return false;
    *out_size = static_cast<unsigned long long>(end);
    return true;
}

static bool parse_pkg(FILE *f, MaxPS4ExecutableInfo *out_info,
                      char *diagnostic, unsigned long diagnostic_size) {
    if (!f) return false;

    unsigned long long file_size = 0;
    if (!get_file_size(f, &file_size)) {
        diag(diagnostic, diagnostic_size, "PS4 PKG: taille illisible");
        return false;
    }

    if (std::fseek(f, 0, SEEK_SET) != 0) {
        diag(diagnostic, diagnostic_size, "PS4 PKG: seek en-tete impossible");
        return false;
    }

    unsigned char header[0x80]{};
    if (std::fread(header, 1, sizeof(header), f) != sizeof(header)) {
        diag(diagnostic, diagnostic_size, "PS4 PKG: en-tete incomplet");
        return false;
    }

    if (u32be(header) != kPkgMagic) {
        diag(diagnostic, diagnostic_size, "PS4 PKG: magic invalide");
        return false;
    }

    const uint32_t flags = u32be(header + 0x04);
    const uint32_t file_count = u32be(header + 0x0C);
    const uint32_t table_count = u32be(header + 0x10);
    const uint16_t table_count_2 = u16be(header + 0x16);
    const uint32_t table_offset = u32be(header + 0x18);
    const uint64_t content_size = u64be(header + 0x38);
    const uint32_t drm_type = u32be(header + 0x70);
    const uint32_t content_type = u32be(header + 0x74);

    if (table_count == 0 || table_count > 65536 ||
        (table_count_2 != 0 && table_count_2 != (table_count & 0xFFFFu))) {
        diag(diagnostic, diagnostic_size, "PS4 PKG: table d'entrees invalide");
        return false;
    }

    const unsigned long long table_bytes =
        static_cast<unsigned long long>(table_count) * 0x20ULL;
    if (table_offset < 0x20 ||
        static_cast<unsigned long long>(table_offset) > file_size ||
        table_bytes > file_size - static_cast<unsigned long long>(table_offset)) {
        diag(diagnostic, diagnostic_size, "PS4 PKG: table hors limites");
        return false;
    }

    char content_id[37]{};
    for (unsigned int i = 0; i < 36; ++i) {
        const unsigned char ch = header[0x40 + i];
        if (ch == 0) break;
        content_id[i] = (ch >= 0x20 && ch <= 0x7E)
            ? static_cast<char>(ch) : '?';
    }

    if (out_info) {
        std::memset(out_info, 0, sizeof(*out_info));
        out_info->kind = MAXPS4_EXEC_PKG;
        out_info->package_flags = flags;
        out_info->package_file_count = file_count;
        out_info->package_table_entry_count = table_count;
        out_info->package_drm_type = drm_type;
        out_info->package_content_type = content_type;
        out_info->package_content_size = content_size;
        std::snprintf(out_info->package_content_id,
                      sizeof(out_info->package_content_id),
                      "%s", content_id);
    }

    if (diagnostic && diagnostic_size != 0) {
        std::snprintf(diagnostic, diagnostic_size,
                      "PS4 PKG reconnu • id=%s • entries=%u • drm=0x%08x • type=0x%08x",
                      content_id[0] ? content_id : "<sans-id>",
                      table_count, drm_type, content_type);
    }
    return true;
}

static bool looks_like_raw_executable(const unsigned char *p, size_t size) {
    if (!p || size < 4) return false;
    if (p[0] == 0x7F && p[1] == 'E' && p[2] == 'L' && p[3] == 'F') {
        return true;
    }
    return u32le(p) == kSelfMagic;
}

} // namespace

bool maxps4_ps4_loader_validate(const char *path, MaxPS4ExecutableInfo *out_info,
                                char *diagnostic, unsigned long diagnostic_size) {
    if (out_info) std::memset(out_info, 0, sizeof(*out_info));

    if (!path || path[0] == '\0') {
        diag(diagnostic, diagnostic_size, "PS4 loader: chemin vide");
        return false;
    }

    FILE *f = std::fopen(path, "rb");
    if (!f) {
        diag(diagnostic, diagnostic_size, "PS4 loader: ouverture impossible errno=%llu",
             static_cast<unsigned long long>(errno));
        return false;
    }

    unsigned char first[32]{};
    if (std::fread(first, 1, sizeof(first), f) != sizeof(first)) {
        std::fclose(f);
        diag(diagnostic, diagnostic_size, "PS4 loader: fichier trop court");
        return false;
    }

    // PS4 PKG headers use big-endian fields and the 0x7F434E54 magic.
    // Recognition is intentionally metadata-only here; execution requires
    // a separately readable ELF/SELF payload.
    if (u32be(first) == kPkgMagic) {
        const bool ok = parse_pkg(f, out_info, diagnostic, diagnostic_size);
        std::fclose(f);
        return ok;
    }

    const bool is_self = u32le(first) == kSelfMagic;
    long elf_offset = 0;

    if (is_self) {
        const unsigned char version = first[4];
        const unsigned char mode = first[5];
        const unsigned char endian = first[6];
        const unsigned char attributes = first[7];
        const unsigned char category = first[8];
        const unsigned char program_type = first[9];
        const uint16_t segment_count = u16le(first + 24);

        if (version != 0 || mode != 1 || endian != 1 || attributes != 0x12 ||
            category != 1 || program_type != 1 || segment_count == 0) {
            std::fclose(f);
            diag(diagnostic, diagnostic_size, "PS4 loader: en-tete SELF non pris en charge");
            return false;
        }

        elf_offset = 32L + static_cast<long>(segment_count) * 32L;
    }

    if (std::fseek(f, elf_offset, SEEK_SET) != 0) {
        std::fclose(f);
        diag(diagnostic, diagnostic_size, "PS4 loader: seek ELF impossible");
        return false;
    }

    unsigned char eh[64]{};
    if (std::fread(eh, 1, sizeof(eh), f) != sizeof(eh)) {
        std::fclose(f);
        diag(diagnostic, diagnostic_size, "PS4 loader: en-tete ELF incomplet");
        return false;
    }
    std::fclose(f);

    if (eh[0] != 0x7f || eh[1] != 'E' || eh[2] != 'L' || eh[3] != 'F') {
        diag(diagnostic, diagnostic_size, "PS4 loader: magic ELF invalide");
        return false;
    }

    if (eh[4] != 2 || eh[5] != 1 || eh[6] != 1 || eh[8] != 0) {
        diag(diagnostic, diagnostic_size, "PS4 loader: ABI ELF incompatible");
        return false;
    }

    const unsigned char osabi = eh[7];
    const uint16_t type = u16le(eh + 16);
    const uint16_t machine = u16le(eh + 18);
    const uint32_t version = u32le(eh + 20);
    const uint64_t entry = u64le(eh + 24);
    const uint16_t phentsize = u16le(eh + 54);
    const uint16_t phnum = u16le(eh + 56);
    const uint16_t shentsize = u16le(eh + 58);

    const bool sce_type = type == 0xFE00 || type == 0xFE10 || type == 0xFE18;
    const bool sce_elf = osabi == 9 && sce_type;
    // Open-source PS4 payload SDKs emit x86-64 System V ET_DYN ELF files.
    // Keep this as a separate, explicit homebrew profile instead of weakening
    // the SCE ELF checks used for commercial-style SELF/ELF validation.
    const bool homebrew_payload_elf = !is_self && osabi == 0 && type == 3;

    if ((!sce_elf && !homebrew_payload_elf) || machine != 62 || version != 1 ||
        phentsize != 56 || phnum == 0 || (shentsize != 0 && shentsize != 64)) {
        diag(diagnostic, diagnostic_size,
             "PS4 loader: ELF non pris en charge osabi=%llu type=0x%llx",
             static_cast<unsigned long long>(osabi),
             static_cast<unsigned long long>(type));
        return false;
    }

    if (out_info) {
        out_info->kind = is_self ? MAXPS4_EXEC_SELF : MAXPS4_EXEC_ELF;
        out_info->type = type;
        out_info->entry = entry;
        out_info->program_header_count = phnum;
    }

    if (homebrew_payload_elf) {
        diag(diagnostic, diagnostic_size,
             "PS4 homebrew ELF valide • entry=0x%llx • ph=%llu",
             static_cast<unsigned long long>(entry),
             static_cast<unsigned long long>(phnum));
    } else {
        diag(diagnostic, diagnostic_size,
             is_self ? "PS4 SELF valide • entry=0x%llx • ph=%llu"
                     : "PS4 SCE ELF valide • entry=0x%llx • ph=%llu",
             static_cast<unsigned long long>(entry),
             static_cast<unsigned long long>(phnum));
    }
    return true;
}

bool maxps4_ps4_loader_extract_plain_pkg_executable(
    const char *pkg_path, const char *output_path,
    char *diagnostic, unsigned long diagnostic_size) {
    if (!pkg_path || !output_path || !pkg_path[0] || !output_path[0]) {
        diag(diagnostic, diagnostic_size, "PS4 PKG: chemin d'extraction invalide");
        return false;
    }

    MaxPS4ExecutableInfo pkg_info{};
    char pkg_diag[256]{};
    if (!maxps4_ps4_loader_validate(pkg_path, &pkg_info,
                                    pkg_diag, sizeof(pkg_diag)) ||
        pkg_info.kind != MAXPS4_EXEC_PKG) {
        if (diagnostic && diagnostic_size) {
            std::snprintf(diagnostic, diagnostic_size, "%s",
                          pkg_diag[0] ? pkg_diag : "PS4 PKG: conteneur invalide");
        }
        return false;
    }

    FILE *in = std::fopen(pkg_path, "rb");
    if (!in) {
        diag(diagnostic, diagnostic_size, "PS4 PKG: ouverture extraction impossible errno=%llu",
             static_cast<unsigned long long>(errno));
        return false;
    }

    unsigned long long file_size = 0;
    if (!get_file_size(in, &file_size) || std::fseek(in, 0, SEEK_SET) != 0) {
        std::fclose(in);
        diag(diagnostic, diagnostic_size, "PS4 PKG: taille extraction illisible");
        return false;
    }

    unsigned char header[0x80]{};
    if (std::fread(header, 1, sizeof(header), in) != sizeof(header)) {
        std::fclose(in);
        diag(diagnostic, diagnostic_size, "PS4 PKG: en-tete extraction incomplet");
        return false;
    }

    const uint32_t table_count = u32be(header + 0x10);
    const uint32_t table_offset = u32be(header + 0x18);

    for (uint32_t i = 0; i < table_count; ++i) {
        const unsigned long long entry_pos =
            static_cast<unsigned long long>(table_offset) +
            static_cast<unsigned long long>(i) * 0x20ULL;
        if (entry_pos > file_size || 0x20ULL > file_size - entry_pos) break;

        if (std::fseek(in, static_cast<long>(entry_pos), SEEK_SET) != 0) break;
        unsigned char entry[0x20]{};
        if (std::fread(entry, 1, sizeof(entry), in) != sizeof(entry)) break;

        const uint32_t data_offset = u32be(entry + 0x10);
        const uint32_t data_size = u32be(entry + 0x14);
        if (data_size < 64 ||
            static_cast<unsigned long long>(data_size) > kMaxPlainPkgExecutableSize ||
            static_cast<unsigned long long>(data_offset) > file_size ||
            static_cast<unsigned long long>(data_size) >
                file_size - static_cast<unsigned long long>(data_offset)) {
            continue;
        }

        if (std::fseek(in, static_cast<long>(data_offset), SEEK_SET) != 0) continue;
        unsigned char probe[64]{};
        if (std::fread(probe, 1, sizeof(probe), in) != sizeof(probe) ||
            !looks_like_raw_executable(probe, sizeof(probe))) {
            continue;
        }

        FILE *out = std::fopen(output_path, "wb");
        if (!out) {
            std::fclose(in);
            diag(diagnostic, diagnostic_size,
                 "PS4 PKG: creation eboot temporaire impossible errno=%llu",
                 static_cast<unsigned long long>(errno));
            return false;
        }

        if (std::fseek(in, static_cast<long>(data_offset), SEEK_SET) != 0) {
            std::fclose(out);
            std::remove(output_path);
            continue;
        }

        unsigned char buffer[64 * 1024];
        uint32_t remaining = data_size;
        bool copied = true;
        while (remaining != 0) {
            const size_t chunk = remaining < sizeof(buffer)
                ? static_cast<size_t>(remaining) : sizeof(buffer);
            if (std::fread(buffer, 1, chunk, in) != chunk ||
                std::fwrite(buffer, 1, chunk, out) != chunk) {
                copied = false;
                break;
            }
            remaining -= static_cast<uint32_t>(chunk);
        }
        std::fclose(out);

        if (!copied) {
            std::remove(output_path);
            continue;
        }

        MaxPS4ExecutableInfo extracted_info{};
        char extracted_diag[256]{};
        const bool valid_executable =
            maxps4_ps4_loader_validate(output_path, &extracted_info,
                                       extracted_diag, sizeof(extracted_diag)) &&
            (extracted_info.kind == MAXPS4_EXEC_ELF ||
             extracted_info.kind == MAXPS4_EXEC_SELF);
        if (!valid_executable) {
            std::remove(output_path);
            continue;
        }

        std::fclose(in);
        if (diagnostic && diagnostic_size) {
            std::snprintf(diagnostic, diagnostic_size,
                          "PKG homebrew/plain • executable direct extrait • entree=%u • %u octets • %s",
                          i, data_size, extracted_diag);
        }
        return true;
    }

    std::fclose(in);
    std::remove(output_path);
    if (diagnostic && diagnostic_size) {
        std::snprintf(
            diagnostic, diagnostic_size,
            "%s • aucun ELF/SELF direct non chiffre dans la table • PFS/chiffre non extrait",
            pkg_diag);
    }
    return false;
}
