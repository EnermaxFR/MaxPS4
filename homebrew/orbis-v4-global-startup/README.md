# Sonde C : données initialisées et BSS

Source originale sous GPL-2.0 comme le dépôt, sans SDK propriétaire.
Ce programme utilise l'entrée de fonction expérimentale V4 : RDI pointe vers
argc/argv, RSP contient une adresse de retour contrôlée. Il ne fournit pas de
CRT PS4 et n'a pas été validé sur console.

```bash
bash scripts/build-orbis-v4-homebrew.sh /tmp/global-boot homebrew/orbis-v4-global-startup/start.c
build/ps4-linked/Bin/maxps4-ps4-linked-test --v4-global-boot /tmp/global-boot/homebrew.elf
```

Clang O1 émet MOV EAX,[RIP+disp32], ADD EAX,[RDI], ADD EAX,[RIP+disp32], RET.
GNU ld résout les deux R_X86_64_PC32 de l'objet pendant la construction.
Le fichier exécutable final n'exige aucune nouvelle relocation dynamique.
Les octets sont chargés puis exécutés sans patch par le test FEX ; résultat
attendu : 1 + 0x1234 + 0 = 4661. Le zéro provient du BSS chargé par le moteur.
Un compilateur produisant un autre corps peut être refusé : aucune admission
arbitraire du C n'est promise.

Sur iPhone, la sonde sera embarquée dans GlobalBootProbe/homebrew.elf par le
script de packaging ; utiliser le bouton « Tester homebrew C — données globales
V4 », conserver le hash et exiger trois retours 4661/backend 2 sur l'appareil.
Cette préparation n'est pas une validation iOS réalisée.
