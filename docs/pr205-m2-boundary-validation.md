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

## Correction et périmètre

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

## Régressions

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

## Vérifications locales

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

Après validation physique de cet incrément :

1. Définir un slow path scalaire JIT inter-mappings avec prévalidation complète,
   état invité précis et tests de flags/registres ; garder les atomiques split
   refusées. Traiter la reprise après faute native séparément.
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
