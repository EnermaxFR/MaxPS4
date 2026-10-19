// Stage 2 of the translator test: the translated pixel shaders, compiled as
// C++ with GCNKernel.h, must export exactly what the GCNShader interpreter
// exports for every live lane, with the same inputs (barycentrics,
// attributes, user SGPRs, constants and texture). Image sampling inside the
// translated code is delegated to the interpreter executing the same MIMG
// instruction for one lane, so only the translation itself is under test.
#include "../Native/GCNShader.hpp"
#include "../Native/GCNKernel.h"
#include "../Native/maxps4_orbis_v4_translation.h"
#include "GCNTranslatorFixtures.hpp"
#include <cassert>
#include <cstdio>
#include <functional>
#include <map>

using namespace maxps4;
using namespace maxps4::gpu;

struct Export { unsigned target, enable, compressed, done; gcn_u32 data[4]; };
struct Env {
  unsigned lane = 0;
  std::function<float(unsigned, unsigned, unsigned)> interp;
  std::function<void(uint32_t, uint32_t, const gcn_u32*, gcn_u32*)> sample;
  std::vector<Export> exports;
};
#define GCN_UNIFORM
#define GCN_ENV_PARAM Env& env
#define GCN_ENV_INTERP(attr, chan, k) env.interp(attr, chan, k)
#define GCN_ENV_SAMPLE(w0, w1, c, t) env.sample(w0, w1, c, t)
#define GCN_ENV_EXPORT(target, enable, compressed, done, e) \
  env.exports.push_back(Export{target, enable, compressed, done, {e[0], e[1], e[2], e[3]}})
#include GCN_TRANSLATED

static float attribute(unsigned attr, unsigned chan, unsigned lane, unsigned k) {
  uint32_t h = attr * 2654435761u ^ chan * 40503u ^ lane * 97u ^ k * 7919u;
  h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
  return float(int32_t(h % 4001) - 2000) / 1000.0f;
}

