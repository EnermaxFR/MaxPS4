# Mission #198 — lot 2 : adressage direct et TLS statique

## Changement livré

L'ancien chemin FEX recalait RIP et RSP sur des pointeurs hôtes. Le nouveau
chemin expérimental `maxps4_fex_engine_run_m1` utilise les adresses x86-64
invitées pour RIP, RSP, FS et les pointeurs absolus. Il conserve les anciens
chemins et n'est pas intégré à l'interface utilisateur.

`GuestAddressSpace` distingue adresse logique et stockage hôte. Son backend
traduit sert aux tests et aux accès contrôlés. Le backend direct FEX réserve
la même plage numérique avec un simple indice mmap sous Linux, vérifie l'adresse obtenue
et refuse une collision sans écraser de mapping existant (aucun MAP_FIXED).
Darwin utilise les API publiques vm_map (vm_allocate/vm_protect), une
allocation fixe sans VM_FLAGS_OVERWRITE et le même contrôle de collision.
Les plages réservées par le kernel restent indisponibles, notamment le
carve-out GPU macOS ARM64 à 64..448 Gio. Les fixtures placent image, pile,
heap et TLS à partir de 48 Tio, une plage disponible sur les hôtes CI.
Le SDK iPhone interdit mach_vm.h ; seuls vm_allocate/vm_protect/vm_deallocate
sont retenus, sans déclaration ni appel d’API privée.
Réservations avec deux gardes PROT_NONE, mappings multi-segments, trous
inaccessibles, protections logiques R/W/X et hôtes sans exécution des octets
x86, alignement selon les pages 4/16 Kio, bornes, budget, chevauchements,
rollback et libération sont contrôlés.

`GuestTLSTemplate` valide un PT_TLS statique contenu dans un PT_LOAD lisible,
copie l'image initiale et crée une zone zéro. `GuestThreadMemory` possède
une pile gardée et un bloc TLS indépendant par thread, avec FS pointant sur
un TCB variant-II (self et DTV minimal). Le thread FEX reçoit `fs_cached`.
Les destructions libèrent pile et TLS. Le heap initial est une réservation
RW gardée de 64 Kio. L'état d'entrée contient argc/argv/envp/auxv minimal,
AT_ENTRY logique et RSP aligné sur 16 octets.

Le loader plaintext et le linker existants restent utilisés. Le mode de
linkage M1 conserve les pointeurs GOT logiques et ne réécrit pas les CALL.
L'admission M1 prouve ensuite un programme fini avant tout dispatch FEX.
Les thunks utilisent le résolveur libkernel existant, notamment le NID
`aI+OeCz8xrQ` de scePthreadSelf et sceKernelClockGettime.

## Fixtures et validation

`OrbisM1Fixture.h` produit deux vrais ELF plaintext : segments RX/RW,
PT_TLS initialisé et zéro, adressage absolu A1/A3, accès via RBX, FS négatif,
PUSH/POP, accès au heap et, dans la variante liée, CALL/GOT/RET avec deux
services libkernel. Aucun résultat attendu n'est assigné par le runtime.
Le test FEX varie l'image TLS (17, 83, 129) et vérifie RAX, les stores mémoire,
le heap, le TLS modifié, la zone zéro, le retour kernel et le nombre de calls.
Il répète chaque exécution pour vérifier réinitialisation et libération.

Tests locaux : les sept suites du lot 1, OrbisM1MemoryTest et les quatorze
suites de services/régression existantes passent sous ASan/UBSan.
OrbisM1MemoryTest passe également sans sanitizers. LeakSanitizer est désactivé
uniquement dans cet exécuteur tracé;
les workflows conservent leurs contrôles. Test supplémentaire de faute réelle
sur gardes/trou, collision hôte conservant le contenu, protections, TLS de
deux threads distincts et refus d'instructions/budget/TLS malformé.

Les 19 runs du code `659cafd` sont SUCCESS (18 validations et un nettoyage de CI obsolète).
Les derniers commits de rapport ne changent pas ce code.

