# Examen de la PR #125

État GitHub revérifié le 11 octobre 2026 : **fusionnée**, commit de fusion
`73ed0f021e051e2ef11f98da18de8800ff01d492`, désormais base de la branche
`codex/fexcore-ios-linkage`. La fermeture initiale sans fusion à 08:34 UTC n'avait
ni motif renseigné, ni commentaire, ni revue. La PR a ensuite été fusionnée à
09:02 UTC ; cette évolution remplace le constat historique de #126.

Commits : `760ec99`, `4ee5c89`, `4e330a7`. Le premier centralise les patches,
compile les unités ARM64 et teste les primitives ; le deuxième ajoute le chemin
Darwin malloc au test ; le dernier désactive le conseil THP Linux sur Darwin.

| Statut | Fichier modifié |
| --- | --- |
| M | .github/workflows/blank.yml |
| M | .github/workflows/parallel-engine-checks.yml |
| M | .github/workflows/parallel-heavy-engine.yml |
| A | MaxPS4-iOS/Tests/FEXDarwinPatchTest.py |
| A | MaxPS4-iOS/Tests/FEXDarwinPrimitivesTest.cpp |
| A | docs/FEXCORE-IOS-TESTS.md |
| A | scripts/patch-fexcore-ios.py |

La CI #125 (`38125083690`) compile onze unités JIT et CPUBackend avec le SDK
iphoneos et conserve leurs objets ARM64. Les workflows engine smoke et heavy
préservent aussi le pont CPU, la vérification Swift, la traduction/repli interpréteur
et les primitives Darwin. Le workflow de packaging `blank.yml` est exclu de ce travail.

| Adaptation | Source FEX | Vérification conservée |
| --- | --- | --- |
| Mutex | Utils/WritePriorityMutex.h | FEXDarwinPrimitivesTest : contention lecteurs/écrivains |
| Signaux | Utils/SignalScopeGuards.h | même test : masques imbriqués, déplacement et restauration |
| CPU virtuel | Interface/Core/JIT/MiscOps.cpp | patch : Apple avant TPIDRRO, CPU constant ; objet ARM64 |
| Headers Linux | Utils/PrctlUtils.h, CPUBackend.cpp | patch et compilation SDK iOS |
| prctl MDWE | CPUBackend.cpp | contrôle de contexte et garde Apple |
| THP | Utils/AllocatorHooks.h | patch : aucune substitution MADV_NORMAL ; garde Apple |

Le script est appelé avant les compilations dans les deux workflows concernés.
`replace_once` exige un fragment unique ; les classes/fonctions sélectionnées
par indices vérifient maintenant aussi l'unicité. Tous les changements sont
préparés avant écriture. Les tests vérifient divergence, duplication du contexte,
absence d'écriture partielle et réexécution idempotente. Un fichier déjà marqué
est ignoré ; le marqueur ne constitue pas une vérification cryptographique du contenu.
