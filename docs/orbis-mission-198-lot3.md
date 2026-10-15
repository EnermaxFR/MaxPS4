# Lot 3 — incrément mémoire/TLS, M1 général encore bloqué

Base distante : `21a5344c1e2c338d18d996759300f553c8eaedb5` sur
`experiment/orbis-mission-198`. Aucun changement de main, cible, draft ou UI.

**Code validé : `23546d04c9c83a3c20c9a3bc0835ac844e157813`, 18 workflows
SUCCESS.** Cycles publiés : `743e687` (copies/stockage TLS), `7fd6a2c`
(registre non copiable), `1399e4a` (file CPU), `23546d0` (erreurs mémoire).

## Étude FEXCore avant changement

Le runtime Darwin utilise AetherPS4
`3347fc13f19860855ce91b66356940989ea6b6a6`, imposé par
`scripts/build-fexcore-darwin-ios.sh`. Sources consultées directement à ce SHA :

- [Context.h](https://github.com/Leviidev/AetherPS4/blob/3347fc13f19860855ce91b66356940989ea6b6a6/runtime/sources/fexcore-darwin/FEXCore/include/FEXCore/Core/Context.h) :
  CreateThread/DestroyThread/ExecuteThread et invalidation de cache, mais pas de
  hook public de traduction générale des adresses de données.
- [SyscallHandler.h](https://github.com/Leviidev/AetherPS4/blob/3347fc13f19860855ce91b66356940989ea6b6a6/runtime/sources/fexcore-darwin/FEXCore/include/FEXCore/HLE/SyscallHandler.h) :
  QueryGuestExecutableRange renseigne étendue/protection, pas le stockage hôte.
  Un handler de syscall ne remplace pas les accès ordinaires du JIT.
- [MemoryOps.cpp](https://github.com/Leviidev/AetherPS4/blob/3347fc13f19860855ce91b66356940989ea6b6a6/runtime/sources/fexcore-darwin/FEXCore/Source/Interface/Core/JIT/MemoryOps.cpp) :
  LoadMem extrait Op->Addr avec GetReg puis émet ldr/ldrb/ldrh directement via
  GenerateMemOperand ; LoadMemPair émet ldp sur cette adresse. Les chemins
  vectoriels ont aussi leurs propres émissions.
- [Frontend.cpp](https://github.com/Leviidev/AetherPS4/blob/3347fc13f19860855ce91b66356940989ea6b6a6/runtime/sources/fexcore-darwin/FEXCore/Source/Interface/Core/Frontend.cpp) :
  fetch/décodage via InstStream/AdjustedInstStream. Traduire les copies du
  chargeur ne traduit donc ni tout le fetch ni les accès du code généré.

**Blocage précis :** le backend complet nécessite des changements dans FEXCore
(fetch conservant RIP invité, traduction des accès scalaires/vectoriels,
atomiques/TSO, accès inter-pages, fautes, invalidation après protect/unmap et
identité d'espace dans les caches). Aucune API publique examinée ne suffit.
Un déplacement de RIP/RSP ou une réécriture des seules constantes de fixture
n'est pas une solution. Ce lot ne modifie pas FEXCore et ne revendique pas ce
backend. Le dispatch M1 refuse explicitement un GuestAddressSpace traduit.
Les particularités iOS W^X/JIT ne fournissent pas ce mécanisme de traduction.

## Code livré

GuestAddressSpace.read/write prévalident la totalité du span, puis copient les
régions adjacentes avec un tampon temporaire, y compris lors d'un recouvrement
de buffers. Un trou, dépassement, page RX ou changement de protection refusé
ne produit pas de copie partielle. Limite 32 Mio, pas de nouvelle page host X,
pas de MAP_FIXED ni modification des gardes/réservations existantes. La durée
de l'opération exige la sérialisation du propriétaire contre protect/release.

GuestTLSModules et GuestDynamicTLS ajoutent le stockage TLS dynamique côté
chargeur : templates PT_TLS validés par le parseur existant, IDs monotones non
recyclés, au plus 16 modules, allocation paresseuse par thread, .tdata copiée,
.tbss et padding zéro, alignement invité jusqu'à 16 Kio, gardes et rollback des
collisions, résolution (module,offset,length) vers une VA invitée. Deux objets
thread possèdent des blocs indépendants. Ajout après création possible ; unload
refusé pendant la vie des threads pour préserver leurs pointeurs. Destruction
libère les blocs. Le propriétaire sérialise et conserve registre/espace vivants.

**Limite ABI/TLS :** c'est un service de stockage et résolution C++, pas une
implémentation du DTV SCE, d'un import __tls_get_addr ou de relocations TLS PRX.
Pas de NID ajouté. Les chemins CPU généraux conservent leurs refus d'ABI TLS
dynamique ; seule la convention de payload du harness est admise ci-dessous.
PT_TLS multiples dans un module, alignements invalides/non pris en charge,
offsets hors bloc et absence de template sont explicitement rejetés.

**Deuxième incrément :** l'API expérimentale run_m1_threads crée jusqu'à quatre
contextes CPU FEX avant toute exécution, avec piles et TLS distincts. Une file
déterministe les exécute jusqu'à HLT puis les joint/détruit dans l'ordre des IDs.
Les erreurs interrompent la file et détruisent aussi les contextes encore ready ;
aucun rapport partiel n'est publié. Le libkernel partagé attache les identités
et conserve les mutex entre threads. Seuls trylock/unlock, déjà vérifiés dans
la table du dépôt, sont ajoutés à l'admission M1 (aucun nouveau NID).

Le harness consomme des PRX plaintext sans PT_DYNAMIC, uniquement leurs
templates PT_TLS ; toute table dynamique/relocation est refusée. Les fixtures
accèdent aux payloads via offsets FS positifs propres au harness, validés contre
les spans exacts des modules du thread ; un span d'un autre thread est refusé.
Cela ne revendique pas les conventions DTV/SCE. La racine du harness impose
16 octets de TLS dont 8 initialisés, et les modules ont 8 octets initialisés
suivis d'au moins 8 octets zéro. L'API C de fixture reste isolée de l'interface.

Les logs du code `23546d04c9c83a3c20c9a3bc0835ac844e157813` établissent deux
threads ayant chacun TLS racine 17→22 et modules 83→84 / 129→131,
RAX/mémoire 131, zones zéro, IDs 1/2 retournés par scePthreadSelf et retours
mutex 0/0. Tous les contextes et blocs coexistent avant le premier dispatch.
Deux répétitions puis une collision de la deuxième pile et un retry testent
le rollback après création du premier contexte. Les bases FS distinctes sont
`0x300000420010` et `0x300004420010`. Le runtime ne substitue pas les résultats.
Les erreurs des copies mémoire sont propagées telles quelles ; le transfert
d'ownership CPU reste sûr aussi lors d'une exception avant/après consommation.

| Preuve sur le code validé | Résultat |
| --- | --- |
| [FEX Linux ARM64 natif / VIXL ARM64 sur x86](https://github.com/EnermaxFR/MaxPS4/actions/runs/38592861055) | Jobs FEX réussis, 4 lignes CPU multi-thread par backend et retry réussi |
| [FEX macOS ARM64 + Swift/C/Metal + IPA](https://github.com/EnermaxFR/MaxPS4/actions/runs/38592860868) | SUCCESS, mêmes sorties multi-thread réelles |
| [Mémoire Linux/macOS ASan/UBSan](https://github.com/EnermaxFR/MaxPS4/actions/runs/38592860827) | SUCCESS, gardes, admission et copies/TLS |
| [Linkage strict complet macOS/iOS ARM64](https://github.com/EnermaxFR/MaxPS4/actions/runs/38592871776) | SUCCESS ; compilation iOS distincte de l'exécution macOS |
| [PKG/PFS/ELF/PRX](https://github.com/EnermaxFR/MaxPS4/actions/runs/38592860594) | SUCCESS |

[IPA Orbis Test expérimentale](https://github.com/EnermaxFR/MaxPS4/actions/runs/38592860868/artifacts/11908598108),
rétention jusqu'au 28 octobre 2026. SHA256 du fichier IPA (pas du ZIP artifact) :
`fb8749deeb476d47a4cf5d084406bab5fa0ab7f202bd093523f091a929526c36`.
SideStore doit re-signer l'app et son framework. Les nouvelles fixtures ELF/PRX
sont dans l'[artefact FEX ARM64 natif](https://github.com/EnermaxFR/MaxPS4/actions/runs/38592861055/artifacts/11907849523)
(`m1-bundle/`) ; les logs Apple sont dans les
[diagnostics](https://github.com/EnermaxFR/MaxPS4/actions/runs/38592860868/artifacts/11908284161).
[Extraits de preuve](orbis-mission-198-lot3-ci-excerpts.txt).

**Scheduler général encore absent :** pas de préemption, attente bloquante,
création/join par appels SCE invités ni gestion pthread générale. La file est
un modèle hôte fini de création/exécution/join, pas un remplacement du scheduler
nécessaire aux programmes PS4 arbitraires. Aucun thread x86 n'est lancé comme
fonction pthread hôte.

## Validation locale et limites

Exécuteur Linux x86-64, g++ 14, copies partielles de sources via connecteur
GitHub (clone bloqué : proxy:8080 inaccessible). ASAN_OPTIONS=detect_leaks=0
uniquement pour l'exécuteur local ; ASan et UBSan actifs. Aucun SDK Apple,
FEXCore compilé ou appareil iPhone dans cet exécuteur.

- Suite mission 198 : PASS, dont gardes par fautes réelles, mapping direct
  avec collision, TLS statique, mémoire M1 sans/avec sanitizers et nouveau test.
- GuestDynamicTLSTest : PASS ASan/UBSan ; mémoire absolue copiée puis suivie,
  spans inter-pages/régions, holes/RX/overflow sans copie partielle, release,
  isolation 2 threads et 3 modules initialisés 17/83/129, tail zéro, ID stable,
  ajout tardif, collision/rollback, alignement 16 Kio, unload busy, IDs non
  recyclés, template absent/malformé, limite 16 modules et base invalide.
  Une première fixture demandait 16 Kio sur une adresse PT_TLS non alignée :
  rejet correct ; fixture corrigée avec adresse/offset/initialisation cohérents.

Les résultats CI exacts sont rapportés séparément dans la conversation #199.
- Suite orbis-v2 : PASS après récupération de la ressource Stik absente de la
  copie partielle initiale ; PKG/PFS sparse >2/4 Gio, 3368 checks du linker,
  métadonnées SCE/PRX, répertoire jeu, préparation V2 et stack.
- Suite ps4-system-services : PASS ASan/UBSan (14 suites C++ et 2 ABI C).
Les preuves du lot 2 ne constituaient pas une preuve de TLS multi-modules.
Le lot 3 exécute réellement les payloads du harness décrits ci-dessus ; cela
ne prouve pas l'ABI TLS SCE ni un backend traduit. M1 général reste incomplet.
Aucune exécution iPhone, aucun jeu PS4 jouable, aucun déchiffrement revendiqué.

## Suivi du 15 octobre — rollback TLS après échec d'allocation

L'inspection locale au SHA AetherPS4 épinglé ci-dessus confirme les accès directs
`GetReg(Op->Addr)`/`GenerateMemOperand` dans MemoryOps.cpp et le fetch
`AdjustedInstStream` dans Frontend.cpp. Le blocage de traduction FEX générale
reste applicable ; ce correctif ne change pas le backend CPU.

`GuestDynamicTLS::resolve` nettoie désormais l'entrée de module et toute
réservation acquise quand les allocations de bookkeeping C++ échouent.
`std::bad_alloc` devient `-ENOMEM`, avec adresse de sortie zéro ; les autres
exceptions sont propagées après rollback. Auparavant une exception pendant
reserve/map pouvait laisser une entrée de bloc zéro, une réservation persistante
ou une exception non gérée à la place du code d'erreur attendu.

`GuestDynamicTLSAllocationTest` injecte un échec sur chaque allocation de resolve,
vérifie l'absence de région/réservation résiduelle et retente la résolution dans
le même thread, puis contrôle .tdata/.tbss et la destruction. Le test est intégré
à la suite mission 198 ASan/UBSan Linux/macOS. Le code précédent termine sur une
exception non gérée avec ce test. Ce test de stockage ne prouve aucune nouvelle
exécution FEX, convention TLS SCE, compilation iOS ou exécution iPhone.
