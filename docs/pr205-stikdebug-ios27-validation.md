# PR #205 — StikDebug, iOS 27.2 et validation M2

## Diagnostic et limites

Le diagnostic réel communiqué par l’utilisateur montre CS_DEBUGGED actif,
MAP_JIT refusé avec errno 1, P_TRACED absent et un alias RW/RW de 16 384 octets.
RX et l’exécution ARM64 n’ont pas été testés. Il ne mesure pas la mailbox du
fournisseur FEXCore : aucune cause unique d’échec StikDebug n’est établie sans
les logs d’activation. Aucun succès iPhone, ni correction de -48, n’est annoncé.

Le parcours FEX utilise `maxps4_jit_begin` → URL avec PID/bundle et script fixe
`maxps4-stik.js` → allocations debugserver RX → préparation des pages → publication
nonce → détachement → vérification Mach des alias RW/RX → canari ARM64 42 → FEX.
MAP_JIT local, la signature et un attachement seul ne remplacent aucune étape.
L’ancien précontrôle du laboratoire ne démarre pas ce parcours.

## Corrections

- Les exceptions du script sont propagées après nettoyage/détachement, au lieu
  d’être absorbées et de permettre une fin normale trompeuse côté StikDebug.
- La mailbox conserve un statut numérique d’échec : `0x100 + étape*16 + région`.
  Étapes : 1 attach, 2 challenge, 3 allocation/plage RX, 4 préparation RX,
  5 publication. Régions 0 dispatcher (16 Kio), 1 cache (16 Mio), 2 canari
  (une page), 15 sans objet. Une erreur avant la validation du challenge ne peut
  pas être écrite dans une mailbox non authentifiée : conserver les logs externes.
- Refus des paquets erreur `Ehh` et des régions débordant la plage de neuf chiffres
  hexadécimaux des page writers amont. Une allocation valide rejetée pour sa plage
  est libérée ; aucune préparation à une adresse tronquée n’est envoyée.
- Avant toute préparation native/canari, sysctl doit confirmer P_TRACED absent.
  Encore attaché : `-EBUSY`, session conservée pour réessayer après détachement.
  Erreur sysctl : refus conservateur ; aucun canari. Aucun BRK ajouté/autorisé.
- Bouton **Tester M2 low VA — JIT 42 / TLS 8** indépendant et rapport partageable.
  Aucun interpréteur. Les étapes non tentées du rapport natif valent -ECANCELED,
  et non un zéro qui ressemblerait à un succès.
- Build principal 1000176 ; la révision exacte reste inscrite dans l’IPA.

## Contrats amont examinés

