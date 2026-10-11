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
le système réellement détecté, le stade du contexte, CPUID, erreurs, valeur et
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
provisionnée et le framework est signé ad hoc après édition de son install name :
**SideStore doit re-signer l'app et son framework**. Une signature ad hoc n'est
pas une autorisation d'installation iPhone ni un entitlement JIT.
Déploiement minimal iOS 18, architecture ARM64 ; compatible au niveau du format
avec iOS 27.2, sous réserve de sa signature et des contrôles d'installation.
Le SDK sélectionné par le runner sera documenté après CI : un build avec un
SDK antérieur à 27.2 ne constitue pas un test sur ce système.

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

## Validation actuelle

Neuf tests Python de patch/diagnostics réussis localement, syntaxe shell et
`git diff --check` réussis. Cet hôte Linux ne possède ni swiftc ni Xcode.
La compilation Apple, les tests Swift et l'IPA restent à vérifier par CI.
Aucun iPhone physique n'est disponible ici : installation SideStore, crash,
blocage, consommation mémoire et stabilité sur iOS 27.2 doivent être mesurés
sur appareil. Le test prévu est borné et libère ses ressources ; cela ne prouve
pas l'absence de fuite ou de crash sur une version d'iOS non exécutée.
