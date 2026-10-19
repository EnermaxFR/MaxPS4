#pragma once
#include "OrbisELFHeadersFixture.h"
// Mission 49: symbol shapes observed on CUSA06015 on top of the mission 48
// bundle. All bytes and identities are synthetic.
//  - libc exports an STT_TLS variable (TLS offset 0, 4 bytes) and builds its
//    own general-dynamic tls_index {DTPMOD64, DTPOFF64} at RELRO 0x80d0.
//  - the app IMPORTS that variable and builds tls_index {DTPMOD64, DTPOFF64}
//    at 0x8040, exactly where the FEX program reads the __tls_get_addr argument.
//  - libc also carries a protected exported function, an unreferenced weak
//    undefined "module_start" and an unreferenced global "__start__Ztext".
namespace mission49_fixture {
using orbis_fixture::put;
constexpr const char* tlsNid = "TlsVariable";
inline size_t addSymbol(std::vector<uint8_t>& b, uint64_t name, uint8_t info, uint8_t other, uint16_t section,
                        uint64_t value, uint64_t size) {
    const auto bytes = dyn_fixture::getTag(b, 0x6100003f), at = 0x800 + bytes;
    if (at + 24 > 0x900) throw std::runtime_error("fixture symbol table full");
    put(b, at, name, 4); b[at + 4] = info; b[at + 5] = other; put(b, at + 6, section, 2);
    put(b, at + 8, value); put(b, at + 16, size);
    orbis_fixture::setTag(b, 0x6100003f, bytes + 24);
    return size_t(bytes / 24);
}
inline size_t addRela(std::vector<uint8_t>& b, uint64_t offset, uint64_t info, uint64_t addend) {
    const auto bytes = dyn_fixture::getTag(b, 0x61000031), at = 0x900 + bytes;
    put(b, at, offset); put(b, at + 8, info); put(b, at + 16, addend);
    orbis_fixture::setTag(b, 0x61000031, bytes + 24);
    return size_t(bytes / 24);
}
inline void apply(mission45_fixture::Bundle& bundle) {
    auto& libc = bundle.libc; auto& app = bundle.app;
    // mission 46 rewrites libc RELA entries 13 and 14 (TLS pointer fixups):
    // append after them only.
    const auto tlsVar = addSymbol(libc, dyn_fixture::appendString(libc, std::string(tlsNid) + "#B#B"), 0x16, 0, 1, 0, 4);
    addSymbol(libc, dyn_fixture::appendString(libc, "Protected00#B#B"), 0x12, 3, 1, 0x4000, 1);
    addSymbol(libc, dyn_fixture::appendString(libc, "module_start"), 0x20, 0, 0, 0, 0);
    addSymbol(libc, dyn_fixture::appendString(libc, "__start__Ztext"), 0x10, 0, 0, 0, 0);
    addRela(libc, 0x80d0, (uint64_t(tlsVar) << 32) | 16, 0);
    addRela(libc, 0x80d8, (uint64_t(tlsVar) << 32) | 17, 0);
    // App: import descriptors (id 2 = "C") for the libc library/module, then the TLS import.
    const auto library = dyn_fixture::appendString(app, "FixtureLibrary"), module = dyn_fixture::appendString(app, "FixtureModule");
    orbis_fixture::appendTag(app, 0x61000015, library | (uint64_t{1} << 32) | (uint64_t{2} << 48));
    orbis_fixture::appendTag(app, 0x6100000f, module | (uint64_t{0x100} << 32) | (uint64_t{2} << 48));
    const auto imported = addSymbol(app, dyn_fixture::appendString(app, std::string(tlsNid) + "#C#C"), 0x16, 0, 0, 0, 0);
    addRela(app, 0x8040, (uint64_t(imported) << 32) | 16, 0);
    addRela(app, 0x8048, (uint64_t(imported) << 32) | 17, 0);
    put(app, 0x340, 0); // Raw index is zero: only the relocations produce {2, 0}.
    mission48_fixture::apply(bundle);
}
// The app now also depends on libc.prx (explicit, never guessed).
inline std::vector<maxps4::OrbisLinker::Input> inputs(const mission45_fixture::Bundle& bundle) {
    auto result = bundle.inputs();
    for (auto& in : result) if (in.file.request.logicalPath == "app.prx") in.file.request.dependencies.push_back("libc.prx");
    return result;
}
}
