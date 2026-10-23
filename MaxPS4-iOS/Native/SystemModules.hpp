#pragma once
// PS4 system modules (libc.prx, libSceFios2.prx, ...) that the user copied,
// decrypted, from their own console. MaxPS4 ships none and decrypts none: an
// encrypted SELF is reported as such and never read further. This is the
// inventory the app shows; linking games against these modules comes later.
#include "OrbisMetadata.hpp"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace maxps4 {
class SystemModules {
public:
    static constexpr size_t fileLimit = 512;
    struct Module {
        std::string file;
        enum class Kind { plaintext, encrypted, invalid } kind = Kind::invalid;
        std::string error;
        uint16_t type = 0;
        std::vector<OrbisMetadata::Identity> modules, libraries; // Exported only.
        size_t exports = 0, imports = 0;
    };
    // .prx/.sprx file names, compared without case: system modules use both.
    static bool candidate(const std::string& name) {
        std::string lower(name);
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        auto suffix = [&](const char* s) {
            const size_t n = std::strlen(s);
            return lower.size() > n && lower.compare(lower.size() - n, n, s) == 0;
        };
        return suffix(".prx") || suffix(".sprx");
    }
    static Module inspect(const std::string& file, const std::vector<uint8_t>& bytes) {
        Module module;
        module.file = file;
        if (bytes.size() >= 4 && bytes[0] == 0x4f && bytes[1] == 0x15 && bytes[2] == 0x3d && bytes[3] == 0x1d) {
            module.kind = Module::Kind::encrypted;
            return module;
        }
        OrbisMetadata metadata;
        if (!metadata.parse(bytes.data(), bytes.size())) {
            module.error = metadata.error;
            return module;
        }
        module.kind = Module::Kind::plaintext;
        module.type = metadata.type;
        for (const auto& m : metadata.modules) if (m.exported) module.modules.push_back(m);
        for (const auto& l : metadata.libraries) if (l.exported) module.libraries.push_back(l);
        for (size_t i = 1; i < metadata.symbols.size(); ++i) {
            const auto& s = metadata.symbols[i];
            if (s.encoded.empty()) continue;
            ++(s.defined ? module.exports : module.imports);
        }
        return module;
    }
    // Regular top-level candidates of `directory` (no symlinks), sorted by name.
    static int scan(const std::string& directory, std::vector<Module>& out) {
        namespace fs = std::filesystem;
        out.clear();
        std::error_code error;
        const auto status = fs::symlink_status(directory, error);
        if (error || !fs::is_directory(status)) return -ENOENT;
        std::vector<fs::path> files;
        for (fs::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
            const auto entry = it->symlink_status(error);
            if (error) break;
            if (fs::is_regular_file(entry) && candidate(it->path().filename().string())) files.push_back(it->path());
            if (files.size() > fileLimit) return -E2BIG;
        }
        if (error) return -EIO;
        std::sort(files.begin(), files.end());
        for (const auto& path : files) {
            std::vector<uint8_t> bytes;
            const auto size = fs::file_size(path, error);
            Module module;
            module.file = path.filename().string();
            if (error || size < 64 || size > OrbisMetadata::fileLimit) {
                module.error = "taille hors limites (64 octets à 64 Mio)";
                out.push_back(std::move(module));
                error.clear();
                continue;
            }
            auto closeFile = [](std::FILE* f) { std::fclose(f); };
            std::unique_ptr<std::FILE, decltype(closeFile)> file(std::fopen(path.string().c_str(), "rb"), closeFile);
            bytes.resize(size_t(size));
            if (!file || std::fread(bytes.data(), 1, bytes.size(), file.get()) != bytes.size()) {
                module.error = "lecture impossible";
                out.push_back(std::move(module));
                continue;
            }
            out.push_back(inspect(module.file, bytes));
        }
        return 0;
    }
    static std::string report(const std::vector<Module>& modules) {
        std::ostringstream out;
        size_t plaintext = 0, encrypted = 0, invalid = 0;
        for (const auto& m : modules)
            ++(m.kind == Module::Kind::plaintext ? plaintext : m.kind == Module::Kind::encrypted ? encrypted : invalid);
        out << "Modules système PS4 — inventaire, aucune exécution\n"
            << "Fichiers .prx/.sprx : " << modules.size() << " ; en clair : " << plaintext
            << " ; chiffrés (SELF) : " << encrypted << " ; invalides : " << invalid << '\n';
        for (const auto& m : modules) {
            out << '\n' << m.file << " : ";
            if (m.kind == Module::Kind::encrypted) {
                out << "SELF chiffré — MaxPS4 ne déchiffre pas ; copier la version déchiffrée depuis votre console\n";
                continue;
            }
            if (m.kind == Module::Kind::invalid) { out << "refusé — " << m.error << '\n'; continue; }
            out << "en clair, " << OrbisMetadata::typeName(m.type) << " ; exports : " << m.exports
                << " ; imports : " << m.imports << '\n';
            for (const auto& id : m.modules)
                out << "  module fourni : " << id.name << " version=" << (id.version >> 8) << '.' << (id.version & 255) << '\n';
            for (const auto& id : m.libraries)
                out << "  bibliothèque exportée : " << id.name << " version=" << id.version << '\n';
        }
        if (modules.empty())
            out << "\nAucun module : importer le dossier des modules système copiés depuis votre propre console.\n";
        out << "\nLes modules restent sur cet appareil. Le chargement des jeux avec ces modules n'est pas encore raccordé.\n";
        return out.str();
    }
};
}
