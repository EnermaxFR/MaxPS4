# Linker Orbis expérimental

Le linker prépare deux ou plusieurs fichiers ELF/PRX Orbis autonomes en mémoire
invitée isolée. Il ne lance aucun jeu. Il ne lit pas le PFS, ne déchiffre aucun
conteneur et ne possède aucune clé, licence ni table NID Sony.

## Audit et séparation des responsabilités

- `PS4ExecutableImage.hpp` conserve le parsing ELF/SELF plaintext et le staging
  des segments (32 Mio/module). Ses validations existantes sont réutilisées.
- `PS4PKGRuntimePlan.hpp` conserve l'inventaire et la validation des dépendances
  explicites (128 modules, 64 Mio/batch). Il ne change pas de comportement.
- `OrbisLinker.hpp` lit les métadonnées dynamiques SCE, indexe les exports,
  résout les imports et prépare toutes les écritures avant de les appliquer.
- `OrbisLinkerBridge.h/.cpp` expose un handle opaque, les cartographies invitées,
  les points d'entrée et des lectures exactes au code natif/Swift. Le pont est
  compilé dans l'application iOS par `project.yml`.
- `maxps4_guest_elf` et `maxps4_fex_runtime` conservent leur admission CPU ELF
  synthétique restrictive. Leur staging PS4 ne devient pas une exécution PS4.

Les cinq workflows associés au commit initial `dd18617` de la PR #173 ont
terminé avec succès (PKG, smoke, heavy, liaison FEX et IPA). Leur état initial
`queued` ne constituait pas une régression du code.

## Sous-ensemble vérifié

