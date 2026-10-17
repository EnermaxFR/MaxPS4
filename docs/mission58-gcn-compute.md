# Mission 58 — exécution compute GCN de Kero Blaster

## Périmètre et état initial

Travail sur `experiment/keroblaster-iphone-m53`, à partir de `bdfd0d4`. Le rapport [mission 57](mission57-resume-validation.md) a été lu intégralement. L’état initial et le diff des fichiers suivis sont conservés dans `mission58-logs/initial-status.txt` et `initial-local.patch`. Les 40 fichiers suivis initialement modifiés en dehors des deux fichiers GNM ciblés ont été comparés octet pour octet à leur état initial ([preuve](mission58-logs/local-work-preservation.txt)). Les modifications locales et fichiers non suivis antérieurs sont conservés ; aucun reset, clean, push, changement de branche ni modification des exécutables commerciaux.

Blocage initial : première soumission DCB à `0x2101703f8`, 3 984 octets, arrêt PM4 à `0x210170a34`, opcode `0x15` (`DISPATCH_DIRECT`), programme CS `0x250180900`, groupes `(32640,1,1)`, dimensions locales `(64,1,1)`, initiateur `1`.

Les mécanismes existants effectuent déjà la copie possédée des commandes PM4, les écritures de registres, certains transferts DMA, les fences et la présentation VideoOut. Ils ne disposent pas d’un exécuteur général de shaders GCN ni d’un rasterizer capable de produire les pixels du jeu. Les chemins Metal existants ne remplacent pas ces opérations.

## Shader et ressources

Le préfixe capturé en mission 57 contient une séquence terminée par `s_endpgm` à l’offset `0x48`, soit **76 octets, 15 instructions**, hors métadonnées suivant le programme. Le nouveau décodeur traite cette séquence complète : [diagnostic](mission58-logs/shader-decode.txt).

