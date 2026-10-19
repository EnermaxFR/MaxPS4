#!/usr/bin/env python3
"""Inspect real Mach-O files and their loader contracts inside the actual IPA."""
import hashlib
import json
import plistlib
import re
from pathlib import Path
import struct
import sys
import zipfile


def macho(data):
    magic, cpu, _, kind, count, _, flags, _ = struct.unpack_from('<8I', data)
    assert magic == 0xFEEDFACF and cpu == 0x100000C, 'Expected thin ARM64 Mach-O'
    assert flags & 1, 'Mach-O must have MH_NOUNDEFS'
    offset = 32
    libraries, platforms, exports, imports, identifiers = [], [], set(), set(), []
    for _ in range(count):
        command, size = struct.unpack_from('<II', data, offset)
        assert size >= 8 and offset + size <= len(data)
        if command in (0xC, 0x80000018, 0x8000001F, 0xD):
            name_offset = struct.unpack_from('<I', data, offset + 8)[0]
            name = data[offset + name_offset:data.index(b'\0', offset + name_offset)].decode()
            (identifiers if command == 0xD else libraries).append(name)
        if command == 0x32:
            platform, minimum = struct.unpack_from('<II', data, offset + 8)
            platforms.append((platform, minimum))
        if command == 2:
            _, _, symoff, nsyms, stroff, _ = struct.unpack_from('<6I', data, offset)
            for i in range(nsyms):
                index, typ, section, _, value = struct.unpack_from('<IBBHQ', data, symoff + 16 * i)
                if typ & 0xE0 or not typ & 1:
                    continue
                name = data[stroff + index:data.index(b'\0', stroff + index)].decode()
                (imports if typ & 0xE == 0 and value == 0 else exports).add(name)
        offset += size
    assert platforms and all(p == 2 and v <= 0x1B0200 for p, v in platforms), 'Expected iOS device deployment <=27.2'
    return kind, libraries, exports, imports, identifiers


