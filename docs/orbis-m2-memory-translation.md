# Orbis M2 — traduction mémoire FEX : blocage structurel

## Implémentation guest→host du 15 octobre 2026 (suite)

Le patch versionné `patches/fexcore-guest-memory.patch` modifie réellement le
FEX Darwin épinglé à `3347fc13`, dans son checkout de compilation. Le script
`patch-fexcore-memory-translation.py` vérifie le patch entier avant application,
accepte sa réapplication exacte et refuse les sources divergentes sans écriture
partielle. Les builds Linux, Darwin, iOS et SideStore appliquent le même patch.

Le fetch lit chaque octet dans le domaine guest, y compris les immédiats qui
traversent deux mappings hôtes et les cibles de multiblock non contiguës. Les
copies debug/SMC sont traduites aussi ; le dispatch SMC `ValidateCode` reste
refusé tant que sa vérification runtime n'est pas traduite.

Avant RA, `Core.cpp` insère des opérateurs IR dédiés aux adresses de données et
aux adresses atomiques. Le résultat host est une valeur SSA allouée par FEX,
séparée des registres guest : pas d'écrasement du registre d'adresse ni de
pointeur laissé dans TMP1–TMP4 pendant CASPair. `MemoryOps.cpp` émet le calcul
complet de l'adresse effective, puis une sélection de plage avec contrôle de
largeur et permissions. NZCV est sauvegardé/restauré. `AtomicOps.cpp` exige R+W
et l'alignement naturel de l'opérande entier avant les séquences LSE/exclusives.
La mémoire du contexte FEX, les spills et les registres ne sont pas traduits.

Couverture implémentée : loads/stores scalaires et vecteurs contigus, TSO,
paires, éléments/broadcast et non-temporal ; CAS 8/16/32/64, CASPair 64/128,
swap et fetch add/sub/and/clr/or/xor/neg. Les chemins spécialisés pile,
memcpy/memset, masked/gather et cache ne sont pas encore raccordés : la
compilation translated les refuse explicitement. Les accès natifs ne sont
admis que si l'opérande entier appartient à une même plage host contiguë.
Un accès de données invalide produit actuellement un trap ARM64 dédié ; la
conversion en faute guest récupérable reste à implémenter. Aucun programme
arbitraire n'est autorisé par cette étape.

`OrbisV4Translation` possède les mappings derrière un verrou. Une lease garde
le verrou du snapshot jusqu'à la fin du décodage/dispatch ; reserve/map/protect/
write/release/clear prennent le même verrou et appellent l'invalidateur AVANT
la mutation. L'invalidateur doit vider les caches de chaque thread FEX associé.
Le test lié exerce les caches de deux threads, le remplacement du code au même
RIP guest et l'attente d'un release concurrent pendant une lease.

La nouvelle suite `FEXGuestTranslationTest.cpp` exécute le vrai frontend, IR,
RA et JIT du FEX épinglé. Elle est compilée pour iOS et exécutée sur les runners
hôtes Linux/macOS, sous timeout externe. Sur x86, le simulateur ARM64 VIXL est
explicitement identifié, avec sa limitation connue des moniteurs exclusifs.
Il ne démontre pas les propriétés de concurrence du matériel ARM64.

Résultats locaux (Linux x86_64, Clang 19.1.7, FEX épinglé + VIXL épinglé) :

- Suite liée complète `scripts/test-ps4-fex-linked.sh` : succès, comprenant la
  nouvelle traduction et les régressions libkernel/framebuffer existantes.
- Nouvelle suite : CAS et XADD 8/16/32/64 bits (succès/mismatch), CASPair 64/128,
  swap, opérations ALU verrouillées et NEG, NZCV, FS/GS, offsets négatifs et
  indexés, remplacement de code, deux caches de threads, fetch entre mappings
  non contigus et cible de multiblock ; chemins LSE et exclusifs exécutés.
- Tests mémoire V4 C++17 stricts : succès. ASan/UBSan : succès local avec
  `detect_leaks=0` ; aucune preuve locale LeakSanitizer n'est revendiquée.
- Une première configuration avec GCC a échoué : FEX impose Clang. La relance
  utilise Clang 19 extrait dans le workspace. Un lancement local concurrent de
  builds a aussi échoué avec `Text file busy` ; le script complet a ensuite été
  relancé séquentiellement avec succès.
- La tentative locale de compilation iOS échoue explicitement avec code 127 :
  `xcrun: command not found`. Cet hôte Linux ne possède pas Xcode/SDK iOS.
