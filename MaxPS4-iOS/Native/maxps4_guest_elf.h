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
    // Maps validated plaintext PS4 segments into the same FEX arena, without
    // instruction admission or execution. Guest addresses remain logical;
    // absolute addressing requires an address-space backend before execution.
    int stage_ps4(const uint8_t* file, size_t length);
    // Admission for the bounded rebased FEX path only. Does not remove the
    // general PS4 address-space blocker or authorize arbitrary instructions.
    int admit_staged_cpu(uint32_t budget) const;
    uint32_t ps4_blockers() const { return blockers; }
    struct Execution { uint64_t rax = 0, rip = 0; uint32_t instructions = 0; };
    // Executes only loader-admitted instructions, with logical guest RIP and
    // permission-checked memory accesses. No executable host allocation.
    int interpret(uint32_t budget, Execution& output);
    void clear();
    const uint8_t* translate(uint64_t address, size_t length, uint32_t permissions) const;
    // Staging-only logical-to-host translation; segment permissions are enforced.
    const uint8_t* host_pointer(uint64_t address, size_t bytes, uint32_t permissions) const {
        return translate(address, bytes, permissions);
    }
    uint64_t entry() const { return guest_entry; }
    const uint8_t* entry_pointer() const { return translate(guest_entry, 1, 5); }
    const std::vector<Segment>& segments() const { return regions; }
private:
    int map_segments(std::vector<Segment> parsed, const std::vector<const uint8_t*>& sources,
                     uint64_t entry);
    uint32_t blockers = 0;
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



