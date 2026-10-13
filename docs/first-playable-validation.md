# Validation de la première image jouable

Base : `e45e750`, main après fusion de #181. Aucun iPhone physique n'est
attaché à cet environnement Linux x86-64. Le critère final sur appareil reste
à valider ; aucune exécution iPhone ni jouabilité de Kero Blaster n'est annoncée.

## Observations

Le test mémoire `PS4GuestFrameTest` a réussi localement : admission SCE/PRX,
configuration framebuffer bornée, entrées, refus des tailles/adresses invalides
et arrêt/repréparation. Cela ne constitue pas une exécution FEX.

Les tests système ASan/UBSan et la construction FEX liée sont en cours. Les
résultats FEX, ARM64 natifs, pont Swift, build iPhone et IPA seront ajoutés après
lecture des journaux du commit exact. Aucun succès n'est présumé.

## Validation physique à réaliser

Installer l'IPA du commit testé, activer StikDebug, lancer la scène, enregistrer
une vidéo montrant l'image et ses déplacements tactiles, partager le rapport
backend/numéro/FNV64 et noter modèle, version iOS et méthode d'activation JIT.
Tester arrêt, reprise explicite après arrière-plan, manette et désactivation des
commandes tactiles. Les preuves CI natives et les snapshots sont complémentaires.
