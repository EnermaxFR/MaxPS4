#include "GCNPixelProgram.hpp"
#include <cassert>
#include <iostream>
using maxps4::gpu::GCNPixelProgram;
int main() {
  // Captured PS instructions, with the literal MOV retained.
  const std::vector<uint32_t> code = {
    0xbeeb03ff, 7, 0xc2800100, 0xbf8c007f,
    0x7e000200, 0x7e020201, 0x7e040202, 0x7e060203,
    0xf800180f, 0x03020100, 0xbf810000};
  std::vector<GCNPixelProgram::Instruction> out;
  std::string error;
  assert(GCNPixelProgram::decode(code, out, error));
  assert(error.empty() && out.size() == 9);
  assert(out[0].extra == 7 && out[1].pc == 2);
  assert(out[7].extra == 0x03020100);
  for (size_t n = 0; n < code.size(); ++n) {
    assert(!GCNPixelProgram::decode({code.begin(), code.begin() + n}, out, error));
    assert(out.empty() && !error.empty());
  }
  auto bad = code;
  bad[8] |= 1U << 10; // compressed export is not supported
  assert(!GCNPixelProgram::decode(bad, out, error));
  bad = code; bad[2] &= ~256U; // register offset needs separate semantics
  assert(!GCNPixelProgram::decode(bad, out, error));
  bad = code; bad[2] = (bad[2] & ~(127U << 15)) | (101U << 15);
  assert(!GCNPixelProgram::decode(bad, out, error));
  bad = code; bad[1] = 0xbf810000; // literal is not a terminator
  assert(GCNPixelProgram::decode(bad, out, error) && out.size() == 9);
  bad = code; bad[0] = 0xbeeb0387; bad.erase(bad.begin() + 1);
  assert(GCNPixelProgram::decode(bad, out, error) && out.size() == 9);
  bad = code; bad[4] = 0x7e0002ff; bad.insert(bad.begin() + 5, 0xbf810000);
  assert(GCNPixelProgram::decode(bad, out, error) && out.size() == 9);
  bad = code; bad.insert(bad.end() - 1, 0x7e000200);
  assert(!GCNPixelProgram::decode(bad, out, error));
  bad = code; bad.insert(bad.end() - 1, {0xf800180f, 0x03020100});
  assert(!GCNPixelProgram::decode(bad, out, error));
  assert(!GCNPixelProgram::decode({0xbf810000}, out, error));
  assert(!GCNPixelProgram::decode(std::vector<uint32_t>(257), out, error));
  std::cout << "PASS GCN pixel program decoding (no rendering)\n";
}
