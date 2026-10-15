# Mission #198 — audit, architecture et jalons de preuve

Base du lot 1 : PR draft #197, branche `experiment/orbis-v3-autotests`,
commit `8b7e0fcda69ecaebe7f1fe5d927c9a216dfc0812`.
Branche de travail : `experiment/orbis-mission-198`. PR séparée ciblant V3.
Aucune modification de main, des PR existantes ni de l'interface stable.

## État constaté dans les sources

| Domaine | Réalité de V3 | Blocage / prochaine preuve |
|---|---|---|
| CPU FEX | Contexte réel, ExecuteThread, registre RAX relu, invalidation du cache, configuration sérialisée | Programme admis et rebasé uniquement ; pas d'adressage x86-64 général |
| Mémoire ELF | PT_LOAD, BSS, translation logique, pages hôte R/RW, trous PROT_NONE, plafond de span 32 Mio, rejet W+X | Adresses absolues invitées non utilisables directement par FEX ; ASLR invité et gestion des fautes à concevoir |
| Pile | Pile CPU privée avec gardes, registres initialisés, argc/argv/envp/auxv minimaux | Pointeurs de pile et AT_ENTRY rebasés ; pas de contrat de processus Orbis général |
| Mémoire dynamique | mmap/munmap/mprotect anonymes logiques, pages invitées 16 Kio, budget 32 Mio, W^X | Stockage vectoriel et permissions logiques ; pas de mapping CPU général ni de protection hôte par page |
| TLS | Métadonnées détectées | Exécution explicitement refusée ; pas de FS/GS/TLS par thread Orbis |
| Threads | Identités bornées, mutex trylock/unlock et ownership testables | Pas de création de threads invités avec dispatch indépendant ni de scheduler Orbis |
| ELF/SELF | ELF x86-64 et SELF plaintext validés et stagés | SELF protégé refusé ; PRX n'est pas une entrée de processus |
| Linker | OrbisLinker : inventaire NID, dépendances, relocations vers image logique ; prepare_orbis : appels GOT acycliques réécrits en CALL relatif sur copie privée | Sous-ensemble de relocations ; TLS et init/fini généraux manquants |
| ABI/HLE | PS4DynamicLinker distinct : thunks bornés, services libkernel connus, syscall gate vérifiée ; mémoire, temps, lecture /app0, pthreadSelf, mutex | Ne pas confondre ce chemin avec le linker PRX ; pas de binding inventé ni de syscall hôte direct |
| PKG/PFS | Lectures bornées, extraction plaintext sélectionnée, offsets larges, chemins confinés | PFS chiffré refusé ; aucune décryption ni compatibilité générale de package |
| GPU/Metal | Décodage PM4 et pipeline Metal synthétiques, transfert de pixels/entrées expérimental | Pas de traduction GNM/GNMX ni de shaders PS4 générale |
| Audio/entrées | Entrées synthétiques et boucle expérimentale présentes | Audio Orbis et cycle de vie interactif d'un homebrew externe non établis |
| iPhone | Build expérimental séparé et parcours JIT gardé | Rapport utilisateur 42/9, native_fex=true, jit=0 selon #198 : preuve synthétique sur appareil, pas preuve JIT ni jeu |

## Audit des PR demandées

- #192 : recherche sur extracteur PKG 1.1, `1e3ca13e05684b6d587fde187c45e5b12ddadad8`.
  fseeko64 seul ne prouve pas la sécurité du parser ; fixtures sparse et risques de bornes documentés.
- #194 : durcissement off_t/pread et EINTR, `28b00e6c9db8791c1ea9505f250f6db40af0b751`.
- #196 : runtime V2 isolé, extraction plaintext et bridge Swift/C++,
  `a82552ef17c8a9a50cf8fe3329cc00ec6387177c`.
- #197 : V3, état CPU/pile et répétitions FEX 42/9. Les succès de CI cités
  dans cette PR appartiennent à V3 ; ils ne valident pas les nouveaux commits.

Les deux linkers ont des responsabilités distinctes. Leur fusion sans contrat
commun de mapping, symboles et ownership introduirait une fausse compatibilité.

