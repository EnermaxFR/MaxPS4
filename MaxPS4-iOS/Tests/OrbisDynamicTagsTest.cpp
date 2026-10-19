// Mission 48: real dynamic-tag set (SCE hash, fingerprint, SONAME, original
// filename, init/fini, arrays, PLTGOT, library attributes, DEBUG/FLAGS) on top
// of the mission 47 header set, through linkTLS and the TLS runtime. Synthetic
// bytes only; init/fini are recorded and NEVER executed here.
#include "../Native/OrbisV4TLSRuntime.hpp"
#include "OrbisELFHeadersFixture.h"
#include <cassert>
#include <iostream>
int main() {
    using namespace maxps4;
    mission45_fixture::Bundle headers, bundle; mission47_fixture::apply(headers); mission48_fixture::apply(bundle);
    std::string reason; OrbisLinker::TLSImage reference, image; OrbisLinker::Report base, report;
    assert(OrbisLinker::linkTLS(headers.inputs(), {1, 2, 3, 4}, {}, reference, reason, 32 * 1024 * 1024, &base));
    if (!OrbisLinker::linkTLS(bundle.inputs(), {1, 2, 3, 4}, {}, image, reason, 32 * 1024 * 1024, &report)) {
        std::cerr << reason << '\n'; return 1;
    }
    assert(report.committed && report.recognisedDynamicTags == 51 && report.initFunctions == 3 && report.finiFunctions == 3);
    assert(report.initArrayEntries == 1 && report.tlsRelocations == 16 && report.resolvedImports == 1);
    assert(report.validatedRelocations == base.validatedRelocations + 1);
    assert(image.mappings().size() == reference.mappings().size() && report.memoryBytes == base.memoryBytes);
    // Dependency order is preserved: providers before the app that needs them.
    const auto& modules = image.image().modules;
    size_t app = modules.size(), kernel = modules.size();
    for (size_t i = 0; i < modules.size(); ++i) {
        if (modules[i].path == "app.prx") app = i;
        if (modules[i].path == "libkernel.prx") kernel = i;
    }
    assert(kernel < app && app < modules.size());
    for (const auto& m : modules) {
        if (m.path == "libkernel.prx") { assert(!m.dynamic.hash && !m.dynamic.init); continue; }
        assert(m.dynamic.hash && m.dynamic.fingerprint && m.dynamic.init && m.dynamic.initAddress == 0x4000);
        assert(m.dynamic.soname == (m.path == "app.prx" ? std::string() : m.path));
        assert(m.dynamic.originalFilename.find("fixture path/") != std::string::npos);
    }
    // libc PREINIT entry relocated (RELATIVE) to its code; recorded, not run.
    const auto* entry = image.image().translate(0x8080f0, 8, 4);
    assert(entry && OrbisMetadata::read(entry, 8) == 0x804000 && !image.image().translate(0x8080f0, 8, 2));
    std::unique_ptr<OrbisV4TLSRuntime> runtime;
    assert(OrbisV4TLSRuntime::load(image, [] {}, runtime, reason) == 0);
    {
        auto lease = runtime->lease(runtime->primary()); uint64_t address = 0; uint32_t initial = 0;
        assert(lease.resolve(2, 0, 4, address) == 0 && lease.memory().read_data(address, &initial, 4) == 0 && initial == 42);
        uint64_t preinit = 0; assert(lease.memory().read_data(0x8080f0, &preinit, 8) == 0 && preinit == 0x804000);
        assert(lease.memory().write_data(0x8080f0, &preinit, 8) == -EFAULT); // Sealed RELRO.
    }
    runtime->close();
    const auto* preserved = image.image().translate(0x808000, 8, 4);
    auto refuse = [&](auto mutate, const char* expected) {
        mission45_fixture::Bundle broken; mission48_fixture::apply(broken); mutate(broken);
        OrbisLinker::Report failed;
        assert(!OrbisLinker::linkTLS(broken.inputs(), {1, 2, 3, 4}, {}, image, reason, 32 * 1024 * 1024, &failed));
        if (reason.find(expected) == std::string::npos) { std::cerr << reason << '\n'; assert(false); }
        assert(!failed.committed && image.image().translate(0x808000, 8, 4) == preserved);
    };
    using orbis_fixture::put;
    refuse([](auto& b) { put(b.fios, dyn_fixture::hashFile + 4, 7, 4); }, "DT_SCE_HASH");
    refuse([](auto& b) { orbis_fixture::setTag(b.libc, 14, dyn_fixture::appendString(b.libc, "libSceFios2.prx")); }, "DT_SONAME differs");
    refuse([](auto& b) { orbis_fixture::setTag(b.libc, 32, 0x80f8); }, "without pointer relocation");
    refuse([](auto& b) {
        const auto n = dyn_fixture::getTag(b.libc, 0x61000031) / 24; put(b.libc, 0x900 + (n - 1) * 24 + 16, 0x8000);
    }, "outside module code");
    refuse([](auto& b) { orbis_fixture::setTag(b.app, 12, 0x8000); }, "DT_INIT"); // OrbisMetadata refuses first in linkTLS.
    refuse([](auto& b) { orbis_fixture::appendTag(b.app, 24, 0); }, "unsupported dynamic tag 24");
    std::cout << "PASS dynamic tags: 51 validated (SCE hash structure, fingerprint, SONAME, init/fini, arrays, PLTGOT, attributes), preinit entry relocated into code, dependency order kept, init/fini executed=0; no CPU admission\n";
}
