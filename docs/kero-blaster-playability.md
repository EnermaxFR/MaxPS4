# Kero Blaster : progression vers l’exécution

Base : `436311f` sur main, après #179 et #180. Aucun contenu commercial,
clé, firmware ou exécutable propriétaire n’est inclus dans cette évolution.

## Ce que ce changement permet

Le nouveau chemin joint le linker SCE à des pages appartenant au moteur puis
à FEX : ELF/PRX en clair → graphe explicite → NID qualifiés → RELA → copie des
segments et du BSS relocalisé → permissions → admission CPU → pile privée
protégée → thread FEX → CALL/RET entre modules → HLT → destruction du thread.
Les octets du fichier et l’image du linker ne sont jamais modifiés. Les appels
GOT admis sont réécrits en CALL relatif de même longueur (CALL + NOP) dans une
copie privée avant application des protections. Le GOT conserve ses adresses
invitées, y compris une relocation dans le BSS.

`maxps4_fex_engine_prepare_orbis` ne lance aucune instruction.
`maxps4_fex_engine_run_orbis` ne réussit que si FEX a terminé ; il fournit RAX,
l’entrée logique et le backend réel : 2 = ARM64 natif, 3 = simulateur ARM64 VIXL
sur x86. Aucun repli interpréteur n’existe dans ce chemin.

L’admission accepte MOV EAX immédiat, ADD EAX immédiat, NOP, des stores C7
RIP-relatifs prouvés RW/non-X, CALL GOT vers une fonction invitée admise, RET
dans une fonction et HLT dans la racine. Les autres instructions sont refusées.
Les appels sont acycliques, la profondeur est limitée à 32, et le budget est
consommé sur chaque instruction source visitée, même dans les fonctions appelées.
Les NOP générés après CALL ajoutent une instruction CPU finie par appel ; ce
budget est un contrôle d’admission, pas un compteur mesuré par FEX.

Les pages x86 sont des données hôte R ou RW, jamais PROT_EXEC. Les permissions
X invitées servent aux contrôles du CPU ; seul le code ARM64 produit par FEX
utilise le fournisseur JIT existant. Un arena rebased conserve les distances
entre tous les modules ; les pages partagées entre segments sont refusées,
ainsi qu’une étendue totale supérieure à 32 Mio, même avec peu de données.
RELRO est copié avec ses permissions finales. Aucun pointeur hôte n’est renvoyé
à Swift. Les tables, adresses et ressources doivent rester vivantes pendant la
préparation ; ensuite le moteur possède ses pages.

Chaque exécution crée une pile RW encadrée de pages PROT_NONE, initialise RIP,
RSP, GDT et pile CALL/RET FEX, puis détruit le thread et ses piles. Une nouvelle
préparation, même refusée, invalide l’admission précédente et ferme les services.
Fermer le moteur libère les mappings et le contexte. Les régions StikDebug
réservées suivent leur cycle de vie existant.

Les erreurs mémoire sont refusées **avant** dispatch par une preuve des accès
admis. Cette protection ne constitue pas un gestionnaire de signaux pour du
code arbitraire : les fautes imprévues dans FEX ne sont pas converties en crash
Orbis récupérable. Les syscalls, TLS, paramètres processus SCE et initialisations
PRX non interprétées restent bloqués ; aucune initialisation n’est simulée.

## Importation iPhone autorisée

Dans Diagnostic FEXCore : « Importer et exécuter un dossier Orbis compatible ».
Sélectionner un dossier contenant `maxps4.json` et des fichiers ELF/PRX en clair
que l’on est autorisé à utiliser. Les noms sont des basenames uniques ; pas de
traversée de répertoire ni de lien sortant du dossier. Le manifeste est limité
à 64 Kio, chaque fichier à 32 Mio, le total à 64 Mio et le graphe à 128 modules.
Exemple pour les fixtures libres fournies par les tests :

```json
{
  "root": "app.elf",
  "modules": [
    {"path": "app.elf", "loadBias": 1048576, "dependencies": ["provider.prx"]},
    {"path": "provider.prx", "loadBias": 2097152, "dependencies": []}
  ]
}
```

Le parcours vérifie les fichiers, lie les modules, prépare les pages et admet
le CPU avant d’initialiser FEX et d’exécuter. Si StikDebug/JIT est indisponible,
il indique que l’exécution n’a pas été tentée. Il affiche RAX et l’entrée après
exécution terminée. L’image, les entrées de jeu et l’audio sont annoncés comme
indisponibles. La signature SideStore et l’activation JIT restent inchangées.

