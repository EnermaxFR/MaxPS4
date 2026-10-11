#!/usr/bin/env python3
"""Compare real pinned generic and AetherPS4-bundled Darwin declarations."""
import argparse
import json
from pathlib import Path
import re

CONTRACTS = {
    'ScopedJITWriteProtect': ('FEXCore/include/FEXCore/Utils/AllocatorHooks.h', r'class\s+ScopedJITWriteProtect\b'),
    'GetWritableAddress': ('FEXCore/include/FEXCore/Utils/AllocatorHooks.h', r'void\s*\*\s*GetWritableAddress\s*\('),
    'GetExecutableAddress': ('FEXCore/include/FEXCore/Utils/AllocatorHooks.h', r'void\s*\*\s*GetExecutableAddress\s*\('),
    'OnBufferReusedInPlace': ('FEXCore/Source/Interface/Context/Context.h', r'\bOnBufferReusedInPlace\s*;'),
    'BeginBufferInvalidationSafepoint': ('FEXCore/Source/Interface/Context/Context.h', r'\bBeginBufferInvalidationSafepoint\s*;'),
    'EndBufferInvalidationSafepoint': ('FEXCore/Source/Interface/Context/Context.h', r'\bEndBufferInvalidationSafepoint\s*;'),
}


def declaration(text, pattern):
    # Comments must not satisfy a contract. Preserve newlines for evidence.
    clean = re.sub(r'/\*.*?\*/|//[^\n]*', lambda m: '\n' * m.group().count('\n'), text, flags=re.S)
    match = re.search(pattern, clean)
    return None if match is None else clean[:match.start()].count('\n') + 1


def audit(generic, aether):
    darwin = aether / 'runtime/sources/fexcore-darwin'
    caller = aether / 'src/core/fex/fex_guest_engine.cpp'
    calls = caller.read_text()
    rows = []
    for name, (relative, pattern) in CONTRACTS.items():
        caller_line = declaration(calls, r'\b' + name + r'\b')
        bundled_line = declaration((darwin / relative).read_text(), pattern)
        generic_line = declaration((generic / relative).read_text(), pattern)
        if caller_line is None or bundled_line is None:
            raise ValueError(f'Pinned AetherPS4 contract changed: {name}')
        rows.append({'contract': name, 'aether_use': f'{caller.relative_to(aether)}:{caller_line}',
                     'bundled_declaration': f'{relative}:{bundled_line}',
                     'generic_declaration': None if generic_line is None else f'{relative}:{generic_line}'})
    return {'contracts': rows, 'generic_compatible': all(row['generic_declaration'] for row in rows),
            'bundled_source': 'runtime/sources/fexcore-darwin',
            'abi_compilation_required': True, 'runtime_verified': False,
            'note': 'Use the complete matching Darwin source/header set; do not add identity address translations or empty safepoints.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('generic', type=Path)
    parser.add_argument('aether', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result = audit(args.generic, args.aether)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(f"AetherPS4 Darwin contracts: {len(result['contracts'])}; generic compatible: {result['generic_compatible']}")
