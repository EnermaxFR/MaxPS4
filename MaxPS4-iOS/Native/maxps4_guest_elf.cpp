#include "maxps4_guest_elf.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <new>
#include <sys/mman.h>
#include <unistd.h>

namespace maxps4 {
namespace {
uint64_t number(const uint8_t* p, size_t width) {
    uint64_t value = 0;
    for (size_t i = 0; i < width; ++i) value |= uint64_t(p[i]) << (8 * i);
    return value;
}
constexpr uint64_t limit = 32 * 1024 * 1024;
}
int validate_guest_program(const uint8_t* bytes, size_t length, uint32_t budget) {
    if (!bytes || !length || length > 65536 || !budget || budget > 1000000) return -EINVAL;
    bool initialized = false;
    uint32_t instructions = 0;
    for (size_t offset = 0; offset < length;) {
        if (++instructions > budget) return -E2BIG;
        const uint8_t op = bytes[offset++];
        if (op == 0xB8 || op == 0x05) {
            if (length - offset < 4 || (op == 0x05 && !initialized)) return -ENOTSUP;
            initialized = true;
            offset += 4;
        } else if (op == 0x90) {
            // NOP has no architectural side effect.
        } else if (op == 0xF4) {
            return initialized && offset == length ? 0 : -ENOTSUP;
        } else return -ENOTSUP;
    }
    return -ENOTSUP;
}
GuestELF::~GuestELF() { clear(); }
void GuestELF::clear() {
    if (arena) ::munmap(arena, arena_size);
    arena = nullptr;
    arena_size = 0;
    guest_base = guest_entry = 0;
    regions.clear();
}
const uint8_t* GuestELF::translate(uint64_t address, size_t length, uint32_t permissions) const {
    if (!arena || !length || (permissions & ~7U)) return nullptr;
    for (const auto& segment : regions) {
        if ((segment.permissions & permissions) != permissions || address < segment.address) continue;
        const uint64_t offset = address - segment.address;
        if (offset <= segment.memory_size && length <= segment.memory_size - offset)
            return static_cast<const uint8_t*>(arena) + (address - guest_base);
    }
    return nullptr;
}
int GuestELF::load(const uint8_t* file, size_t length, uint32_t budget) {
    clear();
    if (!file || length < 64 || length > limit || !budget || budget > 1000000) return -EINVAL;
    if (std::memcmp(file, "\177ELF\2\1\1", 7) || number(file + 16, 2) != 2 ||
        number(file + 18, 2) != 62 || number(file + 20, 4) != 1 ||
        number(file + 52, 2) != 64 || number(file + 54, 2) != 56 ||
        number(file + 40, 8) || number(file + 60, 2)) return -ENOEXEC;
    const uint64_t table = number(file + 32, 8), count = number(file + 56, 2);
    if (table < 64 || table > length || !count || count > 64 || count > (length - table) / 56) return -ENOEXEC;
    const long host_page = ::sysconf(_SC_PAGESIZE);
    if (host_page <= 0) return -EIO;
    const uint64_t page = static_cast<uint64_t>(host_page);
    const uint64_t entry = number(file + 24, 8);
    uint64_t begin = UINT64_MAX, end = 0;
    size_t code_offset = 0, code_size = 0;
    std::vector<Segment> parsed;
    try {
        for (uint64_t i = 0; i < count; ++i) {
            const uint8_t* ph = file + table + i * 56;
            // Reject dynamic linking, interpreters, TLS and all unhandled metadata.
            if (number(ph, 4) != 1) return -ENOEXEC;
            Segment s {number(ph + 16, 8), number(ph + 8, 8), number(ph + 32, 8),
                       number(ph + 40, 8), static_cast<uint32_t>(number(ph + 4, 4))};
            const uint64_t align = number(ph + 48, 8);
            if (!s.memory_size || s.memory_size > limit || s.file_size > s.memory_size ||
                s.file_offset > length || s.file_size > length - s.file_offset ||
                s.address > UINT64_MAX - s.memory_size ||
                s.address + s.memory_size > UINT64_MAX - (page - 1) ||
                !(s.permissions & 4) || (s.permissions & ~7U) || (s.permissions & 3) == 3 ||
                (align > 1 && ((align & (align - 1)) || s.address % align != s.file_offset % align))) return -ENOEXEC;
            const uint64_t low = s.address / page * page;
            const uint64_t high = (s.address + s.memory_size + page - 1) / page * page;
            // Host pages must not alias two independently protected guest regions.
            for (const auto& old : parsed) {
                const uint64_t old_low = old.address / page * page;
                const uint64_t old_high = (old.address + old.memory_size + page - 1) / page * page;
                if (low < old_high && old_low < high) return -ENOEXEC;
            }
            begin = std::min(begin, low);
            end = std::max(end, high);
            if (end - begin > limit) return -E2BIG;
            if ((s.permissions & 1) && entry >= s.address && entry - s.address < s.file_size) {
                code_offset = static_cast<size_t>(s.file_offset + entry - s.address);
                code_size = static_cast<size_t>(s.file_size - (entry - s.address));
            }
            parsed.push_back(s);
        }
    } catch (const std::bad_alloc&) { return -ENOMEM; }
    if (!code_size) return -ENOEXEC;
    const int admitted = validate_guest_program(file + code_offset, code_size, budget);
    if (admitted) return admitted;
    arena_size = static_cast<size_t>(end - begin);
    void* mapped = ::mmap(nullptr, arena_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapped == MAP_FAILED) { arena_size = 0; return -errno; }
    arena = mapped;
    guest_base = begin;
    guest_entry = entry;
    regions = std::move(parsed);
    for (const auto& s : regions) {
        const size_t low = static_cast<size_t>(s.address / page * page - begin);
        const size_t bytes = static_cast<size_t>((s.address + s.memory_size + page - 1) / page * page - begin - low);
        auto* destination = static_cast<uint8_t*>(arena) + low;
        if (::mprotect(destination, bytes, PROT_READ | PROT_WRITE) != 0) {
            const int error = -errno; clear(); return error;
        }
        std::memcpy(static_cast<uint8_t*>(arena) + (s.address - begin), file + s.file_offset, s.file_size);
        const int protection = PROT_READ | ((s.permissions & 2) ? PROT_WRITE : 0);
        if (::mprotect(destination, bytes, protection) != 0) {
            const int error = -errno; clear(); return error;
        }
    }
    return 0;
}
}
