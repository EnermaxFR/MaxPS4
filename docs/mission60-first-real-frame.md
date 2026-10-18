# Mission 60 — premier pipeline graphique réel (rapport d'étape, mission interrompue)

Mission arrêtée à la demande de l'utilisateur le 18 octobre 2026 ; à reprendre.

## 1–2. Commits

- Commit initial : `fc256ac` (branche `experiment/keroblaster-iphone-m53`).
- Commits de la mission : `43090fa` (suivi du décodeur PS de la mission 59, inchangé), `2691e29` (interpréteur GCN graphique + tuilage), `5f39e88` (pipeline logiciel), `b351db6` (détuilage VideoOut), puis le commit de sauvegarde contenant ce rapport, la correction d'orientation et les diagnostics.

## 3. Fichiers

- Nouveaux : `Native/GCNShader.hpp`, `Native/GCNTiling.hpp`, `Native/GCNRenderer.hpp`, `Tests/GCNRendererTest.cpp`.
- Modifiés : `Native/GNMCommandExecutor.hpp` (dessins exécutés par le renderer, valeurs par défaut de `CLEAR_STATE`), `Native/OrbisV4GraphicsServices.hpp` (flip tuilé, `GPU.PRESENT`), `Tests/GNMCommandExecutorTest.cpp`, `Tests/OrbisV4GraphicsServicesTest.cpp`, `scripts/test-orbis-graphics.sh`.
- Les autres modifications locales et fichiers non suivis antérieurs sont intacts. Aucun fichier du jeu n'a été modifié (SHA-256 vérifiés après chaque lancement).

## 4. Instructions GCN (gfx7) implémentées dans `GCNShader`

SOP1 (mov b32/b64, not, wqm, getpc/setpc/swappc, *_saveexec), SOP2 (arith., logique 32/64, décalages, min/max, cselect, bfe, mul), SOPK, SOPC, SOPP (branches, waitcnt, endpgm), SMRD (s_load/s_buffer_load x1–x16), VOP1/VOP2/VOPC/VOP3 (flottants avec abs/neg/clamp/omod, entiers, retenues VOP3b, comparaisons et CMPX, mad/fma/med3, conversions dont `v_cvt_pkrtz_f16_f32`), VINTRP (p1/p2/mov), EXP (pos/param/MRT, compressé), MUBUF/MTBUF (chargements formatés avec conversions et hors-bornes à zéro), MIMG (`image_sample`/`_l`/`_lz`, `image_load`, filtrage point/bilinéaire, wrap/clamp/mirror, sRGB). Toute instruction inconnue échoue avec son PC.

Les décodages ont été vérifiés à la main sur les octets du jeu (`texture_vv.sb`, `texture_p.sb`, `rectangle_p.sb`) et du VS embarqué ; aucun désassembleur gfx7 n'est disponible localement (LLVM 21 refuse).

## 5. Pipeline (`GCNRenderer`)

Indices (auto, 16/32 bits, offset/clamp), VS par waves de 64 avec réutilisation des sommets, triangle list/strip et rect list, clipping homogène, viewport, rasterisation en virgule fixe 1/256 avec règle top-left, culling, PS en quads 2×2 (WQM, I/J perspective), interpolation `SPI_PS_INPUT_CNTL`, clear/test de profondeur (plan de profondeur côté hôte, non écrit en mémoire invitée), blending sRGB, écriture 8_8_8_8 linéaire ou tuilée Display 2D-thin. `CLEAR_STATE` charge désormais les valeurs matérielles par défaut des ciseaux, `VGT_MAX_VTX_INDX`, masques et plage Z.

Correction issue du jeu réel : `FACE` désigne le sens visuel à l'écran (y vers le bas). Avec la convention inverse, run-01 éliminait les 1032 triangles de sprites.

## 6–7. Tests

`scripts/test-orbis-graphics.sh` : 10 suites, natif et ASan/UBSan, toutes réussies (`ASAN_OPTIONS=detect_leaks=0`, LeakSanitizer non validé). `GCNRendererTest` vérifie la bijection du tuilage 1080p, FP16/sRGB, l'interpréteur, un clear rect-list (8192 px), un dessin indexé avec appel de fetch shader, matrice, texture, culling et blending (erreur ≤ 1/255 par canal), les refus explicites, et — avec `MAXPS4_KERO_DIR` — l'exécution des vrais `texture_vv.sb`/`texture_p.sb` du jeu (1024 px exacts). Test de flip tuilé ajouté.

## 8–11. Lancement de Kero Blaster (journaux `mission60-logs/run-01`, `run-02`)

run-02 (après correction d'orientation), arrêté manuellement après 2 flips :

| Dessin | Type | Résultat réel |
|---|---|---|
| 1 | rect list, VS embarqué, PS clear | clear de profondeur : 2 073 600 écritures de profondeur, 0 pixel couleur (`CB_TARGET_MASK=0`) |
| 2 | 24 indices, `texture_vv`/`texture_p` | 2 triangles rasterisés, 2 073 600 pixels écrits, 2,08 M échantillons |
| 3 | 3072 indices | 28 triangles rasterisés (996 éliminés), 29 952 pixels écrits, zone 104,96–603,143 |

Par frame : 3 dessins exécutés, environ 2,1 M pixels écrits, puis flip détuilé de 1920×1080 (buffers 0 et 1 en alternance).

## 12. Image obtenue

Une image est présentée par le présentateur X11 et capturée (`run-02/first-guest-frame.png`). Elle est uniforme : `distinct_colors=1`, même hash FNV que run-01. Il s'agit de la couleur de fond écrite par le compute du jeu (`0xff383819`). **Ce n'est pas encore une image de jeu visible.** Les pixels écrits par les dessins 2 et 3 n'ont pas modifié l'image.

Explication la plus probable, non encore prouvée : les textures échantillonnées (`0x2561d1800`, 4096², et `0x2521c0000`) sont encore vides (alpha 0). Le jeu n'a fait que 11 `sceKernelOpen` et 8 `sceKernelRead`, sans charger `rsc_k`. Le blending `SRC_ALPHA/ONE_MINUS_SRC_ALPHA` laisse alors la destination inchangée. Les compteurs ajoutés en fin de mission (`changed`, `src_alpha_nonzero`, `texels_nonzero`) n'ont pas encore été exécutés sur le jeu.

## 13. Prochain blocage

Déterminer à partir des nouveaux compteurs si les texels sont réellement nuls, puis suivre le chargement des ressources au fil des frames. Le rendu prend plusieurs secondes par frame dans l'interpréteur, ce qui ralentira l'observation des écrans suivants.

## 14. Mission suivante recommandée

1. Relancer avec les nouveaux diagnostics et un délai plus long, puis vérifier `texels_nonzero` et `changed`.
2. Si les textures restent vides, tracer le chemin de chargement des ressources (Fios2/lectures `rsc_k`, copies vers l'atlas).
3. Accélérer le PS plein écran (cache de décodage par draw déjà présent ; éviter les `std::function` par lane, lecture de texels par lignes).
4. Ensuite, prévoir un backend Metal qui consomme la même description de dessin validée. Aucune IPA ni test iPhone n'a été fait dans cette mission.
