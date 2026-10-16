# Mission 12 — préparation de la validation iOS V4

Branche locale : `experiment/maxps4-ubuntu-codex`, HEAD `b031576`, avec les
modifications non commitées des missions 8 à 12. Aucun push, PR modifiée,
fusion ou workflow distant lancé.

**État : préparation de compilation IPA complétée localement, compilation
Xcode/Swift et production d'une nouvelle IPA encore non exécutées.** L'hôte
est Linux x86 ; Swift, xcrun, Xcode et le SDK iOS ne sont pas installés.
Les vérifications ARM64 partielles ci-dessous ne remplacent pas ce passage.

## Audit des douze fichiers de mission 11

| Fichier | Vérification |
| --- | --- |
| `Native/maxps4_fex_runtime.h` | Requête C de 40 octets sur ABI 64 bits ; offsets entrée 24 et ABI 32 ; pointeurs opaques, entiers de largeur fixe et size_t ; déclaration dans extern C |
| `Native/maxps4_fex_runtime.cpp` | Définition C non manglée, compilation C++23, verrou de runtime, bail pendant dispatch, invalidation avant unmap, thread détruit avant GDT/call-return stack ; ancien staging conservé |
| `Native/OrbisV4PRXLaunch.hpp` | Contrat synthétique borné : PRX autonome, aucune dépendance/import/TLS/init-fini, code admis jusqu'au HLT, lecture invitée contrôlée, pile privée RW/NX ; aucun élargissement ajouté |
| `Sources/MaxPS4FEXDiagnostic.swift` | MAXPS4_FEX_APP, options V4 décodées uniquement pour l'action distincte ; NSData conservés pendant l'appel C ; succès conditionné au statut et au backend FEX réel |
| `Sources/MaxPS4GameDirectoryView.swift` | Bouton V4 séparé et identifié, opérations en tâche de fond, accès security-scoped borné, commandes bloquées pendant le travail ; ancienne action conservée |
| `Tests/OrbisV4PRXFixture.h` | ELF PRX fabriqué en mémoire ; code et donnée de résultat réellement chargés, aucun contenu commercial |
| `Tests/OrbisV4PRXLaunchTest.cpp` | Droits RX/RW-NX et adresses distinctes, refus de budget/code/ABI/entrée/pointeur/W+X/TLS et DT_INIT autrement préparable |
| `Tests/PS4FEXLinkedExecutionTest.cpp` | Nouvelle API réelle, valeurs 0/42/99/7, sorties nulles après refus, reprises, appelants concurrents sérialisés, ancienne voie encore fonctionnelle ; export de la fixture de dossier |
| `Tests/FEXSwiftDiagnosticTest.swift` | Refus des manifestes V4 incomplets avant JIT ; exécution attendue de v4-synthetic via le vrai pont ; code audité, non exécuté sous Swift localement |
| `Tests/PS4SystemServicesABITest.c` | Assertions de taille/offset et signature de l'API ; compilation C native et objet Mach-O ARM64 réussis |
| `scripts/test-orbis-analysis.sh` | Nouveau test d'admission compilé avec ASan/UBSan non récupérable et exécuté avec délai borné |
| `docs/mission11-v4-prx-launcher.md` | Description du sous-ensemble synthétique cohérente ; les preuves VIXL ne sont pas présentées comme des tests iPhone |

Les chemins Native/Sources/Tests de ce tableau sont relatifs à `MaxPS4-iOS`.

## Exposition et configuration iOS

`MaxPS4-Bridging-Header.h` inclut `maxps4_fex_runtime.h`. Le projet Xcode prend
tout `Sources`, active `MAXPS4_FEX_APP`, cible ARM64/iOS 18 et lie le framework
`MaxPS4FEXRuntime`. Les ponts C++ de l'application restent en C++17 ; le runtime
FEX séparé est compilé par CMake en C++23. Aucun type C++ ne traverse l'API V4.

La bibliothèque embarque un seul ensemble cohérent FEXCore/FEXCore_Base.
La configuration conserve le lien strict `-undefined,error`, les dépendances
épinglées, les patches existants et le fournisseur JIT protégé. Les changements
de cette mission ne modifient ni l'admission CPU ni le contrat de pile/TLS.

## Problème reproduit et correction

Le framework copié par `build-fexcore-app.sh` fournissait son header public
`MaxPS4FEXRuntime.h` et `maxps4_jit_memory.h`, mais omettait
`OrbisLinkerBridge.h`, inclus transitivement par le header public. Le bridging
header du projet accède directement aux sources Native et pouvait masquer
cette lacune du module framework.

Reproduction locale : un module framework minimal importé avec Clang ciblant
`arm64-apple-ios18.0`, en mode freestanding, échoue sur ce header manquant.
Avec le header ajouté, le même import et l'assertion de taille V4 passent.
Journaux : `/tmp/maxps4-mission12-headers/missing-header.log` et
`complete-header.log`. Aucun SDK iOS ni lien d'exécutable n'est utilisé par
cette reproduction.

Le packaging copie désormais ce header. Un test Swift importe le **module
framework public** et lie son symbole V4 avant la construction Xcode, pour
éviter que le bridging header du projet masque à nouveau ce défaut.

## Contrôles ajoutés pour GitHub Actions

- `V4PRXLinkSmoke.c` : véritable unité C11 liée au runtime C++ ; taille/offsets
  et appel du symbole V4 avec moteur nul, sans initialiser le JIT.
