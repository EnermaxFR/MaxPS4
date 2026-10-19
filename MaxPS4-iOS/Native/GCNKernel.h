// Dual-language prelude for GCN shaders translated by GCNShaderTranslator.
// The same text compiles as C++17 (host twin, used to test translations
// against the GCNShader interpreter bit for bit) and as Metal Shading Language
// (GPU execution on Apple devices). Every operation reproduces
// GCNShader::compute() for the same VOP3 opcode number. Transcendental
// functions (sqrt, exp2, log2, sin, cos) follow the host libm in C++ and
// Metal's implementation on the GPU; compile Metal with fast math disabled.
#ifndef MAXPS4_GCN_KERNEL_H
#define MAXPS4_GCN_KERNEL_H

#ifdef __METAL_VERSION__
#include <metal_stdlib>
using namespace metal;
#define GCN_INLINE inline
#define GCN_THREAD thread
typedef uint gcn_u32;
typedef int gcn_i32;
typedef ulong gcn_u64;
typedef long gcn_i64;
inline float gcn_f(gcn_u32 v) { return as_type<float>(v); }
inline gcn_u32 gcn_b(float f) { return as_type<uint>(f); }
#define gcn_fma fma
#define gcn_fmin fmin
#define gcn_fmax fmax
#define gcn_floor floor
#define gcn_ceil ceil
#define gcn_trunc trunc
#define gcn_rint rint
#define gcn_sqrt sqrt
#define gcn_exp2 exp2
#define gcn_log2 log2
#define gcn_sin sin
#define gcn_cos cos
#define gcn_isnan isnan
inline gcn_u32 gcn_popcount(gcn_u32 v) { return popcount(v); }
inline gcn_u32 gcn_clz(gcn_u32 v) { return clz(v); }
inline gcn_u32 gcn_ctz(gcn_u32 v) { return ctz(v); }
#else
#include <cmath>
#include <cstdint>
#include <cstring>
#define GCN_INLINE static inline
#define GCN_THREAD
typedef uint32_t gcn_u32;
typedef int32_t gcn_i32;
typedef uint64_t gcn_u64;
typedef int64_t gcn_i64;
GCN_INLINE float gcn_f(gcn_u32 v) { float f; std::memcpy(&f, &v, 4); return f; }
GCN_INLINE gcn_u32 gcn_b(float f) { gcn_u32 v; std::memcpy(&v, &f, 4); return v; }
#define gcn_fma std::fma
#define gcn_fmin std::fmin
#define gcn_fmax std::fmax
#define gcn_floor std::floor
#define gcn_ceil std::ceil
#define gcn_trunc std::trunc
#define gcn_rint std::nearbyint
#define gcn_sqrt std::sqrt
#define gcn_exp2 std::exp2
#define gcn_log2 std::log2
#define gcn_sin std::sin
#define gcn_cos std::cos
#define gcn_isnan std::isnan
GCN_INLINE gcn_u32 gcn_popcount(gcn_u32 v) { return gcn_u32(__builtin_popcount(v)); }
GCN_INLINE gcn_u32 gcn_clz(gcn_u32 v) { return gcn_u32(__builtin_clz(v)); }
GCN_INLINE gcn_u32 gcn_ctz(gcn_u32 v) { return gcn_u32(__builtin_ctz(v)); }
#endif

// IEEE binary16 from float; rtz selects V_CVT_PKRTZ rounding (= gcnHalf).
GCN_INLINE gcn_u32 gcn_half(float value, bool rtz) {
  const gcn_u32 f = gcn_b(value), sign = (f >> 16) & 0x8000u;
  const gcn_i32 exponent = gcn_i32((f >> 23) & 255u) - 127 + 15;
  gcn_u32 mantissa = f & 0x7fffffu;
  if (((f >> 23) & 255u) == 255u) return sign | 0x7c00u | (mantissa ? 0x200u : 0u);
  if (exponent >= 31) return sign | (rtz ? 0x7bffu : 0x7c00u);
  if (exponent <= 0) {
    if (exponent < -10) return sign;
    mantissa |= 0x800000u;
    const gcn_u32 shift = gcn_u32(14 - exponent);
    gcn_u32 h16 = mantissa >> shift;
    if (!rtz) {
      const gcn_u32 rest = mantissa & ((1u << shift) - 1u), mid = 1u << (shift - 1u);
      if (rest > mid || (rest == mid && (h16 & 1u))) ++h16;
    }
    return sign | h16;
  }
  gcn_u32 h16 = (gcn_u32(exponent) << 10) | (mantissa >> 13);
  if (!rtz) {
    const gcn_u32 rest = mantissa & 0x1fffu;
    if (rest > 0x1000u || (rest == 0x1000u && (h16 & 1u))) ++h16;
  }
  return sign | h16;
}
GCN_INLINE float gcn_half_to_float(gcn_u32 h) {
  const gcn_u32 sign = (h & 0x8000u) << 16, exponent = (h >> 10) & 31u, mantissa = h & 1023u;
  if (!exponent) { const float v = float(mantissa) * 5.9604644775390625e-8f; return sign ? -v : v; }
  if (exponent == 31u) return gcn_f(sign | 0x7f800000u | (mantissa << 13));
  return gcn_f(sign | ((exponent + 112u) << 23) | (mantissa << 13));
}
GCN_INLINE float gcn_legacy_mul(float x, float y) { return (x == 0.0f || y == 0.0f) ? 0.0f : x * y; }
GCN_INLINE float gcn_ldexp(float x, gcn_i32 e) {
  // Exact scaling by 2^e in steps that stay within the float exponent range.
  // Beyond +-400 every finite non-zero value has already saturated to inf/0.
  if (e > 400) e = 400;
  if (e < -400) e = -400;
  while (e > 100) { x *= 1.2676506002282294e30f; e -= 100; }
  while (e < -100) { x *= 7.888609052210118e-31f; e += 100; }
  return x * gcn_f(gcn_u32(e + 127) << 23);
}

