# MaxPS4 — émulateur PS4 expérimental pour iOS

[![Plateforme](https://img.shields.io/badge/Plateforme-iOS%20ARM64-222222)](https://github.com/EnermaxFR/MaxPS4)
[![Statut](https://img.shields.io/badge/Statut-portage%20exp%C3%A9rimental-orange)](https://github.com/EnermaxFR/MaxPS4/actions)
[![Licence](https://img.shields.io/badge/Licence-GPL--2.0-blue)](LICENSE)
[![Compatibilit%C3%A9](https://img.shields.io/badge/Jeux%20PS4-non%20jouables%20actuellement-red)](docs/LEGAL-COMPLIANCE.md)

**MaxPS4** est un projet indépendant de recherche et de portage d'émulation PS4 sur iPhone, avec interface SwiftUI, expérimentations ARM64 et intégration progressive de composants issus d'**AetherPS4**, de **FEXCore** et de **shadPS4**.

> **Transparence :** l'application ne sait pas encore lancer un jeu PS4. Un workflow vert, une IPA produite ou un test synthétique réussi ne démontrent ni une émulation PS4 opérationnelle, ni un JIT fonctionnel sur iPhone.

## État vérifié du développement

| Composant | État |
| --- | --- |
| Interface iOS, bibliothèque et importation | Prototypes présents ; ne prouvent pas le démarrage d'un jeu |
| CPU invité AetherPS4 / FEXCore Darwin | Unité `fex_guest_engine.cpp` compilée pour iOS ARM64 dans CI |
| Liaison native complète AetherPS4 | **Bloquée** par des dépendances et symboles non résolus |
| shadPS4 | Utilitaires et pont C++ limités ; portage du noyau et du GPU **non réalisé** |
| Runtime FEXCore Darwin | Liaison iOS ARM64 et contexte natif validés ; bloc x86-64 via JIT testé sur macOS ARM64 |
| FEXCore JIT sur iPhone | **Non opérationnel / non validé sur appareil** |
| Exécution de jeux PS4 (dont Kero Blaster, Sonic Mania) | **Non prise en charge actuellement** |
| IPA de développement | Générée par certains workflows ; **ne garantit pas** la fonctionnalité de l'émulateur |

Les sources amont AetherPS4 sont récupérées pendant certains builds CI pour des tests de portage. Le checkout intégral de shadPS4 fait l'objet de [la PR #83](https://github.com/EnermaxFR/MaxPS4/pull/83) et n'est pas équivalent à un moteur compilé pour iOS.

## Avancement récent — chargeur Orbis et Kero Blaster (17 octobre 2026)

> **Statut de ces travaux :** résultats de développement **locaux** rapportés par les missions Codex 24 à 26. Les modifications ne sont pas encore commitées ni publiées sur GitHub ; leur présence sur `main`, dans une IPA ou sur iPhone **n'est pas établie**.

| Chantier | Résultat rapporté | Limite actuelle |
| --- | --- | --- |
| Mission 24 — chargement ELF | Parcours d'analyse, précontrôle mémoire et dispatch Orbis V4 pour un `eboot.bin` ELF en clair ; homebrew exécuté 3 fois via FEX/VIXL, **RAX=4661, backend 3** | Les SELF ne sont pas encore exécutés par ce parcours |
| Mission 25 — Kero Blaster (CUSA06015) | Analyse locale de `eboot.bin`, `libc.prx` et `libSceFios2.prx`, sans modifier les originaux | Aucun chargement V4 ni lancement du jeu |
| Mission 26 — en-têtes SELF | Incohérence `SELF.HEADER / -ENOEXEC` expliquée par une annexe `PT_SCE_LIBVERSION` et validation stricte des bornes | Nouveau refus `SELF.BLOCK / -ENOTSUP` pour des blocs non pris en charge, notamment signés |
| Validation | Régressions SELF, linker, mémoire et FEX ; ASan/UBSan/LeakSanitizer rapportés réussis | Compilation iOS et test sur iPhone physique non effectués pour ces travaux |

**Priorité suivante :** caractériser précisément les blocs SELF non pris en charge (signature, chiffrement éventuel, compression, offsets et correspondance avec les segments ELF) sans affaiblir les contrôles de sécurité. Les autres obstacles identifiés comprennent le démarrage dynamique, des dépendances manquantes et des relocations TLS de type 16.

**Kero Blaster n'est pas jouable dans MaxPS4 à ce stade.** La présence d'un `eboot.bin` SELF et d'un en-tête ELF intégré ne démontre pas que les segments exécutables soient déchiffrés. Aucun contournement du chiffrement ou des licences n'est revendiqué.

## IPA FEXCore pour SideStore

Le workflow [MaxPS4 FEXCore SideStore IPA](https://github.com/EnermaxFR/MaxPS4/actions/workflows/fexcore-app.yml)
compile l’application Xcode et embarque le véritable runtime Darwin. Télécharger
l’artefact **MaxPS4-FEXCore-SideStore**, extraire l’IPA, puis la faire signer par
SideStore. Dans MaxPS4 : **Diagnostic → Tester FEXCore** affiche contexte, CPUID,
résultat, erreurs et backend réel. Sur iOS, le calcul 40 + 2 utilise actuellement
l’interpréteur natif et le JIT FEX reste indisponible.

Voir [installation, signature, validations et limites iOS 27.2](docs/fexcore-app-sidestore.md).
La compilation ARM64 ne remplace pas une installation et un test sur iPhone.

## Objectifs du portage

1. Compiler et relier les véritables composants CPU, mémoire, HLE et exécution native des deux projets.
2. Adapter les dépendances POSIX/Darwin et les contraintes ARM64 sans simuler les résultats d'émulation.
3. Étudier le rendu graphique compatible iOS et les prérequis d'exécution du code invité.
4. Vérifier le fonctionnement réel sur un appareil iOS, avec logs et tests reproductibles.

**La publication sur l'App Store n'est pas un objectif actuel.** La distribution expérimentale par IPA reste soumise aux licences des composants et aux restrictions techniques et de signature iOS ; un compte développeur Apple ne rend pas automatiquement le JIT disponible.

## Conformité et responsabilité

- **Licences** : MaxPS4 indique la [GNU GPL v2.0](LICENSE). Chaque source ou dépendance tierce doit être attribuée, auditée et redistribuée dans le respect de sa propre licence. Aucun audit de conformité complet n'est revendiqué.
- **Attribution** : contributions et propriété intellectuelle des auteurs originaux conservées. [shadPS4](https://github.com/shadps4-emu/shadPS4) et [AetherPS4](https://github.com/Leviidev/AetherPS4) sont des projets distincts.
- **Marques** : MaxPS4 est indépendant et **n'est affilié ni à Sony Interactive Entertainment / PlayStation ni aux équipes officielles des projets cités**.
- **Contenus** : aucun jeu commercial, firmware PS4, clé, contenu chiffré protégé ou fichier PKG non autorisé n'est fourni. Ne publiez que des contenus que vous avez le droit de redistribuer.
- **Sécurité et honnêteté** : pas de revendication de compatibilité jeu, de JIT actif ou de conformité juridique totale sans preuve technique et examen adapté.

Voir le document détaillé [Conformité, attribution et limites légales](docs/LEGAL-COMPLIANCE.md).

## Builds, contributions et communauté

Consultez les [GitHub Actions](https://github.com/EnermaxFR/MaxPS4/actions) pour distinguer compilation réussie, diagnostics non bloquants et tests sur appareil. Les builds de développement ne sont pas des versions stables de l'émulateur.

Les contributions sont bienvenues via [Issues](https://github.com/EnermaxFR/MaxPS4/issues), [Pull Requests](https://github.com/EnermaxFR/MaxPS4/pulls) et [CONTRIBUTING.md](CONTRIBUTING.md).

[![Discord](https://img.shields.io/badge/Discord-Rejoindre%20MaxPS4-5865F2?logo=discord&logoColor=white)](https://discord.gg/G7BaXN4fd)

**[Rejoindre le Discord MaxPS4](https://discord.gg/G7BaXN4fd)**

---
*Dernière mise à jour documentaire : 17 octobre 2026 — les avancées locales mentionnées ci-dessus restent à publier et à valider sur appareil.*

Développement JIT iPhone : [protocole SideStore + StikDebug et limites de validation](docs/fexcore-stikdebug-jit.md). La PR #131 dépend de la PR #130 ; aucune exécution FEX JIT sur iPhone n’est encore confirmée.

## Prototype GPU Apple Metal

Le diagnostic GPU propose un triangle animé natif Metal, sans émulation GPU PS4.
Voir [les limites, tests et contrôles sur iPhone](docs/metal-gpu-prototype.md).

Integration PR #157: the integration branch retains main's fail-closed iPhone FEX JIT gate. PM4/FEX/Metal execution is validated separately on macOS; the prepared-region provider does not enable iPhone FEX execution. See [integration validation](docs/integration-157-validation.md).

Fondation runtime Orbis : [audit, préparation ELF/PRX et fonctionnalités manquantes](docs/orbis-runtime-foundation.md).

Analyse ELF Orbis/SCE et préparation PRX non chiffrée : [périmètre, limites et recette iPhone](docs/orbis-sce-prx-analysis.md).

Pré-démarrage d'un dossier de jeu accessible : [audit PKG/ELF/PRX/FEX, rapport exportable et procédure Kero Blaster](docs/kero-blaster-preflight.md). Le PFS opaque reste inaccessible ; aucun démarrage du jeu n'est validé.

Audit du lecteur AetherPS4 et diagnostic indépendant d'en-tête PFS : [comparaison, licences et procédure iPhone](docs/aether-pkg-pfs-audit.md). Aucun déchiffrement, montage PFS ou démarrage de Kero Blaster ajouté.

Première scène synthétique interactive : [pixels invités, commandes et import iPhone](docs/first-playable-frame.md). Les preuves FEX natives et l’IPA ne remplacent pas une validation sur iPhone physique.

Diagnostic PKG/PFS en lecture seule et export sélectionné du sous-ensemble D32 non chiffré : [formats, limites et recette iPhone](docs/pkg-pfs-readonly.md). Aucun déchiffrement ni démarrage de Kero Blaster validé.
