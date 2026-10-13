#pragma once
#include "PS4PKGInventory.hpp"
#include <map>

namespace maxps4 {
// PARAM.SFO uses little-endian fields, unlike the outer CNT header.
// Parse atomically; strings remain metadata, never filesystem paths.
class PS4SFO {
public:
    std::map<std::string, std::string> strings;
    std::string error;
    bool parse(const std::vector<uint8_t>& bytes) {
        strings.clear(); error.clear();
        auto fail = [&](const char* why) { error = why; return false; };
        if (bytes.size() < 20 || std::memcmp(bytes.data(), "\0PSF", 4))
            return fail("SFO : signature absente ou en-tête tronqué");
        auto le = [&](size_t at, size_t width) {
            uint32_t v = 0;
            for (size_t i = 0; i < width; ++i) v |= uint32_t(bytes[at + i]) << (8 * i);
            return v;
        };
        if (le(4, 4) != 0x101) return fail("SFO : version de format non prise en charge");
        const uint64_t keys = le(8, 4), data = le(12, 4), count = le(16, 4);
        if (count > 1024 || !PS4PKGInventory::range(20, count * 16, bytes.size()) ||
            keys < 20 + count * 16 || keys > data || data > bytes.size())
            return fail("SFO : tables hors limites ou chevauchantes");
        std::map<std::string, std::string> pending;
        std::vector<std::string> seen;
        std::vector<std::pair<uint64_t, uint64_t>> ranges;
        for (size_t i = 0; i < count; ++i) {
            const size_t at = 20 + i * 16;
            const uint64_t key = keys + le(at, 2), offset = data + le(at + 12, 4);
            const uint32_t format = le(at + 2, 2), length = le(at + 4, 4), capacity = le(at + 8, 4);
            if (key >= data || (key != keys && bytes[key - 1] != 0) || length > capacity ||
                !PS4PKGInventory::range(offset, capacity, bytes.size()))
                return fail("SFO : clé ou valeur hors limites / longueur supérieure à capacité");
            std::string name;
            size_t end = static_cast<size_t>(key);
            while (end < data && bytes[end] && name.size() < 64) {
                if (bytes[end] < 0x21 || bytes[end] > 0x7e) return fail("SFO : clé invalide");
                name += static_cast<char>(bytes[end++]);
            }
            if (name.empty() || end == data || bytes[end]) return fail("SFO : clé non terminée");
            for (const auto& previous : seen)
                if (previous == name) return fail("SFO : clé dupliquée");
            seen.push_back(name);
            for (const auto& previous : ranges)
                if (capacity && previous.second && offset < previous.first + previous.second && previous.first < offset + capacity)
                    return fail("SFO : valeurs chevauchantes");
            ranges.emplace_back(offset, capacity);
            if (format == 0x204) {
                if (!length || length > 4096 || bytes[offset + length - 1] != 0)
                    return fail("SFO : texte non terminé ou supérieur à 4096 octets");
                std::string value(reinterpret_cast<const char*>(bytes.data() + offset), length - 1);
                // Strict UTF-8, excluding controls, overlong forms and surrogates.
                if (!displayUTF8(value)) return fail("SFO : texte UTF-8 invalide ou caractères de contrôle");
                pending.emplace(name, std::move(value));
            } else if (format == 0x404) {
                if (length != 4) return fail("SFO : entier de longueur invalide");
            } else if (format != 0x4) return fail("SFO : type de valeur non pris en charge");
        }
        strings = std::move(pending);
        return true;
    }
private:
    static bool displayUTF8(const std::string& s) {
        for (size_t i = 0; i < s.size();) {
            const uint8_t first = static_cast<uint8_t>(s[i++]);
            if (first < 0x80) { if (first < 0x20 || first == 0x7f) return false; continue; }
            size_t n; uint32_t cp, minimum;
            if (first >= 0xc2 && first <= 0xdf) { n = 1; cp = first & 31; minimum = 0x80; }
            else if (first >= 0xe0 && first <= 0xef) { n = 2; cp = first & 15; minimum = 0x800; }
            else if (first >= 0xf0 && first <= 0xf4) { n = 3; cp = first & 7; minimum = 0x10000; }
            else return false;
            if (n > s.size() - i) return false;
            while (n--) {
                const uint8_t c = static_cast<uint8_t>(s[i++]);
                if ((c & 0xc0) != 0x80) return false;
                cp = (cp << 6) | (c & 63);
            }
            if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) ||
                (cp >= 0x80 && cp <= 0x9f) || (cp >= 0x202a && cp <= 0x202e) ||
                (cp >= 0x2066 && cp <= 0x2069)) return false;
        }
        return true;
    }
};
}
