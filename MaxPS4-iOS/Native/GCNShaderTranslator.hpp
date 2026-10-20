#pragma once
// GCN (gfx7) pixel shader -> straight-line function in the subset of C++ and
// Metal Shading Language that GCNKernel.h defines, for GPU execution of guest
// pixel shaders. One function call shades one fragment (one GCN lane).
//
// Execution model of a translated shader:
// * Scalar instructions are uniform for a draw. They are not translated: the
//   host runs the program once with the GCNShader interpreter and hands the
//   final SGPR file to the function as `s` ("scalar prepass"). This is exact
//   only when every SGPR is written at most once and never read by vector code
//   before that write; the translator verifies both and refuses otherwise.
// * EXEC, VCC and SGPR pairs used as lane masks are per-fragment booleans:
//   saveexec/mask arithmetic, V_CMPX (discard) and divergent if/else work
//   lane by lane; an inactive fragment writes no VGPR, samples nothing and
//   is not exported. s_wqm_b64 exec is accepted only before EXEC changes.
// * Control flow: forward uniform branches (s_branch, s_cbranch_scc0/1 on an
//   SCC from a scalar compare) become guards on the skipped instructions,
//   without goto (MSL has none); s_cbranch_execz is a shortcut for inactive
//   waves and is ignored when the region it may skip has no scalar effect.
//   Loops (backward branches), execnz and vccz/vccnz are refused.
// * fp32 denormals are flushed as FLOAT_MODE denormal mode 0 (inputs and
//   outputs, multiply-add products, interpolation): see floatModeSupported.
// * Interpolation, image sampling and exports call macros the including
//   backend defines (GCN_ENV_INTERP, GCN_ENV_SAMPLE, GCN_ENV_EXPORT).
//   GCN_ENV_SAMPLE(slot, w0, w1, c, t) names the image by its slot in
//   Result::images; GCNKernelImage.h's gcn_image implements it.
// Anything else fails with the exact program counter; nothing is guessed.
#include "GCNShader.hpp"
#include "GCNKernel.h"
#include <algorithm>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace maxps4::gpu {
struct GCNShaderTranslator {
  struct Result {
    bool ok = false;
    std::string error, body;   // body: statements of the function (uses s, v, vcc, exec, scc)
    unsigned instructions = 0, vgprs = 0, branches = 0;
    bool usesInterpolation = false, usesImages = false, needsScalarPrepass = false;
    std::set<unsigned> exportTargets;
    unsigned exports = 0;          // EXP instructions (any target)
    bool mrt0Compressed = false;   // the MRT0 export's COMPR bit
    // Images the host binds (GCNImageBinding) from the scalar prepass, by slot:
    // T# at s[rsrc..rsrc+7], S# at s[ssamp..ssamp+3] when sampled.
    struct Image { unsigned rsrc = 0, ssamp = 0; bool sampled = false; };
    std::vector<Image> images;
  };

  // Memory adapter over the program words for GCNShader::fetch (same decoder).
  struct WordMemory {
    uint64_t base; const std::vector<uint32_t>& words;
    int read_data(uint64_t at, void* out, size_t n, bool = false) const {
      if (at < base || (at - base) + n > words.size() * 4) return -1;
      std::memcpy(out, reinterpret_cast<const uint8_t*>(words.data()) + (at - base), n); return 0;
    }
  };

  static std::string hex(uint64_t v) { std::ostringstream o; o << "0x" << std::hex << v << "u"; return o.str(); }

