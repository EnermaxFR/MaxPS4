# Première image jouable : démonstration synthétique

La scène est un programme x86-64 original, sans ressource commerciale,
redistribué sous la licence GPL v2 du dépôt (voir `LICENSE`). Elle
passe par ELF SCE → dépendance PRX explicite → NID fictif qualifié → RELA →
pages invitées possédées → admission CPU → FEXCore → HLT. Les pixels ne sont
pas dessinés en Swift : Swift reçoit une copie RGBA de la mémoire invitée et
la confie à CGImage/UIKit, affichée sans interpolation. Ce protocole de démo
n'est pas l'ABI graphique PS4 et ne rend pas Kero Blaster jouable.

## Utilisation sur iPhone

1. Installer l'IPA `MaxPS4-FEXCore-SideStore` issue du workflow de cette PR avec
   SideStore, puis activer le JIT par le parcours StikDebug existant.
2. Ouvrir **Diagnostic FEXCore → Première image jouable → Lancer / relancer**.
   L'app contient exactement les ELF/PRX fabriqués et comparés par les tests.
3. Maintenir Gauche/Droite/Haut/Bas pour déplacer le carré. Action change sa
   couleur. La position boucle aux bords ; deux directions opposées s'annulent.
4. Connecter une manette iOS : pavé directionnel ou stick gauche, bouton A.
   Désactiver « Commandes tactiles » masque le pavé et efface les boutons tactiles.
5. Arrêter, relancer, puis passer en arrière-plan : l'arrêt annule la cadence
   et libère le moteur sur la file de travail. Le retour exige un lancement explicite.
6. Partager le rapport : numéro d'image, backend ARM64, boutons et FNV64 des
   octets RGBA copiés. Une capture/vidéo de l'iPhone reste nécessaire pour attester
   le critère physique ; ce rapport seul ne prouve pas l'affichage sur appareil.

**Importer une démo Orbis** accepte un dossier autorisé contenant `maxps4.json`
et les modules en clair. Le dossier `playable-bundle` des artefacts CI est
importable ; le ZIP doit être décompressé dans Fichiers avant sélection du dossier.
Le dossier fourni dans `MaxPS4-iOS/Resources/PlayableDemo` peut aussi être transféré.
Un PFS opaque ou SELF protégé reste refusé. Aucun déchiffrement/DRM n'est ajouté.

## Contrat borné et provenance

`frame` dans le manifeste décrit `pixels`, `input`, `width`, `height` : adresses
logiques invitées, pas des pointeurs hôte. Exemple fourni : pixels `0x110100`,
entrée `0x110010`, 32 × 24 pixels RGBA8, stride = largeur × 4. Chaque zone est
entièrement RW/non-X ; les zones sont alignées sur quatre octets et disjointes.
Dimensions autorisées : 1…320 × 1…240. Une configuration invalide efface la
configuration précédente. La taille de copie doit être exacte ; aucune sortie
partielle n'est publiée en cas d'erreur.

Les boutons sont un mot little endian : gauche=1, droite=2, haut=4, bas=8,
action=16. Seul ce mot est écrit par le pont hôte. Les instructions invitées
lisent les bits, calculent x/y, sélectionnent la couleur et écrivent tous les
pixels ainsi qu'un compteur. La position reste dans les pages invitées entre
images. Le pont natif n'écrit ni coordonnées ni couleurs. La PRX est appelée
par le CALL résolu et relocalisé à chaque image.

L'admission ajoute uniquement des formes déterminées de MOV EAX/EDX RIP-relatif,
MOV EBX,EAX, MOV EDX/EBX immédiat, AND/OR immédiat, SHL/SHR immédiat,
ADD/SUB EDX,EAX, CMP EDX immédiat et CMOVE EAX,EBX. Les registres lus doivent
être initialisés, la comparaison doit être valide et tous les accès mémoire
sont prouvés avant dispatch. Pas de pointeur calculé, branche ni boucle invitée.
Les streams chevauchants restent refusés. Le dessin est déroulé en instructions
finies : ≤65536 instructions source admises, appels acycliques de profondeur ≤32,
HLT obligatoire. C'est un budget statique, pas une mesure de cycles FEX.

