#!/usr/bin/env python3
"""Original legal ET_SCE_EXEC ELF: MOV EAX,40; ADD EAX,2; HLT. No SDK code."""
from pathlib import Path
import hashlib
import struct
import sys


def fixture():
    data = bytearray(131)
    data[:9] = b'\x7fELF\x02\x01\x01\x09\x00'
    struct.pack_into('<HHIQQQIHHHHHH', data, 16,
                     0xfe00, 62, 1, 0x1000, 64, 0, 0, 64, 56, 1, 0, 0, 0)
    struct.pack_into('<IIQQQQQQ', data, 64, 1, 5, 120, 0x1000, 0, 11, 32, 1)
    data[120:] = b'\xb8\x28\x00\x00\x00\x05\x02\x00\x00\x00\xf4'
    return bytes(data)


if __name__ == '__main__':
    path = Path(sys.argv[1])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(fixture())
    print('ET_SCE_EXEC, x86-64, FreeBSD ABI, RAX expected=42, SHA256=' + hashlib.sha256(fixture()).hexdigest())
    print('Generated only: no execution demonstrated.')
