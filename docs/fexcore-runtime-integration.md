# Intégration progressive FEXCore au moteur natif

## Périmètre et architecture

Base : main `0d6d308a0425991c8a4b798d0e4c055b4aad796a`, après fusion de
#125 et #127. Branche `codex/fexcore-runtime-integration`, PR #128.
Aucune modification directe de main ni fusion automatique.

Le moteur actuel possède une ABI C (maxps4_native), un pont shadps4_bridge
avec interpréteur x86 restreint/traduction ARM64 en données, et un adaptateur
NativeGuestCpuBackend d'Aether qui teste uniquement des blocs synthétiques.
Il n'initialisait pas le runtime FEXCore.

Cette étape ajoute `MaxPS4FEXRuntime`, une vraie bibliothèque native contenant
le FEXCore Darwin cohérent validé dans #127, compilé statiquement avec son
FEXCore_Base, AllocatorHooks, fmt, xxhash, cephes et SoftFloat. Elle inclut
le pont natif et l'adaptateur CPU actuels ainsi que les sources Darwin réelles
HostFeatures/CPUInfo. Aucun header/objet FEX générique n'entre dans cette cible.
Elle ne charge pas un second libFEXCore.dylib ; ne pas lier simultanément deux
copies du runtime dans l'application.

La nouvelle ABI C ne transporte aucun type C++ FEX :

| Interface | Comportement |
| --- | --- |
| maxps4_fex_engine_open | initialise la configuration partagée, découvre les features Darwin, crée un vrai ContextImpl |
| maxps4_fex_engine_close | détruit le contexte avant de libérer la configuration du dernier moteur |
| maxps4_fex_engine_stage | CONTEXT_CREATED seulement ; ne signifie pas InitCore/JIT prêt |
| maxps4_fex_engine_cpuid | appelle réellement RunCPUIDFunction du contexte FEX |
| maxps4_fex_engine_run | conserve l'interpréteur borné, renvoie explicitement CPU_RESTRICTED_INTERPRETER |

Un verrou sérialise l'initialisation/destruction de la configuration FEX globale
et les interrogations CPUID. Le consommateur conserve chaque handle en vie
pendant les appels et sérialise sa fermeture. Ce manager possède exclusivement
la configuration FEX du processus : de futurs consommateurs doivent partager
ce propriétaire, pas appeler indépendamment Initialize/Shutdown.

L'appel à InitCore est volontairement différé : il alloue/écrit le dispatcher
JIT et utilise SignalDelegator. Créer un contexte/obtenir un CPUID n'est pas une
initialisation du dispatcher, une traduction de bloc ou une exécution JIT.
Les capacités de lancement PS4 restent nulles ; launch conserve ENOSYS.

## Builds et tests reproductibles sans IPA

Sur Mac ARM64/Xcode/CMake/Ninja, checkout AetherPS4 épinglé à
`3347fc13f19860855ce91b66356940989ea6b6a6` avec les répertoires
`runtime/sources/fexcore-darwin` et `src/core/guest_cpu` :

```bash
bash scripts/build-fexcore-darwin-ios.sh build/aether-link build/fex-ios --runtime
bash scripts/build-fexcore-darwin-ios.sh build/aether-link build/fex-host --host-runtime
```

iOS : sdk iphoneos, arm64, minimum iOS 18 ; builds réels du dylib natif et du
harness Mach-O, sans application/IPA. Mac : sdk macosx, arm64, minimum macOS 15 ;
le même harness est réellement exécuté. Les deux builds conservent la liaison
stricte de FEXCore_shared et l'ajout de FEXCore_Base de #127.
La bibliothèque native possède elle aussi une liaison stricte et un inventaire
nm/otool. Le build échoue s'il reste un import FEXCore/LogMan/maxps4 non résolu.

FEXRuntimeIntegrationTest vérifie trois cycles de création/fermeture, deux
contextes simultanés, interrogation CPUID réelle avant/après fermeture du
premier, rechargement après fermeture du dernier, ABI/capacités natives, dispatch
Aether existant, bloc x86 arithmétique retournant 42 via l'interpréteur, épuisement
du budget, instruction non supportée et arguments invalides. Il emploie des
contrôles explicites même si NDEBUG est défini, jamais de faux runtime.

Le workflow fexcore-ios-linkage exécute les deux jobs indépendamment. Les
workflows heavy et smoke restent actifs, avec leurs tests existants non désactivés.
BUILD_TESTING=OFF de FEX ne change pas ces tests ; le nouveau harness est construit
explicitement et exécuté sur macOS. Il n'est pas exécuté comme binaire iOS sur
le runner macOS.

