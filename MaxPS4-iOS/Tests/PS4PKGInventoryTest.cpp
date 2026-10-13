#include "../Native/PS4PKGInventory.hpp"
#include "../Native/PS4PKGBridge.h"
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
    assert(text.find("Bilan des descripteurs : lisibles déclarés=2 ; chiffrés déclarés=0") != std::string::npos);
    assert(text.find("inconnus=0 ; invalides=0") != std::string::npos);
    assert(text.find("Octets copiés en mémoire : 32") != std::string::npos);
    // A plaintext ELF magic with only 16 bytes must be diagnosed as truncated,
    // never accepted as an executable or dispatched to FEX.
    auto shortElf = original;
    std::memcpy(shortElf.data() + 0x700, "\177ELF", 4);
    file = std::fopen(path, "wb"); assert(file);
    assert(fwrite(shortElf.data(), 1, shortElf.size(), file) == shortElf.size());
    assert(fclose(file) == 0);
    assert(maxps4_pkg_inventory_report(path, report.data(), report.size()) == 0);
    assert(std::string(report.data()).find("ELF tronqué : en-tête ELF64 incomplet") != std::string::npos);
    // ELF64 identity alone does not prove a usable PS4 executable.
    auto wrongElf = original;
    wrongElf.resize(0x740, 0);
    put(wrongElf, 0x430, wrongElf.size(), 8);
    put(wrongElf, 0x28, wrongElf.size() - 0x600, 8);
    put(wrongElf, 0x5d4, 64, 4);
    std::memcpy(wrongElf.data() + 0x700, "\177ELF", 4);
    wrongElf[0x704] = 2; wrongElf[0x705] = 1; wrongElf[0x706] = 1;
    wrongElf[0x710] = 2; // ET_EXEC
    wrongElf[0x712] = 0x3e; // EM_X86_64
    wrongElf[0x714] = 1; // ELF version
    wrongElf[0x734] = 64; // ELF64 e_ehsize
    file = std::fopen(path, "wb"); assert(file);
    assert(fwrite(wrongElf.data(), 1, wrongElf.size(), file) == wrongElf.size());
    assert(fclose(file) == 0);
    assert(maxps4_pkg_inventory_report(path, report.data(), report.size()) == 0);
    assert(std::string(report.data()).find("en-tête ELF64 x86-64 reconnu") != std::string::npos);
    wrongElf[0x712] = 3; // Unsupported machine: reject despite ELF magic.
    file = std::fopen(path, "wb"); assert(file);
    assert(fwrite(wrongElf.data(), 1, wrongElf.size(), file) == wrongElf.size());
    assert(fclose(file) == 0);
    assert(maxps4_pkg_inventory_report(path, report.data(), report.size()) == 0);
    assert(std::string(report.data()).find("ELF incompatible : machine x86-64") != std::string::npos);
    // Reject an ELF64 header with a non-executable e_type even when its
    // architecture, version and header length are otherwise valid.
    wrongElf[0x712] = 0x3e;
    wrongElf[0x710] = 1; // ET_REL: not admitted by this read-only probe.
    file = std::fopen(path, "wb"); assert(file);
    assert(fwrite(wrongElf.data(), 1, wrongElf.size(), file) == wrongElf.size());
    assert(fclose(file) == 0);
    assert(maxps4_pkg_inventory_report(path, report.data(), report.size()) == 0);
    assert(std::string(report.data()).find("ELF incompatible : type exécutable/dynamique non reconnu") != std::string::npos);
    wrongElf[0x710] = 2;
    // A program-header table outside the bounded plaintext resource is invalid.
    wrongElf[0x738] = 56; // e_phentsize
    wrongElf[0x73a] = 1;  // e_phnum
    wrongElf[0x720] = 0x40; // e_phoff=64, past the 64-byte resource
    file = std::fopen(path, "wb"); assert(file);
    assert(fwrite(wrongElf.data(), 1, wrongElf.size(), file) == wrongElf.size());
    assert(fclose(file) == 0);
    assert(maxps4_pkg_inventory_report(path, report.data(), report.size()) == 0);
    assert(std::string(report.data()).find("ELF invalide : table de segments absente ou hors ressource") != std::string::npos);
    wrongElf[0x738] = 0; wrongElf[0x73a] = 0; wrongElf[0x720] = 0;
    // Protected entries must never be inspected as ELF, even if their bytes
    // happen to start with the ELF magic.
    wrongElf[0x712] = 0x3e;
    put(wrongElf, 0x5c8, 0x80000000U, 4);
    file = std::fopen(path, "wb"); assert(file);
    assert(fwrite(wrongElf.data(), 1, wrongElf.size(), file) == wrongElf.size());
    assert(fclose(file) == 0);
    assert(maxps4_pkg_inventory_report(path, report.data(), report.size()) == 0);
    const std::string protectedReport(report.data());
    assert(protectedReport.find("chiffrés déclarés=1") != std::string::npos);
    assert(protectedReport.find("en-tête ELF64 x86-64 reconnu") == std::string::npos);
    assert(protectedReport.find("signature ELF candidate") == std::string::npos);
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
    assert(maxps4_pkg_inventory_report(nullptr, tiny, sizeof(tiny)) != 0 && tiny[0] == 0);
    assert(maxps4_pkg_inventory_report(path, nullptr, 0) != 0);
    assert(unlink(path) == 0);
    assert(maxps4_pkg_inventory_report(path, tiny, sizeof(tiny)) != 0);
    std::puts("PASS PKG inventory: public layout, bounds, budgets, protection flags, ambiguity, exact I/O, C/Swift report ABI");
    std::puts("Synthetic fixtures only; no DRM, PFS extraction, PS4 executable admission or iPhone execution tested");
}
