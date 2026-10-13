# PKG/PFS en lecture seule — issue #186

Base : main `4ceedf4` ; la PR #185 est déjà fusionnée. Cette mission ajoute une
PR brouillon séparée sans fusion. Aucun jeu, clé, licence ou PKG commercial
n'est fourni. Les fixtures originales générées par les tests sont librement
redistribuables sous la licence du dépôt et contiennent du texte/ELF synthétique.

## Structure et sources vérifiables

Référence de format : [LibOrbisPkg, commit 643477263b](https://github.com/maxton/LibOrbisPkg/tree/643477263b2644e0803e0f58b8726ea4e3f3b7d4),
`PKG/PkgReader.cs`, `PKG/Entry.cs`, `PKG/Enums.cs`, `PFS/PfsStructs.cs`,
`PFS/PfsReader.cs`, `PFS/PFSBuilder.cs`. Implémentation indépendante des champs,
sans copier son code ni sa cryptographie. Il s'agit d'une documentation publique
par implémentation, pas d'une spécification Sony ni d'une vérification d'authenticité.
Le template PS4PFS.bt indique par erreur 20130305 : le lecteur et le constructeur
PfsStructs utilisent **20130315** ; seul ce dernier magic est accepté.

| Structure | Champs lus | Validation / limites |
| --- | --- | --- |
| CNT, big endian | signature 0x7f434e54 ; flags +0x04 ; count +0x10 ; secure/secondary +0x14/+0x16 ; table +0x18 ; corps +0x20/+0x28 ; Content ID +0x40 ; DRM/type/flags +0x70/+0x74/+0x78 ; pfs_flags +0x408 ; PFS +0x410/+0x418 ; taille +0x430 | En-tête 0x5a0 ; taille réelle = déclarée ; 1–4096 entrées ; compteurs cohérents ; plages par soustraction pour éviter overflow |
| Descripteur CNT, 32 octets | id, name offset, flags1/2, offset/size 32 bits, reserved 64 bits | Plages hors en-tête/table ; doublons et chevauchements invalides ; métadonnées SFO/images/noms seules admissibles avec flags nuls ; clés/licences refusées ; une table CNT n'est jamais un FS |
| SFO, little endian | signature NUL PSF ; version 0x101 ; keys/data/count ; records 16 octets | 1 Mio, 1024 records, clés terminées ≤64, length ≤capacity, pas de doublons/chevauchements ; UTF-8 strict sans contrôles ≤4096 ; entier 0x404 =4 octets, texte 0x204, binaire 0x4 |
| PFS, little endian | version 64 bits +0 ; magic 64 bits +8 ; mode 16 bits +0x1c ; bloc 32 bits +0x20 ; backups +0x24 ; nblock +0x28 ; inode count +0x30 ; ndblock +0x38 ; inode blocks +0x40 | Seul l'en-tête 0x48 est lu pour identifier le bit de chiffrement 0x4. Aucun seed ni payload protégé lu. Version=1 et magic=20130315 requis pour interpréter le bit |
| PFS D32 inode, 0xa8 octets | mode +0 ; flags +4 ; size +8 ; size_compressed +0x10 ; blocks +0x60 ; db[12] +0x64 ; ib[5] +0x94 | 4096 inodes ; table par blocs avec padding ; mode dir 0x4000/file 0x8000 ; flags readonly/internal seuls ; size=compressed size ; étendues après table et dans image, sans chevauchement |
| Dirent, en-tête 16 octets | inode/type/namelen/entsize 32 bits ; nom ASCII terminé | Taille multiple de 8, ≥17+namelen, dans le même bloc ; nom ≤255 ; types 2/3 et dot/dotdot 4/5 vérifiés ; doublons, cycles, liens spéciaux, slash, backslash, colon, contrôles et traversal refusés |

`pfs_flags` CNT est conservé brut, sans déduire le chiffrement depuis les flags
CNT ou la présence d'un nom. La plage PFS recouvrant la table ou les payloads
CNT est refusée avant toute lecture. Les champs id/fmode/clean/readonly et les
timestamps/uid/gid inode sont ignorés comme métadonnées : ils n'autorisent rien.

## Statuts et sous-ensemble effectif

- **Absent** : aucune image PFS déclarée.
- **Chiffré déclaré/inaccessible** : en-tête reconnu et mode & 4 ; arrêt après
  72 octets. `eboot.bin` ne peut pas être extrait avec les capacités actuelles.
- **Inconnu** : version/magic non reconnus ; chiffrement indéterminé, jamais
  assimilé à non chiffré ou à absence de fichier.
- **Malformé** : bornes, compteurs, tables, dirents, cycles ou étendues invalides.
- **Non chiffré déclaré/non pris en charge** : signé, 64 bits, backups, flags
  inconnus, compression, blocs indirects/non contigus ou budgets dépassés.
- **Accessible** : mode exact 8, D32 non signé non comprimé contigu ; arbre
  vérifié depuis inode 0, avec répertoire `uroot`. Seuls ses fichiers sont
  exposés sous des chemins relatifs triés. Aucune admission ELF ou exécution.

La lecture des inodes et répertoires cumule au maximum 8 Mio ; chaque répertoire
≤1 Mio, profondeur ≤16, total ≤4096 dirents, chemins cumulés ≤1 Mio, bloc puissance de deux 4–64 Kio.
Le nombre de blocs doit correspondre exactement à la taille de la tranche.
Les pointeurs inutilisés D32 acceptent zéro ou -1 ; les pointeurs indirects
non nuls sont refusés. L'extraction sélectionnée est atomique vers un buffer
mémoire ≤64 Mio et relit le PKG/PFS. Aucun fichier source n'est modifié, aucun
chemin PFS n'est joint à un chemin hôte. Clés/licences connues (keystone,
license.dat/info, entry_keys/image_key, *.rif) restent non exportables.

