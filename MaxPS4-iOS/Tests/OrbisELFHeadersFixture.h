#pragma once
#include "OrbisTLSMission45Fixture.h"
#include "OrbisEHFrameFixture.h"
#include "OrbisDynamicTagsFixture.h"
// Program-header shapes observed on CUSA06015, applied to the synthetic
// mission 45 bundle. All bytes and identities remain fabricated.
namespace mission46_fixture {
using orbis_fixture::put;
constexpr size_t tlsHeader = 64 + 4 * 56, dataHeader = 64 + 5 * 56, paramHeader = 64 + 6 * 56;
// Shift TLS by 0x20 inside the RW PT_LOAD and expose its first 24 bytes as
// the module parameter view, exactly as in the real modules.
inline std::vector<uint8_t> realLayout(std::vector<uint8_t> b, size_t initial, size_t total, size_t relocations) {
    b.insert(b.begin() + 0x1000, 0x20, 0);
    put(b, 56, 7, 2);
    put(b, tlsHeader + 8, 0x1020); put(b, tlsHeader + 16, 0xc020);
    put(b, dataHeader + 32, initial + 0x20); put(b, dataHeader + 40, std::max<size_t>(64, total) + 0x20);
    put(b, paramHeader, 0x61000002, 4); put(b, paramHeader + 4, 4, 4); put(b, paramHeader + 8, 0x1000);
    put(b, paramHeader + 16, 0xc000); put(b, paramHeader + 32, 24); put(b, paramHeader + 40, 24); put(b, paramHeader + 48, 8);
    put(b, 0x1000, 24); put(b, 0x1008, 0x3C13F4BF, 4); put(b, 0x100c, 1, 4); put(b, 0x1010, 0x04008061, 4);
    if (initial >= 16 && total >= 520)
        for (size_t i = 0; i < 2; ++i) put(b, 0x900 + (relocations + i) * 24, 0xc020 + (i ? 512 : 8));
    return b;
}
}
namespace mission47_fixture {
// libc/Fios2: param + TLS (mission 46), EH_FRAME in text, 0x6fffff01 (and
// 0x6fffff00 for libc) as inert entries. App: EH_FRAME, 0x6fffff00/01.
// Text PT_LOAD of every module: file 0x200, guest 0x4000 (mission 45 bias map).
inline void apply(mission45_fixture::Bundle& bundle) {
    bundle.libc = mission46_fixture::realLayout(bundle.libc, 416, 1168, 13);
    bundle.fios = mission46_fixture::realLayout(bundle.fios, 0, 64, 3);
    eh_fixture::addEHFrame(bundle.libc, 7, 0x4000, 0x200);
    eh_fixture::program(bundle.libc, 8, 0x6fffff00, 0, 0xb00, 0, 0x10, 0, 0x10);
    eh_fixture::program(bundle.libc, 9, 0x6fffff01, 0, 0xb10, 0, 0x8, 0, 0x10);
    eh_fixture::addEHFrame(bundle.fios, 7, 0x4000, 0x200);
    eh_fixture::program(bundle.fios, 8, 0x6fffff01, 0, 0xb10, 0, 0x8, 0, 0x10);
    eh_fixture::addEHFrame(bundle.app, 4, 0x4000, 0x200);
    eh_fixture::program(bundle.app, 5, 0x6fffff00, 0, 0xb00, 0, 0x10, 0, 0x10);
    eh_fixture::program(bundle.app, 6, 0x6fffff01, 0, 0xb10, 0, 0x8, 0, 0x10);
}
}
namespace mission48_fixture {
// Mission 47 headers plus the real dynamic-tag set. libc carries one
// DT_PREINIT_ARRAY entry produced by a RELATIVE relocation into its code, as
// libc.prx does (entry -> text +0x600 there, text start here).
inline void apply(mission45_fixture::Bundle& bundle) {
    mission47_fixture::apply(bundle);
    dyn_fixture::add(bundle.libc, {"libc.prx", 0x4000, 0x8100, 0, 1, 0, false});
    dyn_fixture::add(bundle.fios, {"libSceFios2.prx", 0x4000, 0x8100, 0, 1, 0, false});
    dyn_fixture::add(bundle.app, {"", 0x4000, 0x8100, 0x8060, 0, 1, false});
    const auto count = dyn_fixture::getTag(bundle.libc, 0x61000031) / 24, entry = 0x900 + count * 24;
    using orbis_fixture::put;
    put(bundle.libc, entry, 0x80f0); put(bundle.libc, entry + 8, 8); put(bundle.libc, entry + 16, 0x4000);
    orbis_fixture::setTag(bundle.libc, 0x61000031, (count + 1) * 24);
    orbis_fixture::setTag(bundle.libc, 32, 0x80f0); orbis_fixture::setTag(bundle.libc, 33, 8);
}
}
