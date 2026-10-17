#!/usr/bin/env python3
"""Hash workspace inputs, including uncommitted files, without publishing source."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

root=Path(__file__).resolve().parents[1]
paths=subprocess.check_output(['git','ls-files','-c','-o','--exclude-standard','-z'],cwd=root).decode().split('\0')
files={}
for name in sorted(set(paths)):
    if not name.startswith(('MaxPS4-iOS/','scripts/','cmake/','patches/','homebrew/','.github/workflows/')):
        continue
    if '/Assets.xcassets/' in name or '.xcodeproj/' in name or name.endswith('.bak'):
        continue  # Generated assets/project and editor backups do not identify source.
    path=root/name
    if path.is_file():
        files[name]=hashlib.sha256(path.read_bytes()).hexdigest()
fingerprint=hashlib.sha256(json.dumps(files,sort_keys=True,separators=(',',':')).encode()).hexdigest()
report={'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        'dirty':bool(subprocess.check_output(['git','status','--porcelain'],cwd=root)),
        'fingerprint':fingerprint,'files':files}
Path(sys.argv[1]).write_text(json.dumps(report,sort_keys=True,indent=2)+'\n')
