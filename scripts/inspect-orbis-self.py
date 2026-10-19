#!/usr/bin/env python3
"""Read-only SELF/ELF metadata inventory. No decryption, extraction or execution.

SELF flags/layout reference: shadPS4 elf.h at
443f022450f6857256e780daa3d5a14029a748f7. This independent diagnostic never
relaxes the production loader. Clear block ranges are used only for metadata.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import struct

LIMIT = 32 * 1024 * 1024


def inspect(b):
    def require(ok, reason):
        if not ok:
            raise ValueError(reason)

    def span(offset, size):
        require(0 <= offset <= len(b) and 0 <= size <= len(b) - offset, 'file range out of bounds')
        return b[offset:offset + size]

    def u(offset, fmt):
        return struct.unpack(fmt, span(offset, struct.calcsize(fmt)))

    require(64 <= len(b) <= LIMIT, 'file size outside diagnostic budget')
    result = dict(size=len(b), sha256=hashlib.sha256(b).hexdigest(), cpu_executed=False,
                  guest_mapped=False, signature_verified=False, execution_admitted=False)
    is_self = b[:4] == bytes.fromhex('4f153d1d')
    origin = 0
    blocks = []
    if is_self:
        require(b[4:10] == bytes.fromhex('000101120101'), 'unsupported SELF header identity')
        header_size, meta_size, declared_size = u(12, '<HHI')
        count, = u(24, '<H')
        require(0 < count <= 128, 'SELF block count invalid')
        origin = 32 + count * 32
        require(header_size >= origin + 64 and header_size + meta_size <= len(b), 'SELF header/meta bounds')
        require(header_size + meta_size <= declared_size <= len(b), 'SELF declared size outside file')
        for i in range(count):
            flags, offset, size, decoded = u(32 + i * 32, '<4Q')
            require(offset >= header_size + meta_size and offset <= declared_size and size <= declared_size - offset,
                    'SELF block outside declared payload')
            blocks.append(dict(index=i, flags=hex(flags), offset=offset, size=size, decoded_size=decoded,
                               encrypted=bool(flags & 2), signed=bool(flags & 4), compressed=bool(flags & 8),
                               blocked=bool(flags & 0x800),
                               program_header_id=((flags >> 20) & 0xfff) if flags & 0x800 else None,
                               associated_descriptor=((flags >> 20) & 0xfff) if flags & 0xfffff == 0x10004 else None,
                               signature_verified=False))
        result.update(format='SELF', elf_origin=origin, header_size=header_size, meta_size=meta_size,
                      declared_size=declared_size, trailing_bytes=len(b) - declared_size, blocks=blocks)
    else:
        result['format'] = 'ELF'
    ident = span(origin, 16)
    require(ident == b'\x7fELF\x02\x01\x01\x09' + bytes(8), 'unsupported ELF identity')
    etype, machine, version, entry, phoff, shoff, flags, ehsize, phentsize, phnum, shentsize, shnum, shstr = u(origin+16, '<HHIQQQIHHHHHH')
    require(machine == 62 and version == 1 and flags == 0 and ehsize == 64 and phentsize == 56 and 0 < phnum <= 128,
            'unsupported ELF header')
    require(etype in (0xfe00, 0xfe10, 0xfe18), 'unsupported Orbis ELF type')
    require(phoff >= 64, 'invalid program table offset')
    if is_self:
        require(origin + phoff + phnum * 56 <= header_size, 'program table outside SELF header')
    if shnum:
        require(shentsize == 64 and shnum <= 4096 and shstr < shnum, 'invalid section table')
        span(origin + shoff, shnum * shentsize)
    else:
        require(shoff == 0 and shstr == 0, 'inconsistent absent section table')
    headers = []
    for i in range(phnum):
        typ, perm, offset, address, paddr, filesz, memsz, align = u(origin + phoff + i * 56, '<II6Q')
        require(offset <= (1 << 64)-1-filesz and address <= (1 << 64)-1-memsz, 'ELF range overflow')
        if typ in (1, 0x61000010):
            require(filesz <= memsz and memsz <= LIMIT and perm & 4 and not perm & ~7,
                    'invalid load segment')
            require(align <= 1 or (align & (align - 1) == 0 and address % align == offset % align), 'ELF alignment')
        headers.append(dict(index=i, type=hex(typ), permissions=perm, offset=offset, address=address,
                            file_size=filesz, memory_size=memsz, alignment=align))
    result.update(elf_type=hex(etype), machine=machine, entry=entry, headers=headers)
    for block in blocks:
        if block['associated_descriptor'] is not None and not block['encrypted'] and not block['compressed']:
            block['digest_bytes_all_zero'] = not any(span(block['offset'], block['size']))
    for block in blocks:
        if block['blocked']:
            require(block['program_header_id'] < phnum, 'SELF block ID outside program table')
    loads = [h for h in headers if h['type'] in ('0x1', '0x61000010')]
    require(sum(h['memory_size'] for h in loads) <= LIMIT, 'total load memory budget')
    for i, h in enumerate(loads):
        for other in loads[:i]:
            require(h['address'] + h['memory_size'] <= other['address'] or
                    other['address'] + other['memory_size'] <= h['address'], 'overlapping load segments')
    result['entry_in_rx'] = any(h['permissions'] & 5 == 5 and not h['permissions'] & 2 and
                               h['address'] <= entry < h['address'] + h['file_size'] for h in loads)

    def clear_read(offset, size):
        # Metadata only. Never return bytes from an encrypted/compressed block.
        if not is_self:
            return span(offset, size)
        matches = []
        for block in blocks:
            if not block['blocked']:
                continue
            h = headers[block['program_header_id']]
            if h['offset'] <= offset and offset-h['offset'] <= h['file_size'] and size <= h['file_size']-(offset-h['offset']):
                matches.append((block, h))
        require(len(matches) == 1, 'metadata range has missing/ambiguous SELF block')
        block, h = matches[0]
        require(not block['encrypted'], 'encrypted metadata block refused')
        require(not block['compressed'], 'compressed metadata block refused')
        require(block['size'] == block['decoded_size'] == h['file_size'], 'SELF block size mismatch')
        return span(block['offset'] + offset - h['offset'], size)

    result['load_plan'] = []
    for h in loads:
        matches = [x for x in blocks if x['blocked'] and x['program_header_id'] == h['index']]
        readable = (not is_self) or (len(matches) == 1 and not matches[0]['encrypted'] and not matches[0]['compressed'] and
                    matches[0]['size'] == matches[0]['decoded_size'] == h['file_size'])
        result['load_plan'].append(dict(program_header=h['index'], guest_relative=h['address'],
            file_size=h['file_size'], memory_size=h['memory_size'], permissions=h['permissions'],
            declared_clear_direct_bytes=readable, wx_refused=bool(h['permissions'] & 1 and h['permissions'] & 2),
            instantiated=False))
    # Protected inputs stop before dynamic/string/symbol/relocation payload reads.
    if any(x['encrypted'] or x['compressed'] for x in blocks):
        result['metadata_status'] = 'blocked: encrypted/compressed SELF block; headers only'
        return result
    dyn = [h for h in headers if h['type'] == '0x2']
    sce = [h for h in headers if h['type'] == '0x61000000']
    if not dyn and not sce:
        result['metadata_status'] = 'no dynamic metadata'
        return result
    require(len(dyn) == len(sce) == 1, 'missing/duplicate dynamic tables')
    dynamic = clear_read(dyn[0]['offset'], dyn[0]['file_size'])
    require(len(dynamic) % 16 == 0 and len(dynamic) <= 65536, 'dynamic table budget')
    tags = {}; repeated = []; terminated = False
    for offset in range(0, len(dynamic), 16):
        tag, value = struct.unpack_from('<QQ', dynamic, offset)
        if tag == 0:
            terminated = True; break
        if tag in (1, 0x6100000d, 0x6100000f, 0x61000013, 0x61000015, 0x61000007, 0x61000017, 0x61000019):
            repeated.append((tag, value))
        else:
            require(tag not in tags, 'duplicate singleton dynamic tag')
            tags[tag] = value
    require(terminated, 'unterminated dynamic table')
    result['dynamic_tags'] = {hex(k): hex(v) for k, v in tags.items()}

    def table(offset, size):
        require(offset <= sce[0]['file_size'] and size <= sce[0]['file_size'] - offset, 'SCE table out of bounds')
        return clear_read(sce[0]['offset'] + offset, size)

    strings = table(tags[0x61000035], tags[0x61000037])
    require(strings and strings[0] == 0 and len(strings) <= 1024*1024, 'string table budget')

    def string(index):
        require(index < len(strings), 'string index out of bounds')
        end = strings.find(b'\0', index, min(index+1024, len(strings)))
        require(end >= index, 'unterminated string')
        return strings[index:end].decode('ascii', errors='backslashreplace')

    result['dependencies'] = [string(v) for t, v in repeated if t == 1]
    result['identities'] = [dict(tag=hex(t), name=string(v & 0xffffffff), id=v >> 48, version=(v >> 32) & 0xffff)
                            for t, v in repeated if t in (0x6100000d, 0x6100000f, 0x61000013, 0x61000015)]
    symbols = table(tags[0x61000039], tags[0x6100003f])
    require(symbols[:24] == bytes(24), 'missing null symbol')
    require(tags[0x6100003b] == 24 and len(symbols) % 24 == 0 and len(symbols) // 24 <= 65536, 'symbol table budget/encoding')
    imports = []; exports = 0
    for offset in range(24, len(symbols), 24):
        name, info, visibility, section, value, size = struct.unpack_from('<IBBHQQ', symbols, offset)
        if section == 0 and name:
            imports.append(dict(symbol_index=offset//24, name=string(name), kind=info & 15))
        elif section != 0:
            exports += 1
    result.update(imports=imports, defined_symbols=exports)
    relocations = {}; count = 0
    for offset_tag, size_tag in ((0x6100002f, 0x61000031), (0x61000029, 0x6100002d)):
        if size_tag not in tags:
            continue
        encoding_tag, expected = (0x61000033, 24) if offset_tag == 0x6100002f else (0x6100002b, 7)
        require(tags.get(encoding_tag) == expected, 'unsupported relocation table encoding')
        data = table(tags[offset_tag], tags[size_tag])
        require(len(data) % 24 == 0 and len(data)//24 <= 65536, 'relocation budget/size')
        for offset in range(0, len(data), 24):
            target, info, addend = struct.unpack_from('<QQq', data, offset)
            kind = info & 0xffffffff
            relocations[str(kind)] = relocations.get(str(kind), 0) + 1
            count += 1
    result.update(metadata_status='inventoried from declared clear blocks; not linked',
                  relocation_count=count, relocation_types=relocations,
                  init=hex(tags.get(12, 0)), fini=hex(tags.get(13, 0)))
    return result


def inspect_file(path):
    # Fail before reading a FIFO, device or symlink. Do not alter source files.
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        before = os.fstat(fd)
        if not stat.S_ISREG(before.st_mode) or not 64 <= before.st_size <= LIMIT:
            raise ValueError('regular file within size budget required')
        with os.fdopen(fd, 'rb') as source:
            fd = -1  # File object now owns and closes the descriptor.
            b = source.read(LIMIT + 1)
            after = os.fstat(source.fileno())
            if (before.st_size, before.st_mtime_ns, before.st_ino) != (after.st_size, after.st_mtime_ns, after.st_ino):
                raise ValueError('source changed during snapshot')
            return inspect(b)
    finally:
        if fd >= 0:
            os.close(fd)



if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('files', nargs='+', type=Path)
    args = parser.parse_args()
    reports = []
    for path in args.files:
        try:
            reports.append(dict(path=str(path), **inspect_file(path)))
        except (OSError, ValueError, KeyError, struct.error) as error:
            reports.append(dict(path=str(path), error=str(error), execution_admitted=False, cpu_executed=False))
    print(json.dumps(reports, ensure_ascii=False, indent=2))
    raise SystemExit(int(any('error' in report for report in reports)))
