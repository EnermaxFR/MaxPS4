# Rapport FEXCore iOS ARM64

## Résultat vérifié

La cible Darwin complète **compile et se lie strictement** pour iOS ARM64.
`libFEXCore.dylib` est un Mach-O arm64 iOS, minimum iOS 18, drapeau MH_NOUNDEFS,
aucun import FEXCore/LogMan manquant. Les 18 symboles qui bloquent encore le
diagnostic générique existent tous dans ce binaire. Aucun stub de liaison ajouté.
Aucune IPA produite, aucune fusion effectuée par ce travail, aucun commit ajouté
directement sur main. Le JIT n'a pas été exécuté sur iPhone.

## Base, branche et commits

Branche : `codex/fexcore-ios-linkage`. Base finale demandée : dernier main
`73ed0f021e051e2ef11f98da18de8800ff01d492` (fusion de #125 faite par l'utilisateur).
La branche préparée depuis `4e330a7` a été rebasée après ce changement de consigne.
Les corrections validées de #126 sont reprises dans un commit sur cette nouvelle base.

| Commit | Modification |
| --- | --- |
| `0e7ce983c94953cf3ac289faae38b6909e31a8da` | Dépendances réelles génériques ; diagnostics/contrats/tests ; nouvelle cible complète Darwin ; liaison FEXCore_Base ; cache public Darwin |
| `4f168823a0b7e574861cbe3ba7b334d151ecfc45` | Cross-compilation sans autodétection CPU Linux /proc/cpuinfo |
| `6f062c623f97b6d7fe20512e8724cc706e867d9d` | Manifest des sources réelles et vérification de toutes les formes d'import FEXCore |

PR de travail : https://github.com/EnermaxFR/MaxPS4/pull/127 (brouillon ; aucune fusion automatique).
Le commit documentaire suivant ne change pas le code validé.

## Sources et cible de liaison

Le diagnostic générique conserve douze objets : ALUOps, AtomicOps, BranchOps,
ConversionOps, EncryptionOps, JIT, MemoryOps, MiscOps, MoveOps, VectorOps,
Arm64Relocations et CPUBackend. Huit unités réelles supplémentaires couvrent
Arm64Emitter, LookupCache, CPUID, LongJump, LogManager, ForcedAssert,
SpinWaitLock et AllocatorHooks. Il reste distinctement BLOCKED sur 18 symboles.

La cible complète compile l'ensemble cohérent du port Darwin épinglé à AetherPS4
`3347fc13f19860855ce91b66356940989ea6b6a6`, base FEX `f2b679f6028ce1c38875233aecfcf5d3f8ebecec`.
Son manifest/compile_commands conserve les unités de configuration, contexte,
frontend, dispatcher, interpréteur, tables x86, IR/passes, cache, allocateur
64 bits, telemetry, threads, profiler et les onze unités JIT avec CPUBackend.
Toutes les dépendances fmt/xxhash/cephes/SoftFloat sont compilées pour le même SDK.
Aucune bibliothèque Homebrew macOS n'est liée à la cible iOS.

Commande reproductible sur Mac avec Xcode/SDK iPhoneOS, CMake et Ninja :

```bash
git clone --filter=blob:none --no-checkout https://github.com/Leviidev/AetherPS4.git build/aether-link
git -C build/aether-link sparse-checkout set runtime/sources/fexcore-darwin
git -C build/aether-link checkout 3347fc13f19860855ce91b66356940989ea6b6a6
bash scripts/build-fexcore-darwin-ios.sh build/aether-link build/fexcore-darwin-ios
```

Le script configure CMAKE_SYSTEM_NAME=iOS, CMAKE_SYSTEM_PROCESSOR/OSX_ARCHITECTURES=arm64,
SDK obtenu par xcrun --sdk iphoneos --show-sdk-path, minimum iOS 18, Debug,
TUNE_CPU=none/TUNE_ARCH=generic et target FEXCore_shared. La cible upstream
omettait FEXCore_Base : le hook CMake ajoute cette vraie dépendance et
LINKER:-undefined,error. Les deux invalidations builtin sont remplacées par
sys_icache_invalidate dans le checkout temporaire, avec contrôle de contexte.
Le script échoue sur toute erreur de configuration, compilation ou liaison.

## Symboles et artefacts

Diagnostic générique : 121 références initiales ; 43 résolues et 42 nouvelles
transitives ; 120 imports finaux, dont 102 résolus par le SDK et 18 bloquants.
Cible Darwin complète : ces **18/18 définitions sont présentes**, zéro blocage
FEXCore de liaison ; 208 imports SDK normaux. Inventaires exhaustifs, objets
demandeurs et journal linker dans `fexcore-ios-unresolved-symbols.txt`.

Artefact CI vérifié :
https://github.com/EnermaxFR/MaxPS4/actions/runs/38127386275/artifacts/11697593593
Contient dylib, build-link.log/configure.log, compile_commands.json, nm -u/-gU,
otool -L/-Iv, file, architecture et statut. L'objet partiellement lié générique
est désormais également conservé dans son artefact heavy.

Dylib téléchargé localement :
`/workspace/artifacts/pr127-complete/FEXCore/Source/libFEXCore.dylib`.
SHA256 : `77342b7544a4c86e4c8a7f489cb56fccb211057e09bce35e29d21657320ba218`.
Chargements externes : `/usr/lib/libSystem.B.dylib` et `/usr/lib/libc++.1.dylib`.
Lecture indépendante des load commands : plateforme iOS (2), architecture
CPU_TYPE_ARM64, type MH_DYLIB, minimum 0x120000, MH_NOUNDEFS présent.

## Validations

| Validation | Résultat / limite |
| --- | --- |
| FEXDarwinPatchTest.py checkout pristine | 3 tests réussis : idempotence/plateformes, dérive, contexte dupliqué/écriture atomique |
| FEXDiagnosticsTest.py | 3 tests réussis : agrégation cross-objets, classification SDK/cache, commentaires ignorés |
| NativeJITPipelineTest | sept programmes/traduction/repli interpréteur réussis ; mots ARM64 non exécutés |
| FEXDarwinPrimitivesTest local | masques et contention réussis ; Linux/hôte, pas appareil |
| CI heavy 38127386265 | réussie : compilation iOS générique, primitives/allocateur macOS, contrats Darwin, HLE |
| CI smoke 38127386272 | réussie : ABI CPU, pont ARM64, Swift et régressions |
| CI liaison 38127386275 | réussie : vraie compilation/liaison iOS Darwin et analyse du dylib |
| Premier build complet 38127328046 | échec CMake autodétection Linux ; corrigé par TUNE_CPU=none |
| Bash / YAML / Python syntaxe et git diff --check | réussis |
| ctest iOS sur hôte Linux / xcrun local | non exécutables ; le SDK Apple est utilisé en CI macOS |
| Tests runtime FEXCore sur iPhone | non exécutés : appareil, signature et debugger absents |

Les tests existants ne sont ni supprimés ni désactivés. BUILD_TESTING=OFF ne
concerne que la nouvelle cross-compilation du fork (unités de test iOS non
exécutables sur runner macOS) ; les workflows/tests hôte précédents restent actifs.
Le workflow blank.yml de packaging n'a pas été lancé. Les fichiers téléchargés
et générés ne sont pas committés. Recherche locale de fichiers *.ipa : aucun.

## Blocages et prochaines validations, par priorité

1. **Intégration runtime à MaxPS4** : la bibliothèque complète reste une cible
   dédiée ; adapter le pont au contexte/ABI du fork, configurer toutes les couches
   sans mélanger les headers et objets génériques. Le diagnostic générique seul
   demeure incomplet : Config/Core/InterpreterFallbacks/Allocator/Telemetry manquent.
2. **Exécution iPhone / W^X** : debugger/signature réels, disponibilité dynamique
   de BreakpointJIT, gestion de perte de session et SIGTRAP, mapping RX/RW et
   permissions effectives. Un dylib lié ne valide pas les appels dlsym.
3. **Version minimale** : cible complète iOS 18 pour os_sync ; le diagnostic est
   iOS 16. Un support iOS 16 requiert un port/test du mutex, pas seulement un flag.
4. **Sémantique Darwin** : tester pages hôte, sysctl CPU, signaux/ucontext,
   allocations virtuelles, traduction RX↔RW, safepoints/invalidation et concurrence.
   Auditer les adaptations optionnelles Linux ENOSYS du fork avant usage produit.
5. **Code invité** : après le protocole appareil documenté, exécuter un bloc FEX
   arithmétique réel puis invalider/réutiliser le buffer. Aucun jeu PS4 validé.

Documents associés : `fexcore-pr125-review.md`,
`fexcore-darwin-aetherps4-comparison.md`, `ios-executable-memory.md`.

## Sources FEXCore de la configuration complète validée

```text
FEXCore/Source/Interface/Config/Config.cpp
FEXCore/Source/Utils/Allocator.cpp
FEXCore/Source/Utils/FileLoading.cpp
FEXCore/Source/Utils/ForcedAssert.cpp
FEXCore/Source/Utils/LogManager.cpp
FEXCore/Source/Utils/SpinWaitLock.cpp
FEXCore/Source/Utils/WildcardMatcher.cpp
FEXCore/Source/Utils/Allocator/64BitAllocator.cpp
FEXCore/Source/Common/JitSymbols.cpp
FEXCore/Source/Interface/Context/Context.cpp
FEXCore/Source/Interface/Core/LookupCache.cpp
FEXCore/Source/Interface/Core/CodeCache.cpp
FEXCore/Source/Interface/Core/Core.cpp
FEXCore/Source/Interface/Core/CPUBackend.cpp
FEXCore/Source/Interface/Core/Addressing.cpp
FEXCore/Source/Interface/Core/CPUID.cpp
FEXCore/Source/Interface/Core/Frontend.cpp
FEXCore/Source/Interface/Core/OpcodeDispatcher/AVX_128.cpp
FEXCore/Source/Interface/Core/OpcodeDispatcher/Crypto.cpp
FEXCore/Source/Interface/Core/OpcodeDispatcher/Flags.cpp
FEXCore/Source/Interface/Core/OpcodeDispatcher/Vector.cpp
FEXCore/Source/Interface/Core/OpcodeDispatcher/X87.cpp
FEXCore/Source/Interface/Core/OpcodeDispatcher/X87F64.cpp
FEXCore/Source/Interface/Core/OpcodeDispatcher.cpp
FEXCore/Source/Interface/Core/ArchHelpers/Arm64Emitter.cpp
FEXCore/Source/Interface/Core/Dispatcher/Dispatcher.cpp
FEXCore/Source/Interface/Core/Interpreter/Fallbacks/InterpreterFallbacks.cpp
FEXCore/Source/Interface/Core/Interpreter/Fallbacks/StringCompareFallbacks.cpp
FEXCore/Source/Interface/Core/JIT/JIT.cpp
FEXCore/Source/Interface/Core/JIT/ALUOps.cpp
FEXCore/Source/Interface/Core/JIT/AtomicOps.cpp
FEXCore/Source/Interface/Core/JIT/BranchOps.cpp
FEXCore/Source/Interface/Core/JIT/ConversionOps.cpp
FEXCore/Source/Interface/Core/JIT/EncryptionOps.cpp
FEXCore/Source/Interface/Core/JIT/MemoryOps.cpp
FEXCore/Source/Interface/Core/JIT/MiscOps.cpp
FEXCore/Source/Interface/Core/JIT/MoveOps.cpp
FEXCore/Source/Interface/Core/JIT/VectorOps.cpp
FEXCore/Source/Interface/Core/JIT/Arm64Relocations.cpp
FEXCore/Source/Interface/Core/X86Tables/BaseTables.cpp
FEXCore/Source/Interface/Core/X86Tables/DDDTables.cpp
FEXCore/Source/Interface/Core/X86Tables/H0F38Tables.cpp
FEXCore/Source/Interface/Core/X86Tables/H0F3ATables.cpp
FEXCore/Source/Interface/Core/X86Tables/PrimaryGroupTables.cpp
FEXCore/Source/Interface/Core/X86Tables/SecondaryGroupTables.cpp
FEXCore/Source/Interface/Core/X86Tables/SecondaryModRMTables.cpp
FEXCore/Source/Interface/Core/X86Tables/SecondaryTables.cpp
FEXCore/Source/Interface/Core/X86Tables/VEXTables.cpp
FEXCore/Source/Interface/Core/X86Tables/X87Tables.cpp
FEXCore/Source/Interface/GDBJIT/GDBJIT.cpp
FEXCore/Source/Interface/IR/IRDumper.cpp
FEXCore/Source/Interface/IR/IREmitter.cpp
FEXCore/Source/Interface/IR/PassManager.cpp
FEXCore/Source/Interface/IR/Passes/IRDumperPass.cpp
FEXCore/Source/Interface/IR/Passes/IRValidation.cpp
FEXCore/Source/Interface/IR/Passes/RedundantFlagCalculationElimination.cpp
FEXCore/Source/Interface/IR/Passes/RegisterAllocationPass.cpp
FEXCore/Source/Interface/IR/Passes/x87StackOptimizationPass.cpp
FEXCore/Source/Utils/LongJump.cpp
FEXCore/Source/Utils/Telemetry.cpp
FEXCore/Source/Utils/Threads.cpp
FEXCore/Source/Utils/Profiler.cpp
FEXCore/Source/Utils/ArchHelpers/Arm64.cpp
FEXCore/Source/Utils/AllocatorHooks.cpp
FEXCore/Source/Utils/AllocatorHooks.cpp
```

## Environnement Git relevé

```text
origin	https://github.com/EnermaxFR/MaxPS4.git (fetch)
origin	https://github.com/EnermaxFR/MaxPS4.git (push)
branche : codex/fexcore-ios-linkage
base : 73ed0f021e051e2ef11f98da18de8800ff01d492
```

## Validation du dernier commit de code

Commit `6f062c623f97b6d7fe20512e8724cc706e867d9d` : les trois workflows
ont terminé avec succès : [liaison complète 38127613795](https://github.com/EnermaxFR/MaxPS4/actions/runs/38127613795),
[heavy 38127613792](https://github.com/EnermaxFR/MaxPS4/actions/runs/38127613792),
[smoke 38127613774](https://github.com/EnermaxFR/MaxPS4/actions/runs/38127613774).
Les journaux locaux sont conservés dans `fexcore-ios-local-validation.txt`.
Le commit documentaire final conserve exactement ce code validé.
