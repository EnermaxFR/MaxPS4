#include "../Native/PS4SELFBlockReport.hpp"
#include "PS4ExecutableFixture.h"
#include <cassert>
#include <cstdio>

int main() {
    using maxps4::PS4SELFBlockReport;
    auto self = plainSELF();
    auto report = PS4SELFBlockReport::inspect(self.data(), self.size());
    assert(!report.error && report.stage.empty() && report.isSelf && !report.loadError);
    assert(report.blocks.size() == 2 && report.programHeaders == 2);
    assert(report.blocks[0].programHeader == 1 && report.blocks[0].programType == 1);
    assert(report.blocks[1].programHeader == 0 && report.blocks[1].inBounds);
    assert(report.verdict() == "Blocs en clair acceptés par le staging actuel");
    assert(report.text() == PS4SELFBlockReport::inspect(self.data(), self.size()).text()); // Deterministic.

    // Each class is reported, while the staging refusal stays unchanged.
    auto classify = [&](uint64_t flags, const char* prefix) {
        auto bad = self; guest_test::put(bad, flags, 32, 8);
        const auto r = PS4SELFBlockReport::inspect(bad.data(), bad.size());
        assert(!r.error && r.loadError != 0);
        assert(r.verdict().rfind(prefix, 0) == 0);
        return r;
    };
    auto r = classify(0x100806, "Blocs chiffrés");
    assert(r.encrypted == 1 && r.blocks[0].encrypted && r.blocks[0].signedBlock);
    r = classify(0x100808, "Blocs compressés");
    assert(r.compressed == 1 && r.blocks[0].compressed);
    r = classify(0x100805, "Blocs signés en clair");
    assert(r.signedOnly == 1 && r.blocks[0].ordered);
    r = classify(0x101800 | 0x10000, "Blocs en clair, mais");
    assert(r.blocks[0].blockSize == 0x2000 && r.blocks[0].digests && r.blocks[0].id == 1);
    r = classify(0x100800 | (uint64_t{1} << 40), "Drapeaux de bloc inconnus");
    assert(r.unknown == 1 && r.blocks[0].unknownFlags == uint64_t{1} << 40);
    r = classify(0x100806, "Blocs chiffrés"); // Encryption outranks every other class.

    auto outside = self; guest_test::put(outside, UINT64_MAX, 40, 8);
    r = PS4SELFBlockReport::inspect(outside.data(), outside.size());
    assert(r.outOfBounds == 1 && r.verdict().rfind("Blocs hors", 0) == 0);

    auto noElf = self; noElf[96] = 0;
    r = PS4SELFBlockReport::inspect(noElf.data(), noElf.size());
    assert(r.stage == "ELF.HEADER" && r.blocks.size() == 2 && r.blocks[0].programHeader < 0);

    auto badCount = self; guest_test::put(badCount, 129, 24, 2);
    r = PS4SELFBlockReport::inspect(badCount.data(), badCount.size());
    assert(r.error == -ENOEXEC && r.stage == "SELF.HEADER" && r.blocks.empty());

    auto elf = ps4ELF();
    r = PS4SELFBlockReport::inspect(elf.data(), elf.size());
    assert(!r.isSelf && !r.error && r.verdict() == "ELF brut accepté par le staging actuel");
    for (size_t length = 0; length <= self.size(); ++length) // Every truncation stays bounded.
        (void)PS4SELFBlockReport::inspect(self.data(), length).text();
    assert(PS4SELFBlockReport::inspect(nullptr, 0).error == -EINVAL);
    std::puts("PASS SELF block report: classes, ID mapping, verdict priority, truncation, no admission change");
}
