# Orbis V4 — intégration expérimentale sécurisée

## Origine
- Base : `main` après fusion de la PR #200.
- Source à examiner : PR #199 (`experiment/orbis-mission-198`), basée sur l'ancienne branche `experiment/orbis-v3-autotests`.
- La PR #199 reste ouverte et intacte. Aucun remplacement du moteur FEXCore stable.

## Périmètre candidat
1. Isoler les contrats mémoire M1 (gardes, W^X, vérification des plages, rollback).
2. Porter et tester les structures TLS multi-modules, sans prétendre disposer de l'ABI TLS SCE complète.
3. Examiner les contextes FEX multi-threads et leurs tests déterministes.
4. Réconcilier chaque fichier avec le `main` actuel, sans copier aveuglément les changements V3 anciens.
5. Ajouter des tests CI, puis une IPA Orbis Test distincte, et demander des essais sur iPhone avant toute fusion.

## Bloqueurs
- Traduction des accès JIT vers un stockage hôte distinct non réalisée.
- ABI TLS dynamique SCE, relocations PRX générales et ordonnanceur invités incomplets.
- Aucun test Orbis M1 sur iPhone confirmé.
- Aucun jeu PS4 validé.

## Garde-fous
- Ne pas modifier l'application stable ni son identifiant.
- Ne pas fusionner cette branche sans nouvelle autorisation.
- Refuser explicitement les cas non pris en charge, sans interpréteur présenté comme FEX JIT.
- Exiger CI et preuve d'exécution réelle sur iPhone avant toute revendication de fonctionnalité.
