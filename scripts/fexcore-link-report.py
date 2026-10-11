#!/usr/bin/env python3
"""Report real Mach-O symbol dependencies; a symbol inventory is not a full link."""
import argparse
import collections
import json
from pathlib import Path
import re
import subprocess


def parse_nm(text):
    defined, undefined = set(), set()
    # Apple nm -g: [address] type symbol. Ignore object/archive headings.
    for line in text.splitlines():
        match = re.fullmatch(r'\s*(?:[0-9a-fA-F]+\s+)?([A-Za-z?])\s+(\S+)\s*', line)
        if not match:
            continue
        kind, name = match.groups()
        if kind == 'U':
            undefined.add(name)
        elif kind in 'ABCDIRSTWV':
            defined.add(name)
    return defined, undefined


def owner(name):
    base = 'FEXCore/Source/'
    families = (
        ('FEXCore::CPU::CPUBackend', 'Interface/Core/CPUBackend.cpp'),
        ('FEXCore::CPU::Arm64JITCore::Op_', 'Interface/Core/JIT/*Ops.cpp'),
        ('FEXCore::CPU::Arm64Emitter', 'Interface/Core/ArchHelpers/Arm64Emitter.cpp'),
        ('FEXCore::LookupCache', 'Interface/Core/LookupCache.cpp'),
        ('FEXCore::GuestToHostMap', 'Interface/Core/LookupCache.cpp'),
        ('FEXCore::UncheckedLongJump', 'Utils/LongJump.cpp'),
        ('FEXCore::CPUIDEmu', 'Interface/Core/CPUID.cpp'),
        ('FEXCore::CPU::InterpreterOps', 'Interface/Core/Interpreter/Fallbacks/InterpreterFallbacks.cpp'),
        ('FEXCore::Config::', 'Interface/Config/Config.cpp'),
        ('FEXCore::Telemetry::', 'Utils/Telemetry.cpp'),
        ('FEXCore::Assert::', 'Utils/ForcedAssert.cpp'),
        ('FEXCore::Utils::SpinWaitLock', 'Utils/SpinWaitLock.cpp'),
        ('LogMan::', 'Utils/LogManager.cpp'),
        ('fextl::pmr::', 'Utils/Allocator.cpp'),
    )
    for family, source in families:
        if family in name:
            return 'FEXCore implementation', [base + source]
    if 'FEXCore::Context::' in name:
        return 'FEXCore context/runtime', [base + 'Interface/Context/Context.cpp', base + 'Interface/Core/Core.cpp']
    if 'FEXCore::Allocator::' in name:
        if re.search(r'Allocator::(?:malloc|calloc|free|realloc|aligned_alloc|aligned_free|memalign|posix_memalign|valloc)\(', name):
            return 'FEXCore libc allocation', [base + 'Utils/AllocatorHooks.cpp']
        return 'FEXCore virtual memory', [base + 'Utils/Allocator.cpp', base + 'Utils/Allocator/64BitAllocator.cpp']
    if '__clear_cache' in name:
        return 'Darwin cache portability', ['CodeEmitter/CodeEmitter/Buffer.h: sys_icache_invalidate']
    if 'fmt::' in name:
        return 'Target fmt implementation', ['fmt headers with FMT_HEADER_ONLY, or an iOS-built libfmt (not Homebrew macOS libfmt)']
    if 'FEXCore::' in name or 'fextl::' in name:
        return 'Unmapped FEXCore dependency', []
    return 'SDK/runtime import; validate with linker', ['iPhoneOS libc++, libSystem and compiler runtime']


def report(before, after, demangler):
    before_defs, before_undef = parse_nm(before)
    after_defs, after_undef = parse_nm(after)
    initial = before_undef - before_defs
    remaining = sorted(after_undef - after_defs)
    # Mach-O prepends one underscore to the C++ Itanium symbol.
    names = '\n'.join(name[1:] if name.startswith('_') else name for name in remaining) + '\n'
    readable = subprocess.run([demangler], input=names, text=True, capture_output=True, check=True).stdout.splitlines()
    if len(readable) != len(remaining):
        raise ValueError('Demangler output does not match the real symbol inventory')
    rows = []
    for symbol, name in zip(remaining, readable):
        category, candidates = owner(name)
        rows.append({'symbol': symbol, 'demangled': name, 'category': category, 'owner_candidates': candidates})
    return {
        'target': 'arm64-apple-ios16.0',
        'before_count': len(initial), 'remaining_count': len(remaining),
        'resolved': sorted(initial - set(remaining)),
        'new_dependencies': sorted(set(remaining) - initial),
        'categories': dict(collections.Counter(row['category'] for row in rows)),
        'remaining': rows,
        'complete_link_proven': False,
        'note': 'Source owners are porting candidates, not proof of link closure. See strict-link.log and strict-link-status.txt.',
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('before', type=Path)
    parser.add_argument('after', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--demangler', default='c++filt')
    args = parser.parse_args()
    result = report(args.before.read_text(), args.after.read_text(), args.demangler)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(f"Object references: {result['before_count']} before, {result['remaining_count']} after; "
          f"{len(result['resolved'])} resolved, {len(result['new_dependencies'])} new transitive dependencies")
    print('Complete link is not established by this inventory.')
