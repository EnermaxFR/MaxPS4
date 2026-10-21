# Sonde Orbis V4 depuis les sources

Programme original assembleur x86-64, sans SDK ni bibliothèque propriétaire.
Le script construit un ELF avec deux PT_LOAD RX et RW, BSS et alignement 16 Kio,
puis définit OSABI=9 et ET_SCE_EXEC sur **notre propre fichier non chiffré**.
Il ne transforme aucun SELF/PKG commercial.

```sh
bash scripts/build-orbis-v4-homebrew.sh /tmp/orbis-homebrew
build/ps4-linked/Bin/maxps4-ps4-linked-test --v4-homebrew /tmp/orbis-homebrew/homebrew.elf
```

Prérequis : Clang avec assembleur intégré x86-64, Python 3, GNU ld sous Linux.
Sous macOS : `brew install lld` ; `ELF_LD` permet de choisir un linker ELF.
Le résultat standard est RAX=296 : lecture de 40 dans `.data`, addition de 256,
puis HLT. Le test FEX change aussi cette donnée pour vérifier 355 et 256.

**Contrat expérimental `MAXPS4_V4_ABI_STATIC_EXEC_HLT` uniquement.** Ce fichier
est une sonde homebrew au format ELF Orbis, pas une application SDK PS4 dont
le démarrage sur console serait démontré. HLT termine le test FEX ; ce n'est
pas une implémentation de sortie de processus PS4. Pas de libc, argc/argv,
TLS, imports, threads, graphique ni appels système. Ne pas le présenter comme
une preuve de compatibilité avec un homebrew PS4 général ou un jeu commercial.
