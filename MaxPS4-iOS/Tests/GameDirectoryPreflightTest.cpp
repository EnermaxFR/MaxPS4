#include "../Native/OrbisAnalysisBridge.h"
#include "OrbisLinkerFixture.h"
#include "OrbisFixture.h"
#include <cassert>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
static void write(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream file(path,std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    assert(file.good());
}
static std::vector<uint8_t> read(const fs::path& path) {
    std::ifstream file(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
int main() {
    char temp[] = "/tmp/maxps4-directory-XXXXXX";
    assert(mkdtemp(temp));
    const fs::path root(temp);
    struct Cleanup { fs::path path; ~Cleanup() { fs::remove_all(path); } } cleanup{root};
    auto inspect = [&](const fs::path& path, int expected = 0) {
        std::vector<char> report(2*1024*1024);
        assert(maxps4_game_directory_report(path.c_str(),report.data(),report.size())==expected);
        return std::string(report.data());
    };
    auto text = inspect(root);
    assert(text.find("Premier blocage du précontrôle : eboot.bin manquant")!=std::string::npos);
    assert(text.find("maxps4.json absent")!=std::string::npos);
    // These fabricated fixtures are not game data and never enter a CPU.
    const auto app = orbis_fixture::make(false), provider = orbis_fixture::make(true);
    write(root/"eboot.bin",app);
    fs::create_directories(root/"sce_module");
    fs::create_directories(root/"sce_sys");
    write(root/"sce_module/provider.prx",provider);
    write(root/"sce_sys/param.sfo",{0,1,2,3});
    write(root/"assets.bin",{0,1,2,3,4});
    text = inspect(root);
    assert(text == inspect(root));
    assert(text.find(temp)==std::string::npos); // No absolute device path in export.
    assert(text.find("Fichier assets.bin ; octets=5")!=std::string::npos);
    assert(text.find("=== eboot.bin ===") < text.find("=== sce_module/provider.prx ==="));
    assert(text.find("NID=AAAAAAAAAAA")!=std::string::npos);
    assert(text.find("fournisseurs PRX déclarés=1 ; export candidat, non lié")!=std::string::npos);
    assert(text.find("RELA type=7")!=std::string::npos);
    assert(text.find("TLS : aucun PT_TLS déclaré")!=std::string::npos);
    assert(text.find("non tenté par le précontrôle")!=std::string::npos);
    assert(read(root/"eboot.bin")==app && read(root/"sce_module/provider.prx")==provider);
    // Enumeration order must not affect the export.
    fs::remove(root/"eboot.bin"); write(root/"eboot.bin",app);
    assert(inspect(root)==text);
    fs::remove(root/"sce_module/provider.prx");
    assert(inspect(root).find("fournisseurs PRX déclarés=0 ; service/import absent")!=std::string::npos);
    // SELF/encrypted content is diagnosed, never decrypted or dispatched.
    auto self = app; orbis_fixture::put(self,0,0x1d3d154f,4); write(root/"eboot.bin",self);
    assert(inspect(root).find("Premier blocage du précontrôle : eboot.bin: Conteneur SELF non pris en charge")!=std::string::npos);
    write(root/"encrypted.pkg",{0x7f,'C','N','T',0,0,0,0});
    assert(inspect(root).find("encrypted.pkg: contenu non accessible")!=std::string::npos);
    fs::remove(root/"encrypted.pkg");
    // Inventory includes unreadable ELF and explicit TLS rather than crashing.
    write(root/"eboot.bin",{1});
    assert(inspect(root).find("Taille ELF invalide")!=std::string::npos);
    const auto tls = orbis_test::fixture(false,true); write(root/"eboot.bin",tls);
    assert(inspect(root).find("TLS Orbis par thread non implémenté")!=std::string::npos);
    write(root/"eboot.bin",app);
    // Do not follow a symlink to content outside or inside the selected root.
    fs::create_symlink("/etc/passwd",root/"external.prx");
    assert(inspect(root).find("external.prx: lien ou fichier spécial refusé")!=std::string::npos);
    fs::create_directory_symlink(root/"sce_module",root/"linked-directory");
    assert(inspect(root).find("linked-directory: lien ou fichier spécial refusé")!=std::string::npos);
    assert(mkfifo((root/"blocked.prx").c_str(),0600)==0);
    assert(inspect(root).find("blocked.prx: lien ou fichier spécial refusé")!=std::string::npos);
    const fs::path rootLink = root/"root-link";
    fs::create_directory_symlink(root,rootLink);
    assert(inspect(rootLink,-EACCES).find("lien symbolique refusé")!=std::string::npos);
    write(root/"fake\nImport succeeded.prx",{1});
    assert(inspect(root).find("fake\\x0aImport succeeded.prx")!=std::string::npos);
    char tiny[8];
    assert(maxps4_game_directory_report(root.c_str(),tiny,sizeof(tiny))==-ENOSPC && tiny[7]==0);
    assert(maxps4_game_directory_report(nullptr,tiny,sizeof(tiny))==-ENOSPC);
    assert(maxps4_game_directory_report(root.c_str(),nullptr,0)==-EINVAL);
    assert(inspect(root/"missing",-EACCES).find("Rejet")!=std::string::npos);
    // Oversized input is diagnosed before allocation, cumulative bytes bounded.
    fs::resize_file(root/"eboot.bin",64*1024*1024+1);
    assert(inspect(root).find("Taille ELF invalide")!=std::string::npos);
    fs::remove_all(root); fs::create_directory(root);
    fs::path deep=root;
    for (size_t i=0;i<17;++i) { deep/="dir"; fs::create_directory(deep); }
    assert(inspect(root,-E2BIG).find("inventaire incomplet")!=std::string::npos);
    fs::remove_all(root); fs::create_directory(root);
    for (size_t i=0;i<4097;++i) write(root/std::to_string(i),{});
    text=inspect(root,-E2BIG);
    assert(text.find("4096 entrées")!=std::string::npos && text.find("Fichier ")==std::string::npos);
    fs::remove_all(root); fs::create_directory(root);
    for (size_t i=0;i<129;++i) write(root/(std::to_string(i)+".prx"),{});
    assert(inspect(root,-E2BIG).find("128 exécutables")!=std::string::npos);
    fs::remove_all(root); fs::create_directory(root);
    write(root/"a.prx",{}); fs::resize_file(root/"a.prx",64*1024*1024);
    write(root/"eboot.bin",app);
    assert(inspect(root,-E2BIG).find("Budget cumulé de lecture ELF/PRX")!=std::string::npos);
    std::cout << "PASS read-only game directory: deterministic inventory, NID/PRX, SELF/PKG refusal, TLS, symlinks/FIFO, budgets, truncation; no execution\n";
}
