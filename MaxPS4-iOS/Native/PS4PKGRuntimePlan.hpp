#pragma once
#include "PS4PKGInventory.hpp"
#include "PS4ExecutableImage.hpp"
#include <algorithm>

namespace maxps4 {
// A display-only package reference never grants access to executable bytes.
// A future provider must supply legally accessible standalone plaintext files.
// This adapter stages with the existing Orbis ELF/PRX loader; it never executes,
// decrypts, resolves NIDs, relocates, or touches host executable memory.
class PS4PKGRuntimePlan {
public:
    enum class Kind { executable, module, metadata };
    struct Reference { std::string path; Kind kind; PS4PKGInventory::Access descriptorAccess; };
    std::vector<Reference> references;
    static constexpr size_t maxDependencies = 128;
    void inventory(const PS4PKGInventory& pkg, const std::vector<std::string>& names) {
        references.clear();
        if (names.size() != pkg.entries.size()) return;
        for (size_t i = 0; i < names.size(); ++i) {
            std::string name = names[i];
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
                return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : static_cast<char>(c);
            });
            const auto slash = name.find_last_of('/');
            const auto base = name.substr(slash == std::string::npos ? 0 : slash + 1);
            if (base == "eboot.bin") references.push_back({names[i], Kind::executable, pkg.entries[i].access});
            else if (ends(name, ".prx") || ends(name, ".sprx")) references.push_back({names[i], Kind::module, pkg.entries[i].access});
            else if (base == "param.sfo") references.push_back({names[i], Kind::metadata, pkg.entries[i].access});
        }
    }
    struct PreparedModule {
        std::string logicalPath;
        PS4ExecutableImage image;
        // Explicit dependency requests from a future validated Orbis import reader.
        // No dependencies are inferred from arbitrary strings or PKG descriptors.
        std::vector<std::string> dependencies;
    };
    static bool prepareStandalone(const std::string& logicalPath, const uint8_t* bytes, size_t size,
                                  const std::vector<std::string>& dependencies, PreparedModule& output,
                                  std::string& diagnostic) {
        output = PreparedModule{}; diagnostic.clear();
        if (!safePath(logicalPath) || dependencies.size() > maxDependencies) {
            diagnostic = "Module : chemin relatif invalide ou plus de 128 dépendances"; return false;
        }
        std::vector<std::string> seen;
        for (const auto& dependency : dependencies) {
            if (!safePath(dependency) || dependency == logicalPath ||
                std::find(seen.begin(), seen.end(), dependency) != seen.end()) {
                diagnostic = "Module : dépendance invalide, réflexive ou dupliquée"; return false;
            }
            seen.push_back(dependency);
        }
        // Require Orbis ELF, not SELF or CNT. No protected container processing.
        if (!bytes || size < 4 || std::memcmp(bytes, "\177ELF", 4)) {
            diagnostic = "Module : ELF Orbis autonome non chiffré requis ; PKG/SELF refusé"; return false;
        }
        PreparedModule pending;
        const int result = pending.image.load(bytes, size);
        if (result) {
            diagnostic = "Module : rejet du chargeur Orbis ELF/PRX (code " + std::to_string(result) + ")";
            return false;
        }
        pending.logicalPath = logicalPath;
        pending.dependencies = dependencies;
        output = std::move(pending);
        diagnostic = "Segments préparés en mémoire ; dépendances non résolues, aucune exécution";
        return true;
    }
private:
    static bool ends(const std::string& text, const std::string& suffix) {
        return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
    }
    static bool safePath(const std::string& path) {
        if (path.empty() || path.size() > 255 || path.front() == '/' || path.find('\\') != std::string::npos ||
            path.find(':') != std::string::npos) return false;
        size_t start = 0;
        while (start < path.size()) {
            const size_t end = path.find('/', start);
            const auto component = path.substr(start, end == std::string::npos ? end : end - start);
            if (component.empty() || component == "." || component == "..") return false;
            for (unsigned char c : component) if (c < 0x20 || c > 0x7e) return false;
            if (end == std::string::npos) return true;
            start = end + 1;
        }
        return false;
    }
};
}
