#include "common/alignment.h"
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

// PS4 SELF header layout adapted from shadPS4 src/core/loader/elf.h,
// Copyright 2024 shadPS4 Emulator Project, GPL-2.0-or-later.
// Metadata-only inspection; does not decrypt, extract or execute a SELF.
extern "C" int maxps4_native_self_segment_count(const std::uint8_t* data,
                                                   std::size_t count,
                                                   std::uint16_t* segment_count) noexcept {
    if (!segment_count || !data || count < 32 ||
        maxps4_native_executable_signature(data, count) != 2) return 0;
    if (data[6] != 1) return 0; // little-endian SELF
    const std::uint16_t segments = std::uint16_t(data[24]) |
                                   (std::uint16_t(data[25]) << 8);
    if (segments == 0 || segments > 256) return 0;
    // SELF segment headers are 32 bytes each; reject incomplete tables.
    if (std::size_t(segments) > (count - 32) / 32) return 0;
    *segment_count = segments;
    return 1;
}

// Segment flag interpretation follows shadPS4 src/core/loader/elf.h
// (GPL-2.0-or-later). This only inventories metadata, never decrypts segments.
// Returns 1 on success; incomplete SELF segment tables are rejected.
extern "C" int maxps4_native_self_segment_flags(const std::uint8_t* data,
                                                   std::size_t count,
                                                   std::uint16_t* encrypted,
                                                   std::uint16_t* compressed) noexcept {
    if (!encrypted || !compressed || !data) return 0;
    std::uint16_t segments = 0;
    if (!maxps4_native_self_segment_count(data, count, &segments)) return 0;
    std::uint16_t encrypted_total = 0;
    std::uint16_t compressed_total = 0;
    for (std::size_t i = 0; i < segments; ++i) {
        const std::size_t offset = 32 + i * 32;
        // flags are little-endian u64; only low-byte bits 1 and 3 matter.
        const std::uint8_t flags = data[offset];
        if (flags & 0x02) ++encrypted_total;
        if (flags & 0x08) ++compressed_total;
    }
    *encrypted = encrypted_total;
    *compressed = compressed_total;
    return 1;
}

// Bounded SELF segment file-range validation based on shadPS4 self_segment_header.
// Reject segments extending outside supplied bytes; metadata only, no mapping.
extern "C" int maxps4_native_self_file_ranges_valid(const std::uint8_t* data,
                                                       std::size_t count) noexcept {
    std::uint16_t segments = 0;
    if (!maxps4_native_self_segment_count(data, count, &segments)) return 0;
    auto read_u64 = [&](std::size_t offset) noexcept {
        std::uint64_t value = 0;
        for (unsigned i = 0; i < 8; ++i)
            value |= std::uint64_t(data[offset + i]) << (i * 8);
        return value;
    };
    for (std::size_t i = 0; i < segments; ++i) {
        const std::size_t offset = 32 + i * 32;
        const std::uint64_t file_offset = read_u64(offset + 8);
        const std::uint64_t file_size = read_u64(offset + 16);
        // Division-free range check avoids integer overflow.
        if (file_offset > count || file_size > count - file_offset) return 0;
    }
    return 1;
}

// SELF segment memory-size preflight adapted from shadPS4's self_segment_header
// (GPL-2.0-or-later). Metadata-only: no executable allocation or decryption.
extern "C" int maxps4_native_self_memory_sizes_valid(const std::uint8_t* data,
                                                       std::size_t count) noexcept {
    std::uint16_t segments = 0;
    if (!maxps4_native_self_segment_count(data, count, &segments)) return 0;
    auto read_u64 = [&](std::size_t offset) noexcept {
        std::uint64_t value = 0;
        for (unsigned i = 0; i < 8; ++i)
            value |= std::uint64_t(data[offset + i]) << (8 * i);
        return value;
    };
    constexpr std::uint64_t max_segment_bytes = 256ull * 1024 * 1024;
    constexpr std::uint64_t max_total_bytes = 512ull * 1024 * 1024;
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < segments; ++i) {
        const std::size_t offset = 32 + i * 32;
        const std::uint64_t disk_size = read_u64(offset + 16);
        const std::uint64_t memory_size = read_u64(offset + 24);
        if (memory_size < disk_size || memory_size > max_segment_bytes ||
            memory_size > max_total_bytes - total) return 0;
        total += memory_size;
    }
    return 1;
}

