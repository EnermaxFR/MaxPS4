#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "maxps4_ps4_guest_memory.h"

namespace maxps4 {
// Minimal libkernel-facing guest pathname validation. No host filesystem access
// is performed here: mounting /app0 and mapping guest descriptors are future work.
enum class PS4KernelPathResult {
    ok,
    invalid_guest_address,
    invalid_path,
    unsupported_mount
};
class PS4KernelPath final {
public:
    static constexpr size_t max_path = 1024;
    static PS4KernelPathResult read(const PS4GuestMemory& memory,
                                    uint64_t guest_address,
                                    char (&destination)[max_path]) {
        char scratch[max_path] = {};
        if (!memory.read_cstring(guest_address, scratch, sizeof(scratch)))
            return PS4KernelPathResult::invalid_guest_address;
        // Only virtual absolute paths are admissible; reject traversal and
        // duplicate separators before any future host filesystem mapping.
        if (scratch[0] != '/') return PS4KernelPathResult::invalid_path;
        const size_t length = std::strlen(scratch);
        if (length == 0 || length == 1) return PS4KernelPathResult::invalid_path;
        for (size_t i = 1; i < length; ++i) {
            if (scratch[i] == '/' && scratch[i - 1] == '/')
                return PS4KernelPathResult::invalid_path;
            const bool component_start = scratch[i - 1] == '/';
            if (component_start && scratch[i] == '.' &&
                (scratch[i + 1] == '/' || scratch[i + 1] == '\0' ||
                 (scratch[i + 1] == '.' &&
                  (scratch[i + 2] == '/' || scratch[i + 2] == '\0'))))
                return PS4KernelPathResult::invalid_path;
        }
        if (std::strncmp(scratch, "/app0/", 6) != 0 || scratch[6] == '\0')
            return PS4KernelPathResult::unsupported_mount;
        std::memcpy(destination, scratch, length + 1);
        return PS4KernelPathResult::ok;
    }
};
} // namespace maxps4
