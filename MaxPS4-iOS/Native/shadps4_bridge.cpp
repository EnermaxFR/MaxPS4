#include "common/alignment.h"
// MaxPS4 bridge adapter (new work); calls original shadPS4 GPL-2.0-or-later utility.
// This does not implement PS4 emulation or launch any executable.
#include "common/string_util.h"
extern "C" int maxps4_shadps4_utility_probe() noexcept {
    return Common::ToLower("MAXPS4") == "maxps4" ? 1 : 0;
}

#include <cstddef>
#include <cstdint>
#if defined(__APPLE__)
#include <unistd.h>
#endif

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

// First PS4 ELF64 loader stage: validate PT_LOAD mapping without executing code.
// Assumes unmodified ELF64 x86-64 bytes, not an encrypted PS4 SELF container.
// Bounds, integer overflow, and overlap are rejected before eventual mapping.
extern "C" int maxps4_native_elf_load_layout(const std::uint8_t* data,
                                               std::size_t count,
                                               std::uint32_t* load_count,
                                               std::uint64_t* mapped_bytes) noexcept {
    if (!data || !load_count || !mapped_bytes || count < 64 ||
        maxps4_native_executable_signature(data, count) != 1 ||
        data[6] != 1) return 0;
    auto read16 = [&](std::size_t i) -> std::uint16_t {
        return std::uint16_t(data[i]) | (std::uint16_t(data[i + 1]) << 8);
    };
    auto read32 = [&](std::size_t i) -> std::uint32_t {
        std::uint32_t v = 0;
        for (unsigned b = 0; b < 4; ++b) v |= std::uint32_t(data[i + b]) << (8 * b);
        return v;
    };
    auto read64 = [&](std::size_t i) -> std::uint64_t {
        std::uint64_t v = 0;
        for (unsigned b = 0; b < 8; ++b) v |= std::uint64_t(data[i + b]) << (8 * b);
        return v;
    };
    const std::uint64_t offset = read64(32);
    const std::uint16_t entry_size = read16(54);
    const std::uint16_t headers = read16(56);
    if (entry_size != 56 || headers == 0 || headers > 256 ||
        offset > count || std::uint64_t(headers) * entry_size > count - offset) return 0;
    std::uint64_t total = 0;
    std::uint32_t mapped = 0;
    for (std::uint16_t i = 0; i < headers; ++i) {
        const auto h = static_cast<std::size_t>(offset + std::uint64_t(i) * entry_size);
        if (read32(h) != 1) continue; // PT_LOAD
        const std::uint64_t file_offset = read64(h + 8);
        const std::uint64_t virtual_address = read64(h + 16);
        const std::uint64_t file_size = read64(h + 32);
        const std::uint64_t memory_size = read64(h + 40);
        if (file_size > memory_size || file_offset > count ||
            file_size > count - file_offset ||
            virtual_address > UINT64_MAX - memory_size ||
            total > UINT64_MAX - memory_size) return 0;
        const std::uint64_t end = virtual_address + memory_size;
        for (std::uint16_t j = 0; j < i; ++j) {
            const auto old = static_cast<std::size_t>(offset + std::uint64_t(j) * entry_size);
            if (read32(old) != 1) continue;
            const auto old_address = read64(old + 16);
            const auto old_size = read64(old + 40);
            if (old_address > UINT64_MAX - old_size) return 0;
            if (memory_size && old_size &&
                virtual_address < old_address + old_size && old_address < end)
                return 0;
        }
        total += memory_size;
        ++mapped;
    }
    if (!mapped) return 0;
    *load_count = mapped;
    *mapped_bytes = total;
    return 1;
}

