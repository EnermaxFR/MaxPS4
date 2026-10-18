#pragma once
// Reproducible controller input by presented-frame number, for automated
// tests in the real title: "120-125:OPTIONS;300-340:RIGHT+CROSS".
// Buttons use SCE_PAD_BUTTON_* bits; frames are 1-based and inclusive.
#include <cstdint>
#include <string>
#include <vector>

namespace maxps4 {
struct PadInputScript {
    struct Step { uint64_t first = 0, last = 0; uint32_t buttons = 0; };
    std::vector<Step> steps;
    static uint32_t button(const std::string& name) {
        static const struct { const char* name; uint32_t bit; } table[] = {
            {"L3", 0x2}, {"R3", 0x4}, {"OPTIONS", 0x8}, {"UP", 0x10}, {"RIGHT", 0x20}, {"DOWN", 0x40}, {"LEFT", 0x80},
            {"L2", 0x100}, {"R2", 0x200}, {"L1", 0x400}, {"R1", 0x800}, {"TRIANGLE", 0x1000}, {"CIRCLE", 0x2000},
            {"CROSS", 0x4000}, {"SQUARE", 0x8000}, {"TOUCHPAD", 0x100000}};
        for (const auto& b : table) if (name == b.name) return b.bit;
        return 0;
    }
    // Returns false (with the offending item) on a malformed script.
    bool parse(const std::string& text, std::string& error) {
        steps.clear(); size_t at = 0;
        while (at < text.size()) {
            const size_t end = std::min(text.find(';', at), text.size());
            const std::string item = text.substr(at, end - at); at = end + 1;
            if (item.empty()) continue;
            const size_t colon = item.find(':');
            if (colon == std::string::npos) { error = "missing ':' in '" + item + "'"; return false; }
            Step s; const std::string range = item.substr(0, colon);
            const size_t dash = range.find('-');
            try {
                size_t used = 0;
                s.first = std::stoull(range.substr(0, dash), &used); if (used != (dash == std::string::npos ? range.size() : dash)) throw 0;
                s.last = dash == std::string::npos ? s.first : std::stoull(range.substr(dash + 1), &used);
                if (dash != std::string::npos && used != range.size() - dash - 1) throw 0;
            } catch (...) { error = "bad frame range in '" + item + "'"; return false; }
            if (!s.first || s.last < s.first) { error = "bad frame range in '" + item + "'"; return false; }
            std::string names = item.substr(colon + 1); size_t from = 0;
            while (from <= names.size()) {
                const size_t plus = std::min(names.find('+', from), names.size());
                const uint32_t bit = button(names.substr(from, plus - from));
                if (!bit) { error = "unknown button in '" + item + "'"; return false; }
                s.buttons |= bit; from = plus + 1;
            }
            steps.push_back(s);
        }
        return true;
    }
    uint32_t buttonsAt(uint64_t frame) const {
        uint32_t b = 0; for (const auto& s : steps) if (frame >= s.first && frame <= s.last) b |= s.buttons; return b;
    }
};
}
