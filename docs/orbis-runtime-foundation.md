# Orbis Runtime Foundation

Cette évolution prépare des **images de données invitées**, sans exécution de
jeu. Elle réutilise le linker existant et les chargeurs natifs ; aucun PFS, DRM,
SELF chiffré, fichier de jeu ou NID Sony ajouté n'intervient dans les fixtures.

## Audit de main (08db54e)

| Composant existant | Fonction déjà implémentée | Blocage observé et décision |
| --- | --- | --- |
| `PS4ExecutableImage` | ELF64 x86-64 Orbis et SELF plaintext, PT_LOAD/SCE_RELRO, BSS, permissions logiques, bornes, 32 Mio/module | Les champs ELF réservés, les déclarations de sections et les plages de métadonnées non mappées nécessitaient des vérifications supplémentaires. Renforcement dans ce chargeur. Le linker refuse toujours tout SELF. |
| `PS4PKGRuntimePlan` | Inventaire PKG, staging autonome, graphe explicite fermé, cycles/ambiguïtés de chemins, 128 modules/64 Mio | L'inventaire ne fournit pas des exécutables depuis un PFS opaque. Réutilisation du graphe existant ; aucun changement du parseur PKG. |
| `OrbisMetadata` / `OrbisPRXPreparation` | Inventaire SCE, reconnaissance HLE, staging PRX expérimental, RELA et diagnostic | Chemin d'analyse distinct, pas un binding de runtime. Conservé pour compatibilité ; le nouveau rapport utilise le linker transactionnel existant, sans nouveau chargeur. |
| `OrbisLinker` | Identités NID typées, exports exacts, RELA 1/6/7/8, graphe déterministe, transaction forte, W^X et RELRO | Obligeait un ELF statique à posséder des tables SCE ; les dépendances inutilisées n'étaient pas vérifiées ; DT_NEEDED refusé ; addresses rebased vérifiées pour overflow mais pas canonicalité. Corrigé ici. |
| `OrbisLinkerBridge` | Handle propriétaire, mapping invité, entrée et copie de données, sans pointeur hôte dans l'ABI | Diagnostic limité à la première erreur. Ajout d'une API de rapport de préparation, compatible avec les appels existants. |
| `PS4VirtualMemory`, libkernel et threads | Mappings anonymes privés 16 Kio, protections, unmapping, horloges, fichiers/mutex bornés | Attachés au chemin GuestELF existant ; aucune connexion automatique avec les images multimodules SCE. Leurs régressions restent exécutées. |
| `PS4DynamicLinker` / `maxps4_fex_runtime` | Chemin synthétique borné à tables DT standard, trampolines libkernel, admission d'instructions et FEX | Ne prend pas en charge les tables SCE/graphes PRX généraux. FEX reçoit des adresses hôte rebased pour le sous-ensemble admis, pas un backend d'espace d'adresses Orbis arbitraire. Pas de transfert d'un pointeur de vecteur au RIP FEX. |
| Vue diagnostic iOS / `OrbisAnalysisBridge` | Sélection d'un fichier et partage du rapport via Swift/C | Le rapport d'inventaire ne prouvait pas une préparation runtime. Il inclut désormais une tentative native réelle, temporaire, sans fournisseur externe ni registre HLE fictif. |

## Comportement ajouté

Un `ET_SCE_EXEC` sans PT_DYNAMIC ni DYNLIBDATA peut être préparé seul, avec
plusieurs segments et BSS. Un exécutable dynamique ou PRX exige toujours les
tables SCE STRTAB/SYMTAB et les formats explicitement implémentés. Aucun nom de
fichier n'accorde l'admission ELF. Les headers runtime inconnus (notamment
PT_INTERP et les paramètres SCE non interprétés) sont refusés ; les headers
acceptés sont NULL, LOAD, DYNAMIC, NOTE, PHDR, TLS (refusé ensuite), DYNLIBDATA,
SCE_RELRO. Aucun support implicite des ELF Linux ou SELF protégés.

Le chargeur vérifie les octets e_ident réservés, e_flags et la cohérence de la
table de sections. Sans sections, e_shentsize 0 ou 64 est accepté ; la numérotation
étendue n'est pas implémentée. Les données de chaque header ELF file-backed
restent dans le fichier, même pour les métadonnées non mappées.

Le linker valide les limites de requêtes **avant** leur copie/tri et les tailles
de fichiers avant lecture des headers. Il refuse tout segment rebased dont la
fin dépasse `2^47`, en plus des vérifications d'overflow et d'alignement déjà présents.
Les régions invitées ne se chevauchent pas. Les permissions ELF R=4/W=2/X=1
restent distinctes des protections PS4 PROT_READ=1/WRITE=2/EXEC=4.

Chaque descripteur `DT_SCE_NEEDED_MODULE` et `DT_SCE_IMPORT_LIB` doit trouver un
fournisseur exact dans les dépendances explicitement fournies : nom et version,
même sans symbole utilisant ce descripteur. Le diagnostic distingue fournisseur
absent, version incompatible, dépendance non déclarée et ambiguïté. `DT_NEEDED`
est un offset borné dans la table SCE de chaînes ; sa valeur doit nommer un
chemin exact déclaré dans le graphe. Les valeurs vides/dupliquées/invalides sont
refusées. Il n'y a aucune recherche implicite sur disque ni résolution par suffixe.

Les imports référencés ou inutilisés doivent toujours trouver un export complet
compatible (NID/bibliothèque/module/versions/type). Les imports faibles absents
restent refusés. Les quatre formules RELA existantes et leurs contrôles sont
conservés. Aucune relocation inconnue n'est différée silencieusement. Les exports FUNC
doivent entièrement appartenir à du code RX file-backed : une fonction située
dans le BSS ne suffit pas à résoudre un import.