L'API C `maxps4_pkg_pfs_read_file` offre une requête de taille puis une lecture
bornée. Swift alloue cette taille, lit en tâche de fond et ouvre le sélecteur
iOS **Fichiers** pour une destination explicitement choisie. Pas de dossiers
créés depuis les noms PFS. La source doit rester stable pendant l'opération ;
ne pas la modifier avec un autre outil. open O_NOFOLLOW/O_NONBLOCK refuse les
liens source et évite le blocage sur FIFO. Les métadonnées de la source sont
revérifiées après lecture ; ce n'est pas une capture immuable contre un écrivain
concurrent malveillant.

Les PFS signés (dont certains PFS externes avec pfs_image.dat), l'imbrication de
PFS, PFSC et les variantes 64 bits restent bloqués. Ils ne sont pas présentés
comme corrompus ou montés. Aucun accès au PKG utilisateur n'est disponible ici ;
on ne prétend donc pas avoir constaté son bit de chiffrement. Le rapport #184
prouvait seulement un PFS opaque pour l'ancien lecteur, pas son mode effectif.

## Recette iPhone / SideStore

1. Installer l'IPA **du commit de cette PR**, artefact
   `MaxPS4-FEXCore-SideStore` du workflow FEXCore app ; signer via SideStore.
   Lien et statut exacts sont fournis dans la PR après la CI. Un build vert
   ne constitue pas un test physique iPhone.
2. Importer le PKG. Ouvrir sa **fiche du jeu**, puis **Analyser PKG/PFS**.
3. Lire `Diagnostic PKG/PFS`. Pour Kero Blaster, si chiffré : arrêt explicite,
   aucune extraction d'eboot.bin, clé, licence ou tentative de bypass.
4. **Exporter le rapport texte**, partager le rapport avec modèle iPhone,
   version iOS et commit IPA. Ne pas joindre le PKG ni du contenu commercial.
5. Pour la fixture `pfs-plain.pkg` disponible en artefact CI : vérifier
   `PFS non chiffré accessible`, `eboot.bin` de 5 octets. **Exporter un fichier
   PFS accessible…**, sélectionner `eboot.bin`, enregistrer via Fichiers :
   les octets sont `hello`, pas un exécutable. `../eboot.bin`, `keystone`, les
   fixtures encrypted/unknown doivent être refusés.
6. Pour des fichiers légalement accessibles réels, choisir leur destination
   puis conserver l'arborescence dans un dossier (eboot.bin à la racine), et
   utiliser **Import → Dossier de jeu accessible** (#185). Aucun manifeste,
   graphe PRX ou biais n'est inventé ; le diagnostic ELF/SELF et ses blocages
   restent effectifs. Pas de lancement automatique après export.
7. Vérifier FEX →42 et la scène #183 (pixels/input/action/mutation/restart),
   puis exporter les résultats. Aucune fusion sans validation explicite après
   test iPhone ; aucun écran titre ou jouabilité de Kero Blaster annoncé.

## Vérification et prochains blocages

`PS4PFSReadOnlyTest` construit une vraie structure synthétique D32 (superroot,
uroot, inode de fichier et payload), lit et compare les octets, puis vérifie
le bridge CNT/C. Tests négatifs : tronquage, tailles/counts invalides, chiffré
avec compteur attestant une seule lecture de 72 octets, magic inconnu, signed/
64 bits, compression, indirect/non contigu, chemins malveillants, cycles,
chevauchements, erreur I/O et mutations déterministes. `GameDirectoryPreflightTest`
extrait un ELF synthétique via cette API et vérifie l'analyse NID #185.
Le test Swift vérifie aussi les octets exportés et les refus via le vrai ABI C.

CI Linux/Darwin ASan/UBSan, bridge Swift macOS et compilation iPhone ARM64,
packaging IPA après les tests, régressions FEXCore natives/VIXL et scène #183
conservées. Les résultats locaux/CI exacts figurent dans la PR ; ne pas appeler
la simple définition d'un workflow une preuve de réussite.

Vers Kero Blaster : d'abord son rapport PKG/PFS sur appareil ; si chiffré, des
fichiers accessibles par des moyens autorisés sont nécessaires. Sinon, vérifier
la variante réelle (signed/nested/PFSC encore bloqués), puis ELF/SELF, PRX/NID,
relocations, TLS, services/ABI, admission CPU, JIT iPhone et rendu réel. Cette PR
ne résout pas ces besoins et ne ferme pas la mission de jouabilité #184.
