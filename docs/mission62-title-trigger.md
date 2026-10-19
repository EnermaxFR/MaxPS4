# Mission 62 — déclencheur du titre

Reprise du 19 octobre 2026. Aucun fichier du jeu modifié.

## Correction de l'analyse de la mission 61

La table des sprites commence à `eboot+0x148250`, avec des entrées de 40 octets :
fonction à +0, nom à +32. L'adresse `0x14d180` est le nom de l'entrée 506,
pas le début de l'entrée. Lire la fonction à +8 depuis le nom donne donc
la fonction du sprite suivant.

`TitleLogo` est l'entrée 538 : nom à `0x14d680`, fonction à **`0x6f2e0`**
(pointeur à `0x14d660`). `0x5ee70` est le comportement du sprite suivant,
`PixelLogo`.

## Condition d'activation du téléphone

Le vrai comportement TitleLogo :

- initialise un nombre d'ennemis selon un délai conservé dans le profil
  (`0x82fb0`, seuils de 1, 2, 3, 7, 14, 30 et 90 jours), ou 13 ennemis
  si le champ direction à +0x50 est non nul ;
- crée les ennemis depuis la table `0x1182d0` et incrémente son compteur +0x6e
  après chaque création (`0x6f530` à `0x6f56c`) ;
- attend **compteur +0x6e == 0** (`0x6f349`) ;
- fait clignoter le logo pendant environ 40 mises à jour puis lance l'événement
  portant son nom via `0x8d1f0` (`0x6f3e9` à `0x6f406`).

Les ennemis 380, 381 et 382 décrémentent bien le compteur de leur parent après
leur destruction (`0x4cac2` à `0x4cad7`, `0x5020b` à `0x50214`,
`0x4cbd1` à `0x4cbe6`). Leurs points de vie initiaux sont respectivement 8, 20 et 1.

La capture run-02/frame-00235 montre un gros ennemi au sol après disparition
de celui du « o ». Attention : l’absence de celui-ci dans sa zone initiale
(`analyze-kero-run.py`: « o libéré ») ne prouve pas sa destruction.
Il faut traiter aussi les ennemis qui ont changé de position.
Éliminer uniquement l'ennemi du « o » ne suffit donc pas si plusieurs sont créés.

Positions de création, coordonnées en tuiles (multipliées par 16 dans le code) :

| Ordre | Type | X | Y | Direction |
|---|---|---|---|---|
| 1 | 382 | 14 | 9 | 0 |
| 2 | 382 | 9 | 7 | 1 |
| 3 | 381 | 19 | 5 | 0 |
| 4 | 380 | 22 | 13 | 1 |
| 5 | 382 | 26 | 11 | 0 |
| 6 | 382 | 5 | 13 | 1 |
| 7 | 380 | 25 | 4 | 1 |
| 8 | 382 | 1 | 13 | 0 |
| 9 | 380 | 7 | 13 | 1 |
| 10 | 380 | 12 | 13 | 0 |
| 11 | 380 | 18 | 13 | 0 |
| 12 | 382 | 23 | 13 | 1 |
| 13 | 380 | 29 | 13 | 1 |

## Sessions

- `docs/mission62-logs/resume-sources.tar.gz` : copie des sept fichiers critiques
  de la passation et des trois patches FEX/vixl avant intervention. L'arbre FEX/vixl existant est conservé.
- run-01 : arrêt avant chargement, ouverture X11 impossible dans le sandbox.
  Les trois empreintes vérifiées par le lanceur sont correctes.
- run-02 : lancement avec accès au bureau local, MAXINST par défaut (16),
  entrées modifiables dans `docs/mission62-logs/run-02/input.txt`.

Validation du déclencheur en jeu : **réussie**, run-02/frame-01485.png, logo en
couleurs et téléphone qui sonne après nettoyage des sept ennemis.
**Nouvelle partie démarrée** : menu des emplacements au frame 1720,
confirmation puis chargement de `00newgame.pxpack` et `00newgame.pxeve`,
puis de **`00office1.pxpack` / `00office1.pxeve`**.
La capture frame-01935.png montre l'introduction « Cat & Frog Inc. » et son bureau.
Le passage au premier niveau contrôlable reste à vérifier.

L'image noire au début de `00newgame` n'a pas bloqué la progression :
les appuis Croix ont permis d'atteindre l'introduction, dont le rendu fonctionne.
Le dialogue de première partie n'a pas été observé visuellement.

Séquence complète conservée dans `docs/mission62-logs/run-02/input.txt`.
Copie lisible à conserver avec la documentation : `docs/mission62-input-script.txt`.
Les rechargements du fichier sont journalisés dans `boot.txt`.
Aucune modification du runtime n'a été nécessaire pour débloquer le téléphone.

## État final et reprise

Session arrêtée volontairement par SIGTERM au frame **1973** dans l'introduction
(`Runner exit=241`, code remonté par le lanceur après le signal, pas un nouveau crash).
Les SHA-256 d'eboot.bin, libc.prx et libSceFios2.prx correspondent au relevé de début
de run (`run-02/integrity.txt`). Aucun fichier du jeu n'a été écrit par les outils
d'analyse de cette reprise. Aucun commit, push ni merge effectué.

Pour rejouer la séquence (prévue pour les sept ennemis observés ici) :

```bash
MAXPS4_INPUT_FILE="$PWD/docs/mission62-input-script.txt" \
MAXPS4_FRAME_EVERY=5 MAXPS4_TIMEOUT=14400 \
bash scripts/run-keroblaster.sh \
  /home/enermaxfr/Téléchargements/CUSA06015 \
  build/mission51-release docs/mission62-logs/run-03
```

La séquence a été exécutée par rechargements successifs ; son rejeu intégral en
un seul chargement n'a pas encore été testé. Le nombre d'ennemis dépend du profil.
Prochaine validation : laisser finir l'introduction et atteindre le premier niveau
contrôlable, puis vérifier déplacement, tirs et changement de carte hors du titre.
Rien construit ni testé sur iPhone.
