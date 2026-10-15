#!/usr/bin/env python3
"""Fixed experimental identity + existing real Mach-O/SideStore contract."""
import hashlib
import re
import plistlib
import sys
import zipfile
from pathlib import Path
import importlib.util

spec = importlib.util.spec_from_file_location('verify_fex', Path(__file__).with_name('verify-fexcore-ipa.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
module.verify(sys.argv[1])
with zipfile.ZipFile(sys.argv[1]) as ipa:
    info = plistlib.loads(ipa.read('Payload/MaxPS4.app/Info.plist'))
    assert info['CFBundleIdentifier'] == 'com.enermaxfr.maxps4.orbistest', 'Stable/PKG/JIT app identity refused'
    assert info['CFBundleDisplayName'] == 'MaxPS4 Orbis Test'
    _, _, exports, _, _ = module.macho(ipa.read('Payload/MaxPS4.app/Frameworks/MaxPS4FEXRuntime.framework/MaxPS4FEXRuntime'))
    assert {'_maxps4_fex_engine_prepare_orbis', '_maxps4_fex_engine_run_orbis'} <= exports
    assert re.fullmatch(r'[0-9a-f]{40}', info['MaxPS4SourceRevision'])
    assert re.fullmatch(r'[0-9a-f]{40}', info['MaxPS4PRHeadRevision'])
    for name in ['m1-two-threads-three-tls-modules.elf', 'tls-83.prx', 'tls-129.prx']:
        data = ipa.read('Payload/MaxPS4.app/OrbisM1/' + name)
        assert hashlib.sha256(data).hexdigest() == info['MaxPS4M1FixtureSHA256'][name]
        assert data[:4] == b'\x7fELF'
    assert '_maxps4_fex_engine_run_m1_threads' in exports
print('PASS isolated MaxPS4 Orbis Test identity; SideStore must re-sign app + framework')