- Les workflows mémoire V4, services/FEX, liaison Darwin/iOS et IPA SideStore
  ont été déclenchés sur la branche expérimentale. Au premier contrôle, leurs
  jobs sont `queued`, sans étape exécutée ; aucun résultat CI/IPA de ce nouveau
  patch n'est encore acquis. Les liens/résultats finaux sont consignés dans la PR.

**Limites :** le raccordement au runtime M2 public reste désactivé, car ses
chemins mémoire généraux et ses fautes ne sont pas encore tous couverts. L'API
synthétique existante conserve son refus `TRANSLATION_REQUIRED`. Cette étape
livre une traduction réellement exécutée dans FEX pour le sous-ensemble testé,
pas la résolution complète de M2 ou de l'erreur iPhone `-48`. Aucun iPhone n'est
disponible ; une IPA construite et des tests CI ne prouvent pas une exécution
sur appareil. Les paragraphes suivants décrivent l'état historique précédent.

## Résultat et provenance

Travail issu de `integration/orbis-v4-safe-staging` à `f7a4806`, pour #202.
La PR #203 (`f999cad3`) a été examinée comme référence : son diagnostic iPhone
`m1-ios-collision-203-v1`, build `38719373805`, retourne toujours `-48`
(`direct_mapping_collision`). Ses tests non-écrasement/rollback/retry sont
adaptés au contrat V4, sans importer la branche M1 ni modifier ses API.

**La collision iPhone et la traduction générale restent non résolues.**
L'issue autorise un diagnostic reproductible et une étape fail-closed lorsque
les contraintes FEX empêchent l'exécution sûre. Cette livraison applique cette
limite ; elle n'annonce aucune exécution CPU M2.

## Reprise du 15 octobre 2026 : CAS C++17 et validation actuelle

Le head inspecté est `e9d9b9570de5644b3a619d3ffb5d6552a3b8bc43`.
La commande C++17 du workflow `orbis-v4-memory.yml` échoue sur ce head :
`std::atomic_ref` exige C++20. Les résultats CI historiques ci-dessous ne
valident donc pas les derniers ajouts CAS.

Le commit `73f645fed5eea1dc6c28321f69c4774da92442fc` remplace `atomic_ref`
par le builtin Clang/GCC `__atomic_compare_exchange_n`, fort et séquentiellement
cohérent. Une assertion de compilation exige une largeur nativement lock-free ;
l'alignement hôte est vérifié sur la largeur entière avant l'accès. Les contrôles
V4 de permissions R+W, de durée de vie et de frontière de page sont conservés.
Ce correctif ne raccorde pas le CAS à `AtomicOps.cpp`.

Validation locale Linux x86_64, GCC 14.2.0 :

- Compilation C++17 stricte et exécution de `OrbisV4AddressSpaceTest` : succès.
- ASan/UBSan avec `ASAN_OPTIONS=detect_leaks=0` : succès. L'exécution avec
  LeakSanitizer activé échoue explicitement à cause de ptrace ; aucune validation
  locale des fuites n'est revendiquée. La CI conserve LeakSanitizer activé.
- CAS 8/16/32/64 bits au dernier emplacement d'une page : succès, mismatch avec
  mise à jour de expected, valeur mémoire inchangée au mismatch, refus sur page
  seulement inscriptible et après release. Les tests existants d'alignement,
  page RX, copie entre pages et adresses invalides restent exécutés.

Le push sur la branche expérimentale a déclenché notamment :

