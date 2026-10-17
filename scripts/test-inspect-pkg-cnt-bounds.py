#!/usr/bin/env python3
"""Self-contained tests for the read-only CNT inspector (no proprietary fixtures)."""
import importlib.util
import os
import struct
import tempfile
import unittest
from pathlib import Path

MODULE = Path(__file__).with_name("inspect-pkg-cnt-bounds.py")
spec = importlib.util.spec_from_file_location("cnt_inspector", MODULE)
inspector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inspector)


def make_pkg(path, *, count=1, entry_offset=0x100, entry_size=8):
    data = bytearray(0x200)
    data[:4] = bytes.fromhex("7f434e54")
    struct.pack_into(">I", data, 0x10, count)
    struct.pack_into(">I", data, 0x18, 0x80)
    struct.pack_into(">I", data, 0x1c, 0x20)
    struct.pack_into(">III", data, 0x80, 0x1000, 0, 0)
    struct.pack_into(">II", data, 0x80 + 16, entry_offset, entry_size)
    data[0x100:0x108] = bytes.fromhex("89504e470d0a1a0a")
    path.write_bytes(data)


class CNTBoundsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "test.pkg"

    def test_valid_png_prefix(self):
        make_pkg(self.path)
        report = inspector.inspect(self.path)
        self.assertEqual(report["visible_prefix_counts"], {"PNG": 1})
        self.assertFalse(report["execution_supported"])

    def test_entry_outside_source(self):
        make_pkg(self.path, entry_offset=0x1ff, entry_size=4)
        with self.assertRaisesRegex(ValueError, "PKG.BODY_RANGE"):
            inspector.inspect(self.path)

    def test_table_count_overrun(self):
        make_pkg(self.path, count=2)
        with self.assertRaisesRegex(ValueError, "PKG.TABLE_RANGE"):
            inspector.inspect(self.path)

    def test_bad_magic(self):
        make_pkg(self.path)
        data = bytearray(self.path.read_bytes())
        data[0] = 0
        self.path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "PKG.MAGIC"):
            inspector.inspect(self.path)

    def test_empty_entry(self):
        make_pkg(self.path, entry_size=0)
        report = inspector.inspect(self.path)
        self.assertEqual(report["visible_prefix_counts"], {"UNKNOWN": 1})


if __name__ == "__main__":
    unittest.main()