  static Result translate(const std::vector<uint32_t>& words, unsigned vgprs, uint64_t base = 0x1000) {
    WordMemory memory{base, words};
    return translate(memory, base, vgprs);
  }
  // The program at `base` in guest memory (read through the same decoder as
  // the interpreter, so its bounds checks apply).
  template <class Memory>
  static Result translate(Memory& memory, uint64_t base, unsigned vgprs) {
    Result out; out.vgprs = vgprs;
    std::ostringstream body;
    // written/readBeforeWrite: uniform SGPRs (scalar prepass); masks: even
    // SGPRs holding a per-fragment lane bit (saved EXEC, mask arithmetic).
    std::set<unsigned> written, readBeforeWrite, masks;
    struct Jump { uint64_t target; unsigned id; };
    std::vector<Jump> jumps;             // pending forward uniform branches
    std::vector<uint64_t> execzEnds;     // regions an s_cbranch_execz may skip
    unsigned jumpIds = 0;
    enum class Scc { unknown, uniform, wave } scc = Scc::unknown;
    bool execTouched = false;            // EXEC may be off for a covered fragment
    GCNShader decoder;
    uint64_t pc = base;
    auto fail = [&](const GCNShader::Inst& i, const std::string& why) {
      std::ostringstream m; m << "pc=0x" << std::hex << pc << ' ' << GCNShader::opName(i.enc, i.op) << ": " << why;
      out.error = m.str(); out.ok = false; return out;
    };
    for (unsigned steps = 0; steps < 4096; ++steps) {
      // Branch targets reached here close their regions.
      for (const auto& j : jumps) if (j.target < pc) { out.error = "pc=0x" + hex(pc) + ": branch into the middle of an instruction"; return out; }
      for (uint64_t e : execzEnds) if (e < pc) { out.error = "pc=0x" + hex(pc) + ": branch into the middle of an instruction"; return out; }
      jumps.erase(std::remove_if(jumps.begin(), jumps.end(), [&](const Jump& j) { return j.target == pc; }), jumps.end());
      execzEnds.erase(std::remove(execzEnds.begin(), execzEnds.end(), pc), execzEnds.end());
      std::string guard;   // true while a taken uniform branch skips this instruction
      for (const auto& j : jumps) guard += (guard.empty() ? "j" : " || j") + std::to_string(j.id);

      GCNShader::Inst i; std::string why;
      if (!decoder.fetch(memory, pc, i, why)) { out.error = "pc=0x" + hex(pc) + ": " + why; return out; }
      const uint32_t w = i.w0;
      ++out.instructions;
      std::ostringstream ib, pre;   // this instruction's statements; declarations before its guard
      // ---- uniform scalar destinations: write-once, outside skippable regions ----
      auto scalarWrite = [&](unsigned dst, unsigned count) -> std::string {
        if (!execzEnds.empty()) return "scalar write inside a region s_cbranch_execz may skip";
        for (unsigned k = 0; k < count; ++k) {
          const unsigned r = dst + k;
          if (r > 103) return "scalar write to special register " + std::to_string(r);
          if (written.count(r)) return "SGPR " + std::to_string(r) + " written twice (scalar prepass needs write-once SGPRs)";
          if (readBeforeWrite.count(r)) return "SGPR " + std::to_string(r) + " read by translated code before its write";
          if (masks.count(r & ~1u)) return "SGPR " + std::to_string(r) + " holds a lane mask";
          written.insert(r);
        }
        out.needsScalarPrepass = true; return {};
      };
      // A uniform scalar source must not be a lane mask (its prepass value is the wave's).
      auto uniformSource = [&](unsigned code) {
        return !(code == 106 || code == 107 || code == 126 || code == 127 || (code <= 103 && masks.count(code & ~1u)));
      };
      // ---- lane mask operands (per-fragment booleans) ----
      auto isMask = [&](unsigned code) { return code == 106 || code == 126 || (code <= 102 && !(code & 1) && masks.count(code)); };
      auto maskExpr = [&](unsigned code, std::string& e) -> bool {
        if (code == 106) { e = "vcc"; return true; }
        if (code == 126) { e = "exec"; return true; }
        if (code == 128) { e = "false"; return true; }        // 0
        if (code == 193) { e = "true"; return true; }         // -1: every lane
        if (code <= 102 && !(code & 1) && masks.count(code)) { e = "m" + std::to_string(code); return true; }
        return false;
      };
      auto maskDest = [&](unsigned dst, std::string& e) -> std::string {
        if (dst == 106) { e = "vcc"; return {}; }
        if (dst == 126) { e = "exec"; execTouched = true; return {}; }
        if (dst > 102 || (dst & 1)) return "lane mask written outside an SGPR pair, VCC or EXEC";
        if (!masks.count(dst)) {
          if (!execzEnds.empty()) return "lane mask SGPR first written inside a region s_cbranch_execz may skip";
          if (written.count(dst) || written.count(dst + 1)) return "lane mask written into a uniform SGPR pair";
          if (readBeforeWrite.count(dst) || readBeforeWrite.count(dst + 1)) return "lane mask SGPR read as a uniform value before";
          masks.insert(dst); pre << "  bool m" << dst << " = false;\n";
        }
        e = "m" + std::to_string(dst); return {};
      };
      // ---- scalar operands of emitted uniform code (SCC) ----
      std::string bad;
      auto scalarExpr = [&](unsigned code, uint32_t literal) -> std::string {
        if (code <= 103) {
          if (masks.count(code & ~1u)) { bad = "lane mask used as a scalar value"; return "0u"; }
          if (!written.count(code)) readBeforeWrite.insert(code);
          return "s[" + std::to_string(code) + "]";
        }
        if (code >= 128 && code <= 192) return std::to_string(code - 128) + "u";
        if (code >= 193 && code <= 208) return "gcn_u32(" + std::to_string(192 - int(code)) + ")";
        if (code == 255) return hex(literal);
        bad = "scalar operand " + std::to_string(code) + " in a compare unsupported"; return "0u";
      };
      // ---- vector operand expressions ----
      auto operand = [&](unsigned code, uint32_t literal) -> std::string {
        if (code >= 256) {
          if (code - 256 >= vgprs) { bad = "VGPR " + std::to_string(code - 256) + " beyond the allocation"; return "0u"; }
          return "v[" + std::to_string(code - 256) + "]";
        }
        if (code <= 103) {
          if (masks.count(code & ~1u)) { bad = "lane mask (saved EXEC) read as a value"; return "0u"; }
          if (!written.count(code)) readBeforeWrite.insert(code);
          return "s[" + std::to_string(code) + "]";
        }
        if (code >= 128 && code <= 192) return std::to_string(code - 128) + "u";
        if (code >= 193 && code <= 208) return "gcn_u32(" + std::to_string(192 - int(code)) + ")";
        static const char* floats[] = {"0x3f000000u", "0xbf000000u", "0x3f800000u", "0xbf800000u", "0x40000000u", "0xc0000000u", "0x40800000u", "0xc0800000u"};
        if (code >= 240 && code <= 247) return floats[code - 240];
        if (code == 255) return hex(literal);
        bad = "vector operand " + std::to_string(code) + " (EXEC/VCC/M0 as a value) unsupported"; return "0u";
      };
      const uint64_t next = pc + 4ull * i.length;
      switch (i.enc) {
      case GCNShader::Enc::SOPP: {
        const uint64_t target = next + int64_t(int16_t(w & 0xffff)) * 4;
        switch (i.op) {
        case 0: case 12: case 15: break;                        // s_nop, s_waitcnt, s_setprio
        case 1:                                                 // s_endpgm
          if (!execzEnds.empty()) return fail(i, "s_endpgm inside a region s_cbranch_execz may skip");
          if (jumps.empty()) { out.ok = true; out.body = body.str(); return out; }
          ib << "  return;\n";                                  // guarded early exit
          break;
        case 2: case 4: case 5: {                               // s_branch, s_cbranch_scc0/1 (uniform)
          if (target <= pc) return fail(i, "backward branch (loop) not translated");
          for (uint64_t e : execzEnds) if (target > e) return fail(i, "uniform branch leaving a region s_cbranch_execz may skip");
          if (i.op != 2 && scc != Scc::uniform)
            return fail(i, scc == Scc::wave ? "branch on a wave-level SCC (lane mask result)" : "branch on an SCC the translator does not compute");
          if (target == next) break;
          const unsigned id = jumpIds++;
          pre << "  bool j" << id << " = " << (guard.empty() ? "" : "!(" + guard + ") && ") << (i.op == 2 ? "true" : i.op == 4 ? "!scc" : "scc") << ";\n";
          jumps.push_back({target, id});
          ++out.branches;
          break;
        }
        case 8:                                                 // s_cbranch_execz: a shortcut for inactive waves
          if (target <= pc) return fail(i, "backward branch (loop) not translated");
          execzEnds.push_back(target);
          ++out.branches;
          break;
        case 9: return fail(i, "s_cbranch_execnz (wave-level condition, loops) not translated");
        case 6: case 7: return fail(i, "s_cbranch_vccz/vccnz (wave-level VCC) not translated");
        default: return fail(i, "SOPP opcode not translated");
        }
        break;
      }
      case GCNShader::Enc::SOP1: {
        const unsigned dst = (w >> 16) & 127, src = w & 255, op = i.op;
        const bool wide64 = op == 4 || op == 8 || op == 10 || (op >= 36 && op <= 39);
        if (wide64 && (dst == 106 || dst == 126 || isMask(src) || (dst <= 102 && masks.count(dst)) || (op >= 36 && op <= 39))) {
          std::string a, d;
          if (!maskExpr(src, a)) return fail(i, "64-bit lane mask operand " + std::to_string(src) + " unsupported");
          if (op == 10) {                                       // s_wqm_b64 exec, exec
            if (dst != 126 || src != 126) return fail(i, "s_wqm_b64 outside the EXEC live-mask idiom");
            if (execTouched) return fail(i, "s_wqm_b64 after EXEC changed (helper lanes would revive)");
            scc = Scc::wave; break;
          }
          if (op >= 36 && op <= 39) {                           // s_*_saveexec_b64: dst = EXEC, EXEC = op(src, EXEC)
            if (dst == 126) return fail(i, "saveexec into EXEC");
            if (const auto e = maskDest(dst, d); !e.empty()) return fail(i, e);
            static const char* fn[] = {"a && e", "a || e", "a != e", "a && !e"};
            ib << "  { const bool a = " << a << ", e = exec; " << d << " = e; exec = " << fn[op - 36] << "; }\n";
            execTouched = true; scc = Scc::wave; break;
          }
          if (const auto e = maskDest(dst, d); !e.empty()) return fail(i, e);
          ib << "  " << d << " = " << (op == 8 ? "!" : "") << a << ";\n";
          if (op == 8) scc = Scc::wave;
          break;
        }
        if (op == 32 || op == 33) return fail(i, "s_setpc/s_swappc (indirect control flow) not translated");
        if (op >= 36 && op <= 43) return fail(i, "saveexec form not translated");
        if (dst >= 104) return fail(i, "EXEC/VCC write is not a lane mask operation");
        if (!uniformSource(src)) return fail(i, "lane mask used as a scalar value");
        static const std::set<unsigned> wide{4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 31, 44};
        if (const auto e = scalarWrite(dst, wide.count(op) ? 2 : 1); !e.empty()) return fail(i, e);
        if (op != 3 && op != 4) { if (!execzEnds.empty()) return fail(i, "SCC written inside a region s_cbranch_execz may skip"); scc = Scc::unknown; }
        break;
      }
      case GCNShader::Enc::SOP2: {
        const unsigned dst = (w >> 16) & 127, a0 = w & 255, b0 = (w >> 8) & 255, op = i.op;
        const bool logic64 = op == 15 || op == 17 || op == 19 || op == 21 || op == 23 || op == 25 || op == 27 || op == 29;
        if (logic64 && (dst == 106 || dst == 126 || isMask(a0) || isMask(b0) || (dst <= 102 && masks.count(dst)))) {
          std::string a, b, d;
          if (!maskExpr(a0, a) || !maskExpr(b0, b)) return fail(i, "64-bit lane mask operand unsupported");
          if (const auto e = maskDest(dst, d); !e.empty()) return fail(i, e);
          std::string r;
          switch (op) {
          case 15: r = a + " && " + b; break; case 17: r = a + " || " + b; break; case 19: r = a + " != " + b; break;
          case 21: r = a + " && !" + b; break; case 23: r = a + " || !" + b; break; case 25: r = "!(" + a + " && " + b + ")"; break;
          case 27: r = "!(" + a + " || " + b + ")"; break; default: r = a + " == " + b; break;
          }
          ib << "  { const bool r = " << r << "; " << d << " = r; }\n";
          scc = Scc::wave; break;
        }
        if (dst >= 104) return fail(i, "scalar write to EXEC/VCC/special register");
        if (!uniformSource(a0) || !uniformSource(b0)) return fail(i, "lane mask used as a scalar value");
        static const std::set<unsigned> wide{11, 15, 17, 19, 21, 23, 25, 27, 29, 31, 33, 35, 37, 40, 41};
        if (const auto e = scalarWrite(dst, wide.count(op) ? 2 : 1); !e.empty()) return fail(i, e);
        scc = Scc::unknown;
        break;
      }
      case GCNShader::Enc::SOPK: {
        const unsigned dst = (w >> 16) & 127;
        if (i.op >= 3 && i.op <= 14) {                          // s_cmpk_*: uniform SCC
          if (!execzEnds.empty()) return fail(i, "SCC written inside a region s_cbranch_execz may skip");
          const std::string d = scalarExpr(dst, 0); if (!bad.empty()) return fail(i, bad);
          const int32_t k = int16_t(w & 0xffff); const uint32_t uk = w & 0xffff;
          static const char* ops[] = {"==", "!=", ">", ">=", "<", "<="};
          const unsigned n = i.op - 3;
          if (n < 6) ib << "  scc = gcn_i32(" << d << ") " << ops[n] << " gcn_i32(" << k << ");\n";
          else ib << "  scc = " << d << " " << ops[n - 6] << " " << uk << "u;\n";
          scc = Scc::uniform; break;
        }
        if (dst >= 104) return fail(i, "scalar write to EXEC/VCC/special register");
        if (i.op != 0 && !uniformSource(dst)) return fail(i, "lane mask used as a scalar value");
        if (const auto e = scalarWrite(dst, 1); !e.empty()) return fail(i, e);
        if (i.op != 0) scc = Scc::unknown;
        break;
      }
      case GCNShader::Enc::SOPC: {
        if (i.op > 13) return fail(i, "SOPC opcode not translated");
        if (!execzEnds.empty()) return fail(i, "SCC written inside a region s_cbranch_execz may skip");
        const std::string a = scalarExpr(w & 255, i.w1), b = scalarExpr((w >> 8) & 255, i.w1);
        if (!bad.empty()) return fail(i, bad);
        static const char* ops[] = {"==", "!=", ">", ">=", "<", "<="};
        if (i.op < 6) ib << "  scc = gcn_i32(" << a << ") " << ops[i.op] << " gcn_i32(" << b << ");\n";
        else if (i.op < 12) ib << "  scc = " << a << " " << ops[i.op - 6] << " " << b << ";\n";
        else ib << "  scc = ((" << a << " >> (" << b << " & 31u)) & 1u) " << (i.op == 12 ? "== 0u" : "!= 0u") << ";\n";
        scc = Scc::uniform; break;
      }
      case GCNShader::Enc::SMRD: {
        const unsigned dst = (w >> 15) & 127, op = i.op, sbase = ((w >> 9) & 63) * 2;
        const unsigned count = op == 0 || op == 8 ? 1 : op == 1 || op == 9 ? 2 : op == 2 || op == 10 ? 4 : op == 3 || op == 11 ? 8 : 16;
        if (!uniformSource(sbase) || (!(w & 256) && !uniformSource(w & 255))) return fail(i, "lane mask used as a scalar value");
        if (const auto e = scalarWrite(dst, count); !e.empty()) return fail(i, e);
        break;
      }
      case GCNShader::Enc::VOP1: case GCNShader::Enc::VOP2: case GCNShader::Enc::VOP3: case GCNShader::Enc::VOPC: {
        unsigned op = 0, src0 = 0, src1 = 0, src2 = 0, vdst = 0, sdst = 106, absMask = 0, negMask = 0, omod = 0;
        bool clamp = false, hasSrc2 = false, vop3b = false;
        uint32_t literal = i.w1, k = 0;
        switch (i.enc) {
        case GCNShader::Enc::VOP1: op = 384 + i.op; src0 = w & 511; vdst = (w >> 17) & 255; break;
        case GCNShader::Enc::VOP2:
          op = 256 + i.op; src0 = w & 511; src1 = 256 + ((w >> 9) & 255); vdst = (w >> 17) & 255;
          if (i.op == 32 || i.op == 33) k = i.w1;
          break;
        case GCNShader::Enc::VOPC: op = i.op; src0 = w & 511; src1 = 256 + ((w >> 9) & 255); break;
        default:
          op = i.op; vdst = w & 255; src0 = i.w1 & 511; src1 = (i.w1 >> 9) & 511; src2 = (i.w1 >> 18) & 511; hasSrc2 = true; literal = 0;
          if (op >= 293 && op <= 298) { vop3b = true; sdst = (w >> 8) & 127; if (i.w1 >> 27) return fail(i, "VOP3b modifiers"); }
          else { absMask = (w >> 8) & 7; clamp = (w >> 11) & 1; omod = (i.w1 >> 27) & 3; negMask = i.w1 >> 29; if (op < 256) sdst = vdst; }
          break;
        }
        if ((absMask || negMask) && !GCNShader::isFloatInput(op)) return fail(i, "input modifiers on non-float operation");
        if ((clamp || omod) && !GCNShader::isFloatOutput(op) && !(clamp && op < 256)) return fail(i, "output modifiers on non-float result");
        auto src = [&](unsigned index, unsigned code) {
          std::string e = operand(code, literal);
          if ((absMask >> index) & 1) e = "(" + e + " & 0x7fffffffu)";
          if ((negMask >> index) & 1) e = "(" + e + " ^ 0x80000000u)";
          return e;
        };
        if (op < 256) { // compares: inactive lanes write 0; V_CMPX also writes EXEC
          const bool cmpx = (op & 0x10) != 0;
          const unsigned compare = op & ~0x10u;
          if (!gcn_compare_supported(compare) || (compare & 0xf0u) == 0x10u) return fail(i, "compare opcode (class or 64-bit) not translated");
          if (sdst != 106) return fail(i, "compare result outside VCC");
          const std::string a = src(0, src0), b = src(1, src1); if (!bad.empty()) return fail(i, bad);
          if (cmpx) { ib << "  { const bool c = exec && gcn_compare(" << compare << "u, " << a << ", " << b << "); exec = c; vcc = c; }\n"; execTouched = true; }
          else ib << "  vcc = exec && gcn_compare(" << compare << "u, " << a << ", " << b << ");\n";
          break;
        }
        if (!gcn_valu_supported(op)) return fail(i, "vector ALU opcode " + std::to_string(op) + " not translated");
        if (vdst >= vgprs) return fail(i, "VGPR destination beyond the allocation");
        const std::string a = src(0, src0), b = op >= 384 ? "0u" : src(1, src1);
        std::string c = "0u";
        if (op == 287 || op == 262) c = "v[" + std::to_string(vdst) + "]";
        else if (op == 288 || op == 289) c = hex(k);
        else if (hasSrc2 && op >= 320 && op < 384) c = src(2, src2);
        if (!bad.empty()) return fail(i, bad);
        const bool usesCarry = op == 256 || (op >= 293 && op <= 298);
        if (op == 256 && i.enc == GCNShader::Enc::VOP3 && src2 != 106) return fail(i, "v_cndmask condition outside VCC");
        if (vop3b && sdst != 106) return fail(i, "carry-out outside VCC");
        ib << "  { bool k = vcc; gcn_u32 r = gcn_valu(" << op << "u, " << a << ", " << b << ", " << c << ", k);";
        if (GCNShader::isFloatOutput(op) && (omod || clamp)) ib << " r = gcn_omod(r, " << omod << "u, " << (clamp ? "true" : "false") << ");";
        ib << " if (exec) v[" << vdst << "] = r;";
        if (usesCarry && op != 256) ib << " vcc = exec && k;";
        ib << " (void)k; }\n";
        break;
      }
      case GCNShader::Enc::VINTRP: {
        const unsigned vdst = (w >> 18) & 255, attr = (w >> 10) & 63, chan = (w >> 8) & 3, s = w & 255;
        if (vdst >= vgprs) return fail(i, "VGPR destination beyond the allocation");
        out.usesInterpolation = true;
        if (i.op == 2) {
          if (s > 2) return fail(i, "v_interp_mov source");
          ib << "  if (exec) v[" << vdst << "] = gcn_b(GCN_ENV_INTERP(" << attr << "u, " << chan << "u, " << (s == 0 ? 1 : s == 1 ? 2 : 0) << "u));\n";
        } else if (i.op <= 1) {
          if (s >= vgprs) return fail(i, "interpolation VGPR beyond the allocation");
          if (i.op == 0) ib << "  if (exec) v[" << vdst << "] = gcn_interp(GCN_ENV_INTERP(" << attr << "u, " << chan << "u, 1u), v[" << s
                            << "], GCN_ENV_INTERP(" << attr << "u, " << chan << "u, 0u));\n";
          else ib << "  if (exec) v[" << vdst << "] = gcn_interp(GCN_ENV_INTERP(" << attr << "u, " << chan << "u, 2u), v[" << s
                  << "], gcn_f(v[" << vdst << "]));\n";
        } else return fail(i, "VINTRP opcode not translated");
        break;
      }
      case GCNShader::Enc::EXP: {
        const unsigned enable = w & 15, target = (w >> 4) & 63, compressed = (w >> 10) & 1, done = (w >> 11) & 1;
        ib << "  { gcn_u32 e[4] = {";
        for (unsigned c = 0; c < 4; ++c) {
          const unsigned r = (i.w1 >> (c * 8)) & 255;
          const bool used = compressed ? (c < 2 && ((enable >> (c * 2)) & 3)) : ((enable >> c) & 1);
          if (used && r >= vgprs) return fail(i, "export VGPR beyond the allocation");
          ib << (c ? ", " : "") << (used ? "v[" + std::to_string(r) + "]" : std::string("0u"));
        }
        // `live`: the fragment's EXEC bit, i.e. whether the hardware exports this lane.
        ib << "}; GCN_ENV_EXPORT(" << target << "u, " << enable << "u, " << compressed << "u, " << done << "u, e, exec); }\n";
        out.exportTargets.insert(target);
        ++out.exports;
        if (target == 0) out.mrt0Compressed = compressed != 0;
        break;
      }
      case GCNShader::Enc::MIMG: {
        const unsigned op = i.op, dmask = (w >> 8) & 15, vaddr = i.w1 & 255, vdata = (i.w1 >> 8) & 255;
        const unsigned rsrc = ((i.w1 >> 16) & 31) * 4, ssamp = ((i.w1 >> 21) & 31) * 4;
        if ((w >> 14) & 0xf) return fail(i, "MIMG DA/R128/TFE/LWE unsupported");
        if (op != 0 && op != 32 && op != 36 && op != 39) return fail(i, "image opcode not translated");
        const unsigned coords = op == 36 ? 3 : 2, results = unsigned(__builtin_popcount(dmask));
        if (vaddr + coords > vgprs || vdata + results > vgprs) return fail(i, "image VGPRs beyond the allocation");
        for (unsigned r = rsrc; r < rsrc + 8; ++r) { if (!uniformSource(r)) return fail(i, "lane mask used as a resource"); if (!written.count(r)) readBeforeWrite.insert(r); }
        if (op) for (unsigned r = ssamp; r < ssamp + 4; ++r) { if (!uniformSource(r)) return fail(i, "lane mask used as a sampler"); if (!written.count(r)) readBeforeWrite.insert(r); }
        out.usesImages = true;
        unsigned slot = 0;
        while (slot < out.images.size() && !(out.images[slot].rsrc == rsrc && out.images[slot].sampled == (op != 0) &&
                                             (op == 0 || out.images[slot].ssamp == ssamp))) ++slot;
        if (slot == out.images.size()) out.images.push_back({rsrc, op != 0 ? ssamp : 0u, op != 0});
        ib << "  if (exec) { gcn_u32 c[4] = {v[" << vaddr << "], v[" << vaddr + 1 << "], " << (coords > 2 ? "v[" + std::to_string(vaddr + 2) + "]" : std::string("0u"))
           << ", 0u}; gcn_u32 t[4] = {0u, 0u, 0u, 0u}; GCN_ENV_SAMPLE(" << slot << "u, " << hex(w) << ", " << hex(i.w1) << ", c, t);";
        for (unsigned c = 0, n = 0; c < 4; ++c) if ((dmask >> c) & 1) ib << " v[" << vdata + n++ << "] = t[" << c << "];";
        ib << " }\n";
        break;
      }
      default:
        return fail(i, "encoding not translated (buffer access in pixel shaders)");
      }
      body << pre.str();
      const std::string text = ib.str();
      if (!text.empty()) {
        if (guard.empty()) body << text;
        else body << "  if (!(" << guard << ")) {\n" << text << "  }\n";
      }
      pc = next;
    }
    out.error = "program longer than 4096 instructions or missing s_endpgm"; return out;
  }

  // Translated code computes in fp32 denormal mode 0 (GCNKernel.h). A backend
  // may run it for a draw only when the shader's SPI_SHADER_PGM_RSRC1 selects
  // that mode; other modes stay on the interpreter.
  static bool floatModeSupported(uint32_t rsrc1) { return ((rsrc1 >> 16) & 3) == 0; }

  // Complete function text: `name(s, v, env)` shading one fragment.
  static std::string function(const std::string& name, const Result& r) {
    std::ostringstream f;
    f << "GCN_INLINE void " << name << "(GCN_UNIFORM const gcn_u32* s, GCN_THREAD gcn_u32* v, GCN_ENV_PARAM) {\n"
      << "  bool vcc = false, exec = true, scc = false; (void)vcc; (void)exec; (void)scc; (void)s;\n" << r.body << "}\n";
    return f.str();
  }
};
}