## Mémoire exécutable et signaux : prochaine étape appareil

Les mécanismes du fork restent conservés : traductions RX↔RW avec offsets,
ScopedJITWriteProtect, invalidation sys_icache_invalidate, callbacks de réutilisation
avec token de verrouillage, safepoints autour de l'invalidation. Sur iOS,
BreakpointJIT est résolu dynamiquement et dépend d'une session debugger ; la
liaison n'en vérifie pas la présence. Sur macOS la garde W^X a un autre contrat.

L'initialisation de contexte actuelle ne lance pas InitCore, ne crée pas de thread
invité et ne déclenche pas les demandes de mappings JIT/BRK. Aucun handler de
signal factice n'est installé. Les contextes Darwin et les registres opaques/PAC
nécessitent une délégation de signaux réelle avant tout dispatcher.

Protocole préparé pour un iPhone réel :

1. Vérifier signature/get-task-allow, Developer Mode, OS compatible, debugger
   actif et disponibilité réelle de la session BreakpointJIT ; consigner ces faits.
2. Charger uniquement MaxPS4FEXRuntime signé, appeler open, relever stage et
   CPUID, fermer/réouvrir. Comparer le résultat au test hôte sans conclure au JIT.
3. Intégrer SignalDelegator/syscall handler et la gestion d'arrêt selon les vrais
   contrats Aether. Tester refus/erreurs de mappings et perte de debugger sans
   transformer les erreurs en succès ni installer un handler vide.
4. Tester allocation RX, alias RW, écriture via RW, changement/protection W^X,
   invalidation du cache, traduction des adresses et libération, séparément.
5. Alors seulement appeler InitCore, créer un thread avec stack/GDT/TLS et
   call/return stack réelles de la bonne taille de page hôte ; borner le code
   invité et la politique d'accès mémoire/syscalls. Configurer EnableExitOnHLT.
6. Émettre un bloc x86 minimal `B8 28 00 00 00 05 02 00 00 00 F4`
   (`mov eax,40; add eax,2; hlt`), compiler/exécuter via FEX, contrôler RAX=42
   et arrêt HLT. Ce résultat doit provenir de FEX, avec preuve du backend et
   sans repli interpréteur présenté comme exécution JIT.
7. Invalider/réutiliser le buffer, répéter avec plusieurs threads, vérifier les
   safepoints et les signaux avant toute intégration d'un loader PS4.

Aucun de ces résultats iPhone n'a été observé. Le bloc qui retourne 42 dans la
CI actuelle passe exclusivement par l'interpréteur. Voir aussi ios-executable-memory.md.

## Blocages conservés

- Le nouveau runtime est lié au moteur natif et à son harness dédié ; la cible
  de packaging Xcode/Swift existante n'est pas encore modifiée pour le charger.
- InitCore/dispatcher, threads invités, mémoire exécutable et signal delegation
  restent à intégrer avec un vrai test appareil. Une gestion des traps BRK et
  de la perte de session est indispensable avant activation.
- Le fork requiert ici iOS 18/macOS 15 pour os_sync ; support des OS antérieurs
  à porter/tester. Le pont existant continue à être compilé séparément pour iOS 16.
- Pas encore de loader ELF/SELF, de syscalls PS4/HLE ou d'intégration GPU dans ce
  moteur FEX. Aucun jeu PS4 validé ; aucune capacité de lancement annoncée.

## Commits et premiers résultats de validation

- `9b86698041cd71431b669bf193f9b4179db0b99d` : runtime natif, vraie
  création du contexte/CPUID, repli explicite, harness et matrice iOS/macOS.
- `0c0592c790757c8e4e1ff10c67713fef2ed55ea6` : corrige le chemin Bin/ du
  harness pour l'exécution macOS et sa conservation dans les artefacts.

Au premier run `38128560228`, les deux bibliothèques/harness se compilent et
se lient, y compris la bibliothèque native iOS ; le job macOS a échoué avant
exécution du test uniquement sur le chemin d'exécutable, corrigé ensuite.
Tests locaux conservés : patches Darwin 3/3, diagnostics 3/3, sept programmes
traduction/repli et primitives signaux/contention réussis ; header ABI accepté
en C, syntaxe Bash/YAML et git diff --check réussis. Les tests du vrai contexte
FEX exigent le runner Apple ; aucun résultat iPhone n'est déclaré.

