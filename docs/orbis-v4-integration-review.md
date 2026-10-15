# Orbis V4 — revue préparatoire et blocage d’intégration

Date : 2026-10-15 UTC. Statut : **revue documentaire seulement ; intégration du code non réalisée ; ne pas fusionner**.

## Base et périmètre

La comparaison GitHub de `main` et `integration/orbis-v4-implementation` avant ce rapport est `identical`, ahead=0, behind=0, au commit `5d119fdeb2b80bf48ad510c809e5ae5b6ba78000`. #201 est déjà fusionnée ; aucune seconde fusion n’a été tentée.

Ce changement ajoute uniquement ce rapport. Aucun composant runtime, workflow ou réglage de budget n’est modifié. Les PR #199, #203 et #204 restent expérimentales, ouvertes et en brouillon ; leurs branches ne sont ni fusionnées ni supprimées.

## Sources examinées

| PR | HEAD examiné | Périmètre et décision préparatoire |
| --- | --- | --- |
| [#199](https://github.com/EnermaxFR/MaxPS4/pull/199) | `0d4f2692d7bdee79e30cfe2bc8f135329b9f51f9` | M1 direct, copies mémoire vérifiées, stockage TLS multi-modules, contextes FEX ordonnés. Candidats à une intégration progressive après résolution des dépendances et nouvelle CI sur la base #201. |
| [#203](https://github.com/EnermaxFR/MaxPS4/pull/203) | `f999cad3b04c8095ec383628611c020c6b7f4999` | Diagnostic Mach et tests collision/non-écrasement/comptabilité/rollback/retry. À adapter après M1 ; ne corrige pas -48. |
| [#204](https://github.com/EnermaxFR/MaxPS4/pull/204) | `f40c3c076740451e475891c5a096f78167bbbf77` | Traduction FEX opt-in et invalidation ; base différente (`integration/orbis-v4-safe-staging`). Ne pas recopier aveuglément le runtime ou le GuestAddressSpace concurrents. Garder M2 public désactivé. |

Revue effectuée via les métadonnées/diffs GitHub, listes de fichiers, sources mémoire/TLS/M1 de #203, propriétaire de mappings OrbisV4Translation et notes M2 de #204. Ce n’est pas un audit exhaustif du patch FEX ni une validation de la combinaison avec #201.

## Garanties à conserver

- Refus W+X, gardes PROT_NONE, limites de taille et d’overflow, permissions et prévalidation de la totalité des copies avant écriture.
- Backend direct : égalité numérique guest=host obligatoire ; Mach VM_FLAGS_FIXED sans OVERWRITE. Sur POSIX, libérer un résultat mmap déplacé et refuser la collision. Aucun remplacement de mapping hôte occupé.
- Une collision ne doit publier aucune région ni consommer de budget ; vérifier le propriétaire existant puis réussir une nouvelle réservation après libération.
- TLS : rollback des réservations et du bookkeeping en cas de bad_alloc, adresse de sortie zéro et retry dans le même thread ; isolation .tdata/.tbss et destruction.
- Threads FEX : contextes/piles/TLS distincts, admission restrictive et nettoyage lors de l’échec d’un thread ultérieur. Le harness déterministe n’établit ni préemption ni ABI pthread/SCE générale.
- M2 : lease couvrant décodage/dispatch et durée de vie des pointeurs ; invalidation de tous les caches associés avant mutation sous le même verrou. Conserver les tests de remap au même RIP et de rollback si l’invalidation échoue.

## Résultats CI existants vérifiés, non relancés dans cette mission

Les états suivants ont été lus par l’API GitHub sur les HEAD exacts ci-dessus. Ils ne sont pas des résultats de la branche d’intégration.

| Source | Résultat observé | Exemples |
| --- | --- | --- |
| #199 | 13 workflows retournés, tous completed/success | [Mémoire M1](https://github.com/EnermaxFR/MaxPS4/actions/runs/38713227937), [services](https://github.com/EnermaxFR/MaxPS4/actions/runs/38713227682), [liaison Darwin/iOS](https://github.com/EnermaxFR/MaxPS4/actions/runs/38713228124), [IPA](https://github.com/EnermaxFR/MaxPS4/actions/runs/38713227769) |
| #203 | 9 workflows retournés, tous completed/success | [Mémoire/rollback](https://github.com/EnermaxFR/MaxPS4/actions/runs/38719373672), [liaison](https://github.com/EnermaxFR/MaxPS4/actions/runs/38719373709), [IPA](https://github.com/EnermaxFR/MaxPS4/actions/runs/38719373805) |
| #204 | 11 workflows retournés, tous completed/success | [Mémoire V4](https://github.com/EnermaxFR/MaxPS4/actions/runs/38758783285), [services](https://github.com/EnermaxFR/MaxPS4/actions/runs/38758783247), [liaison](https://github.com/EnermaxFR/MaxPS4/actions/runs/38758783365), [SideStore](https://github.com/EnermaxFR/MaxPS4/actions/runs/38758783260), [Orbis Test IPA](https://github.com/EnermaxFR/MaxPS4/actions/runs/38758783354) |

Pour le run mémoire V4 38758783285, les jobs `address-space (ubuntu-latest)` et `address-space (macos-latest)` et leurs étapes de compilation/exécution et sanitizers sont success.

Une compilation/liaison iOS et une IPA ne prouvent pas une exécution sur iPhone. Les résultats natifs et simulateur des branches sources ne prouvent pas leur compatibilité avec la nouvelle intégration.

## Erreurs et limites restantes

Le rapport iPhone de #203, diagnostic `m1-ios-collision-203-v1`, build 38719373805, indique engine_open=0, jit_initialize=0 puis m1_thread_api_status=-48, m1_threads=BLOCKED et reason=direct_mapping_collision. Aucun nouveau test matériel n’a été effectué ici.

Les notes actuelles de #204 consignent aussi un diagnostic utilisateur iPhone sur `4fc3ecb` avec ELF/libkernel réussi. Ce diagnostic n’exerce pas la traduction expérimentale M2 et ne résout donc pas -48.

#204 traduit un sous-ensemble réel du fetch/JIT et des atomiques, mais les chemins généraux memcpy/memset, masked/gather, cache, ValidateCode et fautes récupérables restent à couvrir. Les opérandes traversant plusieurs mappings nécessitent un slow path ; les accès invalides peuvent actuellement provoquer un trap natif. L’entrée M2 publique doit conserver TRANSLATION_REQUIRED. ABI TLS dynamique SCE, imports/relocations généraux et scheduler général restent ouverts. Aucun jeu PS4 fonctionnel n’est démontré.

## Blocage de cette mission

Deux tentatives de `git clone https://github.com/EnermaxFR/MaxPS4.git /workspace/MaxPS4` échouent avec code 128 : `Failed to connect to proxy port 8080 ... Could not connect to server`. Une requête HTTPS indépendante échoue aussi sur le proxy configuré. Le connecteur GitHub reste accessible ; il a permis cette revue.

Aucun checkout n’était présent dans /workspace. Les recherches d’outils n’ont trouvé ni clang++, ni cmake, ni xcrun. Aucun test Linux/macOS/iOS nouveau n’a été exécuté, et aucun workflow n’a été relancé par cette mission. Le proxy n’a pas été contourné ; aucun budget GitHub n’a été modifié.

## Étapes nécessaires avant fusion

1. Restaurer l’accès du proxy de l’exécuteur et fournir un environnement C++/Clang utilisable ; récupérer le dépôt, ses instructions et les dépendances épinglées.
2. Sur cette branche uniquement, intégrer par petits commits les composants mémoire M1, puis TLS, puis threads réellement couverts ; résoudre explicitement les différences du runtime/ABI avec #201. Ne pas fusionner automatiquement les PR sources.
3. Adapter les diagnostics et tests #203 ; conserver collisions réelles, rollback complet, retry et échecs d’allocation TLS.
4. Porter séparément le sous-ensemble M2 et son patch FEX vérifié ; poursuivre le slow path inter-mappings, les refus sûrs/fautes récupérables et l’invalidation. Ne pas activer le dispatch public avant une admission et une borne d’exécution réelles.
5. Exécuter les suites mémoire/TLS M1 et V4 avec ASan/UBSan Linux/macOS, les régressions FEX Linux ARM64 et macOS ARM64, puis les workflows de liaison iOS et IPA sur le SHA intégré exact. Publier liens, erreurs et artefacts.
6. Produire un diagnostic synthétique isolé puis valider sur iPhone physique : adresses guest/host distinctes, état CPU observé, mémoire, arrêt borné, W^X, collisions, invalidation, destruction/recréation et répétitions. Rejouer ensuite deux threads et trois modules TLS. Maintenir -48 non résolu jusqu’à cette preuve.
7. Mettre à jour cette PR avec les résultats de l’intégration ; conserver le brouillon tant que ces conditions restent ouvertes.
