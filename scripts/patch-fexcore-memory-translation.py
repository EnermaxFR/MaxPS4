#!/usr/bin/env python3
"""Apply the reviewed guest-memory IR/decoder patch to the pinned build tree.

Check the complete patch before writing. Reject upstream drift; tolerate only
an exact, fully applied patch on repeated Darwin and IPA build invocations.
"""
from pathlib import Path
import subprocess
import sys

root = Path(sys.argv[1]).resolve()
patch = Path(__file__).resolve().parent.parent / 'patches/fexcore-guest-memory.patch'
args = ['git', 'apply', '--unsafe-paths', '--directory=' + str(root), str(patch)]
# The patch paths are relative to the fexcore-darwin root.
check = subprocess.run(args[:2] + ['--check'] + args[2:], capture_output=True, text=True)
if check.returncode:
    reverse = subprocess.run(args[:2] + ['--reverse', '--check'] + args[2:], capture_output=True, text=True)
    if reverse.returncode:
        raise SystemExit('Pinned FEX memory translation patch diverged:\n' + check.stderr)
else:
    subprocess.run(args, check=True)
print('PASS pinned FEX guest memory translation patch (idempotent)')
