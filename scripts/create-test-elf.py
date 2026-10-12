"""Generate a freely redistributable static x86-64 ELF: MOV EAX,100; ADD EAX,23; HLT."""
import argparse
import struct
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("output", type=Path)
args = parser.parse_args()
code = bytes([0xB8]) + struct.pack("<I", 100) + bytes([0x05]) + struct.pack("<I", 23) + bytes([0xF4])
ident = b"\x7fELF\x02\x01\x01" + bytes(9)
header = struct.pack("<16sHHIQQQIHHHHHH", ident, 2, 62, 1, 0x1000, 64, 0, 0, 64, 56, 1, 0, 0, 0)
segment = struct.pack("<IIQQQQQQ", 1, 5, 120, 0x1000, 0, len(code), len(code), 1)
args.output.parent.mkdir(parents=True, exist_ok=True)
with args.output.open("xb") as output:
    output.write(header + segment + code)
