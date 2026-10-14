# Test iPhone — MaxPS4 Orbis Test

1. Télécharger l’IPA de l’artifact **MaxPS4-Orbis-Test-SideStore** de la PR V2,
   uniquement si le job de packaging est vert. Vérifier le bundle
   `com.enermaxfr.maxps4.orbistest` / nom `MaxPS4 Orbis Test`. Installer via SideStore
   comme application distincte ; ne pas désinstaller/remplacer MaxPS4, PKG Test
   ou JIT Test. SideStore doit resignifier l’app et son framework ; l’archive
   ad hoc ne possède pas elle-même une autorisation JIT.
2. Ouvrir Orbis Test, choisir **Charger la fixture légale**. Lire le rapport :
   ET_SCE_EXEC, x86-64, entrée 0x1000, un segment RX et BSS, aucune dépendance
   dynamique. « Préparation réussie » concerne le linker, pas une exécution.
3. Avant StikDebug, tenter **Préparer FEX/JIT et exécuter**. En l’absence de
   régions JIT préparées, attendu : erreur JIT et `execution=not_attempted`.
   Exporter le diagnostic ; ne pas présenter ce refus comme une exécution.
4. Ouvrir **Diagnostic FEXCore / activation StikDebug**, activer avec StikDebug
   pour cette application et ce processus ; utiliser son script MaxPS4 fourni.
   Après détachement, revenir et lire la confirmation. La confirmation du nonce
   et une canary ARM64 ne prouvent pas l’exécution de l’ELF.
5. Revenir à la vue Orbis Test et réessayer. Preuve attendue uniquement si toutes
   les étapes réussissent : `execution_status=0`, `backend=2`, `native_fex=true`,
   `entry=4096`, `rax=42`, `fixture=true`, `fixture_verified=true`. Exporter le
   rapport avec la révision et le hash de la fixture. Un backend 1/3, un mauvais
   RAX ou une erreur ne satisfait pas la preuve iPhone.
6. Répéter trois fois ; les moteurs et mappings invités doivent être nettoyés.
   Dans le diagnostic, libérer les régions JIT quand aucun test ne tourne ;
   une nouvelle exécution devra attendre une activation valable.
7. Importer ensuite un **ELF Orbis autorisé non chiffré**. Les imports NID et PRX
   absents restent listés et bloquent la liaison ; aucune recherche implicite
   de fichiers/firmware. Le bouton reste indisponible après rejet du linker,
   et toute admission CPU/TLS/JIT échouée bloque l’appel d’exécution.
8. Cas négatifs : ELF tronqué, architecture ARM64, fichier Linux ET_EXEC,
   segment W+X, import non résolu, TLS ou instruction hors du sous-ensemble.
   Attendu : refus explicite, aucun lancement, application utilisable.
9. Pour le test PKG Kero Blaster, conserver l’app **MaxPS4 PKG Test (#195)**.
   Attendu observé : identification correcte, mode PFS 0xd, eboot.bin inaccessible.
   Ne pas mélanger ce diagnostic PKG avec une preuve d’exécution Orbis.

À joindre au résultat : modèle iPhone, version/build iOS, versions SideStore et
StikDebug, SHA256 de l’IPA installée et rapport exporté. Ne pas joindre PKG/ELF
commercial, profil complet, certificats, pairing, nonce ou contenu utilisateur.
Si JIT/liaison/admission échoue, conserver le code et l’étape exacte ; ne pas
forcer l’exécution. Le rendu Metal reste un test synthétique séparé dans la vue
diagnostic et ne constitue pas une image produite par ce nouvel ELF minimal.
