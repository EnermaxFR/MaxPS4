# Analyse ELF Orbis et préparation PRX incrémentale

Référence de non-régression : `bd30c050c65243af577498ef04d34aa1faced1d8`, PR #171.
Le chemin existant `GuestELF → PS4DynamicLinker → FEXCore → libkernel` n'est pas
élargi par cette livraison. Les diagnostics iPhone attendent toujours 42 pour
FEXCore JIT x86-64 et 2 pour le test ELF/libkernel, avec backend 2 et nettoyage.
Leur réussite sur la nouvelle IPA doit être confirmée sur iPhone physique.

## Ce qui est livré

Dans **Diagnostic FEXCore**, **Analyser un exécutable Orbis** ouvre le sélecteur de
fichiers de l'application. Choisir un ELF/PRX non chiffré accessible via Fichiers.
Le fichier est lu sur la queue de diagnostic avec accès security-scoped conservé
jusqu'à la fin de la lecture. Le rapport est sélectionnable et partageable.
Cette action ne crée aucun moteur FEX ni région invitée/JIT et n'exécute pas le
fichier. Le bouton d'exécution des démos reste une action distincte.

`OrbisMetadata` inspecte ELF64 little-endian x86-64, OSABI System V/FreeBSD,
ET_EXEC/ET_DYN et les types SCE EXEC, REPLAY_EXEC, STUBLIB, DYNEXEC, DYNAMIC.
Les replay/stub sont identifiés mais pas préparés. Les tables de sections peuvent
être absentes ; l'inventaire repose sur PT_DYNAMIC et PT_SCE_DYNLIBDATA.
Les PT_LOAD, TLS, RELRO, paramètres processus/module et autres segments connus
sont nommés et affichés avec leurs adresses, offsets, tailles, alignements et flags.
Les paramètres processus/module, commentaires et versions de segments restent
des métadonnées opaques ; aucun contrat de démarrage n'est déduit de leur présence.

Les offsets DT_SCE_STRTAB/SYMTAB/RELA/JMPREL/HASH sont relatifs au début de
PT_SCE_DYNLIBDATA. Les DT_* standard utilisent des adresses virtuelles traduites
uniquement dans des octets de segments chargeables. Les tables standard de
symboles exigent DT_HASH ; GNU_HASH seul n'est pas encore pris en charge.
SCE SYMTABSZ fournit le nombre de symboles. Les chaînes sont bornées et terminées,
les tables sont vérifiées avant lecture et les champs contradictoires sont refusés.
Les versions de modules sont minor dans les bits 32..39, major dans 40..47 ;
les bibliothèques utilisent une version 16 bits. Les identifiants sont dans les
bits 48..63. Les suffixes `NID#bibliothèque#module` sont décodés avec l'alphabet
SCE `ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-`.

Chaque import/export est associé à ses identités et versions déclarées, et non
à des noms de bibliothèque devinés. Le resolver libkernel existant est interrogé
seulement pour les imports qualifiés, bibliothèque version 1 et module 1.0.
« Résolu HLE (analyse) » signifie que le service existe dans la table ; aucun
trampoline n'est installé et aucune exécution du module n'est validée. Un NID
inconnu, une autre bibliothèque/version ou une identité absente ne produit pas
une réussite simulée. Le rapport compte les imports résolus/non résolus.

## Préparation PRX limitée

`OrbisPRXPreparation` est une API C++ testée, séparée du runtime et du sélecteur
mono-fichier. Elle reçoit explicitement 1..32 fichiers ET_SCE_DYNAMIC non chiffrés
et leurs biais de chargement. Elle copie PT_LOAD/PT_SCE_RELRO et BSS dans des
vecteurs d'octets ordinaires, conserve les protections déclarées et refuse W+X.
Elle ne crée pas de mémoire hôte exécutable. Aucun changement du fournisseur
StikDebug RX/RW, des protections des mappings FEX ni du linker validé sur iPhone.

Les dépendances SCE sont recherchées par nom/version exacte de module dans les
fichiers fournis ; DT_NEEDED correspond au nom explicite du fichier fourni.
Aucune recherche sur le disque, dans un PKG ou via un chemin RPATH. Les cycles,
modules ambigus, dépendances absentes et exports ambigus sont refusés.
Les exports globaux visibles résolvent les imports qualifiés des dépendances
directes avec NID/bibliothèque/module et versions identiques. Pas d'interposition,
weak/GNU, symboles spéciaux/ABS ou trampoline HLE PRX dans cette première API.