Le profil retenu est l’ISA GCN1/2 de la PS4 de base, encodages vérifiés avec l’assembleur LLVM 21 pour **gfx700**, et la documentation publique [AMD Sea Islands](https://www.amd.com/content/dam/amd/en/documents/radeon-tech-docs/instruction-set-architectures/sea-islands-instruction-set-architecture_0.pdf) / [Southern Islands](https://docs.amd.com/v/u/en-US/southern-islands-instruction-set-architecture). Le désassembleur installé refuse gfx700 ; le décodage gfx803 exploratoire de mission 57 n’a pas été utilisé comme référence sémantique.

Les 14 instructions après le premier MOV correspondent octet pour octet à l’assemblage gfx700. LLVM canonicalise le premier MOV en constante inline ; le jeu utilise la forme avec DWORD littéral `13`. Les deux formes sont décodées, et la longueur du littéral est testée. Voir [assemblage](mission58-logs/shader-gfx700.s), [encodages](mission58-logs/shader-gfx700-encoding.txt) et [comparaison](mission58-logs/shader-encoding-comparison.txt).

| Offset | Instruction | Effet utilisé |
|---|---|---|
| `00` | `s_mov_b32 vcc_hi, literal(13)` | registre scalaire spécial |
| `08` | `s_mul_i32 vcc_lo, s12, 64` | base d’index du groupe |
| `0c` | `s_buffer_load_dword s12, s[8:11], 0` | borne du dispatch |
| `10` | `v_add_i32 v0, vcc, vcc_lo, v0` | index global, carry |
| `14` | `s_waitcnt lgkmcnt(0)` | attente des lectures synchrones |
| `18` | `v_cmpx_gt_u32 vcc, s12, v0` | restriction EXEC |
| `1c` | `s_cbranch_execz +10` | groupe inactif vers END |
| `20` | `s_buffer_load_dword s8, s[8:11], 1` | masque de répétition |
| `24` | `s_waitcnt lgkmcnt(0)` | attente scalaire |
| `28` | `v_and_b32 v1, s8, v0` | index du motif |
| `2c` | `buffer_load_format_x v1, v1, s[0:3], 0 idxen` | lecture R32_UINT |
| `34` | `v_cmpx_gt_u32_e64 s[0:1], s6, v0` | borne du buffer destination |
| `3c` | `s_waitcnt vmcnt(0)` | attente vectorielle |
| `40` | `buffer_store_format_x v1, v0, s[4:7], 0 idxen` | écriture réelle |
| `48` | `s_endpgm` | fin de wave |

Registres de mission 57 : `COMPUTE_PGM_RSRC1=0x002c0040` (4 VGPR, 16 SGPR alloués), `RSRC2=0x98` (12 SGPR utilisateur, ID de groupe X). Les SGPR utilisateur proviennent des registres SH `0x240..0x24b`. `s12` contient initialement l’ID de groupe X, `v0` l’ID local X.

| SGPR | Base mémoire invitée | Stride | Records | Rôle |
|---|---|---:|---:|---|
| `s[0:3]` | `0x210170934` | 4 | 1 | motif source R32_UINT |
| `s[4:7]` | `0x250660000` | 4 | `0x1fe000` | destination R32_UINT |
| `s[8:11]` | `0x2101709b0` | 16 | 1 | paramètres scalaires |

Le shader calcule `i = groupe_x * 64 + lane_x`, restreint EXEC selon la borne scalaire, lit `source[i & masque]`, puis écrit `destination[i]` si l’index est inférieur au nombre de records destination. Les valeurs scalaires effectivement lues et les écritures vérifiées sont journalisées pendant l’exécution, sans remplacement par une opération spéciale codée pour ce jeu.

## Backend implémenté

`GCNCompute.hpp` fournit un interpréteur C++17 synchrone : décodeur, SGPR, VGPR par lane, VCC/carry, masque EXEC, branche avant, accès scalaires et buffers linéaires R32_UINT. Toutes les instructions du tableau sont exécutées. Les données passent exclusivement par la lease de traduction mémoire invitée ; aucun pointeur invité n’est converti directement en pointeur hôte.

Le programme est copié et décodé avant les effets mémoire. Les branchements doivent cibler des instructions, jamais un littéral. Le calcul de base des descripteurs utilise le profil PS4 sur **40 bits**, vérifié dans la [définition primaire des ressources shadPS4](https://github.com/shadps4-emu/shadPS4/blob/main/src/video_core/amdgpu/resource.h). Les bits hauts non adressants du mot 1 ne sont pas transformés en adresse invitée ; les droits du mapping effectif restent contrôlés. Un test injecte ces bits dans les trois descripteurs.

Les registres non initialisés, formats/flags inconnus, dépassements de ressources et mappings inaccessibles produisent une erreur avec PC, mot brut pour les erreurs de décodage, et groupe courant. Chaque store exige un mapping RW/NX et fait l’objet d’une relecture comparée à la valeur calculée. Le compte des stores vérifiés est distinct du compte des groupes exécutés.

L’exécution séquentielle complète les lectures/écritures avant `s_waitcnt` ; des fences C++ bornent les attentes et la fin du dispatch. **Les barrières LDS, atomiques et la mémoire partagée ne sont pas simulées** : leurs instructions ou ressources sont refusées. La fence PM4 `EVENT_WRITE_EOS` est prise en charge pour `CS_DONE`, index 6, commande 2 (store DWORD immédiat), après achèvement synchrone du compute. Les autres événements/commandes, GDS et destinations invalides sont refusés. Le format a été vérifié dans la [définition des paquets shadPS4](https://github.com/shadps4-emu/shadPS4/blob/main/src/video_core/amdgpu/pm4_cmds.h). Les tests vérifient la fence puis `WAIT_REG_MEM`, et l’absence de signal si le compute échoue.

Une erreur mémoire tardive peut laisser les écritures valides déjà effectuées ; le dispatch échoue alors, sans événement de succès ni flip fabriqué. Il n’existe pas de rollback global.

Limites explicites : 256 DWORD de code maximum, 4 194 304 invocations et 67 108 864 instructions de wave par dispatch ; une seule wave par groupe, taille locale totale de 1 à 64. IDs X/Y/Z pris en charge selon l’ABI configurée. Départs de dispatch non nuls, scratch, trap, LDS, groupes multi-wave, branches arrière, autres opcodes, autres formats et adressage swizzlé restent refusés. Les dépassements de buffers sont refusés, sans prétendre reproduire tous les comportements matériels de retour zéro/discard.

## Tests et corrections

Nouveaux tests : décodage/littéraux/troncature, cible de branche, refus barrier/opcode/modificateurs, initialisation des registres, multiplication modulo 32 bits, carry VCC observé en mémoire, constantes négatives inline, masques EXEC partiels et groupe vide, répétition de quatre valeurs sources, borne distincte de destination, accès sur mappings adjacents, mappings absents et lecture seule, dimensions/budgets, configuration 3D, scratch/LDS interdits et intégration PM4 suivie d’une écriture fence. Un test grandeur réelle exécute 32 640 groupes et compare les 8 355 840 octets produits à une valeur non nulle attendue, avec sentinelles de part et d’autre du buffer : [normal](mission58-logs/compute-full.txt), [ASan/UBSan](mission58-logs/compute-full-sanitized.txt).

La suite graphique couvre aussi les tests existants PM4, soumission, préparation Metal, texture linéaire, GNM et VideoOut. Les assertions qui attendaient l’ancien refus compute ont été adaptées à la validation réelle des ressources.

Les premiers essais ont révélé un verrou réentrant dans **le test** (appel de protection avec lease déjà tenue), corrigé en préparant le mapping lecture seule avant la lease. Deux anciens messages attendus par les tests graphiques ont été corrigés. Les premières compilations strictes signalaient la mise en forme compacte du nouveau header : formatage corrigé. Un essai de la suite a aussi été interrompu par une modification du script pendant son exécution (erreur shell de fin de fichier) ; la suite a ensuite été relancée intégralement avec les fichiers figés. Aucun de ces essais n’est présenté comme réussi.

Validation finale : huit suites graphiques, chacune en mode normal et ASan/UBSan, réussies avec `ASAN_OPTIONS=detect_leaks=0` : [journal final](mission58-logs/graphics-index-state.txt). LeakSanitizer n’est pas validé dans ce sandbox. Le nouveau header passe `-Wall -Wextra -Werror` isolément. Le test compute passe également avec Clang 21 ; ces tests restent des tests hôtes Linux x86_64.

## Intégration et affichage

Commande demandée exécutée, après compilation du target `maxps4-keroblaster-boot`. L’essai initial [boot.txt](mission58-logs/boot.txt) atteint les lectures du shader mais refuse le premier store : la première version utilisait les 48 bits de la description générique AMD, donnant une adresse erronée à partir de `s5=0x00047002`. Ce lancement n’a exécuté aucun store. Le décodage a été corrigé sur la base de la disposition PS4 sur 40 bits, avec test de régression. `run-02`, lancé avec l’ancien décodeur, a été arrêté ; son intégrité a été contrôlée séparément.

Le lancement corrigé [run-03](mission58-logs/run-03/boot.txt) termine le shader réel :

```text
GCN.SCALAR address=0x2101709b0 value=0x001fe000
GCN.SCALAR address=0x2101709b4 value=0x00000000
GCN.RESULT success=1 groups=32640 lanes=2088960 instructions=489600
loads=2154240 stores=2088960 verified=2088960
first=0x250660000:ff383819 last=0x250e57ffc:ff383819
```

Le motif `0xff383819` est chargé depuis la mémoire du jeu, puis écrit par les instructions interprétées ; il n’est pas choisi par le backend. Chaque DWORD écrit a été relu et comparé. Ce dispatch produit **8 355 840 octets** dans la surface commençant à `0x250660000`. Les paramètres observés sont une borne de `2 088 960` et un masque de répétition nul.

Le compute, `EVENT_WRITE_EOS` et son `WAIT_REG_MEM` sont franchis. `run-03` s’arrête ensuite à `0x210170d8c`, opcode `0x2a`, valeur brute INDEX_TYPE `0x400`, devant `DRAW_INDEX_AUTO(3,2)`. Durée 222 537 ms, code 4, 3 692 services sur deux threads. L’exécuteur conserve désormais ce registre brut : un dessin à indices générés ne consomme pas d’index mémoire ; les flags inconnus restent refusés lors d’un vrai fetch indexé. Des tests vérifient la conservation de `0x400`, la poursuite jusqu’au refus du renderer et le refus maintenu du fetch non modélisé.

Le dernier lancement [run-04](mission58-logs/run-04/boot.txt) confirme à nouveau les **2 088 960 stores vérifiés**, puis franchit INDEX_TYPE. Arrêt final :

```text
PM4 pc=0x210170d94 opcode=0x2d:
guest GCN draw needs shader execution/rasterizer; count=3
VS=0x109cf00 PS=0x250180a00 CB_COLOR0_BASE=0x250660000
```

Durée **226 326 ms**, code de sortie **4**, 3 692 services, deux threads. Ce dessin de trois sommets n’est pas annoncé réussi. Le blocage compute initial est donc dépassé ; le blocage courant est le pipeline de shaders graphiques et la rasterisation. Voir [extrait vérifiable](mission58-logs/runtime-result.txt) et [résumé JSON](mission58-logs/summary.json).

Les tests hôtes n’ont relevé aucune erreur ASan/UBSan ; le lancement réel Release lui-même n’est pas instrumenté par ces sanitizers. Les fichiers source et le binaire correspondant au dernier essai sont identifiés dans `run-04/runtime-source.sha256`.

VideoOut enregistre deux buffers **1920×1080**, pitch 1920, format `0x80000000`, mode tiled `0`, premier buffer `0x250660000`. Le compute modifie donc réellement la mémoire du premier buffer enregistré. Ce remplissage d’une valeur constante ne constitue pas une image du jeu rendue. Aucun flip réussi, menu ou gameplay n’est validé. Le chemin VideoOut exige encore le détuilage display-macro pour ce mode ; une lecture linéaire serait incorrecte.

Les shaders VS/PS et les registres du prochain travail graphique ont été capturés pour diagnostic : [audit des registres](mission58-logs/graphics-register-audit.txt). Au prochain dessin, `CB_COLOR0_BASE << 8 = 0x250660000`, PS `0x250180a00` et VS embarqué `0x109cf00`. Le buffer de profondeur est distinct (`DB_Z_READ_BASE = 0x2516500`, unité 256 octets). Le PS suivant utilise notamment `S_BUFFER_LOAD_DWORDX4`, `V_MOV_B32` et `EXP`, qui ne font pas partie du backend compute ; ses encodages ont été comparés à l’assemblage gfx700 ([résultat](mission58-logs/next-ps-comparison.txt)). Aucun export pixel n’a été exécuté. Le rendu des shaders graphiques, la rasterisation, les surfaces de profondeur et la présentation du framebuffer tiled restent à implémenter et valider. Les seuls résultats mémoire prouvés ici sont ceux des tests et des stores compute contrôlés ; ce n’est pas une preuve globale d’absence de toute corruption dans le runtime FEX.

Les SHA-256 des trois exécutables originaux sont identiques avant/après tous les essais terminés. Les captures et journaux complets restent dans `docs/mission58-logs/`.

## Compatibilité iPhone

Le backend est portable C++17, sans code machine hôte généré, dépendance X11/LLVM à l’exécution, allocation exécutable ni modification des protections W^X. Les registres d’une wave occupent environ 64 Kio pour les VGPR plus les SGPR et métadonnées ; les tailles sont bornées et réutilisées groupe par groupe. Il utilise la traduction mémoire existante et ne modifie pas FEXCore. Une future traduction des opérations vers Metal pourra reprendre le décodage et les tests de résultats ; aucune exécution Metal de ce shader n’est revendiquée.

Aucun build iOS, test iPhone ARM64, premier affichage ou jeu jouable sur iPhone n’est validé par ces tests Linux.

## Fichiers et commits

- Nouveau `MaxPS4-iOS/Native/GCNCompute.hpp` et `MaxPS4-iOS/Tests/GCNComputeTest.cpp`.
- Intégration dans `GNMCommandExecutor.hpp`, commentaires de `OrbisV4GraphicsServices.hpp`.
- Assertions adaptées dans `GNMCommandExecutorTest.cpp` et `OrbisV4GraphicsServicesTest.cpp`.
- Suite ajoutée dans `scripts/test-orbis-graphics.sh`.
- Ce rapport et diagnostics `docs/mission58-logs/`.

`2f8f167` conserve séparément le DMA et ses tests déjà présents avant cette mission, validés pendant la reprise. Ce commit ne prétend pas attribuer ces modifications antérieures au nouveau backend.

`9b9bfd9` ajoute le backend compute, l’intégration PM4 et les premiers tests.

`2f18b64` ajoute le décodage PS4 des bases sur 40 bits, la fence CS_DONE et les tests aux dimensions réelles.

`05ef289` conserve le registre INDEX_TYPE brut et reporte le refus des modes inconnus au fetch indexé, avec tests.

Le commit de documentation suivant ces quatre commits ajoute ce rapport et les diagnostics textuels sélectionnés. Les journaux complets, dumps binaires et sauvegarde initiale restent disponibles localement sans être ajoutés en bloc au commit.
