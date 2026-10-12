#!/usr/bin/env python3
"""Propagate real dispatcher allocation failure before emitting ARM64 code."""
from pathlib import Path
import sys

TAG = '// MaxPS4 checked dispatcher allocation'
PATCHES = {
    'FEXCore/Source/Interface/Core/Dispatcher/Dispatcher.h': (
        ('Dispatcher(FEXCore::Context::ContextImpl* ctx);',
         'Dispatcher(FEXCore::Context::ContextImpl* ctx, void* Buffer);'),),
    'FEXCore/Source/Interface/Core/Dispatcher/Dispatcher.cpp': (
        ('Dispatcher::Dispatcher(FEXCore::Context::ContextImpl* ctx)\n  : Arm64Emitter(ctx, FEXCore::Allocator::VirtualAlloc(MAX_DISPATCHER_CODE_SIZE, true), MAX_DISPATCHER_CODE_SIZE)',
         'Dispatcher::Dispatcher(FEXCore::Context::ContextImpl* ctx, void* Buffer)\n  : Arm64Emitter(ctx, Buffer, MAX_DISPATCHER_CODE_SIZE)'),
        ('  return fextl::make_unique<Dispatcher>(CTX);',
         '''  void* Buffer = FEXCore::Allocator::VirtualAlloc(MAX_DISPATCHER_CODE_SIZE, true);
  if (!Buffer || Buffer == reinterpret_cast<void*>(-1)) return {};
  return fextl::make_unique<Dispatcher>(CTX, Buffer);'''),),
    'FEXCore/Source/Interface/Core/Core.cpp': (
        ('  Dispatcher = FEXCore::CPU::Dispatcher::Create(this);',
         '''  Dispatcher = FEXCore::CPU::Dispatcher::Create(this);
  if (!Dispatcher) return false;'''),),
}


def adapt(root):
    updates = {}
    for relative, replacements in PATCHES.items():
        path = root / relative
        text = path.read_text()
        if TAG in text:
            if text.count(TAG) != 1 or any(text.count(new) != 1 for _, new in replacements):
                raise ValueError(f'Patched dispatcher context diverged: {relative}')
            continue
        for old, new in replacements:
            if text.count(old) != 1:
                raise ValueError(f'Unexpected dispatcher context: {relative}')
            text = text.replace(old, new, 1)
        updates[path] = TAG + '\n' + text
    for path, text in updates.items():
        path.write_text(text)


if __name__ == '__main__':
    adapt(Path(sys.argv[1]))
