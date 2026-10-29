# MaxPS4 0.4.0 — première préversion

**MaxPS4 fait tourner son premier jeu PS4 sur iPhone : Sonic Mania, à 60 images par seconde, avec le son, la manette et les sauvegardes.**

C'est une **préversion expérimentale**. Elle est destinée aux curieux et aux testeurs ; attends-toi à des bugs, et à ce que la plupart des jeux ne démarrent pas encore.

💬 Retours, questions et tests : **[Discord MaxPS4](https://discord.gg/G7BaXN4fd)**

## Ce qui marche

- **Sonic Mania** jouable du début à la fin (testé sur iPhone par l'auteur) : 60 images/s calées sur l'écran, son, manette, vraies sauvegardes, pause, arrêt et relance.
- **Manettes** MFi, Xbox, DualShock 4 et DualSense : toutes les touches, sticks et gâchettes analogiques. **Commandes tactiles** réglables (taille, opacité), masquées quand une manette est branchée.
- **Bibliothèque automatique** : les jeux copiés dans *Sur mon iPhone › MaxPS4 › Jeux* apparaissent seuls, avec leur titre et leur icône.
- **Mises à jour et DLC** extraits : un dossier `<jeu>-UPDATE` ou un contenu additionnel copié dans *Jeux* est rattaché à son jeu.
- **Sauvegardes** dans *MaxPS4 › Sauvegardes*, à part des jeux.
- **Fiche de jeu**, **Reprendre** le dernier jeu, **réglages par jeu**, **6 langues** (français, anglais, espagnol, allemand, italien, portugais du Brésil) ; les jeux reçoivent la langue choisie.
- **Rapport de compatibilité** : quand un jeu s'arrête, l'app dit pourquoi et garde un rapport dans *MaxPS4 › Rapports*.

## Installer

1. Télécharge **MaxPS4-FEXCore-SideStore.ipa** ci-dessous et installe-la avec **SideStore**.
2. Ouvre MaxPS4, touche **JIT** et active le JIT avec **StikDebug** ([protocole détaillé](https://github.com/MaxFRdev/MaxPS4/blob/main/docs/fexcore-stikdebug-jit.md)). Le JIT est indispensable.
3. Copie le **dossier complet** de ton jeu (`eboot.bin`, `sce_module`, `sce_sys` et ses données) dans *Sur mon iPhone › MaxPS4 › Jeux*, puis touche **Jouer**.

## Limites

- **Seul Sonic Mania a été testé.** Les jeux 2D légers ont le plus de chances de marcher ; les gros jeux 3D (God of War, Assassin's Creed…) sont hors de portée pour l'instant.
- **Aucun jeu n'est fourni et MaxPS4 ne déchiffre rien** : il faut le dossier déjà extrait d'un jeu que tu possèdes.
- iPhone récent recommandé ; le JIT via StikDebug doit être réactivé à chaque lancement de l'app.

Un jeu bloque ? Envoie son rapport (*MaxPS4 › Rapports*) sur le Discord ou dans une [Issue](https://github.com/MaxFRdev/MaxPS4/issues) : c'est comme ça que les prochains jeux marcheront.

MaxPS4 est un projet indépendant, sous licence GPL-2.0, non affilié à Sony Interactive Entertainment / PlayStation.