StikDebug [1256c52803f63f8c993a8fcc25574c5f4ea6f7d1](https://github.com/StikDebug/StikDebug/tree/1256c52803f63f8c993a8fcc25574c5f4ea6f7d1) :
`Views/HomeView.swift`, `JSSupport/RunJSView.swift`, `JSSupport/JSDebugSupport.swift`,
`Support/ProcessInfo+TXM.swift`.
StikJIT [32287268fa5824f9edce4cb359f5833ce0cf7b00](https://github.com/StikDebug/StikJIT/tree/32287268fa5824f9edce4cb359f5833ce0cf7b00) :
`INTEGRATION.md`, `Resources/universal.js`, `Sources/ScriptRunner.swift`.

Le script fixe emploie les mêmes primitives `vAttach`, `_Msize,rx`,
`prepare_memory_region` et `D`, sans reprendre la boucle BRK Universal JIT.
L’URL base64url est décodée par HomeView et conserve le PID courant. La version
amont examinée classe les iPhone iOS 27 comme TXM ; une version installée plus
ancienne peut décider différemment et ignorer le script. Les fonctions amont
renvoient parfois OK sans inspecter les paquets de réponse de chaque page : le
nonce prouve uniquement la publication, jamais les droits ou l’exécution.
Le canari et les contrôles Mach demeurent obligatoires. Un debugserver ordinaire
ou le script `universal.js` seul ne publie pas la mailbox spécifique MaxPS4.
Compatibilité de source vérifiée, compatibilité réelle avec la version installée
et iOS 27.2 encore à mesurer. Aucun framework StikJIT/helper n’est embarqué.

## Mémoire FEX et -48

Le backend M1 traduit déjà les adresses invitées en stockage hôte distinct.
Les snapshots de plages et permissions sont épinglés pendant compilation et
dispatch ; les caches sont retirés avant destruction des mappings. Aucun
MAP_FIXED écrasant une réservation n’est ajouté. Sur Darwin, -48 correspond à
-EADDRINUSE dans ce domaine POSIX ; il faut conserver l’opération et le domaine
d’erreur exacts, pas interpréter un code Mach comme errno. M2 utilise RIP invité
0x400000, un marqueur hôte occupé, pile privée, TLS et XADD/load/store.
Ses résultats attendus ne prouvent pas à eux seuls la correction de tout ELF.

## Manipulations exactes sur l’iPhone

1. Installer la nouvelle IPA issue du workflow **MaxPS4 FEXCore SideStore IPA**
   via SideStore. Re-signer l’app ET son framework. Activer le mode développeur,
   utiliser le pairing et le VPN configurés selon StikDebug. Conserver la version
   de StikDebug/SideStore, le modèle, la version/build iOS et les logs de signature
   get-task-allow de l’installation ; ne jamais partager le fichier de pairing.
2. Ouvrir MaxPS4 → **Diagnostic FEXCore** → **Activer avec StikDebug**. Ne pas
   démarrer Universal JIT séparément : l’URL fournit le script fixe MaxPS4.
3. Dans StikDebug, conserver les logs de cette demande : PID, attach, les trois
   lignes `MaxPS4 prepared RX region`, puis `MaxPS4 detach: OK`. En cas d’erreur,
   copier la réponse exacte. Ne pas démarrer de test BRK. Si le script est ignoré,
   relever la version et la détection TXM ; utiliser une version officielle qui
   prend en charge iOS 27.2, sans modifier les protections du système.
4. Revenir dans la même session MaxPS4 après détachement. Attendre la confirmation
   du nonce. **Tester FEXCore** doit donner initialisation 0, exécution JIT 0,
   backend FEXCore JIT ARM64 et 42. Un résultat interpréteur 42 ne compte pas.
   `-EBUSY` avec session en attente : attendre detach OK et réessayer sans nouvelle
   activation. Nonce absent : arrêter les essais d’exécution et fournir les logs.
5. **Tester M2 low VA — JIT 42 / TLS 8**, puis **Partager le rapport M2**.
   Attendus : statut 0, initialisation/exécution 0, backend 2, entrée 0x400000,
   JIT/mémoire 42, TLS 8 ; collision/nettoyage/code RX/pile RW-NX tous à 1.
   Refaire M2 une seconde fois, puis **Tester ELF PS4 / libkernel** (résultat 2).
6. Partager ces rapports, la révision source et les logs StikDebug. Ensuite
   **Libérer la mémoire JIT**. Pour une nouvelle activation, reprendre l’étape 2.
   Après une demande bloquée sans publication, terminer StikDebug puis redémarrer
   MaxPS4 : ne pas annuler de force une mailbox potentiellement utilisée.

## Validation reproductible

Tests locaux : script JS réel avec transport simulé (publication, exceptions,
nettoyage, erreurs/plages), ABI hôte sans fournisseur, suites mémoire V4/low VA/
traduction/TLS et M1 natives. GCC disponible ; Xcode, Swift et FEX ARM64 absents
localement. Le proxy shell est inaccessible ; les sources sont lues/publiées via
le connecteur GitHub, à partir du SHA 70c36829b9222bddb96bbf73eb4be78525611428.
LeakSanitizer local échoue sous ptrace ; la reprise ASan/UBSan désactive uniquement
la détection des fuites. Les vérifications CI complètes restent inchangées.
Les tests Swift/C natifs et Mach sont étendus dans les workflows existants.
Les liens et résultats CI de la nouvelle révision sont consignés dans la PR.
Dépôt public et runners standard GitHub uniquement, aucun service payant.
