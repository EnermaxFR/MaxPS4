# Reprise du 18 octobre 2026 — décodage du shader pixel

Le point de reprise est le refus DRAW_INDEX_AUTO décrit dans mission58-gcn-compute.md. Le compute est franchi ; le pipeline graphique reste bloqué.

GCNPixelProgram.hpp ajoute une validation bornée du sous-ensemble GCN1/2 identifié dans le diagnostic next-ps-encoding.txt : S_MOV_B32 (littéral ou constante inline), S_BUFFER_LOAD_DWORDX4 à offset immédiat, S_WAITCNT, V_MOV_B32 et EXP MRT0 RGBA non compressé avec DONE/VM, suivi de S_ENDPGM. Les instructions sont conservées avec leur position et leurs opérandes brutes. Le programme n'est publié qu'après validation complète ; les littéraux ne sont pas interprétés comme des instructions. Les formats non pris en charge et les instructions après l'export final sont refusés.

Tests : séquence documentée, variantes inline/littérales, troncature à chaque DWORD, limites de registres scalaires, offset non immédiat, export compressé ou répété, instruction après DONE et budget du programme. Compilation stricte avec -Wall -Wextra -Werror et exécution ASan/UBSan réussies (ASAN_OPTIONS=detect_leaks=0). Le test est ajouté au script de la suite graphique ; la suite complète n'a pas été relancée pendant cette reprise.

Ce composant n'est pas encore branché au renderer et n'exécute pas le shader. Aucun lancement supplémentaire du jeu, export pixel exécuté, framebuffer rendu, flip, menu, gameplay ou test iPhone n'est revendiqué. La prochaine étape est l'exécution de ces instructions avec contrôle des ressources et de la mémoire, puis le traitement du shader vertex, la rasterisation et les surfaces tiled. Les modifications antérieures du workspace sont conservées.