Artefact natif iOS vérifié indépendamment du run initial :
https://github.com/MaxFRdev/MaxPS4/actions/runs/38128560228/artifacts/11698195702
`libMaxPS4FEXRuntime.dylib`, Mach-O arm64 MH_NOUNDEFS, aucun import FEXCore
ou maxps4 manquant. otool ne liste que libSystem/libc++ : le runtime FEX est
bien intégré statiquement, sans seconde bibliothèque FEX chargée.
SHA256 : `d17a6304d12ad0c8f86809fac28b915d290373f1018c3d19c87fe4c0873b91ee`.
Copie locale : `/workspace/artifacts/pr128-ios/libMaxPS4FEXRuntime.dylib`.
Aucun binaire généré committé.

Diagnostic runtime : `38128727724` reproduit un SIGSEGV à la première ouverture ;
`38128893834` localise l'accès nul dans memmove. Les conteneurs FEX utilisent
aligned_alloc(alignof(T), n*sizeof(T)), y compris alignof(char)/uint32_t.
L'ancien appel direct à aligned_alloc Darwin peut renvoyer null pour ces
alignements. Le correctif `4324a426b25b74fdc3075380deed22c297432eab` conserve
la vraie allocation et normalise l'alignement avant posix_memalign ; le test
lié à la même bibliothèque vérifie alignements 1/4/8/64, tailles non multiples,
écriture/libération et rejet des alignements invalides avant les contextes.
Le mécanisme de trace conserve un backtrace LLDB sur crash, sans rendre le
contrôle vert. Ce patch vise seulement l'implémentation libc-backed Darwin ;
les six adaptations et tests génériques existants restent intacts.

- `4a1cacd3324a4b2c6dcf4c81194f3fb99bf7a377` : journalisation du harness et
  capture LLDB des crashes tout en conservant l'échec comme résultat bloquant.
- `7c13f285ad661e90adb5442c90ecc5c39f5e78ad` : dépendance fmt épinglée
  nécessaire aux headers du vrai test allocateur ; ne lie aucun fmt macOS à iOS.

Le run `38129106711` a révélé cette dépendance d'en-têtes absente dans le nouveau
test ; elle est corrigée sans retirer le test ni changer ses vérifications.

## Validation du code corrigé

Commit `7c13f285ad661e90adb5442c90ecc5c39f5e78ad` : test macOS ARM64
réel réussi dans le run [38129274846](https://github.com/MaxFRdev/MaxPS4/actions/runs/38129274846).
L'allocateur corrigé permet trois cycles avec six contextes réels, CPUID
`GenuineIntel`, feuille maximale 22, contexte restant valide après fermeture
d'un autre, fermeture/réinitialisation globale, et repli interpréteur explicite.
Le crash initial est résolu ; aucune erreur n'a été transformée en succès.

Contrôles existants : [heavy 38129274859](https://github.com/MaxFRdev/MaxPS4/actions/runs/38129274859)
et [smoke 38129274851](https://github.com/MaxFRdev/MaxPS4/actions/runs/38129274851) réussis.

Extrait du harness réellement exécuté :

```text
BEGIN native ABI and real FEX context lifecycle test
OPEN contexts cycle=0
FEX context CPUID vendor=GenuineIntel leaf=22
OPEN contexts cycle=1
FEX context CPUID vendor=GenuineIntel leaf=22
OPEN contexts cycle=2
FEX context CPUID vendor=GenuineIntel leaf=22
PASS real FEX context lifecycle/CPUID and explicit interpreter fallback; FEX JIT NOT executed
```

Les artefacts contiennent runtime-tests.txt, les inventaires natifs et FEX,
les deux dylibs, l'archive FEX, le harness Mach-O et les logs/manifest de sources.
Les journaux LLDB restent conservés pour les exécutions en échec précédentes.
Le commit documentaire final ne modifie pas ce code validé.
Recherche locale *.ipa : aucun fichier. Aucun commit de fusion dans la branche.
Le frontend/packaging reste à intégrer ; l'exécution de code invité via FEX sur
iPhone demeure non validée.

Artefact **corrigé** iOS :
https://github.com/MaxFRdev/MaxPS4/actions/runs/38129274846/artifacts/11698457349
Téléchargé et vérifié : plateforme Mach-O iOS (2), arm64, minimum 18.0,
MH_NOUNDEFS, aucun import FEXCore/LogMan/maxps4 manquant ; seulement libSystem
et libc++ comme dépendances externes.
SHA256 du dylib natif corrigé : `3fb3e2eb8f326246faebd5cfed876dea0b903efbc60eed61f5bf32ee2f348770`.
Local : `/workspace/artifacts/pr128-validated/libMaxPS4FEXRuntime.dylib`.
Artefact tests hôte :
https://github.com/MaxFRdev/MaxPS4/actions/runs/38129274846/artifacts/11698956674
