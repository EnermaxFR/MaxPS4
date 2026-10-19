// GCNKernel.h (C++ side of the dual C++/MSL prelude) against the GCNShader
// interpreter: every supported vector ALU opcode, VOPC compare and output
// modifier, bit for bit, on special values and random operands.
#include "../Native/GCNKernel.h"
#include "../Native/GCNShader.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>

using maxps4::gpu::GCNShader;

int main() {
  std::vector<gcn_u32> specials = {0u, 0x80000000u, 0x3f800000u, 0xbf800000u, 0x7f800000u, 0xff800000u, 0x7fc00000u,
                                   0xffc00001u, 0x00000001u, 0x807fffffu, 0x00800000u, 0x7f7fffffu, 0x4f800000u, 0xcf000000u,
                                   0x3effffffu, 0x3f000000u, 0x477fe000u, 0x477ff000u, 0x33800000u, 0x387fc000u, 1u, 31u, 32u,
                                   0xffffffffu, 0x7fffffffu, 0x12345678u, 255u, 256u};
  uint64_t state = 0x6d61787073343231ull;
  auto next = [&] { state = state * 6364136223846793005ull + 1442695040888963407ull; return gcn_u32(state >> 32); };
  auto operand = [&]() -> gcn_u32 {
    const gcn_u32 kind = next() % 8;
    if (kind == 0) return specials[next() % specials.size()];
    if (kind == 1) return gcn_b(float(gcn_i32(next() % 2001) - 1000) / 8.0f);  // small floats
    if (kind == 2) return next() % 64;                                            // small integers / shifts
    return next();
  };
  unsigned ops = 0; uint64_t cases = 0;
  for (gcn_u32 op = 256; op < 512; ++op) {
    if (!gcn_valu_supported(op)) continue;
    ++ops;
    if (std::getenv("GCN_OPS_TRACE")) std::fprintf(stderr, "op %u\n", op);
    for (int n = 0; n < 200000; ++n) {
      const gcn_u32 a = operand(), b = operand(), c = operand();
      const bool carryIn = next() & 1;
      bool kernelCarry = carryIn, refCarry = carryIn;
      uint32_t expected = 0;
      assert(GCNShader::compute(op, a, b, c, expected, refCarry));
      const gcn_u32 got = gcn_valu(op, a, b, c, kernelCarry);
      // NaN payloads are not specified by C++ or by GPUs (operand order chosen
      // by the compiler): two NaN results of a float operation are equivalent.
      const bool bothNaN = GCNShader::isFloatOutput(op) && gcn_isnan(gcn_f(got)) && gcn_isnan(gcn_f(expected));
      // fmin/fmax may return either zero for (-0, +0) (C and Metal leave it open).
      const bool minMax = op == 271 || op == 272 || op == 337 || op == 340 || op == 343;
      const bool zeros = minMax && ((got | expected) & 0x7fffffffu) == 0;
      if ((got != expected && !bothNaN && !zeros) || kernelCarry != refCarry) {
        std::fprintf(stderr, "op %u a=%08x b=%08x c=%08x carry=%d: kernel %08x/%d, interpreter %08x/%d\n", op, a, b, c,
                     int(carryIn), got, int(kernelCarry), expected, int(refCarry));
        return 1;
      }
      ++cases;
    }
  }
  unsigned compares = 0;
  for (gcn_u32 op = 0; op < 256; ++op) {
    if (!gcn_compare_supported(op)) continue;
    bool dummy = false;
    if (!GCNShader::compare(op, 0, 0, dummy)) continue;
    ++compares;
    for (int n = 0; n < 50000; ++n) {
      const gcn_u32 a = operand(), b = operand();
      bool expected = false;
      GCNShader::compare(op, a, b, expected);
      if (gcn_compare(op, a, b) != expected) { std::fprintf(stderr, "compare %u %08x %08x\n", op, a, b); return 1; }
      ++cases;
    }
  }
  for (int n = 0; n < 200000; ++n) {  // omod/clamp as GCNShader::vectorAlu's finish()
    const gcn_u32 v = operand(), omod = next() & 3; const bool clamp = next() & 1;
    float f = gcn_f(v);
    if (omod == 1) f *= 2; else if (omod == 2) f *= 4; else if (omod == 3) f *= 0.5f;
    if (clamp) f = std::min(1.0f, std::max(0.0f, f));
    if (gcn_omod(v, omod, clamp) != gcn_b(f) && !(gcn_isnan(f) && gcn_isnan(gcn_f(gcn_omod(v, omod, clamp))))) { std::fprintf(stderr, "omod %08x %u %d\n", v, omod, int(clamp)); return 1; }
    ++cases;
  }
  for (gcn_u32 h = 0; h < 65536; ++h)
    assert(gcn_b(gcn_half_to_float(h)) == maxps4::gpu::gcnBits(maxps4::gpu::gcnHalfToFloat(uint16_t(h))) || gcn_isnan(gcn_half_to_float(h)));
  std::printf("PASS GCN kernel prelude: %u vector ALU ops, %u compares, omod/clamp and f16 decode equal to the interpreter (%llu cases)\n",
              ops, compares, (unsigned long long)cases);
}
