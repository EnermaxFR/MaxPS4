#!/usr/bin/env python3
"""Run bounded sparse >2 GiB and >4 GiB PKG/eboot regression tests in CI."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="maxps4-sparse-") as temp:
    out = Path(temp)
    executable = out / "sparse-test"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
        "-Werror", "-pedantic", "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer", "-I", str(root / "MaxPS4-iOS/Native"),
        str(root / "MaxPS4-iOS/Native/PS4PKGBridge.cpp"),
        str(root / "MaxPS4-iOS/Tests/PKGLargeFileResearchTest.cpp"),
        "-o", str(executable),
    ], check=True)
    for offset in (0x80001000, 0xfffffff0):
        path = out / f"sparse-{offset:x}.pkg"
        size = offset + 24
        header = bytearray(0x5c0)
        def be(at, value, width):
            header[at:at + width] = value.to_bytes(width, "big")
        header[:4] = b"\x7fCNT"
        be(0x10, 1, 4)
        be(0x16, 1, 2)
        be(0x18, 0x5a0, 4)
        be(0x20, offset, 8)
        be(0x28, 24, 8)
        be(0x430, size, 8)
        be(0x5a0, 0x1200, 4)
        be(0x5b0, offset, 4)
        be(0x5b4, 24, 4)
        png = b"\x89PNG\r\n\x1a\n" + b"\x00\x00\x00\x0dIHDR" + (1).to_bytes(4, "big") * 2
        with path.open("wb") as handle:
            handle.write(header)
            handle.seek(offset)
            handle.write(png)
        subprocess.run([str(executable), str(path)], check=True)
        print(f"PASS sparse offset={offset:#x}, logical size={size}, allocated={path.stat().st_blocks * 512}", flush=True)
        path.unlink()
