# Mission 61 — Kero Blaster jouable sur iPhone (rapport d'étape, mission interrompue)

Mission arrêtée à la demande de l'utilisateur le 18 octobre 2026 ; reprise prévue le lendemain.
**Kero Blaster n'est pas encore jouable** : il démarre, charge ses ressources et affiche son
premier écran réel animé sous Ubuntu, puis charge l'écran titre. Rien n'a été testé sur iPhone.

## Commits

Commit initial : `fba5f7d` (branche `experiment/keroblaster-iphone-m53`, aucun push, aucune fusion).

| Commit | Objet |
|---|---|
| `fc60289` | scan complet des textures (`GPU.TEXTURE`) et carte mémoire (`MAXPS4_GPU_MEMSCAN`) |
| `231d046` | NOP PM4 Gnmx *prepareFlip label* écrit réellement (blocage au 3ᵉ frame) |
| `c6416fe` | décodeur PNG hôte et service `libScePngDec` |
| `c98ff86`, `bcb0e74` | temps par dessin / par soumission, horodatage des frames |
| `d5d3c92` | historique borné des uploads de shaders (blocage à la 65ᵉ soumission) |
| `523d6d0` | tables sRGB exactes, banc plein écran avec hash de référence |
| `6a5ba8b` | pixel shading parallèle par bandes + accès mémoire direct |
| `474c5ca`, `9fb6ea6` | compute : VGPR réutilisés, buffers en accès direct |
| `41353a9` | service `libSceAudioOut` hôte et sortie Ubuntu (PulseAudio + WAV) |
| `7818d2a` | patch vixl : cache de décodage du simulateur ARM64 |
| `0dd81ba` | patch FEX : `PREFETCH` traduit en no-op ; en-tête WAV robuste |
| `bae2fe0` | clavier X11 et script d'entrées manette (`MAXPS4_INPUT_SCRIPT`) |
| `9af284e`, `fb81d0e` | patch FEX/vixl : préemption par tranche de temps |
| `d8654c2` | `scePngDecCreate` sur mémoire réutilisée ; journal d'appels sans plafond |

Fichiers nouveaux : `Native/PngDecoder.hpp`, `OrbisV4PngDecService.hpp`, `OrbisV4AudioOutService.hpp`,
`LinuxAudioSink.hpp`, `PadInputScript.hpp`, `GPUHostSpan.hpp`, tests `PngDecoderTest`,
`AudioOutServiceTest`, `PadInputScriptTest`, scripts `make-png-fixtures.py`, `test-png-decoder.sh`,
patches `vixl-mission61-decode-cache`, `fexcore-mission61-translated-prefetch`, `fexcore-mission61-preemption`.

**Modifications locales non commitées** (fichiers antérieurs non suivis par git, préservés tels quels
avec mes ajouts) : `OrbisV4SystemServices.hpp` (HideSplashScreen, `pthread_create`/`pthread_join`
POSIX, `CLOCK_SECOND`, conversions UTC/local, `__tls_get_addr`, branchement PngDec/AudioOut),
`OrbisV4ProcessBatch.hpp` (routage hôte), `OrbisV4TLSRuntime.hpp` (`module_size`),
`KeroBlasterBootRunner.cpp` (audio, entrées, captures périodiques, préemption, carte JIT),
`Tests/OrbisV4SystemServicesTest.cpp`, `scripts/run-keroblaster.sh`, `maxps4_ps4_libkernel.cpp`
(trace `FILE.OPEN`). Exception : `OrbisV4HostServices.hpp`, auparavant non suivi, est entré en
entier dans `d8654c2`. Les patches FEX/vixl sont aussi appliqués à l'arbre de build
`build/aether-linked/runtime/sources/fexcore-darwin`.

## Problèmes identifiés et corrigés (démontrés par les journaux)

1. **Textures vides** : prouvé par scan des 64 Mo (0 octet non nul). Cause : tous les PNG du jeu
   sont en palette (182) ou RGB (4) et le provider invité `scePngDec` refusait les palettes ; ses
   appels n'apparaissaient pas dans la trace (code invité). → décodeur hôte, identique à PIL sur
   les 183 PNG du jeu.
2. **Blocage au 3ᵉ frame** : attente active sur un label posé par le NOP `0x68750778`.
3. **Sortie au 10ᵉ frame** : `sceSystemServiceHideSplashScreen` refusé.
4. **Arrêt au 65ᵉ frame** : budget de 256 uploads de shaders.
5. `pthread_create` POSIX, `CLOCK_SECOND` (`time()` renvoyait -1), `sceKernelConvertUtcToLocaltime`,
   `__tls_get_addr`, `PREFETCH` traduit, audio non implémentés.
