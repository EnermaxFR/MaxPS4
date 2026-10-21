# Mission 64 — Sonic Mania (CUSA07023) : jusqu'au logo SEGA

Host Linux x86-64, CPU invité via FEX → ARM64 → simulateur vixl (double émulation).
Dump de l'utilisateur : PKG de base `A0100` et patch `A0103`, extraits avec l'extracteur
shadPS4Plus (`docs/pkg-extractor-1-1-assessment.md`). Le jeu de base est lancé depuis
`~/PS4/CUSA07023/CUSA07023`. Les fichiers du jeu ne sont jamais modifiés : les SHA-256 sont
vérifiés avant et après chaque extraction.

## Résultat réel

Le logo SEGA s'affiche dans la fenêtre. Le fondu commence à l'image 33 (environ 4 min 30)
et le logo est net vers l'image 55. Le fondu monte le bleu, puis le vert, puis le rouge,
avec un fond qui tend vers le blanc. Aucune entrée manette n'a été testée. Après le logo (run 29),
l'écran passe au bleu nuit (image 215) puis au noir (image 230, ~26 min) ; le jeu continue de
présenter des images mais n'a ouvert aucun fichier depuis : la scène suivante n'est pas chargée.

## Ce qu'il a fallu ajouter

- Modules système absents :
  - `libSceNpScore` (`libSceNpScoreRanking.prx`) : console hors ligne, `SCE_NP_ERROR_SIGNED_OUT` ;
  - `libSceRtc` (`sceRtcGetCurrentTick` sur l'horloge hôte) ;
  - `libSceScreenShot` (superposition sans effet) ;
  - `libSceNpToolkit2` : dépendance sans import, avec seulement `module_start`.
- NpManager, SystemService et UserService : réglages locaux acceptés, opérations PSN refusées
  (déconnecté), aucun événement, un utilisateur local.
- Placement :
  - les fichiers réels suivent les modules virtuels quand une image dépasse 4 Mo (l'eboot fait 14 Mo) ;
  - les modules virtuels après libkernel démarrent à `0x1300000` (état de 2 Mo de libkernel) ;
  - la réservation FEX est relevée pour l'image (plafond 256 Mo).
- Lien :
  - PROCPARAM version 3 (80 octets, 3 pointeurs) ;
  - attribut d'import de bibliothèque `0x1` (import strict).
- Trophées : premier `trophyNN.trp` présent (Sonic utilise `trophy01.trp`).
- GPU :
  - `v_cvt_off_f32_i4` (VOP1 14) ;
  - textures 5_6_5 UNORM, dans l'interpréteur et dans l'échantillonneur GPU.
- Noms des imports : `docs/mission64-sonic-mania-names.tsv`, soit les noms de Kero plus les
  NID recalculés (`sha1(nom + 518D64A635DED8C1E6B039B1C3E55230)[:8]` inversé, puis base64 avec `/` → `-`).

## Vitesse

| Mesure | Avant | Après |
|---|---|---|
| Ouverture de l'écran | 180–190 s | 70–120 s |
| Écart entre deux images | 80–100 s | ~7 s (avec `MAXPS4_AUDIO_TIME_SCALE=8`) |

- Profil hôte (gdb, `perf` interdit sur l'hôte) : environ 30 % de surcoûts évitables dans
  vixl et FEX → `patches/fexcore-vixl-mission64-speed.patch`.
- `memcpy` de la libc du jeu redirigé en mémoire vers un service hôte, `maxps4_memcpy`
  (`MAXPS4_HLE_MEMCPY=0` pour revenir en arrière) : le chargement copiait 183 Mo dans le simulateur.
- Échantillonneur de fonctions invitées (`MAXPS4_GUEST_SAMPLES=1`) :
  - au démarrage, `atan2f` de la libc (tables trigonométriques) ;
  - au début de l'affichage, le mixage audio : jusqu'à 84 % du CPU, car le thread audio,
    toujours en retard sur le temps réel, n'attend jamais ;
  - ensuite, le rendu logiciel RSDK (boucle par pixel RGB565 de fondu et de teinte).
- `MAXPS4_AUDIO_TIME_SCALE=N` : chaque tampon audio compte N fois sa durée. Le son n'est de toute
  façon pas en temps réel à cette vitesse. C'est ce réglage qui a fait apparaître le logo.
- `atan2f` de la libc redirigé de la même façon vers `maxps4_atan2f` (`MAXPS4_HLE_MATH=0` pour
  revenir en arrière) ; les redirections sont décrites dans une table de `OrbisV4ProcessBatch.hpp`.
  Valeurs vérifiées sur 65 536 appels ; le gain de temps n'est pas mesuré à part.
- Boucle de fondu RSDK (`FillScreen`, `eboot+0x21d23b`, repérée par ses 185 octets) remplacée en
  mémoire par le service hôte `maxps4_rsdk_fill` (`MAXPS4_HLE_RSDK=0` pour revenir en arrière).
  Mesure sur 10 min, un run seul : 163 images contre 111, écart pendant le fondu 3,10 s contre
  5,43 s ; les 111 images communes sont identiques au pixel près (empreintes FNV-64).
- Dessin des couches de tuiles RSDK (`DrawLayerHScroll`, `eboot+0x22d1b0`, toute la fonction repérée
  par ses 1398 octets) remplacé par `maxps4_rsdk_hscroll` (même interrupteur `MAXPS4_HLE_RSDK=0`).
  Il prenait 62 % du temps hors fondu. Mesure sur 10 min, un run seul : 314 images contre 163,
  1,68 s par image contre 3,16 s ; les 163 images communes sont identiques au pixel près.
- Copie de l'écran logiciel RSDK vers la texture GPU (`eboot+0x2038c0`, 240 lignes par écran) remplacée
  par `maxps4_rsdk_upload` (même interrupteur). Environ 9 % du temps ; les 314 images communes avec le
  run précédent sont identiques. Gain non mesuré seul (394 images en 10 min, en parallèle d'un autre run).
- `MAXPS4_TSO=0` désactive l'ordre mémoire x86 (sans risque : tous les threads invités tournent sur
  un seul thread hôte). Mesure sur 10 min, un run seul par réglage : 123 images contre 111, écart par
  image 3,07 s contre 3,13 s, aucun plantage. Gain dans le bruit, le défaut reste TSO activé.
- `MAXPS4_MAXINST=32` plantait (`LoadMemX87SVEOptPredicate` non géré par la traduction mémoire) :
  corrigé par `patches/fexcore-mission65-translated-x87-sve.patch`. Mesure sur 10 min, un run seul :
  122 images contre 111, démarrage plus court, mais écart par image 3,31 s contre 3,13 s (bruit).
  Le défaut reste 16 : la boucle de rendu RSDK tient déjà dans un bloc.
- Essayé puis écarté : SVE ne peut pas être désactivé (chemin AVX-256 défectueux connu).

## Après le logo

Le jeu ne bloque pas : il charge la scène suivante depuis `Data.rsdk`, déjà lu en mémoire (aucune
ouverture de fichier). L'écran titre commence à l'image 560 (~50 min de run, MAXINST 16) : la bande
rouge à pierre jaune du bas du logo Sonic Mania apparaît en fondu, nette à l'image 582.

## Lancement

```sh
P=~/PS4/maxps4-sonic-notes; G=~/PS4/CUSA07023/CUSA07023
MAXPS4_AUDIO_TIME_SCALE=8 MAXPS4_WINDOW_TITLE="MaxPS4 - Sonic Mania (CUSA07023)" \
MAXPS4_FRAME_EVERY=1 MAXPS4_QUIET_LOCKS=1 MAXPS4_FRAME_DIR=$P/run-NN MAXPS4_GPU_DUMP_DIR=$P/run-NN \
setsid nohup python3 scripts/run-bounded.py 7200 $P/run-NN/boot.txt \
  build/mission51-release/Bin/maxps4-keroblaster-boot docs/mission64-sonic-mania-names.tsv \
  $G/eboot.bin $G/sce_module/libc.prx $G/sce_module/libSceFios2.prx $G
```

`setsid nohup` fait survivre le lancement à la fin de la session. Pour l'arrêter : `kill` sur le
PID donné par `pgrep -x maxps4-keroblas` (`pkill -f` tue aussi son propre shell).

## Non vérifié

- Kero Blaster après ces changements : aucun dump n'est disponible. La disposition mémoire de
  Kero est conservée (modules réels tous les 4 Mo dès `0x400000`), mais ce n'est pas mesuré.
- Les écrans suivants, les commandes, le son et l'iPhone.
