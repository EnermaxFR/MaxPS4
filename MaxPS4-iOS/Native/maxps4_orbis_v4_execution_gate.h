#pragma once
#include "maxps4_orbis_v4_memory_bridge.h"
#include <cerrno>
#include <cstddef>
#include <cstdint>

namespace maxps4 {
// Explicit experimental preflight for a future FEX direct-memory dispatcher.
// This does not launch FEX or change the V3 execution path.
class OrbisV4ExecutionGate final {
public:
    explicit OrbisV4ExecutionGate(GuestAddressSpace& memory) : bridge_(memory) {}
    int preflight(uint64_t entry, uint64_t stack, size_t stack_bytes,
                  const uint8_t*& executable, const uint8_t*& writable_stack) const {
        executable = nullptr;
        writable_stack = nullptr;
        if (!entry || !stack || !stack_bytes || stack_bytes > GuestAddressSpace::budget)
            return -EINVAL;
        if (stack > UINT64_MAX - stack_bytes) return -EOVERFLOW;
        const uint8_t* code = nullptr;
        const uint8_t* data = nullptr;
        int status = bridge_.admit_fex_direct(entry,1,5,code);
        if (status) return status;
        status = bridge_.admit_fex_direct(stack,stack_bytes,6,data);
        if (status) return status;
        executable = code;
        writable_stack = data;
        return 0;
    }
private:
    OrbisV4MemoryBridge bridge_;
};
}
