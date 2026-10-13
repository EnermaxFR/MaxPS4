# Validation de la progression Kero Blaster

## Observations locales

Environnement Linux x86-64, GCC 14 pour les tests ASan/UBSan ; Clang 19,
CMake 4 et Ninja pour FEX. Les outils de compilation supplémentaires sont
installés uniquement dans le workspace ; aucune modification système.

- `test-ps4-system-services.sh` : 13 suites C++ et deux contrôles C ABI réussis.
- `test-orbis-runtime.sh` : 3362 checks linker et segments ELF/SELF réussis.
- `test-orbis-analysis.sh` : métadonnées SCE/NID/PRX et erreurs réussies.
- `PS4OrbisCPUPreparationTest` étendu : BSS relocalisé et cycle CPU refusé,
  et flux d’instructions chevauchants refusés, exécutés avec ASan/UBSan.
- `test-ps4-fex-linked.sh` : FEX réel lié, puis exécution des instructions ARM64
  générées dans VIXL. Aucun symbole FEX/service remplacé, aucun résultat constant.

Extrait observé du journal :

```text
PASS SCE ELF -> PRX NID/RELA -> FEX CALL/RET -> ADD: RAX=42 backend=3 entry=0x104000 store=0x1234
PASS SCE ELF -> PRX NID/RELA -> FEX CALL/RET -> ADD: RAX=9 backend=3 entry=0x104000 store=0x1234
PASS linked Orbis ELF -> NID -> FEX -> libkernel -> resumed ADD/store/HLT (backend=3)
PASS real FEX calls open/read/close/clock/mmap, register return, fourth and seventh ABI arguments
PASS host-pointer isolation, failed restage and cleanup; iPhone execution requires separate device validation
```

Le journal complet et le dossier Orbis synthétique sont générés comme artefacts
par le workflow PS4 system services. Les appels libkernel observés appartiennent
au chemin DT standard existant ; le nouveau chemin SCE/PRX appelle du code
invité et ne possède pas encore de trampolines HLE SCE.

La détection LeakSanitizer est désactivée localement parce que cet environnement
tracé ne la supporte pas ; ASan et UBSan restent actifs. La CI conserve sa
configuration normale et le workflow macOS conserve sa vérification `leaks`.

## Exécution ARM64 native observée

Le commit `d74408e` a passé le [job ARM64 natif](https://github.com/EnermaxFR/MaxPS4/actions/runs/38396773169/job/115293868570)
sur `ubuntu-24.04-arm`. Les journaux donnent RAX=42 puis RAX=9,
`backend=2`, entrée 0x104000, store 0x1234, et les appels libkernel
open/read/close/clock/mmap réussis. La compilation avec le SDK iPhone ARM64
est également réussie dans ce run. Ce sont des observations d’exécution native
Linux ARM64 et de compilation iPhone, pas d’exécution iPhone.

Le test Swift réel de l’import et la construction IPA ont aussi réussi pour
`3b28e25` dans [ce run](https://github.com/EnermaxFR/MaxPS4/actions/runs/38396484592).
Consulter la PR pour les résultats et l’IPA du dernier commit ; ces résultats
intermédiaires n’anticipent pas sa réussite.

## Vérifications distantes à distinguer

La PR déclenche les tests ARM64 Linux natifs, l’import Swift réel sur macOS,
la compilation iPhone ARM64 et la construction IPA SideStore. Leurs résultats
et artefacts sont à lire sur le commit exact de la PR. Un workflow prévu n’est
pas une validation réussie ; la compilation ARM64 et une IPA ne prouvent pas
l’exécution sur appareil réel.

Aucun appareil iPhone, PKG Kero Blaster ou exécutable commercial n’est disponible
localement. Kero Blaster n’est ni exécuté ni jouable dans cette validation.
