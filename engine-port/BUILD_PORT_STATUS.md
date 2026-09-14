# Etat du port moteur -- 2 septembre 2026

Ce document concerne uniquement `source/hde`. Il ne modifie aucune installation
de jeu. **Le port compile, mais le paquet produit ne doit pas encore etre lance
ni copie dans un jeu.**

> **Complement du 2 septembre 2026 (audit).** Le diagnostic de paquetage
> de la section « Erreur de paquetage identifiee » est **incomplet** sur deux
> points decisifs : le runtime manquant depasse deux DLL, et le probleme de
> noms de DLL n'est pas un simple renommage. Voir « Diagnostic de paquetage
> precis » en fin de document ; c'est cette section qui fait foi pour la
> reparation. Le paquet `dist/HDE_Authority_Test` est **interdit de test**.

## Objectif final

Construire un moteur Win32 dans lequel l'hote est l'unique simulation de la
mission. Le client transmet seulement des intentions. L'hote valide et publie
l'etat de tous les joueurs, IA, tirs, collisions, degats, morts, inventaires,
vehicules, objectifs et commandes trainer qui affectent la partie.

## Travail source effectivement realise

Le premier passage de migration est implemente dans la copie source :

- `HD_HOST_AUTHORITY` a ete ajoute au protocole interne ;
- `GameMission.cpp` attribue les ennemis a l'hote dans ce mode ;
- `NM_AUTH_INPUT` transporte les entrees du client vers l'hote ;
- l'hote valide l'expediteur, le soldat, les sequences et le delai de
  conservation d'une entree ;
- un controleur distant permet a l'hote de simuler le soldat du client avec
  les entrees recues ;
- l'hote rejette les paquets historiques `NM_GAME` emis par le client afin que
  celui-ci ne puisse pas imposer position, degat, mort ou inventaire.

Le build Release a ete genere avec succes :

```text
source/hde/_cmake_build/bin/Release/HDE_Authority.exe
SHA-256: 7388BC7035B3FC6AC76D15DE789E25696E1CE7CE65AE3454FDA0F943E87A2D49
```

La compilation n'est pas une validation d'execution ni une livraison.

## Erreur de paquetage identifiee

> Premier diagnostic, conserve comme etat anterieur. Complete et precise
> par « Diagnostic de paquetage precis -- audit du 2 septembre 2026 » en fin
> de document.

Le test dans `C:\\Users\\user\\Downloads\\HIDDEN_Original\\bin` n'a pas demarre
car le binaire compile n'etait pas accompagne d'un environnement compatible :

- `HDE_Authority.exe` depend de `MSVCP140.dll` et `VCRUNTIME140.dll`, absentes
  du paquet de test ;
- les modules moteur construits etaient produits sous des noms tels que
  `hd_i3d_2.dll`, alors que le chargeur historique attend notamment `i3d2.dll`,
  `igraph2.dll`, `inet2.dll`, `isound2.dll` et `itabler2.dll` ;
- les DLL originales restantes dans la copie de jeu ne sont pas encore prouvees
  ABI-compatibles avec le nouvel executable compile.

Le probleme vient donc du paquet de developpement incomplet. Il ne prouve pas
que l'installation, les donnees ou le mod de l'utilisateur sont incorrects.
Ne remplacez pas un `hde.exe` original avec ce binaire. Gardez la copie de test
separee et restaurez son executable original avant tout autre essai.

## Restant avant le premier test a deux PC

1. Construire le moteur et les DLL requises avec un runtime redistribuable
   (preferablement CRT statique) ou inclure les runtimes exacts.
2. Recompiler/emballer les modules sous les noms historiques exacts et verifier
   leurs dependances, architecture x86 et ABI avec l'executable.
3. Produire une installation de test complete dans un dossier neuf, sans
   melanger DLL nouvelles et DLL d'origine.
4. Demarrer ce paquet localement sans trainer, puis avec le meme mod de jeu,
   avant de fournir quoi que ce soit a l'autre PC.
5. Implementer le handshake version/hash et les snapshots/ACK/resynchronisation.
6. Migrer toutes les commandes trainer qui touchent au gameplay vers des
   commandes host-only ; Protection totale et F12 en font partie.
7. Tester les memes binaires sur les deux PC et comparer les journaux hote et
   client pour chaque scenario.

La specification et le plan de migration sont dans `HOST_AUTHORITY_SPEC.md` et
`AUTHORITY_MIGRATION_MAP.md`.