Les constantes et champs SCE ont été vérifiés contre les fichiers
[elf.h](https://github.com/shadps4-emu/shadPS4/blob/443f022450f6857256e780daa3d5a14029a748f7/src/core/loader/elf.h),
[module.h](https://github.com/shadps4-emu/shadPS4/blob/443f022450f6857256e780daa3d5a14029a748f7/src/core/module.h)
et [module.cpp](https://github.com/shadps4-emu/shadPS4/blob/443f022450f6857256e780daa3d5a14029a748f7/src/core/module.cpp).
L'implémentation bornée est propre à MaxPS4 : elle ne réutilise pas leurs accès
par pointeurs non bornés, leurs stubs d'import inconnu ou leur exécution.

- Un `PT_DYNAMIC` avec terminateur `DT_NULL` et un `PT_SCE_DYNLIBDATA` requis.
  Les offsets SCE sont relatifs au contenu de DYNLIBDATA, pas des pointeurs hôte.
- Tables SCE STRTAB/STRSZ, SYMTAB/SYMENT/SYMTABSZ et RELA/RELASZ/RELAENT,
  ainsi que JMPREL/PLTRELSZ/PLTREL (`DT_RELA = 7`). Entrées ELF64 de 24 octets.
- `DT_SCE_MODULE_INFO`, `NEEDED_MODULE`, `EXPORT_LIB` et `IMPORT_LIB` : offset
  chaîne sur 32 bits, version sur 16 bits, identifiant local sur 16 bits.
  Pour les modules, la version conserve les octets minor/major du format.
- NID encodé de 11 caractères, suivi de `#libraryID#moduleID`. Les identifiants
  locaux utilisent l'alphabet Orbis `A-Z a-z 0-9 + -`. Ils servent à lire les
  descripteurs ; ils ne constituent pas l'identité intermodule.
- Résolution exacte sur NID, noms et versions de bibliothèque/module, type de
  symbole. Aucun hash de nom de fonction, NID réel inventé ou alias automatique.
- Symboles locaux, globaux et faibles de types NOTYPE/OBJECT/FUNC, visibilité
  par défaut, SHN_UNDEF ou indice ordinaire. Les imports faibles manquants sont
  refusés, comme les imports globaux. Les exports dupliqués sont refusés même
  faibles ; aucune priorité dépendant de l'ordre d'entrée.
- Dépendances fournies explicitement par le demandeur : un fournisseur d'import
  doit être déclaré et chargé avant le consommateur. Graphe fermé, sans cycles,
  chemins dupliqués ou dépendances dupliquées. Tri lexical stable des racines et
  dépendances avant la validation existante ; ordre indépendant de l'entrée.

Formules [AMD64 System V ABI, relocations](https://gitlab.com/x86-psABIs/x86-64-ABI):

| Type | Valeur écrite sur 64 bits |
| --- | --- |
| R_X86_64_64 (1) | S + A |
| R_X86_64_GLOB_DAT (6) | S |
| R_X86_64_JUMP_SLOT (7) | S |
| R_X86_64_RELATIVE (8) | B + A, symbole nul requis |

`S` est l'adresse invitée du symbole, `A` l'addend signé explicite RELA, `B` le
biais invité choisi par le demandeur. Les dépassements et sous-dépassements sont
refusés, sans wraparound. Le biais ne réécrit pas les métadonnées module-locales.
Aucun type inconnu (y compris TLS, COPY, IFUNC, PC32, REL) n'est silencieusement
ignoré. Les autres tags dynamiques, même certains tags standard usuels, sont
refusés tant que leur sémantique n'est pas implémentée et testée. Ce sous-ensemble
ne prétend donc pas accepter arbitrairement tous les PRX réels ou les ELF Linux.

## Transactions et contrat mémoire/FEX

`OrbisLinker::link(inputs, hle, output, diagnostic, budget)` conserve intégralement
`output` sur échec. L'image provisoire est détruite. Les fichiers source sont
immuables ; aucun pointeur mutable n'est transmis au backend. La construction
vérifie le budget complet des segments avant toute allocation de staging.
Les adresses de tous les segments sont vérifiées contre les dépassements et les
chevauchements intermodules. Les cibles de relocation doivent contenir huit
bytes dans un segment RW et non X. Les écritures qui se chevauchent sont refusées.
Toutes les relocations sont validées avant la première écriture dans l'image
provisoire ; les segments SCE RELRO deviennent read-only après ces écritures.

Le budget est plafonné à 64 Mio pour les segments invités, avec 32 Mio/module.
Il ne mesure pas toute la consommation du processus : l'image précédente reste
vivante pendant la transaction et les index/métadonnées nécessitent de la mémoire
supplémentaire, bornée séparément. Au plus 65 536 symboles et 65 536 relocations
pour le batch, 4 096 entrées dynamiques/module, 1 Mio de chaînes/module et chaînes
identitaires limitées à 255 caractères. Un échec d'allocation est géré.

`Image::mappings()` fournit adresse invitée, taille, permissions ELF PF_R/W/X et
pointeur **const** vers les données appartenant à l'image. Le pont C expose les
mêmes informations sans adresse hôte, puis `maxps4_orbis_read` copie des octets.
Le backend doit créer sa propre cartographie à ces adresses logiques et appliquer
les permissions. Les bytes hôte sont des vecteurs ordinaires, jamais PROT_EXEC.
Les points d'entrée non nuls doivent désigner du code file-backed ; un PRX peut
ne pas avoir d'entrée. Les consultations échouées ne modifient pas les sorties.

Le registre HLE C++ est explicite et vide par défaut. Une entrée contient
l'identité exacte et une adresse de trampoline **invité** déjà cartographié RX.
Un pointeur de fonction hôte n'est pas un binding HLE. Les collisions HLE/export,
versions incompatibles et adresses de données sont refusées. Aucun trampoline
Orbis réel ni appel hôte n'est fourni ici. La fixture HLE teste ce contrat, pas
une implémentation de libkernel.

Le pont natif ne permet aucune exécution. Il reste à implémenter et tester un
backend d'espace d'adresses FEX pour ces cartographies, avec trampolines HLE et
ABI Orbis, TLS, pile, initialisation des modules et syscalls. Passer simplement
un pointeur de vecteur au RIP FEX serait incorrect et n'est pas proposé.

## Validation automatique

`OrbisLinkerFixture.h` fabrique deux ELF Orbis en mémoire ; ses identités
`FixtureLibrary`, `FixtureModule`, `AAAAAAAAAAA` sont des données de test sans
correspondance revendiquée avec un symbole Sony. `OrbisLinkerTest` peut écrire
ces deux fixtures dans un répertoire fourni en argument pour le test Swift.

Le workflow existant `pkg-inventory.yml` exécute sur push/PR les tests linker avec
ASan/UBSan sur Linux et macOS, les régressions PKG/SFO existantes, le véritable
appel Swift/C du linker et la compilation/typecheck iOS ARM64. Le workflow IPA
exécute aussi le test linker avant packaging et compile son pont dans l'app.
Les workflows complets de liaison FEX et smoke/heavy restent actifs.

Les tests couvrent import présent/absent/version incompatible, ambiguïtés,
cycles, dépendances absentes/non déclarées, ordre déterministe, les quatre
formules RELA et PLT RELA, erreurs d'indices/limites/types/addends, chevauchements,
budgets, permissions W+X/RX/R-only/RELRO, métadonnées tronquées/malformées,
transaction échouée après une première relocation valide, immutabilité de la
source, registre HLE et pont natif. Les fixtures PKG/SFO restent inchangées.

## Vers CUSA06015

Kero Blaster n'est pas exécutable dans MaxPS4 à ce stade. Son PFS reste opaque.
La prochaine étape technique est une cartographie FEX des deux modules
synthétiques liés, puis une exécution bornée traversant un trampoline invité
validé. Le chargement de Kero Blaster nécessitera ultérieurement une source
légalement accessible de modules plaintext et des imports/ABI réellement vérifiés.
Aucune extraction ou décryption du PFS protégé n'est une étape de ce travail.
