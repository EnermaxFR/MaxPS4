# Vérification FEXCore ARM64 sans IPA

Le workflow `parallel-heavy-engine.yml` vérifie la révision FEXCore
`f2b679f6028ce1c38875233aecfcf5d3f8ebecec`. Il s'exécute sur les pull requests,
sur les modifications pertinentes de `main` et à la demande. Il ne produit aucune IPA.

Les adaptations de `scripts/patch-fexcore-ios.py` sont appliquées avant toute
compilation, dans les workflows de diagnostic et de packaging :

- attente/réveil du mutex par `atomic_ref` pour Darwin, sans en-têtes Linux ou Windows ;
- masquage POSIX par `pthread_sigmask`, restauration après déplacement et bornes `NSIG` ;
- identifiant de processeur virtuel constant sur Darwin, sans `TPIDRRO` ni syscall Linux ;
- conseil de pages énormes Linux désactivé sur Darwin, sans substituer `MADV_NORMAL` ;
- exclusion des en-têtes Linux et du véritable appel `prctl(PR_GET_MDWE)` sur Apple.

Les branches Linux et Windows restent présentes. Le script vérifie les fragments
attendus avant d'écrire et peut être exécuté à nouveau sans doubler les adaptations.

La CI compile **toutes les unités `.cpp` du JIT** et `CPUBackend.cpp` avec le SDK
iPhoneOS, vérifie les objets Mach-O ARM64 et conserve les logs et symboles externes.
Elle tente les unités restantes même si une compilation échoue, puis échoue à son
tour. Les tests macOS exercent les véritables types FEXCore adaptés : restauration
des masques imbriqués/déplacés et contention entre lecteurs et écrivains. Un délai
maximum de 60 secondes détecte un blocage du test de contention. Ces tests utilisent
les primitives en configuration release (`ASSERTIONS_ENABLED=0`), sans relier le
runtime de journalisation/allocation FEXCore.

Le workflow `parallel-engine-checks.yml` exécute également
`NativeJITPipelineTest.cpp` : traduction de petits programmes x86 en mots ARM64,
repli vers l'interpréteur, rejet des programmes invalides et budgets épuisés.

Les objets ne constituent pas une liaison complète. La mémoire exécutable,
l'intégration avec le FEXCore Darwin d'AetherPS4, l'exécution JIT sur iPhone et les
jeux PS4 restent à valider. Aucun syscall ou symbole manquant n'est simulé pour
faire passer la compilation ou la liaison.

## Examen de la PR #125 et dépendances de liaison

La PR #125, initialement fermée sans fusion, a été fusionnée ensuite dans main au commit `73ed0f0`. Son commit `4e330a711d52fed699de0f0e4c6cd36ab4a54815`
et les artefacts du run `38125083690` restent la base de cette vérification.
L'union des symboles externes des 12 objets, après soustraction des définitions
présentes dans ces mêmes objets, contient **121 références**, dont **54 FEXCore/LogMan**.
Ce total n'est pas un décompte d'erreurs du linker : les imports de libc++,
libSystem et du runtime du compilateur sont normalement résolus par le SDK.

