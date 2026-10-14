# Résultats V2

## Exécuté localement — Linux x86-64

Commande : `ASAN_OPTIONS=detect_leaks=0 bash scripts/test-orbis-v2.sh build/v2`.
Code de sortie final : **0**. GCC 14.2 ; ASan/UBSan actifs, UBSan fatal.
LeakSanitizer indisponible sous le traçage de cet environnement ; aucune preuve
LeakSanitizer locale revendiquée. Log intégral : `orbis-v2-local-tests.txt`.

- PKG inventaire, metadata/SFO, limites, erreurs I/O et pont C : PASS.
- PFS header-only chiffré, extraction D32, chemins/cycles/overlaps/budgets : PASS.
- Extraction via API C à CNT base 2 147 549 184 et 4 295 032 832 octets : PASS.
- Extent de fichier à offset relatif PFS 4 295 098 368 octets : PASS.
  Les fichiers sparse occupent 331 776 octets chacun ; pas d’allocation >4 Gio.
- Mode 0xd : EACCES, length=0, buffer de sortie intact : PASS.
- Artwork sparse traversant 4 Gio et buffers insuffisants : PASS.
- Linker : **3 368 checks PASS**, relocations, graphe PRX, NID et transactions.
- ELF/SELF clair, BSS, corruptions/W+X : PASS.
- Snapshot C Orbis, buffers tronqués, symlink/FIFO et diagnostics : PASS.
- ELF ET_SCE_EXEC original de 131 octets : validation/liaison/mapping natif,
  64 cycles de nettoyage et refus CPU non supporté PASS ; **aucune exécution CPU**.
- ABI JIT host non pris en charge et contrôleur JavaScript Stik : PASS.

Des erreurs découvertes dans les nouveaux tests (header C manquant, appel memcpy
avec source vide, mutation qui produisait un ELF encore valide) ont été corrigées.
La passe finale est verte, sans diagnostic sanitizer masqué.

## Compilation seule / exécution à établir

Aucun nouveau composant Swift/iOS/FEX n’est qualifié de « compilé » avant preuve
CI. Hôte local sans Swift/Xcode/xcrun/FEX Darwin. Les fonctions C++ testées
ci-dessus sont réellement compilées et testées ; l’app Swift, les routines Mach
et les appels FEX sont uniquement inspectés localement.

Les jobs GitHub Actions de la branche/PR fourniront les preuves Linux/macOS,
Swift/C, vrai FEX host, compilation ARM64 et packaging. Les statuts exacts sont
rapportés dans la PR ; un artifact IPA ne doit être fourni que si les contrôles
Mach-O et identité fixe réussissent. Une CI en cours n’est pas un succès.

## iPhone / jeux

Aucun appareil iPhone attaché. Aucune installation, exécution native FEX de
cette fixture ou sortie Metal de cette mission observée sur iPhone.
Le protocole `orbis-v2-iphone-protocol.md` définit les preuves à collecter.
Kero Blaster mode 0xd reste inaccessible ; seule l’observation utilisateur
préexistante de son identification est conservée. Aucun jeu PS4 annoncé fonctionnel.