int main() {
  constexpr uint64_t base = 0x400000, size = 4 * 1024 * 1024;
  constexpr uint64_t program = base, constants = base + 0x10000, texture = base + 0x20000, scratch = base + 0x80000;
  OrbisV4Translation owner([] {});
  assert(!owner.reserve(base, size) && !owner.map(base, size, 6));
  unsigned shaders = 0, lanes = 0, values = 0;
  {
  auto m = owner.lease();
  const float consts[8] = {0.25f, -1.5f, 3.0f, 0.75f, 2.0f, 0.5f, 7.0f, 0.0f};
  uint32_t cbits[8]; std::memcpy(cbits, consts, sizeof(cbits)); cbits[6] = 0xfffffff0u; // s6 as a large integer (carry)
  assert(!m.write_data(constants, cbits, sizeof(cbits)));
  std::vector<uint8_t> tex(64 * 64 * 4);
  for (size_t k = 0; k < tex.size(); ++k) tex[k] = uint8_t((k * 2654435761u) >> 11);
  assert(!m.write_data(texture, tex.data(), tex.size()));
  const uint32_t vsharp[4] = {uint32_t(constants), 0, 32, 0x20077fac};
  const uint32_t tsharp[8] = {uint32_t(texture >> 8), (10u << 20) | (9u << 26), 63 | (63u << 14), 0xfac | (8u << 20) | (9u << 28), 63u << 13, 0, 0, 0};
  const uint32_t ssharp[4] = {2 | (2u << 3) | (2u << 6), 0, (1u << 20) | (1u << 22), 0};

  using Fn = void (*)(const gcn_u32*, gcn_u32*, Env&);
  const std::map<std::string, Fn> functions{{"ps_clear", ps_clear}, {"ps_textured", ps_textured}, {"ps_alu", ps_alu}};
  for (const auto& f : gcnfix::all()) {
    if (!f.translatable) continue;
    assert(!m.write_data(program, f.code.data(), f.code.size() * 4));
    for (unsigned trial = 0; trial < 8; ++trial) {
      GCNShader ref; GCNShader::Wave wave; wave.configure(32, f.vgprs);
      for (unsigned k = 0; k < 4; ++k) wave.s[k] = vsharp[k];
      for (unsigned k = 0; k < 8; ++k) wave.s[8 + k] = tsharp[k];
      for (unsigned k = 0; k < 4; ++k) wave.s[16 + k] = ssharp[k];
      // Live lanes in whole and partial quads; v0/v1 = barycentrics (some outside [0,1]).
      const uint64_t live = trial == 0 ? ~0ull : (0x0f0f33557f00ffffull * (trial + 1)) | 1;
      wave.setExec(live);
      for (unsigned lane = 0; lane < 64; ++lane) {
        wave.v[0][lane] = gcnBits(attribute(9, trial, lane, 0) * 0.75f + 0.5f);
        wave.v[1][lane] = gcnBits(attribute(9, trial, lane, 1) * 0.75f + 0.5f);
      }
      const auto initial = wave.v;
      wave.interpolate = [&](unsigned attr, unsigned chan, unsigned lane, float p[3]) {
        for (unsigned k = 0; k < 3; ++k) p[k] = attribute(attr, chan + 4 * trial, lane, k);
        return true;
      };
      std::string error;
      if (!ref.run(m, program, wave, error)) { std::fprintf(stderr, "%s: interpreter: %s\n", f.name.c_str(), error.c_str()); return 1; }
      // Scalar prepass: write-once SGPRs (enforced by the translator) make the final file exact.
      std::vector<gcn_u32> s(wave.s.begin(), wave.s.end());
      for (unsigned lane = 0; lane < 64; ++lane) {
        Env env; env.lane = lane;
        env.interp = [&](unsigned attr, unsigned chan, unsigned k) { return attribute(attr, chan + 4 * trial, lane, k); };
        env.sample = [&](uint32_t w0, uint32_t w1, const gcn_u32* c, gcn_u32* t) {
          // The same MIMG instruction, one lane, through the interpreter.
          const uint32_t code[3] = {w0, w1, 0xbf810000u};
          assert(!m.write_data(scratch, code, sizeof(code)));
          GCNShader one; GCNShader::Wave w1v; w1v.configure(32, f.vgprs);
          for (unsigned k = 0; k < 32; ++k) w1v.s[k] = s[k];
          w1v.setExec(1);
          const unsigned vaddr = w1 & 255, vdata = (w1 >> 8) & 255, dmask = (w0 >> 8) & 15;
          for (unsigned k = 0; k < 3 && vaddr + k < f.vgprs; ++k) w1v.v[vaddr + k][0] = c[k];
          std::string e; assert(one.run(m, scratch, w1v, e));
          for (unsigned comp = 0, n = 0; comp < 4; ++comp) if ((dmask >> comp) & 1) t[comp] = w1v.v[vdata + n++][0];
        };
        std::vector<gcn_u32> v(f.vgprs, 0);
        for (unsigned r = 0; r < f.vgprs; ++r) v[r] = initial[r][lane];
        functions.at(f.name)(s.data(), v.data(), env);
        if (env.exports.size() != wave.exports.size()) { std::fprintf(stderr, "%s: export count\n", f.name.c_str()); return 1; }
        for (size_t x = 0; x < env.exports.size(); ++x) {
          const auto& e = wave.exports[x]; const auto& g = env.exports[x];
          if (g.target != e.target || g.enable != e.enable || bool(g.compressed) != e.compressed || bool(g.done) != e.done) {
            std::fprintf(stderr, "%s: export %zu header\n", f.name.c_str(), x); return 1;
          }
          if (!((e.exec >> lane) & 1)) continue;   // lane not exported by the hardware
          for (unsigned c = 0; c < 4; ++c) {
            const bool used = e.compressed ? (c < 2 && ((e.enable >> (c * 2)) & 3)) : ((e.enable >> c) & 1);
            if (!used) continue;
            const gcn_u32 want = e.data[c][lane], got = g.data[c];
            const bool nan = gcn_isnan(gcn_f(want)) && gcn_isnan(gcn_f(got)) && !e.compressed;
            if (want != got && !nan) {
              std::fprintf(stderr, "%s trial %u lane %u export %zu comp %u: translated %08x, interpreter %08x\n",
                           f.name.c_str(), trial, lane, x, c, got, want);
              return 1;
            }
            ++values;
          }
        }
        if ((live >> lane) & 1) ++lanes;
      }
    }
    ++shaders;
  }
  } // the lease must end before the owner closes
  owner.close();
  std::printf("PASS translator stage 2: %u translated pixel shaders, %u live lanes, %u exported values equal to the interpreter\n",
              shaders, lanes, values);
}
