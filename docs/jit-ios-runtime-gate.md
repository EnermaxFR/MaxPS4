# Blocage JIT iPhone identifié dans le runtime

Fichier : `MaxPS4-iOS/Native/maxps4_fex_runtime.cpp`.

`maxps4_fex_engine_initialize()` contient sous `#if TARGET_OS_IPHONE` un retour immédiat `-ENOTSUP`. `maxps4_fex_engine_probe()` contient également un retour `-ENOTSUP` sous iPhone. Le chemin macOS exécute `InitCore()` et le test x86, mais pas le chemin iPhone.

Il s'agit d'une désactivation explicite, indépendante de la réussite du script StikDebug. **Ne pas retirer le garde sans fournir un allocateur JIT iOS validé**, sinon le programme pourrait planter ou tenter des mappings interdits.

Étapes : (1) ajouter une capacité d'allocateur iOS réellement détectée ; (2) conserver le refus explicite tant qu'elle est absente ; (3) instrumenter l'échec exact des mappings et distinguer errno des codes Mach ; (4) tester sur appareil avec IPA signée ; (5) seulement ensuite activer le chemin FEXCore.

Le `-13` vu sur appareil peut venir d'une autre couche ; le `-ENOTSUP` de ce fichier vaut `-45` sur Darwin (`-95` sur Linux). Ne pas les confondre.
