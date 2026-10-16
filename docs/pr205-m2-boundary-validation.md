# PR #205 — limites de mappings M2

## Validation physique de b12d7b13

[Rapport communiqué par le testeur](https://github.com/EnermaxFR/MaxPS4/pull/205#issuecomment-6212218789),
source `b12d7b13be92c33d33654b7a9a6df66f9fe4f75d`, iPhone sous iOS
27.2 (24B5110a), le 16 octobre 2026 à 09:14:38 UTC :

- M2 low VA : statut 0, backend JIT ARM64 2, entrée 0x400000,
  JIT 42, mémoire 42, TLS 8.
- Collision, nettoyage, code RX et pile RW/NX : tous à 1.
- Limites mappings : statut 0, refus vérifiés 16/16, mémoire préservée 1.
- ELF/libkernel synthétique : réussi, résultat 2.

Cette preuve physique concerne ces diagnostics bornés sur ce SHA. Elle ne
valide aucun nouveau changement, aucune reprise générale après faute native,
aucun ELF/PRX arbitraire ni jeu PS4. **-48 n'est pas déclaré corrigé globalement.**
Le modèle iPhone et les versions SideStore/StikDebug ne figurent pas dans ce
rapport ; aucune exécution locale sur iPhone n'est revendiquée ici.

## Incrément courant — slow path scalaire

Les opérandes GPR LoadMem/StoreMem et LoadMemTSO/StoreMemTSO de 1/2/4/8 octets
calculent leur adresse effective dans le domaine invité. Une plage unique
conserve un accès natif de la largeur originale. Sinon le JIT vérifie chaque
octet (bornes invitées/hôtes et permissions) **avant** toute lecture, écriture
ou modification de la destination. La seconde passe copie en ordre little-endian
sous la même lease immuable ; aucun pointeur invité n'est déréférencé par l'hôte.
Le snapshot, le stockage et les permissions restent épinglés jusqu'à la fin du
dispatch. Les opérations TSO ont des barrières ISH avant/après l'accès.

Les registres invités d'adresse/source et NZCV sont conservés ; les loads 16 bits
respectent la fusion x86, les loads 32 bits la remise à zéro des bits hauts.
Un refus enregistre adresse/taille/permissions et arrête l'invocation avant
l'opérande. Le frontend ne publie aucun résultat CPU réussi. Il ne s'agit pas
d'une reprise générale à un RIP précis après faute native. Une écriture split
réussie n'est pas atomique entre threads ; **CAS/XADD et toutes les atomiques
split/mal alignées restent refusées**, sans fallback logiciel.

Le budget d'émission compte désormais chaque scalaire et ses deux passes,
avec plafond de 16 Mio et refus avant émission en cas de dépassement. Les
vecteurs/paires et autres accès natifs restent limités à un mapping ; les
opérations bulk/masked/gather/cache/ValidateCode non implémentées restent refusées.
Aucune protection, invalidation, W^X, garde ou activation Stik/BRK n'est affaiblie.

Régressions nouvelles : chaque coupure des largeurs 16/32/64 bits, load/store,
comparaison de tous les octets et sentinelles, registres/flags, destination égale
au registre d'adresse, huit mappings logiques, deux allocations hôtes disjointes,
débordement invité et refus aux trous/protections. Exécution avec TSO activé et
désactivé et LSE/LL-SC. Les tests CAS/XADD 8/16/32/64 existants restent conservés.

Le diagnostic iPhone M2 conserve 42/TLS 8 et le compteur de refus **16/16**.
Avant les refus, il exécute **11 paires store/load scalaires** (toutes les
coupures 16/32/64 bits), vérifie chaque octet et les flags ; un échec empêche
le statut global 0. Log attendu : `M2 scalar cross-mapping: 11/11 store/load pairs; bytes and flags preserved`.
Les 16 refus sont désormais **load/store vectoriels 128 bits + CAS/XADD 64 bits**
× trou/RW/R/PROT_NONE. Ce changement explicite garde les vecteurs split refusés
alors que les scalaires valides deviennent admis. ELF/libkernel reste attendu 2.
La validation physique de b12d7b13 ci-dessus ne valide pas ce nouvel incrément.

## Vérifications locales du slow path

- `ASAN_OPTIONS=detect_leaks=0 CXX=clang++-19 bash scripts/test-orbis-v4-memory.sh` :
  réussi, natif et ASan/UBSan, V4, leases, TLS, M1 et collisions.
- `ASAN_OPTIONS=detect_leaks=0 CXX=clang++-19 bash scripts/test-ps4-system-services.sh` : réussi.
- `scripts/test-ps4-fex-linked.sh` : réussi sur le véritable FEX épinglé,
  Clang 19, Linux x86_64, **backend 3 VIXL**, sans preuve ARM64 native locale.
  Quatre configurations TSO off/on × LSE off/on ; nouvelles régressions,
  CAS/XADD tous formats, ELF/libkernel résultat 2, M1/deux threads et TLS passent.
  M2 répété trois fois : statut 0, entrée 0x400000, RAX/mémoire 42, TLS 8,
  collision/nettoyage 1, 11/11 paires scalaires, 16/16 refus et mémoire préservée 1.
- Patch : application/idempotence et refus de dérive sans mutation réussis.

Les erreurs de développement détectées (réservation de fixture empiétant sur
une garde et type bool du générateur IR à nommer i1) ont été corrigées avant
l'exécution complète réussie. LeakSanitizer désactivé localement sous ptrace ;
ASan/UBSan actifs. La CI native Linux/macOS ARM64, iPhoneOS et l'IPA devront
être vérifiés sur le SHA publié ; les succès b12d7b13 ne leur sont pas attribués.

## Historique b12d7b13 — correction et périmètre

`ReadGuestInstruction` dans le patch FEX copiait les premiers octets avant de
rencontrer un trou ou une plage NX. Il renvoyait false avec un tampon modifié.
La nouvelle prévalidation couvre toute la plage RX avant la copie, refuse les
plages vides, le débordement invité et le débordement de l'adresse hôte. Le
snapshot reste épinglé pendant les deux passes. Une régression utilisant les
véritables en-têtes FEX échoue sur la version 977e1b1 (tampon sentinelle modifié)
et passe avec le correctif.

Les copies contrôlées `GuestAddressSpace::read/write` peuvent déjà traverser
plusieurs régions après validation complète. Le JIT continue à refuser tout
opérande natif hors d'un seul mapping. Aucun accès scalaire JIT inter-mappings
nouveau n'est admis. Les atomiques restent matérielles : aucune décomposition
en lectures/écritures, aucune désactivation des protections. W^X, code invité
RX, pile RW/NX, gardes et distinction VA invitée/hôte sont conservés.

## Régressions b12d7b13

- Fetch RX vers RX dans deux buffers hôtes distincts ; RX vers trou/NX/PROT_NONE :
  refus et tampon intact ; taille nulle et débordements refusés.
- Loads/stores et CAS/XADD 8/16/32/64 bits aux frontières vers un trou, RW,
  lecture seule ou PROT_NONE : faute invitée déterministe avec adresse, taille
  et permissions ; sentinelles intactes.
- CAS/XADD alignés dépassant une région logique courte : refus même si le reste
  de la page hôte existe. Les cas mal alignés restent refusés.
- CAS succès/échec, XADD et les autres régressions atomiques existantes conservés,
  avec LSE et LL/SC. Le simulateur VIXL ne prouve pas la contention matérielle.
- Copie contrôlée traversant deux régions RW, puis relecture exacte.
- Diagnostic M2 embarqué : résultat original 42/TLS 8, puis 16 refus bornés
  (load/store/CAS/XADD 64 bits × trou/RW/R/PROT_NONE). Chaque refus doit donner
  -EFAULT, ne publier aucun résultat CPU et préserver les deux régions accessibles
  ainsi que le marqueur hôte. Les caches sont retirés avant toute remodification.
  Ces refus ne constituent pas une reprise après une faute native.

## Vérifications locales b12d7b13

Linux x86_64 :

- `ASAN_OPTIONS=detect_leaks=0 bash scripts/test-orbis-v4-memory.sh` : réussi,
  natif et ASan/UBSan, V4, low VA, leases, TLS et M1.
- `ASAN_OPTIONS=detect_leaks=0 bash scripts/test-ps4-system-services.sh` : réussi.
- `scripts/test-ps4-fex-linked.sh` avec Clang 19 et les sources épinglées : réussi,
  véritable frontend/JIT FEX exécuté par VIXL, backend **3**, pas ARM64 natif.
  M2 répété trois fois : status=0, entry=0x400000, rax=42, memory=42, tls=8,
  collision=1, cleanup=1, boundaries=16, boundary_status=0, preserved=1.
- Application/idempotence du patch et refus de dérive sans mutation : réussis.

LeakSanitizer refuse de fonctionner sous ptrace dans cet environnement ; seule
sa détection des fuites a été désactivée localement. ASan et UBSan restent actifs.
Un compilateur local a interrompu une compilation lors d'une modification du
fichier de test ; la recompilation du fichier stabilisé et l'exécution complète
ont réussi. Aucun assouplissement de test ni de garde pour ce cas.

Les workflows existants déclenchés par la PR couvrent Linux simulateur, Linux
ARM64, macOS ARM64, compilation iPhoneOS, pont Swift/C, Mach RW/RX et IPA. Leurs
résultats doivent être liés au SHA de ce changement, pas au précédent succès
13/13 sur a3d8da72. Les logs de CI et le compte rendu de livraison font foi pour
ces résultats et l'artefact IPA. Aucun appareil iPhone n'est accessible ici.

## Protocole iPhone

1. Installer l'IPA de ce SHA via SideStore en re-signant app et framework.
2. Activer avec StikDebug, attendre les trois préparations RX et
   `MaxPS4 detach: OK`, revenir dans le même processus sans débogueur attaché.
3. Tester FEXCore : backend natif ARM64 2, résultat 42.
4. Tester M2 low VA deux fois. Attendre statut 0, backend 2, entrée 0x400000,
   JIT/mémoire 42, TLS 8, collision/nettoyage/code RX/pile RW-NX tous à 1.
   Vérifier aussi le log des 11 paires scalaires (voir incrément courant).
   Nouvelle ligne requise : **Limites mappings : statut 0 ; refus vérifiés 16/16 ;
   mémoire préservée 1**. Les logs de refus -EFAULT sont attendus ; aucun signal
   ni crash n'est attendu. Un compteur inférieur à 16 est un échec.
5. Tester ELF/libkernel synthétique : résultat mémoire et RAX 2 ; nettoyage 0
   segments perdus et 0 région RX/RW restante.
6. Partager les rapports et logs, SHA, modèle iPhone, versions iOS, SideStore et
   StikDebug. Une erreur -48 sur un autre scénario reste à diagnostiquer.

La preuve physique sous iOS 27.2 couvre désormais b12d7b13 (rapport ci-dessus).
Toute révision ultérieure reste à valider physiquement ; aucune correction générale de
-48 et aucun jeu PS4 fonctionnel ne sont annoncés. Aucun BRK n'est activé.

## Prochaines modifications concrètes

Après validation physique du slow path scalaire :

1. Traiter séparément la reprise après faute native et la précision de RIP/état
   pour les instructions complexes ; garder les atomiques split refusées.
2. ELF SCE/PRX : étendre les fixtures de relocations/NID et les rejets des tables
   tronquées, puis admettre une relocation supplémentaire avec rollback du
   chargement complet en cas d'import non résolu.
3. libkernel : compléter un service effectivement appelé par une fixture
   invitée, avec traduction des pointeurs, errno, permissions et sorties intactes
   sur échec ; ne pas déclarer un service pris en charge avec un simple stub 0.
4. Mémoire/threads : tests protect/unmap pendant des leases, invalidation de tous
   les caches de threads, isolation TLS et cycle create/join avec nettoyage.
5. GPU : captures bornées de commandes PM4 et ressources invitées sous lease,
   validation des tailles/protections et rejets déterministes avant consommation
   Metal ; ce préalable ne constitue pas un rendu GPU PS4 fonctionnel.

PR maintenue en brouillon, aucune fusion, main inchangée, budget 0 € et uniquement
runners GitHub standard du dépôt public. Aucun changement de facturation.
