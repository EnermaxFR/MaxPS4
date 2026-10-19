# Mission 64 — IPA expérimentale iPhone

Préparation du 19 octobre 2026 depuis les sources locales des missions précédentes,
sur une branche de compilation isolée. Les rapports, captures, journaux et extraits
du jeu des missions 61–63 ne sont pas inclus dans ce changement.

## Portée

Le workflow `fexcore-app.yml` construit une IPA ARM64 iOS 18 ou ultérieur,
avec le runtime FEX, les diagnostics et les programmes homebrew du projet.
Les fichiers commerciaux du jeu doivent être importés séparément par leur propriétaire.

Les résultats Linux (écran titre puis introduction de Kero Blaster) utilisent
un runner de développement distinct. Ils ne prouvent pas que ce parcours fonctionne
dans cette application iPhone. Le premier niveau contrôlable reste non validé.
Cette IPA sert à tester installation, préparation JIT et diagnostics sur appareil ;
aucune jouabilité de Kero Blaster n’est revendiquée.

## Construction et installation

1. Exécuter `MaxPS4 FEXCore SideStore IPA` sur la branche de cette préparation.
2. Télécharger l’artefact `MaxPS4-FEXCore-SideStore` après succès du workflow.
3. Extraire puis re-signer l’IPA avec SideStore et un profil approprié.
4. Relever modèle d’iPhone, version iOS, révision source et résultat des diagnostics JIT.

La signature ad hoc du packaging doit être remplacée pour l’installation.
La disponibilité du JIT et les performances doivent être vérifiées sur l’appareil.