## Diagnostic de paquetage precis -- audit du 2 septembre 2026

Cette section **complete et precise** la section « Erreur de paquetage
identifiee » ci-dessus. Cette derniere n'etait pas fausse, elle etait incomplete
sur deux points decisifs. Elle est conservee telle quelle comme premier
diagnostic ; c'est la presente section qui fait foi pour la reparation.

Constats obtenus en analysant la table d'import PE reelle de
`HDE_Authority.exe` (PE32, x86) et le fichier `_cmake/CMakeLists.txt`, pas en
relisant la documentation.

### C1 -- le runtime manquant est plus large que deux DLL

Le binaire importe :

| Groupe | Modules |
| --- | --- |
| Runtime C++ | `MSVCP140.dll`, `VCRUNTIME140.dll` |
| UCRT | `api-ms-win-crt-convert-l1-1-0.dll`, `-filesystem-`, `-heap-`, `-locale-`, `-math-`, `-process-`, `-runtime-`, `-stdio-`, `-string-`, `-utility-` (10 modules) |
| Systeme | `KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, `ADVAPI32.dll`, `SHELL32.dll` |
| Moteur, import statique | `hd_crash.dll`, `hd_tabler2.dll`, `icom_rd.dll`, `IEditor.dll` |

Copier deux fichiers ne suffira pas : il faut le redistribuable VC++ 2015-2022
**x86** complet, ou un CRT statique.

Cause racine : `source/hde/_cmake/CMakeLists.txt:7`
`set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")` (`/MD`).

### C2 -- le probleme de noms de DLL n'est pas un simple renommage

Deux mecanismes de chargement coexistent dans le meme binaire :

- **Import statique**, avec les noms `hd_*` non historiques. Deux cibles
  seulement portent un `OUTPUT_NAME` correct : `hd_ieditor` -> `IEditor`
  (ligne 159) et `hd_dta_read` -> `icom_rd` (ligne 308). Toutes les autres
  gardent leur nom de cible.
- **Chargement dynamique par les thunks** (`hd_i3d2_thunk`, `hd_igraph_thunk`,
  `hd_inet_thunk`, `hd_isound_thunk`). A l'execution le binaire demande
  `i3d2.dll`, `igraph2.dll`, `inet2.dll`, `isound2.dll` -- les **noms
  historiques**. Chaines presentes dans le binaire ; sources confirmees dans
  `_src/Insanity/Lib/*/Thunk/Main.cpp`.

Or `dist/HDE_Authority_Test/` ne contient que `hd_i3d_2.dll`, `hd_igraph2.dll`,
`hd_inet2.dll`, `hd_isnd2.dll`. **Aucun de ces noms n'est celui que le binaire
cherche.** Consequence double :

1. dans le dossier de test seul, `LoadLibrary` echoue : pas de demarrage ;
2. pose dans une vraie installation, le binaire chargerait les **DLL d'origine
   de 2002** (`i3d2.dll` compilee en VC6 contre `msvcp60`), c'est-a-dire
   executable moderne + moteur VC6. Un demarrage apparent dans ce cas serait un
   **faux positif dangereux**, pas une validation.

### C3 -- `hd_debugmem.dll` construit mais reference par aucun import

Le binaire porte la chaine `DebugMem.dll`. Statut a trancher pendant la
reparation : corriger le nom du module, ou le retirer du paquet.

### R10 -- les fichiers texte du paquet contredisaient la consigne

`dist/HDE_Authority_Test/README.txt` disait encore « Use this exact folder on
BOTH test PCs » et `TEST.txt` « Mettez les 11 fichiers de ce dossier dans chaque
copie de test », alors que `HOST_AUTHORITY_TEST.md` declare le paquet bloque.
Ces fichiers voyagent **avec** le paquet ; c'est celui-la qu'un lecteur suivra.
Corriges le 2 septembre 2026 : les deux portent desormais un avis de blocage
explicite et l'ancienne procedure y est marquee annulee.

### R12 -- compatibilite entre la revision source et le jeu installe

Le `hde.exe` present dans `source/hde/bin` porte le SHA-256
`1C6712221236402F3D1D34F5D3982B62322490680B5E01959E4626B04C2104AD`, alors que
les deux installations testees utilisent
`5D5EED6174658B8FACFBC1251146B109BBB12AD9A119168AA3489F80979D62D0`.
**L'arbre source n'est pas la revision du jeu installe.**

