# PR #205 — import Kero Blaster et diagnostic ENOEXEC

## Ce qui est établi

Point de départ : `b1bc26753d5c5bc854352f6652a7945a97f3f695`.
Le PKG commercial `UP0969-CUSA06015_00-KEROBLASTERUS000-A0100-V0102`
n'est pas disponible dans cet espace de travail. Aucun octet propriétaire,
clé, licence ou payload déchiffré n'est ajouté aux fixtures.

L'import `MaxPS4Emulator.importGame` vérifiait seulement 32 octets avant copie.
Il ne renvoyait pas directement `-8`. La note historique
[`orbis-m2-iphone-validation.md`](orbis-m2-iphone-validation.md) rapporte en
revanche **une exécution PKG à -8, backend absent, entrée 0**. Cela concorde avec
le chemin `executeTestELF` → `run_elf` / `interpret_elf` → `GuestELF::load` :
la signature CNT ne correspond pas à ELF et le chargeur renvoie `-ENOEXEC`
(`-8` sur iOS/Linux), avant publication d'entrée ou de backend. Les fichiers
PKG de moins de 32 Mio pouvaient atteindre ce chargeur sans précontrôle du format.

Le précontrôle reconnaît désormais CNT sur quatre octets, avant allocation du
fichier, création du moteur ou JIT, dans `executeTestELF` **et** `stagePS4`.
Le message `[ELF.CONTAINER]` dirige vers l'analyse PKG/PFS, quelle que soit
l'extension du fichier. Un test Swift utilise un faux `.elf` CNT sparse de
128 Mio pour vérifier que ce refus précède aussi les limites de taille ELF.
Le test natif reproduit indépendamment le `-ENOEXEC` de signature.

Cette note historique n'est pas le rapport complet de l'essai Kero Blaster :
elle localise un chemin reproductible, sans établir l'état réel du PKG commercial
ni son accessibilité PFS. Un PFS chiffré/opaque et un échec ELF restent distincts.

## Changements

- L'API existante d'inventaire conserve son contrat : retour 0 pour un rapport
  complet, même si le PKG est rejeté. La nouvelle API `maxps4_pkg_validate_import`
  renvoie `-ENOEXEC` pour une structure CNT rejetée, avec le même rapport.
- Identifiants stables `PKG.HEADER`, `MAGIC`, `SIZE`, `COUNTS`, `TABLE_RANGE`,
  `BODY_RANGE`, `PFS_RANGE`, `TABLE_READ`, `COMPLETE`, `SOURCE`, `MEMORY`.
  Les refus de structure affichent offset, valeur déclarée et valeur observée.
  Pour `COUNTS`, les valeurs sont entry_count et secondary_count ; le texte
  explique aussi une incohérence secure_count. Les données parsées restent vides
  après refus. `PKG.SOURCE` garde l'errno d'ouverture/lecture de la source.
- `PKG.NAMES`, `PKG.SFO`, `PKG.PFS`, `PKG.RUNTIME` séparent les étapes de
  métadonnées, d'accessibilité et d'admission. Les identifiants de contenu sont
  issus des en-têtes/SFO vérifiés, jamais du nom de fichier.
- Pour le seul PFS D32 déclaré non chiffré et entièrement validé, au maximum
  128 candidats eboot/ELF/SELF/PRX/SPRX sont inspectés, sur 64 octets chacun
  (8192 octets supplémentaires maximum, distincts des budgets CNT/PFS).
  `PKG.CANDIDATE` distingue ELF candidat, SELF candidat et inconnu/tronqué.
  Une signature candidate ne valide ni protection, ni structure, ni exécution.
  Un PFS opaque/chiffré produit zéro candidat et aucune lecture de payload.
- L'import copie dans un répertoire temporaire possédé, valide la copie,
  publie sans remplacer de fichier existant, puis enregistre la bibliothèque
  atomiquement. En cas d'échec, les copies créées sont retirées et la liste en
  mémoire est rétablie. Un échec de nettoyage est affiché avec `IMPORT.ROLLBACK`.
  La source et les destinations préexistantes sont préservées.
- L'étape `IMPORT.*` et le domaine/code Foundation distinguent validation,
  création du staging, copie, revalidation, publication et sauvegarde.
  Les PKG structurellement valides mais chiffrés/non pris en charge restent
  importables **pour inventaire seulement**.
- Les refus de staging ELF/SELF portent une étape `ELF.*`/`SELF.*` et un index.
  Les refus du linker portent `ORBIS.INPUT/ELF/SCE/PRX/NID/RELOCATION/PERMISSIONS`.
  Le précontrôle de budget refuse explicitement CNT/SELF avant d'interpréter
  leurs octets comme table ELF. L'état partiel n'est jamais publié.

