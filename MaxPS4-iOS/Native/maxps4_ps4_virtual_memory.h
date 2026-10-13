#pragma once
#include "maxps4_guest_elf.h"
#include "maxps4_ps4_abi.h"
#include <vector>

namespace maxps4 {
class PS4DynamicMemory {
public:
    virtual ~PS4DynamicMemory() = default;
    virtual const uint8_t* translate(uint64_t address, size_t length, uint32_t permissions) const = 0;
    virtual size_t contiguous(uint64_t address, size_t limit, uint32_t permissions) const = 0;
};
// Logical PS4 anonymous mappings. x86 executable bytes remain host DATA; this
// is not an iOS executable-code allocator or a FEX address-space backend.
// Owner serializes access and resets this object whenever it restages the ELF.
class PS4VirtualMemory final : public PS4DynamicMemory {
public:
    static constexpr uint64_t page_size = 0x4000;
    static constexpr size_t budget = 32 * 1024 * 1024;
    static constexpr uint64_t base = 0x1000000000ULL;
    explicit PS4VirtualMemory(const GuestELF& image) : image_(image) {}
    ps4::Error map(uint64_t hint, uint64_t length, uint32_t protection,
                   uint32_t flags, int32_t descriptor, int64_t offset, uint64_t& result);
    ps4::Error unmap(uint64_t address, uint64_t length);
    ps4::Error protect(uint64_t address, uint64_t length, uint32_t protection);
    const uint8_t* translate(uint64_t address, size_t length, uint32_t permissions) const override;
    size_t contiguous(uint64_t address, size_t limit, uint32_t permissions) const override;
    void reset() { regions_.clear(); used_ = 0; }
    size_t allocated() const { return used_; }
private:
    struct Region {
        uint64_t address;
        std::vector<uint8_t> bytes;
        // ELF-style permissions R=4/W=2/X=1; 0xff denotes an unmapped page.
        std::vector<uint8_t> pages;
    };
    const GuestELF& image_;
    std::vector<Region> regions_;
    size_t used_ = 0;
    static ps4::Error permissions(uint32_t protection, uint8_t& result);
    static bool rounded(uint64_t length, uint64_t& result);
    Region* range(uint64_t address, uint64_t length);
};
} // namespace maxps4
