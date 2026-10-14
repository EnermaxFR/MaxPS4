// SPDX-License-Identifier: GPL-2.0-only
#include "../Native/maxps4_guest_elf.h"
#include "GuestELFFixture.h"
#include "PS4PFSFixture.hpp"
#include "../Native/PS4PKGBridge.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
int main(int argc, char** argv) {
    assert(argc == 2);
    std::ifstream stream(argv[1], std::ios::binary);
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(stream)), {});
    assert(file.size() == 131);
    // End-to-end legal fixture: selected plaintext CNT/PFS extraction -> Orbis
    // validation/linking -> native guest mapping. Still NO CPU execution.
    auto pfs = pfsFixture::image();
    const size_t inode = 65536 + 2 * 0xa8;
    pfsFixture::le(pfs,inode+8,file.size(),8);
    pfsFixture::le(pfs,inode+0x10,file.size(),8);
    std::copy(file.begin(),file.end(),pfs.begin()+4*65536);
    auto pkg = pfsFixture::package(pfs);
    const std::string path = std::string(argv[1])+".pkg";
    { std::ofstream output(path,std::ios::binary);
      output.write(reinterpret_cast<const char*>(pkg.data()),pkg.size()); assert(output.good()); }
    size_t length=0;
    assert(maxps4_pkg_pfs_read_file(path.c_str(),"eboot.bin",nullptr,0,&length)==-ENOSPC && length==file.size());
    std::vector<uint8_t> extracted(length);
    assert(maxps4_pkg_pfs_read_file(path.c_str(),"eboot.bin",extracted.data(),extracted.size(),&length)==0);
    assert(length==file.size() && extracted==file);
    file=std::move(extracted);
    maxps4::OrbisLinker::Image linked;
    std::string why;
    const auto link = [&](const std::vector<uint8_t>& bytes) {
        maxps4::OrbisLinker::Input input{{{"test.elf", {}}, bytes.data(), bytes.size()}, 0};
        return maxps4::OrbisLinker::link({input}, {}, linked, why);
    };
    assert(link(file));
    assert(linked.modules.size() == 1 && linked.modules[0].entry == 0x1000);
    const auto* bss = linked.translate(0x100b, 21, 4); assert(bss);
    for (size_t i=0;i<21;++i) assert(bss[i] == 0);
    maxps4::GuestELF guest;
    for (int i=0;i<64;++i) {
        assert(guest.prepare_orbis(linked, "test.elf", 64, why) == 0);
        assert(guest.entry() == 0x1000 && guest.segments().size() == 1);
        const auto* code = guest.translate(0x1000, 11, 5); assert(code && code[0] == 0xb8);
        assert(!guest.translate(0x1000, 1, 2));
        guest.clear(); assert(guest.segments().size() == 0 && guest.entry() == 0);
    }
    for (auto at : {size_t(7),size_t(18),size_t(54),size_t(68),size_t(72),size_t(80),size_t(96)}) {
        auto corrupt=file; corrupt[at]=0xff;
        assert(!link(corrupt));
    }
    auto oversized=file; guest_test::put(oversized, UINT64_MAX, 104, 8);
    assert(!link(oversized));
    for (size_t length=0;length<file.size();++length) {
        std::vector<uint8_t> shortFile(file.begin(), file.begin()+length);
        assert(!link(shortFile));
    }
    auto unsupported=file; unsupported[120]=0xeb; // Branch not admitted.
    assert(link(unsupported));
    assert(guest.prepare_orbis(linked,"test.elf",64,why) != 0);
    assert(guest.entry() == 0 && guest.segments().size() == 0);
    std::puts("PASS legal plaintext CNT/PFS extraction -> original ET_SCE_EXEC validation, linking, BSS, native guest mapping, 64 cleanup cycles, corruption and CPU admission refusal. NO CPU execution or FEX in this test.");
}
