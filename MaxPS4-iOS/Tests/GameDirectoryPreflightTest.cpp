#include "../Native/OrbisAnalysisBridge.h"
#include "OrbisLinkerFixture.h"
#include "OrbisFixture.h"
#include "OrbisLoaderMission42Fixture.h"
#include "OrbisBootMission43Fixture.h"
#include "PS4PFSFixture.hpp"
#include "../Native/PS4PKGBridge.h"
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
        // Completion is an inventory result, including a missing eboot, valid
        // ELF/PRX or a present manifest. It never grants CPU admission.
        if (expected==0)
            assert(std::strstr(report.data(),"[ORBIS.PREFLIGHT] execution_admitted=0"));
        return std::string(report.data());
    };
    auto text = inspect(root);
    assert(text.find("Premier blocage du précontrôle : eboot.bin manquant")!=std::string::npos);
    assert(text.find("maxps4.json absent")!=std::string::npos);
    const auto clearSELF=mission42::self();
    write(root/"eboot.bin",clearSELF);
    text=inspect(root);
    assert(text.find("SELF en clair : profil statique strict validé")!=std::string::npos);
    assert(text.find("Proposition temporaire en mémoire")!=std::string::npos);
    assert(!fs::exists(root/"maxps4.json") && read(root/"eboot.bin")==clearSELF);
    // Mission 43: exercise the exact standalone C ABI imported by Swift too.
    std::vector<char> standalone(2*1024*1024);
    assert(maxps4_orbis_analysis_report((root/"eboot.bin").c_str(),
        standalone.data(),standalone.size())==0);
    assert(std::strstr(standalone.data(),"SELF en clair : profil statique strict validé"));
    char truncated[8]{};
    assert(maxps4_orbis_analysis_report((root/"eboot.bin").c_str(),
        truncated,sizeof truncated)==-ENOSPC && truncated[7]==0);
    assert(read(root/"eboot.bin")==clearSELF && !fs::exists(root/"maxps4.json"));
    auto protectedSELF=clearSELF; mission42::put(protectedSELF,32,0x802);
    write(root/"eboot.bin",protectedSELF);
    text=inspect(root);
    assert(text.find("SELF.ENCRYPTED")!=std::string::npos);
    assert(text.find("Proposition temporaire en mémoire")==std::string::npos);
    assert(maxps4_orbis_analysis_report((root/"eboot.bin").c_str(),
        standalone.data(),standalone.size())==-ENOEXEC);
    assert(std::strstr(standalone.data(),"SELF.ENCRYPTED"));
    assert(read(root/"eboot.bin")==protectedSELF);
    fs::remove(root/"eboot.bin");
    // Explicit iOS read-only ABI sees dynamic signed-clear layouts; strict
    // launch inspection is unaffected and the source folder remains unchanged.
    auto dynamicELF=orbis_fixture::make(false);
    const std::string dependency="libc.prx";
    std::copy(dependency.begin(),dependency.end(),dynamicELF.begin()+0x6a0);
    orbis_fixture::setTag(dynamicELF,0x61000037,0xb0);
    orbis_fixture::appendTag(dynamicELF,1,0xa0);
    fs::create_directory(root/"sce_module");
    write(root/"sce_module"/dependency,mission43_fixture::wrap(orbis_fixture::make(true)));
    const auto signedDynamic=mission43_fixture::wrap(dynamicELF);
    write(root/"eboot.bin",signedDynamic);
    std::vector<char> clearReport(2*1024*1024);
    assert(maxps4_game_directory_clear_self_report(root.c_str(),clearReport.data(),clearReport.size())==0);
    assert(std::strstr(clearReport.data(),"inspection non authentifiée"));
    assert(std::strstr(clearReport.data(),"ET_SCE_DYNEXEC"));
    assert(std::strstr(clearReport.data(),"candidat inventorié=sce_module/libc.prx"));
    fs::create_directory(root/"other_modules");
    write(root/"other_modules"/dependency,mission43_fixture::wrap(orbis_fixture::make(true)));
    assert(maxps4_game_directory_clear_self_report(root.c_str(),clearReport.data(),clearReport.size())==0);
    assert(std::strstr(clearReport.data(),"candidats ambigus, aucun chemin choisi"));
    fs::remove_all(root/"other_modules");
    assert(std::strstr(clearReport.data(),"execution_admitted=0"));
    assert(!std::strstr(clearReport.data(),"Proposition temporaire en mémoire"));
    assert(inspect(root).find("Conteneur SELF non pris en charge")!=std::string::npos);
    assert(read(root/"eboot.bin")==signedDynamic && !fs::exists(root/"maxps4.json"));
    assert(maxps4_game_directory_clear_self_report(root.c_str(),truncated,sizeof truncated)==-ENOSPC && truncated[7]==0);
    assert(maxps4_game_directory_clear_self_report(root.c_str(),nullptr,0)==-EINVAL);
    assert(maxps4_game_directory_clear_self_report(nullptr,clearReport.data(),clearReport.size())==-EINVAL);
    for(uint8_t flags:{uint8_t(2),uint8_t(8)}) {
        auto opaque=signedDynamic;opaque[32]|=flags;write(root/"eboot.bin",opaque);
        assert(maxps4_game_directory_clear_self_report(root.c_str(),clearReport.data(),clearReport.size())==0);
        assert(std::strstr(clearReport.data(),flags==2 ? "SELF.ENCRYPTED" : "SELF.COMPRESSED_UNSUPPORTED"));
        assert(!std::strstr(clearReport.data(),"ET_SCE_DYNEXEC"));
        assert(read(root/"eboot.bin")==opaque);
    }
    fs::remove(root/"eboot.bin");
    fs::remove_all(root/"sce_module");
    // These fabricated fixtures are not game data and never enter a CPU.
    const auto app = orbis_fixture::make(false), provider = orbis_fixture::make(true);
    // End-to-end: readable synthetic PFS -> explicitly selected memory bytes ->
    // caller-chosen destination -> existing #185 preflight. Never launch.
    auto pfs = pfsFixture::image();
    pfsFixture::le(pfs,65536+2*0xa8+8,app.size(),8);
    pfsFixture::le(pfs,65536+2*0xa8+0x10,app.size(),8);
    assert(app.size()<=65536);
    std::copy(app.begin(),app.end(),pfs.begin()+4*65536);
    write(root/"fixture.pkg",pfsFixture::package(pfs));
    std::vector<uint8_t> selected(app.size()); size_t selectedSize=0;
    assert(maxps4_pkg_pfs_read_file((root/"fixture.pkg").c_str(),"eboot.bin",selected.data(),selected.size(),&selectedSize)==0);
    assert(selectedSize==app.size() && selected==app);
    fs::remove(root/"fixture.pkg");
    write(root/"eboot.bin",selected);
    const auto fromPFS=inspect(root);
    assert(fromPFS.find("=== eboot.bin ===")!=std::string::npos && fromPFS.find("NID=AAAAAAAAAAA")!=std::string::npos);
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
    // A synthetically declared DT_NEEDED must be an explicit blocker too.
    auto needed=orbis_test::fixture();
    const size_t dynamicHeader=64+2*56;
    const size_t terminator=0x800+10*16;
    orbis_test::put(needed,1,terminator); orbis_test::put(needed,1,terminator+8);
    orbis_test::put(needed,0,terminator+16); orbis_test::put(needed,0,terminator+24);
    orbis_test::put(needed,12*16,dynamicHeader+32);
    orbis_test::put(needed,12*16,dynamicHeader+40);
    write(root/"eboot.bin",needed);
    assert(inspect(root).find("DT_NEEDED modA ; fichier adjacent absent")!=std::string::npos);
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
