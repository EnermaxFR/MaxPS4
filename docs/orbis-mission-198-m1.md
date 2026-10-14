# Mission #198 — lot 2 : adressage direct et TLS statique

## Changement livré

L'ancien chemin FEX recalait RIP et RSP sur des pointeurs hôtes. Le nouveau
chemin expérimental `maxps4_fex_engine_run_m1` utilise les adresses x86-64
invitées pour RIP, RSP, FS et les pointeurs absolus. Il conserve les anciens
chemins et n'est pas intégré à l'interface utilisateur.

`GuestAddressSpace` distingue adresse logique et stockage hôte. Son backend
traduit sert aux tests et aux accès contrôlés. Le backend direct FEX réserve
la même plage numérique avec un simple indice mmap, vérifie l'adresse obtenue
et refuse une collision sans écraser de mapping existant (aucun MAP_FIXED).
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

Tests locaux : les sept suites du lot 1 et OrbisM1MemoryTest passent sous
ASan/UBSan. LeakSanitizer est désactivé uniquement dans cet exécuteur tracé;
les workflows conservent leurs contrôles. Test supplémentaire de faute réelle
sur gardes/trou, collision hôte conservant le contenu, protections, TLS de
deux threads distincts et refus d'instructions/budget/TLS malformé.

CI du nouveau code : en attente. Le workflow mémoire vérifie Linux/macOS,
le workflow services exécute FEX natif Linux ARM64 et VIXL ARM64 sur x86,
le workflow FEX app exécute le même lot sur macOS puis compile iOS.
Les fixtures exportées et journaux constituent les preuves, pas le seul
statut de compilation. Aucun appareil iPhone attaché; aucune exécution iPhone
n'est établie par les tests locaux ou les workflows hôtes.

## Limites restantes

M1 général reste incomplet. Le backend direct ne traduit pas les accès JIT
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
