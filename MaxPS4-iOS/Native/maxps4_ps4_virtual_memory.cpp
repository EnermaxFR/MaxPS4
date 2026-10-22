#include "maxps4_ps4_virtual_memory.h"
#include <algorithm>
#include <new>
#include <sys/mman.h>

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
bool PS4VirtualMemory::hosted(const Region& region) const {
    uint64_t window = 0, bytes = 0;
    return region.bytes.empty() && image_.heap(window, bytes) &&
           region.address >= window && region.size <= bytes &&
           region.address - window <= bytes - region.size;
}
bool PS4VirtualMemory::host_protect(uint64_t address, uint64_t length, uint8_t permission) {
    // Executable pages stay host data (read-only): FEX does not run them yet.
    // 0xff (unmapped) and 0 become PROT_NONE; an unmapped page is also zeroed
    // by replacing it inside the window, which the image owns.
    auto* pointer = reinterpret_cast<void*>(address);
    if (permission == 0xff)
        return ::mmap(pointer, length, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == pointer;
    const int protection = ((permission & 5U) ? PROT_READ : 0) |
                           ((permission & 2U) ? PROT_READ | PROT_WRITE : 0);
    return ::mprotect(pointer, length, protection) == 0;
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
    // General images: a span of the image's mmap window (host = guest). Else
    // an available logical span, also excluding ELF segments. No MAP_FIXED
    // host operation can clobber the app: the window belongs to the image.
    uint64_t window = 0, window_bytes = 0;
    const bool host = image_.heap(window, window_bytes);
    uint64_t candidate = host ? window : base;
    for (;;) {
        if (candidate > UINT64_MAX - size) return nomem;
        if (host && candidate + size > window + window_bytes) return nomem;
        uint64_t next = candidate;
        auto exclude = [&](uint64_t address, uint64_t count) {
            if (count > UINT64_MAX - address) { next = UINT64_MAX; return; }
            const uint64_t end = address + count;
            if (candidate < end && address < candidate + size) {
                if (end > UINT64_MAX - (page_size - 1)) next = UINT64_MAX;
                else next = std::max(next, (end + page_size - 1) / page_size * page_size);
            }
        };
        for (const auto& region : regions_) exclude(region.address, region.size);
        if (!host) for (const auto& segment : image_.segments()) exclude(segment.address, segment.memory_size);
        if (next == candidate) break;
        candidate = next;
    }
    try {
        Region pending{candidate, size, nullptr, {}, std::vector<uint8_t>(size_t(size / page_size), permission)};
        if (host) {
            regions_.reserve(regions_.size() + 1);
            if (!host_protect(candidate, size, permission)) return nomem;
            pending.data = reinterpret_cast<uint8_t*>(candidate);
        } else {
            pending.bytes.assign(size_t(size), 0);
            pending.data = pending.bytes.data();
        }
        regions_.push_back(std::move(pending));
    } catch (const std::bad_alloc&) { return nomem; }
    used_ += size_t(size); result = candidate;
    return ok;
}
PS4VirtualMemory::Region* PS4VirtualMemory::range(uint64_t address, uint64_t size) {
    for (auto& region : regions_) {
        if (address < region.address) continue;
        const uint64_t relative = address - region.address;
        if (relative >= region.size || size > region.size - relative) continue;
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
    if (hosted(*region) && !host_protect(address, size, 0xff)) return nomem;
    const size_t first = size_t((address - region->address) / page_size);
    for (size_t i = first; i < first + size / page_size; ++i) {
        region->pages[i] = 0xff;
        if (!region->bytes.empty()) std::fill_n(region->bytes.begin() + i * page_size, page_size, uint8_t(0));
    }
    if (std::all_of(region->pages.begin(), region->pages.end(), [](uint8_t p) { return p == 0xff; })) {
        used_ -= size_t(region->size);
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
    if (hosted(*region) && !host_protect(address, size, permission)) return nomem;
    const size_t first = size_t((address - region->address) / page_size);
    std::fill_n(region->pages.begin() + first, size_t(size / page_size), permission);
    return ok;
}
void PS4VirtualMemory::reset() {
    // Host pages go back to PROT_NONE (and zero) only while they still lie in
    // the current image's window; a cleared image already released them.
    for (const auto& region : regions_)
        if (hosted(region)) host_protect(region.address, region.size, 0xff);
    regions_.clear(); used_ = 0;
}
size_t PS4VirtualMemory::contiguous(uint64_t address, size_t limit, uint32_t permissions) const {
    if (!limit || !permissions || (permissions & ~7U)) return 0;
    for (const auto& region : regions_) {
        if (address < region.address) continue;
        const uint64_t relative = address - region.address;
        if (relative >= region.size) continue;
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
        if (address >= region.address && address - region.address < region.size)
            return region.data + (address - region.address);
    return nullptr;
}
} // namespace maxps4
