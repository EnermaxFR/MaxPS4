#pragma once
// libSceAudioOut on the host: real PCM consumption at the port's rate.
// Output copies the title's buffer, applies the port volume, converts it to
// interleaved S16 stereo at the port frequency and hands it to a host sink
// (PulseAudio on Ubuntu, Core Audio on iOS, WAV capture for verification).
// As on PS4, Output returns once the previous buffer has been consumed: a
// port holds at most one queued buffer and the calling thread blocks
// (scheduler wait with a deadline) until the queue has room.
#include "OrbisV4HostServices.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace maxps4 {
struct OrbisV4AudioOutService {
    using R = OrbisV4HostServices::Registers; using M = OrbisV4HostServices::Memory;
    using Result = OrbisV4HostServices::Result; using O = OrbisV4HostServices::Outcome;
    static constexpr uint32_t notOpened = 0x80260001, invalidPort = 0x80260003, invalidPointer = 0x80260004,
        invalidSize = 0x80260006, invalidFormat = 0x80260007, invalidFrequency = 0x80260008, invalidVolume = 0x80260009,
        invalidType = 0x8026000a, alreadyInit = 0x8026000e, notInit = 0x8026000f, portFull = 0x80260010;
    static constexpr int32_t volume0dB = 32768;
    struct Port {
        uint32_t type = 0, length = 0, frequency = 48000, format = 0, channels = 2; bool floating = false;
        std::array<int32_t, 8> volume{volume0dB, volume0dB, volume0dB, volume0dB, volume0dB, volume0dB, volume0dB, volume0dB};
        uint64_t playEndNs = 0;       // when the queued audio finishes playing
        uint64_t buffers = 0, frames = 0, nonSilentFrames = 0;
        double sumSquares = 0;        // of the converted output, for diagnostics
    };
    struct State {
        bool initialized = false;
        std::map<int32_t, Port> ports; int32_t nextHandle = 1;
        // Host sink: interleaved S16 stereo frames at 'frequency'. Optional.
        std::function<void(int32_t port, uint32_t frequency, const int16_t* stereo, size_t frames)> sink;
        std::function<uint64_t()> clock = [] {
            return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); };
    };
    static Result value(uint64_t v) { return {O::Returned, v}; }
    static bool formatOf(uint32_t param, Port& p) {
        switch (param & 0xff) { // bits 16+ select the 8ch layout (STD vs legacy): same frame size
        case 0: p.channels = 1; p.floating = false; return true;
        case 1: p.channels = 2; p.floating = false; return true;
        case 2: p.channels = 8; p.floating = false; return true;
        case 3: p.channels = 1; p.floating = true; return true;
        case 4: p.channels = 2; p.floating = true; return true;
        case 5: p.channels = 8; p.floating = true; return true;
        case 6: p.channels = 8; p.floating = false; return true; // S16 8CH_STD
        case 7: p.channels = 8; p.floating = true; return true;  // FLOAT 8CH_STD
        default: return false;
        }
    }
    static size_t frameBytes(const Port& p) { return size_t(p.channels) * (p.floating ? 4 : 2); }
    // Converts one buffer to S16 stereo; 8ch keeps front left/right (+ centre/LFE folded).
    static void convert(const Port& p, const uint8_t* in, std::vector<int16_t>& out) {
        out.resize(size_t(p.length) * 2);
        auto sample = [&](size_t frame, unsigned ch) -> float {
            const uint8_t* at = in + frame * frameBytes(p) + ch * (p.floating ? 4 : 2);
            if (p.floating) { float f; std::memcpy(&f, at, 4); return std::isfinite(f) ? f : 0.0f; }
            int16_t s; std::memcpy(&s, at, 2); return s / 32768.0f;
        };
        for (size_t f = 0; f < p.length; ++f) {
            float l, r;
            if (p.channels == 1) l = r = sample(f, 0);
            else if (p.channels == 2) { l = sample(f, 0); r = sample(f, 1); }
            else { const float c = sample(f, 2) * 0.7071f; l = sample(f, 0) + c; r = sample(f, 1) + c; }
            const float gl = p.volume[0] / float(volume0dB), gr = p.volume[p.channels == 1 ? 0 : 1] / float(volume0dB);
            auto s16 = [](float v) { return int16_t(std::lround(std::min(1.0f, std::max(-1.0f, v)) * 32767.0f)); };
            out[f * 2] = s16(l * gl); out[f * 2 + 1] = s16(r * gr);
        }
    }
    template <class Services> // anything with implement(name, handler)
    static std::shared_ptr<State> install(Services& services) {
        auto st = std::make_shared<State>();
        services.implement("sceAudioOutInit", [st](const R&, M&) {
            if (st->initialized) return value(alreadyInit);
            st->initialized = true; return value(0); });
        // sceAudioOutOpen(userId, type, index, len, freq, param) -> handle
        services.implement("sceAudioOutOpen", [st](const R& r, M&) {
            if (!st->initialized) return value(notInit);
            const uint32_t type = uint32_t(r.rsi), index = uint32_t(r.rdx), length = uint32_t(r.rcx), frequency = uint32_t(r.r8);
            if (!(type == 0 || type == 1 || type == 2 || type == 3 || type == 4 || type == 127) || index) return value(invalidType);
            if (length < 256 || length > 2048 || length % 256) return value(invalidSize);
            if (frequency != 48000) return value(invalidFrequency);
            Port p; p.type = type; p.length = length; p.frequency = frequency; p.format = uint32_t(r.r9);
            if (!formatOf(p.format, p)) return value(invalidFormat);
            const int32_t handle = st->nextHandle++;
            st->ports[handle] = p; return value(uint64_t(handle)); });
        // sceAudioOutSetVolume(handle, channel mask, const int32_t vol[8])
        services.implement("sceAudioOutSetVolume", [st](const R& r, M& m) {
            auto it = st->ports.find(int32_t(r.rdi)); if (it == st->ports.end()) return value(notOpened);
            const uint32_t mask = uint32_t(r.rsi); if (!r.rdx) return value(invalidPointer);
            int32_t vol[8] = {}; const unsigned count = unsigned(std::min<uint32_t>(8, 32 - __builtin_clz(mask | 1)));
            if (m.read_data(r.rdx, vol, count * 4)) return value(invalidPointer);
            for (unsigned c = 0; c < count; ++c) if ((mask >> c) & 1) { if (vol[c] < 0 || vol[c] > volume0dB) return value(invalidVolume); }
            for (unsigned c = 0; c < count; ++c) if ((mask >> c) & 1) it->second.volume[c] = vol[c];
            return value(0); });
        services.implement("sceAudioOutClose", [st](const R& r, M&) {
            if (!st->ports.erase(int32_t(r.rdi))) return value(notOpened);
            return value(0); });
        // sceAudioOutOutput(handle, const void* buffer): queue one buffer; NULL
        // waits for the queued audio to finish.
        services.implement("sceAudioOutOutput", [st](const R& r, M& m) {
            auto it = st->ports.find(int32_t(r.rdi)); if (it == st->ports.end()) return value(notOpened);
            Port& port = it->second;
            // MAXPS4_AUDIO_TIME_SCALE=N (default 1): each buffer counts as N times
            // its real duration. When emulation runs far slower than real time the
            // audio thread is never ahead and never waits, taking CPU time from the
            // game; a larger scale makes it wait (the sound is not real time anyway).
            static const uint64_t timeScale = [] {
                const char* e = std::getenv("MAXPS4_AUDIO_TIME_SCALE");
                const unsigned long v = e ? std::strtoul(e, nullptr, 10) : 1;
                return uint64_t(v >= 1 && v <= 1000 ? v : 1);
            }();
            const uint64_t duration = uint64_t(port.length) * 1000000000ull / port.frequency * timeScale;
            const uint64_t now = st->clock();
            const int32_t handle = int32_t(r.rdi); const uint64_t buffer = r.rsi;
            const size_t bytes = size_t(port.length) * frameBytes(port);
            if (buffer && !m.permitted(buffer, bytes, 4)) return value(invalidPointer);
            State* sp = st.get();
            auto queue = [sp, handle, buffer, bytes, duration](M& mem) -> uint64_t {
                auto found = sp->ports.find(handle); if (found == sp->ports.end()) return notOpened;
                Port& p = found->second;
                if (!buffer) return 0;
                std::vector<uint8_t> pcm(bytes);
                if (mem.read_data(buffer, pcm.data(), bytes)) return invalidPointer;
                std::vector<int16_t> stereo; convert(p, pcm.data(), stereo);
                for (size_t f = 0; f < p.length; ++f) {
                    const double l = stereo[f * 2], rr = stereo[f * 2 + 1];
                    p.sumSquares += l * l + rr * rr; if (stereo[f * 2] || stereo[f * 2 + 1]) ++p.nonSilentFrames;
                }
                ++p.buffers; p.frames += p.length;
                if (sp->sink) sp->sink(handle, p.frequency, stereo.data(), p.length);
                p.playEndNs = std::max(sp->clock(), p.playEndNs) + duration;
                return 0;
            };
            // Room for one buffer once the queue ends within one buffer duration.
            const uint64_t ready = buffer ? (port.playEndNs > duration ? port.playEndNs - duration : 0) : port.playEndNs;
            if (ready <= now) return value(queue(m));
            Result b{O::Block, 0, "audio output"}; b.deadlineNs = ready;
            b.ready = [sp, ready] { return sp->clock() >= ready; };
            b.wake = queue;
            return b; });
        return st;
    }
};
}