Le port repare peut donc demarrer proprement et malgre tout ne pas lire les
donnees, tables, scripts et formats `.dta` de l'installation, en particulier
ceux du mod **Ultimate Mod 5.0**. Ce risque doit etre leve par un lancement
local avec le mod, avant tout test LAN. Il ne figurait dans aucune precondition
anterieure.

## Statut du paquet `HDE_Authority_Test` -- 2 septembre 2026 : interdit de test

Tant que C1, C2 et C3 ne sont pas repares et qu'un lancement local n'a pas
reussi, le dossier `source/hde/dist/HDE_Authority_Test/` ne doit etre ni
installe, ni copie dans un jeu, ni fourni au second PC. Aucun `hde.exe` original
ne doit etre remplace, renomme ou ecrase, dans aucune installation.

## Risque de perte du travail -- R9, 2 septembre 2026

`git ls-files source` renvoie **vide**. `source/` n'est pas dans `.gitignore` :
il n'a simplement jamais ete ajoute. Sont hors versionnement `source/` (la
totalite du port moteur, le `CMakeLists.txt` genere de 33 Ko et tous les
correctifs d'autorite), `release/` et `interface/`. Seul `src/`, l'ancien
trainer, est suivi.

Un incident disque, une restauration ou un nettoyage de dossier fait disparaitre
integralement la piste cible. Ce risque prime sur toute question technique.

*Procedure de sauvegarde proposee. A executer uniquement apres autorisation
explicite de l'utilisateur : aucune commande `git add` et aucun commit n'ont ete
lances le 2 septembre 2026.*

1. Copie hors depot d'abord : archive datee de `source/`, `release/` et
   `interface/` vers un support distinct du disque de travail.
2. Decider explicitement ce qui est versionne et ce qui est exclu.
   `source/hde/_vendor/` contient plus de 1,4 Go d'installeurs DirectX et
   `source/hde/_cmake_build/` est un dossier de sortie : les deux doivent etre
   exclus, pas commites.
3. Sur autorisation seulement : versionner `source/hde/_src`,
   `source/hde/_cmake`, les `source/hde/*.md`,
   `source/hde/vc6-project-manifest.json`, puis `interface/`, en laissant
   `_vendor/`, `_cmake_build/`, `_build/`, `bin/` et `dist/` hors suivi.
4. Un commit dedie, sans melange avec des modifications de `src/`.
5. Repeter la sauvegarde hors depot avant chaque changement de packaging.

## Restant avant le premier test a deux PC -- liste revisee le 2 septembre 2026

Cette liste remplace celle de la section precedente, qui reste consultable comme
etat anterieur. L'ordre est contraignant.

1. **Sauvegarde et versionnement approuves** (R9). Aucun `git add`, aucun commit
   sans autorisation explicite.
2. **Packaging executable** : CRT statique ou redistribuable complet (C1) ;
   `OUTPUT_NAME` historiques pour tous les modules moteur (C2) ; sort de
   `hd_debugmem.dll` tranche (C3) ; dossier neuf sans **aucune** DLL de 2002.
3. **Lancement local sans mod** : journal de chargement sans DLL manquante, sans
   erreur de chargement, sans melange de modules anciens et nouveaux.
4. **Lancement local avec le mod Ultimate 5.0** (R12).
5. **Correction du filtre `NM_GAME`** (R1) : il rejette actuellement **tout**
   `NM_GAME` venant du client et risque de bloquer synchronisation, briefing,
   chat, carte, menus, vehicule et debut de mission. Detail dans
   `AUTHORITY_MIGRATION_MAP.md` et `plan.md`. A corriger **avant** de mobiliser
   un second PC.
6. **Test LAN minimal** a deux PC, journaux compares des deux cotes (R8).
7. **Demotion client permanente** (R2, R3, R5).
8. **Instantanes, accuses de reception, resynchronisation.**
9. **Handshake version/hash** (traiter R4 a cette etape).
10. **Commandes trainer host-only** (`NM_AUTH_COMMAND`,
    `NM_AUTH_COMMAND_RESULT`).
11. **Protection totale**, puis **Retour a la vie F12**. Ces deux fonctions
    n'existent pas dans le moteur : aucune ligne. Elles ne doivent etre ni
    testees, ni annoncees, ni presentees comme fonctionnelles avant que les
    etapes 8 et 10 soient faites et verifiees.
