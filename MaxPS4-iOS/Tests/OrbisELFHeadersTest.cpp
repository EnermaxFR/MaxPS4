// Mission 47: the program-header set of the real CUSA06015 modules (module
// param, TLS, PT_GNU_EH_FRAME, 0x6fffff00/0x6fffff01) through linkTLS and the
// TLS runtime. All bytes are synthetic; no CPU transfer is attempted here.
#include "../Native/OrbisV4TLSRuntime.hpp"
#include "OrbisELFHeadersFixture.h"
#include <cassert>
#include <iostream>
namespace {
using orbis_fixture::put;
bool link(const mission45_fixture::Bundle& bundle, maxps4::OrbisLinker::TLSImage& image,
          maxps4::OrbisLinker::Report& report, std::string& reason) {
    return maxps4::OrbisLinker::linkTLS(bundle.inputs(), {1, 2, 3, 4}, {}, image, reason, 32 * 1024 * 1024, &report);
}
}
int main() {
    using namespace maxps4;
    mission45_fixture::Bundle plain, bundle; mission47_fixture::apply(bundle);
    std::string reason; OrbisLinker::TLSImage reference, image; OrbisLinker::Report base, report;
    assert(link(plain, reference, base, reason));
    if (!link(bundle, image, report, reason)) { std::cerr << reason << "\n"; return 1; }
    assert(report.committed && report.moduleParams == 2 && report.ehFrameHeaders == 3 && report.opaqueHeaders == 5);
    assert(report.tlsRelocations == 16 && report.resolvedImports == 1);
    // Metadata adds no mapping: same mapping count, and only the 0x20 TLS
    // shift of mission 46 (two modules) changes the committed byte count.
    assert(image.mappings().size() == reference.mappings().size());
    assert(report.memoryBytes == base.memoryBytes + 2 * 0x20);
    size_t eh = 0;
    for (const auto& m : image.image().modules) {
        if (!m.ehFrameHeader.present) continue;
        ++eh;
        assert(m.ehFrameHeader.address == 0x40e0 && m.ehFrameHeader.ehFrame == 0x40a0 && m.ehFrameHeader.fdeCount == 2);
        assert(m.image.translate(0x40e0, 28, 4) && !m.image.translate(0x40e0, 1, 2));
        const auto* p = image.image().translate(m.bias + 0x40e0, 4, 5);
        assert(p && p[0] == 1 && p[1] == 0x1b && p[2] == 0x03 && p[3] == 0x3b);
    }
    assert(eh == 3);
    std::unique_ptr<OrbisV4TLSRuntime> runtime;
    assert(OrbisV4TLSRuntime::load(image, [] {}, runtime, reason) == 0);
    {
        auto lease = runtime->lease(runtime->primary()); uint64_t address = 0; uint32_t initial = 0;
        assert(lease.resolve(2, 0, 4, address) == 0 && lease.memory().read_data(address, &initial, 4) == 0 && initial == 42);
        for (size_t offset : {size_t(8), size_t(512)}) {
            uint64_t relocated = 0; assert(lease.resolve(2, offset, 8, address) == 0);
            assert(lease.memory().read_data(address, &relocated, 8) == 0 && relocated == 0x804000);
        }
        // Runtime copies keep the unwinding table read-only for the guest.
        uint8_t version = 0; assert(lease.memory().read_data(0x8040e0, &version, 1) == 0 && version == 1);
        assert(lease.memory().write_data(0x8040e0, &version, 1) == -EFAULT);
    }
    runtime->close();
    // Late refusals are transactional and keep the committed image.
    const auto* preserved = image.image().translate(0x808000, 8, 4);
    auto refuse = [&](auto mutate, const char* expected) {
        mission45_fixture::Bundle broken; mission47_fixture::apply(broken); mutate(broken);
        OrbisLinker::Report failed;
        assert(!link(broken, image, failed, reason));
        if (reason.find(expected) == std::string::npos) { std::cerr << reason << '\n'; assert(false); }
        assert(!failed.committed && image.image().translate(0x808000, 8, 4) == preserved);
    };
    refuse([](auto& b) { b.fios[eh_fixture::headerFile(0x200) + 1] = 0x03; }, "PT_GNU_EH_FRAME: unsupported pointer encodings");
    refuse([](auto& b) { put(b.libc, 64 + 7 * 56 + 16, 0xc080); }, "PT_GNU_EH_FRAME");
    refuse([](auto& b) { put(b.app, 64 + 6 * 56 + 40, 4); }, "with memory footprint refused");
    refuse([](auto& b) { put(b.fios, 64 + 8 * 56, 0x6474e551, 4); }, "unsupported runtime program header 1685382481");
    // A relocation cannot target the RX unwinding table, even for DTPMOD64.
    refuse([](auto& b) { put(b.libc, 0x900, 0x40e4); }, "TLS");
    // Strict (non-TLS) path still refuses TLS modules after these headers.
    OrbisLinker::Image strict;
    assert(!OrbisLinker::link(bundle.inputs(), {}, strict, reason) && reason.find("TLS not supported") != std::string::npos);
    std::cout << "PASS ELF headers: 3 PT_GNU_EH_FRAME views (RX text, no mapping/writes/unwinder), 2 module params, 5 inert 0x6fffff0x, TLS 16 DTPMOD64 intact, transactional refusals; no CPU admission\n";
}