// Based on shadPS4 Core::Loader::Elf::Open: SELF segment headers are
// immediately followed by an embedded ELF64 header (GPL-2.0-or-later).
// Returns embedded ELF entry point only; no SELF decryption or execution.
extern "C" int maxps4_native_self_embedded_elf_entry(const std::uint8_t* data,
                                                      std::size_t count,
                                                      std::uint64_t* entry) noexcept {
    if (!entry) return 0;
    std::uint16_t segments = 0;
    if (!maxps4_native_self_segment_count(data, count, &segments)) return 0;
    const std::size_t elf_offset = 32 + std::size_t(segments) * 32;
    if (elf_offset > count || count - elf_offset < 64) return 0;
    return maxps4_native_elf_entry_point(data + elf_offset, count - elf_offset, entry);
}

extern "C" int maxps4_shadps4_alignment_probe() noexcept {
    return Common::AlignUp<std::uint64_t>(0x1001, 0x4000) == 0x4000 &&
           Common::AlignDown<std::uint64_t>(0x7fff, 0x4000) == 0x4000 &&
           Common::Is16KBAligned<std::uint64_t>(0x4000) &&
           !Common::Is16KBAligned<std::uint64_t>(0x4001) ? 1 : 0;
}

// PS4-specific ELF object types from upstream shadPS4 core/loader/elf.h.
// Return the ELF e_type only for recognized PS4 SCE executables or libraries.
// This is loader metadata inspection, NOT guest code execution.
extern "C" int maxps4_native_ps4_elf_type(const std::uint8_t* data,
                                           std::size_t count,
                                           std::uint16_t* out_type) noexcept {
    if (!out_type || !data || count < 64 ||
        maxps4_native_executable_signature(data, count) != 1 || data[6] != 1)
        return 0;
    const std::uint16_t type = std::uint16_t(data[16]) |
                               (std::uint16_t(data[17]) << 8);
    switch (type) {
    case 0xfe00: // ET_SCE_EXEC
    case 0xfe0c: // ET_SCE_STUBLIB
    case 0xfe10: // ET_SCE_DYNEXEC
    case 0xfe18: // ET_SCE_DYNAMIC
        *out_type = type;
        return 1;
    default: return 0;
    }
}

// First bounded native execution backend for *synthetic* x86 guest bytecode.
// Interpreter executes on ARM64 without JIT; not an upstream shadPS4 CPU engine.
// Return: 1 halted, 0 malformed/unsupported, -1 exceeded instruction budget.
extern "C" int maxps4_native_guest_x86_run(const std::uint8_t* code,
                                            std::size_t size,
                                            std::uint32_t budget,
                                            std::uint64_t* result) noexcept {
    if (!code || !result || size == 0 || size > 4096 || budget == 0 || budget > 4096) return 0;
    std::uint64_t rax = 0;
    std::size_t pc = 0;
    for (std::uint32_t step = 0; step < budget; ++step) {
        if (pc >= size) return 0;
        const std::uint8_t op = code[pc++];
        switch (op) {
        case 0x90: // NOP
            break;
        case 0xB8: { // MOV EAX, imm32, zero extends into RAX
            if (size - pc < 4) return 0;
            std::uint32_t imm = 0;
            for (unsigned i = 0; i < 4; ++i)
                imm |= std::uint32_t(code[pc + i]) << (8 * i);
            rax = imm;
            pc += 4;
            break;
        }
        case 0x05: { // ADD EAX, imm32 (wrap at 32 bits)
            if (size - pc < 4) return 0;
            std::uint32_t imm = 0;
            for (unsigned i = 0; i < 4; ++i)
                imm |= std::uint32_t(code[pc + i]) << (8 * i);
            rax = std::uint32_t(rax) + imm;
            rax &= 0xFFFF'FFFFull;
            pc += 4;
            break;
        }
        case 0xC3: // Termination only (no guest call stack yet)
            *result = rax;
            return 1;
        default: return 0;
        }
    }
    return -1;
}
