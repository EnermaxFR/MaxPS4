# Mission 11 — API distincte de lancement PRX V4

Travail local du 16 octobre 2026, branche `experiment/maxps4-ubuntu-codex`,
HEAD `b031576` avec modifications non commitées des missions 8 à 11.
Aucun push, changement à main, fusion ou workflow distant.

## Audit et interfaces

Le parcours iOS existant lit `maxps4.json`, prépare les modules par
`maxps4_fex_engine_prepare_orbis`, initialise FEX et appelle `run_orbis` sur
`staged_ps4`. Le bridge PRX séparé possède des mappings traduits : son
`admit_dispatch` direct reste à `-ENOTSUP` et ne peut pas devenir un lancement
sûr par suppression de ce contrôle.

La nouvelle API C est `maxps4_fex_engine_run_v4_prx(engine, request, result,
backend)`. Elle réutilise `OrbisPRXPreparation`, `OrbisPRXFEXBridge::map`,
`make_translation` et `OrbisV4Translation`. Elle est one-shot et ne modifie pas
l'image, le graphe, les paramètres ou le rapport CPU du lanceur précédent.
Le résultat et le backend V4 passent uniquement par ses propres sorties.

`OrbisV4PRXLaunch.hpp` contient l'admission native testable sans FEX.
Le runtime installe temporairement les ranges dans le handler FEX, avec un
mode V4 explicite, puis restaure les ranges et le mode précédents sur tous
les retours. Le verrou global du runtime sérialise préparation, dispatch,
invalidation et nettoyage avec les autres API FEX. Le client garde les octets
source immuables pendant l'appel et sérialise la fermeture du handle.

## Préconditions et ABI

| Domaine | Contrat minimal |
| --- | --- |
| Entrée | Un fichier plaintext ET_SCE_DYNAMIC autonome, jusqu'à 32 Mio ; biais explicite aligné selon le chargeur et entrée invitée explicite dans du code lisible/exécutable |
| Dépendances | Aucune dépendance DT_NEEDED, module/bibliothèque importé ou symbole externe requis |
| TLS / initialisation | Aucun PT_TLS, DT_INIT/DT_FINI ou autre attribut de démarrage non pris en charge ; refus, sans ignorer ces besoins |
| ABI | `MAXPS4_V4_ABI_SYNTHETIC_HLT` uniquement ; ce n'est pas l'ABI de démarrage d'un jeu PS4 |
| Pile | Réservation privée à l'adresse invitée `0x700000000000`, une page hôte, gardes, RW/NX, RSP à fin de page moins 16, alignement 16 octets |
| Registres | Registres entiers nuls hors RSP, RIP invité explicite, FS/GS à zéro, GDT/LDT et segment 64 bits du contexte initialisés ; aucun argument/auxv PS4 annoncé |
| Contexte | Moteur FEX déjà initialisé, fournisseur JIT autorisé sur iPhone, sortie HLT activée ; call-return stack hôte privé, sans CALL/RET invité admis |
| Instructions | 1 à 64 instructions admises à l'avance : MOV EAX,imm32 ; ADD EAX,imm32 ; NOP ; MOV RDI,imm64 ; MOV EAX,[RDI] ; HLT terminal |

Chaque instruction est entièrement dans du code invité RX. Un MOV EAX,[RDI]
requiert un RDI connu par un MOV immédiat précédent et quatre octets lisibles.
Aucun autre accès registre/mémoire, store, branche, retour, syscall ou préfixe
n'est admis. Toute la séquence jusqu'au HLT doit tenir dans le budget.
Les octets après le HLT ne sont pas exécutés et ne sont pas déclarés admis.

Les exigences non prises en charge retournent `-ENOTSUP`, moteur non initialisé
`-EAGAIN`, requête invalide `-EINVAL`, allocation impossible `-ENOMEM`, faute
mémoire observée `-EFAULT`. Les deux sorties restent nulles en cas d'échec.
Une erreur de réservation de pile conserve son code mémoire réel. Aucun
interpréteur ou dispatch direct n'est utilisé comme repli.

## Durée de vie et protections

Le transfert initial est transactionnel. L'admission s'effectue sur une copie
privée qui n'est pas exposée aux autres threads ; le bail de dispatch couvre
la validation des ranges, la compilation et `ExecuteThread`. Les droits ne
changent pas entre admission et dispatch.

Le contexte partagé est invalidé avant dispatch pour éliminer des constantes
hôtes précédentes. Le callback du propriétaire cible le thread et les caches
partagés de ce contexte, alors qu'aucun autre dispatch n'est actif sous le
verrou global. `close` invalide avant unmap. L'ordre RAII détruit le propriétaire
avant le thread FEX, et le thread avant ses buffers call-return/GDT. Les fautes
et exceptions suivent le même ordre ; le pointeur de faute temporaire est
retiré du CPU. Le handler retrouve ensuite son état antérieur.

RX invité, données RW/NX, guards, collisions et interdiction W+X restent
appliqués par les propriétaires existants. La lecture du PRX ne déchiffre
aucun PFS/SELF protégé et n'utilise aucune clé ou licence.

## Raccordement iOS séparé

