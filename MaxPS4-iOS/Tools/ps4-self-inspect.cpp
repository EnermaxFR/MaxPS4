// Host command: prints the read-only SELF block report for one eboot.bin/PRX.
// Usage: ps4-self-inspect <file>. Reads at most 64 MiB; never writes.
#include "../Native/PS4SELFBlockReport.hpp"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 2) { std::fputs("usage: ps4-self-inspect <eboot.bin|module.prx>\n", stderr); return 2; }
    std::FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::perror(argv[1]); return 1; }
    std::vector<uint8_t> bytes(64 * 1024 * 1024 + 1);
    const size_t size = std::fread(bytes.data(), 1, bytes.size(), f);
    const bool failed = std::ferror(f);
    std::fclose(f);
    if (failed) { std::fputs("lecture incomplète\n", stderr); return 1; }
    if (size == bytes.size()) { std::fputs("fichier supérieur à 64 Mio refusé\n", stderr); return 1; }
    const auto report = maxps4::PS4SELFBlockReport::inspect(bytes.data(), size);
    std::fputs(report.text().c_str(), stdout);
    return report.error ? 1 : 0;
}
