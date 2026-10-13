# Analyse PKG PS4 et test iPhone

Cette étape prépare l'analyse d'un paquet, pas l'exécution de Kero Blaster.
Aucun PKG commercial, jeu, firmware ou matériel cryptographique n'est fourni.

## Rapport et garanties

`PS4PKGInventory` reconnaît le conteneur PS4 `7f 43 4e 54` (CNT),
lit les champs big-endian documentés publiquement, vérifie les tailles réelles
et déclarées, compteurs, tables, corps et PFS. Le calcul des bornes utilise une
soustraction après contrôle de l'offset pour éviter les débordements.
La table est limitée à 4096 entrées ; les plages qui se chevauchent ou les
identifiants dupliqués sont invalides et ne peuvent fournir de données.

Le rapport indique Content ID et Title ID provenant de l'en-tête, type brut et
interprété, flags, DRM déclaré et tailles. Il ne vérifie pas l'authenticité du
paquet. Un identifiant absent/invalide reste indisponible, sans déduction depuis
le nom du fichier. Le nom de la carte de bibliothèque est une étiquette locale.

Seules les ressources autorisées (`entry_names`, `param.sfo`, `pic1.png`,
`icon0.png`) avec flags/réservés nuls, plages non ambiguës et hors PFS peuvent
être copiées en mémoire. Un budget partagé de 1 Mio borne toutes ces lectures,
y compris les tentatives échouées. Aucun fichier n'est exporté.

`PS4SFO` vérifie les tables little-endian, longueurs/capacités, types,
chevauchements, clés dupliquées, chaînes terminées et UTF-8. Le rapport affiche
TITLE, TITLE_ID, CONTENT_ID, APP_VER, VERSION et CATEGORY avec leur source.
Les contradictions entre SFO et en-tête sont signalées. Un SFO chiffré, malformé
ou dépassant le budget n'est pas présenté comme lisible.

Les noms de descripteurs sont uniquement affichés. Les références à eboot.bin,
PRX/SPRX et param.sfo ne prouvent ni l'existence de fichiers dans un système de
fichiers monté, ni l'accessibilité de leurs données. Le PFS reste opaque ; son
inventaire interne n'est pas disponible. Les clés/licences ne sont jamais lues.

## Contrat de préparation du runtime

`PS4PKGRuntimePlan::inventory` produit des références et leurs états déclarés.
Ce plan ne possède pas de fonction d'extraction d'exécutables depuis un PKG.

`prepareStandalone(logicalPath, bytes, size, dependencies, output, diagnostic)`
accepte uniquement un ELF Orbis autonome non chiffré, fourni explicitement par
un futur fournisseur de fichiers légalement accessibles. Il appelle le chargeur
existant `PS4ExecutableImage` pour préparer les segments en mémoire. Les PKG et
SELF sont refusés. Les chemins relatifs et jusqu'à 128 dépendances explicites
sont vérifiés, sans ouverture de chemins ni recherche automatique de modules.
Un échec efface toute préparation antérieure.

La liste des dépendances est fournie par l'appelant ; elle n'est pas inventée
depuis les noms PKG. Un futur lecteur d'imports Orbis devra la produire et un
résolveur devra gérer modules manquants, versions/NID, cycles et ordre de
chargement. Les relocations, TLS et imports restent des obligations du runtime.
Cette interface ne lie pas les modules et n'exécute aucun code. Elle ne propose
ni déchiffrement, ni contournement DRM, ni gestion de clés protégées.

## Validation et fixtures synthétiques

Le workflow `Generic PS4 PKG inventory` exécute les tests natifs avec
ASan/UBSan, teste le pont Swift sur macOS et compile pour iPhone ARM64.
`PS4PKGMetadataTest` génère quatre fixtures dans l'artefact
`MaxPS4-synthetic-PKG-fixtures`. Génération locale depuis la racine :

```sh
c++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  MaxPS4-iOS/Native/PS4PKGBridge.cpp \
  MaxPS4-iOS/Tests/PS4PKGMetadataTest.cpp -o /tmp/pkg-metadata-test
/tmp/pkg-metadata-test build/pkg-fixtures
```

Ces fixtures portent CUSA12345 et le titre « Diagnostic synthétique » ; elles
ne représentent aucun jeu. Les descripteurs nommés eboot.bin et libdemo.sprx
contiennent des octets factices non admissibles comme exécutables.
Les tests incluent toutes les troncatures SFO, offsets extrêmes, flags de
protection, UTF-8 invalide, doublons, chevauchements et mutations déterministes.
Les tests existants FEXCore (résultat 42) et les deux diagnostics ELF/libkernel sont
conservés et exécutés par le workflow de compilation FEXCore SideStore.

## Protocole iPhone / SideStore

1. Sur un iPhone iOS 18 ou plus récent, installer l'IPA de la PR via SideStore.
   SideStore doit signer l'application et le framework intégré. Le JIT n'est
   pas nécessaire à l'analyse PKG.
2. Copier les quatre fixtures dans Fichiers. Dans MaxPS4, importer
   `synthetic-metadata.pkg`, ouvrir sa fiche et toucher **Analyser le PKG**.
   Attendre le rapport, sélectionner/copier le texte pour le conserver.
3. Vérifier CNT/PS4, CUSA12345, le Content ID, APP_VER 01.23, VERSION 01.00,
   le titre synthétique et trois références dont eboot.bin/libdemo.sprx.
   Les deux références exécutables doivent rester « inconnu » et non admises.
4. Importer `synthetic-encrypted-sfo.pkg` : l'entrée SFO doit être déclarée
   chiffrée et APP_VER indisponible. Aucun octet SFO ne doit être inspecté.
5. Importer `synthetic-bad-offset.pkg` puis `synthetic-truncated.pkg` : le
   rapport doit préciser respectivement « Image PFS hors limites » et
   « En-tête PKG tronqué ». Les fichiers importés doivent rester conservés.
6. Fermer/rouvrir l'application et relancer **Analyser le PKG** sur la fixture
   valide : mêmes métadonnées et fichier toujours présent.
7. Importer sa propre copie légalement détenue de Kero Blaster. Recueillir le
   rapport complet, taille et empreinte SHA-256 via la fiche. Comparer les
   identifiants en-tête/SFO lorsqu'ils sont accessibles. Si SFO/noms/PFS ne sont
   pas accessibles, vérifier que la raison est explicitée ; aucune version ni
   présence d'exécutable ne doit être déduite du nom du fichier.
8. Vérifier les diagnostics existants FEXCore 42 et les deux diagnostics ELF/libkernel dans leurs
   conditions habituelles (JIT requis uniquement pour les diagnostics FEX JIT).
   L'analyse/import d'un PKG ne doit déclencher ni initialisation JIT ni jeu.

La réussite de la CI ne remplace pas cette validation sur un iPhone réel.
Le cas commercial Kero Blaster n'a pas de fixture redistribuée dans le dépôt.