6. **Interblocage coopératif** : un thread invité attendait activement un autre thread sans
   appel système → préemption au sommet du dispatcher et aux entrées de blocs.
7. Images de l'écran titre refusées par mon propre `scePngDecCreate` (mémoire réutilisée sans Delete).

## Résultats dans le jeu (Ubuntu, Intel, simulateur ARM64)

- Premier écran réel : logo « 開発室Pixel / STUDIO PIXEL », sprite de Kero, « Just a moment. »,
  barre de chargement animée (`mission61-logs/run-07/first-guest-frame.png`, `run-13/frame-00100.png`).
- run-21 : 110 frames, 14 PNG décodés sans refus, ouverture de `00title.pxeve`, `mptTitle.png`,
  `logo.png`, `keyconfig.png` — **chargement de l'écran titre en cours, pas encore affiché**.
- Audio : port ouvert (48 kHz, S16 stéréo), ~2 200 buffers consommés au rythme réel, signal
  **entièrement silencieux** jusqu'ici (cohérent avec l'écran d'éditeur, non prouvé au-delà).
- Commandes : implémentées et testées unitairement, **pas encore testées dans le jeu**.
- Intégrité : SHA-256 des fichiers du jeu vérifiés après chaque lancement, tous OK.

## Performances mesurées

| Mesure | Avant | Après |
|---|---|---|
| Dessin plein écran (banc, sortie identique au bit) | 1 007 ms | ~120–200 ms (12 threads) |
| Soumission GPU par frame (dont compute de fond) | 1 690 ms | ~175 ms |
| Démarrage jusqu'au 1ᵉʳ frame | ~400 s | ~40 s (cache vixl) |
| Frame hors chargement | 4–5 s | ~0,3 s |
| Débit CPU invité simulé | ~5 M A64/s | ~50 M A64/s |

Le coût dominant restant sous Ubuntu est la **double émulation** (x86 → ARM64 → simulateur vixl) :
le chargement de l'écran titre prend plus de 10 minutes. Sur iPhone, le code ARM64 de FEX
s'exécuterait nativement (non mesuré).

## Tests exécutés

Natif et ASan/UBSan (fuites activées sauf mention) : suite graphique (10 suites ×2), banc plein
écran avec hash `1117a8b71989d145`, ThreadSanitizer sur le renderer parallèle, `GNMCommandExecutorTest`,
`PngDecoderTest` (fixtures + PNG du jeu), `AudioOutServiceTest`, `PadInputScriptTest`,
`OrbisV4SystemServicesTest` (une assertion obsolète corrigée), `maxps4-fex-translation-test`
après chaque patch FEX/vixl. Tous réussis.

## État iPhone

Aucune IPA construite ni testée dans cette mission. Points à traiter : préemption native
(le registre d'entrées de blocs n'existe que pour le simulateur ; sur ARM64 natif il faut
`BlockLinking=false` ou une autre méthode, coût à mesurer), sortie audio Core Audio,
commandes tactiles/manette Bluetooth, intégration du rendu (logiciel multi-thread ou Metal).

## Prochaines étapes

1. Relancer (`MAXPS4_FRAME_EVERY`, délai long) jusqu'à l'affichage de l'écran titre ; vérifier l'audio.
2. Démarrer une partie avec `MAXPS4_INPUT_SCRIPT` (Options/Croix), puis traiter les blocages
   (sauvegardes `sceSaveData*` probablement nécessaires).
3. Accélérer le chargement (copies agrandies dans le simulateur) ou passer au test iPhone.
4. Préparer la version iOS (build ARM64, préemption native, audio, entrées).

## Reprise du 19 octobre 2026 (rapport d'étape)

- Préemption corrigée (`0a6fab8`) : les entrées de blocs de threads terminés pouvaient renvoyer le
  thread principal à un faux RIP ; chaque entrée est validée auprès du cache du thread, et vixl ne
  consulte le registre qu'après un saut (chargement jusqu'au frame 83 : ~3 min au lieu de 19).
  `run-keroblaster.sh` coupe le journal des verrous (`MAXPS4_QUIET_LOCKS`, non commité).
- Le long chargement initial est la synthèse PxTone des 99 effets sonores `.ptnoise` (calcul flottant
  double, très lent dans le simulateur), puis les images de l'écran titre.
- **Blocage restant** (run-22, run-24) : après `logo.png` et un appel `sceKernelConvertUtcToLocaltime`,
  le thread principal exécute indéfiniment la boucle de copie agrandie `eboot+0xc6ec0` (fonction PNG
  `0xc6c00`, unique appelant `0xc333d`) sans nouveau `scePngDecParseHeader`. Cause non identifiée ;
  le runner journalise désormais les registres invités à chaque préemption pour la trouver.
- Écran titre toujours pas affiché ; rien testé sur iPhone.
