# Vérification FEXCore ARM64 sans IPA

Le workflow `parallel-heavy-engine.yml` vérifie la révision FEXCore
`f2b679f6028ce1c38875233aecfcf5d3f8ebecec`. Il s'exécute sur les pull requests,
sur les modifications pertinentes de `main` et à la demande. Il ne produit aucune IPA.

Les adaptations de `scripts/patch-fexcore-ios.py` sont appliquées avant toute
compilation, dans les workflows de diagnostic et de packaging :

- attente/réveil du mutex par `atomic_ref` pour Darwin, sans en-têtes Linux ou Windows ;
- masquage POSIX par `pthread_sigmask`, restauration après déplacement et bornes `NSIG` ;
- identifiant de processeur virtuel constant sur Darwin, sans `TPIDRRO` ni syscall Linux ;
- conseil de pages énormes Linux désactivé sur Darwin, sans substituer `MADV_NORMAL` ;
- exclusion des en-têtes Linux et du véritable appel `prctl(PR_GET_MDWE)` sur Apple.

Les branches Linux et Windows restent présentes. Le script vérifie les fragments
attendus avant d'écrire et peut être exécuté à nouveau sans doubler les adaptations.

La CI compile **toutes les unités `.cpp` du JIT** et `CPUBackend.cpp` avec le SDK
iPhoneOS, vérifie les objets Mach-O ARM64 et conserve les logs et symboles externes.
Elle tente les unités restantes même si une compilation échoue, puis échoue à son
tour. Les tests macOS exercent les véritables types FEXCore adaptés : restauration
des masques imbriqués/déplacés et contention entre lecteurs et écrivains. Un délai
maximum de 60 secondes détecte un blocage du test de contention. Ces tests utilisent
les primitives en configuration release (`ASSERTIONS_ENABLED=0`), sans relier le
runtime de journalisation/allocation FEXCore.

Le workflow `parallel-engine-checks.yml` exécute également
`NativeJITPipelineTest.cpp` : traduction de petits programmes x86 en mots ARM64,
repli vers l'interpréteur, rejet des programmes invalides et budgets épuisés.

Les objets ne constituent pas une liaison complète. La mémoire exécutable,
l'intégration avec le FEXCore Darwin d'AetherPS4, l'exécution JIT sur iPhone et les
jeux PS4 restent à valider. Aucun syscall ou symbole manquant n'est simulé pour
faire passer la compilation ou la liaison.
