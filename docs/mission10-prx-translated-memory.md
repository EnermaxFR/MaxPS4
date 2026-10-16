# Mission 10 — PRX vers mémoire traduite FEX

Audit et modifications locales du 16 octobre 2026 sur
`experiment/maxps4-ubuntu-codex`, HEAD `b031576`. Les changements non commités
des missions 8 et 9 sont conservés. Les tests ci-dessous portent sur le workspace
modifié, pas uniquement sur ce SHA. Aucun push ni workflow distant.

## Diagnostic du refus actuel

`OrbisPRXFEXBridge::map` possède une copie des segments PRX et de leurs
relocations, dans `GuestAddressSpace` avec backend `translated`. Les nombres
invités n'identifient pas les pointeurs du stockage hôte.

`admit_dispatch` appelle `OrbisV4MemoryBridge::admit_fex_direct`. Cette interface
exige un backend direct et une identité numérique invité/hôte : elle refuse
donc ces PRX avec `-ENOTSUP` et un pointeur de sortie nul. Après déchargement,
elle retourne `-ENOENT`. Ce refus est conservé.

`OrbisV4ExecutionGate` contrôle aussi un dispatch **direct**, pas une liaison
automatique de snapshots au traducteur FEX. Le diagnostic synthétique V4 public
reste explicitement bloqué. Le parcours iOS `prepare_orbis` / `run_orbis`
utilise le processus `staged_ps4` existant, et ne consomme pas le nouveau
transfert de mémoire PRX.

Le patch FEX épinglé possède déjà `UsesGuestMemoryTranslation`,
`GuestMemoryRanges`, une validation avant décodage, le fetch invité et des
opérations mémoire traduites. Le problème n'est donc pas l'absence totale
de traduction dans FEX ; il manque son contrat complet avec ce propriétaire PRX
et le lanceur de l'application.

| Interface | État et manque précis |
| --- | --- |
| Chargement / liaison PRX | Segments et relocations copiés transactionnellement ; TLS/RELRO non pris en charge par certaines voies de préparation restent refusés |
| PRX vers propriétaire traduit | Nouvelle copie possédée `make_translation`, transactionnelle, sans admission CPU |
| Propriétaire vers FEX | Ranges invité/hôte/longueur/permissions disponibles sous `Lease` ; consommés par le vrai FEX dans le nouveau test, pas encore par le lanceur iOS |
| Exécution / invalidation | Le bail doit englober validation, compilation et dispatch ; le callback doit invalider tous les caches concernés avant mutation/libération |
| Démarrage de processus | Choix et validation de l'entrée, pile/ABI, TLS, init/fini, services et contrôle des fautes non raccordés dans un lanceur PRX V4 général |
| IR / instructions incomplètes | Les opérations non prises en charge restent rejetées par le patch ; aucune admission générale ou voie de repli ajoutée |

## Tranche minimale implémentée

`OrbisPRXFEXBridge::make_translation(invalidator)` produit un
`unique_ptr<OrbisV4Translation>` à partir du staging déjà validé. La copie
conserve les adresses invitées et permissions ; les adresses hôtes sont celles
d'une nouvelle réservation privée. Les régions proviennent du commit trié et
borné du bridge, avec span maximal 32 Mio et marges de garde déjà validées.

Le propriétaire n'est publié qu'après copie de toutes les régions. Une
exception pendant la préparation détruit le propriétaire provisoire ; le
staging d'origine reste intact. Une région source sans droit de lecture est
refusée plutôt que lue via un pointeur write-only. Les copies ne nécessitent
aucun mapping à adresse fixe ni modification des pages d'un autre propriétaire.

Ce prototype conserve temporairement staging et nouvelle copie : il augmente
la mémoire utilisée et ne constitue pas encore une installation de processus.
Chaque propriétaire garde ses budgets ; aucun budget global de plusieurs
images/processus n'est établi.

Le bridge reste une transaction offline, sans mutation concurrente :
`make_translation` doit être sérialisé avec `map`/`unload`, y compris depuis un
callback. Le chargeur et les buffers sources doivent rester stables pendant
ce transfert. Après retour, ils peuvent être déchargés indépendamment.

Les contraintes du callback sont celles du propriétaire existant : il couvre
tous les threads/caches utilisant le snapshot et son contexte reste vivant
jusqu'au `close` ou à la destruction non vide. Les mutations et la libération
attendent la fin des baux. Une faute d'invalidation conserve le stockage pour
réessayer ; une exception au destructeur termine avant un unmap dangereux.
Le bail emprunte son propriétaire et ne prolonge pas seul sa durée de vie.

## Preuve par le vrai FEX

Le nouveau bloc de `FEXGuestTranslationTest.cpp` génère un ELF SCE PRX non
chiffré avec les fixtures du dépôt, et le fait préparer/reloger par
`OrbisPRXPreparation`, copier par le bridge, puis transférer au propriétaire.
La relocation à `0x48000` doit encore contenir l'adresse **invitée** `0x44000`.
Le staging et le chargeur sont déchargés avant le dispatch.

