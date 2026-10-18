#pragma once
// Ubuntu host audio for the AudioOut service: optional WAV capture (exact
// samples the title produced) and live playback through libpulse-simple,
// loaded at run time (no build dependency; absent library = capture only).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <string>

namespace maxps4 {
class LinuxAudioSink {
public:
    ~LinuxAudioSink() { close(); }
    bool openWav(const std::string& path, uint32_t frequency) {
        wav_ = std::fopen(path.c_str(), "wb"); frequency_ = frequency;
        if (!wav_) return false;
        uint8_t header[44] = {}; std::fwrite(header, 1, sizeof(header), wav_); // patched in close()
        return true;
    }
    // Returns false (with the reason) when PulseAudio cannot be used.
    bool openPulse(uint32_t frequency, std::string& why) {
        library_ = dlopen("libpulse-simple.so.0", RTLD_NOW | RTLD_LOCAL);
        if (!library_) { why = dlerror(); return false; }
        newFn_ = reinterpret_cast<New>(dlsym(library_, "pa_simple_new"));
        writeFn_ = reinterpret_cast<Write>(dlsym(library_, "pa_simple_write"));
        freeFn_ = reinterpret_cast<Free>(dlsym(library_, "pa_simple_free"));
        if (!newFn_ || !writeFn_ || !freeFn_) { why = "libpulse-simple symbols missing"; return false; }
        struct { int format; uint32_t rate; uint8_t channels; } spec{3 /* PA_SAMPLE_S16LE */, frequency, 2};
        int error = 0;
        pulse_ = newFn_(nullptr, "MaxPS4", 1 /* PA_STREAM_PLAYBACK */, nullptr, "Kero Blaster", &spec, nullptr, nullptr, &error);
        if (!pulse_) { why = "pa_simple_new failed (error " + std::to_string(error) + ")"; return false; }
        frequency_ = frequency; return true;
    }
    void write(const int16_t* stereo, size_t frames) {
        if (wav_) { std::fwrite(stereo, 4, frames, wav_); frames_ += frames; }
        if (pulse_) { int error = 0; if (writeFn_(pulse_, stereo, frames * 4, &error) < 0) ++pulseErrors_; }
    }
    uint64_t frames() const { return frames_; }
    uint64_t pulseErrors() const { return pulseErrors_; }
    bool playing() const { return pulse_ != nullptr; }
    void close() {
        if (pulse_) { freeFn_(pulse_); pulse_ = nullptr; }
        if (library_) { dlclose(library_); library_ = nullptr; }
        if (wav_) {
            const uint32_t data = uint32_t(frames_ * 4), riff = 36 + data, fmtSize = 16, rate = frequency_, byteRate = rate * 4;
            const uint16_t pcm = 1, channels = 2, align = 4, bits = 16;
            std::fseek(wav_, 0, SEEK_SET);
            std::fwrite("RIFF", 1, 4, wav_); std::fwrite(&riff, 4, 1, wav_); std::fwrite("WAVEfmt ", 1, 8, wav_);
            std::fwrite(&fmtSize, 4, 1, wav_); std::fwrite(&pcm, 2, 1, wav_); std::fwrite(&channels, 2, 1, wav_);
            std::fwrite(&rate, 4, 1, wav_); std::fwrite(&byteRate, 4, 1, wav_); std::fwrite(&align, 2, 1, wav_);
            std::fwrite(&bits, 2, 1, wav_); std::fwrite("data", 1, 4, wav_); std::fwrite(&data, 4, 1, wav_);
            std::fclose(wav_); wav_ = nullptr;
        }
    }
private:
    using New = void* (*)(const char*, const char*, int, const char*, const char*, const void*, const void*, const void*, int*);
    using Write = int (*)(void*, const void*, size_t, int*);
    using Free = void (*)(void*);
    void* library_ = nullptr; void* pulse_ = nullptr; New newFn_ = nullptr; Write writeFn_ = nullptr; Free freeFn_ = nullptr;
    std::FILE* wav_ = nullptr; uint32_t frequency_ = 48000; uint64_t frames_ = 0, pulseErrors_ = 0;
};
}
