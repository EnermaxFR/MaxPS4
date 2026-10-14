# shadPS4Plus PKG extractor v1.1 assessment

Assessed release [PKG_EXTRACTOR_1_1](https://github.com/AzaharPlus/shadPS4Plus/releases/tag/PKG_EXTRACTOR_1_1),
tag commit `771a8d58aff540448c7cbace7c40a66a5daf010d`. This is a standalone
desktop extraction tool, not an iOS emulator. This assessment is separate from
JIT #169/#191 and changes no production PKG/PFS implementation.

## The >2GB fix and measured limits

The tag's parent diff changes only `src/common/io_file.cpp:382`:
`fseeko` becomes `fseeko64`. The release notes report corruption when extracting
PKGs larger than 2GB. This fixes a seek-width issue on relevant platforms; it
does not establish safe bounds handling or support for individual >2GiB files.
On this Linux LP64 host, off_t is already 64 bits, so the old seek failure cannot
be reproduced as a 32-bit ABI regression here.

Reproduction:

```sh
git clone --branch PKG_EXTRACTOR_1_1 https://github.com/AzaharPlus/shadPS4Plus.git /tmp/shadPS4Plus
python3 scripts/research-pkg-extractor.py /tmp/shadPS4Plus build/pkg-research
```

Requires Python 3, a C++23 compiler and ASan/UBSan. Probes require a disposable
output directory. Two sparse PKGs use only 8/12 KiB of allocated file blocks on
this host, despite logical lengths 2,147,487,768 and 4,294,967,304 bytes.
The real MaxPS4 inventory and artwork APIs successfully read the exact payload
at `0x80001000` and `0xfffffff0` (the latter spans 4GiB). These are synthetic
24-byte PNG header fixtures, not valid images for display or real game packages.
No whole-file buffer, decrypted data or game assets are used.

Additional probes compile the exact tagged PFSC scan function in isolation and
exercise tagged arithmetic expressions. They are not full extractor runs:

| Probe | Measured result |
|---|---|
| PFSC scan, 0x20001-byte input | ASan heap-buffer-overflow: scan reads 4 bytes with only one byte remaining |
| Extracted-file counter increment at 2GiB | UBSan signed integer overflow in `size_decompressed += 0x10000` |
| >4GiB alignment expressions | `-Wconversion` warns of narrowing to int; this LP64 compiler still produces the expected low-bit offset (1). No corruption demonstrated by this probe |
| MaxPS4 sparse inventory/artwork reads | Pass with ASan/UBSan at both offsets; exact bytes preserved |

Local LeakSanitizer is disabled because the traced workspace does not support
it. ASan/UBSan remain active. Existing MaxPS4 inventory, metadata, PFS header and
read-only D32 suites also pass. No full upstream encrypted-PKG extraction or
physical iPhone test was performed.

## Parsing audit and safe reuse

Useful concepts: fixed-size metadata reads, 64-bit seek/length handling, bounded
block-at-a-time reads, preserving declared lengths, and independent metadata
diagnostics. MaxPS4 already uses checked uint64_t ranges, exact reads, fixed
budgets and selected plaintext extraction; preserve those contracts.

Do not transplant `PKG::Extract`/`ExtractFiles` wholesale. The tagged code has:

- unchecked header/entry reads and overflow-prone content offset-plus-size sums;
- a PFSC scanner with incomplete trailing-read bounds and a u32 cursor that can
  wrap for larger images; the not-found sentinel is later used as an offset;
- unchecked PFSC offsets, block-size division, sector-map indices and differences;
- inode file sizes narrowed from s64 to int, plus the demonstrated signed counter
  overflow for a file reaching 2GiB;
- directory record/name lengths and inode indices used without complete range
  checks; zero record sizes can stall loops; paths lack an explicit confinement
  policy before directories/files are created;
- zlib return values/output lengths not validated before data is consumed;
- decrypted-license buffer padding that can describe more input than was allocated.

These latter issues are source-audit findings, not all independently reproduced.
For any future extension, require checked subtraction-based ranges, monotonic
block offsets, exact decompression lengths, finite loop budgets, safe path
components, and source stability across reads. Add isolated format fixtures and
sanitizer tests before extending MaxPS4's admitted formats. Keep selected-file
extraction in memory and refuse unsupported layouts rather than guessing.

## Encrypted PFS and iOS

The upstream extraction path calls RSA/AES/XTS crypto to derive and use keys.
Its ability to handle some packages does not make arbitrary protected PFS
plaintext available. MaxPS4 must keep encrypted PFS header-only and refuse
payload extraction without an explicitly supported lawful plaintext source.
This work adds no decryption, license/key handling, executable admission or
gameplay claims.

The AppImage/EXE and shell/batch installer scripts are desktop artifacts.
`fseeko64` is not the portable Darwin API: use iOS's 64-bit `off_t` with
`fseeko`/`pread`, prove representability and retain overflow checks. The new
tagged call also bypasses the source's `_MSC_VER` macro for `fseeko`; do not
assume its build environment's behavior applies to every Windows toolchain.
Crypto++, zlib and C++23 build settings require independent iOS integration and
license review; desktop filesystem installers cannot be inserted into the
sandboxed/security-scoped iOS import flow. Streaming format readers can be
portable; desktop binaries are not iOS components.

## GPL compatibility

The inspected PKG/PFS sources declare GPL-2.0-or-later; the IO wrapper additionally
attributes the yuzu project. Selecting GPL v2 is compatible in principle with
MaxPS4's declared GPL v2. Preserve per-file copyright/SPDX notices and provide
corresponding source if code is incorporated or redistributed; audit dependency
licenses separately. This is not a blanket audit of the repository or release
binaries. No upstream crypto/parser implementation is copied into production
by this assessment. The probe reads the tagged scan function at runtime solely
to reproduce its boundary failure.

Recommended next scope: promote the sparse boundary fixtures into both Linux
and macOS parser CI, retain header-only encrypted-PFS diagnostics, and consider
additional plaintext formats only in a separate, fixture-driven PR. Keep the
current read-only parser rather than replacing it with the desktop extractor.
