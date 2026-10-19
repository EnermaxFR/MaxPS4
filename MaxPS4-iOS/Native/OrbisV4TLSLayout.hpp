#pragma once

// TLS layout planning only. Does not define Orbis TCB/DTV ABI or enable execution.
#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <limits>
#include <utility>
#include <vector>

namespace maxps4 {

struct OrbisV4TLSModule {
    uint32_t id = 0;
    size_t memory_size = 0;
    size_t alignment = 0;
    std::vector<uint8_t> initial;
};

struct OrbisV4TLSPlacement {
    uint32_t id = 0;
    size_t offset = 0;
    size_t size = 0;
};

class OrbisV4TLSLayout {
public:
    static constexpr size_t max_total_size = 1024 * 1024;
    static constexpr size_t max_modules = 128;
    static constexpr size_t max_alignment = 16384;

    // Transactional: outputs are unchanged on failure.
    static int plan(const std::vector<OrbisV4TLSModule>& modules,
                    std::vector<OrbisV4TLSPlacement>& placements,
                    size_t& total_size) {
        if (modules.empty() || modules.size() > max_modules) return -EINVAL;
        std::vector<OrbisV4TLSPlacement> candidate;
        candidate.reserve(modules.size());
        size_t cursor = 0;
        for (const auto& module : modules) {
            if (!module.id || !module.memory_size ||
                !module.alignment || module.alignment > max_alignment ||
                (module.alignment & (module.alignment - 1)) ||
                module.initial.size() > module.memory_size ||
                module.memory_size > max_total_size) return -EINVAL;
            for (const auto& entry : candidate)
                if (entry.id == module.id) return -EINVAL;
            const size_t mask = module.alignment - 1;
            if (cursor > max_total_size - mask) return -EOVERFLOW;
            const size_t aligned = (cursor + mask) & ~mask;
            if (aligned > max_total_size ||
                module.memory_size > max_total_size - aligned) return -EOVERFLOW;
            candidate.push_back({module.id, aligned, module.memory_size});
            cursor = aligned + module.memory_size;
        }
        placements = std::move(candidate);
        total_size = cursor;
        return 0;
    }

    static int instantiate(const std::vector<OrbisV4TLSModule>& modules,
                           const std::vector<OrbisV4TLSPlacement>& placements,
                           size_t total_size, std::vector<uint8_t>& output) {
        if (modules.empty() || modules.size() != placements.size() ||
            total_size > max_total_size) return -EINVAL;
        std::vector<OrbisV4TLSPlacement> verified;
        size_t verified_size = 0;
        int status = plan(modules, verified, verified_size);
        if (status) return status;
        if (verified_size != total_size) return -EINVAL;
        for (size_t i = 0; i < placements.size(); ++i)
            if (placements[i].id != verified[i].id ||
                placements[i].offset != verified[i].offset ||
                placements[i].size != verified[i].size) return -EINVAL;
        std::vector<uint8_t> bytes(total_size, 0);
        for (size_t i = 0; i < modules.size(); ++i) {
            const auto& source = modules[i].initial;
            for (size_t j = 0; j < source.size(); ++j)
                bytes[placements[i].offset + j] = source[j];
        }
        output = std::move(bytes);
        return 0;
    }
};

} // namespace maxps4
