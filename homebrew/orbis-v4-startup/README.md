# Sonde de démarrage V4

Source originale libre sous la licence GPL-2.0 du dépôt, sans SDK ni libc.
Construction depuis la racine :

```sh
bash scripts/build-orbis-v4-homebrew.sh /tmp/orbis-startup homebrew/orbis-v4-startup/start.S
build/ps4-linked/Bin/maxps4-ps4-linked-test --v4-startup /tmp/orbis-startup/homebrew.elf
```

Contrat expérimental `MAXPS4_V4_ABI_STARTUP_RETURN` : RDI pointe sur un bloc
`argc=1, argv[0], NULL, envp=NULL, AT_NULL, 0`, entièrement invité. `argv[0]`
est la chaîne `homebrew`. RSP est aligné comme une entrée de fonction
(`RSP % 16 == 8`) et contient un retour vers un trampoline invité RX.
Le programme lit argc, ajoute 256 et retourne réellement par RET : FEX doit
observer 257. Une variante de test lit les quatre premiers octets d'argv[0].

Ce contrat est une étape isolée, pas l'ABI de démarrage d'un processus PS4
complet. Le trampoline HLT termine l'exécution de test FEX ; aucun exit libkernel
n'est simulé. Pas de TLS, import, libc, ordonnanceur ou rendu. Une application
OpenOrbis ordinaire ne devient pas compatible grâce à cette seule sonde.
