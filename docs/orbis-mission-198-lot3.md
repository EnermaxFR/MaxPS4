# Lot 3 — incrément mémoire/TLS, M1 général encore bloqué

Base distante : `21a5344c1e2c338d18d996759300f553c8eaedb5` sur
`experiment/orbis-mission-198`. Aucun changement de main, cible, draft ou UI.

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
Pas de NID ajouté. Le chemin CPU existant conserve ses refus de TLS dynamique.
PT_TLS multiples dans un module, alignements invalides/non pris en charge,
offsets hors bloc et absence de template sont explicitement rejetés.

**Scheduler :** non livré dans cet incrément. CreateThread de FEX existe, mais
les deux objets TLS testés ne sont pas deux threads CPU exécutés. Le scheduler
invité avec création/join/synchronisation et preuve CPU reste à développer.

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
Les preuves FEX directes précédentes ne constituent pas une preuve d'exécution
de ce TLS dynamique ni d'un backend traduit. M1 général reste incomplet.
Aucune exécution iPhone, aucun jeu PS4 jouable, aucun déchiffrement revendiqué.
