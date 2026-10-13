# Première chaîne Orbis ELF → FEX → libkernel

## Résultat démontré et périmètre

Travail effectué exclusivement sur `codex/ps4-memory-translation-contract`, à
partir de `14d38c0dac4718989c6bf4e9f4d125b9b62c30c6`. À l'inspection initiale,
GitHub indiquait que la PR #170 était **déjà fusionnée**. Aucun changement de
`main`, fusion de PR ou changement de son statut n'a été effectué ici.

Deux ELF **synthétiques Orbis x86-64** sont chargés, liés, traduits par le vrai
FEXCore épinglé et exécutés jusqu'au HLT final. Les appels passent par des
trampolines x86-64, le handler FEX `OS_GENERIC` et les services existants.
Les valeurs de retour proviennent réellement de libkernel ; elles ne sont pas
précalculées par un interpréteur ou injectées comme résultats attendus.

**La preuve locale utilise le simulateur d'instructions ARM64 VIXL de FEX sur
Debian x86-64, backend = 3.** Il exécute les instructions ARM64 générées par FEX,
y compris les transitions vers les vrais services C++. Ce résultat ne prouve
ni un JIT ARM64 natif (backend = 2), ni l'exécution sur iPhone. Le repli
interpréteur existant reste backend = 1 et n'est jamais utilisé par ce chemin.

Ce n'est pas encore un chargeur/exécuteur universel PS4. L'adressage absolu
arbitraire, les PRX réels, TLS et Kero Blaster restent bloqués.

## Audit : fonctionnalités réutilisées

- `PS4ExecutableImage` : identité Orbis, validation des segments et SELF clair.
- `GuestELF` / `PS4GuestMemory` : mappings détenus, copies vérifiées, permissions
  logiques R/W/X, budgets et invalidation des mappings lors du restaging.
- `PS4Libkernel` : `/app0/` en lecture seule, open/read/close/lseek/stat/fstat,
  erreurs Orbis, horloges et opérations mémoire.
- `PS4VirtualMemory` : mappings anonymes logiques, protections/démappages.
- `PS4Threads` : identités invitées et mutex non bloquants avec propriétaires.
- Runtime FEX existant : configuration, autorisation JIT iOS, dispatcher,
  contexte CPU, pile call/ret, purge de cache et destruction des threads.

Ces composants n'ont pas été réimplémentés. Le nouveau linker et le handler
les raccordent. Les anciens chemins ELF statique, staging PS4, interpréteur et
PM4 restent disponibles.

Un ancien échec Actions a été examiné : run `38322817715`, job Ubuntu
`115054583902`, assertion de fetch dans `PS4GuestMemoryTest.cpp:47`. Le test
échouait sur un ancien commit, `52bb8388`. Le test actuel et les autres tests
mémoire existants passent localement. Les runs du head inspectés au début
étaient en attente ; ils n'ont pas été présentés comme réussis.

## Linker et admission

`PS4DynamicLinker` accepte les PT_LOAD / PT_DYNAMIC d'un ELF Orbis clair avec
tables **standard DT_***, hash SysV, symboles ELF64 et RELA. La dépendance
disponible pour ce premier contrat de démonstration est `libkernel`, version 1.
Les noms `sceKernel*` / `scePthread*` et les NIDs nus du registre existant sont
résolus. Les tags SCE et identifiants encodés `NID#library#module` sont refusés,
pas interprétés comme si leur bibliothèque/version avait été décodée.

Relocations prises en charge dans le modèle à adresses logiques fixes et biais
zéro : NONE, 64, PC32, GLOB_DAT, JUMP_SLOT, RELATIVE, 32 et 32S. Les tailles,
indices, addends, alignements, permissions des destinations, chevauchements et
dépassements signés/non signés sont vérifiés. Les GOT importées reçoivent des
**adresses de trampolines invitées**, jamais des pointeurs de fonction ARM64.
Les imports essentiels inconnus provoquent `-ENOENT` et un diagnostic nominatif
dans le linker. Les tables, types de symboles, dépendances ou relocations hors
périmètre provoquent un refus. DT_INIT/FINI, arrays et flags non pris en charge
sont refusés : aucune initialisation de module n'est silencieusement sautée.