| Famille observée | Implémentation ou action nécessaire |
| --- | --- |
| `CPUBackend`, `Arm64JITCore::Op_*` | Déjà définis par les 12 objets de #125 ; disparus de l'inventaire agrégé |
| `Arm64Emitter` | `Interface/Core/ArchHelpers/Arm64Emitter.cpp` |
| `LookupCache`, `GuestToHostMap` | `Interface/Core/LookupCache.cpp` |
| `UncheckedLongJump` | `Utils/LongJump.cpp` |
| `CPUIDEmu::Function_*` | `Interface/Core/CPUID.cpp`, avec le véritable `git_version.h` généré depuis sa révision |
| `LogMan`, `fmt::v12` | `Utils/LogManager.cpp` et implémentation fmt compilée pour iOS, sans libfmt macOS |
| `ForcedAssert`, fréquence du compteur | `Utils/ForcedAssert.cpp`, `Utils/SpinWaitLock.cpp` |
| Allocations libc FEXCore | `Utils/AllocatorHooks.cpp`, avec les API Darwin `posix_memalign` et `malloc_size` |
| `__clear_cache` | `CodeEmitter/CodeEmitter/Buffer.h` : utiliser `sys_icache_invalidate` sur Darwin |
| `Config::Value`, valeurs de configuration | `Interface/Config/Config.cpp` et ses dépendances de fichiers/configuration |
| `ContextImpl::{CompileBlock,ClearCodeCache,MonoBackpatcherWrite,ThreadRemoveCodeEntryFromJit}` | `Interface/Context/Context.cpp`, `Interface/Core/Core.cpp` et leur chaîne de dépendances |
| `InterpreterOps::GetFallbackHandler` | `Interface/Core/Interpreter/Fallbacks/InterpreterFallbacks.cpp`, puis ses dépendances |
| `Allocator::{mmap,munmap,VirtualName}`, mémoire virtuelle/PMR | `Utils/Allocator.cpp` et `Utils/Allocator/64BitAllocator.cpp`, à adapter pour Darwin ; les allocations libc ne les remplacent pas |
| `TelemetryValues` | `Utils/Telemetry.cpp` et sa configuration/gestion des fichiers |

La CI ajoute les huit implémentations amont identifiées ci-dessus, réalise une
liaison partielle des vrais objets, puis produit `link-report.json` avec chaque
symbole restant, son nom démanglé, les fichiers propriétaires candidats et la
différence avant/après. Le contrôle de régression échoue si une famille couverte
par les corrections demeure non résolue. Les dépendances supplémentaires introduites
par ces fichiers restent visibles dans `new_dependencies`.

Une **vraie liaison iOS stricte** est aussi tentée, sans autoriser de symboles
manquants. Son résultat est conservé dans `strict-link-status.txt` et ses erreurs
dans `strict-link.log`. Un état `BLOCKED` reste attendu tant que les autres familles
ci-dessus ne sont pas portées ; un contrôle de dépendances vert ne prouve donc pas
la liaison complète. Le dylib de diagnostic éventuel n'est pas intégré à l'application.
Les tests réussis de #125 restent inchangés, avec un test supplémentaire de
l'allocateur libc sur macOS, sans mémoire exécutable.

## Contrats Darwin d'AetherPS4

AetherPS4 est épinglé à `3347fc13f19860855ce91b66356940989ea6b6a6`.
Le FEXCore générique et son FEXCore embarqué dans `runtime/sources/fexcore-darwin`
ne sont pas interchangeables. L'audit ignore les commentaires et enregistre les
lignes des déclarations et des usages ; une sonde C++ compile les signatures exactes
avec le SDK iPhoneOS et **uniquement** les headers/générateurs Darwin correspondants.

| Contrat attendu par `fex_guest_engine.cpp` | Generic épinglé | Darwin embarqué |
| --- | --- | --- |
| `ScopedJITWriteProtect` | Absent | Garde d'écriture macOS ; no-op documenté sur la face RW iOS |
| `GetWritableAddress` | Absent | Traduction RX → RW avec conservation de l'offset |
| `GetExecutableAddress` | Absent | Traduction RW → RX avec conservation de l'offset |
| `OnBufferReusedInPlace` | Absent | Invalidation des caches des autres threads avec le même token de verrouillage |
| `BeginBufferInvalidationSafepoint` | Absent | Suspension des autres threads avant invalidation/réutilisation |
| `EndBufferInvalidationSafepoint` | Absent | Reprise après sécurisation du buffer |

Les usages sont respectivement aux lignes 1625, 1276, 1726, 2138, 2177 et 2258 de
`src/core/fex/fex_guest_engine.cpp` dans cette révision. Les traductions d'adresses
sont implémentées dans le `Utils/Allocator.cpp` Darwin ; les callbacks sont appelés
par le code Darwin de gestion du cache. Ajouter seulement des déclarations,
des traductions identitaires ou des callbacks vides au générique ne respecte pas
ce contrat. Il faudra compiler/relier **l'ensemble Darwin correspondant** avant de
l'intégrer à GuestEngine ; la sonde ABI ne le fait pas.

