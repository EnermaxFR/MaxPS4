# Audit AetherPS4 PKG/PFS — issue #188

## Décision

Ne pas porter l'extracteur AetherPS4. Son parcours public dépend de la
cryptographie et écrit l'arbre du jeu ; il ne fournit pas de parcours autonome
PFS non chiffré en lecture seule. Les notices de plusieurs fichiers et la
compatibilité de sa liaison LibreSSL ne sont pas suffisamment établies pour
copier l'ensemble. Le format `pfs.h` possède une licence explicite compatible
au niveau du fichier, mais sa copie n'apporterait pas de lecteur à MaxPS4.

Cette PR ajoute seulement un diagnostic indépendant de l'en-tête PFS et refuse
les sources symlink/FIFO. Aucun code AetherPS4, clé, licence de jeu ou payload
commercial n'est copié. Le lecteur D32 de #187 reste une proposition séparée,
non modifiée, non fusionnée ; cette PR part de main après #185.

## Révisions et sources

Audit statique au 13 octobre 2026, fixé à :

- [AetherPS4 `3347fc13f19860855ce91b66356940989ea6b6a6`](https://github.com/Leviidev/AetherPS4/tree/3347fc13f19860855ce91b66356940989ea6b6a6).
  Dernier changement de `tools/pkg-extract` : `7f9cb7934020b89f8cdfd33aa8110c8397e294b3`.
- [MaxPS4 main / #185 `4ceedf4ecbed2fdd17fcff68689b53a37121765d`](https://github.com/EnermaxFR/MaxPS4/tree/4ceedf4ecbed2fdd17fcff68689b53a37121765d).
- [#187 `c063141c47c391f3f1457b36111b90553424e842`](https://github.com/EnermaxFR/MaxPS4/tree/c063141c47c391f3f1457b36111b90553424e842),
  consultée via ref locale de lecture, aucun push vers sa branche.
- Référence de champs, indépendante d'AetherPS4 :
  [LibOrbisPkg `643477263b2644e0803e0f58b8726ea4e3f3b7d4`, `LibOrbisPkg/PFS/PfsStructs.cs`](https://github.com/maxton/LibOrbisPkg/blob/643477263b2644e0803e0f58b8726ea4e3f3b7d4/LibOrbisPkg/PFS/PfsStructs.cs).
  Version 1, magic **20130315**, mode 16 bits LE à +0x1c, bit chiffré 4.
  Ce sont des indications de format publiées par une implémentation, pas une
  spécification Sony ni une preuve d'authenticité. Aucun code C# n'est repris.

Les chemins AetherPS4 ci-dessous sont relatifs à cette révision, avec numéros
de lignes à vérifier sur son [extracteur](https://github.com/Leviidev/AetherPS4/blob/3347fc13f19860855ce91b66356940989ea6b6a6/tools/pkg-extract/pkg_extractor.cpp).
Le [manifeste de sources](aether-pkg-pfs-source-manifest.json) consigne les
SHA-256 et notices SPDX des fichiers consultés, sans redistribuer leur code.

## Code réel, dépendances et preuves amont

`tools/pkg-extract` **existe** : `main_cli.cpp` lance le C ABI
`bachata_pkg_probe` / `bachata_pkg_extract` de `pkg_extractor.cpp` ; `build.sh`
compile C11/C++20 et lie `aes.c`, `pkg_crypto.cpp`, `pkg_type.cpp`,
`pkg_rsa_native.cpp`, une archive LibreSSL du build macOS et `-lz`.
Le binaire suivi est Mach-O ARM64 macOS, pas un composant iOS ou Linux.
`src/core/pkg_extract/pkg_extractor.cpp` est identique octet par octet au CLI
(SHA-256 `cb0403988dc9b71f1f7e9ee3105c3e49fab6df99d5acc1000def56fcae2aa906`).
La variante Android ajoute JNI et libdeflate (voir son `CMakeLists.txt`).
L'absence de référence à `pkg_extract` dans le CMake principal n'établit pas
que le simple placement sous `src/core` le lie au runtime.

`launcher/AetherPS4/.../PkgExtractorLocator.swift` localise un exécutable ;
`Models/EmulatorProcess.swift:80–168` gère cache, `Process`, progression et
lancement. C'est une enveloppe macOS, pas un parseur ni un modèle utilisable
avec `Process` dans une application iOS. La présence d'un `eboot.bin` en cache
est utilisée pour relancer, sans prouver l'admission ELF du fichier.

`launcher/AetherPS4/Tests/AetherPS4Tests/PkgExtractCLITests.swift` contient deux
tests : localiser le binaire et rejeter `placeholder` en `probe`. Les tests
peuvent être sautés si le binaire manque. Le commentaire mentionne une
vérification manuelle avec un jeu, sans fixture reproductible jointe ; ce n'est
pas une preuve de déchiffrement, de robustesse ou de Kero Blaster. Le test
`waitUntilExit()` ne met pas lui-même de timeout malgré son nom. Aucun test
PKG/PFS natif correspondant n'est enregistré dans `tests/CMakeLists.txt`.
Nous n'exécutons ni le binaire amont ni sa cryptographie.

## Lecture et sécurité : constats statiques

| Parcours AetherPS4 | Ce qui existe | Limite constatée, pas une exploitation démontrée |
| --- | --- | --- |
| Header / CNT (`:40–110`, `:784–802`, `:842–860`) | Struct BE, signature 0x7f434e54, `pread_all`, content ID et tailles | Entrées publiques vérifient lecture/magic, sans comparaison `fstat` de taille réelle, cap global des descripteurs ou contrôle systématique des plages |
| Descripteurs (`:204–305`, `:749–778`) | Entrées de 32 octets, table de types | Pas de validation uniforme des overlaps/flags ; `extract_sce_sys_entries` alloue `entry.size` puis écrit les types connus, sans budget partagé ni vérification de succès des écritures |
| PFS/PFSC (`:356–470`) | Cache initial, recherche PFSC, map de secteurs, géométrie, limite 2 000 000 blocs | Ce chemin lit un seed et appelle la crypto avant PFSC ; pas de lecture seule plaintext séparée. Cache `u32 * 2` issu du fichier, deux buffers de cette taille puis copie PFSC, sans budget iPhone global |
| Inodes/dirents (`:487–638`) | Cap 500 000 inodes, checks locaux dirents, streaming après cache | Stride 0xa8, heuristiques `flat_path_table`, dot/dotdot et fin de run ; pas de validation exhaustive des cycles, alias, types et tailles d'arbre |
| Fichiers (`:641–746`) | Lookup inode, map, streaming, écriture bloc par bloc | Conversion `Size` s64 en `int`, index inférieur de map non contrôlé après conversion, différences de secteurs sans validation globale de monotonie ; `memcpy` payload n'a pas le check fenêtre de `load_pfsc_sector`. Tailles hostiles peuvent rendre ces opérations dangereuses |
| Chemins (`:168–181`, `:658–668`) | `weakly_canonical` / `safe_under` | Contrôle suivi d'un `ofstream` : pas d'ouverture descriptor-relative avec refus de symlink ; course entre vérification et écriture, troncature de destinations existantes possible |
| Erreurs / concurrence (`:103–110`, `:926–1002`) | Status 0/1/2/3, annulation atomique, ≤8 workers, erreurs crypto locales | `pread_all` ne réessaie pas EINTR ; exceptions allocation/filesystem non contenues par le C ABI ou les threads ; fichiers partiels laissés sur erreur, état d'annulation global partagé |

Le backend crypto est seulement identifié pour délimiter la dépendance : aucun
contenu de `keys.h` n'est recherché, publié ou exécuté. Les offsets de seed et
les fallback crypto ne deviennent pas une capacité de MaxPS4. Aucun benchmark
sur iPhone n'a été effectué. Les chiffres de débit des commentaires Android
ne prouvent pas les performances iPhone ; le commentaire « 8 MiB+ par thread »
ne remplace pas un calcul des buffers réels et des tailles contrôlées par PKG.

## Licences par fichier et dépendance

MaxPS4 : `LICENSE` est le texte GPL v2 et README annonce GPL-2.0, sans option
générale explicite « or later » ni exception LibreSSL constatée. Cela ne constitue
pas un audit complet des autres dépendances FEX/renderer. Les nouveaux fichiers
indépendants de cette PR portent `GPL-2.0-only` et restent sous la licence du dépôt.

| Fichiers / dépendance auditée | Notice vérifiée | Compatibilité / décision |
| --- | --- | --- |
| Aether `tools/pkg-extract/pfs.h`, `pkg_type.h`, `pkg_type.cpp` ; mêmes fichiers sous `src/core/pkg_extract` | SPDX Copyright 2024 shadPS4 Emulator Project, GPL-2.0-or-later | Réutilisation de ces fichiers possible sous GPL v2, notices conservées ; aucun copié, car structures/types seuls n'apportent pas le parcours attendu |
| `pkg_extractor.cpp`, `pkg_extractor.h`, `pkg_crypto.cpp`, `pkg_crypto.h` | Commentaires de provenance Bachata/shadPS4, pas de SPDX ou notice individuelle | GPL v2 à la racine et README ne lèvent pas toutes les questions de provenance ; `REUSE.toml` n'annotera pas ces chemins. Permission individuelle à clarifier avant copie |
| `pkg_rsa_native.cpp`, `pkg_rsa_native.h`, `types.h`, `aes_neon.h`, `main_cli.cpp`, `build.sh` | Pas de licence individuelle explicite constatée | Même réserve ; le wrapper CLI et le backend natif ne sont pas réputés libérés par simple proximité |
| `aes.c`, `aes.h` | Commentaire d'implémentation/vecteurs ; référence tiny-AES-c dans `aes_neon.h`, pas de licence embarquée constatée | Ne pas attribuer automatiquement la licence d'une autre version upstream ; provenance et notices exactes à rétablir avant redistribution |
| `keys.h` | Dépendance identifiée par `#include`, contenu exclu de cet audit | Aucune reprise, aucune recherche de clés ; ni licence ni compatibilité affirmée |
| CLI binaire suivi ; Swift locator/process/tests | Pas de permission par fichier établie par cet audit | Aucun binaire, wrapper ou test amont copié |
| [LibreSSL b0504086dbbc186724b0cc92e6ba1832c245de0b, COPYING](https://github.com/shadexternals/libressl/blob/b0504086dbbc186724b0cc92e6ba1832c245de0b/COPYING) | ISC/public domain pour ajouts ; reste conserve OpenSSL/SSLeay historiques, acknowledgments et clauses additionnelles | Le CLI lie statiquement `libcrypto.a`. Compatibilité GPL v2 de l'ensemble non établie : les notices historiques peuvent imposer des conditions incompatibles sans exception. Ne pas certifier le lien au niveau de la bibliothèque sans audit des objets et exception/permission applicable |
| `-lz` du CLI macOS | Bibliothèque système choisie à la liaison, version non fixée par `build.sh` | Licence zlib usuelle permissive, mais fichier/version exacts du SDK non vérifiés ici ; aucune dépendance ajoutée à MaxPS4 |
| [zlib-ng fd0d263cedab1a136f40d65199987e3eaeecfcbd, LICENSE.md](https://github.com/zlib-ng/zlib-ng/blob/fd0d263cedab1a136f40d65199987e3eaeecfcbd/LICENSE.md) | Licence zlib, Gailly/Adler ; conserver notice, origine et modifications | Dépendance CMake du core, distincte du `-lz` CLI ; compatible au niveau de cette notice, aucun code repris |
| [libdeflate c8c56a20f8f621e6a966b716b31f1dedab6a41e3, COPYING](https://github.com/ebiggers/libdeflate/blob/c8c56a20f8f621e6a966b716b31f1dedab6a41e3/COPYING) | MIT, Eric Biggers / Google ; `LICENSES/libdeflate-NOTICE.txt` présent | Android seulement via macro ; conserver notice pour sources/binaire, compatible avec GPL v2, aucun lien ajouté |

Si un port ultérieur est envisagé : établir la provenance et permission de
chaque fichier non annoté, choisir GPL v2 pour les fichiers « or later »,
conserver copyrights/licences/disclaimers, marquer fichiers modifiés et dates,
fournir sources correspondantes complètes et scripts de build avec le binaire
(GPL §§1–3), et satisfaire notices des dépendances. Crédits nécessaires :
shadPS4 Emulator Project, auteurs Bachata/Aether identifiés, auteurs exacts des
dépendances reprises. Un simple lien GitHub ou un fichier de crédits ne règle
pas une incompatibilité de licence. Ce rapport crédite les sources consultées,
sans prétendre autoriser la copie de fichiers dont la licence reste incertaine.

## Comparaison avec MaxPS4

| Capacité | AetherPS4 fixé ci-dessus | MaxPS4 main / #185 | #187 (lecture seulement) | Cette PR |
| --- | --- | --- | --- | --- |
| CNT/descripteurs | Lecture + extraction générale, contrôles incomplets | 4096 entrées, plages/overlaps/doublons, whitelist métadonnées, total payload 1 Mio, O(n²) borné | Même base + vérification tranche PFS avant lecture | Même base ; refuse plage PFS ambiguë avant header |
| PFS | Chemin crypto/PFSC, inodes/dirents heuristiques | Opaque, non monté | Header puis sous-ensemble D32 mode 8, non signé/non comprimé/contigu | Header seul, aucun montage/extraction |
| Fichier du jeu | Extraction sur disque via backend crypto, pas preuve de lancement | Pas de fichier PFS ; #185 analyse séparément un dossier autorisé | Lecture sélectionnée ≤64 Mio, arbre borné ; pas admission automatique | `eboot.bin` demeure inaccessible, même header plaintext |
| Protection déclarée | Backend de déchiffrement hors mission | Clés/licences et entrées protégées exclues | Arrêt après header reconnu portant bit 4 | Arrêt après header de 72 octets maximum, unknown distinct de plaintext |
| Mémoire | Cache piloté par PKG, jusqu'à ~16 Mio de map, inodes jusqu'à 500k, workers ; pas budget global | Lectures metadata partagées 1 Mio, pas allocation PKG complet | Tables/répertoires cumulés 8 Mio, 4096 inodes/dirents, chemins 1 Mio, profondeur 16 ; copie fichier et buffer appelant peuvent coexister | Buffer stack fixe 72 octets ; une lecture supplémentaire, O(n) sur descripteurs déjà inventoriés, pas allocation payload |
| Host I/O | Écritures de l'arbre, checks canoniques sans atomicité | Source via `fopen` (FIFO peut bloquer avant `fstat`) | `O_NOFOLLOW/O_NONBLOCK`, lecture sélectionnée sans joindre chemins hôte | Source `O_NOFOLLOW/O_NONBLOCK/O_CLOEXEC`, pas destination ni chemin PFS construit |

Les budgets ne sont pas des pics RSS mesurés. Cette PR n'augmente pas les
capacités de lecture D32 proposées par #187. Si les deux propositions sont un
jour intégrées, résoudre explicitement le chevauchement du rapport/bridge et
garder un seul diagnostic ; ne pas fusionner #187 pour satisfaire #188.

## Implémentation indépendante et validation

`PS4PFSHeaderDiagnostic.hpp` reçoit un inventaire validé et un callback de
lecture exacte. Validation par soustraction des plages ; tranche PFS hors
header/table/payloads CNT ; si ambiguïté, aucune lecture. Un header reconnu
permet seulement de rapporter le mode déclaré, jamais son authenticité. Pour
un header inconnu le chiffrement est **indéterminé**. Le lecteur ne lit ni seed,
inode, répertoire, fichier, clé ou licence et ne possède aucune API d'extraction.
Il n'interprète aucun chemin PFS. Le bridge conserve les diagnostics NON PRÊT.

`PS4PFSHeaderDiagnosticTest.cpp` génère trois CNT originaux de 1792 octets avec
header PFS seulement (encrypted/plain/unknown), **pas des filesystems montables**.
Le callback assert exactement une lecture de 72 octets pour les headers valides
et zéro pour les overlaps/plages/troncages refusés. Tests du magic erroné
20130305, version inconnue, flags sans chiffrement, absence, UINT64_MAX, I/O,
report C/troncature, symlink/FIFO. Les fixtures contiennent un payload garde
synthétique jamais lu ; aucun test n'est une preuve d'extraction ou de démarrage.

Commandes Linux reproductibles depuis la racine (ASan/UBSan) :

```sh
mkdir -p build/aether-audit
c++ -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer MaxPS4-iOS/Native/PS4PKGBridge.cpp MaxPS4-iOS/Tests/PS4PFSHeaderDiagnosticTest.cpp -o build/aether-audit/pfs-header-test
python3 scripts/run-bounded.py 30 build/aether-audit/pfs-header-tests.txt build/aether-audit/pfs-header-test build/aether-audit/fixtures
bash scripts/test-orbis-runtime.sh build/aether-audit/orbis-runtime
bash scripts/test-orbis-analysis.sh build/aether-audit/orbis-analysis
bash scripts/test-ps4-system-services.sh
```

CI `pkg-inventory.yml` teste le nouveau diagnostic Linux et Darwin sous
sanitizers ; `PKGSwiftDiagnosticTest.swift` exerce les mêmes statuts via le vrai
ABI. La compilation native iPhone et type-check Swift restent en place.
`fexcore-app.yml` lance le nouveau test avant packaging IPA. La nouvelle fixture
`PS4*` déclenche la CI services/FEX déjà existante (ARM64 natif et VIXL), dont
preuves scène #183 pixels/input/action/mutation/restart ; workflows non désactivés.
La définition des jobs n'est pas un résultat : liens, statuts et artefacts exacts
sont consignés dans la PR. Swift/Xcode/iPhone absents localement ; aucun test
physique iPhone ni démarrage réel n'est déclaré. Linux local utilise
`ASAN_OPTIONS=detect_leaks=0` car LeakSanitizer nécessite ptrace ; ASan/UBSan restent actifs.

## Recette iPhone et premier blocage

1. Prendre l'IPA `MaxPS4-FEXCore-SideStore` du workflow **du commit de cette PR**
   quand il réussit ; signer/installer avec SideStore. Un build vert ne valide
   pas l'appareil physique.
2. Importer le PKG légalement détenu puis choisir **Analyser le PKG** depuis
   sa fiche. Chercher `Diagnostic en-tête PFS` dans le journal/diagnostic,
   utiliser **Copier le rapport complet** et partager le texte avec modèle
   iPhone, iOS et commit, sans joindre le PKG.
3. Fixtures artefact `MaxPS4-synthetic-PKG-fixtures` : `pfs-header-encrypted.pkg`
   doit afficher inaccessible ; `pfs-header-plain.pkg` non pris en charge ;
   `pfs-header-unknown.pkg` chiffrement indéterminé. Toutes restent NON PRÊT,
   aucune exportation de fichier du jeu n'est ajoutée.
4. Vérifier le diagnostic FEX →42 et la scène synthétique #183 sur appareil,
   puis partager ses résultats. Ces scènes ne sont pas Kero Blaster.

**Premier blocage Kero Blaster (`CUSA06015`) : `eboot.bin` inaccessible.** Aucun
PKG utilisateur n'est disponible dans cet audit ; « opaque » dans l'ancien
rapport ne prouve pas à lui seul le bit de chiffrement. Si le nouveau rapport
déclare chiffré, le lecteur s'arrête ; il faut des fichiers obtenus par moyens
autorisés. Si non chiffré, ce diagnostic ne monte toujours pas la variante ;
#187 ne propose que D32 simple et ne résout pas signed/nested/PFSC. Puis restent
ELF/SELF, PRX/NID, relocations, TLS, services/ABI, admission CPU, JIT et rendu.
Aucun écran titre, lancement ou jouabilité de Kero Blaster n'est promis.
