"""Synthetic read-only probes. No game, keys or encrypted payloads required."""
import os
import argparse
import shlex
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('upstream', type=Path, help='shadPS4Plus checkout at PKG_EXTRACTOR_1_1')
parser.add_argument('output', type=Path, help='disposable probe output directory')
args = parser.parse_args()
ROOT = args.output.resolve()
ROOT.mkdir(parents=True, exist_ok=True)
MAX = Path(__file__).resolve().parents[1]
UP = args.upstream.resolve()
assert subprocess.check_output(['git', '-C', str(UP), 'rev-parse', 'HEAD'], text=True).strip() == '771a8d58aff540448c7cbace7c40a66a5daf010d'
compiler = shlex.split(os.environ.get('CXX', 'c++'))
env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')

def run(args, **kwargs):
    return subprocess.run(args, check=True, env=env, **kwargs)

run(compiler + [ '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pedantic',
     '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
     '-I', str(MAX / 'MaxPS4-iOS/Native'), str(MAX / 'MaxPS4-iOS/Native/PS4PKGBridge.cpp'),
     str(MAX / 'MaxPS4-iOS/Tests/PKGLargeFileResearchTest.cpp'), '-o', str(ROOT / 'sparse-bridge')])
for offset in (0x80001000, 0xfffffff0):
    path = ROOT / f'sparse-{offset:x}.pkg'
    size = offset + 24
    header = bytearray(0x5c0)
    def be(at, value, n):
        header[at:at+n] = value.to_bytes(n, 'big')
    header[:4] = b'\x7fCNT'
    be(0x10, 1, 4); be(0x16, 1, 2); be(0x18, 0x5a0, 4)
    be(0x20, offset, 8); be(0x28, 24, 8); be(0x430, size, 8)
    be(0x5a0, 0x1200, 4); be(0x5b0, offset, 4); be(0x5b4, 24, 4)
    payload = b'\x89PNG\r\n\x1a\n' + b'\x00\x00\x00\x0dIHDR' + (1).to_bytes(4, 'big') * 2
    with path.open('wb') as file:
        file.write(header); file.seek(offset); file.write(payload)
    run([str(ROOT / 'sparse-bridge'), str(path)])
    print(f'offset={offset:#x} logical_size={size} allocated={path.stat().st_blocks * 512}', flush=True)
    path.unlink()

# Compile the exact tagged PFSC scan function independently of crypto/GUI.
source = (UP / 'src/core/file_format/pkg.cpp').read_text()
scan = source[source.index('u32 GetPFSCOffset('):source.index('\nPKG::PKG()')]
probe = ROOT / 'upstream-scan.cpp'
probe.write_text('#include <span>\n#include <vector>\n#include <cstring>\n#include <cstdint>\n'
                 'using u32=uint32_t; using u8=uint8_t;\n' + scan +
                 '\nint main(){std::vector<u8> b(0x20001);return GetPFSCOffset(b)==0;}\n')
run(compiler + [ '-std=c++23', '-fsanitize=address,undefined', str(probe), '-o', str(ROOT / 'upstream-scan')])
result = subprocess.run([str(ROOT / 'upstream-scan')], env=env, capture_output=True, text=True)
(ROOT / 'upstream-scan.log').write_text(result.stderr)
assert result.returncode != 0 and 'heap-buffer-overflow' in result.stderr
print('CONFIRMED tagged PFSC scan: 4-byte read past 0x20001-byte input', flush=True)

# Evaluate the exact tagged alignment arithmetic at a >4GB sector offset.
probe = ROOT / 'upstream-alignment.cpp'
probe.write_text('''#include <cstdint>
#include <cstdio>
int main() {
    uint64_t sectorOffset=0x100001001ULL; uint32_t pfsc_offset=0;
    int sectorOffsetMask = (sectorOffset + pfsc_offset) & 0xFFFFF000;
    int previousData = (sectorOffset + pfsc_offset) - sectorOffsetMask;
    std::printf("tagged previousData=%d correct=%llu\\n", previousData,
        static_cast<unsigned long long>((sectorOffset+pfsc_offset)&0xfff));
    // This conversion happens to retain the low bits on this LP64 compiler;
    // it is implementation-defined and must not be assumed portable.
}
''')
run(compiler + [ '-std=c++17', '-Wconversion', str(probe), '-o', str(ROOT / 'upstream-alignment')])
run([str(ROOT / 'upstream-alignment')])

probe = ROOT / 'upstream-file-size.cpp'
probe.write_text('''#include <cstdint>
int main() {
    volatile int size_decompressed=0x7fff0000;
    size_decompressed += 0x10000;
    return 0;
}
''')
run(compiler + [ '-std=c++17', '-fsanitize=undefined', '-fno-sanitize-recover=all',
     str(probe), '-o', str(ROOT / 'upstream-file-size')])
result = subprocess.run([str(ROOT / 'upstream-file-size')], env=env, capture_output=True, text=True)
(ROOT / 'upstream-file-size.log').write_text(result.stderr)
assert result.returncode != 0 and 'signed integer overflow' in result.stderr
print('CONFIRMED tagged extracted-file counter overflow at 2GiB', flush=True)
