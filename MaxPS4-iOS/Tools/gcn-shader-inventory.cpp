// gcn-shader-inventory: for each PS4 shader binary given (a game's *.sb files,
// read only), locates the GCN program by its prologue and OrbShdr binary
// information, decodes it and prints an instruction inventory. -d also prints
// the disassembly. Nothing is translated or executed.
#include "../Native/GCNDecoder.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>
using namespace maxps4::gpu;
int main(int argc, char** argv) {
    bool listing = false;
    std::map<std::string, unsigned> counts, files;
    int failures = 0;
    for (int a = 1; a < argc; ++a) {
        const std::string path = argv[a];
        if (path == "-d") { listing = true; continue; }
        std::ifstream in(path, std::ios::binary);
        const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::vector<std::uint32_t> words(bytes.size() / 4);
        for (std::size_t n = 0; n < words.size(); ++n)
            for (unsigned b = 0; b < 4; ++b) words[n] |= std::uint32_t(std::uint8_t(bytes[4 * n + b])) << (8 * b);
        GCNProgram program;
        std::size_t start = 0;
        while (start < words.size() && !locateGCNProgram(words.data() + start, words.size() - start, program)) ++start;
        if (start == words.size()) { std::printf("%s: no GCN program\n", path.c_str()); ++failures; continue; }
        const auto decoded = decodeGCN(words.data() + start, program.codeWords);
        std::printf("%s: type %u, %zu dwords at byte %zu, %zu instructions%s\n", path.c_str(), program.type,
                    program.codeWords, start * 4, decoded.instructions.size(), decoded ? "" : " DECODE ERROR");
        if (!decoded) { ++failures; continue; }
        std::map<std::string, bool> seen;
        for (const auto& i : decoded.instructions) {
            const auto text = gcnDisassemble(i);
            const auto name = text.substr(0, text.find(' '));
            ++counts[name];
            if (!seen[name]) { seen[name] = true; ++files[name]; }
            if (listing) std::printf("  %4zu  %s\n", i.word, text.c_str());
        }
    }
    std::printf("\n%zu distinct instructions (uses, files):\n", counts.size());
    for (const auto& [name, count] : counts) std::printf("  %-28s %5u %3u\n", name.c_str(), count, files[name]);
    return failures ? 1 : 0;
}