Le programme est `mov rdi,0x48010; mov eax,[rdi]; hlt`, avec la valeur 42 dans
le segment de données. Le test ajoute une pile privée RW/NX, renseigne
explicitement RIP/RSP et le call-return stack du fixture FEX, publie les ranges
sous bail et conserve celui-ci jusqu'au retour de `ExecuteThread`.
Toutes les adresses hôtes de ces ranges diffèrent des adresses invitées.
Trois dispatchs sur deux threads FEX successifs doivent produire RAX=42.
Le snapshot est retiré du handler puis `close` invalide les deux caches et
libère la mémoire avant destruction des threads.

Ce résultat passe pour TSO/LSE `(0,0)`, `(0,1)`, `(1,0)`, `(1,1)` avec le vrai
frontend/JIT FEX et le simulateur d'instructions ARM64 VIXL sur hôte x86.
Ce n'est pas une exécution ARM64 native ou un test iPhone. Les deux threads
du nouveau fixture sont exécutés successivement, sans preuve de dispatchs
FEX simultanés. Les limites upstream VIXL sur les instructions exclusives
restent signalées par le test existant.

## Régressions et résultats

| Validation locale | Résultat |
| --- | --- |
| Nouveau `OrbisPRXTranslationTest`, C++17 strict, natif | PASS |
| Même test, ASan/UBSan, LeakSanitizer actif | PASS |
| `scripts/test-orbis-analysis.sh /tmp/maxps4-mission10-analysis` | PASS : bridge PRX, nouveau transfert, métadonnées, dossiers |
| `scripts/test-orbis-v4-memory.sh /tmp/maxps4-mission10-memory` | PASS : six suites et M1, natif et ASan/UBSan |
| `scripts/test-orbis-runtime.sh /tmp/maxps4-mission10-runtime` | PASS : 3370 checks linker et staging ELF/SELF |
| `scripts/test-ps4-system-services.sh` | PASS : suites de services, admission et ABI C avec sanitizers |
| CMake `maxps4-fex-translation-test`, puis exécution bornée | PASS, quatre configurations TSO/LSE |
| CMake `maxps4-ps4-linked-test`, exécutions normales, `--m1`, `--m2` | PASS, backend 3 ; M2 scalar 11/11, refus 55/55, limites 16/16 |

Le nouveau test natif vérifie les pointeurs invités relogés, deux copies
indépendantes du même espace logique, la collision de réservation refusée,
les trous sans modification de sortie, RX invité/RW-NX, le refus W+X sans
changement des permissions, le refus des sources write-only, le rollback après
invalidation échouée pendant une copie partielle, et le maintien du refus
`-ENOTSUP`. Un worker tente une libération pendant un bail ; le callback vérifie
les octets RX encore vivants avant unmap. La fermeture terminale et la survie
au déchargement du chargeur sont également contrôlées.

Les suites V4 existantes revérifient les collisions avec un occupant hôte,
sa préservation, les guards, remaps, protections, concurrence avec mutations,
réutilisation d'adresse hôte et invalidation de durée de vie.

Les suites natives sont exécutées hors traçage sandbox afin de conserver la
détection des fuites. Le build FEX utilise ses flags upstream : aucun passage
ASan/UBSan du JIT complet n'est revendiqué. La suite d'analyse a précédé les
dernières assertions du nouveau test ; celui-ci a ensuite été recompilé et
réexécuté séparément, en natif et sous sanitizers, avec succès.

Versions locales : AetherPS4 `3347fc13f19860855ce91b66356940989ea6b6a6`,
VIXL `5f418449c48f6ca3ad37c47ec632f17a561c0580`. Journaux FEX sous
`/tmp/maxps4-mission10-fex-translation.txt`, `-linked.txt`, `-m1.txt`, `-m2.txt`.
La dernière vérification FEX, avec RAX remis à zéro avant chaque dispatch et
absence de faute mémoire exigée, utilise
`/tmp/maxps4-mission10-fex-translation-final.txt`.

Fichiers propres à cette mission :
`MaxPS4-iOS/Native/OrbisPRXFEXBridge.hpp`,
`MaxPS4-iOS/Tests/OrbisPRXTranslationTest.cpp`,
`MaxPS4-iOS/Tests/FEXGuestTranslationTest.cpp`,
`scripts/test-orbis-analysis.sh`, et ce rapport.

## État réel et blocages

Opérationnels localement : transfert PRX vers propriétaire traduit, prêt
de mappings par bail, et consommation de ce transfert par un vrai FEX pour
le programme synthétique décrit. Le chemin de production iOS n'utilise pas
encore ce transfert pour lancer un PRX ; son dispatch V4 demeure bloqué.
L'API ajoutée ne transforme jamais un snapshot valide en admission de jeu.

Restent à intégrer et prouver : adaptateur FEX possédé par le processus,
sérialisation de tous ses caches/threads et de leurs snapshots, validation de
l'entrée et du jeu d'instructions, propagation des fautes sans publier de
succès partiel, pile/TLS/ABI et init/fini, services et cycle de vie complet.
Le mécanisme général conserve `-ENOTSUP` tant que cette intégration manque.

Aucun ELF de jeu commercial, PKG/PFS, DRM ou clé n'est traité. Aucun succès
de jeu ou correction de -48 n'est annoncé. Les protections mémoire restent
actives ; aucune modification à main, fusion ou publication.