## Résultat vérifié des corrections de liaison

Sur le commit `45a9930bae918606dc967999a7b6dffbd5b65ae1`, le
[run 38126286849](https://github.com/EnermaxFR/MaxPS4/actions/runs/38126286849)
compile les **20 objets iOS ARM64** (12 initiaux + 8 dépendances). Les tests de
#125, le nouveau test de l'allocateur Darwin et la sonde des six signatures
Darwin réussissent. Le
[run 38126286846](https://github.com/EnermaxFR/MaxPS4/actions/runs/38126286846)
valide aussi ABI CPU, pont natif, frontend Swift et traduction/repli interpréteur.

L'inventaire avant/après résout **43 références** et révèle **42 dépendances
transitives supplémentaires**, soit 120 imports restants : **102 imports du SDK**
et **18 symboles FEXCore**. La vraie liaison stricte résout les imports du SDK et
échoue précisément sur ces 18 symboles :

| Symboles effectivement manquants au linker | Nombre | Propriétaire à porter/compiler |
| --- | ---: | --- |
| `Config::Value<bool>::GetIfExists`, `Config::Value<uint64_t>::GetIfExists` | 2 | `Interface/Config/Config.cpp` |
| `Config::detail::{HIDEHYBRID,DISABLEL2CACHE,DYNAMICL1CACHE,HIDEHYPERVISORBIT,ENABLECODECACHINGWIP,DYNAMICL1CACHEDECREASECOUNTHEURISTIC,DYNAMICL1CACHEINCREASECOUNTHEURISTIC}` | 7 | `Interface/Config/Config.cpp` et ses données générées |
| `ContextImpl::{CompileBlock,ClearCodeCache,MonoBackpatcherWrite,ThreadRemoveCodeEntryFromJit}` | 4 | `Interface/Context/Context.cpp`, `Interface/Core/Core.cpp` et leur runtime |
| `Allocator::{VirtualName,mmap,munmap}` | 3 | `Utils/Allocator.cpp` et son backend de mémoire virtuelle Darwin |
| `CPU::InterpreterOps::GetFallbackHandler` | 1 | `Interface/Core/Interpreter/Fallbacks/InterpreterFallbacks.cpp` et ses dépendances |
| `Telemetry::TelemetryValues` | 1 | `Utils/Telemetry.cpp` et ses dépendances |

Les noms exacts/signatures, les objets demandeurs et l'erreur du linker sont
conservés dans `strict-link.log`, dans l'artefact `heavy-fexcore-source-inventory`
du run ci-dessus. `link-report.json` contient aussi les 120 imports agrégés et
leurs propriétaires candidats. Ces 18 blocages subsistent ; ils n'ont pas été
supprimés par des symboles factices ou par une option permissive du linker.

## Liaison complète du runtime Darwin cohérent

La branche `codex/fexcore-ios-linkage` part de `main` à `73ed0f0`.
La nouvelle cible `FEXCore_shared` utilise seulement le fork Darwin embarqué
par AetherPS4 et les dépendances CMake correspondantes. Elle ne transforme pas
les objets génériques en objets Darwin et n'intègre pas encore ce runtime à l'app.

Au commit `4f16882`, le run [38127386275](https://github.com/EnermaxFR/MaxPS4/actions/runs/38127386275)
a produit une véritable bibliothèque `libFEXCore.dylib` iOS ARM64, liaison stricte
réussie, aucun import FEXCore/LogMan manquant. Les 18 symboles bloquants du
diagnostic générique sont définis dans ce binaire. Ses 208 imports SDK sont liés
à libSystem et libc++. Les tests existants passent également : heavy
[38127386265](https://github.com/EnermaxFR/MaxPS4/actions/runs/38127386265), smoke
[38127386272](https://github.com/EnermaxFR/MaxPS4/actions/runs/38127386272).

Le rapport principal est `docs/fexcore-ios-linkage-report.md`. La cible complète
est distincte du diagnostic générique `BLOCKED` décrit plus haut. Elle cible iOS
18 pour les API de mutex du fork. Aucun test appareil ni IPA n'a été exécuté/produit.
