# Conformité, licences et attribution — MaxPS4

**État : préparation du portage, conformité complète non encore auditée.**

## Origine et indépendance

MaxPS4 est un projet expérimental et indépendant sur iOS. Il n'est **ni développé, ni approuvé, ni affilié** à Sony Interactive Entertainment, PlayStation ou à l'équipe officielle shadPS4. Les marques PlayStation, PS4 et Sony appartiennent à leurs propriétaires respectifs.

shadPS4 : https://github.com/shadps4-emu/shadPS4 — merci à ses auteurs et contributeurs.

AetherPS4 : https://github.com/Leviidev/AetherPS4 — ses auteurs conservent les droits et mentions applicables à leurs contributions.

Le dépôt MaxPS4 contient une interface Swift, des prototypes ELF/CPU et des ponts natifs. La CI récupère des sources AetherPS4 pour compiler des composants réels (dont le moteur invité avec les en-têtes FEXCore Darwin), mais leur liaison complète à l'application reste bloquée. La récupération des sources complètes shadPS4 est proposée dans la PR #83 ; elle ne constitue pas à elle seule un portage du noyau ou du GPU. **Aucun jeu PS4 n'est actuellement jouable** et le JIT sur iPhone n'est pas validé. Il ne faut présenter aucun de ces projets comme un portage complet.

## Licence et redistribution

MaxPS4 inclut le texte de la GNU GPL version 2 dans son fichier [LICENSE](../LICENSE). shadPS4 est distribué sous GPL-2.0. Avant toute incorporation de son code source et redistribution d'une application dérivée, il faut :
- conserver les mentions de copyright, licences et avis requis par les auteurs ;
- identifier les fichiers modifiés et documenter leurs modifications ;
- assurer l'accès au code source correspondant et respecter les obligations de distribution de la GPL applicables aux binaires ;
- examiner **toutes les dépendances et ressources** (y compris leurs licences propres), et vérifier la compatibilité de l'ensemble, notamment avec les conditions de signature/distribution iOS ;
- ne pas reprendre sans autorisation les logos, marques ou œuvres d'autrui lorsqu'elles ne sont pas couvertes par une licence adaptée.

Le présent document **ne constitue pas une certification juridique**. Un audit du code réellement intégré et de chaque mode de distribution est nécessaire avant de présenter MaxPS4 comme entièrement conforme.

## Jeux, firmware et données

Aucun jeu commercial, PKG, clé, fichier de firmware PS4 ou contenu protégé de Sony n'est fourni par MaxPS4. Les utilisateurs doivent disposer des droits nécessaires sur leurs propres fichiers. La possession d'un jeu ne règle pas automatiquement toutes les questions légales concernant le déchiffrement, les mesures techniques de protection ou l'extraction selon le droit applicable. Ne pas distribuer de moyens de contournement ni promettre une légalité universelle.

## Distribution de développement hors App Store

Le projet privilégie actuellement les tests par IPA hors App Store. Cela n'exempte **pas** les distributions des obligations de licence, d'attribution, d'accès aux sources correspondantes, de signature ou des restrictions de sécurité d'iOS. Un abonnement Apple Developer ne confère pas automatiquement le droit ou la capacité d'exécuter du JIT sur les appareils iOS. La conformité du produit complet n'est pas certifiée.

## Limites techniques et transparence

Une compilation iOS réussie n'implique pas que l'émulation soit opérationnelle. Les écrans de scan PKG et les tests ELF/CPU ne prouvent pas qu'un jeu PS4 peut démarrer. L'exécution x86-64 sur ARM64, les services PS4 et le moteur graphique restent à développer et à tester.

Références :
- https://github.com/shadps4-emu/shadPS4
- https://github.com/Leviidev/AetherPS4
- https://github.com/shadps4-emu/shadPS4/blob/main/LICENSE
- https://www.gnu.org/licenses/old-licenses/gpl-2.0.html
