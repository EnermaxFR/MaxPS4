#include "../Native/PS4SFO.hpp"
#include "../Native/PS4PKGRuntimePlan.hpp"
#include "../Native/PS4PKGBridge.h"
#include "PS4ExecutableFixture.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <filesystem>

using PKG = maxps4::PS4PKGInventory;
static void put(std::vector<uint8_t>& b, size_t at, uint64_t value, size_t width, bool little = false) {
    for (size_t i = 0; i < width; ++i) b.at(at + (little ? i : width - 1 - i)) = static_cast<uint8_t>(value >> (8 * i));
}
static std::vector<uint8_t> sfoFixture() {
    const std::vector<std::pair<std::string, std::string>> fields = {
        {"TITLE", "Diagnostic synthétique"}, {"TITLE_ID", "CUSA12345"},
        {"CONTENT_ID", "EP0000-CUSA12345_00-SYNTHETIC0000000"},
        {"APP_VER", "01.23"}, {"VERSION", "01.00"}, {"CATEGORY", "gd"}
    };
    const size_t keys = 20 + fields.size() * 16;
    std::vector<uint8_t> b(keys, 0);
    std::memcpy(b.data(), "\0PSF", 4);
    put(b, 4, 0x101, 4, true); put(b, 8, keys, 4, true); put(b, 16, fields.size(), 4, true);
    for (size_t i = 0; i < fields.size(); ++i) {
        put(b, 20 + i * 16, b.size() - keys, 2, true);
        b.insert(b.end(), fields[i].first.begin(), fields[i].first.end()); b.push_back(0);
    }
    while (b.size() % 4) b.push_back(0);
    const size_t data = b.size(); put(b, 12, data, 4, true);
    for (size_t i = 0; i < fields.size(); ++i) {
        const size_t at = 20 + i * 16, length = fields[i].second.size() + 1;
        put(b, at + 2, 0x204, 2, true); put(b, at + 4, length, 4, true);
        put(b, at + 8, length, 4, true); put(b, at + 12, b.size() - data, 4, true);
        b.insert(b.end(), fields[i].second.begin(), fields[i].second.end()); b.push_back(0);
    }
    return b;
}
static std::vector<uint8_t> pkgFixture() {
    auto sfo = sfoFixture();
    const std::string names("\0sce_sys/param.sfo\0eboot.bin\0sce_module/libdemo.sprx\0", 53);
    std::vector<uint8_t> b(0x800 + sfo.size() + 32, 0);
    std::memcpy(b.data(), "\177CNT", 4);
    std::memcpy(b.data() + 0x40, "EP0000-CUSA12345_00-SYNTHETIC0000000", 36);
    put(b, 4, 0x01000000, 4); put(b, 0x74, 0x1a, 4);
    put(b, 0x10, 4, 4); put(b, 0x16, 4, 2); put(b, 0x18, 0x5a0, 4);
    put(b, 0x20, 0x700, 8); put(b, 0x28, b.size() - 0x700, 8); put(b, 0x430, b.size(), 8);
    const uint32_t ids[] = {0x200, 0x1000, 0x2001, 0x2002};
    const size_t offsets[] = {0x700, 0x800, 0x800 + sfo.size(), 0x810 + sfo.size()};
    const size_t sizes[] = {names.size(), sfo.size(), 16, 16};
    const size_t nameOffsets[] = {0, 1, 19, 29};
    for (size_t i = 0; i < 4; ++i) {
        const size_t at = 0x5a0 + i * 32;
        put(b, at, ids[i], 4); put(b, at + 4, nameOffsets[i], 4);
        put(b, at + 16, offsets[i], 4); put(b, at + 20, sizes[i], 4);
    }
    std::copy(names.begin(), names.end(), b.begin() + 0x700);
    std::copy(sfo.begin(), sfo.end(), b.begin() + 0x800);
    return b;
}
static void write(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    FILE* file = std::fopen(path.c_str(), "wb"); assert(file);
    assert(std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size()); assert(!std::fclose(file));
}
int main(int argc, char** argv) {
    auto bytes = pkgFixture(); const auto original = bytes;
    PKG pkg;
    auto read = [&](uint64_t at, uint8_t* target, size_t size) {
        if (!PKG::range(at, size, bytes.size())) return false;
        std::memcpy(target, bytes.data() + at, size); return true;
    };
    assert(pkg.parse(bytes.size(), read));
    assert(pkg.contentID == "EP0000-CUSA12345_00-SYNTHETIC0000000" && pkg.titleID == "CUSA12345");
    assert(pkg.contentType == 0x1a && std::string(PKG::packageType(0x1a, 0)) == "application");
    std::vector<std::string> names; std::string why;
    assert(pkg.resolveNames(read, names, why));
    maxps4::PS4PKGRuntimePlan plan; plan.inventory(pkg, names);
    assert(plan.references.size() == 3 && plan.references[1].path == "eboot.bin");
    assert(plan.references[1].descriptorAccess == PKG::Access::unknown);
    std::vector<uint8_t> output;
    assert(!pkg.extract(2, read, output, why)); // Name alone grants no executable access.
    auto sfo = sfoFixture(); const auto originalSFO = sfo;
    maxps4::PS4SFO metadata;
    assert(metadata.parse(sfo) && metadata.strings.at("APP_VER") == "01.23");
    assert(metadata.strings.at("TITLE") == "Diagnostic synthétique");
    // Every truncation, overflowing offsets, overlap, unsupported formats, duplicates.
    for (size_t n = 0; n < originalSFO.size(); ++n) {
        sfo.assign(originalSFO.begin(), originalSFO.begin() + n);
        assert(!metadata.parse(sfo) && metadata.strings.empty());
    }
    for (const auto& change : std::vector<std::pair<size_t, uint32_t>>{
             {4, 2}, {8, UINT32_MAX}, {12, UINT32_MAX}, {16, UINT32_MAX},
             {24, UINT32_MAX}, {28, 0}, {32, UINT32_MAX}, {48, 0}}) {
        sfo = originalSFO; put(sfo, change.first, change.second, 4, true);
        assert(!metadata.parse(sfo) && metadata.strings.empty() && !metadata.error.empty());
    }
    sfo = originalSFO; put(sfo, 36, 0, 2, true); // Duplicate TITLE key.
    assert(!metadata.parse(sfo));
    sfo = originalSFO; sfo.back() = 'X'; assert(!metadata.parse(sfo));
    const size_t data = size_t(originalSFO[12]) | (size_t(originalSFO[13]) << 8);
    for (uint8_t invalid : {uint8_t(0), uint8_t(0x1b), uint8_t(0xff), uint8_t(0xc0)}) {
        sfo = originalSFO; sfo[data] = invalid; assert(!metadata.parse(sfo));
    }
    // Deterministic mutation sweep, checked under ASan/UBSan in CI.
    uint32_t seed = 12345;
    for (size_t i = 0; i < 2000; ++i) {
        sfo = originalSFO; seed = seed * 1664525U + 1013904223U;
        sfo[seed % sfo.size()] ^= static_cast<uint8_t>(seed >> 24);
        if (!metadata.parse(sfo)) assert(metadata.strings.empty());
    }
    bytes[0x40] = 0x1b; assert(pkg.parse(bytes.size(), read) && pkg.contentID.empty() && pkg.titleID.empty());
    bytes = original;
    for (const auto* magic : {"\177ELF", "\177PKG"}) {
        std::memcpy(bytes.data(), magic, 4);
        assert(!pkg.parse(bytes.size(), read) && pkg.error.find("Format") != std::string::npos);
    }
    bytes = original;
    maxps4::PS4PKGRuntimePlan::PreparedModule module;
    // Contract has no package reader and accepts only separate Orbis ELF bytes.
    assert(!plan.prepareStandalone("eboot.bin", bytes.data(), bytes.size(), {}, module, why));
    auto elf = ps4ELF();
    assert(plan.prepareStandalone("eboot.bin", elf.data(), elf.size(), {"sce_module/libdemo.sprx"}, module, why));
    assert(module.dependencies.size() == 1 && !module.image.segments.empty());
    for (const auto* path : {"../eboot.bin", "/eboot.bin", "sce_module//a.prx", "a/", "a\\b", "a:b"})
        assert(!plan.prepareStandalone(path, elf.data(), elf.size(), {}, module, why) && module.image.segments.empty());
    assert(!plan.prepareStandalone("eboot.bin", elf.data(), elf.size(), {"a.prx", "a.prx"}, module, why));
    assert(!plan.prepareStandalone("eboot.bin", elf.data(), elf.size(), {"eboot.bin"}, module, why));
    assert(!plan.prepareStandalone("eboot.bin", elf.data(), elf.size(), std::vector<std::string>(129, "x"), module, why));
    const auto folder = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::temp_directory_path() / "maxps4-synthetic-pkg";
    std::filesystem::create_directories(folder);
    const auto path = folder / "synthetic-metadata.pkg";
    write(path, bytes);
    std::vector<char> report(16384);
    assert(!maxps4_pkg_inventory_report(path.c_str(), report.data(), report.size()));
    const std::string text(report.data());
    assert(text.find("APP_VER (param.sfo) : 01.23") != std::string::npos);
    assert(text.find("TITLE_ID (param.sfo) : CUSA12345") != std::string::npos);
    assert(text.find("Référence : sce_module/libdemo.sprx") != std::string::npos);
    assert(text.find("référence seule, données exécutables non admises") != std::string::npos);
    // Library title info: exact key=value lines, bounded and NUL-terminated.
    std::vector<char> info(4096, 'x');
    assert(maxps4_pkg_read_title_info(path.c_str(), info.data(), info.size()) == 0);
    assert(std::string(info.data()) == "TITLE=Diagnostic synthétique\nTITLE_ID=CUSA12345\n"
        "CONTENT_ID=EP0000-CUSA12345_00-SYNTHETIC0000000\nAPP_VER=01.23\nVERSION=01.00\nCATEGORY=gd\n");
    assert(maxps4_pkg_read_title_info(path.c_str(), info.data(), 8) == -ENOSPC && std::string(info.data()) == "TITLE=D");
    assert(maxps4_pkg_read_title_info(path.c_str(), nullptr, 0) == -EINVAL);
    assert(maxps4_pkg_read_title_info((folder / "missing.pkg").c_str(), info.data(), info.size()) == -ENOENT && !info[0]);
    put(bytes, 0x5c8, 0x80000000, 4); write(path, bytes); // Encrypted SFO descriptor: never read.
    assert(maxps4_pkg_read_title_info(path.c_str(), info.data(), info.size()) == -ENOENT && !info[0]);
    bytes = original;
    put(bytes, 0x5c8, 0x80000000, 4); write(folder / "synthetic-encrypted-sfo.pkg", bytes);
    write(path, bytes); assert(!maxps4_pkg_inventory_report(path.c_str(), report.data(), report.size()));
    assert(std::string(report.data()).find("APP_VER (param.sfo) : indisponible") != std::string::npos);
    bytes = original; put(bytes, 0x410, UINT64_MAX, 8); write(folder / "synthetic-bad-offset.pkg", bytes);
    write(path, bytes); assert(!maxps4_pkg_inventory_report(path.c_str(), report.data(), report.size()));
    assert(std::string(report.data()).find("Image PFS hors limites") != std::string::npos);
    write(folder / "synthetic-truncated.pkg", std::vector<uint8_t>(original.begin(), original.begin() + 32));
    write(path, original); // Restore the downloadable valid fixture.
    if (argc == 1) std::filesystem::remove_all(folder);
    std::puts("PASS PKG metadata, SFO bounds/UTF-8, protected refusal, runtime preparation and synthetic iPhone fixtures");
}
