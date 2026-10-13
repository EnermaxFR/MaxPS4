# Pré-démarrage réel : dossier accessible et premier blocage

Mission #184, base main `7ea4b577` (PR #183). Le travail ouvre une PR brouillon,
sans fusion et sans revendication de démarrage du jeu. Aucun fichier de jeu,
PKG commercial, clé ou licence n'est ajouté au dépôt.

## Constat fourni par le test utilisateur

Le rapport PKG partagé le 13 octobre 2026 identifie via un SFO lisible :
Kero Blaster, CUSA06015, APP_VER 01.00, VERSION 01.02, CATEGORY gd.
Le CNT mesure 10 223 616 octets, possède 26 descripteurs et un PFS de
8 650 752 octets à l'offset 1 572 864. Classes observées : 3 lisibles déclarés,
5 chiffrés, 4 non pris en charge, 13 inconnus, 1 invalide.

**Premier blocage confirmé : le contenu du jeu est inaccessible à ce lecteur.
Le PFS est opaque/non monté ; aucun eboot.bin n'est accessible.** Les octets
inspectés (15 585) sont les ressources admissibles SFO/images. Ils ne renseignent
ni les instructions du jeu, ni ses imports, ses relocations ou son TLS.
Le descripteur metas aliasant la table et le flag entry_names 0x40000000 ne
sont pas une raison de contourner la politique d'accès. Cette trace ne prouve
pas non plus l'absence d'eboot.bin dans le PFS.

Une fixture reproduit les 26 descripteurs, plages et flags de ce rapport dans
PS4PKGInventoryTest. Tous les payloads sont des zéros synthétiques. Elle vérifie
les classes, l'absence de lecture des entrées refusées et le diagnostic PFS ;
elle ne reproduit pas un package commercial ni ses données SFO/images.

## Audit du chemin actuel

| Étape / sources | Pris en charge | Blocage / limite |
| --- | --- | --- |
| PS4PKGInventory / PS4PKGBridge / PS4SFO | CNT, plages, flags, inventaire borné, SFO/images admissibles | Aucun montage/extraction PFS, déchiffrement ou utilisation de clés/licences ; noms = affichage |
| PS4ExecutableImage | ELF64 x86-64 Orbis 0xfe00/0xfe10/0xfe18, PT_LOAD/RELRO, BSS ; staging SELF uniquement pour blocs déjà clairs non compressés | Staging distinct de la liaison ; 32 Mio mémoire par image ; SELF protégé refusé |
| OrbisMetadata / OrbisAnalysisBridge | Analyse des types ELF standard/SCE, segments, tables DT standard/SCE, descripteurs et NID qualifiés, RELA, PT_TLS, init/fini | Analyse seule ; SELF refusé même si le staging séparé accepte certains SELF clairs |
| OrbisLinker | Graphe explicite jusqu'à 128 modules ; noms/versions exacts ; SCE DYNLIBDATA ; RELA 64/GLOB_DAT/JUMP_SLOT/RELATIVE ; NONE ignorée ; RELRO finale, rollback | Pas de découverte/graphe/biais implicites ; TLS, init/fini, tags/headers inconnus refusés ; 64 Mio mémoire par lot |
| PS4DynamicLinker | Démonstrations bornées à tables DT standard et trampolines invités libkernel | Ne remplace pas le linker Orbis ; SCE/PRX/TLS généraux refusés |
| PS4Libkernel / PS4Threads | open/read/close/lseek/stat, mémoire/temps, identité bootstrap et mutex non bloquants, montage hôte app0 explicite | Pas de services PS4 complets ni création/ordonnancement général de threads ; reconnaissance NID ≠ binding |
| maxps4_fex_engine_prepare_orbis / run_orbis | Liaison, pages possédées, admission finie des instructions avant InitCore puis FEX réel pour le sous-ensemble admis | Registre HLE vide dans cette voie ; pas d'espace d'adresses/ABI/TLS général d'un jeu ; aucun repli simulant un succès |
| FEXCore JIT iPhone | Fournisseur StikDebug et diagnostics existants ; backend réel exposé | Autorisation et fonctionnement doivent être mesurés sur l'appareil ; compilation/fixture ≠ preuve iPhone |
| PM4 / Metal / scène #183 | Scène synthétique avec pixels/input invités et rendu existant | Pas de rendu ni audio PS4 général connecté ; aucune scène substituée à Kero Blaster |

L'amélioration générique de cette PR est l'admission de R_X86_64_NONE par le
linker utilisé par le runtime. L'analyse annonçait déjà cette relocation comme
prise en charge, alors que ce linker la refusait. Selon l'ABI AMD64, elle ne lit
pas de symbole/cible/addend et n'écrit rien. Les tests vérifient les valeurs
inutilisées hors limites, le chevauchement avec une vraie relocation, les
compteurs distincts et le rollback en cas d'erreur ultérieure. Cette correction
est justifiée par les fixtures et l'audit ; aucun besoin NONE de Kero Blaster
n'est déduit de la trace PKG.

