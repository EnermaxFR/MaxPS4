# Initialisation contrôlée du dispatcher FEX Darwin

Branche `codex/fexcore-ios-jit-init`, issue de `main` au commit
`2d99a4f4152b260d3588fe6072407bde3685074f` (PR #128 fusionnée).
Fork Darwin AetherPS4 inchangé : `3347fc13f19860855ce91b66356940989ea6b6a6`.
Cette étape initialise réellement le dispatcher sur macOS ARM64. Elle ne crée
pas encore de thread invité et n'exécute pas de code x86 dans FEX.

## Contrat et ressources

L'ABI C opaque de #128 reste compatible. `maxps4_fex_engine_initialize` ajoute
une opération explicite : contexte créé (1), dispatcher initialisé (2).
Un second appel réussit sans remplacer le dispatcher. Le verrou existant
sérialise configuration, initialisation, interrogation du stade et destruction.
La fermeture doit toujours être sérialisée par l'appelant avec toutes les opérations.
Le `SignalDelegator` fourni par FEX lui-même conserve les adresses émises par
le dispatcher : aucun faux gestionnaire de signaux n'est ajouté. Il n'installe
aucun handler OS, ne restaure aucun contexte après faute et ne rend pas
l'exécution invitée sûre. Son stockage précède le contexte et lui survit.
La destruction du contexte libère son dispatcher avant l'arrêt de la configuration.
Le repli existant continue à indiquer `MAXPS4_CPU_RESTRICTED_INTERPRETER`.
Ni le stade 2 ni le résultat 42 de ce repli ne prouvent une exécution FEX JIT.

`InitCore` appelle `Dispatcher::Create`, puis `SignalDelegator::SetConfig`.
Le constructeur du dispatcher écrivait dans le résultat non vérifié de
`VirtualAlloc`. Le patch minimal effectue l'allocation dans la fabrique,
rejette `nullptr` et `MAP_FAILED` avant émission, et retourne `false` depuis
`InitCore` si cette allocation échoue. L'ABI C conserve alors le stade 1 et
renvoie l'errno négatif ou `-ENOMEM`. Un nouvel appel peut réessayer.
Ce traitement ne garantit pas la récupération de toute allocation C++ interne
ni des assertions fatales de FEX. Aucun symbole de remplacement n'est ajouté.
Le patch est vérifié sur les véritables sources épinglées : idempotence,
divergence sans écriture partielle et altération d'un patch appliqué.

## Pourquoi iOS retourne ENOTSUP

Le `VirtualAlloc(..., Execute=true)` de ce fork appelle `iOSJITAlloc`, qui charge
BreakpointJIT et demande un mapping exécutable externe avant de créer son alias RW.
Le pont MaxPS4 ne dispose pas d'un fournisseur approuvé ni d'un contrat de durée
pour cette session. L'initialisation iOS retourne donc `-ENOTSUP` avant `InitCore`,
sans chargement de framework, appel de mapping externe ou tentative de contournement.
Les contextes, CPUID et le repli interprété restent utilisables.
Ce verrou concerne ce chemin FEX, pas les anciennes sondes indépendantes.

Sur macOS, le dispatcher conserve le vrai `MAP_JIT`, la garde
`ScopedJITWriteProtect` et l'invalidation `sys_icache_invalidate` validés auparavant.
Sur iOS, traductions RW/RX, verrou d'écriture, réutilisation et safepoints de
Aether sont conservés ; aucun callback vide n'est installé pour les satisfaire.
Le garde macOS s'applique au thread écrivain. L'alias RW iOS ne justifie pas
un passage arbitraire RWX ni un remplacement identité de RW/RX.
Les tailles de page hôte et invitée doivent rester distinctes (16 Kio/4 Kio).

## Première exécution invitée à préparer

Le futur test FEX, distinct de `maxps4_fex_engine_run`, utilisera uniquement :
`B8 28 00 00 00 05 02 00 00 00 F4` (`mov eax,40; add eax,2; hlt`).
Le HLT est une sortie contrôlée après `EnableExitOnHLT`, pas un RET avec pile
invitée indéfinie. La mémoire des octets invités n'a pas besoin de PROT_EXEC.
Avant d'exposer ce test : fournir un contrat de syscalls rejetant explicitement
les opérations non prises en charge (jamais une réussite factice), limiter
`QueryGuestExecutableRange` à l'allocation invitée réelle, configurer GDT/état
CPU, pile invitée et pile call/ret FEX avec gardes. Ensuite `CreateThread`,
`ExecuteThread`, lire RAX=42 et RIP, puis `DestroyThread` avant de libérer les piles
et le contexte. `CreateThread` initialise le compilateur et le backend ARM64 :
il exige un SyscallHandler réel et ne désigne pas l'interpréteur de MaxPS4.

Aucun `SmokeSyscallHandler` d'Aether n'est copié : il retourne artificiellement
0 pour tout syscall et annonce toute la mémoire comme exécutable. Aucun backend
JIT n'est annoncé par la présente API. L'initialisation du dispatcher est l'étape
intermédiaire vérifiable livrée ici, plutôt qu'une exécution simulée.

## Protocole iPhone physique

1. Appareil ARM64, iOS >=18, Xcode/SDK compatible, identité de signature et profil
   de développement valides, mode développeur, autorisations debugger/JIT réellement
   accordées par la plateforme. `get-task-allow` ou l'attachement d'un debugger
   ne suffisent pas à prouver un droit d'allocation JIT.
2. Définir un fournisseur mémoire conforme aux protections iOS, avec propriété
   des mappings RW/RX, erreurs de refus explicites et arrêt de session synchronisé.
   Ne pas activer le chargeur BreakpointJIT existant pour contourner les protections.
3. Vérifier séparément allocation, écriture via RW, protection, invalidation du
   cache d'instructions, exécution native minimale, résultat et libération.
4. Installer la délégation Darwin réelle pour les fautes/suspensions/arrêts,
   préservation des masques de signaux et contextes ARM64, synchronisation des
   threads et safepoints. Tester les conflits avec le debugger.
5. Ensuite seulement exécuter le bloc x86 borné ci-dessus, avec délai de surveillance
   externe et journaux du backend FEX, RAX/RIP, signal éventuel et nettoyage.
   Répéter après fermeture et reprise de session, puis tester plusieurs threads.

Aucune IPA n'est requise ni générée dans cette mission. Aucun appareil n'a été
utilisé ici. La liaison, la génération du dispatcher et les anciens tests de
traduction ne prouvent ni l'exécution JIT sur iPhone ni celle d'un jeu PS4.

## Validations

Local Linux : 3 tests du nouveau patch, 3 tests des adaptations Darwin et
3 tests des diagnostics réussis ; primitives de masques/signaux et contention
réussies ; ancien binaire de régression du pipeline natif réussi (7 programmes,
ARM64 traité comme données). `bash -n` et `git diff --check` réussis.
L'hôte Linux n'a pas Xcode : les builds macOS/iOS et l'exécution réelle de
`InitCore` sont demandés à la matrice CI existante, sans retirer de tests.
Les résultats CI, commits et artefacts seront ajoutés après exécution.

Blocages prioritaires : fournisseur JIT iOS conforme ; délégation Darwin OS et
safepoints ; SyscallHandler invité borné et piles/contextes ; exécution du bloc
sur hôte puis appareil ; intégration de cette bibliothèque au packaging applicatif.
