#pragma once
// Bounded, synchronous GCN1/2 (base PS4, gfx7 encodings) wave64 interpreter
// for graphics stages. Instructions are fetched from guest memory through the
// checked Memory interface; nothing is translated to host code. Unknown
// encodings, opcodes, operand kinds or resource formats fail closed with the
// exact program counter: a result is never invented for an unimplemented
// instruction.
#include "GCNTiling.hpp"
#include "GPUHostSpan.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace maxps4::gpu {
inline float gcnFloat(uint32_t v) { float f; std::memcpy(&f, &v, 4); return f; }
inline uint32_t gcnBits(float f) { uint32_t v; std::memcpy(&v, &f, 4); return v; }
// IEEE binary16 conversion. rtz=true is V_CVT_PKRTZ_F16_F32 rounding.
inline uint16_t gcnHalf(float value, bool rtz) {
  const uint32_t f = gcnBits(value), sign = (f >> 16) & 0x8000U;
  const int exponent = int((f >> 23) & 255) - 127 + 15;
  uint32_t mantissa = f & 0x7fffffU;
  if (((f >> 23) & 255) == 255)
    return uint16_t(sign | 0x7c00U | (mantissa ? 0x200U : 0));
  if (exponent >= 31) return uint16_t(sign | (rtz ? 0x7bffU : 0x7c00U));
  if (exponent <= 0) {
    if (exponent < -10) return uint16_t(sign);
    mantissa |= 0x800000U;
    const unsigned shift = unsigned(14 - exponent);
    uint32_t half = mantissa >> shift;
    if (!rtz) {
      const uint32_t rest = mantissa & ((1U << shift) - 1), mid = 1U << (shift - 1);
      if (rest > mid || (rest == mid && (half & 1))) ++half;
    }
    return uint16_t(sign | half);
  }
  uint32_t half = (uint32_t(exponent) << 10) | (mantissa >> 13);
  if (!rtz) {
    const uint32_t rest = mantissa & 0x1fffU;
    if (rest > 0x1000U || (rest == 0x1000U && (half & 1))) ++half;
  }
  return uint16_t(sign | half);
}
inline float gcnHalfToFloat(uint16_t h) {
  const uint32_t sign = uint32_t(h & 0x8000U) << 16, exponent = (h >> 10) & 31,
                 mantissa = h & 1023U;
  if (!exponent) {
    const float v = std::ldexp(float(mantissa), -24);
    return sign ? -v : v;
  }
  if (exponent == 31) return gcnFloat(sign | 0x7f800000U | (mantissa << 13));
  return gcnFloat(sign | ((exponent + 112) << 23) | (mantissa << 13));
}
inline float srgbToLinear(float c) {
  return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
inline float linearToSrgb(float c) {
  if (!(c > 0)) return 0;
  if (c >= 1) return 1;
  return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

// Table forms of the conversions above, equal bit for bit to them:
// srgbByteToLinear(b) == srgbToLinear(b / 255.0f) and
// linearToSrgbByte(v) == uint8_t(std::lround(linearToSrgb(v) * 255.0f)).
inline float srgbByteToLinear(uint8_t b) {
  static const auto table = [] { std::array<float, 256> t{}; for (unsigned i = 0; i < 256; ++i) t[i] = srgbToLinear(i / 255.0f); return t; }();
  return table[b];
}
inline uint8_t linearToSrgbByteDirect(float v) { return uint8_t(std::lround(linearToSrgb(v) * 255.0f)); }
inline uint8_t linearToSrgbByte(float v) {
  // thresholds[k] = smallest float in [0, 1] whose direct conversion is >= k
  // (the quantised curve is non-decreasing; ordered by bit pattern).
  static const auto thresholds = [] {
    std::array<float, 256> t{};
    auto bits = [](float f) { uint32_t b; std::memcpy(&b, &f, 4); return b; };
    auto value = [](uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; };
    for (unsigned k = 1; k < 256; ++k) {
      uint32_t lo = 0, hi = bits(1.0f);
      while (lo < hi) { const uint32_t mid = lo + (hi - lo) / 2; if (linearToSrgbByteDirect(value(mid)) >= k) hi = mid; else lo = mid + 1; }
      t[k] = value(lo);
    }
    return t;
  }();
  if (!(v > 0)) return 0;
  if (v >= 1) return 255;
  return uint8_t(std::upper_bound(thresholds.begin() + 1, thresholds.end(), v) - (thresholds.begin() + 1));
}

struct GCNShader {
  enum class Enc : uint8_t { SOP2, SOPK, SOP1, SOPC, SOPP, SMRD, VOP2, VOP1, VOPC, VOP3, VINTRP, EXP, MUBUF, MTBUF, MIMG };
  struct Inst { uint32_t w0 = 0, w1 = 0; uint8_t length = 1; Enc enc = Enc::SOPP; uint16_t op = 0; };
  struct Export {
    unsigned target = 0, enable = 0;
    bool compressed = false, done = false, validMask = false;
    uint64_t exec = 0;
    std::array<std::array<uint32_t, 64>, 4> data{};
  };
  // Interpolation inputs for one lane: P0, P10, P20 of a PS input channel.
  using Interpolant = std::function<bool(unsigned attr, unsigned chan, unsigned lane, float out[3])>;
  struct Wave {
    std::array<uint32_t, 128> s{};
    bool scc = false;
    unsigned sgprs = 0, vgprs = 0;
    std::vector<std::array<uint32_t, 64>> v;
    std::vector<Export> exports;
    Interpolant interpolate;
    // fp32 denormal mode, FLOAT_MODE[5:4] of SPI_SHADER_PGM_RSRC1 (bits 17:16)
    // as MODE.FP_DENORM: 0 flush inputs and outputs, 1 flush outputs only,
    // 2 flush inputs only, 3 keep both. Callers that do not set it keep them.
    unsigned fp32Denorm = 3;
    uint64_t exec() const { return uint64_t(s[126]) | (uint64_t(s[127]) << 32); }
    void setExec(uint64_t m) { s[126] = uint32_t(m); s[127] = uint32_t(m >> 32); }
    uint64_t vcc() const { return uint64_t(s[106]) | (uint64_t(s[107]) << 32); }
    void setVcc(uint64_t m) { s[106] = uint32_t(m); s[107] = uint32_t(m >> 32); }
    void configure(unsigned scalarCount, unsigned vectorCount) {
      sgprs = scalarCount; vgprs = vectorCount;
      v.assign(vectorCount, std::array<uint32_t, 64>{});
    }
  };
  struct Stats {
    uint64_t waves = 0, instructions = 0, scalarLoads = 0, bufferLoads = 0,
             bufferOutOfRange = 0, samples = 0, exports = 0, texels = 0, texelsNonZero = 0, textureBase = 0,
             textureWidth = 0, textureHeight = 0, texturePitch = 0, textureTile = 0, textureDfmt = 0, textureNfmt = 0;
    std::map<uint32_t, uint64_t> histogram; // (encoding << 16) | opcode
    void merge(const Stats& o) {
      waves += o.waves; instructions += o.instructions; scalarLoads += o.scalarLoads; bufferLoads += o.bufferLoads;
      bufferOutOfRange += o.bufferOutOfRange; samples += o.samples; exports += o.exports; texels += o.texels; texelsNonZero += o.texelsNonZero;
      if (o.texels) { textureBase = o.textureBase; textureWidth = o.textureWidth; textureHeight = o.textureHeight;
                      texturePitch = o.texturePitch; textureTile = o.textureTile; textureDfmt = o.textureDfmt; textureNfmt = o.textureNfmt; }
      for (const auto& [k, v] : o.histogram) histogram[k] += v;
    }
  } stats;
  template <class Memory> static uint8_t* hostSpan(const Memory& m, uint64_t guest, uint64_t bytes, uint32_t permissions) {
    return gpu::hostSpan(m, guest, bytes, permissions);
  }
  static constexpr uint64_t maxStepsPerWave = 1u << 20;
  static constexpr uint64_t addressMask = (uint64_t(1) << 40) - 1; // PS4 GPU VA
  std::unordered_map<uint64_t, Inst> cache;

  static const char* encodingName(Enc e) {
    static const char* names[] = {"SOP2", "SOPK", "SOP1", "SOPC", "SOPP", "SMRD", "VOP2", "VOP1",
                                  "VOPC", "VOP3", "VINTRP", "EXP", "MUBUF", "MTBUF", "MIMG"};
    return names[unsigned(e)];
  }
  // Mnemonics for the implemented subset (diagnostics only).
  static std::string opName(Enc e, unsigned op) {
    auto from = [&](std::initializer_list<std::pair<unsigned, const char*>> t) -> std::string {
      for (const auto& p : t) if (p.first == op) return p.second;
      return std::string(encodingName(e)) + "." + std::to_string(op);
    };
    switch (e) {
    case Enc::SOP1: return from({{3, "s_mov_b32"}, {4, "s_mov_b64"}, {7, "s_not_b32"}, {9, "s_wqm_b32"}, {10, "s_wqm_b64"}, {31, "s_getpc_b64"}, {32, "s_setpc_b64"}, {33, "s_swappc_b64"}, {36, "s_and_saveexec_b64"}, {37, "s_or_saveexec_b64"}});
    case Enc::SOPP: return from({{0, "s_nop"}, {1, "s_endpgm"}, {2, "s_branch"}, {4, "s_cbranch_scc0"}, {5, "s_cbranch_scc1"}, {6, "s_cbranch_vccz"}, {7, "s_cbranch_vccnz"}, {8, "s_cbranch_execz"}, {9, "s_cbranch_execnz"}, {12, "s_waitcnt"}});
    case Enc::SMRD: return from({{0, "s_load_dword"}, {1, "s_load_dwordx2"}, {2, "s_load_dwordx4"}, {3, "s_load_dwordx8"}, {4, "s_load_dwordx16"}, {8, "s_buffer_load_dword"}, {9, "s_buffer_load_dwordx2"}, {10, "s_buffer_load_dwordx4"}, {11, "s_buffer_load_dwordx8"}, {12, "s_buffer_load_dwordx16"}});
    case Enc::VOP1: return from({{1, "v_mov_b32"}, {5, "v_cvt_f32_i32"}, {6, "v_cvt_f32_u32"}, {7, "v_cvt_u32_f32"}, {8, "v_cvt_i32_f32"}, {42, "v_rcp_f32"}, {46, "v_rsq_f32"}, {51, "v_sqrt_f32"}});
    case Enc::VOP2: return from({{0, "v_cndmask_b32"}, {3, "v_add_f32"}, {4, "v_sub_f32"}, {5, "v_subrev_f32"}, {8, "v_mul_f32"}, {26, "v_lshlrev_b32"}, {27, "v_and_b32"}, {28, "v_or_b32"}, {31, "v_mac_f32"}, {32, "v_madmk_f32"}, {33, "v_madak_f32"}, {37, "v_add_i32"}, {47, "v_cvt_pkrtz_f16_f32"}});
    case Enc::VOP3: return from({{321, "v_mad_f32"}, {331, "v_fma_f32"}, {303, "v_cvt_pkrtz_f16_f32"}, {264, "v_mul_f32"}, {259, "v_add_f32"}});
    case Enc::VINTRP: return from({{0, "v_interp_p1_f32"}, {1, "v_interp_p2_f32"}, {2, "v_interp_mov_f32"}});
    case Enc::EXP: return "exp";
    case Enc::MUBUF: return from({{0, "buffer_load_format_x"}, {1, "buffer_load_format_xy"}, {2, "buffer_load_format_xyz"}, {3, "buffer_load_format_xyzw"}, {12, "buffer_load_dword"}});
    case Enc::MTBUF: return from({{0, "tbuffer_load_format_x"}, {1, "tbuffer_load_format_xy"}, {2, "tbuffer_load_format_xyz"}, {3, "tbuffer_load_format_xyzw"}});
    case Enc::MIMG: return from({{0, "image_load"}, {32, "image_sample"}, {36, "image_sample_l"}, {39, "image_sample_lz"}});
    default: return std::string(encodingName(e)) + "." + std::to_string(op);
    }
  }
  std::string histogramText(size_t limit = 64) const {
    std::ostringstream out;
    size_t n = 0;
    for (const auto& [key, count] : stats.histogram) {
      if (n++ == limit) { out << " ..."; break; }
      out << (n > 1 ? "," : "") << opName(Enc(key >> 16), key & 0xffff) << ':' << count;
    }
    return out.str();
  }

  template <class Memory>
  bool fetch(Memory& memory, uint64_t pc, Inst& out, std::string& error) {
    if (auto it = cache.find(pc); it != cache.end()) { out = it->second; return true; }
    Inst i;
    if ((pc & 3) || memory.read_data(pc, &i.w0, 4)) { error = "instruction memory unreadable"; return false; }
    const uint32_t w = i.w0;
    bool literal = false;
    if ((w >> 23) == 0x17d) { i.enc = Enc::SOP1; i.op = (w >> 8) & 255; literal = (w & 255) == 255; }
    else if ((w >> 23) == 0x17e) { i.enc = Enc::SOPC; i.op = (w >> 16) & 127; literal = (w & 255) == 255 || ((w >> 8) & 255) == 255; }
    else if ((w >> 23) == 0x17f) { i.enc = Enc::SOPP; i.op = (w >> 16) & 127; }
    else if ((w >> 28) == 0xb) { i.enc = Enc::SOPK; i.op = (w >> 23) & 31; }
    else if ((w >> 30) == 2) { i.enc = Enc::SOP2; i.op = (w >> 23) & 127; literal = (w & 255) == 255 || ((w >> 8) & 255) == 255; }
    else if ((w >> 27) == 0x18) { i.enc = Enc::SMRD; i.op = (w >> 22) & 31; if (!(w & 256) && (w & 255) == 255) { error = "SMRD literal offset unsupported"; return false; } }
    else if ((w >> 25) == 0x3f) { i.enc = Enc::VOP1; i.op = (w >> 9) & 255; literal = (w & 511) == 255; }
    else if ((w >> 25) == 0x3e) { i.enc = Enc::VOPC; i.op = (w >> 17) & 255; literal = (w & 511) == 255; }
    else if (!(w >> 31)) { i.enc = Enc::VOP2; i.op = (w >> 25) & 63; literal = (w & 511) == 255 || i.op == 32 || i.op == 33; }
    else if ((w >> 26) == 0x34) { i.enc = Enc::VOP3; i.op = (w >> 17) & 511; i.length = 2; }
    else if ((w >> 26) == 0x32) { i.enc = Enc::VINTRP; i.op = (w >> 16) & 3; }
    else if ((w >> 26) == 0x3e) { i.enc = Enc::EXP; i.length = 2; }
    else if ((w >> 26) == 0x38) { i.enc = Enc::MUBUF; i.op = (w >> 18) & 127; i.length = 2; }
    else if ((w >> 26) == 0x3a) { i.enc = Enc::MTBUF; i.op = (w >> 16) & 7; i.length = 2; }
    else if ((w >> 26) == 0x3c) { i.enc = Enc::MIMG; i.op = (w >> 18) & 127; i.length = 2; }
    else { std::ostringstream s; s << "unsupported encoding word=0x" << std::hex << w; error = s.str(); return false; }
    if (literal) i.length = 2;
    if (i.length == 2 && memory.read_data(pc + 4, &i.w1, 4)) { error = "instruction literal unreadable"; return false; }
    if (cache.size() < 65536) cache.emplace(pc, i);
    out = i;
    return true;
  }

  // Runs one wave from 'pc' until S_ENDPGM.
  template <class Memory>
  bool run(Memory& memory, uint64_t pc, Wave& wave, std::string& error) {
    ++stats.waves;
    auto& s = wave.s;
    Inst i;
    std::string why;
    auto fail = [&](const std::string& reason) {
      std::ostringstream m;
      m << "GCN pc=0x" << std::hex << pc << ' ' << encodingName(i.enc) << '.' << std::dec << i.op
        << " (" << opName(i.enc, i.op) << ") word=0x" << std::hex << i.w0;
      if (i.length == 2) m << ",0x" << i.w1;
      m << ": " << reason;
      error = m.str();
      return false;
    };
    bool ok = true;
    // ---- scalar operands -------------------------------------------------
    auto scalarIndexValid = [&](unsigned r) {
      return (r < wave.sgprs && r <= 103) || r == 106 || r == 107 || r == 124 || r == 126 || r == 127;
    };
    auto sread = [&](unsigned code) -> uint32_t {
      if (code <= 127) {
        if (!scalarIndexValid(code)) { ok = false; why = "scalar register " + std::to_string(code) + " outside the allocated/implemented set"; return 0; }
        return s[code];
      }
      if (code >= 128 && code <= 192) return code - 128;
      if (code >= 193 && code <= 208) return uint32_t(192 - int(code));
      static const uint32_t floats[] = {0x3f000000, 0xbf000000, 0x3f800000, 0xbf800000, 0x40000000, 0xc0000000, 0x40800000, 0xc0800000};
      if (code >= 240 && code <= 247) return floats[code - 240];
      if (code == 251) return wave.vcc() == 0;
      if (code == 252) return wave.exec() == 0;
      if (code == 253) return wave.scc;
      if (code == 255) return i.w1;
      ok = false; why = "scalar operand " + std::to_string(code) + " unsupported";
      return 0;
    };
    auto sread64 = [&](unsigned code) -> uint64_t {
      if (code <= 127) {
        if (code & 1) { ok = false; why = "misaligned 64-bit scalar register"; return 0; }
        return uint64_t(sread(code)) | (uint64_t(sread(code + 1)) << 32);
      }
      if (code >= 128 && code <= 208) return uint64_t(int64_t(int32_t(sread(code))));
      if (code == 255) return i.w1; // 32-bit literal, zero-extended
      ok = false; why = "64-bit scalar operand " + std::to_string(code) + " unsupported";
      return 0;
    };
    auto swrite = [&](unsigned code, uint32_t value) {
      if (!scalarIndexValid(code)) { ok = false; why = "scalar destination " + std::to_string(code) + " outside the allocated/implemented set"; return; }
      s[code] = value;
    };
    auto swrite64 = [&](unsigned code, uint64_t value) {
      if (code & 1) { ok = false; why = "misaligned 64-bit scalar destination"; return; }
      swrite(code, uint32_t(value)); swrite(code + 1, uint32_t(value >> 32));
    };
    // ---- vector operands -------------------------------------------------
    auto vsrc = [&](unsigned code, unsigned lane) -> uint32_t {
      if (code >= 256) {
        if (code - 256 >= wave.vgprs) { ok = false; why = "VGPR " + std::to_string(code - 256) + " beyond allocation"; return 0; }
        return wave.v[code - 256][lane];
      }
      return sread(code);
    };
    auto vdstOk = [&](unsigned r) {
      if (r >= wave.vgprs) { ok = false; why = "VGPR destination " + std::to_string(r) + " beyond allocation"; return false; }
      return true;
    };
    for (uint64_t steps = 0;; ++steps) {
      if (steps >= maxStepsPerWave) return fail("instruction budget exhausted (loop?)");
      if (!fetch(memory, pc, i, why)) return fail(why);
      ++stats.instructions;
      ++stats.histogram[(uint32_t(i.enc) << 16) | i.op];
      uint64_t next = pc + uint64_t(i.length) * 4;
      const uint32_t w = i.w0;
      const uint64_t exec = wave.exec();
      switch (i.enc) {
      case Enc::SOPP: {
        const int16_t simm = int16_t(w & 0xffff);
        const uint64_t target = next + int64_t(simm) * 4;
        switch (i.op) {
        case 0: case 12: case 15: break; // s_nop, s_waitcnt (synchronous), s_setprio
        case 1: return true;            // s_endpgm
        case 2: next = target; break;
        case 4: if (!wave.scc) next = target; break;
        case 5: if (wave.scc) next = target; break;
        case 6: if (!wave.vcc()) next = target; break;
        case 7: if (wave.vcc()) next = target; break;
        case 8: if (!exec) next = target; break;
        case 9: if (exec) next = target; break;
        default: return fail("SOPP opcode not implemented");
        }
        break;
      }
      case Enc::SOP1: {
        const unsigned dst = (w >> 16) & 127, src = w & 255;
        switch (i.op) {
        case 3: swrite(dst, sread(src)); break;
        case 4: swrite64(dst, sread64(src)); break;
        case 7: { const uint32_t r = ~sread(src); swrite(dst, r); wave.scc = r != 0; break; }
        case 8: { const uint64_t r = ~sread64(src); swrite64(dst, r); wave.scc = r != 0; break; }
        case 10: { // s_wqm_b64: enable every lane of a quad with any active lane
          const uint64_t a = sread64(src);
          uint64_t r = 0;
          for (unsigned q = 0; q < 16; ++q) if ((a >> (q * 4)) & 15) r |= uint64_t(15) << (q * 4);
          swrite64(dst, r); wave.scc = r != 0; break;
        }
        case 31: swrite64(dst, next); break;
        case 32: { const uint64_t t = sread64(src); if (!ok) return fail(why); next = t & addressMask; break; }
        case 33: { const uint64_t t = sread64(src); if (!ok) return fail(why); swrite64(dst, next); next = t & addressMask; break; }
        case 36: case 37: case 38: case 39: { // s_{and,or,xor,andn2}_saveexec_b64
          const uint64_t a = sread64(src);
          uint64_t r = i.op == 36 ? a & exec : i.op == 37 ? a | exec : i.op == 38 ? a ^ exec : a & ~exec;
          swrite64(dst, exec); wave.setExec(r); wave.scc = r != 0; break;
        }
        default: return fail("SOP1 opcode not implemented");
        }
        break;
      }
      case Enc::SOP2: {
        const unsigned dst = (w >> 16) & 127, a0 = w & 255, b0 = (w >> 8) & 255;
        const unsigned op = i.op;
        if (op == 11 || op == 15 || op == 17 || op == 19 || op == 21 || op == 23 || op == 25 || op == 27 || op == 29) {
          const uint64_t a = sread64(a0), b = sread64(b0);
          uint64_t r = 0;
          switch (op) {
          case 11: r = wave.scc ? a : b; break;
          case 15: r = a & b; break; case 17: r = a | b; break; case 19: r = a ^ b; break;
          case 21: r = a & ~b; break; case 23: r = a | ~b; break; case 25: r = ~(a & b); break;
          case 27: r = ~(a | b); break; case 29: r = ~(a ^ b); break;
          }
          if (op != 11) wave.scc = r != 0;
          swrite64(dst, r);
          break;
        }
        if (op == 31 || op == 33 || op == 35) {
          const uint64_t a = sread64(a0); const unsigned sh = sread(b0) & 63;
          const uint64_t r = op == 31 ? a << sh : op == 33 ? a >> sh : uint64_t(int64_t(a) >> sh);
          swrite64(dst, r); wave.scc = r != 0; break;
        }
        const uint32_t a = sread(a0), b = sread(b0);
        uint32_t r = 0;
        switch (op) {
        case 0: { const uint64_t t = uint64_t(a) + b; r = uint32_t(t); wave.scc = t >> 32; break; }
        case 1: r = a - b; wave.scc = b > a; break;
        case 2: { r = a + b; wave.scc = ((~(a ^ b)) & (a ^ r)) >> 31; break; }
        case 3: { r = a - b; wave.scc = ((a ^ b) & (a ^ r)) >> 31; break; }
        case 4: { const uint64_t t = uint64_t(a) + b + wave.scc; r = uint32_t(t); wave.scc = t >> 32; break; }
        case 5: { const uint64_t t = uint64_t(a) - b - wave.scc; r = uint32_t(t); wave.scc = (t >> 32) != 0; break; }
        case 6: wave.scc = int32_t(a) < int32_t(b); r = wave.scc ? a : b; break;
        case 7: wave.scc = a < b; r = wave.scc ? a : b; break;
        case 8: wave.scc = int32_t(a) > int32_t(b); r = wave.scc ? a : b; break;
        case 9: wave.scc = a > b; r = wave.scc ? a : b; break;
        case 10: r = wave.scc ? a : b; break;
        case 14: r = a & b; wave.scc = r != 0; break;
        case 16: r = a | b; wave.scc = r != 0; break;
        case 18: r = a ^ b; wave.scc = r != 0; break;
        case 20: r = a & ~b; wave.scc = r != 0; break;
        case 22: r = a | ~b; wave.scc = r != 0; break;
        case 30: r = a << (b & 31); wave.scc = r != 0; break;
        case 32: r = a >> (b & 31); wave.scc = r != 0; break;
        case 34: r = uint32_t(int32_t(a) >> (b & 31)); wave.scc = r != 0; break;
        case 36: r = ((1U << (a & 31)) - 1) << (b & 31); if ((a & 31) == 0) r = 0; break;
        case 38: r = uint32_t(int32_t(a) * int32_t(b)); break;
        case 39: case 40: { // s_bfe_u32 / s_bfe_i32: offset b[4:0], width b[22:16]
          const unsigned off = b & 31, width = (b >> 16) & 127;
          if (!width) r = 0;
          else if (width >= 32) r = a >> off;
          else {
            r = (a >> off) & ((1U << width) - 1);
            if (op == 40 && (r >> (width - 1)) & 1) r |= ~((1U << width) - 1);
          }
          wave.scc = r != 0; break;
        }
        default: return fail("SOP2 opcode not implemented");
        }
        swrite(dst, r);
        break;
      }
      case Enc::SOPK: {
        const unsigned dst = (w >> 16) & 127;
        const int32_t k = int16_t(w & 0xffff);
        const uint32_t uk = w & 0xffff;
        if (i.op == 0) { swrite(dst, uint32_t(k)); break; }
        const uint32_t d = sread(dst);
        switch (i.op) {
        case 3: wave.scc = int32_t(d) == k; break; case 4: wave.scc = int32_t(d) != k; break;
        case 5: wave.scc = int32_t(d) > k; break; case 6: wave.scc = int32_t(d) >= k; break;
        case 7: wave.scc = int32_t(d) < k; break; case 8: wave.scc = int32_t(d) <= k; break;
        case 9: wave.scc = d == uk; break; case 10: wave.scc = d != uk; break;
        case 11: wave.scc = d > uk; break; case 12: wave.scc = d >= uk; break;
        case 13: wave.scc = d < uk; break; case 14: wave.scc = d <= uk; break;
        case 15: { const uint32_t r = d + uint32_t(k); wave.scc = ((~(d ^ uint32_t(k))) & (d ^ r)) >> 31; swrite(dst, r); break; }
        case 16: swrite(dst, uint32_t(int32_t(d) * k)); break;
        default: return fail("SOPK opcode not implemented");
        }
        break;
      }
      case Enc::SOPC: {
        const uint32_t a = sread(w & 255), b = sread((w >> 8) & 255);
        const int32_t x = int32_t(a), y = int32_t(b);
        switch (i.op) {
        case 0: wave.scc = x == y; break; case 1: wave.scc = x != y; break; case 2: wave.scc = x > y; break;
        case 3: wave.scc = x >= y; break; case 4: wave.scc = x < y; break; case 5: wave.scc = x <= y; break;
        case 6: wave.scc = a == b; break; case 7: wave.scc = a != b; break; case 8: wave.scc = a > b; break;
        case 9: wave.scc = a >= b; break; case 10: wave.scc = a < b; break; case 11: wave.scc = a <= b; break;
        case 12: wave.scc = !((a >> (b & 31)) & 1); break; case 13: wave.scc = (a >> (b & 31)) & 1; break;
        default: return fail("SOPC opcode not implemented");
        }
        break;
      }
      case Enc::SMRD: {
        const unsigned dst = (w >> 15) & 127, base = ((w >> 9) & 63) * 2;
        const unsigned op = i.op;
        const unsigned count = op == 0 || op == 8 ? 1 : op == 1 || op == 9 ? 2 : op == 2 || op == 10 ? 4 : op == 3 || op == 11 ? 8 : op == 4 || op == 12 ? 16 : 0;
        if (!count || (op > 4 && op < 8) || op > 12) return fail("SMRD opcode not implemented");
        const uint64_t offset = (w & 256) ? uint64_t(w & 255) * 4 : uint64_t(sread(w & 255));
        if (!ok) return fail(why);
        uint64_t address = 0, limit = std::numeric_limits<uint64_t>::max();
        if (op >= 8) { // S_BUFFER_LOAD: V# in s[base:base+3]
          const uint32_t lo = sread(base), hi = sread(base + 1), records = sread(base + 2);
          if (!ok) return fail(why);
          if (hi & 0x80000000U) return fail("swizzled scalar buffer unsupported");
          const uint64_t stride = (hi >> 16) & 0x3fff;
          address = (lo | (uint64_t(hi & 0xffff) << 32)) & addressMask;
          limit = stride ? stride * records : records;
        } else {
          address = sread64(base) & addressMask;
          if (!ok) return fail(why);
        }
        for (unsigned k = 0; k < count; ++k) {
          const uint64_t at = offset + k * 4;
          uint32_t value = 0;
          if (at + 4 <= limit) {
            if (memory.read_data(address + at, &value, 4)) {
              std::ostringstream m; m << "scalar load unreadable at 0x" << std::hex << address + at; return fail(m.str());
            }
          } else ++stats.bufferOutOfRange; // hardware returns zero
          swrite(dst + k, value);
          ++stats.scalarLoads;
        }
        break;
      }
      case Enc::VOP1: case Enc::VOP2: case Enc::VOPC: case Enc::VOP3: {
        if (!vectorAlu(wave, i, exec, sread, sread64, swrite64, vsrc, vdstOk, ok, why)) return fail(why);
        break;
      }
      case Enc::VINTRP: {
        const unsigned vdst = (w >> 18) & 255, attr = (w >> 10) & 63, chan = (w >> 8) & 3, src = w & 255;
        if (!vdstOk(vdst) || !wave.interpolate) return fail(ok ? "no interpolation inputs for this stage" : why);
        for (unsigned lane = 0; lane < 64; ++lane) {
          if (!((exec >> lane) & 1)) continue;
          float p[3];
          if (!wave.interpolate(attr, chan, lane, p)) return fail("PS input " + std::to_string(attr) + " not provided by the bound vertex shader");
          if (i.op == 2) { // v_interp_mov_f32: P10, P20 or P0
            if (src > 2) return fail("v_interp_mov source");
            wave.v[vdst][lane] = gcnBits(src == 0 ? p[1] : src == 1 ? p[2] : p[0]);
            continue;
          }
          if (i.op == 3) return fail("VINTRP opcode not implemented");
          if (src >= wave.vgprs) return fail("interpolation VGPR beyond allocation");
          // p * coord + base, under the wave's fp32 denormal mode (as v_mad).
          const bool flushIn = wave.fp32Denorm == 0 || wave.fp32Denorm == 2, flushOut = wave.fp32Denorm == 0 || wave.fp32Denorm == 1;
          auto in = [&](float f) { return flushIn ? gcnFloat(flushDenormal(gcnBits(f))) : f; };
          auto out = [&](float f) { return flushOut ? gcnFloat(flushDenormal(gcnBits(f))) : f; };
          const float coord = in(gcnFloat(wave.v[src][lane]));
          if (i.op == 0) wave.v[vdst][lane] = gcnBits(out(out(in(p[1]) * coord) + in(p[0])));
          else wave.v[vdst][lane] = gcnBits(out(out(in(p[2]) * coord) + in(gcnFloat(wave.v[vdst][lane]))));
        }
        break;
      }
      case Enc::EXP: {
        Export e;
        e.enable = w & 15; e.target = (w >> 4) & 63; e.compressed = (w >> 10) & 1;
        e.done = (w >> 11) & 1; e.validMask = (w >> 12) & 1; e.exec = exec;
        for (unsigned c = 0; c < 4; ++c) {
          const unsigned r = (i.w1 >> (c * 8)) & 255;
          const bool used = e.compressed ? (c < 2 && ((e.enable >> (c * 2)) & 3)) : ((e.enable >> c) & 1);
          if (!used) continue;
          if (!vdstOk(r)) return fail(why);
          e.data[c] = wave.v[r];
        }
        wave.exports.push_back(e);
        ++stats.exports;
        break;
      }
      case Enc::MUBUF: case Enc::MTBUF: {
        if (!bufferAccess(memory, wave, i, exec, sread, vdstOk, ok, why)) return fail(why);
        break;
      }
      case Enc::MIMG: {
        if (!imageAccess(memory, wave, i, exec, sread, vdstOk, ok, why)) return fail(why);
        break;
      }
      }
      if (!ok) return fail(why);
      pc = next;
    }
  }

  // ---------------------------------------------------------------------
  // Vector ALU. All encodings are mapped to the VOP3 opcode space:
  // VOPC 0..255, VOP2 256+op, VOP3-only 320..383, VOP1 384+op.
  static bool isFloatInput(unsigned op) {
    if (op < 256) return op < 0x20; // F32 compares (CMP/CMPX)
    if (op >= 256 && op < 320) {
      const unsigned o = op - 256;
      return (o >= 3 && o <= 8) || (o >= 13 && o <= 16) || o == 31 || o == 32 || o == 33 || o == 43 || (o >= 45 && o <= 47);
    }
    if (op >= 384) {
      const unsigned o = op - 384;
      return o == 7 || o == 8 || o == 10 || o == 12 || o == 13 || (o >= 32 && o <= 46) || o == 51 || o == 53 || o == 54;
    }
    return op == 320 || op == 321 || op == 331 || op == 337 || op == 340 || op == 343;
  }
  static bool isFloatOutput(unsigned op) {
    if (op < 256) return false;
    if (op >= 384) { const unsigned o = op - 384; return o == 5 || o == 6 || o == 11 || (o >= 17 && o <= 20) || (o >= 32 && o <= 46) || o == 51 || o == 53 || o == 54; }
    if (op < 320) { const unsigned o = op - 256; return (o >= 3 && o <= 8) || (o >= 13 && o <= 16) || o == 31 || o == 32 || o == 33 || o == 43; }
    return op == 320 || op == 321 || op == 331 || op == 337 || op == 340 || op == 343;
  }
  // v_min/v_max_f32: a NaN operand, quiet or signalling, yields the other one
  // (IEEE_MODE=0, the graphics default; signalling-NaN quieting of
  // IEEE_MODE=1 is not modelled). Equal operands (+-0) return the first.
  static float minNum(float x, float y) { return std::isnan(x) ? y : std::isnan(y) ? x : (y < x ? y : x); }
  static float maxNum(float x, float y) { return std::isnan(x) ? y : std::isnan(y) ? x : (x < y ? y : x); }
  static uint32_t flushDenormal(uint32_t v) { return (v & 0x7f800000U) ? v : (v & 0x80000000U); }
  // compute() under an fp32 denormal mode (Wave::fp32Denorm): float inputs
  // (all but ldexp's exponent) and float results are flushed to signed zero
  // as the mode says; with output flushing, the rounded product of the
  // multiply-add forms is flushed too (GCN's v_mad/v_mac never keep
  // denormals). Mode 3 is compute() itself.
  static bool computeMode(unsigned op, uint32_t a, uint32_t b, uint32_t c, uint32_t& r, bool& carry, unsigned mode) {
    const bool in = mode == 0 || mode == 2, out = mode == 0 || mode == 1;
    if (in && isFloatInput(op)) { a = flushDenormal(a); if (op != 299) b = flushDenormal(b); c = flushDenormal(c); }
    if (!compute(op, a, b, c, r, carry, out)) return false;
    if (out && isFloatOutput(op)) r = flushDenormal(r);
    return true;
  }
  static bool compareMode(unsigned op, uint32_t a, uint32_t b, bool& result, unsigned mode) {
    if ((mode == 0 || mode == 2) && (op & 0xe0) == 0) { a = flushDenormal(a); b = flushDenormal(b); }
    return compare(op, a, b, result);
  }
  // VOP3 output modifiers of a float result (omod, then clamp; NaN clamps to 0).
  static uint32_t outputModifiers(uint32_t v, unsigned omod, bool clamp, unsigned mode) {
    float f = gcnFloat(v);
    if (omod == 1) f *= 2; else if (omod == 2) f *= 4; else if (omod == 3) f *= 0.5f;
    if (clamp) f = std::min(1.0f, std::max(0.0f, f));
    const uint32_t r = gcnBits(f);
    return (mode == 0 || mode == 1) ? flushDenormal(r) : r;
  }
  // Returns false for an unimplemented opcode. carry: in/out lane bit.
  // flushProduct: the multiply-add forms flush their rounded product.
  static bool compute(unsigned op, uint32_t a, uint32_t b, uint32_t c, uint32_t& r, bool& carry, bool flushProduct = false) {
    const float fa = gcnFloat(a), fb = gcnFloat(b), fc = gcnFloat(c);
    auto F = [&](float v) { r = gcnBits(v); return true; };
    auto legacyMul = [](float x, float y) { return (x == 0 || y == 0) ? 0.0f : x * y; };
    auto P = [&](float p) { return flushProduct ? gcnFloat(flushDenormal(gcnBits(p))) : p; };
    switch (op) {
    case 256: r = carry ? b : a; return true;                  // v_cndmask_b32
    case 259: return F(fa + fb);
    case 260: return F(fa - fb);
    case 261: return F(fb - fa);
    case 262: return F(P(legacyMul(fa, fb)) + fc);               // v_mac_legacy_f32 (c = dst)
    case 263: return F(legacyMul(fa, fb));
    case 264: return F(fa * fb);
    case 265: r = uint32_t(int32_t(int64_t(int32_t(a << 8) >> 8) * int64_t(int32_t(b << 8) >> 8))); return true;
    case 267: r = (a & 0xffffff) * (b & 0xffffff); return true;
    case 269: return F(fa < fb ? fa : fb);                    // min_legacy
    case 270: return F(fa > fb ? fa : fb);                    // max_legacy
    case 271: return F(minNum(fa, fb));
    case 272: return F(maxNum(fa, fb));
    case 273: r = uint32_t(std::min(int32_t(a), int32_t(b))); return true;
    case 274: r = uint32_t(std::max(int32_t(a), int32_t(b))); return true;
    case 275: r = std::min(a, b); return true;
    case 276: r = std::max(a, b); return true;
    case 277: r = a >> (b & 31); return true;
    case 278: r = b >> (a & 31); return true;
    case 279: r = uint32_t(int32_t(a) >> (b & 31)); return true;
    case 280: r = uint32_t(int32_t(b) >> (a & 31)); return true;
    case 281: r = a << (b & 31); return true;
    case 282: r = b << (a & 31); return true;
    case 283: r = a & b; return true;
    case 284: r = a | b; return true;
    case 285: r = a ^ b; return true;
    case 286: r = (a & 31) ? ((1U << (a & 31)) - 1) << (b & 31) : 0; return true;
    case 287: return F(P(fa * fb) + fc);                         // v_mac_f32 (c = dst)
    case 288: return F(P(fa * fc) + fb);                         // v_madmk_f32 (c = K)
    case 289: return F(P(fa * fb) + fc);                         // v_madak_f32 (c = K)
    case 290: r = uint32_t(__builtin_popcount(a)) + b; return true;
    case 293: { const uint64_t t = uint64_t(a) + b; r = uint32_t(t); carry = t >> 32; return true; }
    case 294: r = a - b; carry = b > a; return true;
    case 295: r = b - a; carry = a > b; return true;
    case 296: { const uint64_t t = uint64_t(a) + b + carry; r = uint32_t(t); carry = t >> 32; return true; }
    case 297: { const uint64_t t = uint64_t(a) - b - carry; r = uint32_t(t); carry = (t >> 32) != 0; return true; }
    case 298: { const uint64_t t = uint64_t(b) - a - carry; r = uint32_t(t); carry = (t >> 32) != 0; return true; }
    case 299: return F(std::ldexp(fa, int32_t(b)));
    case 303: r = uint32_t(gcnHalf(fa, true)) | (uint32_t(gcnHalf(fb, true)) << 16); return true;
    case 304: r = (a & 0xffff) | (b << 16); return true;
    case 320: return F(P(legacyMul(fa, fb)) + fc);
    case 321: return F(P(fa * fb) + fc);
    case 322: r = uint32_t(int32_t(int64_t(int32_t(a << 8) >> 8) * int64_t(int32_t(b << 8) >> 8)) + int32_t(c)); return true;
    case 323: r = (a & 0xffffff) * (b & 0xffffff) + c; return true;
    case 328: { const unsigned off = b & 31, width = c & 31; r = width ? (a >> off) & ((1U << width) - 1) : 0; return true; }
    case 329: { const unsigned off = b & 31, width = c & 31; if (!width) { r = 0; return true; }
      r = uint32_t(int32_t((a >> off) << (32 - width)) >> (32 - width)); return true; }
    case 330: r = (a & b) | (~a & c); return true;
    case 331: return F(std::fma(fa, fb, fc));
    case 334: r = uint32_t(((uint64_t(a) << 32) | b) >> (c & 31)); return true;
    case 335: r = uint32_t(((uint64_t(a) << 32) | b) >> ((c & 3) * 8)); return true;
    case 337: return F(minNum(minNum(fa, fb), fc));
    case 338: r = uint32_t(std::min({int32_t(a), int32_t(b), int32_t(c)})); return true;
    case 339: r = std::min({a, b, c}); return true;
    case 340: return F(maxNum(maxNum(fa, fb), fc));
    case 341: r = uint32_t(std::max({int32_t(a), int32_t(b), int32_t(c)})); return true;
    case 342: r = std::max({a, b, c}); return true;
    case 343: { float m = maxNum(minNum(fa, fb), minNum(maxNum(fa, fb), fc)); return F(m); }
    case 344: { int32_t x = int32_t(a), y = int32_t(b), z = int32_t(c); r = uint32_t(std::max(std::min(x, y), std::min(std::max(x, y), z))); return true; }
    case 345: r = std::max(std::min(a, b), std::min(std::max(a, b), c)); return true;
    case 361: r = a * b; return true;                                   // v_mul_lo_u32
    case 362: r = uint32_t((uint64_t(a) * b) >> 32); return true;       // v_mul_hi_u32
    case 363: r = uint32_t(int32_t(a) * int32_t(b)); return true;       // v_mul_lo_i32
    case 364: r = uint32_t((int64_t(int32_t(a)) * int32_t(b)) >> 32); return true;
    case 385: r = a; return true;                                        // v_mov_b32
    case 389: return F(float(int32_t(a)));
    case 390: return F(float(a));
    case 391: { if (std::isnan(fa) || fa <= 0) r = 0; else if (fa >= 4294967295.0f) r = 0xffffffffU; else r = uint32_t(fa); return true; }
    case 392: { if (std::isnan(fa)) r = 0; else if (fa >= 2147483647.0f) r = 0x7fffffff; else if (fa <= -2147483648.0f) r = 0x80000000U; else r = uint32_t(int32_t(fa)); return true; }
    case 394: r = gcnHalf(fa, false); return true;                      // v_cvt_f16_f32
    case 395: return F(gcnHalfToFloat(uint16_t(a)));                     // v_cvt_f32_f16
    case 396: r = uint32_t(int32_t(std::floor(fa + 0.5f))); return true;
    case 397: r = uint32_t(int32_t(std::floor(fa))); return true;
    case 398: r = gcnBits(float(int32_t(a << 28) >> 28) * 0.0625f); return true;   // v_cvt_off_f32_i4: signed 4 bits / 16
    case 401: return F(float(a & 255));
    case 402: return F(float((a >> 8) & 255));
    case 403: return F(float((a >> 16) & 255));
    case 404: return F(float(a >> 24));
    case 416: return F(fa - std::floor(fa));
    case 417: return F(std::trunc(fa));
    case 418: return F(std::ceil(fa));
    case 419: return F(std::nearbyint(fa));
    case 420: return F(std::floor(fa));
    case 421: return F(std::exp2(fa));
    case 423: return F(std::log2(fa));
    case 426: case 427: return F(1.0f / fa);
    case 430: return F(1.0f / std::sqrt(fa));
    case 435: return F(std::sqrt(fa));
    case 437: return F(std::sin(fa * 6.28318530717958647692f));
    case 438: return F(std::cos(fa * 6.28318530717958647692f));
    case 439: r = ~a; return true;
    case 440: { uint32_t x = a, y = 0; for (int k = 0; k < 32; ++k) { y = (y << 1) | (x & 1); x >>= 1; } r = y; return true; }
    case 441: r = a ? uint32_t(__builtin_clz(a)) : 0xffffffffU; return true;
    case 442: r = a ? uint32_t(__builtin_ctz(a)) : 0xffffffffU; return true;
    default: return false;
    }
  }
  static bool compare(unsigned op, uint32_t a, uint32_t b, bool& result) {
    const unsigned kind = op & 0x0f, family = op & 0xf0;
    if (family == 0x00 || family == 0x10) {
      const float x = gcnFloat(a), y = gcnFloat(b);
      const bool unordered = std::isnan(x) || std::isnan(y);
      switch (kind) {
      case 0: result = false; break; case 1: result = x < y; break; case 2: result = x == y; break;
      case 3: result = x <= y; break; case 4: result = x > y; break; case 5: result = x < y || x > y; break;
      case 6: result = x >= y; break; case 7: result = !unordered; break; case 8: result = unordered; break;
      case 9: result = !(x >= y); break; case 10: result = !(x < y || x > y); break; case 11: result = !(x > y); break;
      case 12: result = !(x <= y); break; case 13: result = !(x == y); break; case 14: result = !(x < y); break;
      default: result = true; break;
      }
      return true;
    }
    if (kind > 7) return false;
    if (family == 0x80 || family == 0x90) {
      const int32_t x = int32_t(a), y = int32_t(b);
      const bool v[] = {false, x < y, x == y, x <= y, x > y, x != y, x >= y, true};
      result = v[kind]; return true;
    }
    if (family == 0xc0 || family == 0xd0) {
      const bool v[] = {false, a < b, a == b, a <= b, a > b, a != b, a >= b, true};
      result = v[kind]; return true;
    }
    return false;
  }
  template <class SRead, class SRead64, class SWrite64, class VSrc, class VDst>
  bool vectorAlu(Wave& wave, const Inst& i, uint64_t exec, SRead& sread, SRead64& sread64, SWrite64& swrite64,
                 VSrc& vsrc, VDst& vdstOk, bool& ok, std::string& why) {
    const uint32_t w = i.w0;
    unsigned op = 0, src0 = 0, src1 = 0, src2 = 0, vdst = 0, sdst = 106;
    unsigned absMask = 0, negMask = 0, omod = 0;
    bool clamp = false, hasSrc2 = false, carryOutToSgpr = false, carryInFromSgpr = false;
    uint32_t literalK = 0;
    switch (i.enc) {
    case Enc::VOP1: op = 384 + i.op; src0 = w & 511; vdst = (w >> 17) & 255; break;
    case Enc::VOP2:
      op = 256 + i.op; src0 = w & 511; src1 = 256 + ((w >> 9) & 255); vdst = (w >> 17) & 255;
      if (i.op == 32 || i.op == 33) literalK = i.w1;
      break;
    case Enc::VOPC: op = i.op; src0 = w & 511; src1 = 256 + ((w >> 9) & 255); break;
    default: // VOP3
      op = i.op; vdst = w & 255; src0 = i.w1 & 511; src1 = (i.w1 >> 9) & 511; src2 = (i.w1 >> 18) & 511;
      hasSrc2 = true;
      if ((op >= 293 && op <= 298)) { // VOP3b: carry-out SGPR pair in [14:8]
        sdst = (w >> 8) & 127; carryOutToSgpr = true; carryInFromSgpr = op >= 296;
        if (i.w1 >> 27) { why = "VOP3b output modifiers/negation unsupported"; return false; }
      } else {
        absMask = (w >> 8) & 7; clamp = (w >> 11) & 1; omod = (i.w1 >> 27) & 3; negMask = i.w1 >> 29;
        if (op < 256) sdst = vdst;
      }
      break;
    }
    if ((absMask || negMask) && !isFloatInput(op)) { why = "input modifiers on non-float operation"; return false; }
    if ((clamp || omod) && !isFloatOutput(op)) {
      if (!(clamp && op < 256)) { why = "output modifiers on non-float result"; return false; }
    }
    // Unsupported opcodes are rejected before any lane is modified.
    {
      uint32_t r; bool c = false, cmp = false;
      if (op < 256 ? !compare(op, 0, 0, cmp) : (op == 257 || op == 258 || op == 291 || op == 292 || op == 386 || !compute(op, 0, 0, 0, r, c))) {
        if (!(op == 257 || op == 386 || op == 291 || op == 292)) { why = "vector ALU opcode " + std::to_string(op) + " not implemented"; return false; }
      }
    }
    auto mod = [&](unsigned index, uint32_t v) {
      if ((absMask >> index) & 1) v &= 0x7fffffffU;
      if ((negMask >> index) & 1) v ^= 0x80000000U;
      return v;
    };
    auto finish = [&](uint32_t v) {
      if (!isFloatOutput(op) || (!omod && !clamp)) return v;
      return outputModifiers(v, omod, clamp, wave.fp32Denorm);
    };
    // Lane-wide special forms.
    if (op == 386) { // v_readfirstlane_b32: vdst is an SGPR
      unsigned lane = 0; while (lane < 64 && !((exec >> lane) & 1)) ++lane;
      const uint32_t v = vsrc(src0, lane < 64 ? lane : 0);
      if (!ok) return false;
      if (vdst > 103 || vdst >= wave.sgprs) { why = "readfirstlane destination"; return false; }
      wave.s[vdst] = v; return true;
    }
    if (op == 257) { // v_readlane_b32 sdst, vsrc0, ssrc1
      const unsigned lane = sread(i.enc == Enc::VOP2 ? ((w >> 9) & 255) : src1) & 63;
      const uint32_t v = vsrc(src0, lane);
      if (!ok) return false;
      if (vdst > 103 || vdst >= wave.sgprs) { why = "readlane destination"; return false; }
      wave.s[vdst] = v; return true;
    }
    if (op == 291 || op == 292) { // v_mbcnt_lo/hi_u32_b32
      if (!vdstOk(vdst)) return false;
      for (unsigned lane = 0; lane < 64; ++lane) if ((exec >> lane) & 1) {
        const uint32_t mask = vsrc(src0, lane), add = vsrc(src1, lane);
        const uint64_t below = (uint64_t(1) << lane) - 1;
        const uint32_t part = op == 291 ? uint32_t(below) : uint32_t(below >> 32);
        wave.v[vdst][lane] = uint32_t(__builtin_popcount(mask & part)) + add;
      }
      return ok;
    }
    if (op < 256) { // compares
      uint64_t mask = 0;
      for (unsigned lane = 0; lane < 64; ++lane) if ((exec >> lane) & 1) {
        bool r = false;
        compareMode(op, mod(0, vsrc(src0, lane)), mod(1, vsrc(src1, lane)), r, wave.fp32Denorm);
        if (r) mask |= uint64_t(1) << lane;
      }
      if (!ok) return false;
      const bool writesExec = (op & 0x10) != 0;
      if (writesExec) wave.setExec(mask);
      swrite64(sdst, mask);
      return ok;
    }
    if (!vdstOk(vdst)) return false;
    const bool usesCarryIn = op == 296 || op == 297 || op == 298 || op == 256;
    const bool writesCarry = op >= 293 && op <= 298;
    uint64_t carryIn = 0, carryOut = 0;
    if (usesCarryIn) carryIn = (carryInFromSgpr || (op == 256 && i.enc == Enc::VOP3)) ? sread64(src2) : wave.vcc();
    if (!ok) return false;
    for (unsigned lane = 0; lane < 64; ++lane) {
      if (!((exec >> lane) & 1)) continue;
      const uint32_t a = mod(0, vsrc(src0, lane));
      const uint32_t b = (op >= 384) ? 0 : mod(1, vsrc(src1, lane));
      uint32_t c = 0;
      if (op == 287 || op == 262) c = wave.v[vdst][lane];             // mac: accumulate destination
      else if (op == 288 || op == 289) c = literalK;
      else if (hasSrc2 && op >= 320 && op < 384) c = mod(2, vsrc(src2, lane));
      if (!ok) return false;
      bool carry = (carryIn >> lane) & 1;
      uint32_t r = 0;
      computeMode(op, a, b, c, r, carry, wave.fp32Denorm);
      wave.v[vdst][lane] = finish(r);
      if (carry) carryOut |= uint64_t(1) << lane;
    }
    if (writesCarry) {
      if (carryOutToSgpr) swrite64(sdst, carryOut); else wave.setVcc(carryOut);
    }
    return ok;
  }

  // ---------------------------------------------------------------------
  // Buffer resources (V#) and typed element conversion.
  static bool formatInfo(unsigned dfmt, unsigned& components, unsigned& bits) {
    switch (dfmt) {
    case 1: components = 1; bits = 8; return true;
    case 2: components = 1; bits = 16; return true;
    case 3: components = 2; bits = 8; return true;
    case 4: components = 1; bits = 32; return true;
    case 5: components = 2; bits = 16; return true;
    case 10: components = 4; bits = 8; return true;
    case 11: components = 2; bits = 32; return true;
    case 12: components = 4; bits = 16; return true;
    case 13: components = 3; bits = 32; return true;
    case 14: components = 4; bits = 32; return true;
    default: return false;
    }
  }
  static bool convertComponent(uint32_t raw, unsigned bits, unsigned nfmt, uint32_t& out) {
    const uint32_t maxU = bits == 32 ? 0xffffffffU : (1U << bits) - 1;
    const int32_t sv = bits == 32 ? int32_t(raw) : int32_t(raw << (32 - bits)) >> (32 - bits);
    switch (nfmt) {
    case 0: if (bits == 32) return false; out = gcnBits(float(raw) / float(maxU)); return true;
    case 1: if (bits == 32) return false; out = gcnBits(std::max(-1.0f, float(sv) / float(maxU >> 1))); return true;
    case 2: out = gcnBits(float(raw)); return true;
    case 3: out = gcnBits(float(sv)); return true;
    case 4: out = raw; return true;
    case 5: out = uint32_t(sv); return true;
    case 7: if (bits == 32) { out = raw; return true; } if (bits == 16) { out = gcnBits(gcnHalfToFloat(uint16_t(raw))); return true; } return false;
    default: return false;
    }
  }
  template <class Memory, class SRead, class VDst>
  bool bufferAccess(Memory& memory, Wave& wave, const Inst& i, uint64_t exec, SRead& sread, VDst& vdstOk, bool& ok, std::string& why) {
    const uint32_t w = i.w0, x = i.w1;
    const bool typed = i.enc == Enc::MTBUF;
    const unsigned op = i.op, offset = w & 0xfff;
    const bool offen = (w >> 12) & 1, idxen = (w >> 13) & 1, addr64 = (w >> 15) & 1;
    if (addr64 || (!typed && ((w >> 16) & 1))) { why = "MUBUF addr64/LDS forms unsupported"; return false; }
    if ((x >> 23) & 1) { why = "buffer TFE unsupported"; return false; }
    const unsigned vaddr = x & 255, vdata = (x >> 8) & 255, rsrc = ((x >> 16) & 31) * 4;
    const uint32_t soffset = sread(x >> 24);
    const uint32_t d0 = sread(rsrc), d1 = sread(rsrc + 1), d2 = sread(rsrc + 2), d3 = sread(rsrc + 3);
    if (!ok) return false;
    if (d1 & 0x80000000U) { why = "swizzled buffer resource unsupported"; return false; }
    if (d3 & (1U << 23)) { why = "buffer ADD_TID_ENABLE unsupported"; return false; }
    const uint64_t base = (d0 | (uint64_t(d1 & 0xffff) << 32)) & addressMask, stride = (d1 >> 16) & 0x3fff;
    unsigned count = 0, dfmt = 0, nfmt = 0, rawBytes = 0;
    bool formatted = false;
    if (typed) {
      if (op > 3) { why = "typed buffer store unsupported"; return false; }
      count = op + 1; formatted = true; dfmt = (w >> 19) & 15; nfmt = (w >> 23) & 7;
    } else if (op <= 3) {
      count = op + 1; formatted = true; dfmt = (d3 >> 15) & 15; nfmt = (d3 >> 12) & 7;
    } else if (op == 12 || op == 13 || op == 14 || op == 15) {
      count = op == 12 ? 1 : op == 13 ? 2 : op == 14 ? 4 : 3; rawBytes = 4;
    } else { why = "buffer opcode (store/atomic/sub-dword) not implemented"; return false; }
    unsigned components = 0, bits = 0;
    if (formatted && !formatInfo(dfmt, components, bits)) { why = "buffer data format " + std::to_string(dfmt) + " unsupported"; return false; }
    if (vdata + count > wave.vgprs) { why = "buffer destination VGPRs beyond allocation"; return false; }
    for (unsigned lane = 0; lane < 64; ++lane) {
      if (!((exec >> lane) & 1)) continue;
      uint64_t index = 0, voffset = 0;
      unsigned reg = vaddr;
      if (idxen) { if (reg >= wave.vgprs) { why = "buffer address VGPR"; return false; } index = wave.v[reg++][lane]; }
      if (offen) { if (reg >= wave.vgprs) { why = "buffer address VGPR"; return false; } voffset = wave.v[reg][lane]; }
      const uint64_t inside = voffset + offset;
      const bool outOfRange = stride ? index >= d2 : (inside + (formatted ? components * bits / 8 : count * 4)) > d2;
      std::array<uint32_t, 4> value{0, 0, 0, 0};
      if (formatted) {
        // Missing components read as 0 (x,y,z) and 1 (w) in the format's type.
        const bool integer = nfmt == 4 || nfmt == 5;
        value[3] = integer ? 1 : 0x3f800000U;
        if (!outOfRange) {
          const uint64_t at = base + uint64_t(soffset) + index * stride + inside;
          std::array<uint8_t, 16> bytes{};
          const unsigned size = components * bits / 8;
          if (memory.read_data(at, bytes.data(), size)) {
            std::ostringstream m; m << "buffer element unreadable at 0x" << std::hex << at; why = m.str(); return false;
          }
          for (unsigned c = 0; c < components; ++c) {
            uint32_t raw = 0; std::memcpy(&raw, bytes.data() + c * bits / 8, bits / 8);
            if (!convertComponent(raw, bits, nfmt, value[c])) { why = "buffer number format " + std::to_string(nfmt) + " unsupported for this size"; return false; }
          }
          // Destination swizzle from the V# (4..7 = X..W, 0 = zero, 1 = one).
          std::array<uint32_t, 4> swizzled{};
          for (unsigned c = 0; c < 4; ++c) {
            const unsigned sel = (d3 >> (c * 3)) & 7;
            swizzled[c] = sel >= 4 ? value[sel - 4] : sel == 1 ? (integer ? 1 : 0x3f800000U) : 0;
          }
          value = swizzled;
          ++stats.bufferLoads;
        } else { value = {0, 0, 0, 0}; ++stats.bufferOutOfRange; }
      } else if (!outOfRange) {
        const uint64_t at = base + uint64_t(soffset) + index * stride + inside;
        if (memory.read_data(at, value.data(), count * rawBytes)) {
          std::ostringstream m; m << "buffer dword unreadable at 0x" << std::hex << at; why = m.str(); return false;
        }
        ++stats.bufferLoads;
      } else ++stats.bufferOutOfRange;
      for (unsigned c = 0; c < count; ++c) wave.v[vdata + c][lane] = value[c];
    }
    (void)vdstOk;
    return true;
  }

  // ---------------------------------------------------------------------
  // Image resources (T#/S#): 2D, linear-aligned or display 2D-thin tiled.
  struct Texture {
    uint64_t base = 0; unsigned width = 0, height = 0, pitch = 0, dfmt = 0, nfmt = 0, tile = 0;
    std::array<unsigned, 4> select{};
    const uint8_t* host = nullptr; // whole resource, when it lies in one readable range
    Display2DThin32 layout;        // tile 10
  };
  static bool decodeTexture(const uint32_t* t, Texture& tex, std::string& why) {
    const unsigned type = t[3] >> 28;
    if (type != 9) { why = "image resource type " + std::to_string(type) + " unsupported (2D only)"; return false; }
    tex.base = ((uint64_t(t[0]) | (uint64_t(t[1] & 255) << 32)) << 8) & addressMask;
    tex.dfmt = (t[1] >> 20) & 63; tex.nfmt = (t[1] >> 26) & 15;
    tex.width = (t[2] & 0x3fff) + 1; tex.height = ((t[2] >> 14) & 0x3fff) + 1;
    tex.pitch = ((t[4] >> 13) & 0x3fff) + 1; tex.tile = (t[3] >> 20) & 31;
    for (unsigned c = 0; c < 4; ++c) tex.select[c] = (t[3] >> (c * 3)) & 7;
    if (((t[3] >> 12) & 15) != 0 || ((t[3] >> 16) & 15) != 0) { why = "mipmapped image resource unsupported"; return false; }
    if (t[5] & 0x1fff) { why = "array image resource unsupported"; return false; }
    if (tex.dfmt != 10 && tex.dfmt != 4 && tex.dfmt != 1 && tex.dfmt != 14 && tex.dfmt != 16) { why = "image data format " + std::to_string(tex.dfmt) + " unsupported"; return false; }
    if (tex.dfmt == 16 && tex.nfmt != 0) { why = "image 5_6_5 number format " + std::to_string(tex.nfmt) + " unsupported (UNORM only)"; return false; }
    if (!(tex.nfmt == 0 || tex.nfmt == 9 || (tex.nfmt == 7 && (tex.dfmt == 4 || tex.dfmt == 14)) || tex.nfmt == 4)) { why = "image number format " + std::to_string(tex.nfmt) + " unsupported"; return false; }
    if (tex.tile != 8 && tex.tile != 10) { why = "image tiling index " + std::to_string(tex.tile) + " unsupported"; return false; }
    if (tex.pitch < tex.width) { why = "image pitch smaller than width"; return false; }
    return true;
  }
  static unsigned texelBytes(unsigned dfmt) { return dfmt == 1 ? 1 : dfmt == 16 ? 2 : dfmt == 14 ? 16 : 4; }
  template <class Memory>
  bool texel(Memory& memory, const Texture& tex, unsigned x, unsigned y, std::array<float, 4>& out, std::string& why) {
    const unsigned bpp = texelBytes(tex.dfmt);
    const uint64_t offset = tex.tile == 8 ? (uint64_t(y) * tex.pitch + x) * bpp : tex.layout.offset(x, y);
    std::array<uint8_t, 16> raw{};
    if (tex.host) std::memcpy(raw.data(), tex.host + offset, bpp);
    else if (memory.read_data(tex.base + offset, raw.data(), bpp)) {
      std::ostringstream m; m << "texel unreadable at 0x" << std::hex << tex.base + offset; why = m.str(); return false;
    }
    ++stats.texels;
    if (std::any_of(raw.begin(), raw.begin() + bpp, [](uint8_t b) { return b != 0; })) ++stats.texelsNonZero;
    stats.textureBase = tex.base; stats.textureWidth = tex.width; stats.textureHeight = tex.height;
    stats.texturePitch = tex.pitch; stats.textureTile = tex.tile; stats.textureDfmt = tex.dfmt; stats.textureNfmt = tex.nfmt;
    std::array<float, 4> c{0, 0, 0, 1};
    if (tex.dfmt == 10) {
      for (unsigned k = 0; k < 4; ++k) {
        if (tex.nfmt == 4) c[k] = gcnFloat(raw[k]);
        else c[k] = tex.nfmt == 9 && k < 3 ? srgbByteToLinear(raw[k]) : raw[k] / 255.0f;
      }
    } else if (tex.dfmt == 1) c[0] = tex.nfmt == 4 ? gcnFloat(raw[0]) : raw[0] / 255.0f;
    else if (tex.dfmt == 16) { // 5_6_5 UNORM: x in bits 4:0, y in 10:5, z in 15:11
      const unsigned v = raw[0] | (unsigned(raw[1]) << 8);
      c[0] = float(v & 31) / 31.0f; c[1] = float((v >> 5) & 63) / 63.0f; c[2] = float(v >> 11) / 31.0f;
    }
    else if (tex.dfmt == 4) { uint32_t v; std::memcpy(&v, raw.data(), 4); c[0] = tex.nfmt == 7 ? gcnFloat(v) : tex.nfmt == 4 ? gcnFloat(v) : float(v) / 4294967295.0f; }
    else { for (unsigned k = 0; k < 4; ++k) { uint32_t v; std::memcpy(&v, raw.data() + 4 * k, 4); c[k] = gcnFloat(v); } }
    for (unsigned k = 0; k < 4; ++k) {
      const unsigned sel = tex.select[k];
      out[k] = sel >= 4 ? c[sel - 4] : sel == 1 ? 1.0f : 0.0f;
    }
    return true;
  }
  static int wrapCoordinate(int v, int size, unsigned mode, bool& border) {
    border = false;
    switch (mode) {
    case 0: { int m = v % size; return m < 0 ? m + size : m; }
    case 1: { const int period = 2 * size; int m = v % period; if (m < 0) m += period; return m < size ? m : period - 1 - m; }
    case 2: return std::min(std::max(v, 0), size - 1);
    case 3: { int m = v < 0 ? -v - 1 : v; return std::min(m, size - 1); }
    case 6: if (v < 0 || v >= size) border = true; return std::min(std::max(v, 0), size - 1);
    default: return -1;
    }
  }
  template <class Memory, class SRead, class VDst>
  bool imageAccess(Memory& memory, Wave& wave, const Inst& i, uint64_t exec, SRead& sread, VDst& vdstOk, bool& ok, std::string& why) {
    const uint32_t w = i.w0, x = i.w1;
    const unsigned op = i.op, dmask = (w >> 8) & 15;
    const bool unorm = (w >> 12) & 1;
    if ((w >> 14) & 0xf) { why = "MIMG DA/R128/TFE/LWE unsupported"; return false; }
    const unsigned vaddr = x & 255, vdata = (x >> 8) & 255, rsrc = ((x >> 16) & 31) * 4, ssamp = ((x >> 21) & 31) * 4;
    if (op != 0 && op != 32 && op != 36 && op != 39) { why = "image opcode not implemented"; return false; }
    uint32_t t[8], smp[4] = {0, 0, 0, 0};
    for (unsigned k = 0; k < 8; ++k) t[k] = sread(rsrc + k);
    if (op != 0) for (unsigned k = 0; k < 4; ++k) smp[k] = sread(ssamp + k);
    if (!ok) return false;
    Texture tex;
    if (!decodeTexture(t, tex, why)) return false;
    {
      uint64_t bytes = uint64_t(tex.pitch) * tex.height * texelBytes(tex.dfmt);
      if (tex.tile == 10) {
        if (texelBytes(tex.dfmt) != 4) { why = "tiled image requires 32-bit texels"; return false; }
        if (!tex.layout.init(tex.pitch, tex.height, why)) return false;
        bytes = tex.layout.sizeBytes();
      }
      tex.host = hostSpan(memory, tex.base, bytes, 4);
    }
    const unsigned clampX = smp[0] & 7, clampY = (smp[0] >> 3) & 7;
    const unsigned mag = (smp[2] >> 20) & 3, min = (smp[2] >> 22) & 3, mip = (smp[2] >> 26) & 3;
    const bool forceUnnorm = (smp[0] >> 15) & 1;
    if ((smp[0] >> 12) & 7) { why = "depth-compare sampler unsupported"; return false; }
    if (mag != min || mag > 1) { why = "sampler filter combination unsupported (point/bilinear only)"; return false; }
    (void)mip; // single-level resources only (validated in decodeTexture)
    const unsigned outputs = unsigned(__builtin_popcount(dmask));
    if (vdata + outputs > wave.vgprs || vaddr + 2 > wave.vgprs) { why = "image VGPRs beyond allocation"; return false; }
    for (unsigned lane = 0; lane < 64; ++lane) {
      if (!((exec >> lane) & 1)) continue;
      std::array<float, 4> color{0, 0, 0, 0};
      if (op == 0) {
        const uint32_t px = wave.v[vaddr][lane], py = wave.v[vaddr + 1][lane];
        if (px < tex.width && py < tex.height) { if (!texel(memory, tex, px, py, color, why)) return false; }
      } else {
        // Coordinate and filter arithmetic flushes fp32 denormals to signed zero
        // at every step (GCN's default single-precision mode; Apple GPUs do the
        // same), so GCNKernelImage.h on the GPU agrees bit for bit.
        auto ftz = [](float f) { const uint32_t b = gcnBits(f); return (b & 0x7f800000u) ? f : gcnFloat(b & 0x80000000u); };
        float u = ftz(gcnFloat(wave.v[vaddr][lane])), v = ftz(gcnFloat(wave.v[vaddr + 1][lane]));
        if (!(unorm || forceUnnorm)) { u = ftz(u * float(tex.width)); v = ftz(v * float(tex.height)); }
        if (!std::isfinite(u) || !std::isfinite(v)) { u = 0; v = 0; }
        u = std::min(std::max(u, -1.0e8f), 1.0e8f); v = std::min(std::max(v, -1.0e8f), 1.0e8f);
        if (mag == 0) {
          bool bx, by;
          const int cx = wrapCoordinate(int(std::floor(u)), int(tex.width), clampX, bx);
          const int cy = wrapCoordinate(int(std::floor(v)), int(tex.height), clampY, by);
          if (cx < 0 || cy < 0) { why = "sampler clamp mode unsupported"; return false; }
          if (bx || by) color = {0, 0, 0, 0};
          else if (!texel(memory, tex, unsigned(cx), unsigned(cy), color, why)) return false;
        } else {
          const float fu = ftz(u - 0.5f), fv = ftz(v - 0.5f);
          const int x0 = int(std::floor(fu)), y0 = int(std::floor(fv));
          const float ax = ftz(fu - float(x0)), ay = ftz(fv - float(y0));
          for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
            bool bx, by;
            const int cx = wrapCoordinate(x0 + dx, int(tex.width), clampX, bx), cy = wrapCoordinate(y0 + dy, int(tex.height), clampY, by);
            if (cx < 0 || cy < 0) { why = "sampler clamp mode unsupported"; return false; }
            std::array<float, 4> c{0, 0, 0, 0};
            if (!(bx || by) && !texel(memory, tex, unsigned(cx), unsigned(cy), c, why)) return false;
            const float weight = ftz((dx ? ax : ftz(1 - ax)) * (dy ? ay : ftz(1 - ay)));
            for (unsigned k = 0; k < 4; ++k) color[k] = ftz(color[k] + ftz(ftz(c[k]) * weight));
          }
        }
        ++stats.samples;
      }
      unsigned out = vdata;
      for (unsigned k = 0; k < 4; ++k) if ((dmask >> k) & 1) wave.v[out++][lane] = op == 0 && tex.nfmt == 4 ? gcnBits(color[k]) : gcnBits(color[k]);
    }
    (void)vdstOk;
    return true;
  }
};
} // namespace maxps4::gpu
