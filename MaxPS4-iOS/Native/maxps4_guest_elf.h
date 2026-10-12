#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace maxps4 {
// A deliberately bounded position-independent ELF demonstration, not a PS4 ABI.
// Guest addresses are translated into one owned host arena; x86 code is data
// for the ARM64 translator and never needs host PROT_EXEC.
class GuestELF final {
public:
    struct Segment {
        uint64_t address, file_offset, file_size, memory_size;
        uint32_t permissions;
    };
    GuestELF() = default;
    ~GuestELF();
    GuestELF(const GuestELF&) = delete;
    GuestELF& operator=(const GuestELF&) = delete;
    int load(const uint8_t* file, size_t length, uint32_t budget);
    void clear();
    const uint8_t* translate(uint64_t address, size_t length, uint32_t permissions) const;
    uint64_t entry() const { return guest_entry; }
    const uint8_t* entry_pointer() const { return translate(guest_entry, 1, 5); }
    const std::vector<Segment>& segments() const { return regions; }
private:
    std::vector<Segment> regions;
    void* arena = nullptr;
    size_t arena_size = 0;
    uint64_t guest_base = 0, guest_entry = 0;
};
// Admission guarantees a finite sequence: MOV EAX,imm32; ADD EAX,imm32;
// NOP; terminal HLT. The ELF loader also admits C7 05 disp32 imm32 stores
// after proving each target lies wholly in a guest RW/non-X segment. This
// standalone validator has no segment proof and still rejects memory operands.
// No branches, stack operations, arbitrary memory accesses or syscalls.
int validate_guest_program(const uint8_t* bytes, size_t length, uint32_t budget);
}

