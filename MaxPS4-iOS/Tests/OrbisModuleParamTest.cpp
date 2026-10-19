// Mission 46: PT_SCE_MODULE_PARAM in the TLS linking path, with the layout
// observed in libc.prx/libSceFios2.prx (param at RW PT_LOAD +0, TLS at +0x20).
// All bytes and identities are synthetic; no CPU transfer is attempted.
#include "../Native/OrbisV4TLSRuntime.hpp"
#include "OrbisELFHeadersFixture.h"
#include <cassert>
#include <iostream>
namespace {
using orbis_fixture::put;
using mission46_fixture::realLayout;
using mission46_fixture::dataHeader;
using mission46_fixture::paramHeader;
struct Batch {
    mission45_fixture::Bundle base;
    std::vector<uint8_t> libc = realLayout(base.libc, 416, 1168, 13), fios = realLayout(base.fios, 0, 64, 3);
    std::vector<maxps4::OrbisLinker::Input> inputs() const {
        auto result = base.inputs();
        result[1].file.bytes = libc.data(); result[1].file.size = libc.size();
        result[2].file.bytes = fios.data(); result[2].file.size = fios.size();
        return result;
    }
};
uint64_t read64(const maxps4::OrbisLinker::Image& image, uint64_t address) {
    const auto* p = image.translate(address, 8, 4); assert(p);
    return maxps4::OrbisMetadata::read(p, 8);
}
}
int main() {
    using namespace maxps4;
    Batch batch; std::string reason;
    OrbisLinker::TLSImage image; OrbisLinker::Report report;
    assert(OrbisLinker::linkTLS(batch.inputs(), {1, 2, 3, 4}, {}, image, reason, 32 * 1024 * 1024, &report));
    assert(report.committed && report.moduleParams == 2 && report.tlsRelocations == 16 && report.resolvedImports == 1);
    const auto& modules = image.image().modules;
    size_t seen = 0;
    for (const auto& m : modules) if (m.path == "libc.prx" || m.path == "libSceFios2.prx") {
        assert(m.moduleParam.present && m.moduleParam.address == 0xc000 && m.moduleParam.sdkVersion == 0x04008061);
        // No extra mapping: the view stays inside its single PT_LOAD mapping.
        size_t owning = 0;
        for (const auto& s : m.image.segments)
            owning += s.header.address <= 0xc000 && 0xc018 <= s.header.address + s.header.memorySize;
        assert(owning == 1);
        ++seen;
    } else assert(!m.moduleParam.present);
    assert(seen == 2);
    assert(read64(image.image(), 0x80c000) == 24 && read64(image.image(), 0xc0c000) == 24);
    // TLS DTPMOD64 patches and the template stay intact next to the param.
    assert(read64(image.image(), 0x808000) == 2 && read64(image.image(), 0xc08000) == 3);
    std::unique_ptr<OrbisV4TLSRuntime> runtime;
    assert(OrbisV4TLSRuntime::load(image, [] {}, runtime, reason) == 0);
    {
        auto lease = runtime->lease(runtime->primary()); uint64_t address = 0;
        uint32_t initial = 0; assert(lease.resolve(2, 0, 4, address) == 0);
        assert(lease.memory().read_data(address, &initial, 4) == 0 && initial == 42);
        for (size_t offset : {size_t(8), size_t(512)}) {
            uint64_t relocated = 0; assert(lease.resolve(2, offset, 8, address) == 0);
            assert(lease.memory().read_data(address, &relocated, 8) == 0 && relocated == 0x804000);
        }
        uint64_t zero = 1; assert(lease.resolve(3, 0, 64, address) == 0);
        assert(lease.memory().read_data(address, &zero, 8) == 0 && zero == 0);
        // The param view is not TLS: thread blocks never alias its guest bytes.
        assert(address < 0xc0c000 || address >= 0xc0c018);
    }
    runtime->close();

    // Every refusal is transactional: the committed TLS image is preserved.
    const auto* preserved = image.image().translate(0x808000, 8, 4);
    auto refuse = [&](auto mutate, const char* expected) {
        Batch broken; mutate(broken);
        OrbisLinker::Report failed;
        assert(!OrbisLinker::linkTLS(broken.inputs(), {1, 2, 3, 4}, {}, image, reason, 32 * 1024 * 1024, &failed));
        if (reason.find(expected) == std::string::npos) { std::cerr << reason << '\n'; assert(false); }
        assert(!failed.committed && image.image().translate(0x808000, 8, 4) == preserved);
    };
    // Param overlapping the TLS template (memory/TLS boundary).
    refuse([](Batch& b) { put(b.libc, paramHeader + 16, 0xc010); put(b.libc, paramHeader + 8, 0x1010); }, "overlaps PT_TLS");
    // Relocation into the param (pointer-free profile); also a TLS DTPMOD64 one.
    refuse([](Batch& b) { put(b.libc, 0x900, 0xc008); }, "PT_SCE_MODULE_PARAM");
    // Param beyond the file-backed bytes of the RW PT_LOAD (Fios2 BSS).
    refuse([](Batch& b) { put(b.fios, dataHeader + 32, 0x10); }, "file-backed");
    // Different file/address delta or an unknown structure version.
    refuse([](Batch& b) { put(b.libc, paramHeader + 8, 0x1008); }, "file offset differs");
    refuse([](Batch& b) { put(b.libc, 0x100c, 2, 4); }, "unsupported version");
    // Dependency graph and budgets are still enforced before any write.
    refuse([](Batch& b) { b.base.app[0] = 0; }, "ELF");
    {
        Batch ok; OrbisLinker::TLSImage small; OrbisLinker::Report budget;
        // Exactly the committed bytes fit; one byte less refuses (no param surcharge).
        assert(OrbisLinker::linkTLS(ok.inputs(), {1, 2, 3, 4}, {}, small, reason, report.memoryBytes, &budget) && budget.committed);
        assert(!OrbisLinker::linkTLS(ok.inputs(), {1, 2, 3, 4}, {}, small, reason, report.memoryBytes - 1, &budget) && !budget.committed);
        assert(reason.find("budget") != std::string::npos);
    }
    // Strict (non-TLS) path keeps refusing these TLS modules after the param.
    OrbisLinker::Image strict;
    assert(!OrbisLinker::link(batch.inputs(), {}, strict, reason) && reason.find("TLS not supported") != std::string::npos);
    std::cout << "PASS module param: 2 verified PT_SCE_MODULE_PARAM views (no mapping, no write), TLS +0x20 intact, 16 DTPMOD64, refusals transactional; no CPU admission\n";
}
