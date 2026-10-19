#!/usr/bin/env python3
"""Wrap our source-built static homebrew as an unsigned, uncompressed SELF fixture.
No signing, keys, DRM processing or protected input support. Code is unchanged.
"""
from pathlib import Path
import argparse
import struct


def wrap(elf):
    if not 64 <= len(elf) <= 32*1024*1024:
        raise ValueError('fixture ELF size')
    if elf[:9] != b'\x7fELF\x02\x01\x01\x09\0' or struct.unpack_from('<HH',elf,16)!=(0xfe00,62):
        raise ValueError('only a plaintext static Orbis ELF fixture is accepted')
    phoff, = struct.unpack_from('<Q',elf,32)
    stride, count = struct.unpack_from('<HH',elf,54)
    if stride!=56 or not 0<count<=128 or phoff<64 or phoff+count*56>len(elf):
        raise ValueError('fixture program table')
    origin=32+count*32
    header_end=(origin+64+count*56+15)&~15
    out=bytearray(header_end)
    struct.pack_into('<I',out,0,0x1d3d154f)
    out[4:10]=bytes.fromhex('000101120101')
    struct.pack_into('<H',out,12,header_end)
    struct.pack_into('<HH',out,24,count,0x22)
    out[origin:origin+64]=elf[:64]
    struct.pack_into('<Q',out,origin+32,64)
    struct.pack_into('<Q',out,origin+40,0)
    struct.pack_into('<HHH',out,origin+58,0,0,0)
    for i in range(count):
        h=struct.unpack_from('<II6Q',elf,phoff+i*56)
        typ, flags, offset, va, pa, filesz, memsz, align=h
        if typ!=1 or flags not in (4,5,6) or filesz>memsz or offset>len(elf) or filesz>len(elf)-offset:
            raise ValueError('fixture needs bounded PT_LOAD R/RX/RW only')
        out[origin+64+i*56:origin+64+(i+1)*56]=elf[phoff+i*56:phoff+(i+1)*56]
        padding=(-len(out))%16
        out.extend(bytes(padding)); source=len(out)
        if source+filesz>32*1024*1024:
            raise ValueError('fixture SELF budget')
        out.extend(elf[offset:offset+filesz])
        struct.pack_into('<4Q',out,32+i*32,0x800|(i<<20),source,filesz,filesz)
    struct.pack_into('<I',out,16,len(out))
    return out


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf',type=Path); parser.add_argument('output',type=Path)
    args=parser.parse_args()
    if args.elf.resolve()==args.output.resolve():
        raise SystemExit('refusing to overwrite the source fixture')
    args.output.write_bytes(wrap(args.elf.read_bytes()))
