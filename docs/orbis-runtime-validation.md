# Validation de la fondation Orbis

Observations locales du 13 octobre 2026, Linux x86-64, GCC 14.2, base `08db54e`.
Aucun fichier propriétaire ; les fixtures sont générées par le code de test.

## Vérifications réalisées

| Vérification | Résultat local |
| --- | --- |
| `ASAN_OPTIONS=detect_leaks=0 bash scripts/test-orbis-runtime.sh` | Réussite : 3362 checks linker (incluant génération des deux fixtures), chargeur ELF/SELF plaintext et BSS/permissions |
| `ASAN_OPTIONS=detect_leaks=0 bash scripts/test-orbis-analysis.sh` | Réussite : parsing SCE, PRX, NID, TLS inventorié, pont de rapport natif et refus de préparation |
| `ASAN_OPTIONS=detect_leaks=0 bash scripts/test-ps4-system-services.sh` | Réussite : 12 exécutables C++ existants et les deux compilations C ABI ; mémoire/protection/lifecycle, libkernel, threads, linker standard et admission CPU |
| Tests `PS4PKGInventoryTest` et `PS4PKGMetadataTest` via `PS4PKGBridge.cpp` | Réussite avec ASan/UBSan, C++17, warnings fatals ; inventaire PKG et readiness inchangés |
| `FEXDiagnosticsTest.py` | 3 tests réussis |
| `FEXJITProviderPatchTest.py` sur le Darwin embarqué d'AetherPS4 `3347fc13f19860855ce91b66356940989ea6b6a6` | 3 tests réussis |
| `FEXDispatcherInitPatchTest.py` sur le même Darwin | 3 tests réussis |
| `FEXDarwinPatchTest.py` sur FEX upstream `f2b679f6028ce1c38875233aecfcf5d3f8ebecec` | 3 tests réussis |
| YAML des 14 workflows, `bash -n scripts/test-orbis-runtime.sh`, `git diff --check` | Réussite |

Les tests C++ sont compilés avec `-std=c++17 -Wall -Wextra -Werror -pedantic
-fsanitize=address,undefined -fno-omit-frame-pointer`. Les tests système ajoutent
`-pthread`. Les exécutables sont lancés par `scripts/run-bounded.py` avec 30 s.

Le premier lancement ASan a rencontré la limitation de LeakSanitizer sous
ptrace dans l'environnement Codex. Les relances désactivent **seulement** la
détection de fuites avec `ASAN_OPTIONS=detect_leaks=0` ; les vérifications
AddressSanitizer et UndefinedBehaviorSanitizer restent actives. La CI ne désactive
pas LeakSanitizer. Cela ne constitue pas un résultat positif de détection des fuites.

Deux tentatives de commande ont été corrigées : `FEXJITProviderPatchTest.py`
nécessite un chemin de sources, et `FEXDarwinPatchTest.py` nécessite le FEX
upstream brut plutôt que les sources Darwin déjà adaptées d'AetherPS4. Les
tests finaux ci-dessus utilisent leurs sources épinglées respectives.

## Vérifications impossibles localement

| Vérification | Cause et chemin de vérification disponible |
| --- | --- |
| Compilation Darwin / iOS ARM64 | Environnement Linux sans Xcode, xcrun ou SDK iPhone. Les jobs macOS de `pkg-inventory.yml` et `orbis-analysis.yml` compilent les ponts natifs pour iPhone et vérifient Swift/C. |
| Exécution des tests Swift/C | swiftc absent. Les tests Swift existants ont été étendus et restent exécutés par les jobs macOS. |
| Build/lien FEX et exécution x86-64 vers ARM64 | Clang, CMake et Ninja absents ; le FEX épinglé refuse GCC. Les sources ont été examinées, les patches testés ; aucune compilation ou exécution FEX locale n'est revendiquée. `test-ps4-fex-linked.sh` conserve le test réel borné, avec backend VIXL explicitement signalé sur x86. |
| Packaging IPA SideStore | Nécessite les frameworks FEX, Xcode et SDK iOS ; `fexcore-app.yml` conserve le test linker avant packaging et `project.yml` conserve les deux ponts natifs dans l'app. |
| Device ARM64/JIT iPhone, exécution PS4 ou jeu | Aucun device ni backend général d'espace d'adresses Orbis. Une préparation synthétique réussie ne valide aucune de ces exécutions. |

Les résultats futurs GitHub Actions doivent être lus sur la PR ; cette page ne
les annonce pas comme passés. La PR doit rester ouverte, sans fusion automatique.