Le programme complet est admis avant le mapping/exécution : immédiats GPR hors
RSP, transfert RAX vers RDI/RSI, ADD EAX, NOP, stores RIP-relatifs vérifiés,
appels aux seules GOT résolues, préparation bornée d'un argument de pile et HLT
final. Le budget inclut les instructions des trampolines et leur padding.
Instructions arbitraires, branches, syscalls directs et accès mémoire génériques
sont refusés. Les registres écrits en mémoire doivent avoir une origine prouvée,
et RSP/RCX ne peuvent pas divulguer les pointeurs CPU privés.

Les CALL indirects importés sont réécrits en CALL relatifs directs + NOP de même
longueur, vers les trampolines invités. Cela conserve les offsets du programme
dans l'arène rebased de FEX. **Le blocker CPU_ADDRESS_SPACE_REQUIRED reste 4** :
cette optimisation bornée ne constitue pas une traduction générale des adresses
virtuelles PS4. Les pages x86 restent des données non exécutables sur l'hôte ;
le code JIT ARM64 reste séparé et soumis au fournisseur iOS existant.

## ABI et cycle de vie

Un trampoline conserve le quatrième argument RCX dans R10, place un ID de
service opaque dans EAX puis exécute SYSCALL / RET. Le handler vérifie le site
SYSCALL et l'ID dans le contexte CPU réel. En `OS_GENERIC`, il écrit **RAX et le
RIP de reprise** dans le frame FEX ; renvoyer une valeur C++ seule serait
insuffisant. RBX, RBP et R12–R15 sont conservés et vérifiés par exécution.

Le CPU utilise une pile hôte privée entourée de guard pages. Son RSP et ses
adresses de retour ne sont jamais publiés comme pointeurs invités. Seul le
septième argument, lu dans les limites prouvées de cette pile, est copié dans
un frame à adresse logique pour `sceKernelMmap`. RCX doit être explicitement
initialisé avant mmap ; les services sans quatrième argument reçoivent zéro
dans ce champ plutôt qu'un RIP CPU privé laissé par SYSCALL. Les arguments
pointeurs et chaînes sont ensuite contrôlés par les services existants.

Chaque exécution crée/détruit un contexte FEX invité indépendant et ses piles.
Le verrou du runtime couvre staging, liaison, exécution, services et nettoyage.
Les métadonnées exécutables/callbacks sont retirées en sortie ; le cache FEX est
purgé avant chaque dispatch. Un restaging refusé supprime les imports/mappings,
ferme les descripteurs et retire `/app0/`. Aucune racine de fichiers iPhone
n'est montée automatiquement par l'importeur Swift.

**TLS et threads :** seul le thread bootstrap (identité 1) exécute ce chemin.
Les identités/mutex existants sont testés ; scePthreadCreate/join, ordonnanceur,
piles de threads concurrentes, FS/TLS par thread et initialisation PT_TLS restent
à construire. Un ELF avec PT_TLS est explicitement refusé. Une identité de
thread ou un mutex disponible ne constitue pas une preuve de threads CPU PS4.

## Démonstrateurs et reproduction

Les bytes sont construits dans `Tests/PS4LinkedELFFixture.h` et vérifiés dans
`Tests/PS4FEXLinkedExecutionTest.cpp` :

1. NID `aI+OeCz8xrQ` → scePthreadSelf → retour 1 → ADD EAX,1 → store 2 → HLT.
   Trois exécutions consécutives vérifient le même résultat et un appel réel.
2. open `/app0/fixture` → read `PS4!` → close → horloge monotone → mmap.
   Deux exécutions vérifient le descripteur invité 3, le retour read 4, les
   données, le timespec, les retours 0 et l'adresse de l'allocation logique.
   Le quatrième et le septième argument de mmap sont vérifiés par ce résultat.
   L'accès à l'allocation est ensuite contrôlé par les API mémoire de l'hôte ;
   les loads arbitraires depuis cette allocation ne sont pas encore admis au CPU.

Test local / CI Linux :

```sh
# Clang, CMake, Ninja ; checkout Aether épinglé et VIXL épinglé sur hôte x86.
bash scripts/test-ps4-fex-linked.sh AETHER_CHECKOUT BUILD_DIR
bash scripts/test-ps4-system-services.sh
```

