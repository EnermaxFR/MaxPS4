#!/usr/bin/env python3
"""Verify real pinned-file patch application, repeatability and drift refusal."""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

repo = Path(__file__).resolve().parents[2]
source = Path(sys.argv[1])
patch = repo / 'patches/fexcore-guest-memory.patch'
paths = re.findall(r'^\+\+\+ b/(.*)$', patch.read_text(), re.M)
assert len(paths) == 14, paths
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    for name in paths:
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / name, target)
    command = [sys.executable, str(repo / 'scripts/patch-fexcore-memory-translation.py'), str(root)]
    reverse = ['git','apply','--reverse','--unsafe-paths','--directory='+str(root),str(patch)]
    # Exercise the pristine pinned source, even when the provided build tree
    # already contains the patch from a prior iOS/macOS build.
    checked = subprocess.run(reverse[:2]+['--check']+reverse[2:],capture_output=True)
    if checked.returncode == 0: subprocess.run(reverse,check=True)
    subprocess.run(command, check=True)
    snapshot = {name: (root / name).read_bytes() for name in paths}
    subprocess.run(command, check=True)
    assert snapshot == {name: (root / name).read_bytes() for name in paths}
    damaged = root / 'FEXCore/Source/Interface/Core/JIT/MemoryOps.cpp'
    damaged.write_text(damaged.read_text().replace('DEF_OP(TranslateGuestAddress)', 'DEF_OP(DivergedTranslation)', 1))
    before = {name: (root / name).read_bytes() for name in paths}
    result = subprocess.run(command, capture_output=True)
    assert result.returncode != 0, 'Diverged patch was accepted'
    assert before == {name: (root / name).read_bytes() for name in paths}, 'Partial mutation on rejection'
print('PASS pinned memory patch: application, idempotence, drift refused without partial edits')
