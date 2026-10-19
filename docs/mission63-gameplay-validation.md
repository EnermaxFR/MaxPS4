# Mission 63 — validation hors de l'écran titre

Reprise du 19 octobre 2026, après la mission 62. Environnement Ubuntu x86-64,
FEX ARM64 exécuté par le simulateur vixl. Aucune validation iPhone.

## Session

`docs/mission63-logs/run-01`, MAXINST=16, captures toutes les 10 images,
audio dans `audio.wav`. Entrées relues depuis `run-01/input.txt`, initialement
copiées de `docs/mission62-input-script.txt`, puis prolongées par des appuis
Croix espacés pour les dialogues de l'introduction.

Une empreinte SHA-256 de chacun des 1 145 fichiers du jeu est enregistrée dans
`docs/mission63-logs/game-before.json`. Les outils d'analyse écrivent dans le
dépôt, jamais dans le dossier du jeu. Le montage `/app0` de PS4Libkernel est
en lecture seule, y compris pour les appels invités.

## Parcours à vérifier

1. Rejeu intégral de la séquence du titre et nouvelle partie.
2. Introduction `00office1`, ascenseur `00sceneElv`, clinique `00clinic1`.
3. Téléportation vers `01field1`, premier niveau : déplacement, saut, tirs,
   ennemis, dégâts et changement de carte.
4. Sauvegarde puis rechargement ; distinguer état en mémoire et persistance
   après fermeture du runner.

## État de la sauvegarde avant cette session

Les providers invités de `OrbisV4LocalServices.hpp` renvoient `0x809f0001`
pour les fonctions `sceSaveData*`, et des erreurs explicites pour les dialogues.
Ils ne sont pas remplacés par des services hôtes dans
`OrbisV4ProcessBatch::overridden`. `sceKernelWrite` n'écrit que sur stdout/stderr ;
les fichiers `/app0` restent en lecture seule.

Ces erreurs sont du code invité : leur absence dans les lignes `CALL` du runner
ne prouve pas que le jeu n'a pas tenté de sauvegarder. Les appels PLT de l'eboot
ont été relevés localement, sans modifier le jeu. Le relevé désassemblé reste hors du dépôt.
Le jeu importe à la fois le montage SaveData classique et les API SaveDataMemory2.

Le script original `01field1.pxeve`, événement `Intro`, appelle `<save>` puis
`gIntroFallIn`, qui appelle à nouveau `<save>`. La persistance ne peut pas être
déclarée fonctionnelle avec les seuls providers actuels.

## Résultats

Session en cours. Les résultats seront renseignés après observation.
