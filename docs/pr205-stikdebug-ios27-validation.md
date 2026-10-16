# PR #205 — StikDebug, iOS 27.2 et validation M2

## Diagnostic et limites

Le diagnostic initial communiqué par l’utilisateur montrait CS_DEBUGGED actif,
MAP_JIT refusé avec errno 1, P_TRACED absent et un alias RW/RW de 16 384 octets.
RX et l’exécution ARM64 n’avaient pas été testés dans ce diagnostic initial. Il ne mesure pas la mailbox du
fournisseur FEXCore : aucune cause unique d’échec StikDebug n’est établie sans
les logs d’activation. Le rapport physique ultérieur ci-dessous valide uniquement les cas bornés indiqués ; aucune correction générale de -48 n’est annoncée.

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
Compatibilité de source vérifiée ; le rapport physique ci-dessous confirme les
résultats des diagnostics bornés sous iOS 27.2, sans fournir les logs détaillés
d’attachement ni les versions installées de StikDebug et SideStore. Aucun framework StikJIT/helper n’est embarqué.

## Mémoire FEX et -48

Le backend M1 traduit déjà les adresses invitées en stockage hôte distinct.
Les snapshots de plages et permissions sont épinglés pendant compilation et
dispatch ; les caches sont retirés avant destruction des mappings. Aucun
MAP_FIXED écrasant une réservation n’est ajouté. Sur Darwin, -48 correspond à
-EADDRINUSE dans ce domaine POSIX ; il faut conserver l’opération et le domaine
d’erreur exacts, pas interpréter un code Mach comme errno. M2 utilise RIP invité
0x400000, un marqueur hôte occupé, pile privée, TLS et XADD/load/store.
Ses résultats attendus ne prouvent pas à eux seuls la correction de tout ELF.

## Validation physique rapportée — 16 octobre 2026

Source testée : `a3d8da72fcc961aed470d9f20939067d9baafea6`, build principal
1000176. iPhone sous **iOS 27.2 (24B5110a)** ; horodatage du rapport :
**2026-10-16 08:19:40 UTC**. Preuve communiquée par l’utilisateur dans
[le commentaire 6211253159](https://github.com/EnermaxFR/MaxPS4/pull/205#issuecomment-6211253159),
non reproduite par l’agent.

- ELF/libkernel synthétique v1 : ouverture FEX, chargement, initialisation JIT,
  exécution, lecture et vérification tous au statut 0 ; backend natif FEXCore
  ARM64 2 ; `scePthreadSelf` appelé une fois ; mémoire résultat invitée 2 et RAX 2.
  Après nettoyage : 0 segment invité perdu et 0 région RX/RW restante.
- M2, adresse invitée basse `0x400000` : statut 0, backend 2, RAX 42,
  mémoire invitée 42, TLS 8.
- Marqueur de collision hôte préservé=1, nettoyage=1, code RX=1,
  pile RW/NX=1.
- Aucun -48 observé dans ce diagnostic borné sur appareil. Cela valide le cas
  low-VA testé, sans prouver que toutes les dispositions mémoire PS4 ni tous
  les scénarios -48 sont corrigés.

Le modèle iPhone, les versions SideStore/StikDebug, les logs d’activation et
une seconde exécution M2 ne sont pas précisés dans ce rapport ; ne pas les
déduire des résultats. Cette preuve concerne le SHA testé ci-dessus, pas une
nouvelle révision du code natif.

Les opérations traduites non prises en charge (split/bulk/masked/gather/cache/
ValidateCode), le redémarrage général après faute native et l’exécution arbitraire
de PS4 ELF/PRX restent non validés et refusés par les gardes existantes.
GPU et exécution de jeux PS4 restent non validés. Aucun BRK autorisé.

## Prochain incrément minimal recommandé

Commencer par un diagnostic synthétique borné des accès scalaires traversant
deux mappings invités adjacents, avant les atomiques inter-mappings ou la
reprise après faute native. Premier incrément : tests de refus uniquement,
sans élargir les opérations admises ni modifier les gardes de production.
Tester lecture/écriture à la frontière, deuxième plage absente ou sans
permission, et débordement d’adresse ; vérifier refus déterministe avant tout
accès hôte, absence d’écriture partielle, marqueurs hôtes préservés et nettoyage.
Un test négatif refusé ne constitue pas une exécution traduite réussie.

L’admission ultérieure d’un accès scalaire traversant deux plages nécessitera
une validation complète des deux plages et permissions avant effet de bord,
des tests Linux/macOS ARM64 et un nouveau rapport iPhone lié au SHA.
Conserver le refus des atomiques inter-mappings : ne pas les simuler par deux
accès non atomiques. La reprise après faute reste un incrément distinct, après
définition et validation de l’état invité récupérable.

## Manipulations exactes sur l’iPhone (protocole de reproduction)

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