def verify(path):
    prefix = 'Payload/MaxPS4.app/'
    framework = prefix + 'Frameworks/MaxPS4FEXRuntime.framework/'
    with zipfile.ZipFile(path) as ipa:
        assert ipa.testzip() is None
        metadata = plistlib.loads(ipa.read(prefix + 'Info.plist'))
        assert metadata['CFBundleExecutable'] == 'MaxPS4'
        project = (Path(__file__).resolve().parents[1] / 'MaxPS4-iOS/project.yml').read_text()
        expected_build = re.search(r"CURRENT_PROJECT_VERSION:\s*['\"]?([0-9]+)", project)
        assert expected_build, 'Missing expected build number in project.yml'
        assert metadata['CFBundleVersion'] == expected_build.group(1), (
            f"IPA build {metadata['CFBundleVersion']} does not match project build {expected_build.group(1)}"
        )
        assert metadata['CFBundleIdentifier'] == 'com.enermaxfr.maxps4'
        assert metadata['CFBundlePackageType'] == 'APPL'
        assert tuple(map(int, metadata['MinimumOSVersion'].split('.'))) <= (27, 2)
        for name in (prefix + 'MaxPS4', framework + 'MaxPS4FEXRuntime'):
            assert ipa.getinfo(name).external_attr >> 16 & 0o111, 'Mach-O must be executable'
        assert prefix + 'Assets.car' in ipa.namelist(), 'Original icon asset missing'
        assert 'stikdebug' in metadata.get('LSApplicationQueriesSchemes', []), 'StikDebug scheme missing'
        scripts = [n for n in ipa.namelist() if n.startswith(prefix) and n.endswith('/maxps4-stik.js')]
        assert len(scripts) == 1, 'Fixed StikDebug adapter missing'
        assert b'prepare_memory_region' in ipa.read(scripts[0])
        assert ipa.read(scripts[0]) == (Path(__file__).resolve().parents[1] / 'MaxPS4-iOS/Resources/JIT/maxps4-stik.js').read_bytes()
        main = ipa.read(prefix + 'MaxPS4')
        assert b'maxps4.testFEXCore' in main, 'Diagnostic FEXCore button not present in compiled app'
        assert b'maxps4.orbisDirectory.v4PRX' in main, 'Distinct V4 PRX action missing from compiled app'
        assert b'maxps4.m1Collision.bundled' in main and b'maxps4.m1Collision.imported' in main, 'M1 diagnostic actions absent'
        assert b'maxps4.cGameBootProbe' in main, 'C homebrew diagnostic action absent'
        assert b'maxps4.experimentalSELF' in main, 'Experimental SELF action absent'
        assert b'maxps4.separateEboot' in main, 'Separate eboot diagnostic action absent'
        c_probe=ipa.read(prefix+'GameBootProbe/homebrew.elf')
        assert c_probe[:9]==b'\x7fELF\x02\x01\x01\x09\x00' and struct.unpack_from('<Q',c_probe,24)[0]==0x400000
        assert struct.unpack_from('<H',c_probe,16)[0]==0xfe00
        assert b'maxps4.globalGameBootProbe' in main, 'Global data homebrew action absent'
        global_probe=ipa.read(prefix+'GlobalBootProbe/homebrew.elf')
        assert global_probe[:9]==b'\x7fELF\x02\x01\x01\x09\x00'
        assert struct.unpack_from('<H',global_probe,16)[0]==0xfe00
        assert struct.unpack_from('<Q',global_probe,24)[0]==0x400000
        provenance=json.loads(ipa.read(prefix+'BuildProvenance.json'))
        fingerprint=hashlib.sha256(json.dumps(provenance['files'],sort_keys=True,separators=(',',':')).encode()).hexdigest()
        assert fingerprint==provenance['fingerprint']==metadata['MaxPS4SourceFingerprint']
        assert provenance['head']==metadata['MaxPS4SourceRevision'] and provenance['dirty']==metadata['MaxPS4SourceDirty']
        fixture_root=prefix+'M1CollisionFixtures/'
        fixture_manifest=json.loads(ipa.read(fixture_root+'manifest.json'))
        assert fixture_manifest['profile']=='iphone-low-m1' and fixture_manifest['entry']==0x400004000
        assert fixture_manifest['snapshot']==0x400010100
        expected_fixtures={'m1-two-threads-three-tls-modules.elf','tls-83.prx','tls-129.prx'}
        assert set(fixture_manifest['files'])==expected_fixtures
        for name,digest in fixture_manifest['files'].items():
            data=ipa.read(fixture_root+name)
            assert hashlib.sha256(data).hexdigest()==digest and struct.unpack_from('<Q',data,24)[0]==0x400004000
        runtime = ipa.read(framework + 'MaxPS4FEXRuntime')
        kind, libraries, _, imports, _ = macho(main)
        rkind, dependencies, exports, unresolved, identifiers = macho(runtime)
        load_path = '@rpath/MaxPS4FEXRuntime.framework/MaxPS4FEXRuntime'
        assert kind == 2 and rkind == 6, 'Expected app executable and runtime dylib'
        assert load_path in libraries and identifiers == [load_path]
        assert all(n.startswith(('/usr/lib/', '/System/Library/')) for n in dependencies)
        assert not any('FEXCore' in n or 'LogMan' in n or 'maxps4_' in n for n in unresolved)
        required = {'_maxps4_fex_engine_' + n for n in ('open', 'initialize', 'cpuid', 'interpret_elf', 'run_elf', 'run_v4_prx', 'close', 'stage')}
        required |= {'_maxps4_jit_' + n for n in ('begin', 'cancel', 'state', 'error', 'leases', 'confirmation', 'launch_failed')}
        required |= {'_maxps4_fex_m1_collision_diagnostic','_maxps4_fex_engine_run_m1_threads'}
        required |= {'_maxps4_fex_eboot_experimental_preflight','_maxps4_fex_engine_run_eboot_experimental'}
        required |= {'_maxps4_fex_eboot_preflight','_maxps4_fex_engine_run_eboot'}
        assert required <= exports and required <= imports, 'App must actually import the FEX/session ABI'
        assert '_maxps4_fex_v4_preflight' in exports, 'V4 diagnostic preflight export absent'
        assert any('CreateNewContext' in name for name in exports), 'Real FEX C++ runtime absent'
        fmeta = plistlib.loads(ipa.read(framework + 'Info.plist'))
        assert fmeta['CFBundleExecutable'] == 'MaxPS4FEXRuntime'
        assert not any('BreakpointJIT.framework/' in n for n in ipa.namelist())
        print('PASS actual ARM64 iOS app + embedded coherent FEX Darwin runtime')
        print('PASS opaque ABI imports, SDK-only runtime dependencies, deployment compatible with iOS 27.2')
        print('Runtime SDK imports:', len(unresolved))
        print('Application SHA256:', hashlib.sha256(main).hexdigest())
        print('Runtime SHA256:', hashlib.sha256(runtime).hexdigest())
    print('IPA SHA256:', hashlib.sha256(Path(path).read_bytes()).hexdigest())
    print('Physical iPhone installation/execution NOT validated; SideStore re-signing required')


if __name__ == '__main__':
    verify(sys.argv[1])
