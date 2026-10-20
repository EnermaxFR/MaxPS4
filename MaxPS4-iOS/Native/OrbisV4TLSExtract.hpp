#pragma once
// PT_TLS extraction for validated, plaintext ELF images only.
// This does not implement the Orbis TLS ABI or enable execution.
#include "PS4ExecutableImage.hpp"
#include "OrbisV4TLSLayout.hpp"
#include <cerrno>
#include <cstdint>
#include <vector>

namespace maxps4 {
struct OrbisV4TLSExtract {
    static int fromPlainELF(const PS4ExecutableImage& image,
                            const uint8_t* file, size_t file_size,
                            uint32_t module_id, OrbisV4TLSModule& output) {
        if (!file || file_size < 64 || image.isSelf || !module_id ||
            !image.requiresTLS) return -EINVAL;
        const PS4ExecutableImage::Header* tls = nullptr;
        for (const auto& h : image.headers) {
            if (h.type != 7) continue;
            if (tls) return -ENOTSUP; // ambiguous multiple PT_TLS entries
            tls = &h;
        }
        if (!tls) return -EINVAL;
        if (!tls->memorySize || tls->memorySize > OrbisV4TLSLayout::max_total_size ||
            tls->fileSize > tls->memorySize ||
            !tls->alignment || tls->alignment > OrbisV4TLSLayout::max_alignment ||
            (tls->alignment & (tls->alignment - 1)) ||
            tls->offset > file_size || tls->fileSize > file_size - tls->offset ||
            tls->address % tls->alignment != tls->offset % tls->alignment)
            return -ENOEXEC;
        OrbisV4TLSModule candidate;
        candidate.id = module_id;
        candidate.memory_size = static_cast<size_t>(tls->memorySize);
        candidate.alignment = static_cast<size_t>(tls->alignment);
        candidate.initial.assign(file + tls->offset, file + tls->offset + tls->fileSize);
        output = std::move(candidate);
        return 0;
    }
};
} // namespace maxps4
