// Host command (Linux, FEX linked build): how far a game boots in the engine.
// Usage: ps4-boot-probe <game directory> [frame directory]
// Loads <dir>/sce_module/*.prx then <dir>/eboot.bin, links them with the
// engine's HLE (prepare_orbis_general) in import probe mode, so that every
// missing import is a stub reported on stderr at its first call, mounts <dir> as /app0 and runs the
// entry point through FEX with the software GPU backend (each submission is
// executed on the CPU by the GCN interpreter, render targets written linearly).
// Prints the link diagnostic, the run status and
// the guest fault. A display thread sends 60 Hz vblanks and completes every
// flip; with a frame directory, it saves one presented frame in 30 there as
// frame-NNNN.png (at most 100). Game files are only read.
#include "../Native/OrbisMetadata.hpp"
#include "../Native/maxps4_fex_runtime.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
struct Module {
    std::string name;
    std::vector<uint8_t> bytes;
    maxps4::OrbisMetadata metadata;
    std::vector<std::string> dependencies;
    std::vector<const char*> pointers;
};
bool read(const std::filesystem::path& path, Module& module) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { std::fprintf(stderr, "%s : lecture impossible\n", path.c_str()); return false; }
    module.bytes.assign(std::istreambuf_iterator<char>(in), {});
    module.name = path.filename().string();
    if (!module.metadata.parse(module.bytes.data(), module.bytes.size())) {
        std::fprintf(stderr, "%s : %s\n", module.name.c_str(), module.metadata.error.c_str());
        return false;
    }
    return true;
}
// PNG with stored (uncompressed) deflate blocks: RGBA8, no dependency.
uint32_t crc(const uint8_t* p, size_t n, uint32_t c = 0xffffffffU) {
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xedb88320U & (0U - (c & 1U)));
    }
    return c;
}
void be32(std::vector<uint8_t>& out, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) out.push_back(uint8_t(v >> s));
}
void chunk(std::ofstream& f, const char* type, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> head;
    be32(head, uint32_t(data.size()));
    head.insert(head.end(), type, type + 4);
    uint32_t c = crc(head.data() + 4, 4);
    c = crc(data.data(), data.size(), c) ^ 0xffffffffU;
    std::vector<uint8_t> tail;
    be32(tail, c);
    f.write((const char*)head.data(), head.size());
    f.write((const char*)data.data(), data.size());
    f.write((const char*)tail.data(), tail.size());
}
bool write_png(const std::string& path, const maxps4_videoout_frame& frame, const std::vector<uint8_t>& pixels) {
    const uint32_t w = frame.width, h = frame.height;
    if (!w || !h || uint64_t(frame.pitch_in_pixels) * 4 * h > pixels.size()) return false;
    const bool bgra = frame.pixel_format == 0x80000000U;
    std::vector<uint8_t> raw;
    raw.reserve(size_t(h) * (w * 4 + 1));
    for (uint32_t y = 0; y < h; ++y) {
        raw.push_back(0); // Filter: none.
        const uint8_t* row = pixels.data() + size_t(y) * frame.pitch_in_pixels * 4;
        for (uint32_t x = 0; x < w; ++x) {
            const uint8_t* p = row + x * 4;
            raw.push_back(bgra ? p[2] : p[0]); raw.push_back(p[1]);
            raw.push_back(bgra ? p[0] : p[2]); raw.push_back(255);
        }
    }
    std::vector<uint8_t> z{0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (size_t at = 0; at < raw.size() || at == 0; ) {
        const size_t n = std::min<size_t>(65535, raw.size() - at);
        z.push_back(at + n == raw.size() ? 1 : 0);
        z.push_back(uint8_t(n)); z.push_back(uint8_t(n >> 8));
        z.push_back(uint8_t(~n)); z.push_back(uint8_t(~n >> 8));
        z.insert(z.end(), raw.begin() + at, raw.begin() + at + n);
        at += n;
        if (at == raw.size()) break;
    }
    for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
    be32(z, (b << 16) | a);
    std::vector<uint8_t> ihdr;
    be32(ihdr, w); be32(ihdr, h);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    std::ofstream f(path, std::ios::binary);
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    f.write((const char*)signature, 8);
    chunk(f, "IHDR", ihdr);
    chunk(f, "IDAT", z);
    chunk(f, "IEND", {});
    return bool(f);
}
// Stands in for the display: vblanks, flip completion, saved frames.
void display(maxps4_fex_engine* engine, const std::string& directory, std::atomic<bool>& done) {
    uint64_t flips = 0;
    unsigned saved = 0;
    std::vector<uint8_t> pixels;
    while (!done) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        maxps4_fex_engine_videoout_vblank(engine, 1);
        maxps4_videoout_frame frame{};
        if (maxps4_fex_engine_videoout_snapshot(engine, &frame, nullptr, 0)) continue;
        pixels.resize(frame.byte_count);
        if (maxps4_fex_engine_videoout_snapshot(engine, &frame, pixels.data(), pixels.size())) continue;
        if (flips % 30 == 0 && !directory.empty() && saved < 100) {
            char name[32];
            std::snprintf(name, sizeof name, "/frame-%04u.png", saved++);
            const bool ok = write_png(directory + name, frame, pixels);
            std::fprintf(stderr, "flip %llu : %ux%u format 0x%x%s -> %s%s\n", (unsigned long long)flips,
                frame.width, frame.height, frame.pixel_format, frame.tiled ? " tiled" : "", name + 1, ok ? "" : " (échec)");
            uint32_t draws = 0, rendered = 0, failed = 0;
            char error[160] = {};
            maxps4_fex_engine_gnm_software_status(engine, &draws, &rendered, &failed, error, sizeof error);
            std::fprintf(stderr, "GPU logiciel : %u dessins, %u rendus, %u échecs%s%s\n", draws, rendered, failed,
                error[0] ? " ; premier : " : "", error);
        }
        ++flips;
        maxps4_fex_engine_videoout_complete(engine, frame.ticket, 1);
    }
    std::fprintf(stderr, "%llu image(s) présentée(s)\n", (unsigned long long)flips);
}
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fputs("usage: ps4-boot-probe <dossier du jeu> [millisecondes]\n", stderr); return 2; }
    const std::filesystem::path root = std::filesystem::absolute(argv[1]);
    const std::string frames = argc > 2 ? argv[2] : "";
    if (!frames.empty()) std::filesystem::create_directories(frames);
    // Providers first: the linker requires a module's dependencies before it.
    std::vector<Module> modules;
    std::vector<std::filesystem::path> prx;
    if (std::filesystem::is_directory(root / "sce_module"))
        for (const auto& entry : std::filesystem::directory_iterator(root / "sce_module"))
            if (entry.path().extension() == ".prx") prx.push_back(entry.path());
    std::sort(prx.begin(), prx.end());
    for (const auto& path : prx) { modules.emplace_back(); if (!read(path, modules.back())) return 1; }
    modules.emplace_back();
    if (!read(root / "eboot.bin", modules.back())) return 1;
    // Same placement as ps4-orbis-analyze: 1 MiB aligned, 1 MiB apart.
    std::vector<maxps4_orbis_module> table;
    uint64_t bias = 0x400000;
    for (auto& m : modules) {
        for (const auto& needed : m.metadata.needed)
            for (const auto& other : modules)
                if (&other != &m && other.name == needed) m.dependencies.push_back(needed);
        for (const auto& d : m.dependencies) m.pointers.push_back(d.c_str());
        table.push_back({m.name.c_str(), m.bytes.data(), m.bytes.size(), bias,
                         m.pointers.data(), m.pointers.size()});
        uint64_t end = 0;
        for (const auto& s : m.metadata.segments)
            if (s.type == 1 || s.type == 0x61000010) end = std::max(end, s.address + s.memsz);
        std::printf("module %-24s base 0x%llx, %zu dépendance(s)\n", m.name.c_str(),
                    (unsigned long long)bias, m.dependencies.size());
        bias += ((end + 0xfffff) & ~uint64_t{0xfffff}) + 0x100000;
    }
    maxps4_fex_engine* engine = nullptr;
    if (int e = maxps4_fex_engine_open(&engine)) { std::printf("ouverture FEX refusée (%d)\n", e); return 1; }
    maxps4_fex_engine_set_import_probe(engine, 1);
    char diagnostic[1024] = {};
    uint64_t relocation = 0;
    const int prepared = maxps4_fex_engine_prepare_orbis_general(engine, table.data(), table.size(),
        "eboot.bin", &relocation, diagnostic, sizeof diagnostic);
    std::printf("\nliaison : %d (%s)\n%s\n", prepared, prepared ? std::strerror(-prepared) : "ok", diagnostic);
    if (prepared) { maxps4_fex_engine_close(engine); return 1; }
    std::printf("relocation 0x%llx\n", (unsigned long long)relocation);
    if (int64_t e = maxps4_fex_engine_mount_app0(engine, root.c_str())) std::printf("montage /app0 : %lld\n", (long long)e);
    // Software GPU: submissions are drawn on the CPU, then signal their labels, EOP events and flips.
    if (int e = maxps4_fex_engine_gnm_backend(engine, 3)) std::printf("GPU logiciel refusé (%d)\n", e);
    if (int e = maxps4_fex_engine_initialize(engine)) { std::printf("JIT FEX indisponible (%d)\n", e); return 1; }
    uint64_t value = 0, entry = 0;
    uint32_t backend = 0;
    std::fflush(stdout);
    std::atomic<bool> done{false};
    std::thread screen(display, engine, frames, std::ref(done));
    const int status = maxps4_fex_engine_run_orbis(engine, &value, &backend, &entry);
    done = true;
    screen.join();
    std::printf("\nexécution : %d (%s), valeur %llu, backend %u, entrée 0x%llx\n", status,
        status ? std::strerror(-status) : "ok", (unsigned long long)value, backend, (unsigned long long)entry);
    maxps4_fex_cpu_report report{};
    if (!maxps4_fex_engine_last_cpu_report(engine, &report))
        std::printf("sortie du processus : %u, statut %d\n", report.exited, report.exit_status);
    maxps4_fex_guest_fault fault{};
    if (!maxps4_fex_engine_last_guest_fault(engine, &fault))
        std::printf("faute invitée : signal %d, adresse 0x%llx, rip 0x%llx\n", fault.signal,
            (unsigned long long)fault.address, (unsigned long long)fault.rip);
    maxps4_fex_engine_close(engine);
    return status ? 1 : 0;
}