- [Mémoire V4](https://github.com/EnermaxFR/MaxPS4/actions/runs/38737972087).
- [Compilation/liaison FEX iOS ARM64 et runtime macOS](https://github.com/EnermaxFR/MaxPS4/actions/runs/38737972135).
- [IPA SideStore](https://github.com/EnermaxFR/MaxPS4/actions/runs/38737972212).
- [IPA Orbis Test distincte](https://github.com/EnermaxFR/MaxPS4/actions/runs/38737972308).

À la rédaction, ces exécutions sont en attente (`queued`), sans résultat ni
nouvel artefact validé. Elles portent sur `73f645f`, pas sur une implémentation
de traduction générale. L'environnement local est Linux sans Xcode/SDK iOS ;
il ne peut pas compiler FEX pour iOS. L'utilisateur confirme qu'aucun iPhone
n'est disponible pour cette validation. Aucun essai matériel n'a été effectué.

Le checkout réel du FEX épinglé confirme les points supplémentaires à traiter
avant tout dispatch translated :

- `Core.cpp:555` (cacheabilité), `:582` (fetch), `:649` (désassemblage debug),
  `:680` (copie pour ValidateCode) utilisent des pointeurs directs.
- `Frontend.cpp:1461` reconstruit le flux par arithmétique de pointeur pour chaque
  bloc ; traduire seulement l'entrée ne couvre pas les cibles non contiguës.
- `MemoryOps.cpp` contient aussi les paires, TSO, opérations vectorielles
  masquées/gather/élément/broadcast, pile, memcpy/memset, caches et non-temporal.
  Les loads/stores de contexte FEX et spills sont hôtes et ne doivent pas passer
  dans la table d'adresses PS4. Les adresses effectives incluent les offsets.
- `AtomicOps.cpp` comprend CASPair, CAS, swap et fetch add/sub/and/clr/or/xor/neg.
  CASPair réutilise TMP1 à TMP4 : un pointeur traduit placé sans précaution dans
  un de ces temporaires serait écrasé. Préserver NZCV, registres vivants et ordre
  mémoire fait partie du raccordement, pas seulement du traducteur C++.
- `HLE/SyscallHandler.h:62` fournit une implémentation vide par défaut de
  `InvalidateGuestCodeRange`. L'existence de cette méthode ne prouve pas
  l'invalidation des blocs Orbis après protect/release/remap.

Les modifications du décodeur, de MemoryOps et d'AtomicOps, la synchronisation
des mappings et l'invalidation JIT ne sont pas livrées par cette reprise.
La mission de traduction générale reste incomplète ; `-48` n'est pas déclaré
corrigé. Aucun dispatch translated supplémentaire n'est activé.

## Audit du backend épinglé

Sources : `Leviidev/AetherPS4`, commit
`3347fc13f19860855ce91b66356940989ea6b6a6`, sous
`runtime/sources/fexcore-darwin/`. Audit effectué sur le checkout réel.

| Chemin dans FEXCore | Observation | Conséquence |
| --- | --- | --- |
| `Source/Interface/Core/Core.cpp`, `GenerateIR`, vers 555–587 | `GuestRIP` est converti en pointeur pour le décodage | Déplacer uniquement le stockage ne traduit pas le fetch |
| `Source/Interface/Core/Frontend.cpp`, `AdjustAddrForSpecialRegion`, vers 1344–1364 | Ajustement spécial vsyscall, sinon flux d'instructions direct | Pas de table de pages Orbis générale |
| `Source/Interface/Core/JIT/MemoryOps.cpp`, `LoadMem` vers 704, `StoreMem` vers 1639 | `GetReg(Op->Addr)` devient opérande des instructions ARM64 ; chemins GPR, vecteur, SVE et TSO | Le JIT ne consulte pas `PS4DynamicMemory::translate` |
| `Source/Interface/Core/JIT/AtomicOps.cpp` | CAS/LSE et exclusives utilisent aussi les adresses mémoire directement | Ajouter un callback scalaire ne suffit pas ; atomicité et traversées de pages à concevoir |
| `include/FEXCore/HLE/SyscallHandler.h` | `QueryGuestExecutableRange` fournit base/taille/writable | Métadonnées d'exécutabilité, pas callback de traduction des loads/stores |
| `Source/Interface/Core/LookupCache.h`, `GuestToHostMap` | Cache guest RIP → blocs ARM64 compilés | Ce nom ne désigne pas une traduction des pages de données |

Dans MaxPS4, `executeImage` rebase RIP et utilise une pile hôte privée pour un
jeu d'instructions déjà admis. Ce chemin V3 ne résout pas les pointeurs absolus,
FS/TLS ni les adresses PS4 générales et n'est pas utilisé par la nouvelle API.
Changer RIP seulement, appliquer un décalage global ou renvoyer un pointeur
traduit depuis `QueryGuestExecutableRange` ne règle pas tous ces accès.

## Contrat et cycle de vie

`GuestAddressSpace` possède les réservations et leurs deux gardes PROT_NONE.
Les adresses invitées translated sont distinctes des pointeurs hôtes ; read/write
valident la totalité d'une plage avant copie, y compris aux frontières.
`PS4DynamicMemory` expose des pointeurs empruntés, invalidés par release/clear,
et ne synchronise pas les accès : l'appelant sérialise la durée de vie.
Le budget est 32 Mio, les pages hôtes admises 4/16 Kio, les VA sous 2^47.
Les permissions sont ELF R=4/W=2/X=1. W+X est refusé ; RX invité est R hôte,
jamais du code x86 exécutable par ARM64. RW invité n'est pas exécutable.

Le backend direct exige guest=host et conserve les gardes. Darwin utilise
`vm_allocate(VM_FLAGS_FIXED)` **sans OVERWRITE**. Ailleurs, mmap utilise un hint
et libère tout résultat déplacé avant de retourner EADDRINUSE. Une collision ne
publie aucune région et ne consomme aucun budget. `reservation_failure()` expose
adresse invitée, octets avec gardes, code brut Mach/errno et erreur portable.
Sur POSIX, host_status=0 avec EADDRINUSE indique un hint déplacé ; sur Darwin,
le code brut est un statut Mach, pas errno. Le diagnostic est remis à zéro à
chaque reserve ; clear ne l'efface pas. Ne pas conclure à une collision pour
n'importe quelle erreur Mach : seul KERN_NO_SPACE est classé EADDRINUSE.

Le bridge vérifie backend, plage, permissions et égalité numérique avant
admission. Le gate publie ses deux pointeurs seulement après validation totale.
Les nouvelles réservations synthétiques appartiennent à un objet local :
libération à chaque retour, y compris exception d'allocation ; aucun pointeur
n'est conservé dans FEX. Aucun thread FEX n'est créé ni cache modifié par M2.

## API expérimentale C

`maxps4_fex_engine_v4_synthetic(engine, instruction_budget, report)` :

- pointeurs valides et budget 2 à 16 obligatoires, sinon EINVAL ;
- moteur non initialisé : EAGAIN / NOT_INITIALIZED ;
- moteur initialisé : stage `mov eax,42; hlt` en RX et pile distincte RW/NX,
  puis préflight direct obligatoire ;
- stockage translated : ENOTSUP / TRANSLATION_REQUIRED ; aucune exécution ;
- `cpu_valid`, `backend`, RAX/RIP/RSP restent zéro, jamais des valeurs attendues
  copiées dans un résultat CPU. Les deux champs de permissions décrivent le
  staging seulement. Une erreur de staging a sa propre raison.

Le budget ne constitue pas un timeout FEX. Avant d'activer un dispatch, il faudra
une vraie limite vérifiée et des états RAX/RIP/RSP observés. Ni HLT ni un nombre
d'instructions annoncé ne prouvent seuls une borne d'exécution native.
L'API n'utilise aucun fallback V3 et ne modifie pas les API existantes.

## Reproduction et couverture

Depuis la racine :

```sh
mkdir -p build/orbis-v4
c++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  MaxPS4-iOS/Tests/OrbisV4AddressSpaceTest.cpp -o build/orbis-v4/memory-test
build/orbis-v4/memory-test
c++ -std=c++17 -fsanitize=address,undefined -fno-omit-frame-pointer \
  MaxPS4-iOS/Tests/OrbisV4AddressSpaceTest.cpp -o build/orbis-v4/memory-sanitized
build/orbis-v4/memory-sanitized
```

Les tests couvrent translated/direct, collision avec allocation hôte réellement
occupée, non-écrasement, comptabilité, retry après libération, gardes via contrat,
W^X, code RX non modifiable via contrat, pile non-X, tailles nulles, dépassements,
traversées de permissions sans copie partielle et cycles de destruction.
Ils ne prouvent pas une gestion des fautes ou des permissions par un JIT traduit.
`FEXRuntimeIntegrationTest.cpp` teste la nouvelle C ABI avant/après initialisation,
plusieurs appels et plusieurs créations/destructions moteur, puis les régressions
CPU V3. Ce test est compilé pour iOS et exécuté sur le runtime hôte Darwin par le
workflow de liaison existant. CI mémoire : Linux/macOS avec ASan/UBSan.

Dans l'environnement cloud local, LeakSanitizer échoue sous ptrace ; les tests
ASan/UBSan ont été exécutés avec `ASAN_OPTIONS=detect_leaks=0`. Cette exception
locale ne désactive pas la détection de fuites dans la CI.

## Validation iPhone et prochaines étapes obligatoires

1. Capturer adresse, taille et statut Mach de la réservation M1 échouée dans un
   rapport appareil accessible ; le nouveau getter C++ n'est pas encore branché
   au rapport Swift M1 de #203. Aucun nouveau résultat matériel n'est disponible.
2. Concevoir dans FEX une traduction cohérente pour fetch, scalar/vector/TSO,
   atomiques, pile, FS/TLS, traversées de pages, fautes et invalidation de code,
   avec ownership et synchronisation des mappings. Tester ces familles réellement.
3. Vérifier sur iPhone le cycle JIT/signature/entitlements, W^X, nettoyage,
   destruction/recréation et répétitions ; ne pas modifier les allocations iOS
   occupées pour faire passer un test. Aucun bypass JIT ni RWX n'est ajouté.
4. Démontrer une exécution FEX synthétique bornée, état CPU et raison d'arrêt
   observés. Ensuite seulement réévaluer le fixture M1 multi-thread/TLS.

Une CI verte valide ses seuls tests. Une compilation iOS/IPA n'est pas un essai
sur appareil. Aucun ELF/PRX général, ABI TLS SCE complète ou jeu PS4 fonctionnel
n'est démontré ici. Ne pas fermer #202 comme résolue avec cette étape partielle.
