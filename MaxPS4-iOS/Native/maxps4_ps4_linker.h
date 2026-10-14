#pragma once
#include "maxps4_guest_elf.h"
#include "maxps4_ps4_libkernel.h"
#include <string>
#include <vector>

namespace maxps4 {
// Initial, deliberately bounded ELF64/Orbis linker. Standard DT_* tables only;
// SCE offset tables, external PRX graphs and TLS are rejected explicitly.
// All addresses written into guest memory are logical guest addresses.
class PS4DynamicLinker final {
public:
    struct Import { uint64_t slot, thunk; PS4KernelService service; };
    int load(GuestELF& image, const uint8_t* file, size_t length, uint32_t budget);
    // Stages logical GOT calls for the separate M1 admission pass. This does
    // not authorize CPU dispatch; callers must prove code and map the image.
    int load_m1(GuestELF& image, const uint8_t* file, size_t length, uint32_t budget);
    void reset() { imports_.clear(); stack_ = 0; diagnostic_.clear(); }
    const std::vector<Import>& imports() const { return imports_; }
    const std::string& diagnostic() const { return diagnostic_; }
    uint64_t stack_top() const { return stack_ + stack_size; }
    static constexpr size_t stack_size = 0x4000;
    // Internal FEX callback only. No pointer crosses the application ABI.
    bool gate(const GuestELF& image, uint64_t cpu_rip, uint32_t token,
              PS4KernelService& service) const;
private:
    int load_impl(GuestELF&, const uint8_t*, size_t, uint32_t, bool m1);
    std::vector<Import> imports_;
    uint64_t stack_ = 0;
    std::string diagnostic_;
};
}
