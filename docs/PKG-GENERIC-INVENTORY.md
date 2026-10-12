# Generic PKG inventory: first increment of #161 / #162

Baseline audited: main `754b915` (PRs #159 and #160 merged). The previous
Swift entry report used a 128-byte header, did not interpret encryption flags,
and could mistake raw bytes for accessible payload. It also concluded that no
executable was accessible after inspecting only small entries. The first concrete
dependency is a trustworthy inventory before any executable admission.

`PS4PKGInventory` is an exact-read, bounded parser. It validates the 0x5a0-byte
header, declared package size at 0x430, total/secondary/secure counts, entry
bounds, body and PFS ranges. Its entry-table budget is 4096 descriptors.
Invalid payload ranges are retained with diagnostics; malformed headers reject
atomically. Duplicate IDs and overlapping payloads prohibit copying. Descriptors
retain IDs, name offsets, both flag words and reserved bytes. Header/table bounds
are structural checks, not signature or digest authentication.

The public schema was checked against LibOrbisPkg at
[643477263b2644e0803e0f58b8726ea4e3f3b7d4](https://github.com/maxton/LibOrbisPkg/tree/643477263b2644e0803e0f58b8726ea4e3f3b7d4/LibOrbisPkg/PKG):
PkgReader.cs (header offsets), Entry.cs (32-byte descriptors and flags1 bit 31),
Enums.cs (known IDs). No upstream implementation, cryptography, keys, licenses
or game data is included.

Copying is deliberately narrow: entry_names (0x200), param.sfo (0x1000), pic1.png
(0x1006), icon0.png (0x1200), only with both flags and reserved bytes zero,
valid unambiguous ranges outside the declared PFS image. Encryption-marked
entries are not read. Unknown flags/types remain unsupported/unknown.
The absence of an encryption flag is a declaration, not proof of authenticity.
Copies are opaque bytes in memory, limited to 1 MiB per extraction and 1 MiB total
in the UI report. No archive path is ever used to write a file. Metadata formats
are not yet validated; a reported ELF/SELF signature is only a candidate.
An eboot.bin text clue is not a filesystem entry or proof of accessibility.

The C bridge opens one regular-file descriptor and reads only bounded ranges.
The parser's Reader contract requires an unchanged source during inspection.
The Swift wrapper has a 2 MiB report buffer and propagates I/O/truncation errors.
The existing "Examiner les entrées du PKG" button and launch preflight use this
report. Other legacy diagnostic probes remain available; consolidation is future
work. FEXCore, Metal, display name MaxPS4 and JIT safeguards are preserved.

## Verification

```sh
c++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  MaxPS4-iOS/Native/PS4PKGBridge.cpp \
  MaxPS4-iOS/Tests/PS4PKGInventoryTest.cpp -o /tmp/pkg-test
/tmp/pkg-test
```

In the managed Linux executor, LeakSanitizer cannot run under ptrace. Local tests
there use `ASAN_OPTIONS=detect_leaks=0`; AddressSanitizer and UBSan remain enabled.
CI runs the same suite with default sanitizer settings on Ubuntu, plus native
and Swift bridge tests on macOS and compilation/type-checking for iPhone arm64.
The existing FEXCore application workflow builds the SideStore IPA on PRs.
A compiled IPA does not establish successful installation or game execution.

## Next dependencies

1. Resolve bounded names from the plaintext entry_names table and validate known
   metadata formats. Consolidate the remaining Swift probes onto this inventory.
2. Implement an explicit bounded executable candidate admission path only when
   the unencrypted format is understood; never infer a game filesystem from the
   PKG descriptor table. PFS remains opaque; there is no decryption fallback.
3. Validate accessible ELF/SELF with the existing segment loader, then resolve
   imports/relocations before wiring a real executable to FEX guest memory.
4. Diagnose the actual iPhone ENOTSUP restriction separately; preserve it. Only
   real device traces can guide HLE services, PM4/Metal, audio and controls.

No Kero Blaster PKG was available in this workspace. No iPhone test, game launch,
first frame or gameplay is claimed by this change. #161 is only partially covered.
