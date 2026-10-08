// MaxPS4 bridge adapter (new work); calls original shadPS4 GPL-2.0-or-later utility.
// This does not implement PS4 emulation or launch any executable.
#include "common/string_util.h"
extern "C" int maxps4_shadps4_utility_probe() noexcept {
    return Common::ToLower("MAXPS4") == "maxps4" ? 1 : 0;
}

#include <cstddef>
#include <cstdint>

// Bounded native executable signature classifier for the future PS4 loader.
// Returns 1 for ELF64 x86-64, 2 for a possible SELF, 0 otherwise.
extern "C" int maxps4_native_executable_signature(const std::uint8_t* data,
                                                    std::size_t count) noexcept {
    if (!data || count < 4) return 0;
    if (count >= 20 && data[0] == 0x7f && data[1] == 'E' &&
        data[2] == 'L' && data[3] == 'F' && data[4] == 2 &&
        data[5] == 1 && data[18] == 0x3e && data[19] == 0)
        return 1;
    if (data[0] == 0x4f && data[1] == 0x15 &&
        data[2] == 0x3d && data[3] == 0x1d)
        return 2;
    return 0;
}

// Read an ELF64 little-endian entry point without trusting unbounded input.
// Returns 1 only for a complete x86-64 ELF header (minimum 64 bytes).
extern "C" int maxps4_native_elf_entry_point(const std::uint8_t* data,
                                                std::size_t count,
                                                std::uint64_t* entry) noexcept {
    if (!entry || !data || count < 64 || maxps4_native_executable_signature(data, count) != 1 ||
        data[6] != 1) return 0;
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(data[24 + i]) << (8 * i);
    }
    *entry = value;
    return 1;
}