Ce dossier ne remplace pas les données de Kero Blaster. Aucun outil de
contournement ou de déchiffrement n’est fourni : obtenir les fichiers en clair
par une voie autorisée en dehors de MaxPS4. Un PKG reconnu à PFS opaque ne
fournit pas eboot.bin, les PRX ni les ressources ; l’action de lancement PKG
explique ce blocage et indique le nouvel import. Aucun exécutable réel de
CUSA06015 n’a été inspecté dans cette mission, donc ses imports requis restent
inconnus. Un dossier réel peut encore être refusé par les formats ou le CPU.

## Audit du lancement et obstacles restants

| Étape | Fonction réellement disponible | Blocage avant Kero Blaster |
| --- | --- | --- |
| PKG | Inventaire borné, identification et SFO, fichiers conservés | PFS CUSA06015 opaque ; aucune extraction des exécutables/ressources |
| ELF/SELF/PRX | Chargeur plaintext ; linker SCE ELF/PRX explicite | SELF protégé refusé ; formats/tags non interprétés refusés |
| NID/dépendances | Identité NID/bibliothèque/module/versions/type exacte, RELA 1/6/7/8 | Fournisseurs réels légalement disponibles et bindings HLE SCE manquants |
| Mémoire | BSS, relocations, permissions, RELRO, pages possédées | Arena borné rebased ; pas d’adressage absolu PS4 arbitraire, TLS ni mémoire GPU |
| CPU | Nouveau CALL/RET réel entre ELF SCE et PRX invité via FEX | Admission limitée ; branches/boucles et instructions générales non exposées |
| Processus | RIP/RSP, pile gardée, création/destruction de thread FEX | argv/envp/auxv Orbis, paramètres processus, init/fini PRX, sorties ABI et signaux |
| Services | Chemin DT standard existant : open/read/close, mmap, horloges, identité thread, mutex bornés | Le nouveau chemin SCE n’installe aucun trampoline HLE ; threads invités exécutables/events complets absents |
| Fichiers | /app0 borné dans le chemin libkernel existant | Ressources réelles absentes ; aucun mount automatique dans le chemin SCE |
| Graphiques | PM4 borné et tests Metal synthétiques existants | ABI video-out/GNM, shaders et pipeline de jeu non raccordés |
| Entrées | Interfaces applicatives existantes | Aucun scePad/tactile relié au processus SCE ; aucun état de jeu testé |
| Audio | Aucun audio de jeu validé | ABI audio-out, buffers, cadence et synchronisation à implémenter |
| iPhone | Import dossier, préparation, tentative FEX, erreurs explicites | Exécution sur appareil réel exige validation séparée ; compilation ne la prouve pas |

Les services existants sont testés avec leurs résultats observables et leurs
vraies erreurs. Aucun nouveau service retournant systématiquement succès n’est
ajouté. Un import SCE manquant donne la cause du linker ; un syscall non admis
donne un refus CPU avec PC/opcode. L’analyse NID HLE n’équivaut pas à son binding.

## Démonstration reproductible

```sh
ASAN_OPTIONS=detect_leaks=0 bash scripts/test-ps4-system-services.sh
ASAN_OPTIONS=detect_leaks=0 bash scripts/test-orbis-runtime.sh
ASAN_OPTIONS=detect_leaks=0 bash scripts/test-orbis-analysis.sh
bash scripts/test-ps4-fex-linked.sh /chemin/aether-epingle build/ps4-linked
```

Les sources Aether/FEX et VIXL sont épinglées dans le script et le workflow
`PS4 system services`. Le même test s’exécute sur Ubuntu x86 avec backend 3 et
sur `ubuntu-24.04-arm` avec backend 2. Le test modifie le MOV de la fonction PRX :
40 → RAX 42 ; 7 → RAX 9. Il vérifie le store 0x1234 dans l’app, les appels répétés,
le refus avant initialisation, les sorties mises à zéro sur échec, le refus de
nouvelle préparation incomplète et la fermeture de chaque moteur. Les tests
libkernel existants vérifient ensuite FEX → service → reprise du CPU, RAX, lecture
de fichier, horloge et mmap. Les tests ASan/UBSan couvrent également BSS
relocalisé, invalid memory, opcode inconnu, terminaison invalide et récursion.

Le script exporte `orbis-bundle/{maxps4.json,app.elf,provider.prx}`. Tous ses
fichiers sont fabriqués par le test, avec NID fictif documenté, et redistribuables.
Le workflow IPA teste aussi l’import Swift réel du dossier sur macOS ARM64,
puis construit l’app ARM64 et l’IPA SideStore. Un succès macOS ne valide pas
iOS réel. Les résultats observés de cette mission figurent dans
[kero-blaster-validation.md](kero-blaster-validation.md).