CI du code `659cafd` : [mémoire Linux/macOS](https://github.com/EnermaxFR/MaxPS4/actions/runs/38582054391)
et [exécution FEX Linux ARM64 natif / VIXL](https://github.com/EnermaxFR/MaxPS4/actions/runs/38582054410)
passent dans leurs jobs respectifs. Le test M1 FEX macOS passe dans le [workflow application](https://github.com/EnermaxFR/MaxPS4/actions/runs/38582054416).
Compilation/linkage strict iOS et packaging réussis au même code :
[linkage complet](https://github.com/EnermaxFR/MaxPS4/actions/runs/38582054331).
Douze exécutions M1 par backend, soit 36 cas réels au total. Les résultats
17/83/129 → RAX/mémoire 29/95/141 et TLS 22/88/134 sont observés par le CPU,
avec 0/2 calls selon la variante. Pointeurs : RIP `0x300000004000`,
FS `0x300000420010`, distincts du stockage de staging hôte.

[IPA expérimentale isolée](https://github.com/EnermaxFR/MaxPS4/actions/runs/38582054416/artifacts/11903377596),
identité Orbis Test et vérification ARM64/signatures/imports effectuée par CI.
SHA256 IPA : `1f6e5a666061c83ceed12ec05258abceca4efde1a7df54dde528df75e54ddab7`.
Rétention jusqu’au 28 octobre 2026 ; SideStore doit re-signer app et framework.
Ce binaire prouve une compilation iOS, pas l’exécution de M1 sur iPhone. Le workflow mémoire vérifie Linux/macOS,
le workflow services exécute FEX natif Linux ARM64 et VIXL ARM64 sur x86,
le workflow FEX app exécute le même lot sur macOS puis compile iOS.
Les fixtures exportées et journaux constituent les preuves, pas le seul
statut de compilation. Aucun appareil iPhone attaché; aucune exécution iPhone
n'est établie par les tests locaux ou les workflows hôtes.

## Échecs conservés et corrections

- `a61874b` : M1 macOS refuse une réservation. Les preuves Linux restent valides.
- `4a33c9e` : tests négatifs supplémentaires ; sentinelle non nulle pour prouver
  le store CPU issu de la zone TLS zéro.
- `275c79f` : réservation exacte Mach ; le SDK iPhone rejette mach_vm.h.
- `93b528b` : APIs vm_map publiques compatibles avec le SDK iPhone.
- `e95d0d0` : diagnostique précisément le refus EADDRINUSE à 192 Gio ;
  cette adresse se trouve également dans le carve-out GPU. Aucun mapping
  hôte ou kernel n’est remplacé pour faire réussir le test.
- `6859ba9` : tentative avec l’API de plages Mach, non retenue. Le code XNU
  précise que 64..448 Gio sont réservés au GPU ; l’application ne doit pas
  remplacer ce carve-out.
- `659cafd` : retire cette tentative ; les adresses de fixtures et les régions
  pile/heap/TLS passent à 48 Tio. Nouveau préflight direct complet, également
  sans sanitizers, pour tester la politique VA d’un processus ordinaire.
- `d4eeca7` / `dc2a9da` / `659cafd` : annulent uniquement les runs inachevés
  des SHAs explicitement obsolètes de ce lot. Les résultats terminés restent
  conservés. Le workflow temporaire de nettoyage est retiré dans le rapport final.

[Extraits CI](orbis-mission-198-m1-ci-excerpts.txt). Le clone shell est impossible
(proxy inaccessible). Copie locale partielle du dépôt via connecteur ; commits
publiés par API avec parents et contrôle du SHA de branche.

## Limites restantes

M1 général reste incomplet. Sur iOS, la compilation du backend ne démontre
pas que la politique VA autorise les adresses de ces fixtures : aucune
exécution iPhone n’est testée et les limites VA de l’appareil ne sont pas
levées par la compilation. Le backend direct ne traduit pas les accès JIT
vers un espace hôte différent : collisions et adresses sous 4 Gio sont
refusées. L'admission accepte uniquement un sous-ensemble fini vérifié,
sans branches ni adresses de registre inconnues. TLS dynamique, relocations
TLS et modules TLS multiples, scheduler invité, nouveaux threads CPU,
imports SCE encodés généraux et mmap invité piloté par l'ABI restent à faire.
Le DTV est un contrat statique minimal, pas une implémentation complète
du runtime TLS Orbis. Les protections/unmaps de sous-plages nécessitent
encore un découpage des mappings; protect travaille sur un mapping entier
et release sur une réservation entière. Deux segments partageant une page
hôte sont refusés. Budget total de réservation : 32 Mio.

Aucun DRM contourné, aucun jeu PS4 jouable revendiqué, aucune demande de
test iPhone. PR #199 conservée en draft, aucune fusion ni modification main.