Un tick sur la file FEX existante prend les entrées, exécute un thread FEX avec
pile gardée, copie le framebuffer après HLT et publie une image immuable sur le
thread principal. Cadence cible ≈30 Hz ; pas de rattrapage ni de backlog d'images.
Un seul snapshot peut attendre l'UI. Le cache de code est conservé pour une même
image admise, invalidé à la préparation/configuration et aux autres dispatchs.
Aucune instruction invitée ne peut modifier le code X. L'arrêt ne préempte pas
une instruction FEX : le dispatch fini en cours termine puis ferme son moteur.
Les défauts internes imprévus de FEX ne sont pas transformés en succès ou gérés
comme des signaux invités. Les tests de processus ont une limite de temps externe.

## Tests et preuves

```sh
ASAN_OPTIONS=detect_leaks=0 bash scripts/test-ps4-system-services.sh
bash scripts/test-orbis-runtime.sh
bash scripts/test-orbis-analysis.sh
bash scripts/test-ps4-fex-linked.sh /chemin/aether-epingle build/ps4-linked
```

- Tests mémoire : dimensions zéro/excessives/overflow, plage invalide, zone X,
  chevauchement, boutons invalides, taille incorrecte, admission, arrêt/repréparation.
- Test lié réel : 12 images par moteur, deux ouvertures/fermetures, état x/y et
  compteur lus depuis la mémoire, chaque pixel comparé, snapshot égal à une lecture
  indépendante de la mémoire invitée, action et directions opposées.
- Deuxième moteur : modification de l'immédiat d'une instruction de pixel ; le
  pixel effectivement exécuté change, les autres restent identiques.
- Export `playable-evidence/frame-N.{rgba,ppm}` et journal donnant backend/FNV64.
  Les PPM convertissent seulement les snapshots observés ; aucune scène hôte.
- CI Ubuntu x86 : FEX traduit vers ARM64 puis VIXL exécute ces instructions,
  backend=3. CI Ubuntu ARM64 et macOS ARM64 : backend=2, exécution native.
- Test Swift macOS : import du même dossier, FEX natif, CGImage conservant les
  octets invités et rendu CoreGraphics avec readback identique, plusieurs images
  et redémarrage. Les fixtures exportées sont comparées aux ressources de l'app.
- Workflow iOS : construction complète iPhone ARM64 et IPA SideStore existante.

Les résultats effectivement observés et les liens du commit validé seront
consignés dans `first-playable-validation.md`. Une CI prévue ne vaut pas un test
réussi, et une compilation iPhone ne vaut pas une exécution sur iPhone physique.

## Audio et compatibilité avec un jeu réel

Audio isolé et différé : ce chemin SCE ne possède pas de service audio-out ni
trampoline audio. Aucune tonalité hôte ne représente un son invité. Étape suivante :
contrat PCM invité borné, cadence et file audio limitée, transmission à AVAudioEngine,
gestion des interruptions/session audio et tests de provenance des échantillons.

Avant un vrai jeu PS4, il reste notamment : exécutables et ressources obtenus
légalement en clair ; instructions/adressage et contrôle de flux généraux avec
interruptions et budgets dynamiques ; ABI processus, TLS, init/fini PRX ; bindings
HLE/NID réels ; threads/synchronisation/events et système de fichiers ; scePad,
video-out/GNM/PM4, shaders et mémoire GPU ; audio-out et synchronisation ;
validation JIT, mémoire, performances et stabilité sur appareils physiques.
Aucun contenu Kero Blaster n'est inclus ; aucune compatibilité jouable revendiquée.
