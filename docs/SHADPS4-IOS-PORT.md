# Feuille de route du portage natif shadPS4 → MaxPS4 iOS

**État : étude de faisabilité et interface de raccordement uniquement ; aucun moteur shadPS4 compilé dans l'IPA.**

## Provenance vérifiable

- Amont : https://github.com/shadps4-emu/shadPS4
- Révision examinée : `0fe263a4760dfbfa973366890061749b4af0de97` (7 octobre 2026).
- Licence amont : GPL-2.0, avec des fichiers déclarés SPDX `GPL-2.0-or-later`. Examiner la licence exacte de chaque fichier et dépendance avant intégration. Conserver les avis et rendre disponible le code source correspondant à toute distribution binaire.
- Source des remarques de compilation : `CMakeLists.txt` de la révision ci-dessus.

## Différences concrètes de plateforme

1. **Architecture :** le CMake amont reconnaît `arm64`, mais cette détection n'implique pas que l'exécution des binaires PS4 x86-64 fonctionne sur iPhone. Il faut un moteur d'exécution x86-64 adapté à ARM64, avec une politique iOS compatible pour le code généré.
2. **Cible :** la présence de `if(APPLE)` dans CMake ne prouve pas une prise en charge `iphoneos`. Compiler pour iPhone requiert un SDK, des frameworks et des chemins de dépendances compatibles iOS, pas seulement macOS ARM64.
3. **Graphismes :** un adaptateur d'API graphique utilisable sur iOS doit remplacer ou rendre compatible le backend graphique existant ; vérifier les fonctions GPU nécessaires jeu par jeu.
4. **Services PS4 :** loader SELF/ELF, mémoire invitée, appels système et bibliothèques HLE doivent être validés avant tout lancement.
5. **Distribution :** licence GPL de l'ensemble dérivé, notices des dépendances, source correspondant à l'IPA, restrictions Apple et droits sur les contenus doivent être audités.

## Jalons d'intégration

- [x] Préparer une interface Swift `MaxPS4ShadPS4Backend` et quatre critères explicites de disponibilité.
- [x] Documenter les obligations d'attribution, les limites de l'application actuelle et la provenance de l'amont.
- [ ] Sélectionner un ensemble minimal de sources C++ amont et de dépendances compatibles `iphoneos-arm64`, avec leurs notices de licence.
- [ ] Compiler **réellement** ce sous-ensemble dans CI iOS et enregistrer les erreurs, sans substituer un stub présenté comme une émulation.
- [ ] Relier une fonction C ABI de diagnostic native à Swift et tester le chargement sur iPhone.
- [ ] Réaliser l'exécution x86-64 sur ARM64, les services PS4 et le rendu graphique.
- [ ] Tester avec un jeu dont les fichiers sont acquis/utilisés légalement ; ne pas inclure de PKG, firmware, clés ou actifs commerciaux dans le dépôt.

**Ne jamais déclarer le jeu jouable uniquement parce que CI a produit une IPA.**
