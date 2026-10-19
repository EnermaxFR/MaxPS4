# Mission 61 — note de passation (19 octobre 2026)

État exact pour reprendre le travail sur Kero Blaster (CUSA06015), branche
`experiment/keroblaster-iphone-m53`. Rapport détaillé : `docs/mission61-keroblaster-playable.md`.
Rien n'a été poussé ni fusionné.

## Ce qui fonctionne réellement (Ubuntu x86, simulateur ARM64 vixl)

- Démarrage, chargement complet, écran titre jouable (~1 s par image).
- Commandes vérifiées dans le jeu : marche, saut, tir gauche/droite, **tir vers le haut**
  (Haut maintenu *avant* d'appuyer sur Carré : la direction est figée à l'appui), menu Options.
- Collisions, dégâts et destruction des ennemis ; Kero subit aussi les contacts.
- Effets sonores réels (PulseAudio + capture WAV). L'écran titre est silencieux dans le jeu original.

## Ce qui bloque

**Aucune partie n'a encore démarré.** D'après `rsc_k/text/00title.pxeve` :
`*[TitleLogo]` → `<swtN[Phone1;1]><flgP[50]>`, puis `*[Phone1]` → `<jmpF[50;_phone]>` → fondu et `<home>`.
Le déclencheur de l'événement `TitleLogo` n'est pas identifié :
- éliminer le Negativus posé sur le « o » du logo (réussi et reproductible, frame ~200) ne suffit pas :
  le logo reste gris, le téléphone ne change pas d'état et aucune carte n'est chargée ;
- tirer sur le centre du logo, ou appuyer sur Haut/Bas devant le téléphone, ne déclenche rien ;
- Kero n'a pas encore été posé exactement sur le bureau du téléphone (l'inertie le fait dépasser).

Pistes d'analyse :
- `TitleLogo` est une classe de sprite (table des classes `eboot+0x14d180`, entrée 32 → `0x5ee70`)
  qui suit un objet parent (pointeur `+0x80`) ;
- objets de `00title.pxpack` (coordonnées en tuiles, format 8.8) : TitleLogo (15,6), type 4 sans nom
  (15,5) — probablement le parent —, Phone1 type 53 (10,12), Cake (17,12), type 239 (6,12),
  type 159 (15,13) = départ de Kero ;
- hypothèse non testée : éliminer **tous** les Negativus de l'écran.

## Comment tester vite

- Lancement : `MAXPS4_AUDIO_WAV=<run>/audio.wav MAXPS4_FRAME_EVERY=5 MAXPS4_TIMEOUT=14400 bash scripts/run-keroblaster.sh /home/enermaxfr/Téléchargements/CUSA06015 build/mission51-release docs/mission61-logs/run-NN`
  (≈8 min jusqu'à l'écran titre, au frame ~110).
- **Session pilotable** : `MAXPS4_INPUT_FILE=<chemin absolu>` ; le fichier (une étape par ligne,
  `debut-fin:BOUTON+BOUTON`, frames absolues) est relu à chaque modification, sans redémarrer.
  Boutons : `UP DOWN LEFT RIGHT CROSS SQUARE CIRCLE TRIANGLE L1 R1 L2 R2 OPTIONS TOUCHPAD`
  et sticks `LS_*`/`RS_*`.
- Séquence validée pour éliminer le Negativus du « o » : `docs/mission61-logs/run-42/input-script.txt`
  (frames 130-262).
- Analyse automatique : `python3 scripts/analyze-kero-run.py docs/mission61-logs/run-NN`.
- `MAXPS4_MAXINST` doit rester petit : 256 échoue (« Translated JIT block exceeds bounded temporary
  space », limite du patch de traduction mémoire). 24 et 32 démarrent, mais leur gain de vitesse n'a pas été mesuré.
- Arrêter un lancement : `pkill -x maxps4-keroblas` (`pkill -f` tue aussi le shell lanceur).

## Travail non commité (sur le disque, fichiers non suivis par git avant la mission)

Ces fichiers contenaient déjà du travail antérieur non publié ; mes modifications y sont restées
non commitées pour ne pas le mêler :
- `MaxPS4-iOS/Native/OrbisV4TLSRuntime.hpp` : `module_size()` sans verrou (**correction de
  l'interblocage `__tls_get_addr`, indispensable**) ;
- `MaxPS4-iOS/Native/OrbisV4SystemServices.hpp` : HideSplashScreen, `pthread_create`/`pthread_join`,
  `CLOCK_SECOND`, conversions UTC/local, `__tls_get_addr`, branchement PngDec/AudioOut ;
- `MaxPS4-iOS/Native/OrbisV4ProcessBatch.hpp` : routage hôte (`overridden()`) ;
- `MaxPS4-iOS/Tests/KeroBlasterBootRunner.cpp` : audio, entrées (script/fichier/clavier), captures,
  préemption, registres journalisés, carte JIT ;
- `MaxPS4-iOS/Tests/OrbisV4SystemServicesTest.cpp`, `scripts/run-keroblaster.sh`,
  `MaxPS4-iOS/Native/maxps4_ps4_libkernel.cpp` (trace `FILE.OPEN`).

Les patches FEX/vixl sont appliqués à `build/aether-linked/runtime/sources/fexcore-darwin`
et enregistrés dans `patches/` (`vixl-mission61-decode-cache`, `fexcore-mission61-translated-prefetch`,
`fexcore-mission61-preemption`).

## iPhone

Rien n'a été construit ni testé. À prévoir :
- JIT activé sur l'appareil (débogueur ou StikDebug) ;
- préemption en ARM64 natif : le registre d'entrées de blocs n'existe que pour le simulateur ;
  `MAXPS4_NO_BLOCK_LINKS` / `BlockLinking=false` fonctionne aussi en natif, coût à mesurer ;
- sortie audio Core Audio ;
- commandes tactiles et manettes Bluetooth.

Règle : ne jamais écrire dans le dossier du jeu. Un fichier temporaire d'analyse
(`eboot.bin.rel.pkl`) y avait été créé par erreur ; il a été déplacé hors du dossier.
Les SHA-256 des fichiers du jeu sont restés corrects.
