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
// * EXEC: only live-mask idioms are accepted (s_wqm_b64 exec, saving EXEC
//   into SGPRs and restoring it). Branches, V_CMPX and other EXEC writes are
//   refused: per-lane divergence is not modelled.
// * VCC is a per-fragment boolean (compares, carries, v_cndmask).
// * Interpolation, image sampling and exports call macros the including
//   backend defines (GCN_ENV_INTERP, GCN_ENV_SAMPLE, GCN_ENV_EXPORT).
//   GCN_ENV_SAMPLE(slot, w0, w1, c, t) names the image by its slot in
//   Result::images; GCNKernelImage.h's gcn_image implements it.
// Anything else fails with the exact program counter; nothing is guessed.
#include "GCNShader.hpp"
#include "GCNKernel.h"
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace maxps4::gpu {
struct GCNShaderTranslator {
  struct Result {
    bool ok = false;
    std::string error, body;   // body: statements of the function (uses s, v, vcc)
    unsigned instructions = 0, vgprs = 0;
    bool usesInterpolation = false, usesImages = false, needsScalarPrepass = false;
    std::set<unsigned> exportTargets;
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
    Result out; out.vgprs = vgprs;
    std::ostringstream body;
    std::set<unsigned> written, readBeforeWrite, execAlias;
    GCNShader decoder; WordMemory memory{base, words};
    uint64_t pc = base;
    auto fail = [&](const GCNShader::Inst& i, const std::string& why) {
      std::ostringstream m; m << "pc=0x" << std::hex << pc << ' ' << GCNShader::opName(i.enc, i.op) << ": " << why;
      out.error = m.str(); out.ok = false; return out;
    };
    for (unsigned steps = 0; steps < 4096; ++steps) {
      GCNShader::Inst i; std::string why;
      if (!decoder.fetch(memory, pc, i, why)) { out.error = "pc=0x" + hex(pc) + ": " + why; return out; }
      const uint32_t w = i.w0;
      ++out.instructions;
      // ---- scalar destinations: write-once, never EXEC/VCC except idioms ----
      auto scalarWrite = [&](unsigned dst, unsigned count) -> std::string {
        for (unsigned k = 0; k < count; ++k) {
          const unsigned r = dst + k;
          if (r > 103) return "scalar write to special register " + std::to_string(r);
          if (written.count(r)) return "SGPR " + std::to_string(r) + " written twice (scalar prepass needs write-once SGPRs)";
          if (readBeforeWrite.count(r)) return "SGPR " + std::to_string(r) + " read by vector code before its write";
          written.insert(r);
        }
        out.needsScalarPrepass = true; return {};
      };
      // ---- vector operand expressions ----
      std::string bad;
      auto operand = [&](unsigned code, uint32_t literal) -> std::string {
        if (code >= 256) {
          if (code - 256 >= vgprs) { bad = "VGPR " + std::to_string(code - 256) + " beyond the allocation"; return "0u"; }
          return "v[" + std::to_string(code - 256) + "]";
        }
        if (code <= 103) { if (!written.count(code)) readBeforeWrite.insert(code); return "s[" + std::to_string(code) + "]"; }
        if (code >= 128 && code <= 192) return std::to_string(code - 128) + "u";
        if (code >= 193 && code <= 208) return "gcn_u32(" + std::to_string(192 - int(code)) + ")";
        static const char* floats[] = {"0x3f000000u", "0xbf000000u", "0x3f800000u", "0xbf800000u", "0x40000000u", "0xc0000000u", "0x40800000u", "0xc0800000u"};
        if (code >= 240 && code <= 247) return floats[code - 240];
        if (code == 255) return hex(literal);
        bad = "vector operand " + std::to_string(code) + " (EXEC/VCC/M0 as a value) unsupported"; return "0u";
      };
      const uint64_t next = pc + 4ull * i.length;
      switch (i.enc) {
      case GCNShader::Enc::SOPP:
        if (i.op == 0 || i.op == 12) break;                     // s_nop, s_waitcnt
        if (i.op == 1) { out.ok = true; out.body = body.str(); return out; }  // s_endpgm
        return fail(i, "control flow is not translated (no per-lane divergence model)");
      case GCNShader::Enc::SOP1: {
        const unsigned dst = (w >> 16) & 127, src = w & 255;
        if (i.op == 10 && dst == 126 && src == 126) break;          // s_wqm_b64 exec, exec: live mask
        if (i.op == 4 && src == 126) {                              // s_mov_b64 sN, exec (save)
          if (dst > 102 || (dst & 1)) return fail(i, "EXEC saved outside an SGPR pair");
          execAlias.insert(dst); break;
        }
        if (i.op == 4 && dst == 126 && execAlias.count(src)) break; // s_mov_b64 exec, sN (restore)
        if (i.op == 32 || i.op == 33 || (i.op >= 36 && i.op <= 43)) return fail(i, "control flow / *_saveexec not translated");
        if (dst >= 104) return fail(i, "EXEC/VCC write is not a live-mask idiom");
        static const std::set<unsigned> wide{4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 31, 44};
        if (const auto e = scalarWrite(dst, wide.count(i.op) ? 2 : 1); !e.empty()) return fail(i, e);
        break;
      }
      case GCNShader::Enc::SOP2: {
        const unsigned dst = (w >> 16) & 127;
        if (dst >= 104) return fail(i, "scalar write to EXEC/VCC/special register");
        static const std::set<unsigned> wide{11, 15, 17, 19, 21, 23, 25, 27, 29, 31, 33, 35, 37, 40, 41};
        if (const auto e = scalarWrite(dst, wide.count(i.op) ? 2 : 1); !e.empty()) return fail(i, e);
        break;
      }
      case GCNShader::Enc::SOPK: {
        if (i.op >= 3 && i.op <= 14) break;                         // s_cmpk_*: SCC only
        const unsigned dst = (w >> 16) & 127;
        if (dst >= 104) return fail(i, "scalar write to EXEC/VCC/special register");
        if (const auto e = scalarWrite(dst, 1); !e.empty()) return fail(i, e);
        break;
      }
      case GCNShader::Enc::SOPC: return fail(i, "scalar compares feed SCC branches (control flow not translated)");
      case GCNShader::Enc::SMRD: {
        const unsigned dst = (w >> 15) & 127, op = i.op;
        const unsigned count = op == 0 || op == 8 ? 1 : op == 1 || op == 9 ? 2 : op == 2 || op == 10 ? 4 : op == 3 || op == 11 ? 8 : 16;
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
        if (op < 256) { // compares: VCC (or a VOP3 sdst that is VCC)
          if (!gcn_compare_supported(op)) return fail(i, "compare opcode (V_CMPX or class) not translated");
          if (sdst != 106) return fail(i, "compare result outside VCC");
          const std::string a = src(0, src0), b = src(1, src1); if (!bad.empty()) return fail(i, bad);
          body << "  vcc = gcn_compare(" << op << "u, " << a << ", " << b << ");\n";
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
        body << "  { bool k = vcc; gcn_u32 r = gcn_valu(" << op << "u, " << a << ", " << b << ", " << c << ", k);";
        if (usesCarry && op != 256) body << " vcc = k;";
        if (GCNShader::isFloatOutput(op) && (omod || clamp)) body << " r = gcn_omod(r, " << omod << "u, " << (clamp ? "true" : "false") << ");";
        body << " v[" << vdst << "] = r; }\n";
        break;
      }
      case GCNShader::Enc::VINTRP: {
        const unsigned vdst = (w >> 18) & 255, attr = (w >> 10) & 63, chan = (w >> 8) & 3, s = w & 255;
        if (vdst >= vgprs) return fail(i, "VGPR destination beyond the allocation");
        out.usesInterpolation = true;
        if (i.op == 2) {
          if (s > 2) return fail(i, "v_interp_mov source");
          body << "  v[" << vdst << "] = gcn_b(GCN_ENV_INTERP(" << attr << "u, " << chan << "u, " << (s == 0 ? 1 : s == 1 ? 2 : 0) << "u));\n";
        } else if (i.op <= 1) {
          if (s >= vgprs) return fail(i, "interpolation VGPR beyond the allocation");
          if (i.op == 0) body << "  v[" << vdst << "] = gcn_b(GCN_ENV_INTERP(" << attr << "u, " << chan << "u, 1u) * gcn_f(v[" << s
                              << "]) + GCN_ENV_INTERP(" << attr << "u, " << chan << "u, 0u));\n";
          else body << "  v[" << vdst << "] = gcn_b(GCN_ENV_INTERP(" << attr << "u, " << chan << "u, 2u) * gcn_f(v[" << s
                    << "]) + gcn_f(v[" << vdst << "]));\n";
        } else return fail(i, "VINTRP opcode not translated");
        break;
      }
      case GCNShader::Enc::EXP: {
        const unsigned enable = w & 15, target = (w >> 4) & 63, compressed = (w >> 10) & 1, done = (w >> 11) & 1;
        body << "  { gcn_u32 e[4] = {";
        for (unsigned c = 0; c < 4; ++c) {
          const unsigned r = (i.w1 >> (c * 8)) & 255;
          const bool used = compressed ? (c < 2 && ((enable >> (c * 2)) & 3)) : ((enable >> c) & 1);
          if (used && r >= vgprs) return fail(i, "export VGPR beyond the allocation");
          body << (c ? ", " : "") << (used ? "v[" + std::to_string(r) + "]" : std::string("0u"));
        }
        body << "}; GCN_ENV_EXPORT(" << target << "u, " << enable << "u, " << compressed << "u, " << done << "u, e); }\n";
        out.exportTargets.insert(target);
        break;
      }
      case GCNShader::Enc::MIMG: {
        const unsigned op = i.op, dmask = (w >> 8) & 15, vaddr = i.w1 & 255, vdata = (i.w1 >> 8) & 255;
        const unsigned rsrc = ((i.w1 >> 16) & 31) * 4, ssamp = ((i.w1 >> 21) & 31) * 4;
        if ((w >> 14) & 0xf) return fail(i, "MIMG DA/R128/TFE/LWE unsupported");
        if (op != 0 && op != 32 && op != 36 && op != 39) return fail(i, "image opcode not translated");
        const unsigned coords = op == 36 ? 3 : 2, results = unsigned(__builtin_popcount(dmask));
        if (vaddr + coords > vgprs || vdata + results > vgprs) return fail(i, "image VGPRs beyond the allocation");
        for (unsigned r = rsrc; r < rsrc + 8; ++r) if (!written.count(r)) readBeforeWrite.insert(r);
        if (op) for (unsigned r = ssamp; r < ssamp + 4; ++r) if (!written.count(r)) readBeforeWrite.insert(r);
        out.usesImages = true;
        unsigned slot = 0;
        while (slot < out.images.size() && !(out.images[slot].rsrc == rsrc && out.images[slot].sampled == (op != 0) &&
                                             (op == 0 || out.images[slot].ssamp == ssamp))) ++slot;
        if (slot == out.images.size()) out.images.push_back({rsrc, op != 0 ? ssamp : 0u, op != 0});
        body << "  { gcn_u32 c[4] = {v[" << vaddr << "], v[" << vaddr + 1 << "], " << (coords > 2 ? "v[" + std::to_string(vaddr + 2) + "]" : std::string("0u"))
             << ", 0u}; gcn_u32 t[4] = {0u, 0u, 0u, 0u}; GCN_ENV_SAMPLE(" << slot << "u, " << hex(w) << ", " << hex(i.w1) << ", c, t);";
        for (unsigned c = 0, n = 0; c < 4; ++c) if ((dmask >> c) & 1) body << " v[" << vdata + n++ << "] = t[" << c << "];";
        body << " }\n";
        break;
      }
      default:
        return fail(i, "encoding not translated (buffer access in pixel shaders)");
      }
      pc = next;
    }
    out.error = "program longer than 4096 instructions or missing s_endpgm"; return out;
  }

  // Complete function text: `name(s, v, env)` shading one fragment.
  static std::string function(const std::string& name, const Result& r) {
    std::ostringstream f;
    f << "GCN_INLINE void " << name << "(GCN_UNIFORM const gcn_u32* s, GCN_THREAD gcn_u32* v, GCN_ENV_PARAM) {\n"
      << "  bool vcc = false; (void)vcc; (void)s;\n" << r.body << "}\n";
    return f.str();
  }
};
}