// Supported vector ALU operations (VOP3 numbering, as GCNShader::compute).
GCN_INLINE bool gcn_valu_supported(gcn_u32 op) {
  switch (op) {
  case 256: case 259: case 260: case 261: case 262: case 263: case 264: case 269: case 270: case 271: case 272:
  case 273: case 274: case 275: case 276: case 277: case 278: case 279: case 280: case 281: case 282: case 283:
  case 284: case 285: case 287: case 288: case 289: case 293: case 294: case 295: case 299: case 303: case 304:
  case 320: case 321: case 328: case 330: case 331: case 337: case 340: case 343: case 361: case 385: case 389:
  case 390: case 391: case 392: case 394: case 395: case 397: case 401: case 402: case 403: case 404: case 416:
  case 417: case 418: case 419: case 420: case 421: case 423: case 426: case 427: case 430: case 435: case 437:
  case 438: case 439:
    return true;
  default:
    return false;
  }
}

// r = op(a, b, c); carry is the lane's carry/condition bit (in and out).
GCN_INLINE gcn_u32 gcn_valu(gcn_u32 op, gcn_u32 a, gcn_u32 b, gcn_u32 c, GCN_THREAD bool& carry) {
  const float fa = gcn_f(a), fb = gcn_f(b), fc = gcn_f(c);
  switch (op) {
  case 256: return carry ? b : a;                                   // v_cndmask_b32
  case 259: return gcn_b(fa + fb);
  case 260: return gcn_b(fa - fb);
  case 261: return gcn_b(fb - fa);
  case 262: return gcn_b(gcn_legacy_mul(fa, fb) + fc);
  case 263: return gcn_b(gcn_legacy_mul(fa, fb));
  case 264: return gcn_b(fa * fb);
  case 269: return gcn_b(fa < fb ? fa : fb);
  case 270: return gcn_b(fa > fb ? fa : fb);
  case 271: return gcn_b(gcn_fmin(fa, fb));
  case 272: return gcn_b(gcn_fmax(fa, fb));
  case 273: return gcn_u32(gcn_i32(a) < gcn_i32(b) ? gcn_i32(a) : gcn_i32(b));
  case 274: return gcn_u32(gcn_i32(a) > gcn_i32(b) ? gcn_i32(a) : gcn_i32(b));
  case 275: return a < b ? a : b;
  case 276: return a > b ? a : b;
  case 277: return a >> (b & 31u);
  case 278: return b >> (a & 31u);
  case 279: return gcn_u32(gcn_i32(a) >> (b & 31u));
  case 280: return gcn_u32(gcn_i32(b) >> (a & 31u));
  case 281: return a << (b & 31u);
  case 282: return b << (a & 31u);
  case 283: return a & b;
  case 284: return a | b;
  case 285: return a ^ b;
  case 287: return gcn_b(fa * fb + fc);                            // v_mac_f32 (c = dst)
  case 288: return gcn_b(fa * fc + fb);                            // v_madmk_f32 (c = K)
  case 289: return gcn_b(fa * fb + fc);                            // v_madak_f32 (c = K)
  case 293: { const gcn_u64 t = gcn_u64(a) + b; carry = (t >> 32) != 0; return gcn_u32(t); }
  case 294: carry = b > a; return a - b;
  case 295: carry = a > b; return b - a;
  case 299: return gcn_b(gcn_ldexp(fa, gcn_i32(b)));
  case 303: return gcn_half(fa, true) | (gcn_half(fb, true) << 16);
  case 304: return (a & 0xffffu) | (b << 16);
  case 320: return gcn_b(gcn_legacy_mul(fa, fb) + fc);
  case 321: return gcn_b(fa * fb + fc);
  case 328: { const gcn_u32 off = b & 31u, width = c & 31u; return width ? (a >> off) & ((1u << width) - 1u) : 0u; }
  case 330: return (a & b) | (~a & c);
  case 331: return gcn_b(gcn_fma(fa, fb, fc));
  case 337: return gcn_b(gcn_fmin(gcn_fmin(fa, fb), fc));
  case 340: return gcn_b(gcn_fmax(gcn_fmax(fa, fb), fc));
  case 343: return gcn_b(gcn_fmax(gcn_fmin(fa, fb), gcn_fmin(gcn_fmax(fa, fb), fc)));
  case 361: return a * b;
  case 385: return a;
  case 389: return gcn_b(float(gcn_i32(a)));
  case 390: return gcn_b(float(a));
  case 391: return (gcn_isnan(fa) || fa <= 0.0f) ? 0u : (fa >= 4294967295.0f ? 0xffffffffu : gcn_u32(fa));
  case 392: return gcn_isnan(fa) ? 0u : (fa >= 2147483647.0f ? 0x7fffffffu : (fa <= -2147483648.0f ? 0x80000000u : gcn_u32(gcn_i32(fa))));
  case 394: return gcn_half(fa, false);
  case 395: return gcn_b(gcn_half_to_float(a & 0xffffu));
  case 397: return gcn_u32(gcn_i32(gcn_floor(fa)));
  case 401: return gcn_b(float(a & 255u));
  case 402: return gcn_b(float((a >> 8) & 255u));
  case 403: return gcn_b(float((a >> 16) & 255u));
  case 404: return gcn_b(float(a >> 24));
  case 416: return gcn_b(fa - gcn_floor(fa));
  case 417: return gcn_b(gcn_trunc(fa));
  case 418: return gcn_b(gcn_ceil(fa));
  case 419: return gcn_b(gcn_rint(fa));
  case 420: return gcn_b(gcn_floor(fa));
  case 421: return gcn_b(gcn_exp2(fa));
  case 423: return gcn_b(gcn_log2(fa));
  case 426: case 427: return gcn_b(1.0f / fa);
  case 430: return gcn_b(1.0f / gcn_sqrt(fa));
  case 435: return gcn_b(gcn_sqrt(fa));
  case 437: return gcn_b(gcn_sin(fa * 6.28318530717958647692f));
  case 438: return gcn_b(gcn_cos(fa * 6.28318530717958647692f));
  case 439: return ~a;
  default: return 0u;  // never emitted: the translator refuses unsupported opcodes
  }
}