## Prochain incrément ELF/SCE/PRX et libkernel

Le graphe explicite de fichiers autonomes non chiffrés reste requis. Préparer
les dépendances, vérifier les identités SCE/NID et les versions, résoudre les
imports puis valider toutes les relocations avant engagement. Les tests de
rollback couvrent notamment un second module invalide après préparation d'un
premier et une relocation tardive refusée. Aucun lien direct d'une adresse hôte
ARM64 dans un GOT invité x86-64 ; aucun import manquant remplacé par succès fictif.

Les services libkernel existants (fichiers app0 en lecture seule, mémoire,
horloges, identité de thread et mutex non bloquants) sont conservés. Sans ELF
accessible et imports observés, aucun service supplémentaire n'est inventé pour
ce titre. Un prochain service devra partir d'une identité réellement décodée,
avec tests ABI, pointeurs invités, erreurs et rollback avant activation.

Aucun changement du JIT M2, de ses barrières TSO, des atomiques, de W^X ou du
contrat mémoire. Aucun BRK ajouté. Cette modification ne démontre ni le lancement
de Kero Blaster, ni une correction générale de `-48`.

## Vérification et essai iPhone

Les fixtures positives/négatives couvrent structure CNT, copies tronquées,
flags chiffrés/non interprétés, PFS clair/chiffré/inconnu, signatures candidates,
formats ELF incompatibles, rollback de staging et collisions de destination.
Le test Swift réel injecte une erreur de sauvegarde après copie et vérifie
source, destination et nettoyage. Il est exécuté en CI macOS et avant l'IPA.
Les scripts V4/M1/TLS, services PS4 et linker restent les contrôles de régression.

1. Installer l'IPA issue du **SHA de cette correction**, après succès CI ;
   re-signer application et framework via SideStore.
2. Dans MaxPS4, Importer → sélectionner le PKG conservé dans Fichiers.
   Un import réussi indique « PKG PS4 importé (non exécutable) ».
3. En cas de refus, conserver le message entier, notamment `[IMPORT.*]`,
   `[PKG.*]`, domaine/code, offset/declared/observed. Ne pas renommer le PKG en ELF.
4. Bibliothèque → informations du PKG → « Analyser PKG/PFS » ; conserver le
   rapport complet, le SHA source, la taille, le Content ID/SFO et les étapes.
   Un résultat `accepted=1` décrit uniquement la structure du conteneur.
5. Si le PFS est opaque/chiffré, arrêter ce chemin. S'il est lisible, les formats
   candidats restent à valider séparément ; aucune exécution automatique.
6. Vérifier StikDebug et le détachement dans le même processus, puis JIT natif
   42 et M2 deux fois : backend 2, JIT/mémoire 42, TLS 8, scalaires 11/11,
   refus scalaires 55/55, limites 16/16, statuts 0 et mémoire préservée 1.
   ELF/libkernel doit conserver 2. Partager les rapports complets si un compteur
   diffère. Les contrôles CI ne remplacent pas cet essai physique.

PR #205 reste brouillon, main n'est ni modifiée ni fusionnée. Runners publics
standard uniquement, budget 0 €, aucun changement de facturation.

## Résultats locaux avant publication

Linux x86-64 : suites PKG inventory/metadata/PFS header/PFS read-only et fichiers
sparse >2/4 Go réussies ; linker Orbis **3370 contrôles** et staging ELF/SELF
réussis. V4/M1/TLS réussis en natif et ASan/UBSan ; services PS4, chargeurs invités,
admission CPU et ABI C réussis. `ASAN_OPTIONS=detect_leaks=0` est utilisé pour
l'environnement local sous ptrace ; ASan/UBSan restent actifs. Swift/iPhoneOS et
l'IPA nécessitent les runners macOS CI. Aucun test physique iPhone effectué ici.

Reproduction depuis la racine :

```sh
ASAN_OPTIONS=detect_leaks=0 bash scripts/test-orbis-runtime.sh build/kero-runtime
ASAN_OPTIONS=detect_leaks=0 bash scripts/test-orbis-v4-memory.sh build/kero-v4
ASAN_OPTIONS=detect_leaks=0 bash scripts/test-ps4-system-services.sh
python3 scripts/test-pkg-sparse-ci.py
```

Les commandes C++/Swift des tests PKG et de la transaction d'import se trouvent
également dans `.github/workflows/pkg-inventory.yml` et `fexcore-app.yml`.
