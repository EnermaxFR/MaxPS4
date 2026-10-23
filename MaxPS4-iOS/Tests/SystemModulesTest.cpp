#include "../Native/SystemModules.hpp"
#include "PS4OrbisExecutionFixture.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>

// Inventory of user-supplied system modules: plaintext PRX described,
// encrypted SELF and malformed files reported, other files ignored.
int main() {
    namespace fs = std::filesystem;
    const auto directory = fs::temp_directory_path() / "maxps4-system-modules-test";
    fs::remove_all(directory);
    std::vector<maxps4::SystemModules::Module> modules;
    assert(maxps4::SystemModules::scan(directory.string(), modules) == -ENOENT);
    fs::create_directories(directory);
    auto write = [&](const char* name, const std::vector<uint8_t>& bytes) {
        std::ofstream out(directory / name, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        assert(bool(out));
    };
    std::vector<uint8_t> self(128, 0);
    self[0] = 0x4f; self[1] = 0x15; self[2] = 0x3d; self[3] = 0x1d;
    write("libProvider.SPRX", orbis_execution::make(true));
    write("libEncrypted.prx", self);
    write("broken.prx", std::vector<uint8_t>(128, 0));
    write("tiny.sprx", std::vector<uint8_t>(10, 0));
    write("readme.txt", std::vector<uint8_t>(128, 0));
    fs::create_directories(directory / "nested.prx");
    assert(maxps4::SystemModules::scan(directory.string(), modules) == 0);
    assert(modules.size() == 4);
    using Kind = maxps4::SystemModules::Module::Kind;
    assert(modules[0].file == "broken.prx" && modules[0].kind == Kind::invalid && !modules[0].error.empty());
    assert(modules[1].file == "libEncrypted.prx" && modules[1].kind == Kind::encrypted);
    assert(modules[2].file == "libProvider.SPRX" && modules[2].kind == Kind::plaintext);
    assert(modules[2].type == 0xfe18 && modules[2].exports > 0 && !modules[2].modules.empty());
    assert(modules[3].file == "tiny.sprx" && modules[3].kind == Kind::invalid);
    const auto text = maxps4::SystemModules::report(modules);
    std::fputs(text.c_str(), stdout);
    assert(text.find("en clair : 1 ; chiffrés (SELF) : 1 ; invalides : 2") != std::string::npos);
    assert(text.find("module fourni : " + modules[2].modules[0].name) != std::string::npos);
    assert(text.find("SELF chiffré") != std::string::npos);
    fs::remove_all(directory);
    std::puts("PS4 system module inventory checks passed");
}
