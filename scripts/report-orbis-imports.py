#!/usr/bin/env python3
"""Join an explicit import catalogue to a metadata-only V4 preflight log.

Engineering categories are descriptive, never linker resolvers or execution
admission. Missing exports and implemented refusal paths remain distinct.
"""
import argparse
import collections
import csv
import json
import pathlib
import re

CATEGORIES = (
    'loading-initialization', 'memory-threads', 'files-storage', 'audio',
    'graphics', 'network-psn', 'system-other',
)
IDENTITY = ('file', 'nid', 'library', 'module', 'library_version', 'module_version', 'type')
TOKEN = re.compile(r'([\w-]+)=("(?:[^"\\]|\\.)*"|[^\s]+)')


def fields(text):
    result = {}
    end = 0
    for match in TOKEN.finditer(text):
        if text[end:match.start()].strip():
            raise ValueError('unparsed diagnostic field')
        key, value = match.groups()
        if key in result:
            raise ValueError('duplicate diagnostic field')
        result[key] = json.loads(value) if value.startswith('"') else value
        end = match.end()
    if text[end:].strip():
        raise ValueError('unparsed diagnostic suffix')
    return result


def category(row):
    lib, name = row['library'], row['name']
    # Domain first: e.g. trophy creation is PSN-related, not process startup.
    if lib == 'libSceAudioOut':
        return 'audio'
    if lib in ('libSceGnmDriver', 'libSceVideoOut', 'libScePngDec'):
        return 'graphics'
    if lib.startswith('libSceNp'):
        return 'network-psn'
    if lib in ('libSceSaveData', 'libSceSaveDataDialog', 'libSceDiscMap', 'libSceFios2'):
        return 'files-storage'
    if lib == 'libSceSysmodule' or name in {
        '__start__Ztext', '__stop__Ztext', 'module_start', 'module_stop',
        '__cxa_finalize', '__pthread_cxa_finalize', 'Need_sceLibcInternal',
        '__elf_phdr_match_addr', '_sceKernelRtldSetApplicationHeapAPI', '_exit',
        'sceKernelGetProcParam', 'sceKernelGetModuleInfoForUnwind',
        'sceKernelGetModuleInfoFromAddr', 'sceKernelInternalMemoryGetModuleSegmentInfo',
        'sceUserServiceInitialize', 'sceUserServiceTerminate',
    }:
        return 'loading-initialization'
    if (lib == 'libScePosix' or name.startswith(('scePthread', 'pthread_', '__tls_', '__stack_chk', 'sceLibcHeap'))
            or name in {'malloc', 'calloc', 'realloc', 'free', 'memcpy', 'memset', 'memmove',
                        'memcmp', 'mmap', 'munmap', 'msync', 'getpagesize', 'sceKernelMlock',
                        'sceKernelAllocateDirectMemory', 'sceKernelReleaseDirectMemory',
                        'sceKernelGetDirectMemorySize', 'sceKernelMapDirectMemory',
                        'sceKernelMapNamedFlexibleMemory', 'sceKernelMunmap'}):
        return 'memory-threads'
    if name in {
        '_Stdout', 'printf', 'vprintf', 'vsnprintf', 'fflush', '_read', '_write',
        'sceKernelOpen', 'sceKernelClose', 'sceKernelRead', 'sceKernelWrite',
        'sceKernelPread', 'sceKernelPwrite', 'sceKernelLseek', 'sceKernelStat',
        'sceKernelFstat', 'sceKernelFsync', 'sceKernelTruncate', 'sceKernelFtruncate',
        'sceKernelGetdirentries', 'sceKernelMkdir', 'sceKernelRmdir', 'sceKernelRename',
        'sceKernelUnlink',
    }:
        return 'files-storage'
    return 'system-other'


def identity(row):
    return tuple(str(row[key]) for key in IDENTITY)


