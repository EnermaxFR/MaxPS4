# Diagnostic FEXCore JIT : erreur -13

Branche de travail : `fix/fexcore-jit`. Aucun changement au runtime ni aux protections iOS.

## État de référence

Le README du dépôt indique que le calcul 40 + 2 fonctionne via l'interpréteur natif sur iOS et que le JIT FEXCore n'est pas opérationnel sur iPhone. Un test réussi ne doit jamais être affiché comme une preuve de JIT.

## Mesures nécessaires sur appareil

1. Enregistrer l'opération exacte qui renvoie `-13`, ainsi que `errno` immédiatement après l'appel en échec (avant tout autre appel système).
2. Journaliser séparément la demande d'allocation, le retour du protocole StikDebug, l'adresse et la taille des régions RX, la création de l'alias RW (`vm_remap`) et ses protections (`vm_protect`).
3. Pour les API Mach, conserver le `kern_return_t` brut et le message `mach_error_string` : ne pas confondre les codes Mach avec `errno` POSIX.
4. Confirmer que le runtime FEXCore consomme réellement les régions reçues, et ne tente pas une allocation indépendante interdite.
5. Vérifier les entitlements de l'IPA *signée et installée*, pas seulement ceux du projet Xcode.
6. Distinguer explicitement « script StikDebug terminé », « mapping RW/RX validé » et « exécution JIT ARM64 réellement réussie ».

## Critères de validation

- Aucun faux statut « JIT activé » en cas de repli interpréteur.
- Un test natif minimal confirme la génération et l'exécution effective de code, avec journal d'erreur complet en cas d'échec.
- Les vérifications de taille, d'alignement et de protections mémoire restent actives.
- Compilation et test sur appareil requis avant toute annonce de correction.

Ne pas fusionner de changement fonctionnel tant que la cause du `-13` n'est pas isolée.