- `V4PRXLinkSmoke.swift` : création de la requête importée, taille/offsets et
  appel de l'API ; compilation et liaison contre la dylib, puis contre le module
  framework dans le packaging. Le test de moteur nul exige -EINVAL et sorties
  nulles ; il ne prétend pas exécuter un PRX.
- Les deux configurations CMake ajoutent la cible C. Le script Darwin vérifie
  ARM64 et l'export exact `_maxps4_fex_engine_run_v4_prx`, puis compile/lie Swift
  pour ARM64/iOS 18 ou ARM64/macOS 15 selon le SDK réel.
- Les exécutables de test iOS sont seulement construits ; les tests C/Swift
  s'exécutent uniquement dans la configuration hôte macOS.
- Le build IPA et `verify-fexcore-ipa.py` exigent l'import V4 dans l'application,
  son export dans le runtime de l'IPA, et le marqueur du bouton V4 compilé.
  Le vérificateur inspecte les Mach-O réellement extraits du ZIP final.
- Le workflow IPA conserve le checkout du SHA de tête exact pour une PR. Une
  clé `with` dupliquée a été retirée lors de la validation de publication. Le
  workflow de liaison surveille les nouvelles fixtures/tests et conserve les
  exécutables de liaison avec les journaux comme artefacts de diagnostic.

Les signatures, entitlements, W^X/NX, profils mémoire et limites du lanceur
restent inchangés. Aucune IPA n'est publiée automatiquement par ce travail local.

## Résultats de cette mission

| Test exécuté localement | Résultat |
| --- | --- |
| CMake, nouvelles cibles C ABI + tests FEX | PASS |
| C11 lié au vrai runtime C++ local | PASS : symbole non manglé, -EINVAL et sorties nulles |
| `nm` de l'archive runtime | Définition globale de `maxps4_fex_engine_run_v4_prx` présente |
| ABI C, `clang -target arm64-apple-ios18.0 -ffreestanding` | PASS : objet Mach-O ARM64, sans SDK ni lien iOS |
| Import Clang du module framework ARM64 freestanding | Défaut reproduit, puis PASS avec headers complets |
| `scripts/test-orbis-analysis.sh /tmp/maxps4-mission12-analysis` | PASS : bridge, transfert, admission V4, métadonnées, dossiers sous ASan/UBSan |
| `scripts/test-orbis-v4-memory.sh /tmp/maxps4-mission12-memory` | PASS : six suites et M1, natif et ASan/UBSan |
| `scripts/test-orbis-runtime.sh /tmp/maxps4-mission12-runtime` | PASS : 3370 checks linker et staging |
| `scripts/test-ps4-system-services.sh` | PASS : suites natives et ABI C ; journal `/tmp/maxps4-mission12-services.txt` |
| API V4 et ancien lanceur via tests FEX liés | PASS : valeurs et reprises, appels concurrents sérialisés, ancienne voie conservée |
| FEX M1/M2 et traduction | PASS : M2 11/11, refus 55/55, limites 16/16 ; traduction sur quatre combinaisons TSO/LSE |
| Audit de signature iOS Python existant | PASS : 3 tests |
| Bash, YAML des workflows, syntaxe Python du vérificateur, `git diff --check` | PASS |

Les tests sous sanitizers tournent hors traçage sandbox pour garder la
détection des fuites active. Le JIT complet utilise les options upstream ;
aucune couverture ASan/UBSan du JIT complet n'est revendiquée.
Les tests FEX utilisent VIXL sur x86 (backend 3), pas un iPhone ou une exécution
ARM64 native. Journaux : `/tmp/maxps4-mission12-linked.txt`, `-m1.txt`,
`-m2.txt`, `-translation.txt`. Leurs succès ne constituent pas une preuve IPA.

## Préparation pour le passage macOS / IPA

Après publication explicitement autorisée de ces sources, les workflows
existants `FEXCore complete Darwin iOS linkage` et
`MaxPS4 FEXCore SideStore IPA` peuvent valider la branche expérimentale.
Les étapes de compilation/liaison C et Swift deviennent obligatoires avant
le packaging, puis Xcode compile toute l'interface et le ZIP final est vérifié.
Les sources locales non publiées ne peuvent pas être testées par les runs
GitHub existants sur `b031576`.

Restent non exécutés : compilation complète Apple ARM64/iOS, import/liaison
Swift, construction Xcode, génération et vérification d'une nouvelle IPA,
signature/provisionnement utilisables et installation/exécution sur appareil.
Le framework public et ses nouveaux contrôles sont préparés, mais la réussite
de ces étapes ne peut pas être annoncée depuis cet hôte sans les outils Apple.

Aucun besoin d'élargir le sous-ensemble V4 n'est identifié. TLS, init/fini,
graphe PRX général, ABI de jeu et instructions non admises restent refusés.
Pas de contournement DRM/PFS, de changement à main, de fusion ou de push.
Aucune correction générale de -48 ni jouabilité commerciale annoncée.

## Fichiers propres à mission 12

- `MaxPS4-iOS/Tests/V4PRXLinkSmoke.c` et `.swift` (nouveaux).
- `cmake/FEXCoreLinkedTests.cmake`, `cmake/FEXCoreIOSLinkage.cmake`.
- `scripts/test-ps4-fex-linked.sh`, `scripts/build-fexcore-darwin-ios.sh`,
  `scripts/build-fexcore-app.sh`, `scripts/verify-fexcore-ipa.py`.
- `.github/workflows/fexcore-ios-linkage.yml` ; le workflow IPA existant
  `.github/workflows/fexcore-app.yml` est réutilisé sans changement de comportement.
- Ce rapport. Les autres modifications locales sont celles des missions 8 à 11.
