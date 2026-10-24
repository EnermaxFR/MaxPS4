#!/usr/bin/env python3
"""Give each host thread its own VIXL simulator and keep simulated atomics atomic."""
from pathlib import Path
import sys

TAG = '// MaxPS4 simulator per host thread'
PATCHES = {
    'FEXCore/Source/Interface/Core/Dispatcher/Dispatcher.cpp': (
        ('''#ifdef VIXL_SIMULATOR
void Dispatcher::ExecuteDispatch(FEXCore::Core::CpuStateFrame* Frame) {
  Simulator.WriteXRegister(0, reinterpret_cast<int64_t>(Frame));''',
         '''#ifdef VIXL_SIMULATOR
// The emitter's Simulator is one simulated CPU shared by every guest thread
// that runs this dispatcher; each host thread gets its own instead.
static vixl::aarch64::Simulator& ThreadSimulator(unsigned VectorLength, size_t StackSize) {
  struct PerThread {
    vixl::aarch64::Decoder Decoder;
    vixl::aarch64::Simulator Simulator;
    PerThread(unsigned VectorLength, size_t StackSize)
      : Simulator {&Decoder, stdout, vixl::aarch64::SimStack(StackSize).Allocate()} {
      Simulator.SetVectorLengthInBits(VectorLength);
      Simulator.DisableGCSCheck();
    }
  };
  thread_local PerThread State(VectorLength, StackSize);
  return State.Simulator;
}

void Dispatcher::ExecuteDispatch(FEXCore::Core::CpuStateFrame* Frame) {
  auto& Simulator = ThreadSimulator(this->Simulator.GetVectorLengthInBits(), SimulatorStackSize);
  Simulator.WriteXRegister(0, reinterpret_cast<int64_t>(Frame));'''),
        ('''void Dispatcher::ExecuteJITCallback(FEXCore::Core::CpuStateFrame* Frame, uint64_t RIP) {
  Simulator.WriteXRegister(0, reinterpret_cast<int64_t>(Frame));''',
         '''void Dispatcher::ExecuteJITCallback(FEXCore::Core::CpuStateFrame* Frame, uint64_t RIP) {
  auto& Simulator = ThreadSimulator(this->Simulator.GetVectorLengthInBits(), SimulatorStackSize);
  Simulator.WriteXRegister(0, reinterpret_cast<int64_t>(Frame));'''),),
    # CAS, LDADD, SWP... are a plain read then a plain write in the simulator.
    # Exclusive pairs (LDXR/STXR) keep per-simulator monitors.
    'External/vixl/src/aarch64/simulator-aarch64.cc': (
        ('#include "simulator-aarch64.h"\n',
         '''#include "simulator-aarch64.h"

#include <mutex>

static std::mutex& MaxPS4AtomicLock() {
  static std::mutex Lock;
  return Lock;
}
'''),
        ('void Simulator::VisitLoadStoreExclusive(const Instruction* instr) {\n',
         '''void Simulator::VisitLoadStoreExclusive(const Instruction* instr) {
  std::lock_guard<std::mutex> Atomic(MaxPS4AtomicLock());
'''),
        ('void Simulator::VisitAtomicMemory(const Instruction* instr) {\n',
         '''void Simulator::VisitAtomicMemory(const Instruction* instr) {
  std::lock_guard<std::mutex> Atomic(MaxPS4AtomicLock());
'''),),
}


def adapt(root):
    updates = {}
    for relative, replacements in PATCHES.items():
        path = root / relative
        text = path.read_text()
        if TAG in text:
            if text.count(TAG) != 1 or any(text.count(new) != 1 for _, new in replacements):
                raise ValueError(f'Patched simulator context diverged: {relative}')
            continue
        for old, new in replacements:
            if text.count(old) != 1:
                raise ValueError(f'Unexpected simulator context: {relative}')
            text = text.replace(old, new, 1)
        updates[path] = TAG + '\n' + text
    for path, text in updates.items():
        path.write_text(text)


if __name__ == '__main__':
    adapt(Path(sys.argv[1]))
