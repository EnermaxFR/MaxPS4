// Mission 49: symbol handling through linkTLS and the TLS runtime: exported
// and imported STT_TLS symbols resolved to {module id, offset} for the
// existing DTV, protected visibility, unbound weak/global plain-name symbols.
// Synthetic bytes only; no CPU transfer here (see FEXGuestTranslationTest).
#include "../Native/OrbisV4TLSRuntime.hpp"
#include "OrbisSymbolsFixture.h"
#include <algorithm>
#include <cassert>
#include <iostream>
namespace {
using orbis_fixture::put;
uint64_t read64(const maxps4::OrbisLinker::Image& image, uint64_t address) {
    const auto* p = image.translate(address, 8, 4); assert(p); return maxps4::OrbisMetadata::read(p, 8);
}
}
int main() {
    using namespace maxps4;
    mission45_fixture::Bundle reference, bundle; mission48_fixture::apply(reference); mission49_fixture::apply(bundle);
    std::string reason; OrbisLinker::TLSImage base, image; OrbisLinker::Report before, report;
    assert(OrbisLinker::linkTLS(reference.inputs(), {1, 2, 3, 4}, {}, base, reason, 32 * 1024 * 1024, &before));
    if (!OrbisLinker::linkTLS(mission49_fixture::inputs(bundle), {1, 2, 3, 4}, {}, image, reason, 32 * 1024 * 1024, &report)) {
        std::cerr << reason << '\n'; return 1;
    }
    assert(report.committed && report.tlsSymbols == 1 && report.protectedSymbols == 1);
    assert(report.unboundWeakSymbols == 1 && report.unboundGlobalSymbols == 1);
    assert(report.tlsRelocations == before.tlsRelocations + 2 && report.tlsOffsetRelocations == 2);
    assert(report.imports == before.imports + 1 && report.resolvedImports == before.resolvedImports + 1);
    assert(image.mappings().size() == base.mappings().size() && report.memoryBytes == before.memoryBytes);
    // tls_index pairs produced by relocations: {libc module id = 2, offset 0}.
    assert(read64(image.image(), 0x408040) == 2 && read64(image.image(), 0x408048) == 0);
    assert(read64(image.image(), 0x8080d0) == 2 && read64(image.image(), 0x8080d8) == 0);
    // Every pre-existing relocation result is unchanged (no corruption).
    for (uint64_t a : {uint64_t(0x808000), uint64_t(0x8080c0), uint64_t(0xc08000), uint64_t(0x8080f0), uint64_t(0x408000)})
        assert(read64(image.image(), a) == read64(base.image(), a));
    std::unique_ptr<OrbisV4TLSRuntime> runtime;
    assert(OrbisV4TLSRuntime::load(image, [] {}, runtime, reason) == 0);
    for (int t = 0; t < 2; ++t) {
        OrbisV4TLSRuntime::Thread second;
        if (t) assert(runtime->create_thread(second) == 0);
        auto lease = runtime->lease(t ? second : runtime->primary());
        uint64_t index[2]{}; assert(lease.memory().read_data(0x408040, index, sizeof(index)) == 0);
        uint64_t address = 0; uint32_t value = 0;
        assert(lease.resolve(uint32_t(index[0]), index[1], 4, address) == 0);
        assert(lease.memory().read_data(address, &value, 4) == 0 && value == 42); // TLS template byte 0.
        uint64_t other = 0; assert(lease.resolve(2, 8, 8, other) == 0 && other == address + 8);
        uint64_t relocated = 0; assert(lease.memory().read_data(other, &relocated, 8) == 0 && relocated == 0x804000);
        assert(lease.resolve(uint32_t(index[0]), 1168, 1, other) == -EFAULT); // DTV bounds still enforced.
    }
    runtime->close();
    const auto* preserved = image.image().translate(0x808000, 8, 4);
    auto refuse = [&](auto mutate, const char* expected, bool keepDeps = true) {
        mission45_fixture::Bundle broken; mission49_fixture::apply(broken); mutate(broken);
        OrbisLinker::Report failed;
        const auto inputs = keepDeps ? mission49_fixture::inputs(broken) : broken.inputs();
        if (OrbisLinker::linkTLS(inputs, {1, 2, 3, 4}, {}, image, reason, 32 * 1024 * 1024, &failed)) {
            std::cerr << "unexpectedly linked: " << expected << '\n'; assert(false);
        }
        if (reason.find(expected) == std::string::npos) { std::cerr << expected << " <> " << reason << '\n'; assert(false); }
        assert(!failed.committed && image.image().translate(0x808000, 8, 4) == preserved);
    };
    const size_t appRela = 0x900 + 24, libcSymbols = 0x800 + 2 * 24;
    refuse([](auto& b) { put(b.app, appRela + 24 + 16, 4); }, "DTPOFF64 outside TLS symbol");
    refuse([](auto& b) { put(b.app, appRela + 16, 1); }, "DTPMOD64 addend must be zero");
    refuse([](auto& b) { put(b.app, appRela + 8, (uint64_t{2} << 32) | 1); }, "TLS symbol used by non-TLS relocation");
    refuse([](auto& b) { put(b.app, appRela + 8, (uint64_t{1} << 32) | 17); }, "TLS");
    refuse([](auto& b) { put(b.app, appRela + 8, (uint64_t{2} << 32) | 18); }, "TLS relocation unsupported");
    refuse([](auto& b) { put(b.libc, libcSymbols + 8, 1168); }, "STT_TLS symbol outside PT_TLS");
    // Import identity: no TLS export under that NID, or libc not declared.
    refuse([](auto& b) {
        const std::string name = std::string(mission49_fixture::tlsNid) + "#C#C";
        auto at = std::search(b.app.begin() + 0x600, b.app.begin() + 0x800, name.begin(), name.end());
        assert(at != b.app.begin() + 0x800); *at = 'X';
    }, "unresolved TLS NID");
    refuse([](auto&) {}, "missing explicit dependency", false);
    // Unbound plain-name symbols are refused as soon as a relocation uses them.
    refuse([](auto& b) { mission49_fixture::addRela(b.libc, 0x80e0, (uint64_t{4} << 32) | 1, 0); }, "unbound weak undefined symbol module_start");
    refuse([](auto& b) { mission49_fixture::addRela(b.libc, 0x80e0, (uint64_t{5} << 32) | 6, 0); }, "unbound global undefined symbol __start__Ztext");
    // Visibility: hidden/internal are not exported (undefined protected: OrbisLinkerTest).
    refuse([](auto& b) { b.libc[libcSymbols + 24 + 5] = 2; }, "visibility");
    refuse([](auto& b) { b.libc[libcSymbols + 24 + 5] = 1; }, "visibility");
    // Plain-name undefined symbols must be NOTYPE with zero value/size.
    refuse([](auto& b) { b.libc[libcSymbols + 2 * 24 + 4] = 0x22; }, "invalid NID identity: module_start");
    refuse([](auto& b) { put(b.libc, libcSymbols + 3 * 24 + 8, 8); }, "invalid undefined symbol: __start__Ztext");
    std::cout << "PASS symbols: imported/exported STT_TLS -> tls_index {DTV module 2, offset} via DTPMOD64/DTPOFF64 (main+secondary threads), protected export, unbound weak/global plain names never addressed, existing writes unchanged; no CPU admission\n";
}
