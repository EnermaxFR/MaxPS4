#!/usr/bin/env python3
"""Deterministic Orbis V3 fixture autotests; no claim of CPU execution."""
import pathlib
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
GEN = ROOT / "scripts/create-orbis-test-v2.py"

def check(path: pathlib.Path) -> None:
    blob = path.read_bytes()
    assert len(blob) == 131, f"unexpected ELF size: {len(blob)}"
    assert blob[:9] == b"\x7fELF\x02\x01\x01\x09\x00"
    assert struct.unpack_from("<H", blob, 16)[0] == 0xfe00
    assert struct.unpack_from("<H", blob, 18)[0] == 62
    assert struct.unpack_from("<Q", blob, 24)[0] == 0x1000
    assert struct.unpack_from("<Q", blob, 32)[0] == 64
    assert struct.unpack_from("<H", blob, 56)[0] == 1
    assert struct.unpack_from("<I", blob, 64)[0] == 1
    assert struct.unpack_from("<I", blob, 68)[0] == 5
    assert struct.unpack_from("<Q", blob, 72)[0] == 120
    assert struct.unpack_from("<Q", blob, 80)[0] == 0x1000
    assert struct.unpack_from("<Q", blob, 96)[0] == 11
    assert struct.unpack_from("<Q", blob, 104)[0] == 32
    code = blob[120:131]
    assert code[:1] == b"\xb8" and code[5:6] == b"\x05" and code[-1:] == b"\xf4"
    expected = struct.unpack_from("<I", code, 1)[0] + struct.unpack_from("<I", code, 6)[0]
    assert expected == 42, f"fixture semantics changed: {expected}"
    # Reject corrupted magic, truncated headers, out-of-bounds segments and W+X.
    for mutant in (
        b"BAD!" + blob[4:],
        blob[:63],
        blob[:120],
        blob[:68] + b"\x07\x00\x00\x00" + blob[72:],
    ):
        assert not structurally_safe(mutant), "invalid fixture accepted"

def structurally_safe(data: bytes) -> bool:
    if len(data) < 120 or data[:4] != b"\x7fELF":
        return False
    if struct.unpack_from("<I", data, 68)[0] & 0x2 and struct.unpack_from("<I", data, 68)[0] & 0x1:
        return False
    offset = struct.unpack_from("<Q", data, 72)[0]
    size = struct.unpack_from("<Q", data, 96)[0]
    return offset <= len(data) and size <= len(data) - offset

def main() -> int:
    with tempfile.TemporaryDirectory(prefix="maxps4-orbis-autotest-") as tmp:
        path = pathlib.Path(tmp) / "fixture.elf"
        subprocess.run([sys.executable, str(GEN), str(path)], check=True)
        check(path)
    print("PASS V3 fixture structural autotests; CPU execution NOT tested")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
