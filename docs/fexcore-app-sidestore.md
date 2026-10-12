# MaxPS4 — FEXCore embarqué et IPA SideStore

Branche `codex/fexcore-app-integration`, base main
`fecb7ef15b91ae051fe7a82058f412f6f06a4b9d` (PR #129 fusionnée).
Runtime Darwin Aether épinglé `3347fc13f19860855ce91b66356940989ea6b6a6`.

## Intégration

Le dépôt utilisait uniquement swiftc pour assembler une IPA. Le nouveau
`MaxPS4-iOS/project.yml` génère un véritable projet Xcode via XcodeGen.
La cible compile toutes les sources Swift existantes, conserve les ressources
et embarque `MaxPS4FEXRuntime.framework`. Ce framework contient le runtime FEX
cohérent, le pont natif et les adaptateurs CPU existants, sans objets génériques
FEX ni deuxième bibliothèque FEX partagée. L'adaptateur ABI de l'allocateur
Aether est ajouté pour préserver son diagnostic Swift existant ; il n'alloue rien.
La frontière Swift/C utilise le header opaque de #128/#129.

Onglet **Diagnostic → Tester FEXCore** : un écran lance le test sur une tâche
hors du thread UI, interdit les lancements simultanés par ce bouton et affiche
le système réellement détecté, le stade du contexte, CPUID invité et fournisseur,
le debugger détecté (sans en déduire un droit JIT), erreurs, valeur et
backend. Le handle est fermé par defer après chaque test.
Sur iOS, InitCore retourne explicitement ENOTSUP comme dans #129 ; le véritable
interpréteur natif MaxPS4 exécute mov eax,40/add eax,2/ret avec un budget de 32.
Le résultat 42 n'est pas présenté comme une exécution JIT FEX. Les autres erreurs
ne déclenchent pas un repli silencieux. Sur macOS, la même couche Swift appelle
la sonde FEX JIT de #129, mov/add/hlt, sans interpréteur.

Le runtime livré définit MAXPS4_PLATFORM_JIT_ONLY : l'ancien allocateur BRK
expérimental est refusé sur iOS, même si un debugger est attaché. Les interfaces
et diagnostics restent disponibles mais ne tentent pas ce protocole externe.
Aucun framework BreakpointJIT n'est livré ou chargé par le nouveau diagnostic.
Les mécanismes de mémoire exécutable iOS conformes restent à intégrer avant
activation FEX JIT. Ni SideStore, ni une signature de développement, ni un
indicateur debugger ne suffisent à prouver les droits JIT.

## Construction et vérification

Workflow **MaxPS4 FEXCore SideStore IPA** (`fexcore-app.yml`), automatiquement
sur cette PR et disponible manuellement. Les anciens workflows/tests restent
actifs ; l'ancien workflow IPA est nommé Legacy pour éviter de confondre les
artefacts. Les anciennes configurations sans MAXPS4_FEX_APP affichent que FEX
n'est pas embarqué plutôt que d'inventer un contexte.

Le workflow exécute les tests natifs #129, huit diagnostics Swift réels sur
macOS, puis compile le runtime iOS et l'application entière avec Xcode.
`verify-fexcore-ipa.py` contrôle le ZIP, l'exécutable, le framework, ARM64,
LC_BUILD_VERSION iOS, les chemins dyld, les imports/exports C utilisés par
l'app, le véritable CreateNewContext FEX et l'absence de dépendances non SDK
ou symboles FEX manquants. L'icône existante est conservée via un asset Xcode.

Commandes macOS (SDK iPhoneOS, CMake, Ninja, XcodeGen requis) :

```sh
bash scripts/build-fexcore-app.sh /path/to/pinned/AetherPS4
python3 scripts/verify-fexcore-ipa.py build/MaxPS4-FEXCore-SideStore.ipa
```

La compilation ne nécessite pas de certificat personnel. L'app n'est pas
provisionnée et le framework est signé ad hoc après édition de son install name
et après la suppression des headers par Xcode lors de son embarquement :
**SideStore doit re-signer l'app et son framework**. Une signature ad hoc n'est
pas une autorisation d'installation iPhone ni un entitlement JIT.
Déploiement minimal iOS 18, architecture ARM64 ; compatible au niveau du format
avec iOS 27.2, sous réserve de sa signature et des contrôles d'installation.
Le runner utilise Xcode 26.6 et le SDK iOS 26.5. Cette compilation avec un SDK
antérieur à 27.2 ne constitue pas un test sur ce système.

## Installation ou mise à jour SideStore

1. Télécharger l'artefact **MaxPS4-FEXCore-SideStore** dans la page du workflow
   réussi. Extraire le ZIP Actions pour obtenir `MaxPS4-FEXCore-SideStore.ipa`.
2. Disposer d'une installation SideStore fonctionnelle, de ses prérequis de
   pairing/réseau et d'un compte Apple utilisable pour la signature.
3. Dans SideStore, importer l'IPA avec le bouton d'ajout d'application.
   SideStore doit signer aussi le framework embarqué. Respecter les limites
   d'applications/App IDs et la durée du profil indiquées par SideStore/Apple.
4. Pour mettre à jour, réutiliser le même compte et la même identité bundle
   finale. Sauvegarder les imports de jeux avant toute désinstallation :
   supprimer l'app peut supprimer sa bibliothèque locale.
5. Ouvrir MaxPS4 → Diagnostic → Tester FEXCore. Conserver le rapport affiché,
   la version iOS et les éventuels messages d'installation/crash.

En cas d'échec de signature de framework, ne pas annoncer l'IPA installée :
collecter l'erreur exacte de SideStore et le profil/identité sans publier de secret.
Ne pas ajouter d'entitlements privés ou de contournement pour faire passer
l'installation. Aucun jeu PS4 ni JIT sur iPhone n'est déclaré fonctionnel.

## Résultats vérifiés

Code final : `883781beace17cb1e0e3ce495a8a75dd9165eee4`.

| Validation | Résultat | Exécution |
| --- | --- | --- |
| Application iOS entière via Xcode, framework embarqué, IPA | Réussi | [38133153640](https://github.com/EnermaxFR/MaxPS4/actions/runs/38133153640) |
| ABI Swift/C, contexte réel, CPUID et RAX=42 par FEX JIT macOS | Huit cycles réussis | même run |
| Nettoyage heap via leaks --atExit | Huit cycles supplémentaires ; 0 fuite / 0 octet perdu | même run |
| Tests natifs #129, cycles, signaux, repli interprété | Réussis | même run et [38133153657](https://github.com/EnermaxFR/MaxPS4/actions/runs/38133153657) |
| Liaison stricte FEX Darwin iOS et macOS | Réussie | 38133153657 |
| Smoke existant, dont Swift frontend | Réussi | [38133153684](https://github.com/EnermaxFR/MaxPS4/actions/runs/38133153684) |
| Heavy existant | Réussi | [38133153653](https://github.com/EnermaxFR/MaxPS4/actions/runs/38133153653) |
| Installation/signature SideStore sur iOS 27.2 | Non exécutée | appareil nécessaire |
| Interface tactile et diagnostic sur iPhone | Non exécutés ; compilation vérifiée | appareil nécessaire |
| JIT FEX sur iPhone | Non exécuté et volontairement indisponible | fournisseur conforme manquant |

Le log du 11 octobre 2026 contient `Process 6817: 0 leaks for 0 total leaked bytes`
à 10:50:02 UTC, `BUILD SUCCEEDED` à 10:53:49, puis le framework `valid on disk`
et `satisfies its Designated Requirement`. Il s'agit d'une signature ad hoc,
pas d'un profil iPhone. Le contrôle leaks porte sur le heap du programme hôte ;
il ne mesure pas toutes les allocations VM, les tâches SwiftUI ni la mémoire
sur un véritable iPhone.

Neuf tests Python de patch/diagnostics réussis localement, syntaxe shell et
git diff --check réussis. Le test NativeJITPipelineTest.cpp a été recompilé
avec g++ et les sept programmes ont réussi (ARM64 traité comme données,
interpréteur réel). Cet hôte Linux ne possède ni swiftc ni Xcode ; les
compilations et exécutions Apple ont été réalisées par les runners CI.

## IPA livrée et inspection indépendante

[Artefact IPA vérifié — MaxPS4-FEXCore-SideStore](https://github.com/EnermaxFR/MaxPS4/actions/runs/38133153640/artifacts/11699684286),
identifiant `11699684286`, conservation jusqu'au 25 octobre 2026.
L'artefact `11699414712` du même run contient les logs de compilation,
tests, leaks et inspection. Les premières versions des commits cd812d2 et
cdc49cc ont également produit une vraie IPA et passé leurs contrôles ;
l'artefact ci-dessus ajoute la vérification du framework tel qu'il est livré.

L'archive a été téléchargée et contrôlée de nouveau avec verify-fexcore-ipa.py
sur l'hôte Linux. Elle contient :
- Payload/MaxPS4.app/MaxPS4 : véritable exécutable Mach-O ARM64 iOS.
- Payload/MaxPS4.app/Frameworks/MaxPS4FEXRuntime.framework : véritable dylib
  Darwin/FEX et Info.plist, signature ad hoc recalculée.
- Info.plist, Assets.car et les ressources existantes de l'application.

Les deux binaires possèdent MH_NOUNDEFS, sont exécutables et ciblent iOS,
pas le simulateur. L'app importe effectivement open/initialize/cpuid/run/close/stage
depuis le framework. Celui-ci exporte ces fonctions et CreateNewContext réel.
Ses seules dépendances dynamiques sont libSystem et libc++ ; ses 230 imports
SDK ne sont pas des symboles FEX manquants. Aucun objet FEX générique, autre FEX
partagé ou framework BreakpointJIT n'est embarqué. Identité com.enermaxfr.maxps4,
version 0.2, build 3, déploiement iOS 18.0, SDK iphoneos26.5.

| Fichier vérifié | SHA-256 |
| --- | --- |
| IPA | `ab8208861130b525e0f2ee9d138a0fb5f1225e8c7f028d6de062ac05ab1b86c9` |
| Exécutable MaxPS4 | `669c280b22a236071ac90856757a9e22464d1c3897ebeb09471b2ab483223d7b` |
| Framework natif | `75eda8a68853f9333249f5056c73de015427ce4f9a84b6cf637b474ffe1c6ed4` |

Une IPA de compilation vérifiée n'est pas une IPA dont l'installation SideStore
a été exécutée. La signature personnelle de l'app et de son framework,
l'acceptation du profil et le lancement sur iOS 27.2 restent à vérifier.
En cas de refus, conserver l'erreur exacte ; aucun faux paquet ne remplace le build.

## Protocole iPhone et blocages restants

Après installation, vérifier successivement le lancement et les écrans existants,
puis Diagnostic → Tester FEXCore. Résultat attendu d'après le code iOS :
contexte créé/stade 1, CPUID invité GenuineIntel, InitCore ENOTSUP, résultat 42
par l'interpréteur natif MaxPS4, sans JIT FEX. Il s'agit d'une attente, pas
d'un résultat appareil observé.

Répéter vingt fois, fermer/réouvrir l'écran, suspendre et relancer l'app, relever
latence, éventuel crash et évolution du heap/VM avec les outils autorisés
(Instruments/Xcode si disponibles). Tester aussi sous pression mémoire.
Les tâches du diagnostic sont bornées mais les erreurs OOM/assertions internes
FEX ne sont pas garanties récupérables. Aucune absence de crash/blocage/fuite
sur iPhone n'est revendiquée avant ces mesures.

Le JIT iOS nécessite encore un fournisseur de mémoire exécutable autorisé,
des mappings RW/RX avec durée de vie contrôlée, la délégation Darwin complète
des fautes/arrêts/suspensions, l'invalidation du cache et les safepoints.
L'app ne tentera pas d'activer ces fonctions avec une simple présence debugger.
Les syscall/HLE PS4, GPU et exécution de jeux restent hors de cette sonde.

## Commits et état Git

| Commit | Changement |
| --- | --- |
| `cd812d20ff442f90fd8bf9081c3bc686f2078ccd` | Application Xcode, framework réel, écran Swift et IPA vérifiée |
| `cdc49ccd29b3ffa14ba94477dcabe8f2d689b48b` | CPUID/debugger détectés et test des fuites heap |
| `883781beace17cb1e0e3ce495a8a75dd9165eee4` | Signature du framework après copie, modes exécutables et asset icon |

Le commit documentaire suivant complète ce rapport et le README. Ses résultats
CI sont consignés dans la description de la [PR #130](https://github.com/EnermaxFR/MaxPS4/pull/130).
Aucun test antérieur n'est retiré. Aucun commit de fusion ni modification
directe de main ; la PR n'est pas fusionnée automatiquement.