// Resolve the ELF64 entry to a validated executable PT_LOAD segment.
// This is metadata validation, not a guest memory mapping or code execution.
extern "C" int maxps4_native_elf_executable_entry(
    const std::uint8_t* data, std::size_t count,
    std::uint64_t* entry, std::uint64_t* file_offset) noexcept {
    if (!entry || !file_offset || !data || count < 64) return 0;
    std::uint32_t segments = 0;
    std::uint64_t bytes = 0;
    std::uint64_t start = 0;
    if (!maxps4_native_elf_load_layout(data, count, &segments, &bytes) ||
        !maxps4_native_elf_entry_point(data, count, &start)) return 0;
    auto u32 = [&](std::size_t p) -> std::uint32_t {
        std::uint32_t v = 0;
        for (unsigned j = 0; j < 4; ++j) v |= std::uint32_t(data[p + j]) << (8 * j);
        return v;
    };
    auto u64 = [&](std::size_t p) -> std::uint64_t {
        std::uint64_t v = 0;
        for (unsigned j = 0; j < 8; ++j) v |= std::uint64_t(data[p + j]) << (8 * j);
        return v;
    };
    const auto phoff = u64(32);
    const auto phnum = std::size_t(data[56]) | (std::size_t(data[57]) << 8);
    for (std::size_t i = 0; i < phnum; ++i) {
        const auto p = static_cast<std::size_t>(phoff + i * 56);
        if (u32(p) != 1 || (u32(p + 4) & 1u) == 0) continue; // PT_LOAD + PF_X
        const auto offset = u64(p + 8);
        const auto va = u64(p + 16);
        const auto filesz = u64(p + 32);
        // Require an actual instruction byte in the file-backed area.
        if (start >= va && start - va < filesz) {
            *entry = start;
            *file_offset = offset + (start - va);
            return 1;
        }
    }
    return 0;
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
    bool zero_flag = false;
    bool flags_valid = false;
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
            // x86 MOV does not modify RFLAGS (including ZF).
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
            zero_flag = static_cast<std::uint32_t>(rax) == 0;
            flags_valid = true;
            pc += 4;
            break;
        }
        case 0x2D: { // SUB EAX, imm32 (wrap at 32 bits)
            if (size - pc < 4) return 0;
            std::uint32_t imm = 0;
            for (unsigned i = 0; i < 4; ++i)
                imm |= std::uint32_t(code[pc + i]) << (8 * i);
            rax = static_cast<std::uint32_t>(static_cast<std::uint32_t>(rax) - imm);
            zero_flag = static_cast<std::uint32_t>(rax) == 0;
            flags_valid = true;
            pc += 4;
            break;
        }
        case 0x35: { // XOR EAX, imm32 (32-bit result zero-extends)
            if (size - pc < 4) return 0;
            std::uint32_t imm = 0;
            for (unsigned i = 0; i < 4; ++i)
                imm |= std::uint32_t(code[pc + i]) << (8 * i);
            rax = static_cast<std::uint32_t>(rax) ^ imm;
            zero_flag = static_cast<std::uint32_t>(rax) == 0;
            flags_valid = true;
            pc += 4;
            break;
        }
        case 0x25: { // AND EAX, imm32; zero extends to RAX
            if (size - pc < 4) return 0;
            std::uint32_t imm = 0;
            for (unsigned i = 0; i < 4; ++i)
                imm |= std::uint32_t(code[pc + i]) << (8 * i);
            rax = static_cast<std::uint32_t>(rax) & imm;
            zero_flag = static_cast<std::uint32_t>(rax) == 0;
            flags_valid = true;
            pc += 4;
            break;
        }
        case 0x0D: { // OR EAX, imm32, wraps to 32 bits
            if (size - pc < 4) return 0;
            std::uint32_t imm = 0;
            for (unsigned i = 0; i < 4; ++i)
                imm |= std::uint32_t(code[pc + i]) << (8 * i);
            rax = static_cast<std::uint32_t>(rax) | imm;
            zero_flag = static_cast<std::uint32_t>(rax) == 0;
            flags_valid = true; // OR defines ZF in x86
            pc += 4;
            break;
        }
        case 0xA9: { // TEST EAX, imm32: only ZF currently consumed by JZ/JNZ
            if (size - pc < 4) return 0;
            std::uint32_t imm = 0;
            for (unsigned i = 0; i < 4; ++i)
                imm |= std::uint32_t(code[pc + i]) << (8 * i);
            zero_flag = (static_cast<std::uint32_t>(rax) & imm) == 0;
            flags_valid = true;
            pc += 4;
            break;
        }
        case 0x3D: { // CMP EAX, imm32: update zero comparison state
            if (size - pc < 4) return 0;
            std::uint32_t imm = 0;
            for (unsigned i = 0; i < 4; ++i)
                imm |= std::uint32_t(code[pc + i]) << (8 * i);
            zero_flag = static_cast<std::uint32_t>(rax) == imm;
            flags_valid = true;
            pc += 4;
            break;
        }
        case 0x74: // JZ rel8
        case 0x75: { // JNZ rel8
            if (pc >= size || !flags_valid) return 0;
            const std::int8_t displacement = static_cast<std::int8_t>(code[pc++]);
            const bool taken = op == 0x74 ? zero_flag : !zero_flag;
            if (taken) {
                const std::int64_t target = static_cast<std::int64_t>(pc) + displacement;
                if (target < 0 || target >= static_cast<std::int64_t>(size)) return 0;
                pc = static_cast<std::size_t>(target);
            }
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

// Data-only translation entry used for the optional JIT preflight.
extern "C" int maxps4_arm64_translate_preview(
    const std::uint8_t* guest, std::size_t count,
    std::uint32_t* output, std::size_t capacity,
    std::size_t* emitted) noexcept;

// Per-thread, data-only translation cache. No executable memory, shared state,
// or external guest pointers are retained. Exact bytes are compared to avoid
// hash collisions; a bounded four-entry ring prevents unbounded memory growth.
namespace {
struct ARM64PreviewCacheEntry {
    std::uint8_t guest[4096] = {};
    std::uint32_t words[4096] = {};
    std::size_t guest_size = 0;
    std::size_t word_count = 0;
    bool valid = false;
};
struct ARM64PreviewCache {
    ARM64PreviewCacheEntry entries[4] = {};
    std::size_t next = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
};
thread_local ARM64PreviewCache preview_cache;

bool translate_or_reuse(const std::uint8_t* guest, std::size_t size) noexcept {
    if (!guest || size == 0 || size > 4096) return false;
    for (auto& entry : preview_cache.entries) {
        if (entry.valid && entry.guest_size == size) {
            bool same = true;
            for (std::size_t i = 0; i < size; ++i)
                if (entry.guest[i] != guest[i]) { same = false; break; }
            if (same) {
                ++preview_cache.hits;
                return true;
            }
        }
    }
    ++preview_cache.misses;
    auto& entry = preview_cache.entries[preview_cache.next];
    preview_cache.next = (preview_cache.next + 1) % 4;
    entry.valid = false;
    std::size_t emitted = 0;
    if (maxps4_arm64_translate_preview(guest, size, entry.words, 4096, &emitted) != 1)
        return false;
    for (std::size_t i = 0; i < size; ++i) entry.guest[i] = guest[i];
    entry.guest_size = size;
    entry.word_count = emitted;
    entry.valid = true;
    return true;
}
} // namespace

// Diagnostic only: reports translation cache activity, never JIT execution.
extern "C" void maxps4_arm64_preview_cache_stats(
    std::uint64_t* hits, std::uint64_t* misses) noexcept {
    if (hits) *hits = preview_cache.hits;
    if (misses) *misses = preview_cache.misses;
}

// Explicit non-executable preflight: 1 means supported and cached as DATA,
// 0 means the guest sequence is invalid/unsupported. Never signals usable JIT.
extern "C" int maxps4_native_arm64_preflight(
    const std::uint8_t* code, std::size_t size) noexcept {
    return translate_or_reuse(code, size) ? 1 : 0;
}

// StikDebug iOS 26 universal breakpoint ABI.
// These entry points are intentionally NOT called by the current backend:
// BRK without the universal.js debugger attached would terminate the app.
// They provide an app-side protocol surface for a future W^X JIT allocator.
// Source protocol: StikDebug/StikJIT INTEGRATION.md.
#if defined(__APPLE__) && defined(__aarch64__)
extern "C" __attribute__((naked, noinline, optnone))
void maxps4_stikdebug_jit26_detach() noexcept {
    __asm__("mov x16, #0\n"
            "brk #0xf00d\n"
            "ret");
}

extern "C" __attribute__((naked, noinline, optnone))
void* maxps4_stikdebug_jit26_prepare_region(void* address,
                                             std::size_t length) noexcept {
    __asm__("mov x16, #1\n"
            "brk #0xf00d\n"
            "ret");
}
#endif

// JIT allocator groundwork: validate an ordinary writable page and release it.
// Deliberately NEVER requests executable protection or sends BRK. Success
// proves only that the VM allocator works, not that iOS permits JIT execution.
#if defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif
extern "C" int maxps4_native_jit_writable_page_probe(
    std::size_t* page_size_out) noexcept {
    if (!page_size_out) return 0;
    *page_size_out = 0;
#if defined(__APPLE__)
    const long page = sysconf(_SC_PAGESIZE);
    if (page <= 0 || page > 65536) return 0;
    void* memory = mmap(nullptr, static_cast<std::size_t>(page),
                        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (memory == MAP_FAILED) return 0;
    auto* bytes = static_cast<volatile std::uint8_t*>(memory);
    bytes[0] = 0x2a;
    const bool ok = bytes[0] == 0x2a;
    const int released = munmap(memory, static_cast<std::size_t>(page));
    if (!ok || released != 0) return 0;
    *page_size_out = static_cast<std::size_t>(page);
    return 1;
#else
    return 0;
#endif
}

// Read-only debugger attachment hint on iOS. A traced process does NOT
// necessarily have StikDebug's script, JIT permissions or executable pages.
// Never emit BRK merely to probe whether a debugger exists.
#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <unistd.h>
#endif
extern "C" int maxps4_native_debugger_attached() noexcept {
#if defined(__APPLE__)
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
    struct kinfo_proc info = {};
    std::size_t length = sizeof(info);
    if (sysctl(mib, 4, &info, &length, nullptr, 0) != 0 ||
        length != sizeof(info)) return -1; // Unknown: not false.
    return (info.kp_proc.p_flag & P_TRACED) != 0 ? 1 : 0;
#else
    return -1;
#endif
}

// A protocol ABI existing in the binary does not mean the debugger attached,
// that executable memory was prepared, or that a functional recompiler exists.
extern "C" int maxps4_stikdebug_jit26_protocol_available() noexcept {
#if defined(__APPLE__) && defined(__aarch64__)
    return 1;
#else
    return 0;
#endif
}

// Execution backend selection point for an eventual ARM64 dynamic recompiler.
// iOS code-signing/JIT entitlements must be validated before enabling JIT.
// No RWX memory allocation or code generation is attempted here.
// requested_mode: 0 = interpreter, 1 = request JIT; used_mode reports reality.
extern "C" int maxps4_native_guest_run_with_backend(
    const std::uint8_t* code, std::size_t size, std::uint32_t budget,
    int requested_mode, int* used_mode, std::uint64_t* result) noexcept {
    if (!used_mode || (requested_mode != 0 && requested_mode != 1)) return 0;
    *used_mode = 0; // Never claim JIT execution on iOS without executable-code support.
    if (requested_mode == 1 && code && size > 0 && size <= 4096) {
        // Cache exact translated blocks as DATA; execution still uses interpreter.
        (void)translate_or_reuse(code, size);
    }
    return maxps4_native_guest_x86_run(code, size, budget, result);
}

// 0: ARM64 dynamic recompiler unavailable; 1: fully implemented and permitted.
// Never equate ARM64 compilation support with a functional JIT entitlement.
extern "C" int maxps4_native_arm64_jit_ready() noexcept {
    return 0;
}

// Structural verifier for the restricted ARM64 translation preview.
// No generated code is executed. Reject unknown instructions and branches
// outside the block before any future executable-memory handoff.
extern "C" int maxps4_arm64_verify_preview(
    const std::uint32_t* words, std::size_t count) noexcept {
    if (!words || count == 0 || count > 4096 ||
        words[count - 1] != 0xD65F03C0u) return 0;
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint32_t w = words[i];
        if (w == 0xD65F03C0u) {
            if (i + 1 != count) return 0;
        } else if ((w & 0xFFE0001Fu) == 0x52800000u ||
                   (w & 0xFFE0001Fu) == 0x72A00000u ||
                   (w & 0xFFE0001Fu) == 0x52800001u ||
                   (w & 0xFFE0001Fu) == 0x72A00001u ||
                   (w & 0xFFC0001Fu) == 0x31000000u ||
                   (w & 0xFFC0001Fu) == 0x71000000u ||
                   w == 0x2B010000u || w == 0x6B010000u ||
                   w == 0x4A010000u || w == 0x0A010000u ||
                   w == 0x2A010000u || w == 0x6A00001Fu ||
                   w == 0x6B01001Fu || w == 0x6A01001Fu ||
                   w == 0xD503201Fu) {
            continue;
        } else if ((w & 0xFF000010u) == 0x54000000u &&
                   ((w & 0xFu) == 0 || (w & 0xFu) == 1)) {
            std::int32_t rel = static_cast<std::int32_t>((w >> 5) & 0x7ffffu);
            if (rel & 0x40000) rel -= 0x80000;
            const std::int64_t dest = static_cast<std::int64_t>(i) + rel;
            if (dest < 0 || dest >= static_cast<std::int64_t>(count)) return 0;
        } else {
            return 0;
        }
    }
    return 1;
}

// Offline x86->AArch64 code emission prototype. Generated instruction words are
// DATA ONLY: never mapped executable or jumped into. No iOS JIT entitlement implied.
// Subset: MOV EAX,imm32; ADD/SUB/XOR/AND/OR/CMP/TEST EAX,imm32; bounded forward/backward JZ/JNZ rel8; NOP; RET. Refuse other instructions.
extern "C" int maxps4_arm64_translate_preview(
    const std::uint8_t* guest, std::size_t count,
    std::uint32_t* output, std::size_t capacity,
    std::size_t* emitted) noexcept {
    if (!guest || !output || !emitted || count == 0 || count > 4096 ||
        capacity == 0 || capacity > 4096) return 0;
    std::size_t pc = 0, n = 0;
    bool terminated = false;
    bool cmp_ready = false;
    // Record x86 instruction boundaries and resolve conditional targets
    // only after ARM64 instruction sizes are known.
    std::size_t arm_at_guest[4097] = {};
    bool boundary[4097] = {};
    struct Fixup { std::size_t arm_index, guest_target; std::uint32_t condition; };
    Fixup fixups[4096] = {};
    std::size_t fixup_count = 0;
    auto put = [&](std::uint32_t instruction) noexcept -> bool {
        if (n >= capacity) return false;
        output[n++] = instruction;
        return true;
    };
    while (pc < count) {
        boundary[pc] = true;
        arm_at_guest[pc] = n;
        const std::uint8_t opcode = guest[pc++];
        if (opcode == 0xB8) {
            // MOVZ/MOVK preserve ARM64 NZCV, matching x86 MOV preserving ZF.
            if (count - pc < 4) return 0;
            std::uint32_t imm = 0;
            for (unsigned i = 0; i < 4; ++i)
                imm |= std::uint32_t(guest[pc+i]) << (i*8);
            pc += 4;
            // MOVZ W0,#lo16; MOVK W0,#hi16,LSL#16
            if (!put(0x52800000u | ((imm & 0xffffu) << 5)) ||
                !put(0x72A00000u | (((imm >> 16) & 0xffffu) << 5))) return 0;
        } else if (opcode == 0x05 || opcode == 0x2D) {
            cmp_ready = true; // ADDS/SUBS will set NZCV.Z
            if (count - pc < 4) return 0;
            std::uint32_t imm = 0;
            for (unsigned i = 0; i < 4; ++i)
                imm |= std::uint32_t(guest[pc+i]) << (i*8);
            pc += 4;
            if (imm <= 4095) {
                if (!put((opcode == 0x05 ? 0x31000000u : 0x71000000u) | (imm << 10))) return 0; // ADDS/SUBS W0,W0,#imm12
            } else {
                // Materialize full 32-bit immediate into W1 then ADDS/SUBS W0,W0,W1.
                // This preserves x86 32-bit wrapping semantics.
                if (!put(0x52800001u | ((imm & 0xffffu) << 5)) ||
                    !put(0x72A00001u | (((imm >> 16) & 0xffffu) << 5)) ||
                    !put(opcode == 0x05 ? 0x2B010000u : 0x6B010000u)) return 0;
            }
        } else if (opcode == 0x35 || opcode == 0x25 || opcode == 0x0D) {
            cmp_ready = true; // XOR/AND/OR all define x86 ZF
            if (count - pc < 4) return 0;
            std::uint32_t imm = 0;
            for (unsigned i = 0; i < 4; ++i)
                imm |= std::uint32_t(guest[pc + i]) << (8 * i);
            pc += 4;
            // MOVZ/MOVK W1,#imm32; EOR, AND, or ORR W0,W0,W1.
            if (!put(0x52800001u | ((imm & 0xffffu) << 5)) ||
                !put(0x72A00001u | (((imm >> 16) & 0xffffu) << 5)) ||
                !put(opcode == 0x35 ? 0x4A010000u : (opcode == 0x25 ? 0x0A010000u : 0x2A010000u))) return 0;
            if (!put(0x6A00001Fu)) return 0; // TST W0,W0 sets NZCV.Z for XOR/AND/OR
        } else if (opcode == 0x3D || opcode == 0xA9) {
            cmp_ready = true;
            if (count - pc < 4) return 0;
            std::uint32_t imm = 0;
            for (unsigned i = 0; i < 4; ++i)
                imm |= std::uint32_t(guest[pc + i]) << (8 * i);
            pc += 4;
            // Materialize operand in W1 and set ZF-equivalent ARM64 NZCV.
            if (!put(0x52800001u | ((imm & 0xffffu) << 5)) ||
                !put(0x72A00001u | (((imm >> 16) & 0xffffu) << 5)) ||
                !put(opcode == 0x3D ? 0x6B01001Fu : 0x6A01001Fu)) return 0; // CMP or TST W0,W1
        } else if (opcode == 0x74 || opcode == 0x75) {
            if (pc >= count || !cmp_ready) return 0;
            const std::int8_t offset = static_cast<std::int8_t>(guest[pc++]);
            const std::int64_t target = static_cast<std::int64_t>(pc) + offset;
            if (target < 0 || target >= static_cast<std::int64_t>(count) ||
                fixup_count >= 4096) return 0;
            // Back edges must start at an instruction that recomputes ZF.
            // Arithmetic and logic ops now generate NZCV.Z, permitting
            // bounded decrement loops such as SUB EAX,1 / JNZ loop.
            if (target < static_cast<std::int64_t>(pc)) {
                const std::uint8_t target_op = guest[target];
                const bool sets_zf = target_op == 0x3D || target_op == 0xA9 ||
                    target_op == 0x05 || target_op == 0x2D ||
                    target_op == 0x35 || target_op == 0x25 || target_op == 0x0D;
                if (!sets_zf || target >= static_cast<std::int64_t>(pc - 2))
                    return 0;
            }
            fixups[fixup_count++] = { n, static_cast<std::size_t>(target),
                                       opcode == 0x74 ? 0u : 1u };
            if (!put(0)) return 0;
        } else if (opcode == 0x90) {
            if (!put(0xD503201Fu)) return 0; // ARM64 NOP
        } else if (opcode == 0xC3) {
            if (pc != count || !put(0xD65F03C0u)) return 0; // ARM64 RET
            terminated = true;
            break;
        } else return 0;
    }
    if (!terminated) return 0;
    for (std::size_t i = 0; i < fixup_count; ++i) {
        const auto& fixup = fixups[i];
        if (!boundary[fixup.guest_target]) return 0;
        // A branch landing directly on another conditional branch could
        // bypass the flag producer required by that branch. Until we have
        // control-flow flag liveness analysis, reject such merges.
        const std::uint8_t target_opcode = guest[fixup.guest_target];
        if (target_opcode == 0x74 || target_opcode == 0x75) return 0;
        const std::size_t dest = arm_at_guest[fixup.guest_target];
        const std::int64_t delta = static_cast<std::int64_t>(dest) -
                                   static_cast<std::int64_t>(fixup.arm_index);
        if (delta < -(1 << 18) || delta >= (1 << 18)) return 0;
        // B.cond immediate is a signed 19-bit count of ARM64 instructions.
        const std::uint32_t encoded = static_cast<std::uint32_t>(delta) & 0x7ffffu;
        output[fixup.arm_index] = 0x54000000u | (encoded << 5) |
                                  fixup.condition;
    }
    *emitted = n;
    return 1;
}
