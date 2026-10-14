# Orbis V3 — validation et limites

Branche `experiment/orbis-v3-autotests`, PR #197 en brouillon, base V2 #196.

## Diagnostic iPhone

Le message « Backend d’espace d’adresses FEX Orbis, pile/TLS et ABI requis »
indique un parcours de préparation sans admission/dispatch CPU. Le chargement
ou la compilation iOS ne démontre aucune exécution. Le parcours de staging
conserve le bit de blocage 4 pour l’adressage PS4 général ; il ne faut pas le
supprimer pour autoriser arbitrairement un jeu.

Le parcours Orbis Test réutilise `prepare_orbis` puis `executeImage` du même
moteur FEX. Il vérifie les instructions avant tout dispatch. Les programmes
bornés et les accès RIP-relatifs admis utilisent un seul arena rebasé ; cela
préserve leurs déplacements sans prétendre mapper tout l’espace PS4.

## Développement V3

- Pile privée entre deux pages de garde, RW et sans permission d’exécution.
- RSP aligné à 16 octets ; argc=0, argv/envp nuls, AT_PAGESZ, AT_ENTRY et AT_NULL.
  ABI synthétique minimale : aucun argument, environnement hôte ou TLS exposé.
- Registres entiers nuls sauf RSP, RIP issu du segment d’entrée validé.
- État initial relu depuis le frame FEX et la pile ; résultat RAX relu après
  `ExecuteThread`. Le rapport C ne publie aucun pointeur hôte.
- 48 exécutions alternant MOV/ADD/HLT donnant 42 et 9 dans trois contextes.
  Vérification des permissions, refus après préparation incompatible et
  invalidation des anciennes traductions quand les mappings sont réutilisés.
- Autotests Swift : 42/9/42/9, état CPU et corruption ; ajout au rapport exportable.
- CI existante conservée, activée pour V3 ; IPA nommée séparément.

## Vérifications locales

La suite `scripts/test-orbis-v2.sh /tmp/maxps4-v3-tests` est passée avec
ASan/UBSan, dont extraction CNT/PFS plaintext, sparse >4 Gio, 3368 vérifications
du linker, fichiers corrompus, mapping/BSS, 64 nettoyages et pile minimale.

Commande locale : `ASAN_OPTIONS=detect_leaks=0 bash scripts/test-orbis-v2.sh
/tmp/maxps4-v3-tests`. LeakSanitizer refuse l’exécuteur sous ptrace ; aucun
contrôle n’est désactivé dans les workflows GitHub Actions. Cette vérification
locale ne compile ni n’exécute FEX : le proxy local est injoignable et le checkout
FEX épinglé n’est pas disponible. Sources récupérées via le connecteur GitHub.
Le test d’intégration C++ passe aussi une vérification syntaxique locale.

## Preuves CI

Première validation du changement :
- Safety ASan/UBSan : https://github.com/EnermaxFR/MaxPS4/actions/runs/38565794304
- FEX macOS, pont Swift, iOS et IPA : https://github.com/EnermaxFR/MaxPS4/actions/runs/38565794229

Au commit `5cc0ff105a533da6fcdabeff6f5a80f0b19cb82d` :

- Safety Ubuntu et macOS : succès, ASan/UBSan.
- FEX Linux ARM64 natif : 48 observations CPU, alternant RAX=42 et RAX=9,
  backend=2, entry=0x1000, pile/registres vérifiés.
- FEX VIXL sur x86 : 48 observations identiques, backend=3 (simulateur).
- Jobs et preuves : https://github.com/EnermaxFR/MaxPS4/actions/runs/38565794272
  (ARM64 job 115858003702, simulateur job 115858003611).
- macOS ARM64 : 48 exécutions natives FEX et quatre exécutions via Swift
  (42/9/42/9, fixture_verified=true et cpu_state_verified=true).
- Compilation complète iOS ARM64 et packaging SideStore : succès.
- Workflow complet : https://github.com/EnermaxFR/MaxPS4/actions/runs/38565788460
- IPA distincte : https://github.com/EnermaxFR/MaxPS4/actions/runs/38565788460/artifacts/11895596608
  Nom : MaxPS4-Orbis-V3-Test-SideStore, ZIP 12 649 854 octets.
  Digest ZIP : sha256:783462ea2a60b66f9f67465829a8c76066123fec796b68cfa478da617c447d3c.
  Rétention jusqu’au 28 octobre 2026 (GitHub peut appliquer ses règles de rétention).
- Logs/rapports : https://github.com/EnermaxFR/MaxPS4/actions/runs/38565788460/artifacts/11895511900
- Linkage FEX iOS et hôte strict : https://github.com/EnermaxFR/MaxPS4/actions/runs/38565794307
  deux jobs passés. Aucun symbole de remplacement ni second moteur ajouté.

Extraits ARM64 natif, relus dans le log du job :
```
PASS V3 ET_SCE_EXEC real FEX restart=0 repeat=0 result=42 backend=2 entry=0x1000 stack/regs=verified (host evidence only)
PASS V3 ET_SCE_EXEC real FEX restart=0 repeat=1 result=9 backend=2 entry=0x1000 stack/regs=verified (host evidence only)
PASS V3 ET_SCE_EXEC real FEX restart=2 repeat=15 result=9 backend=2 entry=0x1000 stack/regs=verified (host evidence only)
```
Ces valeurs proviennent des instructions MOV/ADD/HLT exécutées par FEX et du
frame CPU relu ; aucun résultat n’est assigné par le harness.

## Blocages restants

Adressage absolu PS4 général, TLS Orbis, ABI SCE complète et programmes hors
admission restent refusés. L’ABI minimale n’initialise pas le runtime commercial
PS4. JIT/StikDebug doit être activé sur iPhone ; seul un rapport produit sur
l’appareil constitue une preuve d’exécution iPhone. Aucun jeu PS4 déclaré
fonctionnel. Aucun déchiffrement de PKG/PFS protégés.
