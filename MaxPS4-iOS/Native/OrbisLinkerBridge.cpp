#include "OrbisLinkerBridge.h"
#include "OrbisLinker.hpp"
#include <memory>
struct maxps4_orbis_image { maxps4::OrbisLinker::Image image; };
namespace {
void report(char* out, size_t capacity, const std::string& text) {
    if (out && capacity) { const auto n = std::min(capacity - 1, text.size()); std::memcpy(out, text.data(), n); out[n] = 0; }
}
bool bounded(const char* text) {
    if (!text) return false;
    for (size_t i = 0; i < 256; ++i) if (!text[i]) return true;
    return false;
}
}
static int32_t linkModules(const maxps4_orbis_module* modules, size_t count,
    size_t budget, maxps4_orbis_image** output, char* diagnostic, size_t capacity, bool fullReport) {
    report(diagnostic, capacity, "invalid module input");
    if (!output || *output || !modules || !count || count > 128 || (!diagnostic && capacity)) return -EINVAL;
    try {
        std::vector<maxps4::OrbisLinker::Input> inputs;
        for (size_t i = 0; i < count; ++i) {
            const auto& m = modules[i];
            if (!bounded(m.path) || m.dependency_count > 128 || (m.dependency_count && !m.dependencies)) return -EINVAL;
            maxps4::OrbisLinker::Input in;
            in.file.request.logicalPath = m.path; in.file.bytes = m.bytes; in.file.size = m.size; in.bias = m.load_bias;
            for (size_t j = 0; j < m.dependency_count; ++j) {
                if (!bounded(m.dependencies[j])) return -EINVAL;
                in.file.request.dependencies.emplace_back(m.dependencies[j]);
            }
            inputs.push_back(std::move(in));
        }
        auto pending = std::make_unique<maxps4_orbis_image>(); std::string message;
        maxps4::OrbisLinker::Report preparation;
        if (!maxps4::OrbisLinker::link(inputs, {}, pending->image, message, budget, &preparation)) {
            report(diagnostic, capacity, fullReport ? preparation.text() : message); return -ENOEXEC;
        }
        const auto text = fullReport ? preparation.text() : std::string{};
        if (fullReport && (!diagnostic || capacity <= text.size())) {
            report(diagnostic, capacity, "Rapport tronqué ; image non engagée"); return -ENOSPC;
        }
        report(diagnostic, capacity, text); *output = pending.release(); return 0;
    } catch (const std::bad_alloc&) { report(diagnostic, capacity, "allocation failed"); return -ENOMEM; }
}
extern "C" int32_t maxps4_orbis_link(const maxps4_orbis_module* modules, size_t count,
    size_t budget, maxps4_orbis_image** output, char* diagnostic, size_t capacity) {
    return linkModules(modules, count, budget, output, diagnostic, capacity, false);
}
extern "C" int32_t maxps4_orbis_link_with_report(const maxps4_orbis_module* modules, size_t count,
    size_t budget, maxps4_orbis_image** output, char* diagnostic, size_t capacity) {
    return linkModules(modules, count, budget, output, diagnostic, capacity, true);
}
extern "C" void maxps4_orbis_close(maxps4_orbis_image* image) { delete image; }
extern "C" size_t maxps4_orbis_mapping_count(const maxps4_orbis_image* image) {
    size_t count = 0; if (image) for (const auto& m : image->image.modules) count += m.image.segments.size(); return count;
}
extern "C" int32_t maxps4_orbis_get_mapping(const maxps4_orbis_image* image, size_t index, maxps4_orbis_mapping* output) {
    if (!image || !output) return -EINVAL;
    for (const auto& m : image->image.modules) for (const auto& s : m.image.segments) {
        if (!index) { *output = {m.bias + s.header.address, s.header.memorySize, s.header.flags}; return 0; }
        --index;
    }
    return -ERANGE;
}
extern "C" int32_t maxps4_orbis_entry(const maxps4_orbis_image* image, const char* path, uint64_t* output) {
    if (!image || !output || !bounded(path)) return -EINVAL;
    for (const auto& m : image->image.modules) if (m.path == path) { *output = m.entry; return 0; }
    return -ENOENT;
}
extern "C" int32_t maxps4_orbis_read(const maxps4_orbis_image* image, uint64_t address, uint8_t* output, size_t size) {
    if (!image || !output || !size) return -EINVAL;
    const auto* p = image->image.translate(address, size, 4); if (!p) return -EFAULT;
    std::memcpy(output, p, size); return 0;
}