## Architecture minimale de bout en bout

Source légale non chiffrée -> snapshot immutable -> validation ELF/PRX ->
résolution déterministe des modules/NID/relocations -> espace mémoire invité ->
contexte CPU et TLS par thread -> dispatch FEX -> syscall gate ABI Orbis ->
services HLE -> soumission GPU/audio/entrées -> présentation Metal.

Chaque transition doit publier soit un objet complet détenu par le runtime,
soit une erreur précise avec rollback. Ne pas exposer les pointeurs hôte dans
les rapports C/Swift. L'adressage invité logique reste la référence, les
adresses hôte ne servent qu'au backend actuellement restreint.

Ordre de développement : définir un backend mémoire réellement compatible
avec les accès générés FEX avant d'admettre des pointeurs absolus, TLS ou du
code arbitraire. Les APIs logiques HLE existantes ne suffisent pas à cela.
Préserver l'admission bornée jusqu'à disposer d'une gestion des fautes et
d'un budget d'exécution qui interrompe effectivement le CPU.

## Lot 1 : protections aux limites de l'arène

La réservation commune ELF/staging/Orbis inclut maintenant une page hôte
PROT_NONE avant et après le span invité. Les offsets relatifs entre segments,
le plafond invité de 32 Mio, les permissions R/RW et les trous sont conservés.
Les gardes coûtent deux pages hôte supplémentaires. clear, destruction et
remplacement échoué libèrent la réservation entière, gardes comprises.

GuestArenaGuardTest provoque de vraies fautes de lecture/écriture en processus
enfant sur les deux gardes et les trous ; il vérifie l'absence des mappings
après nettoyage. Couverture : ELF, ELF SCE, SELF plaintext, Orbis lié,
remplacement échoué et répétitions. La suite conserve les régressions RX/NX,
BSS, adressage relatif, rejet d'instructions et rollback.

Commande : `bash scripts/test-orbis-mission-198.sh`.
Résultat local : 7 suites réussies, GCC Linux x86-64 avec ASan/UBSan.
LeakSanitizer local désactivé car environnement tracé ; aucun désarmement en CI.
Ce lot ne constitue ni une exécution CPU FEX locale ni le jalon M1.

## Jalons et critères d'acceptation

| Jalon | Critère | État à la création du lot 1 |
|---|---|---|
| M1 | Homebrew légal : pile, mémoire, TLS, appels système, résultats CPU réels | Non atteint : TLS et adressage général bloquent |
| M2 | ELF/PRX avec imports NID, relocations et dispatch vérifiés | Partiel synthétique ; homebrew externe non validé |
| M3 | Homebrew légal dessinant via Metal, commandes réellement traduites | Non atteint ; pipeline synthétique existant |
| M4 | Audio, commandes, boucle interactive et cycle de vie stables | Non atteint |
| M5 | Homebrew/jeu légal interactif sur iPhone avec rapport appareil | Non atteint ; aucun appareil attaché ici |
| M6 | Profils CPU/GPU/mémoire et compatibilité ; intégration UI avec accord ultérieur | Différé |

Prochain lot précis : contrat backend mémoire FEX et fixture légale de
pointeurs absolus, pile et TLS ; d'abord prouver le mapping et le rollback,
puis le dispatch sur Linux ARM64/macOS ARM64. Aucun élargissement de whitelist
ne doit masquer l'absence d'isolation, de gestion des fautes ou de TLS.

## CI, artefacts et limites de cette session

- Nouveau workflow Orbis mission 198 : Linux/macOS ASan/UBSan et rapports.
- Workflow V2 conservé et activé pour la nouvelle branche : PKG/PFS/linker.
- Workflow FEXCore app conservé et activé : FEX réel macOS, Swift/C++, iOS,
  Metal et IPA distincte `MaxPS4-Orbis-Mission-198-Test-SideStore`.
- Les workflows PR existants continuent à tester le code natif et les ABI.
- Le proxy shell local est injoignable : fichiers récupérés et publication
  via connecteur GitHub au SHA exact. Snapshot local partiel, pas clone complet.
