# Correctif #167 : consommer les régions StikDebug dans le véritable JIT FEX

Ce correctif est empilé sur les PR brouillon #166 puis #168. Aucune fusion
autorisée. Le nom visible reste MaxPS4. Il active un chemin d'exécution FEX
conditionné aux permissions réelles ; il ne constitue pas une preuve sur iPhone.

## Origine précise de -45

Dans la version de main `aae0b434fc87b7503eed2b767300a4607f4634e2`,
`maxps4_fex_engine_initialize()` retourne explicitement `-ENOTSUP` sous
`TARGET_OS_IPHONE`, avant `InitCore()`. `runELF()` refuse aussi inconditionnellement
l'exécution sous iPhone. Darwin définit ENOTSUP à 45. Ce n'est donc ni un code
Mach, ni un diagnostic de droits réellement interrogés. L'état fournisseur 1
indique une demande en attente : ce runtime n'appelait jamais
`maxps4_jit_prepare()`. CPUID au stade 1 et un résultat interprété 42 ne valident
aucune instruction exécutée par FEX.

Le correctif appelle `maxps4_jit_prepare()` avant `InitCore()` et utilise le même
chemin `CreateThread()` / `ExecuteThread()` que macOS pour les ELF admis.
Sans activation : `-EACCES` ; sans nonce final correspondant : `-EAGAIN` ; si
Mach refuse les alias/protections : l'erreur et l'étape restent affichées.
Un seul contexte peut réserver le dispatcher/cache préparé (`-EBUSY` sinon).
Le remplacement de l'allocateur dans le checkout FEX épinglé est déjà appliqué
par `build-fexcore-darwin-ios.sh`. L'allocation du code cache échoue désormais
par `std::bad_alloc` rattrapé par le pont C, au lieu d'une assertion potentiellement
désactivée suivie d'une écriture via un pointeur nul.
Les objets FEX sont compilés avec les exceptions activées afin que ce refus
remonte effectivement au pont C. Les constructions partielles du thread,
dispatcher et cache sont nettoyées pendant ce retour d'erreur. La taille utile
du cache et la classification des PC excluent toute la page de garde hôte
(16 Kio sur iPhone), au lieu d'exclure seulement une page invitée de 4 Kio.

## Contrat StikDebug et permissions

La requête `stikdebug://enable-jit` contient le bundle réel, le PID courant et
les octets UTF-8 du seul script livré dans `script-data` (base64url). Les
constantes fournies sont une adresse de mailbox alignée, un nonce aléatoire
non nul et les tailles 16 Kio, 16 Mio et une page hôte pour le canari.
Le script vérifie le challenge nonce de la demande dans la mailbox avant toute
allocation (rejeu refusé, y compris après une nouvelle activation), prépare
les RX via les services debugger, publie les métadonnées puis le nonce en
dernier pendant l'arrêt du processus, et détache. Une ouverture d'URL refusée
annule uniquement sa propre demande non publiée ; une demande déjà transmise
n'est jamais annulée pendant que le debugger peut écrire.

Le retour à MaxPS4 affiche une confirmation séparée. Elle n'annonce jamais
« JIT actif ». Le test vérifie les protections RX de chaque sous-région VM,
le remap partagé, les alias RW sans X et RX sans W, leurs plafonds, la cohérence
mémoire, les caches d'instructions et un canari ARM64. Le code Mach d'un échec
de page de garde est désormais conservé. Le canari ne sélectionne pas le backend
FEX. Le backend FEX est sélectionné uniquement après retour de `ExecuteThread()`.

StikDebug peut ignorer le script lorsque sa détection TXM indique une absence.
MaxPS4 ne prétend pas obtenir des régions dans ce cas : la confirmation reste
absente. Utiliser les réglages officiels de StikDebug pour exécuter le script
fourni si nécessaire. Aucun paramètre d'URL non reconnu n'est inventé. Le
pairing, VPN, DDI et la signature effective `get-task-allow` restent à vérifier
dans StikDebug / SideStore ; aucune protection ou signature n'est modifiée par
le runtime. Si iOS termine le processus pour une violation de signature, le
runtime ne peut pas récupérer un SIGKILL ; la validation physique est nécessaire.

## Validation iPhone

1. Installer l'IPA de **ce commit** provenant du workflow
   « MaxPS4 FEXCore SideStore IPA », avec une signature SideStore compatible.
2. Sans activation, « Tester FEXCore » : backend interpréteur, résultat 42,
   initialisation négative, jamais « JIT actif ».
3. « Activer avec StikDebug » : confirmer la requête pour ce PID dans StikDebug.
   Attendre trois préparations RX puis `MaxPS4 detach: OK`. Revenir au même
   processus MaxPS4 ; le nonce doit être confirmé.
4. « Tester FEXCore » : exiger initialisation **0**, exécution FEX **0**,
   stade **2**, fournisseur **2**, RAX **42**, backend **FEXCore JIT ARM64**,
   **JIT actif** et **0** régions encore utilisées après nettoyage.
5. Importer `MaxPS4-test-123.elf` (créé par `scripts/create-test-elf.py`) : exiger
   backend FEX ARM64 JIT et RAX **123**. Le repli interprété 123 n'est pas une
   validation JIT. Les ELF restent limités aux instructions admises et HLT final.
6. Libérer la mémoire : l'ancien succès disparaît et le prochain test revient
   au repli. Vérifier aussi ouverture d'URL refusée, attach refusé et session
   sans confirmation : aucun faux succès, pas d'attente bloquante.

Conserver modèle iPhone, build iOS, versions SideStore/StikDebug, signature
effective, log script, diagnostic complet et éventuel crash report.

## Kero Blaster CUSA06015 : blocages précis

Le mappage ELF/SELF clair et la relecture du point d'entrée sont déjà présents.
Le PFS opaque ne donne pas accès à l'exécutable ; aucun déchiffrement DRM n'est
ajouté. Il faut le fichier clair et ses bibliothèques légalement accessibles
pour inventorier les imports/NID et relocations effectivement nécessaires.
Le dispatch de l'image PS4 reste bloqué : adressage absolu invité, ABI Orbis,
relocations/imports, TLS, services et routage des exceptions manquent. L'exécution
de l'ELF de test utilise une arène relocalisée et des instructions contrôlées,
ce qui ne suffit pas pour lancer un ELF PS4 arbitraire. PM4→Metal existe pour
une soumission limitée ; shaders/textures/services graphiques du jeu, manettes
et audio PS4 ne sont pas encore raccordés. Aucun démarrage ou jeu jouable annoncé.

## Tests

Tests JS : paquets GDB, publication du nonce, rejeu refusé, rollback et détachement.
Tests Swift : URL/PID/bundle/nonce exacts, base64url UTF-8 et paramètres invalides.
Tests Mach macOS : nonce périmé, échec d'ouverture, confirmations distinctes,
permissions réelles, leases et nettoyage. Les tests Mach et Swift et le build
iOS ARM64/IPA sont exécutés par le workflow macOS ; aucun SDK Apple n'est installé
dans le workspace Linux. Les résultats CI et l'artefact doivent être confirmés
avant de déclarer une IPA disponible. Aucun test physique iPhone à cette étape.
