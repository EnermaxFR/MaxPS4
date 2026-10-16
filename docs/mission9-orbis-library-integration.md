# Mission 9 — bibliothèque et dossier Orbis

Travail local sur `experiment/maxps4-ubuntu-codex`, à partir de `b031576`,
en conservant les modifications non publiées de la mission 8.

## Parcours raccordé

Dans l'onglet **Bibliothèque**, **Dossier Orbis accessible** ouvre le sélecteur
de dossier et le précontrôle existants. Le dossier est sélectionné explicitement,
en lecture seule, avec un accès security-scoped limité à chaque opération.
Il n'est ni extrait du PKG ni copié dans la bibliothèque, et aucun rapprochement
automatique avec un titre importé n'est effectué.

Après l'inventaire, l'utilisateur peut demander **Vérifier et tenter l'exécution
bornée**. Cette action refait le précontrôle, lit le manifeste `maxps4.json`
avec graphe PRX et biais explicites, puis appelle la préparation native existante.
Seule une préparation réussie, suivie d'une initialisation JIT réussie, permet
l'appel à `maxps4_fex_engine_run_orbis`. Chaque refus arrête cette voie ; aucun
succès synthétique ni contournement mémoire n'est ajouté. Le nettoyage du moteur
existant reste assuré par `defer`.

Le rapport natif publie désormais `[ORBIS.PREFLIGHT] execution_admitted=0` :
un code de retour zéro du précontrôle signifie seulement que l'inventaire a
été produit, y compris si eboot.bin est absent. L'interface expose cette
distinction et bloque les commandes ainsi que la fermeture interactive de la
fiche pendant l'opération.

## Limite V4 réelle

La voie appelée par le dossier utilise `prepare_orbis` / `run_orbis` et
`staged_ps4`, le processus Orbis borné existant. Elle n'intègre pas le pont PRX
en mémoire traduite V4. `OrbisPRXFEXBridge::admit_dispatch` refuse ce dernier
avec `-ENOTSUP`, sans publier de pointeur hôte. Les tests de traduction FEX et
de durée de vie ne suffisent pas à valider une pile/TLS et une initialisation
complètes de jeu dans ce lanceur.

Cette limite est affichée avant la tentative et dans son rapport. Aucun repli
de V4 vers la voie existante n'a été ajouté. L'intégration d'un lanceur PRX V4
reste à réaliser ; ce changement raccorde l'interface au parcours explicitement
choisi, sans annoncer ce lanceur comme opérationnel.

## Fichiers propres à cette mission

- `Sources/MaxPS4HomeView.swift` : entrée depuis la bibliothèque, en plus des
  changements de la mission 8 déjà présents dans ce fichier.
- `Sources/MaxPS4GameDirectoryView.swift` : distinction inventaire/admission,
  libellé de tentative et limite V4, protection de la fermeture pendant le travail.
- `Native/GameDirectoryPreflight.hpp` : statut explicite sans admission CPU.
- `Tests/GameDirectoryPreflightTest.cpp` : tout inventaire terminé, avec ou sans
  fichiers et manifeste, reste sans admission CPU.
- `Tests/OrbisAnalysisSwiftTest.swift` : même distinction via le pont Swift.

Ces chemins sont relatifs à `MaxPS4-iOS`. Ce document complète le livrable.
Les autres fichiers modifiés du workspace proviennent de la mission 8.

## Validation et état du lancement

`scripts/test-orbis-v4-memory.sh /tmp/maxps4-mission9-memory` passe avec
six suites et M1 en natif et ASan/UBSan, détection des fuites activée.
Le premier passage de `scripts/test-orbis-analysis.sh` passe : pont PRX/FEX,
métadonnées et précontrôle des dossiers, sans exécution invitée. Une seconde
exécution passe également sur le nouveau contrat du rapport après modification
(`/tmp/maxps4-mission9-analysis-final`). `git diff --check` passe.
Les tests s'exécutent hors traçage sandbox pour conserver LeakSanitizer.

Swift/SwiftUI et le SDK iOS ne sont pas installés localement : la compilation
et les tests Swift, ainsi que le parcours visuel sur iPhone, restent à vérifier.
Aucun workflow GitHub n'est lancé pour ces modifications locales.

Aucun jeu utilisateur ni PKG commercial n'a été lancé. Kero Blaster reste
sans exécutable accessible dans le constat PFS historique ; aucun test réel
de jouabilité n'est établi ici. Les protections W^X, NX, collisions, refus
de traduction et durée de vie restent actives. L'erreur historique -48
n'est pas déclarée corrigée. Aucun push, fusion ou changement à main.
