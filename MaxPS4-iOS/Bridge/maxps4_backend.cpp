#include "maxps4_backend.h"

#include <cstdio>
#include <cstdint>
#include <cstring>

#if defined(MAXPS4_HAS_SHADPS4_FEX)
extern "C" int maxps4_fex_guest_harness_run(void);
extern "C" const char* maxps4_fex_guest_last_error(void);
#endif

static char g_backend_diagnostic[256] =
#if defined(MAXPS4_HAS_SHADPS4_FEX)
    "shadPS4/FEX guest backend linked • on-device self-test not run";
#else
    "FEXCore ARM64 validated • shadPS4 guest backend staging";
#endif

namespace {

static bool read_exact(FILE* f, void* out, size_t size) {
    return f != nullptr && std::fread(out, 1, size, f) == size;
}

static std::uint16_t le16(const unsigned char* p) {
    return static_cast<std::uint16_t>(p[0]) |
           (static_cast<std::uint16_t>(p[1]) << 8);
}

static std::uint32_t le32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

static bool validate_ps4_elf_header(const unsigned char* h, char* reason, size_t reason_size) {
    if (!(h[0] == 0x7f && h[1] == 'E' && h[2] == 'L' && h[3] == 'F')) {
        std::snprintf(reason, reason_size, "ELF magic invalide");
        return false;
    }
    if (h[4] != 2 || h[5] != 1 || h[6] != 1) {
        std::snprintf(reason, reason_size, "ELF64 little-endian attendu");
        return false;
    }
    if (h[7] != 9 || h[8] != 0) {
        std::snprintf(reason, reason_size, "ABI PS4/FreeBSD invalide");
        return false;
    }

    const std::uint16_t type = le16(h + 16);
    const std::uint16_t machine = le16(h + 18);
    const std::uint32_t version = le32(h + 20);
    const std::uint16_t phentsize = le16(h + 54);
    const std::uint16_t shentsize = le16(h + 58);

    if (!(type == 0xfe00 || type == 0xfe10 || type == 0xfe18)) {
        std::snprintf(reason, reason_size, "type SCE non pris en charge: 0x%04x", type);
        return false;
    }
    if (machine != 0x3e) {
        std::snprintf(reason, reason_size, "architecture non x86-64: 0x%04x", machine);
        return false;
    }
    if (version != 1) {
        std::snprintf(reason, reason_size, "version ELF invalide");
        return false;
    }
    if (phentsize != 56) {
        std::snprintf(reason, reason_size, "taille programme ELF invalide: %u", phentsize);
        return false;
    }
    if (shentsize != 0 && shentsize != 64) {
        std::snprintf(reason, reason_size, "taille section ELF invalide: %u", shentsize);
        return false;
    }
    return true;
}

} // namespace

MaxPS4BackendState maxps4_backend_state(void) {
#if defined(MAXPS4_HAS_SHADPS4_FEX)
    return MAXPS4_BACKEND_READY;
#else
    return MAXPS4_BACKEND_NOT_LINKED;
#endif
}

const char *maxps4_backend_name(void) {
#if defined(MAXPS4_HAS_SHADPS4_FEX)
    return "shadPS4/FEXCore ARM64 guest backend";
#else
    return "FEXCore ARM64 validated • shadPS4 staging";
#endif
}

const char *maxps4_backend_diagnostic(void) {
    return g_backend_diagnostic;
}

bool maxps4_backend_self_test(void) {
#if defined(MAXPS4_HAS_SHADPS4_FEX)
    const int result = maxps4_fex_guest_harness_run();
    const char *detail = maxps4_fex_guest_last_error();
    if (result == 0) {
        std::snprintf(g_backend_diagnostic, sizeof(g_backend_diagnostic),
                      "shadPS4/FEX guest backend self-test OK");
        return true;
    }
    std::snprintf(g_backend_diagnostic, sizeof(g_backend_diagnostic),
                  "shadPS4/FEX self-test FAIL code=%d • %s",
                  result, detail ? detail : "no detail");
    return false;
#else
    std::snprintf(g_backend_diagnostic, sizeof(g_backend_diagnostic),
                  "shadPS4/FEX guest backend not linked");
    return false;
#endif
}

bool maxps4_backend_validate_executable(const char *path) {
    if (path == nullptr || path[0] == '\0') {
        std::snprintf(g_backend_diagnostic, sizeof(g_backend_diagnostic),
                      "Loader PS4 • chemin vide");
        return false;
    }

    FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::snprintf(g_backend_diagnostic, sizeof(g_backend_diagnostic),
                      "Loader PS4 • fichier inaccessible");
        return false;
    }

    unsigned char first[32]{};
    if (!read_exact(f, first, sizeof(first))) {
        std::fclose(f);
        std::snprintf(g_backend_diagnostic, sizeof(g_backend_diagnostic),
                      "Loader PS4 • fichier trop court");
        return false;
    }

    // shadPS4 accepts either a raw PS4 ELF or a PS4 SELF wrapper. For SELF,
    // the embedded ELF header follows the 32-byte SELF header plus N 32-byte
    // SELF segment descriptors.
    const bool is_self = le32(first) == 0x1d3d154fU;
    std::uint64_t elf_offset = 0;
    if (is_self) {
        const std::uint16_t segment_count = le16(first + 24);
        elf_offset = 32ULL + static_cast<std::uint64_t>(segment_count) * 32ULL;
        if (std::fseek(f, static_cast<long>(elf_offset), SEEK_SET) != 0) {
            std::fclose(f);
            std::snprintf(g_backend_diagnostic, sizeof(g_backend_diagnostic),
                          "Loader PS4 • SELF tronqué");
            return false;
        }
    } else {
        std::rewind(f);
    }

    unsigned char elf[64]{};
    const bool read_ok = read_exact(f, elf, sizeof(elf));
    std::fclose(f);
    if (!read_ok) {
        std::snprintf(g_backend_diagnostic, sizeof(g_backend_diagnostic),
                      "Loader PS4 • en-tête ELF incomplet");
        return false;
    }

    char reason[128]{};
    if (!validate_ps4_elf_header(elf, reason, sizeof(reason))) {
        std::snprintf(g_backend_diagnostic, sizeof(g_backend_diagnostic),
                      "Loader PS4 • rejeté • %s", reason);
        return false;
    }

    const std::uint16_t type = le16(elf + 16);
    const std::uint16_t phnum = le16(elf + 56);
    std::snprintf(g_backend_diagnostic, sizeof(g_backend_diagnostic),
                  "Loader PS4 • OK • %s • SCE type=0x%04x • ph=%u",
                  is_self ? "SELF" : "ELF", type, phnum);
    return true;
}

bool maxps4_backend_boot(const char *path) {
    if (!maxps4_backend_validate_executable(path)) {
        return false;
    }
#if defined(MAXPS4_HAS_SHADPS4_FEX)
    // The executable has passed the same PS4 ELF/SELF structural gate expected
    // before shadPS4's real loader handoff. Full Emulator::PrepareWindow/RunLoop
    // wiring is the next integration milestone; do not report a running game yet.
    const char* validated = g_backend_diagnostic;
    char copy[sizeof(g_backend_diagnostic)]{};
    std::snprintf(copy, sizeof(copy), "%s", validated);
    std::snprintf(g_backend_diagnostic, sizeof(g_backend_diagnostic),
                  "%s • runtime handoff pending", copy);
    return false;
#else
    return false;
#endif
}

void maxps4_backend_stop(void) {
}
