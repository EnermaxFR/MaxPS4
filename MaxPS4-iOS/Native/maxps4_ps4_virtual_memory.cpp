#include "maxps4_ps4_virtual_memory.h"
#include <algorithm>
#include <new>

namespace maxps4 {
using namespace ps4;
Error PS4VirtualMemory::permissions(uint32_t protection, uint8_t& out) {
    // PS4 PROT_READ=1, WRITE=2, EXEC=4, unlike ELF p_flags.
    if (protection & ~7U) return notsup;
    if ((protection & 6U) == 6U) return access; // Enforce guest W^X.
    out = uint8_t(((protection & 1U) << 2) | (protection & 2U) | ((protection & 4U) >> 2));
    return ok;
}
bool PS4VirtualMemory::rounded(uint64_t length, uint64_t& out) {
    if (!length || length > UINT64_MAX - (page_size - 1)) return false;
    out = (length + page_size - 1) / page_size * page_size;
    return true;
}
Error PS4VirtualMemory::map(uint64_t hint, uint64_t length, uint32_t protection,
    uint32_t flags, int32_t descriptor, int64_t offset, uint64_t& result) {
    // Anonymous PRIVATE only. File mappings, shared/fixed mappings and hints
    // need additional semantics; never pretend to implement them.
    if (hint || flags != 0x1002U) return notsup;
    if (descriptor != -1 || offset != 0) return inval;
    uint8_t permission;
    Error e = permissions(protection, permission);
    if (e) return e;
    uint64_t size;
    if (!rounded(length, size)) return inval;
    if (size > budget - used_ || regions_.size() >= 128) return nomem;
    // Find an available logical span, also excluding ELF segments. No host
    // address is exposed and no MAP_FIXED host operation can clobber the app.
    uint64_t candidate = base;
    for (;;) {
        if (candidate > UINT64_MAX - size) return nomem;
        uint64_t next = candidate;
        auto exclude = [&](uint64_t address, uint64_t count) {
            if (count > UINT64_MAX - address) { next = UINT64_MAX; return; }
            const uint64_t end = address + count;
            if (candidate < end && address < candidate + size) {
                if (end > UINT64_MAX - (page_size - 1)) next = UINT64_MAX;
                else next = std::max(next, (end + page_size - 1) / page_size * page_size);
            }
        };
        for (const auto& region : regions_) exclude(region.address, region.bytes.size());
        for (const auto& segment : image_.segments()) exclude(segment.address, segment.memory_size);
        if (next == candidate) break;
        candidate = next;
    }
    try {
        Region pending{candidate, std::vector<uint8_t>(size_t(size), 0),
            std::vector<uint8_t>(size_t(size / page_size), permission)};
        regions_.push_back(std::move(pending));
    } catch (const std::bad_alloc&) { return nomem; }
    used_ += size_t(size); result = candidate;
    return ok;
}
PS4VirtualMemory::Region* PS4VirtualMemory::range(uint64_t address, uint64_t size) {
    for (auto& region : regions_) {
        if (address < region.address) continue;
        const uint64_t relative = address - region.address;
        if (relative >= region.bytes.size() || size > region.bytes.size() - relative) continue;
        const size_t first = size_t(relative / page_size);
        const size_t last = size_t((relative + size - 1) / page_size);
        for (size_t i = first; i <= last; ++i) if (region.pages[i] == 0xff) return nullptr;
        return &region;
    }
    return nullptr;
}
Error PS4VirtualMemory::unmap(uint64_t address, uint64_t length) {
    uint64_t size;
    if (address % page_size || !rounded(length, size)) return inval;
    Region* region = range(address, size);
    if (!region) return nomem;
    const size_t first = size_t((address - region->address) / page_size);
    for (size_t i = first; i < first + size / page_size; ++i) {
        region->pages[i] = 0xff;
        std::fill_n(region->bytes.begin() + i * page_size, page_size, uint8_t(0));
    }
    if (std::all_of(region->pages.begin(), region->pages.end(), [](uint8_t p) { return p == 0xff; })) {
        used_ -= region->bytes.size();
        const size_t index = size_t(region - regions_.data());
        regions_.erase(regions_.begin() + index);
    }
    return ok;
}
Error PS4VirtualMemory::protect(uint64_t address, uint64_t length, uint32_t protection) {
    uint8_t permission;
    Error e = permissions(protection, permission);
    if (e) return e;
    uint64_t size;
    if (address % page_size || !rounded(length, size)) return inval;
    Region* region = range(address, size);
    if (!region) return nomem;
    const size_t first = size_t((address - region->address) / page_size);
    std::fill_n(region->pages.begin() + first, size_t(size / page_size), permission);
    return ok;
}
size_t PS4VirtualMemory::contiguous(uint64_t address, size_t limit, uint32_t permissions) const {
    if (!limit || !permissions || (permissions & ~7U)) return 0;
    for (const auto& region : regions_) {
        if (address < region.address) continue;
        const uint64_t relative = address - region.address;
        if (relative >= region.bytes.size()) continue;
        const size_t first = size_t(relative / page_size);
        size_t last = first;
        while (last < region.pages.size() && region.pages[last] != 0xff &&
               (region.pages[last] & permissions) == permissions) ++last;
        if (last == first) return 0;
        return std::min(limit, size_t(last * page_size - relative));
    }
    return 0;
}
const uint8_t* PS4VirtualMemory::translate(uint64_t address, size_t size, uint32_t permissions) const {
    if (!size || contiguous(address, size, permissions) != size) return nullptr;
    for (const auto& region : regions_)
        if (address >= region.address && address - region.address < region.bytes.size())
            return region.bytes.data() + (address - region.address);
    return nullptr;
}
} // namespace maxps4
