#include "../Native/PS4PKGInventory.hpp"
#include "../Native/PS4PKGBridge.h"
#include "../Native/PS4PKGRuntimePlan.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

using PKG = maxps4::PS4PKGInventory;
using Access = PKG::Access;
void put(std::vector<uint8_t>& f, size_t at, uint64_t value, size_t width) {
    for (size_t i = 0; i < width; ++i) f.at(at + width - 1 - i) = static_cast<uint8_t>(value >> (i * 8));
}
std::vector<uint8_t> fixture() {
    std::vector<uint8_t> f(0x710, 0);
    std::memcpy(f.data(), "\177CNT", 4);
    put(f, 0x10, 2, 4); put(f, 0x16, 2, 2); put(f, 0x18, 0x5a0, 4);
    put(f, 0x20, 0x600, 8); put(f, 0x28, f.size() - 0x600, 8);
    put(f, 0x430, f.size(), 8);
    put(f, 0x5a0, 0x200, 4); put(f, 0x5b0, 0x600, 4); put(f, 0x5b4, 16, 4);
    put(f, 0x5c4, 1, 4); put(f, 0x5c0, 0x1000, 4); put(f, 0x5d0, 0x700, 4); put(f, 0x5d4, 16, 4);
    std::memcpy(f.data() + 0x600, "\0eboot.bin\0", 11);
    std::memcpy(f.data() + 0x700, "\0PSF", 4);
    return f;
}
int main() {
    auto f = fixture();
    size_t reads = 0, bytesRead = 0;
    auto read = [&](uint64_t offset, uint8_t* destination, size_t count) {
        ++reads; bytesRead += count;
        if (!PKG::range(offset, count, f.size())) return false;
        std::memcpy(destination, f.data() + offset, count); return true;
    };
    PKG pkg;
    std::vector<uint8_t> output;
    std::string why;
    auto parse = [&]() { return pkg.parse(f.size(), read); };
    assert(parse() && pkg.entries.size() == 2);
    assert(reads == 3 && bytesRead == PKG::headerSize + 64); // No payload read during inventory.
    assert(pkg.entries[0].access == Access::plaintext);
    assert(pkg.extract(1, read, output, why) && output.size() == 16 && output[1] == 'P');
    const auto original = f;
    std::vector<std::string> names;
    put(f, 0x5c4, 1, 4);
    assert(parse() && pkg.resolveNames(read, names, why));
    assert(names.size() == 2 && names[1] == "eboot.bin");
    // A display name cannot change the entry's extraction policy.
    put(f, 0x5c0, 0xdead, 4);
    assert(parse() && pkg.resolveNames(read, names, why));
    assert(!pkg.extract(1, read, output, why));
    for (uint32_t off : {2U, 16U, UINT32_MAX}) {
        put(f, 0x5c4, off, 4);
        assert(parse() && !pkg.resolveNames(read, names, why) && names.empty());
    }
    f = original; put(f, 0x5c4, 1, 4); f[0x609] = 'x';
    std::fill(f.begin() + 0x60a, f.begin() + 0x610, 'x');
    assert(parse() && !pkg.resolveNames(read, names, why));
    f = original; put(f, 0x5a8, 0x80000000, 4);
    assert(parse()); reads = 0;
    assert(!pkg.resolveNames(read, names, why) && reads == 0);
    f = original;
    // No magic-based bypass of a protection flag, including ELF-looking payloads.
    std::memcpy(f.data() + 0x700, "\177ELF", 4);
    put(f, 0x5c8, 0x80000000, 4);
    assert(parse() && pkg.entries[1].access == Access::encrypted);
    reads = 0;
    assert(!pkg.extract(1, read, output, why) && output.empty() && reads == 0);
    f = original;
    for (size_t at : {size_t(0x5c8), size_t(0x5cc), size_t(0x5d8)}) {
        put(f, at, 1, 4);
        assert(parse() && pkg.entries[1].access == Access::unsupported);
        assert(!pkg.extract(1, read, output, why)); f = original;
    }
    for (uint32_t id : {0x10U, 0x20U, 0x400U, 0xdeadU}) {
        put(f, 0x5c0, id, 4); assert(parse());
        assert(pkg.entries[1].access == (id == 0xdead ? Access::unknown : Access::unsupported));
        assert(!pkg.extract(1, read, output, why)); f = original;
    }
    for (uint32_t offset : {0U, 0x5a0U, 0x5dfU, 0x708U, UINT32_MAX}) {
        put(f, 0x5d0, offset, 4);
        assert(parse() && pkg.entries[1].access == Access::invalid);
        assert(!pkg.extract(1, read, output, why)); f = original;
    }
    put(f, 0x5d4, UINT32_MAX, 4);
    assert(parse() && pkg.entries[1].access == Access::invalid); f = original;
    put(f, 0x5d0, 0x608, 4); // Payload overlap.
    assert(parse() && pkg.entries[0].access == Access::invalid && pkg.entries[1].access == Access::invalid);
    f = original; put(f, 0x5c0, 0x200, 4); // Duplicate ID, disjoint payloads.
    assert(parse() && pkg.entries[0].access == Access::invalid); f = original;
    put(f, 0x410, 0x700, 8); put(f, 0x418, 16, 8); // Opaque nested filesystem.
    assert(parse() && pkg.entries[1].access == Access::unsupported); f = original;
    put(f, 0x5d4, 0, 4); assert(parse());
    assert(pkg.extract(1, read, output, why) && output.empty()); f = original;
    auto reject = [&](size_t at, uint64_t value, size_t width) {
        f = original; assert(parse()); put(f, at, value, width);
        assert(!parse() && !pkg.error.empty() && pkg.entries.empty() && pkg.fileSize == 0);
    };
    reject(0, 0, 4); reject(0x10, 0, 4); reject(0x10, 4097, 4);
    reject(0x10, UINT32_MAX, 4); reject(0x14, 3, 2); reject(0x16, 3, 2);
    reject(0x18, 0, 4); reject(0x18, 0x700, 4); reject(0x18, UINT32_MAX, 4);
    reject(0x20, UINT64_MAX, 8); reject(0x28, UINT64_MAX, 8);
    reject(0x410, UINT64_MAX, 8); reject(0x418, UINT64_MAX, 8);
    reject(0x430, original.size() + 1, 8); reject(0x430, original.size() - 1, 8);
    f = original;
    for (size_t size = 0; size < original.size(); ++size)
        assert(!pkg.parse(size, read) && pkg.entries.empty());
    // Exact source I/O failures also reject atomically; extraction clears stale data.
    assert(!pkg.parse(f.size(), [](uint64_t, uint8_t*, size_t) { return false; }));
    assert(!pkg.parse(f.size(), [&](uint64_t off, uint8_t* p, size_t n) {
        return off != 0x5c0 && read(off, p, n);
    }) && pkg.entries.empty());
    assert(parse()); output.assign(8, 42);
    assert(!pkg.extract(0, [](uint64_t, uint8_t*, size_t) { return false; }, output, why) && output.empty());
    assert(!pkg.extract(2, read, output, why));
    // Large source can be inventoried without reading/allocating its payload.
    f.resize(0x700 + PKG::extractionBudget + 1);
    put(f, 0x430, f.size(), 8); put(f, 0x28, f.size() - 0x600, 8);
    put(f, 0x5d4, PKG::extractionBudget + 1, 4);
    assert(parse() && pkg.entries[1].access == Access::plaintext);
    reads = 0; assert(!pkg.extract(1, read, output, why) && reads == 0);
    // Largest permitted descriptor table remains bounded and inventories all IDs.
    f.assign(PKG::headerSize + PKG::maxEntries * 32, 0);
    std::memcpy(f.data(), "\177CNT", 4);
    put(f, 0x10, PKG::maxEntries, 4); put(f, 0x16, PKG::maxEntries, 2);
    put(f, 0x18, PKG::headerSize, 4); put(f, 0x430, f.size(), 8);
    for (size_t i = 0; i < PKG::maxEntries; ++i)
        put(f, PKG::headerSize + i * 32, 0x2000 + i, 4);
    reads = bytesRead = 0;
    assert(parse() && pkg.entries.size() == PKG::maxEntries);
    assert(reads == PKG::maxEntries + 1 && bytesRead == f.size());
    // Deterministic malformed-descriptor sweep; any accepted inventory still
    // enforces a 1 MiB extraction cap and exact range checks.
    uint32_t seed = 0x162;
    for (size_t iteration = 0; iteration < 1000; ++iteration) {
        f = original;
        for (size_t n = 0; n < 4; ++n) {
            seed = seed * 1664525U + 1013904223U;
            f[0x5a0 + seed % 64] ^= static_cast<uint8_t>(seed >> 24);
        }
        if (parse()) {
            for (size_t i = 0; i < pkg.entries.size(); ++i) {
                const bool accepted = pkg.extract(i, read, output, why);
                assert(output.size() <= PKG::extractionBudget);
                if (accepted) assert(pkg.entries[i].access == Access::plaintext);
                else assert(output.empty());
            }
        } else assert(pkg.entries.empty());
    }
    f = original;
    // Exercise the actual C ABI used by Swift, including error/truncation paths.
    char path[] = "/tmp/maxps4-pkg-test-XXXXXX";
    const int fd = mkstemp(path); assert(fd >= 0);
    FILE* file = fdopen(fd, "wb"); assert(file);
    assert(fwrite(f.data(), 1, f.size(), file) == f.size()); assert(fclose(file) == 0);
    std::vector<char> report(8192);
    assert(maxps4_pkg_inventory_report(path, report.data(), report.size()) == 0);
    const std::string text(report.data());
    assert(text.find("MaxPS4") != std::string::npos);
    assert(text.find("eboot.bin") != std::string::npos);
    assert(text.find("Octets copiés en mémoire : 32") != std::string::npos);
    // Reproduce the two descriptor classes observed in the real Kero Blaster
    // report, without including or reading any proprietary game payload.
    auto descriptors = original;
    put(descriptors, 0x5a0, 0x100, 4); // metas
    put(descriptors, 0x5b0, 0x5a0, 4); // begins inside the descriptor table
    put(descriptors, 0x5b4, 16, 4);
    put(descriptors, 0x5c0, 0x200, 4); // entry_names
    put(descriptors, 0x5c8, 0x40000000U, 4);
    put(descriptors, 0x5d0, 0x600, 4);
    put(descriptors, 0x5d4, 16, 4);
    f = descriptors;
    assert(parse());
    assert(pkg.entries[0].access == Access::invalid);
    assert(pkg.entries[1].access == Access::unsupported);
    reads = 0;
    assert(!pkg.resolveNames(read, names, why) && reads == 0);
    assert(!pkg.extract(1, read, output, why) && output.empty());
    file = std::fopen(path, "wb"); assert(file);
    assert(fwrite(f.data(), 1, f.size(), file) == f.size());
    assert(fclose(file) == 0);
    assert(maxps4_pkg_inventory_report(path, report.data(), report.size()) == 0);
    const std::string descriptorReport(report.data());
    assert(descriptorReport.find("Diagnostic metas : début à l'offset de la table PKG ; alias possible") != std::string::npos);
    assert(descriptorReport.find("Diagnostic entry_names : table déclarée") != std::string::npos);
    f = original;
    // Malformed names cannot consume 1 MiB then restart the payload budget.
    auto large = original;
    large.resize(0x600 + PKG::extractionBudget + 16, 'x');
    std::fill(large.begin() + 0x600, large.end(), 'x');
    put(large, 0x430, large.size(), 8); put(large, 0x28, large.size() - 0x600, 8);
    put(large, 0x5b4, PKG::extractionBudget, 4);
    put(large, 0x5d0, 0x600 + PKG::extractionBudget, 4);
    put(large, 0x5c4, 1, 4); large[0x600] = 0; // Unbounded, unterminated name.
    file = std::fopen(path, "wb"); assert(file);
    assert(fwrite(large.data(), 1, large.size(), file) == large.size()); assert(fclose(file) == 0);
    assert(maxps4_pkg_inventory_report(path, report.data(), report.size()) == 0);
    const std::string capped(report.data());
    assert(capped.find("Octets inspectés (budget partagé) : 1048576") != std::string::npos);
    assert(capped.find("Octets copiés en mémoire : 0") != std::string::npos);
    char tiny[8]; assert(maxps4_pkg_inventory_report(path, tiny, sizeof(tiny)) != 0 && tiny[7] == 0);
    assert(maxps4_pkg_inventory_report(nullptr, tiny, sizeof(tiny)) != 0 && tiny[0] != 0 && tiny[7] == 0);
    assert(maxps4_pkg_inventory_report(path, nullptr, 0) != 0);
    assert(unlink(path) == 0);
    assert(maxps4_pkg_inventory_report(path, tiny, sizeof(tiny)) != 0);
    // Graph-only Orbis staging: no ELF bytes, no execution, no PFS access.
    using Plan = maxps4::PS4PKGRuntimePlan;
    std::vector<size_t> order;
    std::string graphDiagnostic;
    std::vector<Plan::ModuleRequest> modules{
        {"eboot.bin", {"lib/a.prx"}},
        {"lib/a.prx", {"lib/b.prx"}},
        {"lib/b.prx", {}}
    };
    assert(Plan::orderModules(modules, order, graphDiagnostic));
    assert((order == std::vector<size_t>{2, 1, 0}));
    modules[2].dependencies = {"eboot.bin"};
    assert(!Plan::orderModules(modules, order, graphDiagnostic) && order.empty());
    modules[2].dependencies = {"missing.prx"};
    assert(!Plan::orderModules(modules, order, graphDiagnostic) && order.empty());
    modules[2].dependencies = {};
    modules[0].dependencies = {"lib/a.prx", "lib/a.prx"};
    assert(!Plan::orderModules(modules, order, graphDiagnostic) && order.empty());
    modules[0].dependencies = {"lib/a.prx"};
    modules[1].logicalPath = "../escape.prx";
    assert(!Plan::orderModules(modules, order, graphDiagnostic) && order.empty());
    modules[1].logicalPath = "lib/a.prx";
    modules.push_back({"lib/a.prx", {}});
    assert(!Plan::orderModules(modules, order, graphDiagnostic) && order.empty());
    // Atomic staging: an invalid second module must not expose the first.
    std::vector<Plan::ModuleBytes> batch{
        {{"lib/b.prx", {}}, nullptr, 0},
        {{"eboot.bin", {"lib/b.prx"}}, nullptr, 0}
    };
    std::vector<Plan::PreparedModule> staged;
    staged.emplace_back();
    assert(!Plan::prepareModules(batch, staged, graphDiagnostic));
    assert(staged.empty());
    assert(graphDiagnostic.find("lib/b.prx") != std::string::npos);
    batch[1].request.dependencies = {"missing.prx"};
    assert(!Plan::prepareModules(batch, staged, graphDiagnostic));
    assert(staged.empty());
    assert(graphDiagnostic.find("manquante") != std::string::npos);
    // Two standalone synthetic Orbis ELF64 images, no proprietary payload.
    auto syntheticElf = [](uint16_t type, uint64_t address, uint64_t memorySize) {
        std::vector<uint8_t> elf(0x101, 0);
        std::memcpy(elf.data(), "\\177ELF\\2\\1\\1\\11", 9);
        auto le = [&](size_t at, uint64_t value, size_t width) {
            for (size_t i = 0; i < width; ++i)
                elf.at(at + i) = static_cast<uint8_t>(value >> (i * 8));
        };
        le(16, type, 2); le(18, 62, 2); le(20, 1, 4);
        le(24, address, 8); le(32, 64, 8);
        le(52, 64, 2); le(54, 56, 2); le(56, 1, 2);
        le(64, 1, 4); le(68, 5, 4);
        le(72, 0x100, 8); le(80, address, 8);
        le(96, 1, 8); le(104, memorySize, 8);
        elf[0x100] = 0xc3;
        return elf;
    };
    auto libElf = syntheticElf(0xfe18, 0x1000, 4096);
    auto mainElf = syntheticElf(0xfe00, 0x2000, 4096);
    batch = {
        {{"eboot.bin", {"lib/b.prx"}}, mainElf.data(), mainElf.size()},
        {{"lib/b.prx", {}}, libElf.data(), libElf.size()}
    };
    assert(Plan::prepareModules(batch, staged, graphDiagnostic));
    assert(staged.size() == 2);
    assert(staged[0].logicalPath == "lib/b.prx" && staged[1].logicalPath == "eboot.bin");
    assert(staged[0].image.translate(0x1000, 1, 1) &&
           *staged[0].image.translate(0x1000, 1, 1) == 0xc3);
    assert(staged[1].image.translate(0x2000, 1, 1) &&
           *staged[1].image.translate(0x2000, 1, 1) == 0xc3);
    // A 32 MiB image is individually allowed, but three exceed the batch cap.
    auto largeElf = syntheticElf(0xfe18, 0x3000, 32 * 1024 * 1024);
    batch = {
        {{"lib/a.prx", {}}, largeElf.data(), largeElf.size()},
        {{"lib/b.prx", {}}, largeElf.data(), largeElf.size()},
        {{"lib/c.prx", {}}, largeElf.data(), largeElf.size()}
    };
    assert(!Plan::prepareModules(batch, staged, graphDiagnostic));
    assert(staged.empty());
    assert(graphDiagnostic.find("64 Mio") != std::string::npos);
    std::puts("PASS PKG inventory: public layout, bounds, budgets, protection flags, ambiguity, exact I/O, C/Swift report ABI");
    std::puts("Synthetic fixtures only; no DRM, PFS extraction, PS4 executable admission or iPhone execution tested");
}
