# Modules système PS4 (firmware)

Les jeux PS4 appellent des bibliothèques du firmware : `libc.prx`,
`libSceFios2.prx`, `libSceLibcInternal.sprx`, etc. Plutôt que de réécrire
chacune de leurs fonctions, MaxPS4 peut utiliser les modules que
l'utilisateur a copiés, **déchiffrés, depuis sa propre console**, comme le
dossier `sys_modules` de shadPS4. `libkernel` reste toujours réimplémenté par
MaxPS4 (HLE), car c'est le lien avec le système de l'iPhone.

## Ce que MaxPS4 ne fait pas

- Il ne fournit aucun fichier de Sony : ni firmware, ni module, ni dans l'IPA
  ni dans ce dépôt.
- Il ne déchiffre rien. Un module SELF chiffré (octets `4F 15 3D 1D`) est
  signalé comme tel et n'est pas lu plus loin. Un module utilisable est un ELF
  en clair (octets `7F 45 4C 46`). Voir `LEGAL-COMPLIANCE.md`.
- Le fichier de mise à jour PUP de Sony est lui-même chiffré : il n'est pas
  utilisable.

## Dans l'app

**Diagnostic → Modules système PS4 (firmware)** :

- **Importer un dossier de modules…** copie les fichiers `.prx` et `.sprx` du
  premier niveau du dossier choisi (liens symboliques ignorés) dans le dossier
  `Application Support/SystemModules` de l'app, exclu des sauvegardes. Un
  fichier du même nom est remplacé.
- L'inventaire indique, pour chaque fichier : en clair ou chiffré, type ELF,
  nombre d'exports et d'imports, modules et bibliothèques fournis.
- **Supprimer les modules importés** efface ce dossier.

## État

Inventaire seulement : le chargement d'un jeu avec ces modules (résolution
automatique des modules dont il dépend) est l'étape suivante.

Code : `MaxPS4-iOS/Native/SystemModules.hpp` (inventaire, testé par
`SystemModulesTest`), `maxps4_system_modules_report` dans
`OrbisAnalysisBridge`, `MaxPS4OrbisAnalyzer` (import, Swift) et
`MaxPS4SystemModulesView`.
