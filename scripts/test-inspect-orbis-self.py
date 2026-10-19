#!/usr/bin/env python3
"""Synthetic-only regression tests; never require or distribute game files."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('inspect_self', Path(__file__).with_name('inspect-orbis-self.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def fixture(flags=0x804):
    b = bytearray(257)
    struct.pack_into('<I', b, 0, 0x1d3d154f)
    b[4:10] = bytes.fromhex('000101120101')
    struct.pack_into('<HHI', b, 12, 256, 0, len(b))
    struct.pack_into('<H', b, 24, 1)
    struct.pack_into('<4Q', b, 32, flags, 256, 1, 1)
    b[64:80] = b'\x7fELF\x02\x01\x01\x09'+bytes(8)
    struct.pack_into('<HHIQQQIHHHHHH', b, 80, 0xfe00, 62, 1, 0x400000, 64, 0, 0, 64, 56, 1, 0, 0, 0)
    struct.pack_into('<II6Q', b, 128, 1, 5, 0x4000, 0x400000, 0, 1, 1, 1)
    b[256] = 0xc3
    return b


class SelfInspectionTest(unittest.TestCase):
    def test_signed_is_not_encrypted(self):
        r = module.inspect(fixture())
        self.assertTrue(r['blocks'][0]['signed'])
        self.assertFalse(r['blocks'][0]['encrypted'])
        self.assertTrue(r['load_plan'][0]['declared_clear_direct_bytes'])
        self.assertFalse(r['guest_mapped'])
        self.assertFalse(r['execution_admitted'])
        self.assertFalse(r['signature_verified'])

    def test_protected_stops_before_metadata_payload(self):
        for flags in (0x806, 0x80c):
            b = fixture(flags)
            # Add metadata headers that cannot be read. The protected gate must
            # return headers-only before attempting to interpret their payload.
            struct.pack_into('<H', b, 64+56, 2)
            struct.pack_into('<II6Q', b, 184, 2, 4, 0xdead, 0, 0, 16, 16, 1)
            r = module.inspect(b)
            self.assertIn('headers only', r['metadata_status'])
            self.assertFalse(r['load_plan'][0]['declared_clear_direct_bytes'])
            self.assertNotIn('imports', r)

    def test_bad_ranges_and_ids(self):
        for offset, value in ((40, 258), (48, 2), (32, 0x100804), (136, (1<<64)-1)):
            b = fixture(); struct.pack_into('<Q', b, offset, value)
            with self.assertRaises(ValueError): module.inspect(b)

    def test_truncations(self):
        for length in range(257):
            with self.assertRaises(ValueError): module.inspect(fixture()[:length])

    def test_trailing_is_reported_not_admitted(self):
        r = module.inspect(fixture()+b'original trailer')
        self.assertEqual(r['trailing_bytes'], 16)
        self.assertFalse(r['execution_admitted'])

    def test_dynamic_inventory_and_repeated_attributes(self):
        b = bytearray(0x900)
        b[:16] = b'\x7fELF\x02\x01\x01\x09' + bytes(8)
        struct.pack_into('<HHIQQQIHHHHHH', b, 16, 0xfe10, 62, 1, 0x400000, 64, 0, 0, 64, 56, 3, 0, 0, 0)
        struct.pack_into('<II6Q', b, 64, 1, 5, 0x400, 0x400000, 0, 1, 1, 1)
        tags = [(1, 1), (0x61000035, 0), (0x61000037, 18), (0x61000039, 0x80),
                (0x6100003b, 24), (0x6100003f, 48), (0x6100002f, 0xc0),
                (0x61000031, 24), (0x61000033, 24), (0x61000019, 9),
                (0x61000019, 0x1000000000009), (0, 0)]
        struct.pack_into('<II6Q', b, 120, 2, 4, 0x500, 0, 0, len(tags)*16, len(tags)*16, 8)
        struct.pack_into('<II6Q', b, 176, 0x61000000, 4, 0x700, 0, 0, 0x200, 0, 16)
        b[0x400] = 0xc3
        for i, pair in enumerate(tags): struct.pack_into('<QQ', b, 0x500+16*i, *pair)
        b[0x700:0x712] = b'\0libc.prx\0NID#A#B\0'
        struct.pack_into('<IBBHQQ', b, 0x798, 10, 0x12, 0, 0, 0, 0)
        struct.pack_into('<QQq', b, 0x7c0, 0x410000, (1 << 32) | 7, 0)
        r = module.inspect(b)
        self.assertEqual(r['dependencies'], ['libc.prx'])
        self.assertEqual(r['imports'][0]['name'], 'NID#A#B')
        self.assertEqual(r['relocation_types'], {'7': 1})
        self.assertFalse(r['execution_admitted'])

    def test_file_types_and_original_unchanged(self):
        with tempfile.TemporaryDirectory() as root:
            p = Path(root)/'eboot.bin'; p.write_bytes(fixture())
            before = p.read_bytes()
            module.inspect_file(p)
            self.assertEqual(before, p.read_bytes())
            link = Path(root)/'link'; link.symlink_to(p)
            with self.assertRaises(OSError): module.inspect_file(link)
            with self.assertRaises(ValueError): module.inspect_file(root)


if __name__ == '__main__':
    unittest.main()
