# Diagnostic de la branche fix/fexcore-jit (PR #132)

## Établi par lecture du code

Référence MaxPS4 initiale : `9107230d70ff208bcf0d486346860a69162cd4aa`.
Référence AetherPS4 utilisée par la CI : `3347fc13f19860855ce91b66356940989ea6b6a6`.

* `maxps4_native_map_jit_errno_probe` demande une page RW anonyme avec
  `MAP_JIT`, sans RX, sans BRK, sans exécution. Le retour observé `errno=1`
  signifie EPERM pour cet appel précis. Ce n'est ni EACCES (13), ni un code Mach.
* `CS_DEBUGGED` est un état de signature ; `P_TRACED` est un indice de traçage.
  Aucun ne certifie un gestionnaire du trap `BRK #0xf00d` actif.
* Sur iPhone, `maxps4_fex_engine_initialize` et `maxps4_fex_engine_probe`
  retournent explicitement `-ENOTSUP` avant allocation/exécution FEX. Darwin
  définit ENOTSUP à 45 (Linux à 95). Une erreur -13 vient donc d'une autre couche.
* L'amont FEX Darwin (`FEXCore/Source/Utils/Allocator.cpp`, `iOSJITAlloc`)
  charge `BreakpointJIT.framework` avec dlopen/dlsym, appelle
  `BreakGetJITMapping(nullptr, size)`, puis crée l'alias RW avec
  `vm_remap(copy=FALSE)` et `vm_protect(RW)`. La table WriteToExec/ExecToWrite
  convertit les adresses utilisées par FEX. Ce n'est pas le test local MAP_JIT.
* L'IPA FEX actuelle n'embarque pas BreakpointJIT ; le vérificateur exige son
  absence. Le port expérimental `maxps4_stikdualmap_allocate` n'est pas branché
  sur cet allocateur FEX ; un test du petit traducteur MaxPS4 ne prouve pas FEX.
* `MAXPS4_PLATFORM_JIT_ONLY` interdit ce port dans la bibliothèque de l'app FEX.
  Le contrôle ABI AetherPS4 ne compile ni n'active son allocateur.
* Le projet utilise `CODE_SIGNING_ALLOWED=NO`. Seul le framework est signé ad
  hoc pendant le packaging ; SideStore doit signer l'app et les dépendances.
  Le bundle identifier reste `com.enermaxfr.maxps4`.

## Cause du refus MAP_JIT encore non isolée

EPERM établit un refus par la politique mémoire/signature d'iOS pour cet appel.
Il ne révèle pas à lui seul quelle condition du noyau a refusé l'allocation.
Les entitlements de l'app effectivement installée, le profil, la version/build
d'iOS, le modèle/SoC et les logs debugserver ne sont pas disponibles ici.
L'absence d'autorisation MAP_JIT dans la signature effective et les restrictions
de la version d'iOS restent des hypothèses. Une session debugserver terminée ou
un script incompatible peuvent expliquer un échec du protocole BRK, mais ce
test MAP_JIT n'interroge pas ce protocole. Le retour automatique de StikDebug
et « JIT request completed » ne sont pas des accusés de validation FEX.

## Correctifs sûrs

Les deux entrées publiques du protocole sont maintenant des wrappers : aucun
BRK ni detach tant qu'un gestionnaire n'est pas validé. La validation reste
explicitement absente, sans bouton, préférence persistée ou CS_DEBUGGED capable
de la fabriquer. Les routines ARM64 brutes sont conservées en interne.
La CI appelle ces wrappers sans gestionnaire SIGTRAP et doit survivre avec un
refus, puis audite la signature du bundle construit. Aucun garde FEX n'est retiré.

Un branchement futur doit réutiliser l'allocateur amont et valider un contrat
de session actif avant tout trap. Il devra contrôler les protections réelles
RX/RW, l'identité des pages par écriture/lecture via les deux alias, les
conversions d'adresses FEX et l'invalidation des caches, puis nettoyer les
deux mappings en cas d'échec. Les logs doivent séparer errno des retours Mach.
La lecture amont révèle aussi des chemins d'échec après réception RX qui ne
libèrent pas RX ; ce défaut n'est pas exercé ni corrigé dans cette dépendance
ici. L'ajout du framework seul ne valide ni le trap ni la mémoire exécutable.

## Validation iPhone sans perte de données

1. Sauvegarder les données de l'app. Conserver l'installation existante ; ne
   pas la désinstaller. Installer la version test comme mise à jour avec le
   même identifiant et la même identité de signature. Si SideStore propose
   un autre identifiant ou une réinstallation, interrompre cette étape.
2. Sur macOS, auditer le `.app` exact après resignature, avant installation :
   `python3 scripts/audit-ios-signature.py /chemin/MaxPS4.app`.
   Conserver le JSON, le SHA256 de l'IPA installée, le modèle, iOS/build, versions
   SideStore/StikDebug et le script sélectionné. Une signature non vérifiée ou
   des entitlements inconnus ne doivent pas être transformés en autorisation.
   Ne pas partager le profil complet, les certificats ou les données utilisateur.
3. Demander le JIT dans StikDebug et relever les logs de session debugserver
   attach/detach, le PID ciblé et la prise en charge du trap universel. Comparer
   avec le PID du processus MaxPS4 ; un nouveau lancement est une nouvelle session.
4. Utiliser « Diagnostiquer StikDebug / mémoire JIT (sans BRK) ». Collecter
   CS_DEBUGGED, P_TRACED, MAP_JIT/errno et le rapport FEX. Attendu actuellement :
   aucun trap et FEX refusé avec -ENOTSUP. Un succès d'interpréteur reste distinct.
5. Aucun test BRK ne doit être lancé avec cette intégration non validée.
   Une future version supervisée devra d'abord établir le contrat de trap,
   prouver l'allocation double mapping, exécuter du code ARM64 généré donnant
   42, puis le programme x86 `MOV EAX,40; ADD EAX,2; HLT` par **FEXCore** avec
   backend `MAXPS4_CPU_FEX_ARM64_JIT`. Archiver les résultats et logs sur appareil.

Une compilation ARM64, une IPA vérifiée ou un test macOS ne prouvent pas
l'exécution JIT FEX sur iPhone. PR à conserver non fusionnée.
