#!/usr/bin/env python3
import sys
import zipfile

if len(sys.argv) != 2:
    print("Usage: check-playable-ipa.py <fichier.ipa>")
    sys.exit(2)

ipa = sys.argv[1]

required = [
    "Payload/MaxPS4.app/PlayableDemo/app.elf",
    "Payload/MaxPS4.app/PlayableDemo/provider.prx",
    "Payload/MaxPS4.app/PlayableDemo/maxps4.json",
]

try:
    with zipfile.ZipFile(ipa) as archive:
        names = set(archive.namelist())
        missing = [name for name in required if name not in names]

        if missing:
            print("ÉCHEC : fichiers manquants dans l'IPA :")
            for name in missing:
                print(" -", name)
            sys.exit(1)

        for name in required:
            if archive.getinfo(name).file_size == 0:
                print("ÉCHEC : fichier vide :", name)
                sys.exit(1)

        print("PASS : démo jouable présente dans l'IPA")
except (OSError, zipfile.BadZipFile) as error:
    print("ÉCHEC :", error)
    sys.exit(1)
