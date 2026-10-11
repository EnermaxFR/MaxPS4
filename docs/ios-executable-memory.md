# Mémoire exécutable iOS et protocole appareil

Aucun test sur iPhone n'a été exécuté dans cet environnement Linux.
Une compilation ou une liaison ARM64 ne prouve ni allocation RX, ni exécution JIT,
ni exécution de jeux PS4.

Sources examinées : `MaxPS4-iOS/Native/shadps4_bridge.cpp`,
`MaxPS4OnDeviceJITProbe.swift`, FEX CPUBackend/AllocatorHooks/Allocator et
Aether `src/core/ios/ios_jit_allocator.cpp`/Allocator Darwin.
Les primitives mmap/munmap, mprotect, sys_icache_invalidate, vm_remap/vm_protect
et les gardes pthread_jit_write_protect_np ont des contrats distincts.
L'hôte ARM64 Apple peut avoir des pages de 16 Kio ; utiliser sysconf plutôt
que la taille de page x86 invitée de 4 Kio.

Le générique suppose des mappings RWX. Le port Darwin distingue adresses
exécutables RX et alias RW, traduit les offsets, encadre l'invalidation du cache
et la suspension des autres threads. Sur iOS le fork demande une région RX via
BreakGetJITMapping chargé par dlopen/dlsym, puis crée l'alias RW avec vm_remap.
Cette dépendance de session externe n'est pas un import obligatoire du dylib.
L'absence des symboles, un refus AMFI ou une session debugger perdue restent
possibles après une liaison réussie. Les BRK de cette bibliothèque peuvent
produire SIGTRAP si le service externe ne répond plus.

Prérequis : véritable iPhone ARM64 compatible avec la version minimale ciblée,
Mac/Xcode/SDK et Developer Mode, signature/profil permettant le débogage,
vérification réelle de get-task-allow et des droits disponibles, debugger attaché.
Un entitlement macOS allow-jit ne constitue pas une garantie iOS. Vérifier
les entitlements signés, pas seulement le plist du dépôt.

Protocole, sans génération d'IPA dans ce travail :

1. Lister l'appareil avec `xcrun devicectl list devices`, relever UDID/OS,
   confirmer la signature et le debugger avant lancement d'un harness signé.
2. Allocation : journaliser taille de page, adresse, errno et permissions réelles.
   Un mmap MAP_JIT réussi seul est un résultat d'allocation.
3. Écriture : émettre `mov w0,#42; ret` (0x52800540, 0xD65F03C0) sur la face RW,
   vérifier les octets et, pour les alias, leur même backing/offset.
4. Protection : établir RX conformément au contrat choisi, conserver RW séparé
   pour le fork, invalider le cache instruction ; relever tout refus sans fallback.
5. Exécution : appeler exclusivement l'adresse RX, contrôler retour 42 ;
   distinguer crash/SIGILL/SIGTRAP/refus de protection d'un retour incorrect.
6. Libération : munmap/vm_deallocate pour chaque mapping ; répéter ensuite avec
   réutilisation du buffer et deux threads avant d'essayer un bloc FEX réel.

Harness existant réutilisable : maxps4_native_generated_arm64_execute_probe
(shadps4_bridge.cpp), déjà appelé depuis la sonde Swift. Il exige un debugger,
alloue RW MAP_JIT, émet les deux mots, invalide le cache, demande RX, exécute et
libère. Codes : 1 retour 42, 0 non supporté, -1 debugger absent, -2 allocation
refusée, -3 protection refusée, -4 mauvais retour. Ce harness ne teste pas le
protocole dual mapping FEX et peut s'arrêter sur -2/-3. Ne pas transformer ces
refus en succès ni activer les chemins BRK sans gestion de session/signal vérifiée.
