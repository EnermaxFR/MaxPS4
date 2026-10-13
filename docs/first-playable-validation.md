# Validation de la première image jouable

Base : `e45e750`, main après fusion de #181. Aucun iPhone physique n'est
attaché à cet environnement Linux x86-64. Le critère final sur appareil reste
à valider ; aucune exécution iPhone ni jouabilité de Kero Blaster n'est annoncée.

## Observations locales

- `PS4GuestFrameTest` : admission SCE/PRX, framebuffer borné, entrées, dimensions
  et adresses invalides, isolation, arrêt/repréparation : réussi. Ce test ne lance
  pas FEX et ne prétend pas produire les images de validation.
- `test-orbis-runtime.sh` sous ASan/UBSan : 3362 checks linker et segments ELF/SELF
  réussis. `test-orbis-analysis.sh` : métadonnées SCE/NID, dépendances PRX, RELA,
  inventaire TLS, fichiers malformés et nettoyage transactionnel réussis.
- Les suites système/mémoire s'exécutent aussi sous ASan/UBSan. LeakSanitizer seul
  est désactivé localement, l'environnement tracé ne le supportant pas.
- La compilation locale FEX x86 a été interrompue après réussite des jobs CI
  FEX x86/VIXL et ARM64 natifs ; aucun résultat local FEX n'est revendiqué.

## Images du carré 2 × 2 exécutées sur ARM64 natif

Le commit source `0b207769cfc22150cce5f0243987935cbd6e16df` a passé les
[jobs FEX x86/VIXL et ARM64 Linux, services Linux/macOS et SDK iPhone](https://github.com/EnermaxFR/MaxPS4/actions/runs/38404311584).
Le [job ARM64 natif](https://github.com/EnermaxFR/MaxPS4/actions/runs/38404311584/job/115319769753)
a réellement donné :

```text
PASS guest-frame restart=0 sequence=1 backend=2 input=0 x=7 y=3 bytes=3072 FNV64=5ae9c50e746ed165 guest-memory-equal=yes
PASS guest-frame restart=0 sequence=2 backend=2 input=2 x=8 y=3 bytes=3072 FNV64=1e3241bab45c54e5 guest-memory-equal=yes
PASS guest-frame restart=0 sequence=6 backend=2 input=16 x=7 y=3 bytes=3072 FNV64=c041bebdbc629165 guest-memory-equal=yes
```

Chaque pixel a été comparé ; le snapshot est égal à une lecture indépendante
par l'API mémoire invitée. Le second moteur, fermé puis recréé, exécute une
instruction de couleur modifiée et produit le pixel différent attendu.
Les 12 snapshots RGBA/PPM, leurs journaux et le dossier importable sont joints
à l'artefact `ps4-linked-fex-native-arm64`. L'artefact x86 identifie le backend 3,
exécution des instructions ARM64 générées par FEX dans VIXL, sans preuve native.

## Pont Swift, application iPhone et IPA

Le [workflow macOS](https://github.com/EnermaxFR/MaxPS4/actions/runs/38404311587)
teste le même dossier via Swift et FEX natif : CGImage conserve les octets invités,
le rendu CoreGraphics relu est identique, les entrées modifient les images et
l'ouverture/fermeture du moteur est répétée. Il construit ensuite l'application
avec le SDK iPhone ARM64 et produit l'artefact `MaxPS4-FEXCore-SideStore`.

La première IPA et le pont Swift sont déjà validés sur `74e394b` dans
[ce run](https://github.com/EnermaxFR/MaxPS4/actions/runs/38403223251) ; ce résultat
intermédiaire précède le passage du bloc 2 × 3 au carré 2 × 2. Ne pas l'utiliser
comme preuve du dernier binaire. La PR #182 fournit les liens et le SHA256 de
l'IPA du dernier commit validé. Un correctif conserve la compatibilité du contrôle
frontend iOS 16 pour les callbacks SwiftUI ; l'application vise iOS 18.

Les workflows et preuves se rapportent toujours au SHA testé, pas à une promesse
de réussite. Compilation ARM64, exécution macOS/Linux et IPA ne prouvent pas une
exécution sur iPhone physique. Les manettes/touches et la fermeture en arrière-plan
sur appareil demandent encore l'essai physique ci-dessous. Audio différé.

## Validation physique à réaliser

Installer l'IPA du commit testé, activer StikDebug, lancer la scène, enregistrer
une vidéo montrant l'image et ses déplacements tactiles, partager le rapport
backend/numéro/FNV64 et noter modèle, version iOS et méthode d'activation JIT.
Tester arrêt, reprise explicite après arrière-plan, manette et désactivation des
commandes tactiles. Les preuves CI natives et les snapshots sont complémentaires.
