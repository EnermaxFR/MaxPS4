#!/usr/bin/env python3
"""Read-only PS4 CNT entry-table bounds check. No payload extraction/decryption."""
import argparse
import json
import os
import struct
import sys

ENTRY_BYTES = 32
MAX_ENTRIES = 4096


def inspect(path):
    size = os.stat(path).st_size
    with open(path, "rb") as f:
        header = f.read(0x80)
        if len(header) < 0x80 or header[:4] != b"\x7fCNT":
            raise ValueError("PKG.MAGIC: not a PS4 CNT container")
        count = struct.unpack_from(">I", header, 0x10)[0]
        table_offset = struct.unpack_from(">I", header, 0x18)[0]
        table_size = struct.unpack_from(">I", header, 0x1c)[0]
        if count > MAX_ENTRIES:
            raise ValueError("PKG.COUNTS: entry count exceeds safe limit")
        if table_offset > size or table_size > size - table_offset:
            raise ValueError("PKG.TABLE_RANGE: table outside source")
        if count * ENTRY_BYTES > table_size:
            raise ValueError("PKG.TABLE_RANGE: entries exceed declared table")
        content_id = header[0x40:0x70].split(b"\0", 1)[0].decode("ascii", "replace")
        f.seek(table_offset)
        entries = []
        for i in range(count):
            row = f.read(ENTRY_BYTES)
            if len(row) != ENTRY_BYTES:
                raise ValueError("PKG.TABLE_READ: short entry")
            entry_id = struct.unpack_from(">I", row, 0)[0]
            offset = struct.unpack_from(">I", row, 16)[0]
            length = struct.unpack_from(">I", row, 20)[0]
            if offset > size or length > size - offset:
                raise ValueError(f"PKG.BODY_RANGE: entry {i} outside source")
            entries.append({"index": i, "id": f"0x{entry_id:08x}",
                            "offset": offset, "size": length})
    return {"stage": "PKG.TABLE_RANGE", "structure": "bounded",
            "execution_supported": False, "file_size": size,
            "content_id_header_untrusted": content_id,
            "entry_count": count, "entries": entries}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pkg", help="local PKG path; never uploaded")
    args = parser.parse_args()
    try:
        print(json.dumps(inspect(args.pkg), indent=2))
    except (OSError, ValueError) as exc:
        print(json.dumps({"stage": "PKG.REJECT", "error": str(exc)}), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