def analyse(catalog, log):
    if not catalog or len(catalog) > 65536 or len(log) > 16 * 1024 * 1024:
        raise ValueError('report budget')
    imports, missing, providers, summaries = {}, [], [], []
    for line in log.splitlines():
        if line.startswith('import '):
            record = fields(line[7:])
            record['file'] = record['consumer']
            if record.get('execution-observed') != 'false':
                raise ValueError('log is not an unexecuted game preflight')
            key = identity(record)
            if key in imports:
                raise ValueError('duplicate import identity in preflight')
            imports[key] = record
        elif line.startswith('missing-library='):
            missing.append(fields(line)['missing-library'])
        elif line.startswith('provider='):
            providers.append(fields(line))
        elif line.startswith('[ORBIS.GLOBAL] missing='):
            summaries.append(fields(line[len('[ORBIS.GLOBAL] '):]))
    if len(summaries) != 1:
        raise ValueError('expected one complete global diagnostic summary')
    summary = summaries[0]
    if summary.get('admission') != 'false':
        raise ValueError('metadata-only admission flag required')
    if len(imports) != len(catalog) or len({identity(row) for row in catalog}) != len(catalog):
        raise ValueError('catalog/preflight cardinality mismatch')
    output = []
    stats = {key: collections.Counter() for key in CATEGORIES}
    for row in catalog:
        record = imports[identity(row)]
        if row.get('game_execution') != 'not-observed':
            raise ValueError('catalog must not claim game execution')
        before, after = row['metadata_status'], record['status']
        behavior = record.get('behavior', 'no-export')
        item = dict(row, before_metadata_status=before, metadata_status=after,
                    behavior=behavior, category=category(row), game_execution='not-observed')
        output.append(item)
        group = stats[item['category']]
        group['declared'] += 1
        group['unresolved_before'] += before != 'metadata-match'
        group['unresolved_after'] += after != 'metadata-match'
        if after == 'metadata-match':
            group[behavior] += 1
    unresolved = sum(row['metadata_status'] != 'metadata-match' for row in output)
    if (unresolved != int(summary['unresolved']) or len(output) - unresolved != int(summary['metadata_matches'])
            or len(set(missing)) != int(summary['missing'])):
        raise ValueError('global summary disagrees with enumerated diagnostics')
    graphics = [row for row in output if row['category'] == 'graphics']
    report = {
        'scope': 'supplied import identities; engineering classification; not runtime admission',
        'game_execution': 'not-observed', 'admission': False,
        'counts': {key: int(value) for key, value in summary.items() if key != 'admission'},
        'unresolved_before': sum(row['before_metadata_status'] != 'metadata-match' for row in output),
        'missing_dependencies': sorted(set(missing)), 'providers': providers,
        'categories': {key: dict(stats[key]) for key in CATEGORIES},
        'graphics_missing_imports': [row for row in graphics if row['metadata_status'] != 'metadata-match'],
        'graphics_explicit_refusals': [row for row in graphics if row['behavior'] == 'explicit-refusal'],
        'graphics_partial_profiles': [row for row in graphics if row['behavior'] == 'partial-profile'],
        'graphics_call_necessity': 'Required by current linker metadata; actual game calls not observed.',
    }
    return output, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('catalog', type=pathlib.Path)
    parser.add_argument('preflight', type=pathlib.Path)
    parser.add_argument('--tsv', required=True, type=pathlib.Path)
    parser.add_argument('--json', required=True, type=pathlib.Path)
    args = parser.parse_args()
    if args.catalog.stat().st_size > 16 * 1024 * 1024 or args.preflight.stat().st_size > 16 * 1024 * 1024:
        raise ValueError('input budget')
    sources = {args.catalog.resolve(), args.preflight.resolve()}
    if args.tsv.resolve() in sources or args.json.resolve() in sources or args.tsv.resolve() == args.json.resolve():
        raise ValueError('outputs must not overwrite inputs or each other')
    with args.catalog.open() as stream:
        catalog = list(csv.DictReader(stream, delimiter='\t'))
    output, report = analyse(catalog, args.preflight.read_text())
    with args.tsv.open('w') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(output[0]), delimiter='\t', lineterminator='\n')
        writer.writeheader()
        writer.writerows(output)
    args.json.write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n')
    print(json.dumps({'missing': report['counts']['missing'], 'unresolved': report['counts']['unresolved'],
                      'unresolved_before': report['unresolved_before'], 'admission': False}))


if __name__ == '__main__':
    main()