- Dépendances FEX non disponibles localement ; aucun SDK iOS ni appareil
  attaché. Les runners fournissent les preuves FEX/build ; l'iPhone fournit
  ultérieurement la preuve matérielle, uniquement aux jalons nécessaires.

Les liens CI et IPA du nouveau commit doivent être ajoutés après lecture des
résultats. Une compilation ou un test synthétique réussi ne prouve jamais
qu'un jeu PS4 démarre ou est jouable.

## Preuves du lot 1 corrigé

Code validé : `f5f0c6b82c49eaa6bca1373453ad2f31c839c681`.
Les workflows PR testent le merge ref `2ec3433` avec la base V3 inchangée.

| Vérification | Preuve |
|---|---|
| Gardes et sept suites Linux/macOS ASan/UBSan | [Succès push](https://github.com/EnermaxFR/MaxPS4/actions/runs/38570199330), [succès PR](https://github.com/EnermaxFR/MaxPS4/actions/runs/38570205629) |
| PKG/PFS plaintext, offsets sparse, linker et bridge | [Succès Linux/macOS](https://github.com/EnermaxFR/MaxPS4/actions/runs/38570199324) |
| CPU FEX ARM64 Linux, VIXL sur x86, services et compilation iPhone | [Succès](https://github.com/EnermaxFR/MaxPS4/actions/runs/38570205678) |
| FEX macOS ARM64, bridge Swift/C++, Metal, app iOS et IPA | [Succès](https://github.com/EnermaxFR/MaxPS4/actions/runs/38570199431) |
| Inventaire PKG, ELF plaintext, Orbis/PRX, ranges, PM4, interpréteur sans JIT | Suites PR réussies au commit de code |
| Linkage FEX strict iOS et hôte | [Succès](https://github.com/EnermaxFR/MaxPS4/actions/runs/38570205586) |
| Compilations lourdes FEX/HLE et smoke Swift/iOS | [Heavy réussi](https://github.com/EnermaxFR/MaxPS4/actions/runs/38570205714), [smoke réussi](https://github.com/EnermaxFR/MaxPS4/actions/runs/38570205720) |

Pour chaque backend CPU, les logs contiennent 48 exécutions réelles :
24 résultats 42 et 24 résultats 9, pile/registres vérifiés. Backend 2 pour
Linux/macOS ARM64 natifs, backend 3 pour VIXL ARM64 sur x86. Ce sont des
fixtures synthétiques conservées de V3, pas un nouveau homebrew externe.

Le [premier run macOS](https://github.com/EnermaxFR/MaxPS4/actions/runs/38569982036)
a échoué sur une hypothèse erronée du test : première page suivant le code
supposée vide. Avec les pages hôte de 16 Kio, elle peut couvrir un segment
de données. Le correctif f5f0c6b cherche un vrai trou entre pages de segments.
Les contrôles de faute sur les deux gardes restent actifs. Aucun échec masqué.

**IPA expérimentale** :
[MaxPS4-Orbis-Mission-198-Test-SideStore](https://github.com/EnermaxFR/MaxPS4/actions/runs/38570199431/artifacts/11897497311).
[Diagnostics complets](https://github.com/EnermaxFR/MaxPS4/actions/runs/38570199431/artifacts/11897652044).
Rétention annoncée jusqu'au 28 octobre 2026.

L'archive a été téléchargée et contrôlée dans cette session. Digest ZIP :
`0068231823f3289a9dea96025b03a774f4d359a5f93d5f3cdb6719bba42298f9`.
SHA256 de l'IPA extraite :
`863ac9de7a1658489a0c3800539fda3112e75c001b668767096cad10f5053de9`.
SourceRevision f5f0c6b, bundle `com.enermaxfr.maxps4.orbistest`, Mach-O iOS
ARM64 et ABI app/runtime validés par `verify-orbis-v2-ipa.py`.
SideStore doit re-signer l'app et le framework. Aucune installation sur
appareil ni preuve d'exécution iPhone supplémentaire n'est fournie.

[Extraits CPU/mémoire/IPA](orbis-mission-198-ci-excerpts.txt) et
[résultats locaux](orbis-mission-198-local-tests.txt).
