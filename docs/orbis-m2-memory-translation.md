# Orbis M2 — traduction mémoire FEX : blocage structurel

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
