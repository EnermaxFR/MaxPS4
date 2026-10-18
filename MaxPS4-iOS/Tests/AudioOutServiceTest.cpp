// libSceAudioOut host service: validation, PCM conversion/volume, sink
// delivery and PS4 pacing (one queued buffer, blocking Output) with a fake clock.
#include "../Native/OrbisV4AudioOutService.hpp"
#include "../Native/maxps4_orbis_v4_translation.h"
#include <cassert>
#include <iostream>

using namespace maxps4;
using S = OrbisV4AudioOutService;

int main() {
    constexpr uint64_t base = 0x400000;
    OrbisV4Translation owner([]{}); const auto page = owner.page_size();
    assert(!owner.reserve(base, page * 16) && !owner.map(base, page * 16, 6));
    {
        auto memory = owner.lease();
        struct Registry { std::map<std::string, OrbisV4HostServices::Handler> h; void implement(const std::string& n, OrbisV4HostServices::Handler f) { h[n] = std::move(f); } } services;
        auto st = S::install(services);
        uint64_t now = 1000000000; st->clock = [&] { return now; };
        std::vector<std::vector<int16_t>> delivered;
        st->sink = [&](int32_t, uint32_t frequency, const int16_t* pcm, size_t frames) { assert(frequency == 48000); delivered.emplace_back(pcm, pcm + frames * 2); };
        auto call = [&](const std::string& name, uint64_t a = 0, uint64_t b = 0, uint64_t c = 0, uint64_t d = 0, uint64_t e = 0, uint64_t f = 0) {
            OrbisV4HostServices::Registers r; r.rdi = a; r.rsi = b; r.rdx = c; r.rcx = d; r.r8 = e; r.r9 = f;
            return services.h.at(name)(r, memory);
        };
        auto value = [&](const std::string& name, uint64_t a = 0, uint64_t b = 0, uint64_t c = 0, uint64_t d = 0, uint64_t e = 0, uint64_t f = 0) {
            auto res = call(name, a, b, c, d, e, f); assert(res.outcome == OrbisV4HostServices::Outcome::Returned); return res.value;
        };
        // Title's sequence: Init, Open(0xff, MAIN, 0, 256, 48000, S16 stereo), SetVolume(0xff).
        assert(value("sceAudioOutOpen", 0xff, 0, 0, 256, 48000, 1) == S::notInit);
        assert(value("sceAudioOutInit") == 0 && value("sceAudioOutInit") == S::alreadyInit);
        assert(value("sceAudioOutOpen", 0xff, 0, 0, 255, 48000, 1) == S::invalidSize);
        assert(value("sceAudioOutOpen", 0xff, 0, 0, 256, 44100, 1) == S::invalidFrequency);
        assert(value("sceAudioOutOpen", 0xff, 0, 0, 256, 48000, 9) == S::invalidFormat);
        assert(value("sceAudioOutOpen", 0xff, 9, 0, 256, 48000, 1) == S::invalidType);
        const uint64_t port = value("sceAudioOutOpen", 0xff, 0, 0, 256, 48000, 1); assert(port == 1);
        const int32_t volumes[8] = {16384, 32768, 0, 0, 0, 0, 0, 0}; assert(!memory.write_data(base + 0x100, volumes, sizeof(volumes)));
        assert(value("sceAudioOutSetVolume", port, 0xff, base + 0x100) == 0);
        const int32_t bad[8] = {40000}; assert(!memory.write_data(base + 0x140, bad, sizeof(bad)));
        assert(value("sceAudioOutSetVolume", port, 1, base + 0x140) == S::invalidVolume);
        // One S16 stereo buffer: left channel halved by the volume.
        std::vector<int16_t> pcm(512); for (size_t i = 0; i < 256; ++i) { pcm[i * 2] = int16_t(i * 100); pcm[i * 2 + 1] = int16_t(-int(i) * 50); }
        const uint64_t buffer = base + 0x1000; assert(!memory.write_data(buffer, pcm.data(), pcm.size() * 2));
        assert(value("sceAudioOutOutput", port, buffer) == 0 && delivered.size() == 1);
        for (size_t i = 0; i < 256; ++i) {
            assert(std::abs(delivered[0][i * 2] - int(std::lround(i * 100 * 0.5f / 32768.0f * 32767.0f))) <= 1);
            assert(std::abs(delivered[0][i * 2 + 1] - int(std::lround(-int(i) * 50 / 32768.0f * 32767.0f))) <= 1);
        }
        // The second buffer fits (queue of one); the third blocks until the
        // first has played (256 frames = 5.333 ms).
        const uint64_t duration = 256ull * 1000000000ull / 48000;
        assert(value("sceAudioOutOutput", port, buffer) == 0 && delivered.size() == 2);
        auto blocked = call("sceAudioOutOutput", port, buffer);
        assert(blocked.outcome == OrbisV4HostServices::Outcome::Block && delivered.size() == 2);
        assert(blocked.deadlineNs == now + duration && !blocked.ready());
        now += duration; assert(blocked.ready() && blocked.wake(memory) == 0 && delivered.size() == 3);
        // NULL waits for the queued audio to finish, then returns without output.
        auto drain = call("sceAudioOutOutput", port, 0);
        assert(drain.outcome == OrbisV4HostServices::Outcome::Block && drain.deadlineNs == now + 2 * duration);
        now += 2 * duration; assert(drain.ready() && drain.wake(memory) == 0 && delivered.size() == 3);
        assert(value("sceAudioOutOutput", port, base + 0x10000000) == S::invalidPointer);
        assert(value("sceAudioOutOutput", 77, buffer) == S::notOpened);
        // Float 8ch: front L/R + centre folded at -3 dB.
        const uint64_t surround = value("sceAudioOutOpen", 0xff, 0, 0, 256, 48000, 5); assert(surround == 2);
        std::vector<float> f8(256 * 8, 0.0f); for (size_t i = 0; i < 256; ++i) { f8[i * 8] = 0.25f; f8[i * 8 + 1] = -0.25f; f8[i * 8 + 2] = 0.5f; }
        assert(!memory.write_data(buffer, f8.data(), f8.size() * 4));
        assert(value("sceAudioOutOutput", surround, buffer) == 0);
        assert(std::abs(delivered.back()[0] - int(std::lround((0.25f + 0.5f * 0.7071f) * 32767.0f))) <= 1);
        assert(st->ports.at(int32_t(port)).buffers == 3 && st->ports.at(int32_t(port)).nonSilentFrames == 3 * 255);
        assert(value("sceAudioOutClose", port) == 0 && value("sceAudioOutClose", port) == S::notOpened);
    }
    owner.close();
    std::cout << "PASS audio out: init/open validation, S16/float 1/2/8ch conversion, per-channel volume, sink delivery, "
                 "one-buffer queue with blocking Output at 48 kHz, NULL drain, refusals\n";
}
