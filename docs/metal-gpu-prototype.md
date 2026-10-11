# Prototype GPU natif Apple Metal (issue #138)

Dans l’onglet **Diagnostic**, ouvrir **Diagnostic GPU Metal**, puis **Démarrer le triangle**.
Le périphérique Metal et le résultat de la première commande terminée sont affichés.
Le triangle coloré tourne ; **Arrêter le triangle** suspend le rendu. La fermeture
libère la vue, les drawables et les ressources du moteur après les commandes en vol.
Le passage en arrière-plan suspend le rendu ; le retour reprend sans saut temporel.
La taille du drawable détermine le ratio pour préserver la géométrie après rotation.

L’absence de périphérique désactive le rendu avec un message explicite. Les erreurs
de création du pipeline, de file/encodeur et d’exécution GPU sont affichées sans
forcer de déballage optionnel dans le moteur. Une erreur GPU arrête le test ; fermer
et rouvrir le diagnostic recrée les ressources.

Ce prototype utilise uniquement des shaders Metal natifs et trois sommets générés.
Il ne consomme aucun paquet GCN, shader invité, mémoire graphique PS4 ou contenu
importé. Il ne valide aucune émulation GPU PS4 ni exécution de jeu commercial.
FEXCore, le JIT et le chemin d’import restent indépendants et inchangés.

## Vérification

Le workflow **MaxPS4 FEXCore SideStore IPA** exécute les tests de ratio et animation,
compile les mêmes shaders avec le SDK iPhone et construit l’application complète
avec XcodeGen (Sources inclut automatiquement les nouveaux fichiers). Sur un runner
avec GPU Metal, le test hors écran vérifie les pixels, le fond, la fin des commandes
et la variation entre deux angles. Sans GPU, cette partie affiche SKIP explicitement.
Les tests existants de FEX/JIT restent exécutés par ce workflow.

L’artefact **MaxPS4-FEXCore-SideStore** contient l’IPA pour iOS 18+, ARM64, à re-signer
avec SideStore selon les exigences existantes du projet (application et framework).
Une compilation CI réussie ne valide pas le rendu sur un iPhone physique.

Sur iPhone : vérifier le triangle et les couleurs, démarrer/arrêter plusieurs fois,
faire pivoter l’appareil, passer en arrière-plan, revenir, fermer/rouvrir l’écran,
et vérifier que le diagnostic FEXCore fonctionne toujours. Aucun iPhone physique
n’est disponible dans l’environnement de développement ; ces contrôles restent à
faire sur l’appareil et aucune capture de rendu iPhone n’est revendiquée.
