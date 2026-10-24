#pragma once
// Software execution of graphics PM4 streams through the GCN interpreter:
// register state, indexed/automatic draws (triangle list/strip, rect list),
// the real vertex and pixel programs, viewport, scissor, rasterization in
// 8x8 tiles of 2x2 quads, and colour target 0 writes (8_8_8_8 UNORM).
// Known limits, each reported or documented rather than guessed: no
// clipping (triangles with w <= 0 are dropped), no depth/stencil, no
// culling, colour target 0 only, render targets written linearly (PS4
// tiling is not implemented; only the display reads these buffers), and
// compute dispatches are counted but not run, instance 0 only. A reference,
// not a fast path.
#include "GCNInterpreter.hpp"
#include "PM4Type3Decoder.hpp"
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace maxps4::gpu {
class PM4SoftwareRenderer {
public:
    struct Stats { unsigned draws = 0, rendered = 0, skipped = 0, failed = 0, dispatches = 0; std::string error; };
    explicit PM4SoftwareRenderer(const GCNMemory& memory) : memory_(memory) {}
    Stats stats;
    // Executes one submission; register state persists across submissions.
    void execute(const std::uint32_t* words, std::size_t count) {
        const auto parsed = decodePM4Type3(words, count);
        if (!parsed) { fail("unframed PM4 stream"); return; }
        for (const auto& packet : parsed.packets) {
            const std::uint32_t* data = words + packet.payloadWord;
            const std::size_t n = packet.payloadCount;
            switch (packet.opcode) {
            case 0x69: setRegisters(context_, data, n); break;
            case 0x76: setRegisters(shader_, data, n); break;
            case 0x79: setRegisters(uconfig_, data, n); break;
            case 0x12: context_.fill(0); break;                           // CLEAR_STATE
            case 0x2a: indexBytes_ = (data[0] & 3) == 1 ? 4 : 2; break;     // INDEX_TYPE
            case 0x27: if (n >= 4) draw(data[3], true, (std::uint64_t(data[1]) | std::uint64_t(data[2] & 0xffff) << 32)); break;
            case 0x2d: if (n >= 1) draw(data[0], false, 0); break;
            case 0x15: ++stats.dispatches; break;
            default: break; // synchronization and prefetch packets: nothing to draw
            }
        }
    }
    // Registers are kept by offset from their bank base (context 0xa000,
    // shader 0x2c00, uconfig 0xc000).
    std::uint32_t context(unsigned offset) const { return context_[offset & 0x3ff]; }

private:
    struct Vertex { float pos[4] = {0, 0, 0, 1}; std::array<std::array<float, 4>, 32> param{}; };
    const GCNMemory& memory_;
    std::array<std::uint32_t, 0x400> context_{}, shader_{}, uconfig_{};
    unsigned indexBytes_ = 2;

    static void setRegisters(std::array<std::uint32_t, 0x400>& bank, const std::uint32_t* data, std::size_t n) {
        for (std::size_t k = 1; k < n; ++k) if (data[0] + k - 1 < bank.size()) bank[data[0] + k - 1] = data[k];
    }
    void fail(const std::string& message) { ++stats.failed; if (stats.error.empty()) stats.error = message; }
    static float f(std::uint32_t x) { return gcn_detail::f32(x); }

    bool runVertices(const std::vector<std::uint32_t>& indices, std::vector<Vertex>& out, GCNCodeCache& cache) {
        const std::uint64_t program = (std::uint64_t(shader_[0x48]) | std::uint64_t(shader_[0x49]) << 32) << 8;
        const unsigned users = (shader_[0x4b] >> 1) & 0x1f;
        out.assign(indices.size(), Vertex{});
        for (std::size_t first = 0; first < indices.size(); first += 64) {
            const unsigned lanes = unsigned(std::min<std::size_t>(64, indices.size() - first));
            GCNWave wave(memory_, lanes, &cache);
            for (unsigned k = 0; k < users && k < 16; ++k) wave.s[k] = shader_[0x4c + k];
            for (unsigned lane = 0; lane < lanes; ++lane) { wave.v(0, lane) = indices[first + lane]; wave.v(3, lane) = 0; }
            if (!wave.run(program)) { fail("VS: " + wave.error); return false; }
            for (const auto& e : wave.exports) {
                for (unsigned lane = 0; lane < lanes; ++lane) {
                    if (!((e.mask >> lane) & 1)) continue;
                    float value[4];
                    for (unsigned c = 0; c < 4; ++c) value[c] = f(e.value[c][lane]);
                    if (e.compressed) {
                        value[0] = gcn_detail::f16(e.value[0][lane] & 0xffff); value[1] = gcn_detail::f16(e.value[0][lane] >> 16);
                        value[2] = gcn_detail::f16(e.value[1][lane] & 0xffff); value[3] = gcn_detail::f16(e.value[1][lane] >> 16);
                    }
                    Vertex& vertex = out[first + lane];
                    if (e.target == 12) for (unsigned c = 0; c < 4; ++c) vertex.pos[c] = value[c];
                    else if (e.target >= 32) for (unsigned c = 0; c < 4; ++c) vertex.param[e.target - 32][c] = value[c];
                }
            }
        }
        return true;
    }

    void draw(std::uint32_t count, bool indexed, std::uint64_t indexAddress) {
        ++stats.draws;
        if (!(context_[0x8e] & 0xf) || !context_[0x318]) { ++stats.skipped; return; } // no colour target 0 output
        const std::uint32_t info = context_[0x31c];
        if (((info >> 2) & 0x1f) != 0xa || ((info >> 8) & 7) != 0) { fail("colour target format"); return; }
        const unsigned primitive = uconfig_[0x242];
        if (primitive != 4 && primitive != 6 && primitive != 0x11) { fail("primitive type " + std::to_string(primitive)); return; }
        if (!count || count > (1u << 20)) { fail("draw count"); return; }
        std::vector<std::uint32_t> indices(count);
        if (indexed) {
            std::vector<std::uint8_t> bytes(std::size_t(count) * indexBytes_);
            if (!memory_.read(indexAddress, bytes.data(), bytes.size())) { fail("unreadable index buffer"); return; }
            for (std::uint32_t k = 0; k < count; ++k) {
                std::uint32_t value = 0;
                std::memcpy(&value, bytes.data() + std::size_t(k) * indexBytes_, indexBytes_);
                indices[k] = value + context_[0x102]; // VGT_INDX_OFFSET
            }
        } else for (std::uint32_t k = 0; k < count; ++k) indices[k] = k;
        GCNCodeCache vsCache, psCache;
        std::vector<Vertex> vertices;
        if (!runVertices(indices, vertices, vsCache)) return;
        std::vector<std::array<Vertex, 3>> triangles;
        if (primitive == 4) for (std::size_t k = 0; k + 2 < vertices.size(); k += 3) triangles.push_back({vertices[k], vertices[k + 1], vertices[k + 2]});
        else if (primitive == 6) for (std::size_t k = 0; k + 2 < vertices.size(); ++k) triangles.push_back({vertices[k], vertices[k + 1], vertices[k + 2]});
        else for (std::size_t k = 0; k + 2 < vertices.size(); k += 3) {
            // A rectangle from three corners; the fourth is v1 + v2 - v0.
            Vertex fourth;
            for (unsigned c = 0; c < 4; ++c) fourth.pos[c] = vertices[k + 1].pos[c] + vertices[k + 2].pos[c] - vertices[k].pos[c];
            for (unsigned p = 0; p < 32; ++p) for (unsigned c = 0; c < 4; ++c)
                fourth.param[p][c] = vertices[k + 1].param[p][c] + vertices[k + 2].param[p][c] - vertices[k].param[p][c];
            triangles.push_back({vertices[k], vertices[k + 1], vertices[k + 2]});
            triangles.push_back({vertices[k + 2], vertices[k + 1], fourth});
        }
        bool ok = true;
        for (const auto& triangle : triangles) if (!rasterize(triangle, psCache)) { ok = false; break; }
        if (ok) ++stats.rendered;
    }

    bool rasterize(const std::array<Vertex, 3>& t, GCNCodeCache& cache) {
        // Viewport transform (PA_CL_VPORT_*, PA_CL_VTE_CNTL).
        const std::uint32_t vte = context_[0x206];
        float sx[3], sy[3], sz[3], rw[3];
        for (unsigned k = 0; k < 3; ++k) {
            const float w = t[k].pos[3];
            if (!(w > 0)) return true; // behind the eye: clipping is not implemented
            const bool divide = !((vte >> 8) & 1);
            const float x = divide ? t[k].pos[0] / w : t[k].pos[0], y = divide ? t[k].pos[1] / w : t[k].pos[1];
            const float z = t[k].pos[2] / w;
            sx[k] = ((vte & 1) ? x * f(context_[0x10f]) : x) + ((vte & 2) ? f(context_[0x110]) : 0);
            sy[k] = ((vte & 4) ? y * f(context_[0x111]) : y) + ((vte & 8) ? f(context_[0x112]) : 0);
            sz[k] = ((vte & 16) ? z * f(context_[0x113]) : z) + ((vte & 32) ? f(context_[0x114]) : 0);
            rw[k] = 1.0f / w;
        }
        const float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
        if (area == 0 || !std::isfinite(area)) return true;
        // Colour target 0 (CB_COLOR0_*): pitch in 8-pixel units, slice in 64-pixel units.
        const unsigned pitch = ((context_[0x319] & 0x7ff) + 1) * 8;
        const unsigned height = unsigned((std::uint64_t((context_[0x31a] & 0x3fffff) + 1) * 64) / pitch);
        int minX = int(context_[0x0c] & 0xffff), minY = int(context_[0x0c] >> 16);
        int maxX = std::min<int>(int(context_[0x0d] & 0xffff), int(pitch)), maxY = std::min<int>(int(context_[0x0d] >> 16), int(height));
        minX = std::max(minX, int(std::floor(std::min({sx[0], sx[1], sx[2]}))));
        minY = std::max(minY, int(std::floor(std::min({sy[0], sy[1], sy[2]}))));
        maxX = std::min(maxX, int(std::ceil(std::max({sx[0], sx[1], sx[2]}))) + 1);
        maxY = std::min(maxY, int(std::ceil(std::max({sy[0], sy[1], sy[2]}))) + 1);
        if (minX >= maxX || minY >= maxY) return true;
        const float sign = area > 0 ? 1.0f : -1.0f;
        // Top-left rule: a pixel exactly on an edge belongs to top or left edges only.
        auto edge = [&](unsigned a, unsigned b, float px, float py) {
            return sign * ((sx[b] - sx[a]) * (py - sy[a]) - (sy[b] - sy[a]) * (px - sx[a]));
        };
        auto owns = [&](unsigned a, unsigned b) {
            const float dx = sign * (sx[b] - sx[a]), dy = sign * (sy[b] - sy[a]);
            return (dy == 0 && dx < 0) || dy > 0;
        };
        const bool own[3] = {owns(1, 2), owns(2, 0), owns(0, 1)};
        const std::uint32_t inputs = context_[0x1b4] ? context_[0x1b4] : context_[0x1b3];
        const std::uint64_t program = (std::uint64_t(shader_[0x08]) | std::uint64_t(shader_[0x09]) << 32) << 8;
        const unsigned users = (shader_[0x0b] >> 1) & 0x1f;
        const std::uint64_t base = std::uint64_t(context_[0x318]) << 8;
        const unsigned swap = (context_[0x31c] >> 11) & 3, mask = context_[0x8e] & 0xf;
        // Parameter planes as the parameter cache would hold them.
        std::array<std::array<GCNAttributePlane, 4>, 32> planes{};
        for (unsigned attr = 0; attr < 32; ++attr) {
            const std::uint32_t cntl = context_[0x191 + attr];
            const unsigned param = cntl & 0x1f;
            for (unsigned c = 0; c < 4; ++c) {
                if (cntl & 0x20) { // DEFAULT_VAL: (0,0,0,0), (0,0,0,1), (1,1,1,0), (1,1,1,1)
                    const unsigned dv = (cntl >> 8) & 3;
                    planes[attr][c] = {(c == 3 ? (dv & 1) : (dv >> 1)) ? 1.0f : 0.0f, 0, 0}; continue;
                }
                const float p0 = t[0].param[param][c];
                const bool flat = (cntl >> 10) & 1;
                planes[attr][c] = {p0, flat ? 0 : t[1].param[param][c] - p0, flat ? 0 : t[2].param[param][c] - p0};
            }
        }
        for (int ty = minY & ~7; ty < maxY; ty += 8) {
            for (int tx = minX & ~7; tx < maxX; tx += 8) {
                GCNWave wave(memory_, 64, &cache);
                std::uint64_t covered = 0;
                for (unsigned lane = 0; lane < 64; ++lane) {
                    const unsigned quad = lane >> 2;
                    const int x = tx + 2 * int(quad & 3) + int(lane & 1), y = ty + 2 * int(quad >> 2) + int((lane >> 1) & 1);
                    const float px = float(x) + 0.5f, py = float(y) + 0.5f;
                    const float e0 = edge(1, 2, px, py), e1 = edge(2, 0, px, py), e2 = edge(0, 1, px, py);
                    const bool inside = (e0 > 0 || (e0 == 0 && own[0])) && (e1 > 0 || (e1 == 0 && own[1])) &&
                                        (e2 > 0 || (e2 == 0 && own[2]));
                    if (inside && x >= minX && x < maxX && y >= minY && y < maxY) covered |= 1ull << lane;
                    // Barycentrics (also for helper lanes, by extrapolation).
                    const float b0 = e0 / (sign * area), b1 = e1 / (sign * area), b2 = e2 / (sign * area);
                    const float q0 = b0 * rw[0], q1 = b1 * rw[1], q2 = b2 * rw[2], qs = q0 + q1 + q2;
                    const float pi = qs != 0 ? q1 / qs : b1, pj = qs != 0 ? q2 / qs : b2;
                    // SPI_PS_INPUT_ADDR decides the input VGPR layout.
                    static const unsigned sizes[16] = {2, 2, 2, 3, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1};
                    unsigned reg = 0;
                    for (unsigned bit = 0; bit < 16; ++bit) {
                        if (!((inputs >> bit) & 1)) continue;
                        float value[3] = {0, 0, 0};
                        if (bit <= 2) { value[0] = pi; value[1] = pj; }
                        else if (bit == 3) { value[0] = qs; value[1] = q1; value[2] = q2; }
                        else if (bit <= 6) { value[0] = b1; value[1] = b2; }
                        else if (bit == 8) value[0] = px;
                        else if (bit == 9) value[0] = py;
                        else if (bit == 10) value[0] = b0 * sz[0] + b1 * sz[1] + b2 * sz[2];
                        else if (bit == 11) value[0] = qs;
                        else if (bit == 12) value[0] = area > 0 ? 1.0f : 0.0f;
                        else if (bit == 14) { wave.v(reg, lane) = 1; ++reg; continue; }
                        for (unsigned k = 0; k < sizes[bit]; ++k) wave.v(reg + k, lane) = gcn_detail::u32(value[k]);
                        reg += sizes[bit];
                    }
                }
                if (!covered) continue;
                for (unsigned k = 0; k < users && k < 16; ++k) wave.s[k] = shader_[0x0c + k];
                wave.setExec(covered);
                wave.attribute = [&](unsigned attr, unsigned chan) { return planes[attr & 31][chan & 3]; };
                if (!wave.run(program)) { fail("PS: " + wave.error); return false; }
                for (const auto& e : wave.exports) {
                    if (e.target != 0) continue;
                    if (!writeColor(e, covered, tx, ty, base, pitch, swap, mask)) return false;
                }
            }
        }
        return true;
    }

    bool writeColor(const GCNExport& e, std::uint64_t covered, int tx, int ty, std::uint64_t base,
                    unsigned pitch, unsigned swap, unsigned mask) {
        const std::uint32_t blend = context_[0x1e0];
        for (unsigned row = 0; row < 8; ++row) {
            // Rows of the tile with no covered pixel may lie past the target.
            const std::uint64_t rowLanes = 0x3333ull << (16 * (row >> 1)) << (2 * (row & 1));
            if (!(covered & e.mask & rowLanes)) continue;
            std::uint8_t pixels[32];
            const std::uint64_t address = base + (std::uint64_t(ty + int(row)) * pitch + std::uint64_t(tx)) * 4;
            if (!memory_.read(address, pixels, sizeof pixels)) { fail("unreadable colour target"); return false; }
            bool changed = false;
            for (unsigned column = 0; column < 8; ++column) {
                const unsigned lane = ((row >> 1) * 4 + (column >> 1)) * 4 + (column & 1) + 2 * (row & 1);
                if (!((covered >> lane) & 1) || !((e.mask >> lane) & 1)) continue;
                float c[4];
                if (e.compressed) {
                    c[0] = gcn_detail::f16(e.value[0][lane] & 0xffff); c[1] = gcn_detail::f16(e.value[0][lane] >> 16);
                    c[2] = gcn_detail::f16(e.value[1][lane] & 0xffff); c[3] = gcn_detail::f16(e.value[1][lane] >> 16);
                } else for (unsigned k = 0; k < 4; ++k) c[k] = (e.enable >> k) & 1 ? f(e.value[k][lane]) : (k == 3 ? 1.0f : 0.0f);
                // Byte positions of R, G, B, A for CB COMP_SWAP STD, ALT, STD_REV, ALT_REV.
                static const unsigned order[4][4] = {{0, 1, 2, 3}, {2, 1, 0, 3}, {3, 2, 1, 0}, {1, 2, 3, 0}};
                std::uint8_t* p = pixels + 4 * column;
                float dst[4];
                for (unsigned k = 0; k < 4; ++k) dst[k] = float(p[order[swap][k]]) / 255.0f;
                if ((blend >> 30) & 1) blendColor(blend, c, dst);
                for (unsigned k = 0; k < 4; ++k) {
                    if (!((mask >> k) & 1)) continue;
                    const float v = std::isnan(c[k]) ? 0.0f : std::fmin(std::fmax(c[k], 0.0f), 1.0f);
                    p[order[swap][k]] = std::uint8_t(std::lround(v * 255.0f));
                }
                changed = true;
            }
            if (changed && !memory_.write(address, pixels, sizeof pixels)) { fail("unwritable colour target"); return false; }
        }
        return true;
    }
    // CB_BLEND0_CONTROL with an ADD combine; other functions keep the source.
    static void blendColor(std::uint32_t control, float c[4], const float dst[4]) {
        auto factor = [&](unsigned code, unsigned k) -> float {
            switch (code) {
            case 0: return 0; case 1: return 1;
            case 2: return c[k]; case 3: return 1 - c[k];
            case 4: return c[3]; case 5: return 1 - c[3];
            case 6: return dst[3]; case 7: return 1 - dst[3];
            case 8: return dst[k]; case 9: return 1 - dst[k];
            default: return 1;
            }
        };
        const bool separate = (control >> 29) & 1;
        float out[4];
        for (unsigned k = 0; k < 4; ++k) {
            const bool alpha = k == 3 && separate;
            const unsigned src = alpha ? (control >> 16) & 31 : control & 31;
            const unsigned fn = alpha ? (control >> 21) & 7 : (control >> 5) & 7;
            const unsigned dstFactor = alpha ? (control >> 24) & 31 : (control >> 8) & 31;
            out[k] = fn == 0 ? c[k] * factor(src, k) + dst[k] * factor(dstFactor, k) : c[k];
        }
        for (unsigned k = 0; k < 4; ++k) c[k] = out[k];
    }
};
} // namespace maxps4::gpu