Bibliothèque → **Dossier Orbis accessible** → **Tester le PRX traduit V4
(synthétique)** appelle la nouvelle API. L'action précédente continue d'utiliser
le parcours Orbis existant. Les options V4 sont décodées uniquement pour la
nouvelle action : les anciens manifestes gardent leur comportement.

Exemple pour la fixture, dont l'entrée est `0x44000` :

```json
{
  "root": "eboot.bin",
  "modules": [{"path": "eboot.bin", "loadBias": 262144, "dependencies": []}],
  "v4ABI": "synthetic-hlt-v1",
  "v4Entry": 278528
}
```

Le libellé `eboot.bin` de cette fixture contient un PRX synthétique fabriqué,
pas un exécutable de jeu. La déclaration d'ABI n'accorde aucune confiance au
contenu : l'admission native le revalide. Sans options explicites ou avec un
graphe multiple, l'action V4 refuse avant ouverture du moteur/JIT. Aucun
bouton Jouer pour un jeu commercial n'est activé.

Le binaire de tests exporte la fixture via `--orbis-bundle DIR` dans
`DIR/v4-synthetic`. Exemplaire local : `/tmp/maxps4-mission11-bundle/v4-synthetic`.
Les tests Swift existants consomment ce sous-dossier en CI et contrôlent RAX=42.

## Tests exécutés

| Validation | Résultat local |
| --- | --- |
| Admission V4, C++17 strict | PASS |
| Admission V4, ASan/UBSan non récupérable, LeakSanitizer actif | PASS |
| `scripts/test-orbis-analysis.sh` | PASS : transferts PRX, nouvelle admission, métadonnées et dossiers |
| `scripts/test-orbis-v4-memory.sh` | PASS : six suites et M1, natif et ASan/UBSan |
| `scripts/test-orbis-runtime.sh` | PASS : 3370 checks linker et segments |
| `scripts/test-ps4-system-services.sh` | PASS : services et compilations ABI C, dont requête V4 40 octets et signature de fonction |
| Nouveau test API publique FEX `--v4-prx` | PASS, backend 3, résultats réels 0/42/99/7, refus avec sorties nulles, reprises et deux appelants concurrents sérialisés |
| FEX lié, exécution normale | PASS, nouvelle API puis ancien lanceur, appels/services et nettoyage |
| FEX `--m1`, `--m2` | PASS ; M2 scalar 11/11, refus 55/55, limites 16/16 |
| Test FEX de traduction existant | PASS, quatre configurations TSO/LSE, y compris PRX de mission 10 |
| Export de la fixture et validation JSON | PASS |

Les refus natifs couvrent ABI absent, budget sans HLT, code inconnu, entrée NX
ou non mappée, pointeur de lecture inaccessible, W+X, PT_TLS valide et DT_INIT
qui est autrement préparé avec succès par le chargeur. Les tests de mission 10
et V4 vérifient également copies indépendantes, rollback après préparation
partielle, préservation des occupants hôtes, concurrence avec libération,
invalidation avant unmap et fermeture terminale.

Les vérifications natives sous sanitizers s'exécutent hors traçage sandbox pour
conserver LeakSanitizer. Le JIT complet utilise les flags upstream FEX : aucun
résultat ASan/UBSan du JIT complet n'est annoncé.

Les dernières assertions d'import de bibliothèque et de distinction invité/hôte
ont été revérifiées via les tests ciblés natifs/sanitizers et API FEX après les
passes générales. Journaux et exécutables : `/tmp/maxps4-mission11-*`.
`git diff --check` passe.

## Limites réelles

FEX réel génère les instructions ARM64 exécutées par VIXL sur l'hôte x86,
backend 3. Ce n'est pas une exécution ARM64 native ni un test physique iPhone.
Swift/SwiftUI, xcrun et le SDK iOS sont absents localement : les nouveaux tests
Swift, la compilation de l'interface et son parcours sur appareil restent à
valider. Les tests Swift de refus et d'exécution sont ajoutés au test CI
existant, sans annoncer un passage de CI sur les sources non publiées.

Cette API constitue un lanceur V4 synthétique borné. Un lanceur de jeu avec
graphe PRX, imports système, TLS, init/fini, ABI de processus, instructions
générales, rendu et audio reste absent et refusé. Kero Blaster n'est pas lancé
et aucun résultat de jouabilité n'est établi. L'erreur historique -48 n'est
pas déclarée corrigée.

## Fichiers propres à cette mission

- `MaxPS4-iOS/Native/maxps4_fex_runtime.h` et `.cpp` : API distincte et cycle de vie FEX.
- `MaxPS4-iOS/Native/OrbisV4PRXLaunch.hpp` : admission minimale explicite.
- `MaxPS4-iOS/Sources/MaxPS4FEXDiagnostic.swift` et `MaxPS4GameDirectoryView.swift` : action V4 séparée.
- `MaxPS4-iOS/Tests/OrbisV4PRXFixture.h`, `OrbisV4PRXLaunchTest.cpp`, `PS4FEXLinkedExecutionTest.cpp` : fixture et tests natifs/FEX.
- `MaxPS4-iOS/Tests/FEXSwiftDiagnosticTest.swift`, `PS4SystemServicesABITest.c` : contrat Swift et ABI C.
- `scripts/test-orbis-analysis.sh` : exécution de la nouvelle admission sous sanitizers.
- Ce rapport ; les autres modifications locales proviennent des missions précédentes.
