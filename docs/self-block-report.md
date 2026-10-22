# Caractérisation des blocs SELF (lecture seule)

Reprise de la priorité notée le 17 octobre 2026 : caractériser les blocs SELF
refusés par `SELF.BLOCK / -ENOTSUP`, sans affaiblir les contrôles. Les missions
locales 24 à 26 n'ayant jamais été publiées, ce travail repart de `main`
(`7297a3a`) et ne prétend pas les reproduire.

## Ce qui est ajouté

- `MaxPS4-iOS/Native/PS4SELFBlockReport.hpp` : pour chaque entrée de la table
  SELF, décode les drapeaux (ordonné 0x1, chiffré 0x2, signé 0x4, compressé
  0x8, fenêtre 0x700, segment 0x800, taille de bloc 0xf000, empreintes
  0x10000, étendues 0x20000, ID de program header bits 20..31), vérifie les
  limites, associe l'ID au program header ELF et à son type, puis donne un
  verdict unique. Le résultat du staging `PS4ExecutableImage::load` est
  rapporté tel quel : **les règles d'admission ne changent pas.**
- `MaxPS4-iOS/Tools/ps4-self-inspect.cpp` : outil hôte (Windows, Linux, macOS)
  qui imprime ce rapport pour un `eboot.bin` ou un PRX. Lecture seule, 64 Mio
  maximum.
- `MaxPS4-iOS/Tests/PS4SELFBlockReportTest.cpp`, ajouté au workflow
  `ps4-executable` (sanitizers Linux, macOS, compilation iPhone arm64).

## Ordre des verdicts

1. Blocs chiffrés : il faut un exécutable déjà déchiffré, obtenu légalement
   depuis sa propre console. MaxPS4 ne déchiffre pas et n'embarque aucune clé.
2. Blocs hors limites : fichier tronqué ou en-tête incohérent.
3. Drapeaux inconnus : aucun mappage n'est supposé.
4. Blocs compressés en clair : décompression non implémentée.
5. Blocs signés en clair (type fSELF) : signature non vérifiable ; leur
   admission éventuelle sera une décision séparée, testée et documentée.
6. Sinon : accepté par le staging, ou refusé pour une autre raison (code donné).

## Utilisation

```
g++ -std=c++17 -O2 MaxPS4-iOS/Tools/ps4-self-inspect.cpp -o ps4-self-inspect
./ps4-self-inspect chemin/vers/eboot.bin
```

Le rapport ne contient aucun octet du fichier analysé, seulement des tailles,
offsets et drapeaux ; il peut être partagé dans une issue.

## Limites

Aucune exécution, aucune intégration Swift/iOS pour l'instant, aucune preuve
sur iPhone. Kero Blaster reste non jouable. Étape suivante : lancer l'outil sur
le `eboot.bin` et les PRX du dossier légalement accessible, puis décider quelle
classe de blocs traiter en premier selon le rapport réel.
