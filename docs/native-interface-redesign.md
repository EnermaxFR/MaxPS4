# Interface native MaxPS4

## Architecture et périmètre

L’application utilise SwiftUI, avec UIKit pour le sélecteur de fichiers, les images
et le presse-papiers, GameController pour les entrées, Metal pour les vues de rendu.
`MaxPS4App` injecte l’objet observable `MaxPS4Emulator` dans `MaxPS4HomeView`.
La bibliothèque reste stockée et importée par l’émulateur existant. Les vues
FEX, Metal, dossier Orbis, export PFS, démo jouable et Max Maze sont conservées.

Les cibles application actuelles sont iOS 18+ ARM64 (iPhone et iPad), `MaxPS4`
et `MaxPS4OrbisTest`. L’inspiration macOS concerne le design : aucune cible macOS
native n’est ajoutée. Les tests Swift de présentation s’exécutent sur macOS.

Branche : `design/native-library`, basée sur la préparation IPA `f581e5f`.
Les modifications de runtime antérieures appartiennent à cette base et ne font
pas partie de la refonte.

## Interface

- `NavigationSplitView` : barre latérale sur écran large, navigation repliée sur iPhone.
- Bibliothèque : grille adaptative ou liste, recherche par titre et nom de fichier,
  plusieurs mots, accents et casse ignorés ; tri naturel ou date d’import.
- Grille et liste partagent les actions : fiche, lancement existant, renommage,
  diagnostic, analyse, suppression avec confirmation.
- Le lancement appelle exclusivement `emulator.launch`. Il est désactivé si le
  backend n’est pas prêt ou si le fichier est un PKG ; le diagnostic reste accessible.
- Apparence automatique, claire ou sombre ; présentation et tri mémorisés avec AppStorage.
- Couleurs système, surfaces arrondies, typographie Dynamic Type et barre native translucide.
- Paramètres groupés : apparence, émulation, contrôles, diagnostics, stockage, informations.
- Les anciennes préférences FPS/réseau sont conservées mais les contrôles sont
  désactivés et expliqués, car elles n’ont pas d’effet sur le moteur.
- Manettes : inventaire GameController réel, actualisation et notifications de connexion.
  Les affectations de la démo restent identiques ; aucune fonction de remappage fictive.
- La suppression globale demande maintenant confirmation.

La couche de présentation est isolée dans `MaxPS4LibraryPresentation.swift`.
Le modèle de jeu, le stockage, le moteur, le runtime, les sources natives, les règles
JIT et les entrées de la démo ne sont pas modifiés.

## Validation

`LibraryPresentationTest.swift` vérifie la recherche Unicode/par fichier/multimot,
les tris, les doublons, les préférences stockées et la disponibilité du lancement.
Le workflow `native-ui.yml` exécute ces tests sur macOS et compile tous les fichiers
Swift de production avec `MAXPS4_FEX_APP`, les vrais en-têtes ABI et les SDK iPhoneOS
et iPhoneSimulator ARM64. Il compile des objets Swift ; il ne remplace pas le lien
complet de l’application, la signature, l’installation ou les tests sur appareil.

Les corrections de portabilité des contrôles Apple restent dans les tests et scripts :
les fixtures synthétiques de boot/SELF/eboot et de compatibilité FEX utilisent des
pages distinctes de 16 KiB lorsqu’elles doivent être mappées ; leurs assertions
et refus sont conservés. Les tableaux d’arguments des scripts homebrew/FEX restent
non vides pour Bash 3 sur macOS, avec les mêmes options de compilation.

### Résultats vérifiés

- Workflow interface : succès, révision Swift `3219a0c`,
  https://github.com/EnermaxFR/MaxPS4/actions/runs/39272042180.
- Tests de présentation sur macOS : succès.
- Compilation des sources Swift iPhone ARM64 : succès.
- Compilation des sources Swift simulateur ARM64 : succès.
- Fixtures boot, SELF et eboot corrigées : succès local ASan/UBSan.
- Suite FEX de traduction corrigée : succès dans un exécutable isolé sous
  simulateur ARM64 VIXL sur Linux, avec les bibliothèques existantes inchangées.
- Construction IPA complète : suivi séparé,
  https://github.com/EnermaxFR/MaxPS4/actions/runs/39277105299.
  Un succès de compilation Swift ne prouve ni le lien complet, ni la génération
  d’une IPA, ni son installation ou sa jouabilité sur iPhone.

Recette visuelle sur appareil : navigation compacte et large, modes clair/sombre/système,
texte agrandi, recherche vide/sans résultats, tri, grille/liste, imports, fiches,
renommage/suppression, annulation des alertes, retour des diagnostics, connexion et
déconnexion d’une manette, maintien des entrées dans la démo. Aucun jeu n’est ajouté.

Références Apple :
- https://developer.apple.com/documentation/swiftui/navigationsplitview
- https://developer.apple.com/documentation/swiftui/adding-a-search-interface-to-your-app
