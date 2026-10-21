// maxps4_game_run: the app's entry into OrbisV4GameSession.hpp.
#include "maxps4_game_session.h"
#include "OrbisV4GameSession.hpp"
#include "maxps4_jit_memory.h"
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#include <cerrno>
#include <fstream>
#include <iterator>
#include <sstream>
#include <streambuf>

namespace {
// Diagnostics stream that hands complete lines to the app's log callback.
class LineBuffer final : public std::streambuf {
public:
    explicit LineBuffer(const maxps4_game_callbacks& c) : c_(c) {}
    ~LineBuffer() override { flushLine(); }
protected:
    int_type overflow(int_type ch) override {
        if (ch == traits_type::eof()) return traits_type::not_eof(ch);
        if (ch == '\n') flushLine(); else line_.push_back(char(ch));
        return ch;
    }
private:
    void flushLine() { if (!line_.empty() && c_.log) c_.log(c_.context, line_.c_str()); line_.clear(); }
    const maxps4_game_callbacks& c_;
    std::string line_;
};
bool slurp(const std::string& path, std::vector<uint8_t>& bytes) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    bytes.assign(std::istreambuf_iterator<char>(f), {});
    return !bytes.empty();
}
} // namespace

extern "C" int maxps4_game_run(const char* game_dir, const char* names_tsv, const maxps4_game_callbacks* callbacks,
                               const volatile int32_t* cancel) {
    if (!game_dir || !names_tsv || !callbacks || !callbacks->present) return -EINVAL;
    using namespace maxps4;
    OrbisV4GameFiles game;
    {
        std::ifstream t(names_tsv); std::string line;
        if (!t || !std::getline(t, line)) return -ENOENT;
        while (std::getline(t, line)) {
            std::vector<std::string> f; std::stringstream ss(line); std::string x;
            while (std::getline(ss, x, '\t')) f.push_back(x);
            if (f.size() > 8 && !f[8].empty() && f[8] != "UNKNOWN") game.names[f[7]] = f[8];
        }
    }
    const std::string dir = game_dir;
    for (const char* path : {"eboot.bin", "sce_module/libc.prx", "sce_module/libSceFios2.prx"}) {
        OrbisV4ProcessBatch::File file;
        const std::string p = path; file.name = p.substr(p.find_last_of('/') + 1);
        if (!slurp(dir + "/" + p, file.bytes)) return -ENOENT;
        game.modules.push_back(std::move(file));
    }
    game.app0 = dir;
    LineBuffer lines(*callbacks); std::ostream log(&lines);
    OrbisV4GameHost host{log};
    const maxps4_game_callbacks c = *callbacks;
    host.present = [c](uint32_t w, uint32_t h, const std::vector<uint8_t>& rgba, uint64_t, uint64_t, uint32_t& buttons, std::string& error) {
        if (rgba.size() < size_t(w) * h * 4) { error = "short frame"; return false; }
        if (c.present(c.context, w, h, rgba.data(), &buttons)) { error = "presentation stopped by the app"; return false; }
        return true;
    };
    if (c.audio) host.audio = [c](const int16_t* pcm, size_t frames) { c.audio(c.context, pcm, frames); };
#if defined(__APPLE__) && TARGET_OS_IPHONE
    // Verify the StikDebug-prepared dispatcher/cache regions (nonce, Mach
    // W^X aliases, native canary) before FEX emits code into them.
    host.prepareJIT = [] { return int(maxps4_jit_prepare()); };
#endif
    std::atomic<bool> stop{false};
    host.cancel = &stop;
    std::thread watch;
    if (cancel) watch = std::thread([&] { while (!stop.load()) { if (*cancel) { stop = true; break; }
                                          std::this_thread::sleep_for(std::chrono::milliseconds(50)); } });
    const int status = runOrbisV4Game(game, host);
    if (watch.joinable()) { stop = true; watch.join(); }
    log.flush();
    return status;
}