## Flux iPhone en lecture seule

1. Dans **Import → Dossier de jeu accessible**, sélectionner un dossier de
   fichiers déjà accessibles par des moyens autorisés, avec eboot.bin à la racine.
   Les PRX/sPRX et données peuvent rester dans leurs sous-répertoires.
2. Le précontrôle ne copie rien dans la bibliothèque et n'écrit rien dans la
   source. L'accès security-scoped ne dure que pendant chaque opération.
   Pas de bookmark persistant : sélectionner à nouveau après fermeture.
3. Le rapport v1 contient des chemins relatifs triés, tailles, présence SFO,
   métadonnées de chaque ELF/PRX, segments, NID, RELA, TLS/init/fini, DT_NEEDED,
   fournisseurs déclarés et blocages. Il ne suppose pas qu'un export candidat
   ou un service HLE reconnu est effectivement lié. SFO du dossier est seulement
   inventorié ; la version y reste non vérifiée.
4. **Exporter le rapport texte** ouvre le partage système. Les mêmes octets et
   chemins donnent le même texte ; pas de date ou chemin absolu dans le rapport.
   Les caractères de contrôle des noms sont échappés.
5. **Lancement expérimental FEX — diagnostic** réinspecte le dossier puis appelle
   le chemin borné existant executeOrbisDirectory. Il exige maxps4.json, validé
   par ce chemin, avec root, modules[path/loadBias/dependencies] explicites.
   Ce runtime accepte actuellement uniquement des noms de fichiers simples dans
   le manifeste ; l'inventaire récursif est plus large que le lancement.
   Aucun graphe, biais, trampoline ou manifeste n'est généré à partir du nom
   Kero Blaster. Sans manifeste : refus. Avec manifeste : premier refus réel
   du linker/admission/JIT journalisé ; pas de dispatch si la préparation échoue.

L'inventaire utilise openat/fstatat et O_NOFOLLOW ; liens et fichiers spéciaux
sont refusés sans être ouverts. Limites : 4096 entrées, profondeur 16, 128
exécutables, 65536 symboles/relocations cumulés, 64 Mio d'ELF/PRX lus au total,
rapport de 2 Mio. Une limite dépassée produit un inventaire incomplet refusé,
sans présenter un résultat partiel comme prêt. Les données non exécutables
sont seulement inventoriées. Le changement de source pendant l'analyse n'est
pas une configuration reproductible ; fermer les autres outils d'édition.

## Validation et prochaine preuve nécessaire

Tests locaux Linux : scripts/test-orbis-analysis.sh (métadonnées + nouveau
précontrôle), scripts/test-orbis-runtime.sh (3368 contrôles linker + segments),
scripts/test-ps4-system-services.sh (14 suites + ABI C11), tests PKG inventaire
et métadonnées, ASan/UBSan. LeakSanitizer est désactivé localement car le sandbox
utilise ptrace ; aucune preuve de contrôle des fuites local n'est revendiquée.
Swift, Xcode et FEX ne sont pas installés dans ce workspace.

CI : Linux/Darwin et bridge Swift/iPhone du workflow orbis-analysis ; fixtures
PKG du workflow pkg-inventory ; FEX ARM64/VIXL et scène animée/input #183 du
workflow ps4-system-services (désormais aussi déclenché pour OrbisLinker).
Le workflow fexcore-app exécute le nouveau précontrôle avant de construire et
publier l'artefact MaxPS4-FEXCore-SideStore. Les résultats CI sont à consulter
sur la PR : les commandes ajoutées ne constituent pas des builds verts.

Sur iPhone, installer l'IPA **du commit de cette PR**, activer le fournisseur
JIT existant, vérifier FEX → 42 puis la scène #183 (animation, déplacement,
collision, Action et relâchement), sans les appeler un test du vrai jeu.
Ensuite exporter le précontrôle du dossier de Kero Blaster et le rapport de
la tentative expérimentale. Joindre modèle iPhone, version iOS et commit IPA.

Pour atteindre l'écran titre, il faut d'abord un dossier légalement accessible.
Les nouvelles traces permettront ensuite de prioriser les vrais besoins
ELF/PRX/relocations/TLS/services de ce titre, puis une admission CPU et un rendu
appropriés. Aucun démarrage réel, jouabilité, écran titre ou absence de
régression sur iPhone physique n'est validé par ce travail seul. L'issue #184
reste ouverte pour ces preuves.