Révisions requises : Aether `3347fc13f19860855ce91b66356940989ea6b6a6`,
VIXL `5f418449c48f6ca3ad37c47ec632f17a561c0580`. Sur x86, l'ABI debug FEX requiert
AVX et les wrappers VIXL pour les appels C++ ; le script et le runtime configurent
ces deux contrats explicitement. Aucun symbole FEX de remplacement n'est fourni.

Le script exporte `ps4-libkernel-demo.elf`, `ps4-libkernel-combined.elf` et le
rapport `ps4-linked-tests.txt`. Le premier s'importe depuis les diagnostics
MaxPS4. Il ne possède aucun repli interpréteur : sans autorisation JIT effective,
l'application rapporte que l'exécution FEX est indisponible. Le deuxième requiert
un montage `/app0/` dédié fourni par le harness ; le picker Swift ne monte pas
automatiquement un répertoire de l'application.

## Validation et état Apple

Résultats locaux : Debian 13 x86-64, GCC 14 pour les tests C/C++ portables,
Clang 19 pour le vrai FEX/VIXL.

- Quatre suites services/linker ASan + UBSan réussies, avec débordements PC32,
  32/32S, tables/segments malformés, imports inconnus, GOT protégées et rollback.
- Huit suites existantes réussies sous ASan + UBSan : GuestMemoryRange,
  GuestELFInterpreter, GuestELFMemory, PS4GuestMemory, PS4GuestStaging,
  PS4StagedCPUAdmission, PS4ExecutableImage et GuestRIPStoreAdmission.
- Deux compilations ABI C11 réussies (mémoire et services/liaison/exécution).
- Test réel FEX/VIXL réussi : les deux ELF, registres callee-saved, répétitions,
  pointeur hôte refusé avec Orbis EFAULT, refus avant InitCore et teardown.
  Le runtime FEX/VIXL complet n'a pas été instrumenté ASan/UBSan dans ce test.
- Tests Python du patch dispatcher (3), audit signature iOS (3), syntaxe bash
  et parsing YAML des workflows modifiés réussis.

LeakSanitizer a aussi été tenté sans option spéciale et a échoué avec son
diagnostic « does not work under ptrace ». Les exécutions locales ASan/UBSan
utilisent donc `ASAN_OPTIONS=detect_leaks=0`. Cela ne valide pas l'absence de
fuites par LSan. Les workflows conservent les réglages sanitizer par défaut ;
aucun test n'a été désactivé pour les faire réussir.

Cette machine n'a ni `xcrun`, SDK iPhoneOS, Swift, Xcode ni matériel ARM64 natif.
**Compilation macOS/iOS ARM64, UI Swift et IPA : non validées localement.** Les
workflows testent Ubuntu/macOS, ajoutent le nouveau linker au compile iPhone,
construisent le test FEX natif macOS et le runtime iOS, et testent le nouvel
importeur via Swift. Les triggers push de cette branche permettent aussi la
construction IPA existante, sans fusion ni nouvelle PR. Un run en attente
n'est pas une compilation réussie. Aucune IPA produite ni validation SideStore
sur appareil n'est revendiquée ici. Les règles d'autorisation JIT et de signature
existantes restent obligatoires.

## Travail restant avant Kero Blaster

1. Réussir la CI Apple puis exécuter la démo sur iPhone ARM64 avec l'autorisation
   JIT réelle, vérifier les mêmes retours et l'isolation des pointeurs.
2. Construire l'adressage virtuel PS4 général dans le frontend et les accès
   mémoire générés par FEX, avec permissions et gestion sûre des fautes.
3. Décoder les tables SCE, les IDs/versions de bibliothèques, les dépendances
   PRX, les relocations supplémentaires et les initialisateurs contrôlés.
4. Initialiser TLS/FS, créer et arrêter les vrais threads CPU invités ; ajouter
   attente/join et synchronisation avec rollback de toutes leurs ressources.
5. Élargir l'admission et les services depuis les imports réellement observés
   dans le binaire de Kero Blaster, puis raccorder sa chaîne graphique.

**Kero Blaster n'est pas annoncé jouable.** Aucun binaire de ce jeu n'a été
exécuté et aucun rendu de jeu PS4 n'a été démontré.
