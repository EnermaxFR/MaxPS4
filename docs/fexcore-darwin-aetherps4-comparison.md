# Contrats Darwin et choix de cible

FEX générique : `f2b679f6028ce1c38875233aecfcf5d3f8ebecec`.
AetherPS4 : https://github.com/Leviidev/AetherPS4 au commit
`3347fc13f19860855ce91b66356940989ea6b6a6`. Son répertoire
`runtime/sources/fexcore-darwin` est un fork permanent du même commit FEX,
avec les adaptations de contexte, fichiers, mémoire virtuelle, signaux et pages hôte.

| Contrat | Générique | Darwin embarqué | Décision |
| --- | --- | --- | --- |
| ScopedJITWriteProtect | absent | garde macOS ; iOS écrit via alias RW | construire avec son runtime cohérent |
| GetWritableAddress | absent | RX → RW, offset conservé | ne pas remplacer par identité |
| GetExecutableAddress | absent | RW → RX, offset conservé | ne pas mélanger les pointeurs d'ABI |
| OnBufferReusedInPlace | absent | invalidation avec token de verrouillage | garder l'implémentation complète |
| BeginBufferInvalidationSafepoint | absent | suspend les autres threads | aucune fonction vide ajoutée |
| EndBufferInvalidationSafepoint | absent | reprend après invalidation | vérifier concurrence sur appareil |

`audit-fex-darwin-contracts.py` ignore les commentaires ; la sonde C++ vérifie
les types des six déclarations avec seulement les headers de ce fork. Les appels
Aether sont aux lignes 1625, 1276, 1726, 2138, 2177, 2258 de fex_guest_engine.cpp.

Corrections intégrées : liaison du véritable FEXCore_Base au shared target ;
compilation des vraies dépendances fmt, xxhash, cephes et SoftFloat pour iOS ;
utilisation de sys_icache_invalidate aux deux sites qui appelaient le builtin
non fourni par compiler-rt iPhoneOS ; désactivation du réglage CPU natif Linux
pendant la cross-compilation. SDK et architecture sont explicites.

Les six adaptations génériques restent testées ; elles ne sont pas remplacées
par des signatures du fork. Le fork utilise os_sync_wait_on_address et requiert
ici iOS 18. Le diagnostic générique reste iOS 16. Cette cible n'est pas encore
intégrée à l'app MaxPS4 : abaisser sa version minimale requiert un port du mutex.

Le fork comporte des comportements Linux optionnels non disponibles sur Darwin
(dont prctl ENOSYS pour MDWE/noms de mappings), déjà documentés dans son header.
Aucun symbole factice destiné à compléter la liaison n'est ajouté par MaxPS4.
Ces adaptations doivent être auditées avant intégration produit ; une liaison
ne valide pas leurs comportements. Le chargement BreakpointJIT est dynamique :
la présence de ses fonctions ne peut être vérifiée par le linker.
