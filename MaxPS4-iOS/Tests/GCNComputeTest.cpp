#include "../Native/GCNCompute.hpp"
#include "../Native/GNMCommandExecutor.hpp"
#include "../Native/maxps4_orbis_v4_translation.h"
#include <cassert>
#include <iostream>
using namespace maxps4;
using namespace maxps4::gpu;
// Independent synthetic integer kernel, assembled for gfx700. It repeats four
// input words, masks a partial final wave, and bounds stores separately.
static std::vector<uint32_t> kernel() {
  return {
      0xbeeb03ff, 13, // s_mov_b32 vcc_hi, literal
      0x936ac00c,     // s_mul_i32 vcc_lo, s12, 64
      0xc2060900,     // s_buffer_load_dword s12, s[8:11], 0
      0x4a00006a,     // v_add_i32 v0, vcc, vcc_lo, v0
      0xbf8c007f,     // s_waitcnt lgkmcnt(0)
      0x7da8000c,     // v_cmpx_gt_u32 vcc, s12, v0
      0xbf88000a,     // s_cbranch_execz end
      0xc2040901,     // s_buffer_load_dword s8, s[8:11], 1
      0xbf8c007f,
      0x36020008,             // v_and_b32 v1, s8, v0
      0xe0002000, 0x80000101, // buffer_load_format_x v1, v1, s[0:3], 0 idxen
      0xd1a80000, 0x00020006, // v_cmpx_gt_u32 s[0:1], s6, v0
      0xbf8c0f70,             // s_waitcnt vmcnt(0)
      0xe0102000, 0x80010100, // buffer_store_format_x v1, v0, s[4:7], 0 idxen
      0xbf810000};
}
int main() {
  std::string error;
  std::vector<GCNCompute::Instruction> decoded;
  assert(GCNCompute::decode(kernel(), decoded, error) && decoded.size() == 15);
  assert(decoded[0].length == 2 && decoded[6].target == 14);
  assert(!GCNCompute::decode({0xbeeb03ff}, decoded, error) &&
         error.find("truncated") != std::string::npos);
  assert(
      !GCNCompute::decode({0xbf8a0000, 0xbf810000}, decoded, error)); // barrier
  assert(!GCNCompute::decode({0xffffffff, 0xbf810000}, decoded, error));
  assert(!GCNCompute::decode({0xbf88ffff, 0xbf810000}, decoded,
                             error)); // backwards
  assert(!GCNCompute::decode({0xbf880001, 0xbeeb03ff, 0xbf810000, 0xbf810000},
                             decoded, error)); // target in literal
  auto bad = kernel();
  bad[13] |= 0x100;
  assert(!GCNCompute::decode(bad, decoded, error)); // VOP3 abs
  bad = kernel();
  bad[11] |= 0x1000;
  assert(!GCNCompute::decode(bad, decoded, error)); // offen
  bad = kernel();
  bad.pop_back();
  assert(!GCNCompute::decode(bad, decoded, error));

  constexpr uint64_t base = 0x400000;
  OrbisV4Translation owner([] {});
  const auto page = owner.page_size();
  assert(!owner.reserve(base, page * 6));
  for (unsigned i = 0; i < 6; ++i)
    assert(!owner.map(base + page * i, page, i == 5 ? 4 : 6));
  {
    auto memory = owner.lease();
    GCNCompute engine;
    const uint64_t input = base + page - 8, output = base + page * 2 - 8,
                   constants = base + page * 4;
    std::array<uint32_t, 1024> sh{};
    sh[0x20c] = uint32_t(base >> 8);
    sh[0x212] = 0x40;
    sh[0x213] = 0x98;
    sh[0x207] = 64;
    sh[0x208] = 1;
    sh[0x209] = 1;
    auto descriptor = [&](unsigned r, uint64_t address, uint32_t stride,
                          uint32_t count, uint32_t format) {
      sh[0x240 + r] = uint32_t(address);
      sh[0x241 + r] = uint32_t(address >> 32) | (stride << 16);
      sh[0x242 + r] = count;
      sh[0x243 + r] = format;
    };
    descriptor(0, input, 4, 4, 0x20024004);
    descriptor(4, output, 4, 70, 0x18024004);
    descriptor(8, constants, 16, 1, 0x20077fac);
    const auto original = sh;
    auto writeCode = [&](const std::vector<uint32_t> &code) {
      assert(!memory.write_data(base, code.data(), code.size() * 4));
    };
    auto put = [&](uint64_t address, uint32_t value) {
      assert(!memory.write_data(address, &value, 4));
    };
    auto get = [&](uint64_t address) {
      uint32_t v = 0;
      assert(!memory.read_data(address, &v, 4));
      return v;
    };
    const uint32_t values[] = {0x12345678, 0, 0xffffffff, 0x80000000};
    assert(!memory.write_data(input, values, sizeof(values)));
    put(constants, 90);
    put(constants + 4, 3);
    auto reset = [&] {
      for (unsigned i = 0; i < 132; ++i)
        put(output + i * 4, 0xdeadbeef);
      writeCode(kernel());
      sh = original;
    };
    reset();
    assert(engine.dispatch(memory, sh, {3, 1, 1}, 1, error));
    assert(engine.stats.groups == 3 && engine.stats.lanes == 192 &&
           engine.stats.stores == 70 && engine.stats.verified == 70);
    for (unsigned i = 0; i < 132; ++i)
      assert(get(output + i * 4) == (i < 70 ? values[i % 4] : 0xdeadbeef));
    assert(engine.stats.firstWrite == output &&
           engine.stats.lastWrite == output + 69 * 4);
    // Smaller scalar bound exercises a partial final EXEC and empty group.
    reset();
    put(constants, 65);
    assert(engine.dispatch(memory, sh, {3, 1, 1}, 1, error));
    assert(engine.stats.stores == 65);
    for (unsigned i = 0; i < 132; ++i)
      assert(get(output + i * 4) == (i < 65 ? values[i % 4] : 0xdeadbeef));
    // Predecode rejects unknown instructions before any store.
    reset();
    bad = kernel();
    bad.insert(bad.end() - 1, 0xbf8a0000);
    writeCode(bad);
    assert(!engine.dispatch(memory, sh, {1, 1, 1}, 1, error) &&
           engine.stats.stores == 0 && get(output) == 0xdeadbeef);
    reset();
    sh[0x243] |= 0x00800000;
    assert(!engine.dispatch(memory, sh, {1, 1, 1}, 1, error) &&
           engine.stats.stores == 0); // add_tid
    reset();
    sh[0x243] = (sh[0x243] & ~0x78000U) | 0x10000;
    assert(!engine.dispatch(memory, sh, {1, 1, 1}, 1, error)); // format
    reset();
    sh[0x241] |= 0x80000000;
    assert(!engine.dispatch(memory, sh, {1, 1, 1}, 1, error)); // swizzle
    reset();
    sh[0x242] = 0;
    assert(!engine.dispatch(memory, sh, {1, 1, 1}, 1, error)); // bounds
    reset();
    sh[0x240] = 0;
    sh[0x241] = 4 << 16;
    assert(!engine.dispatch(memory, sh, {1, 1, 1}, 1, error) &&
           engine.stats.stores == 0);
    reset();
    sh[0x244] = 0;
    sh[0x245] = 4 << 16;
    assert(!engine.dispatch(memory, sh, {1, 1, 1}, 1, error) &&
           engine.stats.stores == 0);
    reset();
    sh[0x248] = 0;
    sh[0x249] = 16 << 16;
    assert(
        !engine.dispatch(memory, sh, {1, 1, 1}, 1, error)); // scalar unmapped
    reset();
    sh[0x24a] = 0;
    assert(!engine.dispatch(memory, sh, {1, 1, 1}, 1, error)); // scalar bounds
    reset();
    sh[0x213] = 0;
    assert(!engine.dispatch(memory, sh, {1, 1, 1}, 1, error) &&
           error.find("SGPR") != std::string::npos);
    reset();
    assert(!engine.dispatch(memory, sh, {0, 1, 1}, 1, error));
    assert(!engine.dispatch(memory, sh, {0xffffffff, 0xffffffff, 1}, 1, error));
    assert(!engine.dispatch(memory, sh, {1, 1, 1}, 3, error));
    sh[0x207] = 65;
    assert(!engine.dispatch(memory, sh, {1, 1, 1}, 1, error));
    reset();
    sh[0x213] |= 1;
    assert(!engine.dispatch(memory, sh, {1, 1, 1}, 1, error)); // scratch
    reset();
    sh[0x213] |= 1 << 15;
    assert(!engine.dispatch(memory, sh, {1, 1, 1}, 1, error)); // LDS
    reset();
    sh[0x204] = 1;
    assert(!engine.dispatch(memory, sh, {1, 1, 1}, 1, error));
    reset();
    assert(!engine.dispatch(memory, sh, {1, 2, 1}, 1, error)); // missing Y ABI
    // Full 3-D input ABI, one-wave workgroups, and bounded no-memory shader.
    reset();
    writeCode({0xbf810000});
    sh[0x207] = 2;
    sh[0x208] = 2;
    sh[0x209] = 2;
    sh[0x213] = 0x98 | 0x300 | 0x1000;
    assert(engine.dispatch(memory, sh, {2, 3, 4}, 1, error) &&
           engine.stats.groups == 24 && engine.stats.lanes == 192);
    // Observe modulo-32 multiplication and carry-out via a real buffer store.
    reset();
    sh[0x207] = 1;
    writeCode({0xbe8003ff, 0xffffffff, // s_mov_b32 s0, literal
               0x93088200,             // s_mul_i32 s8, s0, 2
               0x4a000008,             // v0 = s8 + local_id(0), VCC=0
               0x4a020082,             // v1 = 2 + v0, VCC=1
               0x4a04026a,             // v2 = VCC_LO + v1 = 1
               0xe0100000, 0x80010200, // store v2, s[4:7], no index
               0xbf810000});
    assert(engine.dispatch(memory, sh, {1, 1, 1}, 1, error));
    assert(get(output) == 1 && engine.stats.stores == 1);
    reset();
    sh[0x207] = 1;
    writeCode({0xbe8803c1, // s_mov_b32 s8, -1 (inline)
               0x4a000008, 0xe0100000, 0x80010000, 0xbf810000});
    assert(engine.dispatch(memory, sh, {1, 1, 1}, 1, error));
    assert(get(output) == 0xffffffff);
    reset();
    writeCode({0x4a000600, 0xbf810000}); // v0 = s0 + undefined v3
    assert(!engine.dispatch(memory, sh, {1, 1, 1}, 1, error));
    assert(error.find("VGPR") != std::string::npos);
    // Real PM4 integration invokes compute and then performs a fence write.
    reset();
    put(constants, 65);
    GNMCommandExecutor pm4;
    pm4.state.shader = sh;
    std::vector<uint32_t> words{
        0xc0031502, 3, 1, 1, 1, 0xc0033700, 0x500, uint32_t(constants + 8), 0,
        0xaabbccdd};
    assert(pm4.execute(memory, base, words, error) &&
           pm4.compute.stats.stores == 65 && get(constants + 8) == 0xaabbccdd);
    // Read-only mappings are never writable through the compute backend.
    reset();
    sh[0x244] = uint32_t(base + page * 5);
    assert(!engine.dispatch(memory, sh, {1, 1, 1}, 1, error));
  }
  owner.close();
  std::cout
      << "PASS GCN decode, wave64 scalar/vector/EXEC/branch, patterned buffer "
         "output, bounds, dimensions, budgets, undefined registers, "
         "unsupported resources/opcodes, readback and PM4 dispatch\n";
}
