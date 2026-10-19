// Mission 50: provider extension with extra export libraries and host-service
// stubs. Original guest code keeps its addresses; stubs are exact bytes.
#include "../Native/OrbisLinker.hpp"
#include "../Native/OrbisV4LocalServices.hpp"
#include "../Native/OrbisV4ServicePRX.hpp"
#include "OrbisV4CompatibilityFixture.h"
#include "OrbisDynamicTagsFixture.h"
#include <cassert>
#include <iostream>
using namespace maxps4;
int main() {
    OrbisV4LocalServices::Module kernel{};
    for (const auto& m : OrbisV4LocalServices::modules()) if (std::string(m.name) == "libkernel") kernel = m;
    const auto base = OrbisV4LocalServices::provider(kernel);
    OrbisV4ServicePRX::Layout layout;
    const auto prx = OrbisV4ServicePRX::extend(base, {{"libScePosix", 1}},
        {{"9vyP6Z7bqzc", 0, 0x4d500001}, {"7H0iTOciTLo", 1, 0x4d500002}}, 0x1000, layout);
    OrbisMetadata b, x;
    assert(b.parse(base.data(), base.size()) && x.parse(prx.data(), prx.size()));
    assert(x.libraries.size() == 2 && x.libraries[1].name == "libScePosix" && x.modules.size() == 1 &&
           x.modules[0].name == "libkernel" && x.modules[0].version == b.modules[0].version);
    // Original exports and code bytes are unchanged, at the same addresses.
    size_t kept = 0;
    for (const auto& s : b.symbols) if (s.defined && !s.encoded.empty())
        for (const auto& t : x.symbols) if (t.encoded == s.encoded && t.value == s.value && t.size == s.size) ++kept;
    assert(kept == 2);
    const auto* oldCode = base.data() + 0x200;
    size_t newText = 0;
    for (const auto& s : x.segments) if (s.type == 1 && s.address == 0x4000) newText = s.offset;
    uint64_t oldSize = 0;
    for (const auto& s : b.segments) if (s.type == 1 && s.address == 0x4000) oldSize = s.filesz;
    assert(newText && oldSize && oldSize <= layout.stubs - 0x4000 && std::equal(oldCode, oldCode + oldSize, prx.data() + newText));
    for (const auto& s : x.segments) if (s.type == 1 && s.address == 0x8000) assert(s.memsz == 0x1000 && (s.flags & 7) == 6);
    // Stub bytes and identities.
    const uint8_t expected[16] = {0x49, 0x89, 0xca, 0xb8, 0x02, 0x00, 0x50, 0x4d, 0x0f, 0x05, 0xc3, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc};
    const auto* stub1 = prx.data() + newText + (layout.stubs + 16 - 0x4000);
    assert(std::equal(expected, expected + 16, stub1) && prx[newText + (layout.halt - 0x4000)] == 0xf4);
    bool posix = false;
    for (const auto& s : x.symbols) if (s.defined && s.nid == "7H0iTOciTLo")
        posix = s.library == "libScePosix" && s.module == "libkernel" && s.value == layout.stubs + 16 && s.size == 16;
    assert(posix);
    // Duplicate identities and oversized inputs are refused.
    bool refused = false;
    try { OrbisV4ServicePRX::extend(base, {}, {{"k+AXqu2-eBc", 0, 1}}, 0x1000, layout); } catch (const std::invalid_argument&) { refused = true; }
    assert(refused); refused = false;
    try { OrbisV4ServicePRX::extend(base, {}, {{"short", 0, 1}}, 0x1000, layout); } catch (const std::invalid_argument&) { refused = true; }
    assert(refused); refused = false;
    try { OrbisV4ServicePRX::extend(base, {}, {{"7H0iTOciTLo", 2, 1}}, 0x1000, layout); } catch (const std::invalid_argument&) { refused = true; }
    assert(refused); refused = false;
    try { OrbisV4ServicePRX::extend(base, {}, {}, 2, layout); } catch (const std::length_error&) { refused = true; }
    assert(refused); refused = false;
    std::vector<OrbisV4ServicePRX::Stub> many(1100, {"7H0iTOciTLo", 0, 1});
    for (size_t i = 0; i < many.size(); ++i) { many[i].nid = "AAAAAAAAAAA"; many[i].nid[0] = "ABCDEFGHIJ"[i % 10]; many[i].nid[1] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"[(i / 10) % 26]; many[i].nid[2] = "ABCDEFGHIJ"[i / 260]; }
    try { OrbisV4ServicePRX::extend(base, {}, many, 0x1000, layout); } catch (const std::length_error&) { refused = true; }
    assert(refused);
    // OBJECT exports live in RW state, after the base state, with explicit bytes.
    {
        const auto withData = OrbisV4ServicePRX::extend(base, {}, {}, 0x1000, layout,
            {{"f7uOxY9mM1U", 0, 0x10, {1, 2, 3, 4, 5, 6, 7, 8}}});
        OrbisMetadata d; assert(d.parse(withData.data(), withData.size()));
        bool object = false;
        for (const auto& s : d.symbols) if (s.nid == "f7uOxY9mM1U") object = s.defined && s.kind == 1 && s.value == 0x8010 && s.size == 8;
        assert(object);
        for (const auto& s : d.segments) if (s.type == 1 && s.address == 0x8000)
            assert(withData[s.offset + 0x10] == 1 && withData[s.offset + 0x17] == 8);
        for (auto bad : {std::vector<OrbisV4ServicePRX::Data>{{"f7uOxY9mM1U", 0, 0, {1}}},          // inside base state
                         std::vector<OrbisV4ServicePRX::Data>{{"f7uOxY9mM1U", 0, 0xff8, {1,2,3,4,5,6,7,8,9}}}, // beyond state
                         std::vector<OrbisV4ServicePRX::Data>{{"f7uOxY9mM1U", 0, 0x10, {1,2}}, {"AAAAAAAAAAA", 0, 0x11, {1}}}}) {
            refused = false;
            try { OrbisV4ServicePRX::extend(base, {}, {}, 0x1000, layout, bad); } catch (const std::invalid_argument&) { refused = true; }
            assert(refused);
        }
    }
    // A consumer importing libScePosix (module libkernel) links to the stub.
    auto app = compat_fixture::consumer("libkernel", "libkernel.prx", {"7H0iTOciTLo"});
    orbis_fixture::setTag(app, 0x61000015, dyn_fixture::appendString(app, "libScePosix") | (uint64_t{1} << 32) | (uint64_t{1} << 48));
    OrbisLinker::Image image; std::string diagnostic; OrbisLinker::Report report;
    const auto prx2 = OrbisV4ServicePRX::extend(base, {{"libScePosix", 1}}, {{"7H0iTOciTLo", 1, 0x4d500002}}, 0x1000, layout);
    if (!OrbisLinker::link({{{{"app.elf", {"libkernel.prx"}}, app.data(), app.size()}, 0x100000},
                           {{{"libkernel.prx", {}}, prx2.data(), prx2.size()}, 0x200000}}, {}, image, diagnostic, 1 << 20, &report)) {
        std::cerr << diagnostic << '\n'; return 1;
    }
    const auto* got = image.translate(0x102000, 8, 4);
    assert(got && OrbisMetadata::read(got, 8) == 0x200000 + layout.stubs && report.resolvedImports == 1);
    std::cout << "PASS service PRX: original code/exports kept at same addresses, extra library descriptor, exact 16-byte RCX-preserving syscall stubs + HLT, duplicate/identity/size refusals, libScePosix import links to stub, OBJECT data exports bounded\n";
}
