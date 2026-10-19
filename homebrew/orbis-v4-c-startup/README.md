# Programme C de démarrage V4

Source originale sous GPL-2.0 comme le dépôt. Aucun SDK propriétaire, libc,
fichier Sony ou chiffrement. Construction depuis la racine :

```sh
bash scripts/build-orbis-v4-homebrew.sh /tmp/orbis-c homebrew/orbis-v4-c-startup/start.c
build/ps4-linked/Bin/maxps4-ps4-linked-test --v4-c-boot /tmp/orbis-c/homebrew.elf
```

Le contrat V4 `STARTUP_RETURN` fournit en RDI le bloc invité argc/argv.
La fonction C charge argc=1, ajoute 256, puis effectue RET ; résultat attendu
257. Le test lance trois fois les octets exacts du fichier, sans les modifier.
Les segments de données et BSS permettent aussi le contrôle natif RW/NX.

Avec Clang testé, `-O1` émet `MOV EAX,256; ADD EAX,[RDI]; RET`. L'opérande
mémoire est intégralement validé avant exécution. `MAXPS4_HOMEBREW_C_OPT_LEVEL=0`
permet de reproduire le refus du prologue non pris en charge. Optimiser n'admet
pas du C arbitraire : les autres instructions restent refusées par le moteur.

Ce programme au format Orbis utilise un ABI expérimental d'entrée de fonction.
Ce n'est pas une application SDK PS4 dont le CRT ou le fonctionnement console
serait validé. Aucun rendu, import, TLS déclaré ou jeu commercial n'est testé.
