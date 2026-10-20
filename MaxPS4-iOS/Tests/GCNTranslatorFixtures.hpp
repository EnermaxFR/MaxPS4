#pragma once
// Pixel shader programs shared by the two stages of the translator test
// (translation tool and comparison harness). Encodings as GCNRendererTest.
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace gcnfix {
using Words = std::vector<uint32_t>;
inline uint32_t sop1(unsigned op, unsigned dst, unsigned src) { return 0xbe800000U | dst << 16 | op << 8 | src; }
inline uint32_t sop2(unsigned op, unsigned dst, unsigned s0, unsigned s1) { return 0x80000000U | op << 23 | dst << 16 | s1 << 8 | s0; }
inline uint32_t sopp(unsigned op, int16_t imm = 0) { return 0xbf800000U | op << 16 | uint16_t(imm); }
inline uint32_t smrd(unsigned op, unsigned dst, unsigned base, unsigned dwords) { return 0xc0000000U | op << 22 | dst << 15 | (base / 2) << 9 | 256 | dwords; }
inline uint32_t vop1(unsigned op, unsigned dst, unsigned src) { return 0x7e000000U | dst << 17 | op << 9 | src; }
inline uint32_t vop2(unsigned op, unsigned dst, unsigned src0, unsigned vsrc1) { return op << 25 | dst << 17 | (vsrc1 & 255) << 9 | src0; }
inline Words vop3(unsigned op, unsigned dst, unsigned s0, unsigned s1, unsigned s2 = 0, unsigned neg = 0, unsigned abs = 0, bool clamp = false, unsigned omod = 0) {
  return {0xd0000000U | op << 17 | (clamp ? 1u << 11 : 0u) | abs << 8 | dst, s0 | s1 << 9 | s2 << 18 | omod << 27 | neg << 29};
}
inline Words vop3b(unsigned op, unsigned dst, unsigned sdst, unsigned s0, unsigned s1, unsigned s2 = 0) {
  return {0xd0000000U | op << 17 | sdst << 8 | dst, s0 | s1 << 9 | s2 << 18};
}
inline uint32_t sopc(unsigned op, unsigned s0, unsigned s1) { return 0xbf000000U | op << 16 | s1 << 8 | s0; }
inline uint32_t sopk(unsigned op, unsigned dst, uint16_t imm) { return 0xb0000000U | op << 23 | dst << 16 | imm; }
inline uint32_t vopc(unsigned op, unsigned src0, unsigned vsrc1) { return 0x7c000000U | op << 17 | (vsrc1 & 255) << 9 | src0; }
inline uint32_t vintrp(unsigned op, unsigned dst, unsigned attr, unsigned chan, unsigned src) { return 0xc8000000U | dst << 18 | op << 16 | attr << 10 | chan << 8 | src; }
inline Words exp(unsigned target, unsigned en, bool compr, bool done, bool vm, unsigned a, unsigned b, unsigned c, unsigned d) {
  return {0xf8000000U | vm << 12 | done << 11 | compr << 10 | target << 4 | en, a | b << 8 | c << 16 | d << 24};
}
inline Words mimg(unsigned op, unsigned dmask, unsigned vaddr, unsigned vdata, unsigned rsrc, unsigned ssamp) {
  return {0xf0000000U | op << 18 | dmask << 8, vaddr | vdata << 8 | (rsrc / 4) << 16 | (ssamp / 4) << 21};
}
inline Words mubuf(unsigned op, unsigned vaddr, unsigned vdata, unsigned rsrc, unsigned soffset, bool idxen) {
  return {0xe0000000U | op << 18 | unsigned(idxen) << 13, vaddr | vdata << 8 | (rsrc / 4) << 16 | soffset << 24};
}
inline void add(Words& code, std::initializer_list<Words> parts) { for (const auto& p : parts) code.insert(code.end(), p.begin(), p.end()); }
constexpr unsigned V = 256, VCC = 106, EXEC = 126;

struct Fixture { std::string name; Words code; unsigned vgprs; bool translatable; std::string refusal; };

// User SGPRs (set by the harness): s[0:3] V# of constants, s[8:15] T#, s[16:19] S#.
inline std::vector<Fixture> all() {
  std::vector<Fixture> f;
  { // Constant-colour clear (the title's clear pixel shader shape).
    Words c{smrd(10, 4, 0, 0), sopp(12, 0x7f), vop1(1, 0, 4), vop1(1, 1, 5), vop1(1, 2, 6), vop1(1, 3, 7)};
    add(c, {exp(0, 15, false, true, true, 0, 1, 2, 3)}); c.push_back(sopp(1));
    f.push_back({"ps_clear", c, 8, true, ""});
  }
  { // Textured sprite (texture_p.sb shape): save EXEC, WQM, interpolate uv, sample, restore, pack, export.
    Words c{sop1(4, 20, EXEC), sop1(10, EXEC, EXEC),
            vintrp(0, 4, 0, 0, 0), vintrp(1, 4, 0, 0, 1), vintrp(0, 5, 0, 1, 0), vintrp(1, 5, 0, 1, 1),
            vintrp(0, 6, 1, 0, 0), vintrp(1, 6, 1, 0, 1)};
    add(c, {mimg(32, 15, 4, 8, 8, 16)});
    c.push_back(sop1(4, EXEC, 20)); c.push_back(sopp(12, 0x70));
    c.push_back(vop2(8, 8, V + 8, 6)); // tint red by an interpolated factor
    c.push_back(vop2(47, 0, V + 8, 9)); c.push_back(vop2(47, 1, V + 10, 11));
    add(c, {exp(0, 15, true, true, true, 0, 1, 0, 0)}); c.push_back(sopp(1));
    f.push_back({"ps_textured", c, 16, true, ""});
  }
  { // ALU mix: modifiers, VCC compares/cndmask, carries, med3, conversions, clamp/omod.
    Words c{smrd(10, 4, 0, 0), sopp(12, 0x7f)};
    add(c, {vop3(321, 2, V + 0, V + 1, 4 + 0, 2, 1)});                     // v2 = |v0| * -v1 + s4
    c.push_back(vopc(1, V + 0, 1));                                        // vcc = v0 < v1
    c.push_back(vop2(0, 3, V + 0, 2));                                     // v3 = vcc ? v2 : v0
    add(c, {vop3(343, 4, V + 0, V + 1, V + 2)});                           // med3
    add(c, {vop3(264, 5, V + 3, 5, 0, 0, 0, true, 1)});                    // clamp(2 * v3 * s5)
    c.push_back(vop1(8, 6, V + 2));                                        // cvt_i32_f32
    add(c, {vop3b(293, 7, VCC, V + 6, 6)});                                // v7 = v6 + s6, carry -> vcc
    c.push_back(vop2(0, 7, V + 7, 3));                                     // v7 = vcc ? v3 : v7
    c.push_back(vop1(42, 8, V + 4));                                       // rcp
    c.push_back(vop1(32, 9, V + 0));                                       // fract
    c.push_back(vop2(31, 9, V + 8, 5));                                    // mac
    add(c, {exp(0, 15, false, true, true, 5, 7, 8, 9)}); c.push_back(sopp(1));
    f.push_back({"ps_alu", c, 12, true, ""});
  }
  { // Divergent if/else (LLVM's saveexec shape): v0 > 0.5 ? (v1 * s5, 1.0) : (s4 + v1, 2.0).
    Words c{smrd(10, 4, 0, 0), sopp(12, 0x7f),
            vopc(1, 240, 0),                                  // vcc = 0.5 < v0
            sop1(36, 20, VCC), sop2(19, 20, EXEC, 20),        // s[20:21] = exec & ~vcc lanes; exec &= vcc
            sopp(8, 2),                                       // s_cbranch_execz else
            vop2(8, 2, 5, 1), vop1(1, 3, 242),                // then: v2 = s5 * v1, v3 = 1.0
            sop1(37, 20, 20), sop2(19, EXEC, EXEC, 20),       // else lanes
            sopp(8, 2),                                       // s_cbranch_execz end
            vop2(3, 2, 4, 1), vop1(1, 3, 244),                // else: v2 = s4 + v1, v3 = 2.0
            sop2(17, EXEC, EXEC, 20)};                        // end: restore
    add(c, {exp(0, 15, false, true, true, 2, 3, 0, 1)}); c.push_back(sopp(1));
    f.push_back({"ps_ifelse", c, 8, true, ""});
  }
  { // Discard: v_cmpx keeps v0 <= 0.5; killed lanes are not exported.
    Words c{vopc(0x16, 240, 0), vop2(3, 2, V + 0, 1)};       // exec &= 0.5 >= v0; v2 = v0 + v1
    add(c, {exp(0, 15, false, true, true, 0, 1, 2, 2)}); c.push_back(sopp(1));
    f.push_back({"ps_kill", c, 4, true, ""});
  }
  { // Uniform branches on scalar compares (taken and not taken), s_branch, a skipped early exit.
    Words c{smrd(10, 4, 0, 0), sopp(12, 0x7f),
            sopc(10, 6, 144),        // scc = s6 < 16 (u32): false (s6 = 0xfffffff0)
            sopp(5, 1), vop1(1, 0, 242),          // not taken: v0 = 1.0
            sopc(6, 11, 128),        // scc = s11 == 0: true
            sopp(5, 1), vop1(1, 1, 244),          // taken: v1 keeps its value
            sopk(9, 4, 0x1234),      // scc = s4 == 0x1234: false
            sopp(4, 1), sopp(1),                  // scc0 taken: skips s_endpgm
            sopp(2, 1), vop1(1, 2, 246),          // s_branch over v2 = 4.0
            vop1(1, 3, 243)};                     // v3 = -1.0
    add(c, {exp(0, 15, false, true, true, 0, 1, 2, 3)}); c.push_back(sopp(1));
    f.push_back({"ps_uniform", c, 4, true, ""});
  }
  { // Early exit under a pending uniform branch that is not taken: the second export must not happen.
    Words c{smrd(10, 4, 0, 0), sopp(12, 0x7f)};
    add(c, {exp(0, 15, false, false, true, 0, 1, 0, 1)});
    c.push_back(sopc(6, 11, 129));                       // scc = s11 == 1: false
    c.push_back(sopp(5, 1)); c.push_back(sopp(1));       // not taken: s_endpgm runs
    add(c, {exp(0, 15, false, true, true, 1, 0, 1, 0)}); c.push_back(sopp(1));
    f.push_back({"ps_early", c, 4, true, ""});
  }
  // Refusals: exact reasons, never a partial translation.
  f.push_back({"ref_branch", {sopp(2, -1), sopp(1)}, 4, false, "backward branch"});
  f.push_back({"ref_execnz", {sopp(9, 0), sopp(1)}, 4, false, "execnz"});
  f.push_back({"ref_scc_wave", {vopc(1, V + 0, 1), sop1(36, 20, VCC), sopp(5, 0), sopp(1)}, 4, false, "wave-level SCC"});
  f.push_back({"ref_execz_scalar", {sopp(8, 1), sop1(3, 30, 128), sopp(1)}, 4, false, "region s_cbranch_execz may skip"});
  f.push_back({"ref_wqm_after_kill", {vopc(0x11, V + 0, 1), sop1(10, EXEC, EXEC), sopp(1)}, 4, false, "helper lanes"});
  f.push_back({"ref_twice", {sop1(3, 4, 128), sop1(3, 4, 129), sopp(1)}, 4, false, "written twice"});
  { Words c; add(c, {mubuf(0, 0, 1, 0, 128, true)}); c.push_back(sopp(1)); f.push_back({"ref_buffer", c, 4, false, "buffer access"}); }
  f.push_back({"ref_readfirst", {vop1(2, 4, V + 0), sopp(1)}, 4, false, "not translated"});
  f.push_back({"ref_order", {vop1(1, 0, 4), sop1(3, 4, 130), sopp(1)}, 4, false, "read by translated code before its write"});
  // A saved EXEC used as a vector operand.
  f.push_back({"ref_exec_value", {sop1(4, 20, EXEC), vop1(1, 0, 20), sop1(4, EXEC, 20), sopp(1)}, 4, false, "read as a value"});
  return f;
}
} // namespace gcnfix
