#pragma once
// GCN1/2 pixel-program validation. Decoding alone does not render pixels.
#include "GCNCompute.hpp"

namespace maxps4::gpu {
struct GCNPixelProgram {
  enum class Op { scalarMove, scalarLoad4, wait, vectorMove, exportColor, end };
  struct Instruction {
    Op op;
    size_t pc;
    uint32_t word, extra;
  };
  static bool decode(const std::vector<uint32_t>& code,
                     std::vector<Instruction>& out, std::string& error) {
    // Publish a program only after complete validation.
    out.clear();
    error.clear();
    std::vector<Instruction> pending;
    bool exported = false;
    auto fail = [&](size_t pc, const char* reason) {
      std::ostringstream message;
      message << "GCN pixel decode +0x" << std::hex << pc * 4 << ": " << reason;
      if (pc < code.size()) message << " word=0x" << code[pc];
      error = message.str();
      return false;
    };
    if (code.size() > GCNCompute::maxCodeWords) return fail(0, "code budget");
    for (size_t pc = 0; pc < code.size();) {
      uint32_t w = code[pc], extra = 0;
      size_t length = 1;
      Op op;
      if ((w & 0xff800000U) == 0xbe800000U) {
        if (((w >> 8) & 255) != 3 || !GCNCompute::destination((w >> 16) & 127))
          return fail(pc, "unsupported SOP1");
        if ((w & 255) == 255) length = 2;
        else if (!GCNCompute::scalarSource(w & 255)) return fail(pc, "scalar source");
        op = Op::scalarMove;
      } else if ((w & 0xf8000000U) == 0xc0000000U) {
        unsigned dst = (w >> 15) & 127;
        if (((w >> 22) & 31) != 10 || !(w & 256) ||
            ((w >> 9) & 63) * 2 > 100 || dst > 100)
          return fail(pc, "unsupported SMRD");
        op = Op::scalarLoad4;
      } else if ((w & 0xfe000000U) == 0x7e000000U) {
        if (((w >> 9) & 255) != 1) return fail(pc, "unsupported VOP1");
        if ((w & 511) == 255) length = 2;
        else if (!GCNCompute::vectorSource(w & 511)) return fail(pc, "vector source");
        op = Op::vectorMove;
      } else if ((w & 0xfc000000U) == 0xf8000000U) {
        // Full RGBA, uncompressed MRT0, DONE and valid-mask export only.
        if (w != 0xf800180fU || exported) return fail(pc, "unsupported color export");
        length = 2;
        exported = true;
        op = Op::exportColor;
      } else if ((w & 0xffff0000U) == 0xbf8c0000U && !(w & 0xf080)) {
        op = Op::wait;
      } else if (w == 0xbf810000U) {
        if (!exported) return fail(pc, "missing color export");
        pending.push_back({Op::end, pc, w, 0});
        out = std::move(pending);
        return true;
      } else return fail(pc, "unsupported encoding");
      if (exported && op != Op::exportColor) return fail(pc, "instruction after DONE export");
      if (length > code.size() - pc) return fail(pc, "truncated instruction/literal");
      if (length == 2) extra = code[pc + 1];
      pending.push_back({op, pc, w, extra});
      pc += length;
    }
    return fail(code.size(), "missing s_endpgm");
  }
};
}
