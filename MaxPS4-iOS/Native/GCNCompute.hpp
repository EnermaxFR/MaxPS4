#pragma once
// Bounded, synchronous GCN1/2 wave64 interpreter. No executable host mappings.
// Only the explicitly decoded integer/buffer subset is executable; LDS,
// barriers, atomics, floating point and other generations fail closed.
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace maxps4::gpu {
struct GCNCompute {
  enum class Op {
    mov,
    mul,
    scalarLoad,
    add,
    bitAnd,
    compare,
    branchExecZero,
    wait,
    load,
    store,
    end
  };
  struct Instruction {
    Op op;
    uint32_t word = 0, extra = 0;
    size_t pc = 0, length = 1, target = 0;
  };
  struct Stats {
    uint64_t groups = 0, lanes = 0, instructions = 0, loads = 0, stores = 0,
             verified = 0;
    uint64_t firstWrite = 0, lastWrite = 0;
    uint32_t firstValue = 0, lastValue = 0;
    std::array<uint64_t, 8> scalarAddress{};
    std::array<uint32_t, 8> scalarValue{};
    size_t scalarReads = 0;
  } stats;
  static constexpr size_t maxCodeWords = 256;
  static constexpr uint64_t maxThreads = 4 * 1024 * 1024,
                            maxSteps = 64 * 1024 * 1024;
  static bool scalarSource(unsigned s) {
    return s <= 103 || s == 106 || s == 107 || (s >= 128 && s <= 208);
  }
  static bool destination(unsigned s) {
    return s <= 103 || s == 106 || s == 107;
  }
  static bool vectorSource(unsigned s) { return s >= 256 || scalarSource(s); }
  static bool decode(const std::vector<uint32_t> &code,
                     std::vector<Instruction> &out, std::string &error) {
    out.clear();
    auto fail = [&](size_t pc, const std::string &why) {
      std::ostringstream s;
      s << "GCN decode +0x" << std::hex << pc * 4 << ": " << why;
      if (pc < code.size())
        s << " word=0x" << code[pc];
      error = s.str();
      return false;
    };
    if (code.size() > maxCodeWords)
      return fail(0, "code budget");
    bool ended = false;
    for (size_t pc = 0; pc < code.size();) {
      const auto w = code[pc];
      Instruction i{};
      i.word = w;
      i.pc = pc;
      if ((w & 0xff800000U) == 0xbe800000U) {
        if (((w >> 8) & 255) != 3 || !destination((w >> 16) & 127))
          return fail(pc, "unsupported SOP1");
        i.op = Op::mov;
        if ((w & 255) == 255)
          i.length = 2;
        else if (!scalarSource(w & 255))
          return fail(pc, "unsupported scalar source");
      } else if ((w & 0xff800000U) == 0xbf800000U) {
        const auto op = (w >> 16) & 127;
        if (op == 1 && !(w & 65535)) {
          i.op = Op::end;
          ended = true;
        } else if (op == 8) {
          i.op = Op::branchExecZero;
          const int target = int(pc) + 1 + int(int16_t(w));
          if (target <= int(pc))
            return fail(pc, "backward branch unsupported");
          i.target = size_t(target);
        } else if (op == 12 && !(w & 0xf080))
          i.op = Op::wait;
        else
          return fail(pc, "unsupported SOPP (including barrier)");
      } else if ((w & 0xf8000000U) == 0xc0000000U) {
        if (((w >> 22) & 31) != 8 || !(w & 256) || ((w >> 9) & 63) * 2 > 100 ||
            !destination((w >> 15) & 127))
          return fail(pc, "unsupported SMRD");
        i.op = Op::scalarLoad;
      } else if ((w & 0xfc000000U) == 0xe0000000U) {
        const auto op = (w >> 18) & 127;
        if ((op != 0 && op != 4) ||
            (w & ~(0xfc000000U | 0x01fc0000U | 0x2000U | 0xfffU)))
          return fail(pc, "unsupported MUBUF flags/opcode");
        i.op = op == 0 ? Op::load : Op::store;
        i.length = 2;
      } else if ((w & 0xfc000000U) == 0xd0000000U) {
        if (((w >> 17) & 511) != 212 || (w & 0x1ff80U) ||
            !destination(w & 127) || (w & 127) == 103 || (w & 127) == 107)
          return fail(pc, "unsupported VOP3");
        i.op = Op::compare;
        i.length = 2;
      } else if ((w & 0xfe000000U) == 0x7c000000U) {
        if (((w >> 17) & 255) != 212 || !vectorSource(w & 511))
          return fail(pc, "unsupported VOPC");
        i.op = Op::compare;
      } else if (!(w & 0x80000000U)) {
        const auto op = w >> 25;
        if ((op != 37 && op != 27) || !vectorSource(w & 511))
          return fail(pc, "unsupported VOP2");
        i.op = op == 37 ? Op::add : Op::bitAnd;
      } else if ((w & 0xc0000000U) == 0x80000000U) {
        if (((w >> 23) & 127) != 38 || !destination((w >> 16) & 127) ||
            !scalarSource(w & 255) || !scalarSource((w >> 8) & 255))
          return fail(pc, "unsupported SOP2");
        i.op = Op::mul;
      } else
        return fail(pc, "unsupported encoding");
      if (i.length > code.size() - pc)
        return fail(pc, "truncated instruction/literal");
      if (i.length == 2)
        i.extra = code[pc + 1];
      if (i.op == Op::compare && i.length == 2 &&
          ((i.extra >> 18) || !vectorSource(i.extra & 511) ||
           !vectorSource((i.extra >> 9) & 511)))
        return fail(pc, "unsupported VOP3 modifiers/sources");
      if ((i.op == Op::load || i.op == Op::store) &&
          ((i.extra & 0x03e00000U) || !scalarSource(i.extra >> 24)))
        return fail(pc, "unsupported MUBUF addressing/status");
      out.push_back(i);
      pc += i.length;
      if (ended)
        break;
    }
    if (!ended)
      return fail(code.size(), "missing s_endpgm");
    for (auto &i : out)
      if (i.op == Op::branchExecZero) {
        const auto at =
            std::find_if(out.begin(), out.end(), [&](const Instruction &j) {
              return j.pc == i.target;
            });
        if (at == out.end())
          return fail(i.pc, "branch target not an instruction");
        i.target = size_t(at - out.begin());
      }
    return true;
  }
  template <class Memory>
  bool dispatch(Memory &memory, const std::array<uint32_t, 1024> &sh,
                const std::array<uint32_t, 3> &groups, uint32_t initiator,
                std::string &error) {
    stats = {};
    const uint64_t program =
        (uint64_t(sh[0x20c]) << 8) | (uint64_t(sh[0x20d]) << 40);
    auto fail = [&](const std::string &why) {
      std::ostringstream s;
      s << "GCN compute program=0x" << std::hex << program << ": " << why;
      error = s.str();
      return false;
    };
    // Start at zero, one wave per group. Reject hidden state rather than
    // guessing scratch/LDS, dispatch offsets, trap or shared-register ABI.
    if (initiator != 1 || !program || sh[0x204] || sh[0x205] || sh[0x206])
      return fail("unsupported initiator/program/start offset");
    const auto r2 = sh[0x213];
    const unsigned users = (r2 >> 1) & 31, tidComponents = (r2 >> 11) & 3;
    if (users > 16 || tidComponents > 2 || (r2 & ~0x1bbeU))
      return fail("unsupported scratch/LDS/trap/group-size resources");
    const unsigned vgprs = ((sh[0x212] & 63) + 1) * 4,
                   sgprs = (((sh[0x212] >> 6) & 15) + 1) * 8;
    if (sgprs > 104 || vgprs > 256 || (sh[0x212] & 0xfc000000U))
      return fail("unsupported register resources");
    uint64_t threads = 1, totalGroups = 1;
    std::array<uint32_t, 3> local{sh[0x207], sh[0x208], sh[0x209]};
    for (unsigned d = 0; d < 3; ++d) {
      if (!groups[d] || !local[d] || local[d] > 64 || threads > 64 / local[d])
        return fail("invalid local/group dimensions (one wave maximum)");
      threads *= local[d];
      if (totalGroups > maxThreads / groups[d])
        return fail("dispatch group budget");
      totalGroups *= groups[d];
      if (groups[d] > 1 && !(r2 & (1U << (7 + d))))
        return fail("missing workgroup ID input");
      if (local[d] > 1 && tidComponents < d)
        return fail("missing local ID input");
    }
    if (totalGroups > maxThreads / threads || users + unsigned(bool(r2 & 128)) +
                                                      unsigned(bool(r2 & 256)) +
                                                      unsigned(bool(r2 & 512)) >
                                                  sgprs)
      return fail("dispatch thread/register budget");
    std::vector<uint32_t> code;
    std::vector<Instruction> instructions;
    // Read an owned code snapshot; decoder determines instruction length.
    for (size_t n = 0; n < maxCodeWords; ++n) {
      uint32_t w = 0;
      if (memory.read_data(program + n * 4, &w, 4))
        return fail("instruction memory unreadable");
      code.push_back(w);
      // An end word in a literal is not a terminator: decode must succeed.
      if (w == 0xbf810000U) {
        std::string ignored;
        if (decode(code, instructions, ignored))
          break;
      }
    }
    if (!decode(code, instructions, error))
      return fail(error);
    for (uint32_t z = 0; z < groups[2]; ++z)
      for (uint32_t y = 0; y < groups[1]; ++y)
        for (uint32_t x = 0; x < groups[0]; ++x) {
          std::array<uint32_t, 128> s{};
          std::array<bool, 128> defined{};
          std::array<std::array<uint32_t, 64>, 256> v{};
          std::array<uint64_t, 256> vd{};
          const uint64_t live =
              threads == 64 ? ~uint64_t(0) : (uint64_t(1) << threads) - 1;
          uint64_t exec = live;
          for (unsigned j = 0; j < users; ++j) {
            s[j] = sh[0x240 + j];
            defined[j] = true;
          }
          unsigned next = users;
          const uint32_t ids[] = {x, y, z};
          for (unsigned d = 0; d < 3; ++d)
            if (r2 & (1U << (7 + d))) {
              s[next] = ids[d];
              defined[next++] = true;
            }
          for (unsigned lane = 0; lane < threads; ++lane) {
            v[0][lane] = lane % local[0];
            v[1][lane] = (lane / local[0]) % local[1];
            v[2][lane] = lane / (local[0] * local[1]);
          }
          for (unsigned j = 0; j <= tidComponents; ++j)
            vd[j] = live;
          bool valid = true;
          std::string reason;
          auto scalar = [&](unsigned reg) -> uint32_t {
            if (reg >= 128 && reg <= 192)
              return reg - 128;
            if (reg >= 193 && reg <= 208)
              return uint32_t(192 - int(reg));
            if (!destination(reg) || (reg <= 103 && reg >= sgprs) ||
                !defined[reg]) {
              valid = false;
              reason = "undefined/out-of-range SGPR " + std::to_string(reg);
              return 0;
            }
            return s[reg];
          };
          auto source = [&](unsigned reg, unsigned lane) -> uint32_t {
            if (reg < 256)
              return scalar(reg);
            reg -= 256;
            if (reg >= vgprs || !(vd[reg] & (uint64_t(1) << lane))) {
              valid = false;
              reason = "undefined/out-of-range VGPR " + std::to_string(reg);
              return 0;
            }
            return v[reg][lane];
          };
          auto putScalar = [&](unsigned reg, uint32_t value) {
            if (!destination(reg) || (reg <= 103 && reg >= sgprs)) {
              valid = false;
              reason = "SGPR destination out of range";
              return;
            }
            s[reg] = value;
            defined[reg] = true;
          };
          for (size_t ip = 0; ip < instructions.size();) {
            const auto &i = instructions[ip++];
            const auto w = i.word;
            auto at = [&](const std::string &why) {
              std::ostringstream a;
              a << "pc=0x" << std::hex << program + i.pc * 4
                << " group=" << std::dec << x << ',' << y << ',' << z << ": "
                << why;
              return fail(a.str());
            };
            if (++stats.instructions > maxSteps)
              return at("instruction budget");
            if (i.op == Op::end)
              break;
            if (i.op == Op::wait) {
              std::atomic_thread_fence(std::memory_order_seq_cst);
              continue;
            }
            if (i.op == Op::branchExecZero) {
              if (!exec)
                ip = i.target;
              continue;
            }
            if (i.op == Op::mov)
              putScalar((w >> 16) & 127,
                        i.length == 2 ? i.extra : scalar(w & 255));
            else if (i.op == Op::mul) {
              const auto a = scalar(w & 255), b = scalar((w >> 8) & 255);
              putScalar((w >> 16) & 127, a * b);
            } else if (i.op == Op::scalarLoad) {
              const unsigned base = ((w >> 9) & 63) * 2;
              const auto lo = scalar(base), hi = scalar(base + 1),
                         records = scalar(base + 2);
              const uint64_t offset = uint64_t(w & 255) * 4,
                             stride = (hi >> 16) & 0x3fff;
              if (!valid)
                return at(reason);
              if (hi & 0x80000000U)
                return at("scalar descriptor swizzle unsupported");
              if (offset + 4 > (stride ? stride * records : records))
                return at("scalar buffer out of bounds");
              const uint64_t address =
                  ((uint64_t(hi & 65535) << 32) | lo) + offset;
              uint32_t value = 0;
              if ((address & 3) || memory.read_data(address, &value, 4))
                return at("scalar buffer unreadable/alignment");
              if (stats.groups == 0) {
                if (stats.scalarReads < stats.scalarAddress.size()) {
                  stats.scalarAddress[stats.scalarReads] = address;
                  stats.scalarValue[stats.scalarReads] = value;
                  ++stats.scalarReads;
                }
              }
              ++stats.loads;
              putScalar((w >> 15) & 127, value);
            } else if (i.op == Op::add || i.op == Op::bitAnd) {
              const unsigned dst = (w >> 17) & 255;
              uint64_t carry = 0;
              if (dst >= vgprs)
                return at("VGPR destination out of range");
              for (unsigned lane = 0; lane < threads; ++lane)
                if (exec & (uint64_t(1) << lane)) {
                  const auto a = source(w & 511, lane),
                             b = source(256 + ((w >> 9) & 255), lane);
                  if (!valid)
                    return at(reason);
                  const uint64_t sum = uint64_t(a) + b;
                  v[dst][lane] = i.op == Op::add ? uint32_t(sum) : (a & b);
                  vd[dst] |= uint64_t(1) << lane;
                  if (sum >> 32)
                    carry |= uint64_t(1) << lane;
                }
              if (i.op == Op::add) {
                putScalar(106, uint32_t(carry));
                putScalar(107, uint32_t(carry >> 32));
              }
            } else if (i.op == Op::compare) {
              const unsigned src0 = i.length == 2 ? i.extra & 511 : w & 511,
                             src1 = i.length == 2 ? (i.extra >> 9) & 511
                                                  : 256 + ((w >> 9) & 255);
              uint64_t mask = 0;
              for (unsigned lane = 0; lane < threads; ++lane)
                if (exec & (uint64_t(1) << lane)) {
                  const auto a = source(src0, lane), b = source(src1, lane);
                  if (!valid)
                    return at(reason);
                  if (a > b)
                    mask |= uint64_t(1) << lane;
                }
              exec = mask;
              const unsigned dst = i.length == 2 ? w & 127 : 106;
              putScalar(dst, uint32_t(mask));
              putScalar(dst + 1, uint32_t(mask >> 32));
            } else if ((i.op == Op::load || i.op == Op::store) && exec) {
              const unsigned base = ((i.extra >> 16) & 31) * 4,
                             addrReg = i.extra & 255,
                             dataReg = (i.extra >> 8) & 255;
              const auto lo = scalar(base), hi = scalar(base + 1),
                         records = scalar(base + 2), format = scalar(base + 3),
                         soffset = scalar(i.extra >> 24);
              if (!valid)
                return at(reason);
              const uint64_t stride = (hi >> 16) & 0x3fff;
              // Linear 32-bit UINT, X channel. Cache/MTYPE/element-stride
              // hints do not change addresses in this synchronous backend.
              if ((hi & 0x80000000U) || (format & 0xc5800000U) ||
                  ((format >> 15) & 15) != 4 || ((format >> 12) & 7) != 4 ||
                  (format & 7) != 4)
                return at("unsupported formatted buffer resource (need linear "
                          "R32_UINT X)");
              if (dataReg >= vgprs)
                return at("buffer VGPR destination out of range");
              for (unsigned lane = 0; lane < threads; ++lane)
                if (exec & (uint64_t(1) << lane)) {
                  const uint64_t index =
                      (w & 0x2000) ? source(256 + addrReg, lane) : 0;
                  const uint64_t offset = uint64_t(soffset) + (w & 0xfff),
                                 relative = index * stride + offset;
                  if (!valid)
                    return at(reason);
                  if ((stride && (index >= records || offset + 4 > stride)) ||
                      (!stride && relative + 4 > records))
                    return at("formatted buffer out of bounds");
                  const uint64_t address =
                      ((uint64_t(hi & 65535) << 32) | lo) + relative;
                  if (address & 3)
                    return at("buffer address alignment unsupported");
                  if (i.op == Op::load) {
                    if (memory.read_data(address, &v[dataReg][lane], 4))
                      return at("buffer read inaccessible");
                    vd[dataReg] |= uint64_t(1) << lane;
                    ++stats.loads;
                  } else {
                    const auto value = source(256 + dataReg, lane);
                    if (!valid)
                      return at(reason);
                    if (!memory.permitted(address, 4, 6) ||
                        memory.write_data(address, &value, 4))
                      return at("buffer write inaccessible (requires RW/NX)");
                    ++stats.stores;
                    if (stats.stores == 1) {
                      stats.firstWrite = address;
                      stats.firstValue = value;
                    }
                    stats.lastWrite = address;
                    stats.lastValue = value;
                    uint32_t observed = 0;
                    if (memory.read_data(address, &observed, 4) ||
                        observed != value)
                      return at("store readback verification failed");
                    ++stats.verified;
                  }
                }
            }
            if (!valid)
              return at(reason);
          }
          ++stats.groups;
          stats.lanes += threads;
        }
    std::atomic_thread_fence(std::memory_order_seq_cst);
    return true;
  }
};
} // namespace maxps4::gpu