## Diagnostic et transaction

`OrbisLinker::Report`, argument optionnel de `link`, décrit l'étape atteinte,
les ELF/segments validés, les dépendances vérifiées, les imports examinés/résolus,
les relocations examinées/validées/appliquées/différées/refusées et la mémoire.
Sur erreur, les compteurs sont **partiels** et ne prétendent pas décrire les
étapes suivantes ; zéro relocation est annoncée appliquée et aucune nouvelle
image n'est engagée. Les écritures validées mais annulées avec la transaction
sont comptées comme différées, sans possibilité de reprendre l'image rejetée.
La première cause explicite est conservée. Les limites du sous-ensemble constituent
un refus de préparation, pas une réussite d'exécution.

`maxps4_orbis_link_with_report` expose cette préparation au pont C/Swift,
avec les mêmes inputs et le même handle que `maxps4_orbis_link`. Prévoir un
buffer de 4096 octets. Si le rapport de succès ne tient pas, `-ENOSPC` est retourné
et le handle reste nul. Les rapports de refus peuvent être tronqués selon la
capacité et restent terminés. Les callers sérialisent les appels et conservent
les buffers d'entrée immuables pendant l'appel.

La vue « analyse Orbis » existante exécute une préparation du fichier sélectionné
avec zéro dépendance fournie : un PRX externe manquant reste manquant. La
reconnaissance HLE de l'inventaire est explicitement distinguée des bindings
runtime. L'image temporaire est détruite après le diagnostic ; elle n'est pas
conservée pour exécution. Le rapport affiche toujours que le backend général
FEX Orbis, la pile/TLS et l'ABI manquent. Le pont multimodule existe pour un futur
sélecteur de modules ; la sélection iOS actuelle porte sur un seul fichier.

`Image::mappings()` emprunte les bytes propriétaires de l'image : ils restent
valides lors d'un déplacement de cette image et deviennent invalides à son
remplacement/destruction. Le backend doit copier les bytes ou conserver leur
propriétaire. Le pont C n'expose pas ces pointeurs. Les segments restent des
vecteurs de données hôte, sans PROT_EXEC. RELRO passe de RW à R après les
relocations. Les protections sont logiques ; les régions ne constituent pas
encore des mappings de pages FEX. Des segments logiques peuvent partager une
page hôte future : un backend devra décider/refuser leurs contraintes de pages,
sans élargir silencieusement les droits.

## Tests reproductibles

```sh
bash scripts/test-orbis-runtime.sh
bash scripts/test-orbis-analysis.sh
bash scripts/test-ps4-system-services.sh
```

Le nouveau script compile en C++17 avec warnings fatals, ASan/UBSan et lance
les tests sous délai borné. Les fixtures restent synthétiques. Le workflow PKG
exécute le même script sur Linux et macOS, puis les tests Swift/C et la compilation
iOS ARM64 existants. Le workflow IPA SideStore garde son test linker, les deux
ponts natifs et son packaging ; aucune modification du chemin d'inventaire PKG.

Les régressions incluent ELF statique multisegment, BSS/permissions et limites
exactes, troncature à chacune des 3072 longueurs du fichier, headers réservés et
sections incohérentes, plage metadata invalide, adresses rebased non canoniques,
limites de requêtes, dépendance inutilisée, trois modules/deux dépendances,
DT_NEEDED valide/dupliqué/hors limites, module/version/bibliothèque incompatible,
identité ambiguë, refus de headers runtime inconnus et rapport d'échec tardif.
Les checks existants de NID absent, exports ambigus, quatre relocations, cycles,
RELRO, dépassements, transaction précédente intacte, registre HLE et ABI restent
actifs. Les rapports Swift sont vérifiés dans les workflows macOS/iOS.

Les observations locales et vérifications impossibles sont séparées dans
[orbis-runtime-validation.md](orbis-runtime-validation.md).

## Fonctionnalités encore nécessaires avant un jeu

| Fonction | État après cette PR | Travail nécessaire |
| --- | --- | --- |
| ELF/PRX plaintext SCE | Préparation bornée, déterministe, transactionnelle | Étendre seulement avec formats et fixtures documentés |
| Dépendances/NID | Vérification exacte du graphe fourni et exports compatibles | Fournisseurs légaux réels ; catalogue HLE vérifié, aucun symbole inventé |
| Relocations | RELA x86-64 64/GLOB_DAT/JUMP_SLOT/RELATIVE | TLS, PC-relative, autres tags/types selon besoins ; inconnus refusés |
| Mémoire/FEX | Contrat de mappings logiques et copie C ; W^X/RELRO | Backend FEX multimodule, politique de pages, durée de vie, faute mémoire/signal |
| Processus/threads | Services synthétiques bornés existants | Pile de processus, arguments, TLS par thread, schedulers/ABI Orbis |
| Cycle de vie PRX | Ordre de dépendances préparé, cycles refusés | Paramètres SCE, init/fini, unload coordonné ; aucune fonction appelée |
| x86-64 vers ARM64 | Test synthétique borné existant, distinct des images SCE générales | Relier les mappings généraux au véritable FEX puis tester sur ARM64/iPhone |
| Diagnostic iPhone | Analyse + préparation temporaire native d'un fichier ; pont multimodule | Sélecteur de modules multiples et gestion durable de l'image |
| GPU/audio/entrée | Prototypes bornés existants | ABI, synchronisation et pipeline complet pour un jeu |
| Données protégées | PFS/DRM opaque, inchangé | Aucune décryption ni contournement dans cette mission |

La préparation réussie des fixtures ne constitue pas une exécution PS4 réelle.
