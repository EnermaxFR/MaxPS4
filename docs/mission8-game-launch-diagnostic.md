# Mission 8 — absence du bouton Jouer

Audit local du 16 octobre 2026, depuis `b031576` sur
`experiment/maxps4-ubuntu-codex`. Aucun push ni lancement de jeu.

## Cause dans l'application

`MaxPS4Emulator.importGame` reconnaît la signature CNT d'un PKG, puis
`copyForLibrary` valide sa structure et publie une copie locale avec une entrée
dans la bibliothèque. Cela n'installe pas son PFS et ne produit pas d'exécutable.
Un PKG chiffré valide reste importable pour inventaire.

Avant cette modification, `MaxPS4HomeView.gameCard` choisissait **Infos** pour
les `.pkg`, et **Ouvrir** avec une icône de lecture pour les autres fichiers.
La fiche affichait toujours « Exécution PS4 indisponible ». Le diagnostic PFS
n'apparaissait qu'après une action manuelle. Il n'existait pas de bouton
**Jouer** conditionné par l'admission d'un jeu.

`MaxPS4Emulator.launch` refuse explicitement les PKG et exige un moteur natif
prêt pour les autres fichiers. `connectNativeEngine` n'a aucun appel dans les
sources de l'application. Les capacités exigées d'un backend shadPS4 sont le
chargement PS4, l'exécution invitée, les bibliothèques système et le rendu.
Les démonstrations et diagnostics FEX/Orbis constituent un parcours distinct ;
ils ne rendent pas un PKG de la bibliothèque lançable.

## Kero Blaster CUSA06015

La documentation `pkg-pfs-readonly.md` rapporte le constat de l'issue #190 :
PFS chiffré déclaré, mode `0x000d`, `eboot.bin` inaccessible. Le PKG commercial
n'est pas présent dans cet environnement : ce constat historique n'est pas
une nouvelle mesure sur le fichier de l'utilisateur.

Le parseur actuel confirme, sur un en-tête synthétique, que `mode & 4` est
non nul pour `0x000d` et classe ce PFS comme chiffré. Seuls les 72 octets
d'en-tête sont lus par ce diagnostic ; aucun contenu chiffré n'est parcouru.
Le test via l'ABI C vérifie qu'un tel conteneur reste importable, que le scan
des candidats reste à zéro et que lecture et requête de taille d'eboot.bin
échouent sans publier de données.

| État | Ce qu'il établit |
| --- | --- |
| Détecté | Conteneur reconnu ; ni authenticité ni compatibilité prouvées |
| Importé | Copie locale et entrée de bibliothèque ; pas une installation |
| Exécutable accessible | À établir séparément ; PFS chiffré : inaccessible. Une entrée eboot.bin dans un PFS lisible ne prouve pas son admission ELF/SELF |
| Lancement disponible | Exige un exécutable admis et un moteur de jeu opérationnel ; non établi pour Kero Blaster |

## Modification locale

La fiche analyse automatiquement la copie PKG hors du thread principal et
affiche ces états avec les diagnostics PFS et le mode effectif. Un PFS inconnu
n'est pas qualifié de chiffré ; un PFS lisible reste distinct d'un lancement.
Un fichier disparu ou invalide est signalé comme non validé. Le résumé reste
visible pendant l'utilisation des autres diagnostics et ignore un résultat
d'une fiche dont la tâche a été annulée.

Les cartes ouvrent maintenant la fiche **Infos**, y compris pour ELF/SELF,
plutôt que d'afficher un contrôle de lecture sans moteur de jeu connecté.
Aucun bouton Jouer n'est ajouté faute de lancement de jeu établi. Les messages
de diagnostic interpolent correctement les noms de fichiers et le rendu de
la fiche ne déclenche plus le test CPU synthétique du rapport de disponibilité.

## Validation

Tests de régression ajoutés au test natif PFS et au test Swift déjà exécutés
par `.github/workflows/pkg-inventory.yml`. Le test Swift couvre le mode
`0x000d`, le PFS non pris en charge, le chiffrement inconnu, un fichier manquant
et le PFS lisible avec eboot.bin : aucun ne reçoit une promesse de lancement.

Le test natif PFS passe localement avec ASan/UBSan et détection des fuites,
hors traçage sandbox. Les suites `PS4PFSReadOnlyTest` et `PS4PKGInventoryTest`
passent également avec les mêmes sanitizers ; les exécutables et fixtures de
cette mission sont sous `/tmp/maxps4-mission8`. `git diff --check` passe.
Swift, SwiftUI, xcrun et le SDK iOS ne sont pas disponibles
ici : les nouvelles assertions Swift et la compilation iPhone ne sont pas
exécutées localement. Aucun workflow distant n'est lancé pour ces changements
non publiés. Une validation visuelle sur iPhone et une nouvelle analyse du PKG
utilisateur restent nécessaires. Aucun contournement PFS, lancement simulé,
preuve de jouabilité ni correction de l'erreur historique -48 n'est revendiqué.
