# Test intégré ELF PS4 / libkernel

La mission n'est pas déclarée terminée : le critère final exige le rapport du
nouveau test exécuté sur un véritable iPhone. Les résultats ci-dessous sont
des validations de développement ; une compilation IPA ne remplace pas cette
preuve. Aucun changement direct de main ni fusion de PR n'est effectué.

## Chemin exécuté

Le bouton **Tester ELF PS4 / libkernel**, dans Diagnostic FEXCore, appelle
`maxps4_fex_test_ps4_libkernel` sur la file CPU existante. Il construit en mémoire
le même ELF reproductible que les tests :
`Native/maxps4_ps4_synthetic_elf.h`. Aucun fichier commercial n'est nécessaire.

ELF64 x86-64, OSABI Orbis 9, type 0xfe10, entrée 0x4000, deux PT_LOAD RX/RW,
PT_DYNAMIC avec tables DT_* standard et RELA JUMP_SLOT. L'import NID
`aI+OeCz8xrQ` est résolu vers `scePthreadSelf`, libkernel version 1. Le linker
existant pose un trampoline invité et réécrit l'appel dans son arène bornée.
Le runtime existant configure les registres/piles, utilise FEXCore, capture
l'appel dans son handler OS_GENERIC, invoque le vrai service libkernel, écrit
RAX/RIP de reprise puis exécute RET vers le programme invité.

Le programme stocke le retour brut dans [0x10108], ajoute 1 à EAX, stocke le
résultat dans [0x10100], puis termine par HLT. L'hôte relit ces deux valeurs
par l'API mémoire invitée ; il ne les écrit jamais à la place du programme.
Valeurs attendues : retour 1 (identité bootstrap libkernel), mémoire finale 2,
RAX final 2 et un appel. Budget d'admission : 10 instructions, trampoline et
NOP inclus. Aucun repli interpréteur n'est utilisé par ce test.

Le rapport conserve les statuts par étape ; « non tenté » et « indisponible »
remplacent les zéros non mesurés. Il fournit les erreurs du linker, les erreurs
d'initialisation/exécution/lecture, le diagnostic Mach du fournisseur JIT, le
backend réel, l'environnement, la date UTC et la révision Git embarquée dans
l'IPA avant signature. **Partager le rapport ELF PS4** exporte le texte.

Le contexte est fermé après succès ou échec. Le rapport compte les segments
invités encore enregistrés après leur retrait et les leases RX/RW réellement
utilisées après fermeture, comparées au nombre avant le test. Le premier
compteur concerne le chargeur, pas un recensement de toutes les VM de l'OS.
Les régions StikDebug préparées restent réservées pour un autre test ; le
bouton ne supprime pas l'autorisation JIT de l'application.

## Pourquoi les anciens champs étaient à zéro

`MaxPS4FEXDiagnostic.run` initialisait `guestRIP` et `instructions` à zéro.
Seul `maxps4_fex_engine_interpret_elf` remplit ces paramètres. Quand le JIT
réussissait, `run_elf` ne les remplissait pas et aucun repli n'avait lieu.
Ce n'étaient ni un RIP final observé, ni zéro instruction exécutée.

Le chemin JIT affiche maintenant « Repli interpréteur : non utilisé. RIP final
et instructions terminées FEX : non exposés par cette API. » Le chemin
interpréteur conserve ses mesures réelles. Le test 40 + 2 → 42 reste inchangé.

## Tests de développement

Sur Debian 13 x86-64, GCC 14 et Clang 19 :

- Douze suites mémoire/ELF/services/linker sous ASan + UBSan réussies, plus
  deux compilations ABI C11 (y compris layout du nouveau rapport Swift/C).
  `ASAN_OPTIONS=detect_leaks=0` est utilisé localement ; aucune validation
  LeakSanitizer n'est revendiquée. La CI conserve les options par défaut.
- Vrai FEXCore épinglé Aether 3347fc13, VIXL 5f418449 : trois exécutions
  consécutives du test intégré, retour 1, mémoire 2, backend **3** et cleanup.
  Le simulateur VIXL exécute le code ARM64 généré ; cela ne valide pas le
  backend **2** ARM64 natif ni un iPhone.
- Variante de l'ADD invité : mémoire/RAX 8 et retour libkernel toujours 1.
  Vérification du résultat calculé plutôt qu'un retour constant de la démo.
- Appels combinés open/read/close/clock/mmap, quatrième et septième arguments
  SysV, RBX/RBP/R12–R15 préservés, refus avant InitCore, pointeurs hôte refusés,
  restaging invalide et rollback. Test FEX existant → 42 réussi.
- Tests Python diagnostics (3), audit signature (3), patch dispatcher (3),
  script Stik, syntaxe Bash et parsing YAML réussis.
- Runner borné : succès, code d'échec enfant et expiration avec journal
  préservé vérifiés. Chaque exécution CPU de test est limitée à 60 secondes,
  l'export de fixture à 10 secondes ; les jobs et étapes lourdes sont bornés.

Un défaut indépendant a été trouvé dans PS4GuestMemoryTest : son scénario de
chemin de 1025 octets écrivait à data+0x100 dans un segment de 64 octets. Seule
sa fixture est agrandie à 0x2000 ; les contrôles de permission restent stricts.
Les douze suites sont maintenant exécutées par le workflow services.

Le run IPA antérieur 38334480381 avait affiché « 0 leaks » puis expiré dans
`leaks --atExit` après huit minutes. Le contrôle n'est pas supprimé : le nouveau
runner lui impose 60 secondes et redirige vers un fichier régulier, évitant
qu'un pipe hérité retienne tee après la sortie du processus. La cause exacte
du blocage de l'outil reste à confirmer par la nouvelle CI Apple.

## Validation nécessaire sur iPhone

1. Installer l'IPA correspondant à la révision testée via SideStore.
2. Effectuer l'activation StikDebug existante puis revenir dans MaxPS4.
3. Vérifier que **Tester FEXCore** donne encore backend ARM64 JIT et résultat 42.
4. Appuyer sur **Tester ELF PS4 / libkernel** et partager le rapport.
5. La preuve doit montrer appareil iOS physique, chargement/liaison réussis,
   entrée 0x4000, NID/fonction ci-dessus, backend **2**, un appel, retour brut 1,
   reprise confirmée, mémoire finale 2, RAX 2, aucune erreur et zéro lease
   RX/RW après nettoyage lorsque le test démarre sans autre moteur actif.
6. Répéter le test pour vérifier la réutilisation et le nettoyage.

Sans ce rapport, la validation sur iPhone reste ouverte. Un rapport backend 3,
un test macOS ou une IPA compilée ne remplit pas le critère final.

## Limites

Admission finie et adresses CPU internes rebased ; aucune traduction générale
de l'espace virtuel PS4. Tables SCE, PRX complets, TLS/FS, ordonnanceur et
création de threads invités restent hors périmètre. L'ELF synthétique Orbis
avec DT_* standard est une démonstration de ce contrat borné. Aucun démarrage
de Kero Blaster ni rendu de jeu PS4 n'est revendiqué.