Les relocations appliquées sont R_X86_64_NONE, 64, GLOB_DAT, JUMP_SLOT et RELATIVE.
GLOB_DAT/JUMP_SLOT exigent un addend nul. Les addends signés et débordements sont
contrôlés ; les cibles doivent contenir huit octets writable, sans chevauchement.
Les types PC32, COPY, TLS et autres restent refusés par la préparation, même si
le linker synthétique distinct prend déjà en charge certains types supplémentaires.
Tout échec détruit la préparation partielle et efface une précédente réussite.
`unload()` libère les buffers possédés ; les fichiers d'entrée restent au caller.
L'ordre topologique d'initialisation et DT_INIT/DT_FINI sont conservés ; aucun
constructeur/finaliseur n'est appelé. Il n'y a pas de cycle d'exécution PRX intégré.

## Mémoire, TLS et limites

Le biais de PRX doit être aligné à 16 Kio et respecter chaque p_align. Les valeurs
écrites sont des adresses logiques invitées, jamais des pointeurs ARM64. La
préparation accepte uniquement la moitié basse canonique de l'espace x86-64 ;
elle ne prétend pas implémenter l'espace mémoire, ASLR ou les ABI de démarrage Orbis.
PT_TLS doit être contenu dans un PT_LOAD readable, avec image fichier cohérente.
Le rapport donne taille initiale, totale et alignement. Aucun module TLS ID,
DTV, FS base, bloc par thread ou relocation TLS n'est encore fourni ; la préparation
refuse ces besoins. Les régions RELRO ne sont pas finalisées dans un runtime.

Budgets : 64 Mio par fichier d'analyse, 128 segments, 4096 symboles et relocations,
1 Mio de STRTAB, chaînes ASCII imprimables de 1024 octets, rapport de 2 Mio
(troncature signalée). La préparation PRX impose aussi 64 Mio au total pour les
fichiers puis 64 Mio pour les buffers de segments. Un fichier réel dépassant ces
limites est explicitement refusé. SELF est refusé, sans tentative de déchiffrement,
DRM, licence, clé ou extraction de fichier commercial. Les tags inconnus et les
attributs SCE opaques sont signalés comme obstacles, pas appliqués.

Cette livraison n'établit aucune compatibilité Kero Blaster ni démarrage de jeu.

## Validation et recette iPhone

`bash scripts/test-orbis-analysis.sh` compile les fixtures entièrement synthétiques
sous ASan/UBSan et impose une limite de 30 secondes au test. Il exporte une petite
fixture `build/orbis/synthetic-sce.prx` pour le test Swift et la recette Fichiers.
Les tests couvrent tables SCE et standard, exports/imports qualifiés, versions,
relocations signées, deux PRX dépendants, cycles, TLS minimal inventorié, corruption,
adresses/tailles/alignements invalides, troncature du rapport et nettoyage.
`bash scripts/test-ps4-system-services.sh` conserve les 12 suites natives existantes
et impose maintenant 30 secondes à chaque exécutable.

La CI `Orbis SCE analysis and PRX preparation` teste les natives Linux/macOS, le
vrai bridge Swift, et compile C++/Swift pour iphoneos ARM64. La CI IPA existante
conserve ses tests FEX, libkernel, mémoire/JIT, Metal, Swift, audits et fuites.
Le sandbox local empêche LeakSanitizer sous ptrace : la vérification locale utilise
`ASAN_OPTIONS=detect_leaks=0`; la CI ne désactive pas cette vérification.
Aucun résultat Swift, ARM64 ou IPA n'est déduit d'un test natif Linux.

Sur iPhone après installation de l'IPA build 1000172 (ou un build correctif suivant) :

1. Importer la fixture synthétique depuis Fichiers via **Analyser un exécutable Orbis**.
   Vérifier ET_SCE_DYNAMIC/PRX, x86-64, segments, libkernel 1.0/bibliothèque v1,
   NID aI+OeCz8xrQ résolu, relocations, obstacles et absence d'exécution.
2. Analyser un ELF/PRX non chiffré dont les métadonnées sont connues et comparer le
   rapport à readelf et aux déclarations SCE. Les modules requis restent absents
   de l'analyse mono-fichier ; ils ne sont pas chargés automatiquement.
3. Avec StikDebug autorisé, relancer **Tester FEXCore** : résultat 42, backend JIT
   ARM64 réel. Puis **Tester ELF PS4 / libkernel** : 2 PT_LOAD, import scePthreadSelf,
   appel libkernel/retour 1, reprise invitée, valeur finale 2, backend 2 et zéro
   région invitée restante. Partager les rapports et le modèle/version iOS.

Les constantes/layouts ont été recoupés avec shadPS4 `elf.h`, `module.h` et
`module.cpp` à `443f022450f6857256e780daa3d5a14029a748f7` (GPL-2.0-or-later).
