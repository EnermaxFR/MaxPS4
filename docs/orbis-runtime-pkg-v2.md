# MaxPS4 Orbis ELF Runtime & PKG Extraction V2

## Périmètre et références

Branche exclusivement expérimentale `experiment/orbis-runtime-pkg-v2`, application
`MaxPS4 Orbis Test`, bundle `com.enermaxfr.maxps4.orbistest`, build `2000001`.
Base exacte : PR #195, commit `7cdfa000fa86ec8adc131489ac9aa51d2248f1d8`, incluant
les corrections PKG de #194 et les recherches #192. PR de cette mission en
brouillon, à ne pas fusionner. #192/#194/#195 conservées ; #193 reste fermée.
Aucune modification de la branche main, aucun jeu/firmware/clé distribué,
aucune dérivation de clés, aucun déchiffrement PFS ni contournement DRM.

Audit complémentaire en lecture seule de main à
`1592de8f4febb492772d105f54527ee49b0097c1` (#191 déjà fusionnée avant cette mission).
La base #195 précède ces corrections JIT : leurs fichiers sont repris dans la
branche expérimentale, sans fusion ni modification de main. Cela conserve
le challenge nonce, la confirmation réelle de StikDebug, l’exclusion d’un second
propriétaire FEX du pool JIT, la taille utilisable du cache après la page de garde
hôte et le nettoyage des allocations partielles par exceptions.
Les fichiers repris correspondent exactement à main pour le runtime/JIT,
Stik, les tests associés et les adaptations FEX/CMake ; les vérificateurs et le
workflow conservent les ajustements nécessaires à l’identité expérimentale.

## Audit : implémentation et preuves distinctes

« Testé localement » signifie exécuté dans cette mission sur Linux x86-64, GCC
14.2, ASan/UBSan, fixtures synthétiques. Cela ne signifie jamais iPhone.
« CI prévue » ne signifie ni compilation réussie ni test exécuté. Les anciennes
notes du dépôt sur `ENOTSUP` permanent ou un bundle stable décrivent d’anciennes
révisions et ne doivent pas être utilisées comme état de cette branche.

| Composant | Fonctions existantes / changement V2 | Preuve et limite |
| --- | --- | --- |
| `PS4PKGInventory`, `PS4PKGBridge` (#194) | CNT, bornes soustractives u64, lectures exactes, representabilité off_t du début et du dernier octet, artwork admis, tailles de sortie atomiques | Suites Linux locales ; sparse artwork traversant 4 Gio. Ce n’est pas un accès à eboot |
| `PS4PFSReadOnly` | PFS D32 mode 8, blocs contigus, uroot récursif, extraction sélectionnée en mémoire ; cycles, chemins, overlaps et budgets refusés | Suites locales et nouveaux tests d’extraction C à >2/>4 Gio ; inode payload au-delà de 4 Gio dans PFS |
| PFS chiffré | Bit 4 : arrêt après 72 octets du superbloc ; errno EACCES à l’extraction | Modes 4/12/13 testés ; pas de lecture des inodes protégés ni de déchiffrement |
| `PS4ExecutableImage` | ELF64 LE x86-64 FreeBSD, ET_SCE_EXEC/DYNEXEC/DYNAMIC, headers/sections, segments, BSS ; V2 refuse W+X dès le staging | Tests locaux valides/corrompus/plain SELF ; SELF reste refusé par linker |
| `OrbisMetadata` | Inventaire SCE, NID qualifiés, versions, DT_NEEDED, TLS, RELA | Testé localement ; reconnaissance HLE distincte d’un binding et d’une admission CPU |
| `OrbisLinker` | Graphe PRX explicite fermé, versions, cycles/ambiguïtés, exports, RELA 0/1/6/7/8, RELRO et transaction | 3 368 contrôles locaux ; pas d’init/fini PRX ; aucune relocation inconnue ignorée |
| `GuestELF::prepare_orbis` | Admission bornée d’instructions/calls acycliques ; copie vers arena mmap, mapping par pages, RW pour copie puis R/RW, BSS zéro | Nouveau ET_SCE_EXEC chargé et 64 cycles de nettoyage locaux. Les octets x86 restent des données pour FEX ; pas de PROT_EXEC hôte pour ces octets |
| `maxps4_fex_runtime` | Vrai Context FEX, InitCore, CreateThread, ExecuteThread, lecture RAX, DestroyThread ; initialise GDT, pile privée, cache ; verrou global | Code inspecté. Nouveau test FEX avec deux valeurs (42 et 9) ajouté à la CI host. **Non exécuté localement dans cette mission** |
| Swift/C | ABI opaque sans pointeurs hôte exposés ; V2 diagnostic sur snapshot immutable, même snapshot pour validation et préparation | ABI C testé localement. Nouveau test Swift/C et compilation app iOS dans CI ; pas de Swift/Xcode sur l’hôte local |
| JIT/StikDebug | Challenge nonce processus, RX préparées par debugger, alias RW Mach, protections/cache/canary, trois régions, propriétaire unique | Test JavaScript et refus sur hôte unsupported locaux ; fonctions Mach uniquement compilables/testables sur macOS/iOS |
| Metal | Fixtures existantes FEX→PM4→Metal et readback offscreen, démo animée synthétique | CI existante conservée ; aucun rendu exécuté dans cette mission sur iPhone. Le nouvel ELF minimal ne produit pas d’image |
| Application / IPA | Nouvelle vue de test et identité distincte ; diagnostics, import et exécution FEX conditionnelle, export expurgé | Packaging ARM64/SideStore à vérifier en CI. Une IPA ne prouve pas le JIT ni une installation sur appareil |

## Risques et garde-fous

- La reconnaissance d’un jeu via SFO, titre ou icône ne valide ni son executable
  ni sa disponibilité. Observation utilisateur Kero Blaster conservée : jeu
  identifié correctement, `eboot.bin` inaccessible en mode `0xd`. Cette mission
  teste cette règle sur une fixture, sans prétendre reproduire le PKG commercial.
- Les PFS signés, comprimés, variantes inconnues, inodes indirects/non contigus,
  liens spéciaux, encodages non admis et fichiers sensibles sont refusés.
  Maximum 4 096 inodes/entrées, profondeur 16, répertoire 1 Mio, inspection
  cumulée 8 Mio, chemins cumulés 1 Mio/individuels 4 096 caractères ; extraction
  sélectionnée 64 Mio. Aucun chemin de source utilisé comme destination hôte.
- Les programmes généraux, TLS, init/fini, tags/relocations inconnus et imports
  non liés restent bloqués. La réussite de l’analyse C signifie analyse complète,
  pas préparation prête ; l’app exige séparément un succès du linker.
- V2 corrige aussi le pont d’analyse fichier : `open` O_NOFOLLOW/O_NONBLOCK,
  fstat fichier régulier, pread avec reprise EINTR, taille/mtime/ctime à la
  nanoseconde avant/après ; FIFO et symlinks refusés sans blocage. Ce contrôle
  n’est pas une garantie d’authenticité cryptographique. Le nouveau pont bytes
  ne relit aucun chemin ; son caller garde les bytes immutables.
- Le linker peut préparer des segments ne partageant pas les mêmes contraintes
  de pages ; le mapping CPU refuse les pages aliasées, la mémoire inaccessible
  ou les offsets hors budget. Il n’élargit pas les droits.
- FEX exécute un sous-ensemble rebased, pas un espace d’adresses Orbis complet.
  L’admission vérifie un flux fini et ses accès ; elle n’est pas un sandbox pour
  du code PS4 général. Les erreurs de validation/liaison/admission retournent
  des statuts sans lancer FEX. La gestion des fautes CPU générales reste à faire.
- L’activation StikDebug, CS_DEBUGGED, le retour dans l’app et la canary ARM64
  ne prouvent pas à eux seuls l’exécution de cet ELF. Il faut un retour réussi,
  backend natif 2, l’entrée attendue et le résultat réel.
- Nettoyage : handles linker temporaires fermés, engine fermé par defer à chaque
  tentative, thread/stack/mapping nettoyés par les propriétaires existants.
  Une erreur InitCore conserve le propriétaire JIT jusqu’à fermeture du moteur.
  Une session Stik/pool préparé peut rester pour réutilisation ; sa libération
  explicite est exposée dans le diagnostic et refuse les leases actives.
- Export : révision source, SHA256 de l’ELF, taille, statuts numériques, backend,
  entrée invitée, RAX et verification de fixture. Aucun URL/nom de fichier,
  bytecode, NID contrôlé par l’entrée, nonce, adresse hôte, clé, profil ou donnée
  utilisateur. Un hash identifie l’ELF ; sa publication reste choisie par l’utilisateur.

## Programme légal et distinction des exécutions

`scripts/create-orbis-test-v2.py` génère 131 octets indépendants de tout SDK Sony :
ELF x86-64, ABI FreeBSD 9, ET_SCE_EXEC `0xfe00`, un PT_LOAD RX (11 octets file,
32 octets mémoire), entrée invitée `0x1000`, aucun import/PRX/TLS/relocation.
Instructions originales `MOV EAX,40 ; ADD EAX,2 ; HLT`, RAX attendu 42.
SHA256 `a80acf5c6b919cae109c01ec135516807bfc2c358c940f6f0d8c0abe3e5f57f0`.

La fixture intégrée Swift contient exactement les mêmes bytes ; la CI compare
son hash. Le test CPU FEX varie aussi la première constante pour attendre 9,
afin de détecter un résultat fixe simulé. Il passe par `prepare_orbis` puis
`initialize` puis `run_orbis`, sans interpréteur de secours. Un appel avant
initialisation est explicitement refusé. Backend 3 signifie VIXL simulator
exécutant les instructions ARM64 générées par FEX sur host x86 : **pas iPhone**.
Backend 2 indique le backend FEX natif ; seul un rapport obtenu sur un appareil
réel établit une preuve iPhone.

Le test `OrbisRuntimeV2Test` local s’arrête au mapping et à l’admission CPU :
il n’appelle ni FEX ni l’interpréteur. Il ne prouve aucune instruction exécutée.
Le test `OrbisV2SwiftBridgeTest` vérifie snapshot, hash, refus W+X et export,
sans exécuter le programme.

## Validation reproductible et obstacles de l’environnement

```sh
bash scripts/test-orbis-v2.sh build/orbis-v2
python3 scripts/create-orbis-test-v2.py build/orbis-v2/orbis-test.elf
python3 scripts/verify-orbis-v2-ipa.py build/MaxPS4-Orbis-Test-SideStore.ipa
```

Sur l’environnement local tracé uniquement :
`ASAN_OPTIONS=detect_leaks=0 bash scripts/test-orbis-v2.sh build/v2`.
LeakSanitizer signale qu’il ne fonctionne pas sous ptrace ; ASan et UBSan restent
actifs. Le runner V2 force l’arrêt sur UBSan et échoue sur toute commande/test
échoué. La CI Linux/macOS conserve les réglages sanitizer normaux, sans cacher
les erreurs. Les logs de compilation et tests sont conservés comme artifacts.

Le workflow `orbis-v2.yml` exécute les suites natives sur ubuntu-latest et
macos-15. `fexcore-app.yml` conserve les tests host réels, ajoute le pont Swift V2,
compile l’application ARM64 iOS complète puis vérifie le Mach-O, les ABI importées,
le framework embarqué, la signature ad hoc et le bundle expérimental fixe.
L’artifact nommé `MaxPS4-Orbis-Test-SideStore` est publié uniquement après succès.
Ni continue-on-error ni création d’une IPA factice.

Cet environnement local est Linux x86-64, sans xcrun, Swift ou Xcode, sans checkout
FEX Darwin ni accès à un iPhone. La compilation/exécution FEX et la construction
IPA doivent donc être établies par leurs jobs macOS et leurs artifacts, puis par
le protocole appareil. Ne pas appeler cela « seulement compilé » tant qu’un job
réussi n’a pas été observé. Les résultats CI/artifacts exacts sont consignés dans
`orbis-v2-validation.md` et les logs locaux dans `orbis-v2-local-tests.txt`.

## Obstacles avant l’exécution de jeux PS4

1. Source autorisée donnant des exécutables, PRX et assets en clair : un PFS
   `0xd` reste inaccessible ; aucune solution de déchiffrement dans ce périmètre.
2. Preuve JIT/FEX obtenue sur iPhone exact, IPA resignée exacte, session StikDebug
   cohérente et protections RX/RW validées. Une compilation ne répond pas à cela.
3. Espace mémoire Orbis général, instructions/adressage x86 non limités à la
   fixture, gestion sûre des faults/signaux et limites CPU robustes.
4. ABI de processus, paramètres SCE, pile/arguments, TLS par thread, ordonnanceur,
   syscalls, lifecycle PRX, init/fini et bibliothèque HLE réellement compatible.
5. Résolution légale de toutes les dépendances/NID et relocations requises,
   sans exports fictifs ni substitution d’un résultat attendu.
6. GPU PS4/commandes/shaders traduits vers Metal, synchronisation, audio, entrées,
   filesystem et ressources d’un titre réel ; les démos synthétiques n’y suffisent pas.
7. Preuve reproductible de l’exécution du titre réel, avec logs et résultat observé.
   Aucun jeu PS4 déclaré fonctionnel par cette mission.