// VOPC compares (op < 256), as GCNShader::compare; F32, I32 and U32 families.
GCN_INLINE bool gcn_compare_supported(gcn_u32 op) {
  const gcn_u32 kind = op & 15u, family = op & 0xf0u;
  return family == 0x00u || ((family == 0x80u || family == 0xc0u) && kind <= 7u);
}
GCN_INLINE bool gcn_compare(gcn_u32 op, gcn_u32 a, gcn_u32 b) {
  const gcn_u32 kind = op & 15u, family = op & 0xf0u;
  if (family == 0x00u) {
    const float x = gcn_f(a), y = gcn_f(b);
    const bool unordered = gcn_isnan(x) || gcn_isnan(y);
    switch (kind) {
    case 0: return false; case 1: return x < y; case 2: return x == y; case 3: return x <= y;
    case 4: return x > y; case 5: return x < y || x > y; case 6: return x >= y; case 7: return !unordered;
    case 8: return unordered; case 9: return !(x >= y); case 10: return !(x < y || x > y); case 11: return !(x > y);
    case 12: return !(x <= y); case 13: return !(x == y); case 14: return !(x < y); default: return true;
    }
  }
  if (family == 0x80u) {
    const gcn_i32 x = gcn_i32(a), y = gcn_i32(b);
    switch (kind) { case 1: return x < y; case 2: return x == y; case 3: return x <= y; case 4: return x > y;
      case 5: return x != y; case 6: return x >= y; case 7: return true; default: return false; }
  }
  switch (kind) { case 1: return a < b; case 2: return a == b; case 3: return a <= b; case 4: return a > b;
    case 5: return a != b; case 6: return a >= b; case 7: return true; default: return false; }
}

// Output modifiers of float results (VOP3 omod/clamp), as GCNShader.
GCN_INLINE gcn_u32 gcn_omod(gcn_u32 v, gcn_u32 omod, bool clamp) {
  float f = gcn_f(v);
  if (omod == 1u) f *= 2.0f; else if (omod == 2u) f *= 4.0f; else if (omod == 3u) f *= 0.5f;
  if (clamp) { // std::min(1.0f, std::max(0.0f, f)): NaN clamps to 0
    f = (0.0f < f) ? f : 0.0f;
    f = (f < 1.0f) ? f : 1.0f;
  }
  return gcn_b(f);
}
#endif
