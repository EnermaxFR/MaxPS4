// SPDX-License-Identifier: GPL-2.0-or-later
#include "maxps4_ps4_loader.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace {

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

static void diag(char *dst, unsigned long size, const char *fmt,
                 unsigned long long a = 0, unsigned long long b = 0) {
    if (!dst || size == 0) return;
    std::snprintf(dst, size, fmt, a, b);
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

    constexpr uint32_t kSelfMagic = 0x1D3D154Fu;
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
