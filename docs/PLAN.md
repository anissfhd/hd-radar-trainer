# Plan et état du projet HD Radar

> ## Etat canonique -- 2 septembre 2026
>
> Toutes les sections de versions historiques ci-dessous sont un **historique**
> des tentatives du trainer externe. Elles ne prouvent pas que le reseau est
> corrige. Les essais
> utilisateur ont etabli les faits suivants :
>
> - le trainer local peut afficher, proteger ou reanimer localement alors que
>   l'autre PC peut encore afficher un etat different ;
> - V88/V92 tracent bien les changements `death_guard` et `damage_guard`, mais
>   un paquet UDP et des hooks client ne peuvent pas changer l'autorite du
>   moteur pair-a-pair historique ;
> - **Protection totale** (activation comme decochage) et **Retour a la vie
>   F12** ne sont pas valides en LAN et ne doivent pas etre annonces comme
>   fonctionnels ;
> - l'objectif final est que l'hote valide toutes les actions et produise tous
>   les etats de jeu ; le client envoie ses entrees et affiche l'etat officiel.
>
> Un premier port source est compile dans `source/hde`, avec IA attribuee a
> l'hote, entrees client -> hote et rejet des paquets de resultat client par
> l'hote. Il est **incomplet et non deployable** : voir
> [`source/hde/BUILD_PORT_STATUS.md`](source/hde/BUILD_PORT_STATUS.md).
> Le paquet `HDE_Authority_Test` actuel ne doit pas etre copie dans un jeu : il
> depend de runtimes MSVC absents et de DLL moteur non packagees sous les
> noms/ABI attendus. Cette erreur de paquetage est de notre cote, pas une erreur
> d'installation de l'utilisateur.

## Audit technique du port moteur — 2 septembre 2026 (constats R1 à R12)

> Cet audit a été produit en relisant le code du port dans `source/hde/_src` et
> en analysant la table d'import réelle du binaire compilé, pas en relisant la
> documentation. Chaque constat porte une référence vérifiable. Il précise le
> bloc « Etat canonique » ci-dessus et n'annule aucune section historique de ce
> fichier.
>
> Règle de lecture : rien ici ne doit être présenté comme fonctionnel parce que
> cela compile, parce qu'un journal affiche « actif », ou parce que cela se
> comporte correctement sur le seul écran de l'hôte.

Les constats sont rangés en **cinq catégories strictes**. Elles ne doivent
jamais être mélangées dans un rapport, une annonce ou une consigne de test :

| Catégorie | Contenu | Statut |
| --- | --- | --- |
| A | Ancien trainer externe historique | Clos, invalidé pour l'autorité réseau |
| B | Code moteur déjà compilé | Existe, jamais exécuté, non validé |
| C | Erreurs de packaging | Ouvertes, empêchent tout lancement |
| D | Blocages avant le premier test LAN | Ouverts, empêchent tout test à deux PC |
| E | Travail restant | Non commencé |

---

### A. Ancien trainer externe historique — état au 2 septembre 2026

- Périmètre : `src/`, `build/`, `release/`, `archive/`, les helpers
  `HD_AI_AUTHORITY_HOST.exe` / `HD_AI_AUTHORITY_CLIENT.exe` et le canal UDP
  privé port 48217.
- Cette piste **ne modifie pas** le moteur réseau. Le pair-à-pair d'origine
  reste intégralement en place : chaque PC simule une part de l'IA et publie
  ses propres résultats.
- Les traces V88/V92 prouvent uniquement que les commandes UDP **arrivent**.
  Elles ne prouvent à aucun moment que le client affiche l'état canonique de
  l'hôte. Un paquet reçu n'est pas une autorité transférée.
- **Protection totale** (activation comme décochage) et **Retour à la vie
  F12** restent non validés en LAN. Ils ne doivent pas être annoncés comme
  fonctionnels par cette piste, dans aucun document et dans aucune livraison.
- Plusieurs fonctions purement locales du trainer (ESP, radar, armes locales,
  noclip, téléportation carte, overlay) continuent de fonctionner
  correctement. Ce n'est pas contradictoire : local n'est pas autoritaire.
- Règle de test opposable : **aucun** trainer externe, helper V88/V92, hook
  mémoire ou canal UDP d'autorité ne doit tourner pendant un test du moteur.
  Un écart observé pendant qu'un helper tourne n'est pas exploitable comme
  résultat.

---

### B. Code moteur déjà compilé — relevé vérifié le 2 septembre 2026

Binaire : `source/hde/_cmake_build/bin/Release/HDE_Authority.exe`, PE32 x86.
SHA-256 `7388BC7035B3FC6AC76D15DE789E25696E1CE7CE65AE3454FDA0F943E87A2D49`,
vérifié ; la copie présente dans `source/hde/dist/HDE_Authority_Test/` porte le
même hash.

**Ce qui existe réellement dans le code** — relevé de source, pas de
documentation :

| Élément | Emplacement vérifié | Effet réel |
| --- | --- | --- |
| Interrupteur de mode | `Net.h:19` `#define HD_HOST_AUTHORITY 1` | Compile la nouvelle politique ; l'ancien round-robin reste présent dans la branche `#else` |
| Trame d'intention | `Net.h:24-40` `S_authority_input` | 64 slots de touches, 3 axes souris, `key` / `modify_keys` / `mouse_buttons`, 3 valeurs de config. **Aucun champ position, vie, dégât ou inventaire** : ces valeurs sont donc non falsifiables par construction |
| Message réseau | `Net.h:215` `NM_AUTH_INPUT = 0x5700` | **Un seul** nouveau code réseau existe |
| Envoi client | `Net.cpp:227-256` `AuthoritySendInput` | Refuse d'émettre si `IsHost()`. Envoi direct au PID hôte, `INSEND_CANCELOLDER`, séquence croissante |
| Décodage hôte | `Net.cpp:265-285` `AuthorityReadInput` | Contrôle `AUTHORITY_INPUT_PROTOCOL == 1` |
| Acceptation | `Net.cpp:290-310` `AuthorityAcceptInput` | Anti-rejeu par numéro de séquence ; conserve l'état « touche déjà consommée » |
| Consommation | `Net.cpp:313-325` `AuthorityGetInput` | Vérifie le propriétaire **et** la péremption : `AUTHORITY_INPUT_STALE_MS = 500` |
| Déconnexion | `Net.cpp:328`, appelée depuis `Net.cpp:349` | `AuthorityForgetInput(pid)` sur `INET_SYSMSG_PLAYER_DESTROYED` |
| IA côté hôte | `GameMission.cpp:1302-1310` | Tous les `ACTOR_ENEMY` reçoivent `network_actor = 0` sur l'hôte et `= GetHostPID()` sur le client |
| Validation de propriété | `GameMission.cpp:2702-2725` | Entrée acceptée seulement si `actor->GetType()==ACTOR_PLAYER && actor->network_actor == input.sender_pid` |
| Rejet des résultats client | `GameMission.cpp:2730-2737` | L'hôte fait `SkipChunk()` sur **tout** `NM_GAME` reçu — voir R1, ce filtre est trop large |
| Contrôleur distant | `Actors.cpp:13668-13727` | `C_authority_controller` (adaptateur `C_controller`) et 4 slots runtime persistants |
| Simulation hôte / démotion client | `Actors.cpp:13743-13795` | Client : envoie l'intention puis se traite lui-même comme acteur distant. Hôte : rejoue le `Tick` natif du soldat client avec les entrées reçues |

L'approche est bonne et bien écrite : réutiliser le `Tick` natif via un
`C_controller` synthétique fait passer mouvement, tirs, dégâts et collisions
par le code d'origine, au lieu de recréer une seconde synchronisation
incompatible.

**Ce que ce relevé ne dit pas** : ce code n'a jamais été exécuté. Aucune de ces
lignes n'a été observée en fonctionnement, ni en solo, ni en LAN.

#### Défauts relevés dans ce code — aucun n'est visible à la compilation

**R2 — la démotion du joueur client est temporaire.**
`Actors.cpp:13766-13770` : `network_actor` passe à `GetHostPID()` puis est
**restauré à 0** immédiatement après le `Tick`. Tout le reste du code client
— rendu, `NetCodeIn`, `C_human::Hit`, inventaire, voix, programme — voit
toujours le joueur comme local. Le client n'est donc pas encore un simple
afficheur : il conserve des chemins de simulation locale.

**R3 — trou dans les modes de joueur.**
La branche client n'est active que si `mode == PLRMODE_ACTIVE`. Or le
constructeur initialise `mode(PLRMODE_PROGRAM)` (`Actors.cpp:13024`). En
`PLRMODE_PROGRAM`, `PLRMODE_DYING` et `PLRMODE_DEAD`, le client reprend sa
simulation locale et cesse d'envoyer ses intentions. C'est exactement la
fenêtre où naissent les divergences « vivant chez l'hôte / mort chez l'ami »
que l'objectif final veut supprimer.

**R4 — l'autorité n'est pas transitive.**
Le filtre de `GameMission.cpp:2730` ne s'applique que si `IsHost()`. Avec 3 ou
4 joueurs (`NET_MAX_PLAYERS 4`), un client applique toujours directement les
`NM_GAME` d'un autre client. Sans effet à deux PC, mais la règle « le client ne
décide jamais » est fausse au-delà de deux machines.

**R5 — filtrage en réception, pas autorité par construction.**
Les sites d'émission `NM_HUMAN_POS`, `NM_HUMAN_HIT`, `NM_HUMAN_DIE`
(`Actors.cpp:5418`, `5592`, `11019`) sont gardés par un paramètre `net_send`
qui signifie « faut-il répliquer cette action », **pas** « suis-je
propriétaire ». Le client continue donc d'émettre ses résultats ; on ne compte
que sur le rejet côté hôte, avec le trafic inutile correspondant.

**R6 — `AuthorityAcceptInput` n'est pas sûre isolément.**
`Net.cpp:293-296` : le contrôle de séquence n'est appliqué que si
`old.sender_pid == input.sender_pid`. Un PID différent écrase l'entrée sans
contrôle. La sécurité repose entièrement sur la vérification faite chez
l'appelant (`GameMission.cpp:2712`). Cela fonctionne aujourd'hui et casse au
premier second appelant.

**R7 — dimensionnement à marge nulle.**
`HD_AUTHORITY_INPUT_SLOTS = 64` alors que `GKEY_LAST == 65` (`H&D.h:1863`).
Seul `GKEY_DEBUG` est tronqué aujourd'hui, donc sans conséquence, mais ajouter
une touche de jeu fera disparaître une action silencieusement.

**R8 — comportement au timeout non spécifié et jamais observé.**
Le fichier de test livré promet « le joueur cesse de recevoir les anciennes
touches ». Vérifié dans le code : après 500 ms, `AuthorityGetInput` renvoie
`false`, la branche hôte ne s'exécute pas, et l'acteur retombe en acteur
distant sans mise à jour. Arrêt net, glissade ou animation figée : le
comportement réel n'est ni défini ni observé.

---

### C. Erreurs de packaging — diagnostic précis du 2 septembre 2026

Ce diagnostic **complète et précise** l'analyse initiale consignée dans
`source/hde/BUILD_PORT_STATUS.md`. L'analyse initiale n'était pas fausse, elle
était incomplète sur deux points décisifs.

**C1 — le runtime manquant est plus large que deux DLL.**
Table d'import PE réelle de `HDE_Authority.exe` : `MSVCP140.dll`,
`VCRUNTIME140.dll` **et 10 bibliothèques `api-ms-win-crt-*.dll`** (UCRT :
convert, filesystem, heap, locale, math, process, runtime, stdio, string,
utility). Copier deux fichiers ne suffira pas : il faut le redistribuable
VC++ 2015-2022 **x86** complet, ou un CRT statique.
Cause racine : `source/hde/_cmake/CMakeLists.txt:7`
`set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")` (`/MD`).

**C2 — le problème de noms de DLL n'est pas un simple renommage.**
Deux mécanismes de chargement coexistent dans le même binaire :

- *Import statique* : `hd_crash.dll`, `hd_tabler2.dll`, `icom_rd.dll`,
  `IEditor.dll`. Deux cibles seulement portent un `OUTPUT_NAME` historique
  (`hd_ieditor` → `IEditor` ligne 159, `hd_dta_read` → `icom_rd` ligne 308) ;
  toutes les autres gardent leur nom de cible `hd_*`.
- *Chargement dynamique par les thunks* (`hd_i3d2_thunk`, `hd_igraph_thunk`,
  `hd_inet_thunk`, `hd_isound_thunk`) : le binaire demande à l'exécution
  `i3d2.dll`, `igraph2.dll`, `inet2.dll`, `isound2.dll` — les **noms
  historiques**. Chaînes présentes dans le binaire, sources confirmées dans
  `_src/Insanity/Lib/*/Thunk/Main.cpp`.

Or `source/hde/dist/HDE_Authority_Test/` ne contient que `hd_i3d_2.dll`,
`hd_igraph2.dll`, `hd_inet2.dll`, `hd_isnd2.dll`. **Aucun de ces noms n'est
celui que le binaire cherche.** Double conséquence :

1. dans le dossier de test seul, `LoadLibrary` échoue et le jeu ne démarre pas ;
2. posé dans une vraie installation, le binaire chargerait les **DLL d'origine
   de 2002** (`i3d2.dll` compilée en VC6 contre `msvcp60`), c'est-à-dire
   exécutable moderne + moteur VC6 : exactement le mélange ABI que la consigne
   interdit. Un démarrage apparent dans ce cas serait un faux positif
   dangereux, pas une validation.

**C3 — `hd_debugmem.dll` est construit mais n'est référencé par aucun import.**
Le binaire porte la chaîne `DebugMem.dll`. Statut à trancher lors de la
réparation : nom à corriger, ou module à retirer du paquet.

**R10 — les fichiers texte du paquet contredisaient la consigne de sécurité.**
`source/hde/dist/HDE_Authority_Test/README.txt` disait encore « Use this exact
folder on BOTH test PCs » et `TEST.txt` « Mettez les 11 fichiers de ce dossier
dans chaque copie de test », alors que `HOST_AUTHORITY_TEST.md` déclare le
paquet bloqué. Ces fichiers voyagent **avec** le paquet ; c'est celui-là qu'un
lecteur suivra. Corrigé le 2 septembre 2026 : les deux fichiers portent
désormais un avis de blocage explicite et l'ancienne procédure y est marquée
annulée.

**R12 — compatibilité entre la révision source et le jeu installé, non posée.**
Le `hde.exe` présent dans `source/hde/bin` porte le SHA-256
`1C6712221236402F3D1D34F5D3982B62322490680B5E01959E4626B04C2104AD`, alors que
les deux installations testées utilisent
`5D5EED6174658B8FACFBC1251146B109BBB12AD9A119168AA3489F80979D62D0`.
**L'arbre source n'est pas la révision du jeu installé.** Le port réparé peut
donc démarrer proprement et malgré tout ne pas lire les données, tables,
scripts et formats `.dta` de l'installation, en particulier ceux du mod
**Ultimate Mod 5.0**. Ce risque doit être levé par un lancement local avec le
mod, avant tout test LAN. Il ne figurait dans aucune précondition antérieure.

---

### D. Blocages avant le premier test LAN — 2 septembre 2026

**D0 — le paquet `HDE_Authority_Test` est interdit de test.**
Tant que C1, C2 et C3 ne sont pas réparés et qu'un lancement local n'a pas
réussi, ce dossier ne doit être ni installé, ni copié dans un jeu, ni fourni au
second PC. Aucun `hde.exe` original ne doit être remplacé, renommé ou écrasé,
dans aucune installation.

**R1 — le filtre `NM_GAME` de l'hôte est trop large. Blocage très probable au
premier test LAN.**
`GameMission.cpp:2730-2737` jette **tout** `NM_GAME` venant d'un client. Or
`NM_GAME` n'est pas seulement le canal des résultats de simulation : il
transporte aussi, comme sous-codes (`GameMission.cpp:2840-2866`) :

- `NM_SYNC` — le protocole d'accord `C_net_sync` ;
- `NM_MAP` — le gestionnaire de carte ;
- `NM_CHAT` — le chat en mission ;
- `NM_HUMAN_SWITCH_SYNC` — l'arbitrage de changement de soldat.

Et `C_net_sync` ne passe à `SS_RESOLVED` que lorsque **tous** les PID ont
répondu (`NetSync.cpp`, vidage de `pid_to_answer`). L'hôte jetant la réponse du
client, les événements suivants ne se résolvent jamais :

| Événement | Émis depuis | Conséquence attendue |
| --- | --- | --- |
| `NS_GAME_BEGIN` | `GameMission.cpp:1193` | **La mission ne démarre pas** |
| `NS_TO_BRIEFING`, `NS_TO_MAP` | `GameMission.cpp:510`, `596` | Blocage au briefing et à la carte |
| `NS_ENTER`, `NS_LEAVE` | `Briefing.cpp:234`, `351`, `CampAnim.cpp:55`, `89` | Blocage aux transitions |
| `NS_DONE_SUCCESS`, `NS_DONE_FAIL`, `NS_GAME_EXIT` | `GameMission.cpp:2681-2690` | Fin de mission impossible |
| Événements de menu | `Menu.cpp:742` | Menus figés |
| Ramassage de ressource, mine | `Resource.cpp:157`, `Mine.cpp:197` | Actions bloquées |
| Sièges de véhicule | `Vehicle.cpp:2470`, `3235` | Entrée et sortie de véhicule bloquées |
| Changement de soldat | `Actors.cpp:12553` | Arbitrage bloqué |

Ce filtre doit être restreint aux seuls sous-codes de **résultat** avant de
mobiliser un second PC. Sinon le test LAN échouera pour une raison sans rapport
avec l'autorité et coûtera une session à deux personnes.

**R9 — le travail cible n'est pas versionné. Risque le plus élevé du projet.**
`git ls-files source` renvoie **vide**. `source/` n'est pas dans `.gitignore` :
il n'a simplement jamais été ajouté. Sont donc hors versionnement :

- `source/` — la totalité du port moteur, le `CMakeLists.txt` généré de 33 Ko
  et tous les correctifs d'autorité ;
- `release/` — les binaires livrés ;
- `interface/`.

Seul `src/` (l'ancien trainer) est suivi. Un incident disque, une restauration
ou un nettoyage de dossier fait disparaître intégralement la piste cible. Ce
risque prime sur toute question technique.

*Procédure de sauvegarde proposée. À exécuter uniquement après autorisation
explicite de l'utilisateur : aucune commande `git add` et aucun commit n'ont
été lancés le 2 septembre 2026.*

1. Copie hors dépôt d'abord : archive datée de `source/`, `release/` et
   `interface/` vers un support distinct du disque de travail.
2. Décider explicitement ce qui doit être versionné et ce qui doit être exclu.
   `source/_vendor/` contient plus de 1,4 Go d'installeurs DirectX et
   `source/hde/_cmake_build/` est un dossier de sortie : les deux doivent être
   exclus, pas commités.
3. Sur autorisation seulement : versionner `source/hde/_src`,
   `source/hde/_cmake`, les `source/hde/*.md`,
   `source/hde/vc6-project-manifest.json`, puis `interface/`, en laissant
   `_vendor/`, `_cmake_build/`, `_build/`, `bin/` et `dist/` hors suivi.
4. Un commit dédié, sans mélange avec des modifications de `src/`.
5. Répéter la sauvegarde hors dépôt avant chaque changement de packaging.

**R12 — compatibilité données et mod.** Voir catégorie C. Doit être levée par un
lancement local **avec** le mod Ultimate 5.0 avant tout test LAN.

**Règle de blocage sur Protection totale et F12 — 2 septembre 2026.**
Ces deux fonctions n'existent pas dans le moteur : aucune ligne. Elles **ne
doivent être ni testées, ni annoncées, ni présentées comme fonctionnelles** tant
que les deux conditions suivantes ne sont pas remplies :

1. les instantanés hôte vers client, les accusés de réception et la
   resynchronisation existent et sont vérifiés ;
2. le canal de commande trainer host-only (`NM_AUTH_COMMAND` et
   `NM_AUTH_COMMAND_RESULT`) existe et journalise accepté ou refusé avec sa
   raison.

Les migrer avant reproduirait exactement l'échec de V92 : une commande qui
arrive, un journal qui dit « actif », et deux écrans qui ne montrent pas la même
chose.

---

### E. Travail restant — 2 septembre 2026

**E1 — protocole : 1 message sur 8 existe.**
Seul `NM_AUTH_INPUT` est implémenté. Absents du code, vérifié par recherche sur
`Net.h`, `Net.cpp` et `GameMission.cpp` : `NM_AUTH_HELLO`, `NM_AUTH_COMMAND`,
`NM_AUTH_COMMAND_RESULT`, `NM_AUTH_SNAPSHOT`, `NM_AUTH_EVENT`, `NM_AUTH_ACK`,
`NM_AUTH_RESYNC`.

**E2 — aucun instantané, aucun accusé de réception, aucune resynchronisation.**
C'est le seul mécanisme capable de porter la promesse « les deux PC voient le
même état ». Sans lui, le client n'est pas un afficheur d'état officiel : il
continue de faire tourner sa propre simulation pour tout ce qui n'est pas son
`C_player` pendant `Tick` (voir R2 et R3).

**E3 — aucun handshake version/hash.**
Deux binaires différents peuvent encore se connecter. Rien ne refuse une partie
entre versions mélangées avant le début de mission.

**E4 — aucune commande trainer host-only.**
Recherche sur les fichiers réseau et mission : aucune occurrence de protection,
garde de mort, garde de dégâts, réanimation ou commande hôte. Protection
totale, F12, invisibilité, noclip, super-run, téléportation, précision,
cadence, munitions, aimbot, balles à travers murs, véhicules, inventaire et
Fullhands : **la totalité reste à migrer**.

**E5 — domaines non couverts** : menus, inventaire, véhicules, objectifs et
scripts, reprise après perte réseau, reconnexion.

**R11 — la spécification et le code divergent déjà.**
`HOST_AUTHORITY_SPEC.md` annonce « le protocole sera ajouté au canal
`NM_GAME` ». L'implémentation a fait l'inverse : `NM_AUTH_INPUT = 0x5700` est un
code de **premier niveau**, hors du chunk `NM_GAME`. Le choix du code est
meilleur — il échappe au chunk filtré par R1 — mais la spécification doit être
corrigée, sinon la suite du protocole sera conçue au mauvais endroit.

---

### Plan d'exécution arrêté le 2 septembre 2026

Ordre non négociable. Aucune étape ne commence avant que la précédente soit
constatée, journalisée et acceptée par l'utilisateur.

1. **Sauvegarde et versionnement approuvés.** Archive hors dépôt de `source/`,
   `release/` et `interface/`, puis versionnement sur autorisation explicite.
   Aucun `git add`, aucun commit sans accord préalable.
2. **Packaging exécutable.** CRT statique ou redistribuable complet ; noms de
   modules moteur historiques ; dossier neuf sans aucune DLL de 2002 ; sort de
   `hd_debugmem.dll` tranché.
3. **Lancement local sans mod.** Journal de chargement sans DLL manquante, sans
   erreur de chargement, sans mélange de modules anciens et nouveaux.
4. **Lancement local avec le mod Ultimate 5.0.** Lève R12. Vérifier la lecture
   des données, tables et scripts.
5. **Correction du filtre `NM_GAME`.** Restreindre aux seuls sous-codes de
   résultat ; laisser passer `NM_SYNC`, `NM_MAP`, `NM_CHAT` et
   `NM_HUMAN_SWITCH_SYNC`. Lève R1.
6. **Test LAN minimal à deux PC.** La mission démarre, le client se déplace et
   tire, l'hôte voit le résultat, coupure du flux client au-delà de 500 ms.
   Journaux comparés des deux côtés. Lève R8.
7. **Démotion client permanente.** Le client est un acteur distant en
   permanence, pas seulement dans `Tick`, et dans tous les modes y compris
   `PLRMODE_PROGRAM`, `PLRMODE_DYING`, `PLRMODE_DEAD` et véhicule. Lève R2 et
   R3 ; traiter R5 dans la foulée.
8. **Instantanés, accusés de réception, resynchronisation.** Lève E2.
9. **Handshake version/hash.** Lève E3. Traiter R4 à cette étape.
10. **Commandes trainer host-only.** `NM_AUTH_COMMAND` et
    `NM_AUTH_COMMAND_RESULT`, avec journal accepté/refusé et raison. Lève E4 au
    niveau du mécanisme.
11. **Protection totale**, règle évaluée uniquement par l'hôte. Activation
    **et** décochage visibles identiquement sur les deux écrans.
12. **Retour à la vie F12**, commande validée et exécutée par l'hôte, avec
    réapparition identique sur les deux écrans.

Critère d'arrêt à chaque étape : les deux journaux portent le même tick, le même
identifiant d'acteur et le même résultat officiel. En cas d'écart, le test est
en échec et l'on corrige le moteur — jamais par un hook local, jamais par un
second helper client.

> ### V63 — masque visuel Client, sans régression d'autorité IA
>
> - Conserver strictement les deux exécutables existants : l'hôte impose
>   `network_actor=0` à ses ennemis et le Client impose le PID de l'hôte. Cette
>   boucle reste la seule qui écrit les propriétaires IA, toutes les 100 ms.
> - Le trainer hôte ajoute un signal UDP LAN privé, port 48217, distinct des
>   paquets H&D. Il ne commande aucune donnée du jeu : seulement
>   `hide_host_models=0/1` au helper Client déjà installé.
> - Lorsque **Masquer ma position réseau** est coché, W passe maintenant par
>   trois états sûrs : ancre visible → ancre avec modèles hôte cachés chez le
>   client → position réelle visible. Les autres W reprennent la création
>   habituelle d'une ancre. L'hôte voit toujours son propre modèle.
> - Le helper Client cible exclusivement les acteurs `C_player` dont
>   `network_owner != 0` (les joueurs de l'hôte). Il ne touche ni les ennemis,
>   ni le joueur local du Client. Il modifie uniquement le bit
>   `FRMFLAGS_ON=0x20000` de la frame racine validée (`actor+0x28`,
>   `frame+0x80==actor`) ; la source Insanity3D établit que ce bit est celui de
>   `I3D_frame::SetOn(false)` et que le renderer saute alors toute la hiérarchie
>   du modèle. Aucune désactivation `C_player::SetActive`, suppression de
>   modèle, position, santé ou ownership n'est admise.
> - Le Client rétablit automatiquement les bits qu'il avait retirés dès la
>   commande SHOW ou après 1,5 s sans heartbeat. Le trainer émet également
>   SHOW lors de la désactivation/fermeture normale. Si pare-feu Windows le
>   demande, autoriser UDP privé pour `HD_AI_AUTHORITY_CLIENT.exe`.
> - Sorties compilées et vérifiées :
>   `HDFinalAdvancedV63_CLIENT_VISUAL_MASK.exe`,
>   `HD_AI_AUTHORITY_HOST.exe`, `HD_AI_AUTHORITY_CLIENT.exe`.
>
> ### V64 — premier W arme aussi Invisible pour les ennemis
>
> - Dans `UpdateNetworkPositionMask`, le premier W de chaque session où
>   **Masquer ma position réseau** est coché force seulement
>   `enemy_invisibility_enabled=true`. Le booléen persistant de garde du
>   masque empêche tous les W suivants de réécrire ce réglage ; il se remet à
>   zéro seulement lorsque l'utilisateur décoche explicitement le masque.
> - Ne jamais ajouter de chemin qui mette cette case à faux : désactiver le
>   masque, révéler la position, perdre une mission ou fermer le helper Client
>   ne doivent pas désactiver Invisible pour les ennemis. L'utilisateur garde
>   le contrôle exclusif de sa désactivation dans la case dédiée.
> - Cette automatisation ne crée aucune dépendance inverse : cocher Invisible
>   pour les ennemis seul continue de fonctionner sans masque réseau.
>
> ### V65 — W doit toujours alterner visuellement
>
> - Défaut V64 : après « montrer la vraie position », le W suivant recréait une
>   ancre mais la laissait visible. Le cycle réel était donc cacher → montrer →
>   visible → visible → cacher, contrairement au comportement demandé.
> - Correction : ce W définit `hide_new_anchor_requested` avant la recréation
>   de l'état du masque. Lorsque la nouvelle ancre est prête, elle reçoit
>   immédiatement `remote_visual_hidden=true` et le heartbeat Client masque le
>   modèle. Le cycle est maintenant strictement cacher → montrer → cacher →
>   montrer, sans décocher l'invisibilité ennemis automatique.
>
> ### V66 — joueur contrôlé sélectionné par défaut
>
> - `GameplaySettings::enemy_invisibility_scope` est désormais initialisé à
>   `ControlledPlayer`, comme `network_position_mask_scope` l'était déjà.
> - À chaque nouveau lancement, les deux fonctions sélectionnent donc Joueur
>   actuel. Escouade entière reste disponible uniquement après un clic manuel
>   explicite ; aucun comportement réseau ou autorité IA ne change.
>
> ### V67 — masquer l'interpolation réseau avant la réapparition
>
> - Défaut constaté : au W SHOW, le Client réactivait immédiatement le modèle,
>   tandis que H&D interpolait encore sa frame de l'ancienne ancre vers la
>   nouvelle position reçue. Le joueur semblait courir/téléporter très vite.
> - Le helper Client ne change ni position ni message H&D. À SHOW, il conserve
>   seulement `FRMFLAGS_ON` désactivé au moins 800 ms, puis compare la position
>   monde `frame+0xBC` avec `C_human::net_sync_pos` (`actor+0x204`). Il ne
>   restaure le modèle que lorsque leur distance est inférieure à 0,30 m.
> - Après 4 s, un garde de sécurité restaure quand même l'image. Un nouveau
>   HIDE annule cette attente. La boucle `network_actor` d'autorité IA reste
>   totalement séparée.
>
> ### V68 — révélation rapide après arrêt réel du modèle
>
> - Le critère V67 `frame+0xBC == actor+0x204` s'est révélé trop strict pour
>   cette session : la copie réseau était déjà arrivée visuellement mais la
>   valeur interne ne convergeait pas assez vite, jusqu'au garde des 4 s.
> - Le helper mesure maintenant seulement la frame cachée à deux relevés de sa
>   boucle (100 ms). Après un délai initial de 350 ms qui laisse arriver le
>   paquet H&D, deux déplacements inférieurs à 3,5 cm confirment l'arrêt ; le
>   modèle est alors rendu. Une animation encore en cours remet le compteur à
>   zéro. Le délai de sécurité est réduit à 1,2 s.
> - Aucun champ de position ou de synchronisation H&D n'est écrit : seul le
>   bit de rendu temporaire reste concerné.

> ### V69 — case d'armement, W déclenche la première ancre
>
> - Le défaut V68 venait du fait que cocher **Masquer ma position réseau**
>   créait immédiatement une ancre. Le Client recevait donc déjà une position
>   figée avant le premier W.
> - La case ne fait désormais qu'armer la fonction : hook inactif, position
>   native et modèle normalement visibles chez le Client. Le premier W conserve
>   sa mise en sécurité d'Invisible pour les ennemis, demande la première ancre
>   cachée et garde cette demande jusqu'à obtenir un snapshot joueur valide.
> - Le cycle après armement reste strict : W1 ancre + modèle caché ; W2 vraie
>   position + modèle visible ; W3 nouvelle ancre + modèle caché, etc. La
>   désactivation de la case annule l'armement et n'éteint jamais Invisible
>   pour les ennemis.

> ### V70 — notification locale temporaire après W
>
> - Quand W produit réellement l'état `RemoteVisualHidden`, le trainer affiche
>   **« Maintenant invisible »** en vert dans l'overlay jeu pendant 2 s ; à
>   l'état `RealPosition`, il affiche **« Maintenant visible »** en or.
> - La notification est rendue localement par le même overlay GDI que l'ESP,
>   y compris si les cases ESP sont éteintes. Aucun octet H&D ou paquet LAN
>   supplémentaire n'est utilisé. Une pression W sans mission valide ne ment
>   donc pas : elle n'affiche aucun message tant que l'ancre n'est pas créée.

> ### V71 — miroir de vie hôte vers Client, sans invincibilité
>
> - La demande ne concerne pas la case de protection existante : l'hôte peut
>   subir une vraie mort locale, devenir temporairement squelette et employer
>   F12 pour relancer sa réanimation native. Aucun dégât ni santé locale n'est
>   modifié par cette nouvelle case.
> - Quand **Retour à la vie local (F12) — le Client ignore ma mort** est coché,
>   le trainer émet un heartbeat UDP privé V2. Le Client pose trois petits
>   gardes vérifiés : `C_player::Die`, `C_human::Die` et `C_player::Explode`.
>   Chaque garde laisse le chemin natif à tous les acteurs, sauf `C_player`
>   dont `network_owner != 0` : les joueurs appartenant à l'hôte.
> - Cela empêche le PC Client de recevoir/appliquer le passage mort ou
>   squelette du PC hôte. Il ne modifie pas le joueur local Client, les alliés
>   locaux ou l'IA. Après F12 côté hôte, le Client poursuit sa copie vivante.
> - C'est préventif : la case doit être cochée avant la mort. Le heartbeat est
>   retiré par décochage et expire après 1,5 s sans trainer, restaurant les
>   trois prologues originaux avant de libérer leur mémoire distante.

> ### V73 — chute native, mort d'impact neutralisée
>
> - Le test réel montre que la mort d'une grande chute peut être décidée dans
>   le tick/atterrissage de `C_human`, avant ou hors des voies `Hit`, `Die` et
>   `Explode` déjà protégées. La case de protection existante restait donc
>   contournable par une chute.
> - V72 réduisait `fall_speed` (`+0x1B8`) et rendait donc la descente lente.
>   V73 conserve complètement la gravité et la vitesse natives. Un trampoline
>   vérifié à `C_human::Tick+0x23` efface seulement `falling` (`+0x264`) pour
>   l'acteur contrôlé, juste avant les branches qui appellent `CB_DIE` lors de
>   l'impact. Il ne touche aucun autre soldat.
> - Cette écriture appartient exclusivement à **Protection réseau totale —
>   joueur actuel** et s'arrête dès qu'elle est décochée. Si V71 est aussi
>   coché, le Client conserve simultanément l'image vivante côté ami.

> ## Relais prioritaire — 31 août 2026 (lire avant toute nouvelle modification)
>
> Le projet est un trainer externe Win32 pour `hde.exe`. Ne pas réécrire les
> fonctions réseau déjà validées, ni toucher au joueur contrôlé par le second
> PC. L'utilisateur veut maintenant seulement protéger **son joueur actuellement
> contrôlé** ; les autres soldats de son escouade restent normaux pour le
> moment.
>
> ### État réseau établi par lecture du jeu
>
> - Le solo fonctionne : ce n'est pas la cible du diagnostic actuel.
> - Priorité suivante : **Life Unlimited (F3)**. F3 envoie toujours le cheat
>   natif `immortality`, qui est validé sur les soldats d'origine mais ne
>   protège pas certains soldats ajoutés par Ultimate Mod. Avant d'écrire un
>   nouveau hook de dégâts, comparer en mission LAN un soldat d'origine et un
>   soldat ajouté : adresse/VTABLE, callback de dégâts, santé et état après
>   réception d'un tir. Le but est de prolonger uniquement le chemin qui manque
>   aux soldats ajoutés, sans toucher au joueur du second PC.
> - V51 remplace l'envoi `SendInput`/`BlockInput` de F3 : un trampoline bref
>   dans `C_human::Tick` appelle directement, une seule fois, le même
>   `C_player::cbProc(CB_CHEAT, 14, 0)` que le cheat manuel. Il ne cible que
>   le joueur actuellement contrôlé et restaure les octets natifs avant de
>   libérer sa page. Le journal doit contenir `Life Unlimited: native callback
>   completed=1 restored=1 idle=1 freed=1`.
> - En LAN, l'IA est répartie entre les deux processus. La source de
>   `GameMission.cpp` attribue les ennemis tour à tour avec
>   `CB_SET_NETWORK_ACTOR`, soit localement, soit avec le PID du pair. Le PC
>   distant peut donc décider des tirs et d'une mort. Il n'existe pas de moyen
>   fiable de prendre toute son IA depuis un seul PC sans modifier aussi son
>   processus.
> - Ancienne cause du comportement « noclip : personne ne tire ; couper V :
>   mort immédiate » : le déplacement direct noclip ne passait pas par la
>   mise à jour qui émet `NM_HUMAN_POS`. V49 force seulement l'indicateur
>   interne `upd` après la décision de collision : le constructeur natif du
>   paquet s'exécute, sans réactiver la collision. La case séparée
>   **Masquer ma position réseau** reste prioritaire et peut remplacer la
>   position réelle envoyée.
>
> ### Mise à jour V47/V48/V49 — publication sans collision
>
> V47 a tenté de forcer `C_human::Tick` à publier `NM_HUMAN_POS` pendant
> noclip en mettant `test_col_count` à zéro. Le journal a montré que le vol
> continuait, mais cette opération réactivait aussi le test de collision natif
> et rendait les murs solides : régression refusée. V48 restaure
> `test_col_count=1000`, donc le noclip redevient traversant. Ne pas réessayer
> cette méthode. V49 réalise maintenant ce chemin sur le passage natif situé
> après la collision (RVA `0x1BC4E`) : uniquement pour l'acteur noclip, il
> pose `upd=1`, puis laisse le jeu publier `NM_HUMAN_POS`. Vérifier en LAN le
> compteur `network_publish=` du journal et la visibilité réelle chez le pair.
> Binaire attendu :
> `build\\vs2026-x86\\Release\\HDFinalAdvancedV49_NOCLIP_NETWORK_POSITION.exe`.
>
> ### Solution déjà livrée : masque de position réseau avec W
>
> Version compilée :
> `build\\vs2026-x86\\Release\\HDFinalAdvancedV37_NETWORK_MASK_W_TOGGLE.exe`.
> Le code correspondant est toujours présent dans `src/gameplay_mods.cpp` et
> `src/main.cpp`.
>
> 1. La case **Masquer ma position réseau** arme le masque et capture une
>    position-ancre.
> 2. Le hook est posé à `C_human::Tick`, RVA `0x0001BD97` du binaire H&D connu
>    (adresse chargée habituelle `0x0041BD97`). À cet endroit EBX est
>    `C_human*` et les trois positions sortantes sont les locaux
>    `[ebp-10h]`, `[ebp-0Ch]`, `[ebp-08h]` du message `NM_HUMAN_POS`.
> 3. En état masqué, le trampoline remplace ces trois valeurs par l'ancre. Le
>    vrai frame local, la caméra, la collision et le déplacement restent
>    intacts. En état réel, le trampoline est désactivé et l'envoi natif passe.
> 4. W est volontairement laissé au jeu et bascule les états : premier W
>    `masqué -> réel`, second W `réel -> masqué` avec capture d'une nouvelle
>    ancre. Les portées sont **Joueur actuel** et **Escouade entière** ; cette
>    dernière exclut explicitement l'acteur joueur possédé par le pair.
>
> Ne pas décrire ce mécanisme comme un contrôle absolu de l'autre PC : il
> masque le paquet de position humaine sortant, pas tous les faits que le pair
> peut déjà avoir validés. Il est néanmoins validé en jeu et doit rester la
> base de toute évolution LAN.
>
> ### Autre modification livrée : F5 Fullhands
>
> Version actuelle compilée Release + Debug :
> `build\\vs2026-x86\\Release\\HDFinalAdvancedV38_FULLHANDS_F5.exe`.
> F5 est visible dans la grille Cheats et est de nouveau traité dans la boucle
> des raccourcis de `main.cpp`. Il appelle l'infrastructure existante
> `ApplySingleCheat(CheatId::Fullhands)`, donc la saisie `fullhands` et
> `BlockInput` existant sont réutilisés. Ne pas le remplacer par une nouvelle
> simulation de touches. La touche M de rotation d'équipement est une fonction
> séparée à préserver.
>
> ### Travaux demandés — plan initial (mise à jour V39 ci-dessous)
>
> **A. Case « protection totale — joueur contrôlé ».**
>
> But : aucun dégât ni réaction de dommage pour le seul acteur actuellement
> contrôlé par l'utilisateur : balles, tank/bazooka, grenades, chutes,
> collisions, eau, mine et sortie de carte. Les autres soldats et le joueur du
> pair ne doivent pas être modifiés.
>
> Ce qui existe déjà : `InstallPlayerDamageHook` dans
> `src/gameplay_mods.cpp`. Il accroche dynamiquement `C_player::Hit` via le
> slot de vtable `+0x104`, vérifie le prologue
> `83 EC 6C 53 55`, puis retourne avant le corps pour les `C_player`. Cela
> bloque `NM_HUMAN_HIT` et les dégâts normaux, mais il est aujourd'hui lié à
> « Invisible pour les ennemis » et cible tous les acteurs de type joueur.
>
> À faire :
>
> - créer un réglage et statut séparés dans `GameplaySettings` / `GameplayStatus`;
> - rendre le hook Hit ciblé par l'adresse du joueur contrôlé au lieu du seul
>   type `C_player`, et ne l'activer que pour cette nouvelle case ;
> - localiser **sans deviner** `C_human::Die` par vtable/source/désassemblage,
>   vérifier une signature complète du prologue, puis installer un trampoline
>   réversible. Il devra laisser tous les autres acteurs aller au code natif et
>   retourner proprement pour l'acteur protégé ;
> - identifier et couvrir le chemin des paquets `NM_HUMAN_DIE` : une mort
>   distante peut ne jamais passer par Hit ;
> - journaliser un battement indiquant `hit_live`, `die_live`, acteur protégé,
>   cause/paquet observé et chaque refus, comme les heartbeats d'invisibilité ;
> - tester solo, hôte LAN puis client LAN. Désactivation et fermeture doivent
>   restaurer exactement les deux prologues.
>
> Attention : les morts scénarisées de mission peuvent partager `Die`. Le
> comportement demandé est « ne jamais mourir », mais le test doit vérifier
> que la mission ne se bloque pas. Ne jamais écrire des offsets d'état de mort
> au hasard pour contourner le problème.
>
> **B. F10, réanimation seulement du joueur contrôlé.**
>
> L'utilisateur a abandonné pour l'instant la réanimation de toute l'escouade :
> F10 ne doit concerner que son soldat contrôlé. Ne pas toucher aux autres
> soldats locaux ni au soldat du pair.
>
> Le cheat natif `newlife` ne convient pas : dans `C_player::cbProc(CB_CHEAT)`,
> `CHEAT_ZOMBIE` est entouré de `if (!net)`. Il est donc explicitement refusé
> en réseau. Le simuler avec F10 donnerait une fausse fonctionnalité.
>
> Ordre sûr : finir A, puis instrumenter les messages de mort entrants et
> sortants du seul acteur protégé. Si une mort est interceptée avant
> synchronisation, le F10 devrait rarement être nécessaire. Si le pair a déjà
> accepté la mort, un F10 purement local le ferait vivre ici et mourir chez le
> pair : désynchronisation. Dans ce cas, annoncer clairement la limite ou
> installer le composant synchronisé sur le second PC ; ne pas livrer un
> « revive réseau » local non fiable.
>
> ### Règles de mise en œuvre
>
> - Préserver les modifications utilisateur déjà présentes dans le dépôt sale.
> - Toute adresse doit être obtenue depuis le binaire actif, contrôlée contre
>   les limites du module et vérifiée par signature avant écriture.
> - Les pages injectées doivent être restaurées et libérées uniquement après
>   vérification qu'aucun thread ne les exécute ; réutiliser les modèles de
>   restauration déjà employés par noclip/invisibilité.
> - Ne pas confondre « hook vivant » et « protection prouvée » : journaliser
>   les octets relus et produire un test LAN pour chaque état.
> - Compiler Release et Debug avec les presets effectifs :
>   `release-x86-vs2026` et `debug-x86-vs2026`, cible `HDPhase1`.
>
> ### Mise à jour V39 — 31 août 2026
>
> V39 est compilée en Release et Debug :
> `build\\vs2026-x86\\Release\\HDFinalAdvancedV39_ABSOLUTE_PLAYER_GUARD.exe`.
> La première partie de A est maintenant implémentée.
>
> - Nouvelle case : `absolute_player_protection_enabled`.
> - Le garde `C_player::Hit` a maintenant deux modes : zéro comme cible
>   conserve l'ancien comportement partagé de l'invisibilité ; une adresse
>   cible strictement le seul joueur contrôlé.
> - Un second trampoline vérifié est posé à `C_human::Die`, RVA `0x208B0` :
>   signature `83 EC 44 53 55 8B E9 56 57`, convention `thiscall`, retour
>   `ret 8`. Pour l'acteur protégé il retourne avant le corps ; tout autre
>   acteur rejoue exactement les neuf octets natifs puis continue.
> - Le changement de soldat met à jour l'adresse cible dans la page distante,
>   sans démonter le hook. Les heartbeats donnent `hit_live` et `die_live`.
> - F10 est présenté dans Cheats. S'il détecte le joueur courant déjà en état
>   mort, il restaure les deux mêmes états que la branche native `newlife`
>   (`stay_mode=1`, `mode=2`). Il ne simule pas `newlife` et ne forge aucun
>   paquet réseau, car le jeu refuse ce cheat en LAN.
>
> Reste obligatoire avant de qualifier la fonction de validée : un test réel
> solo puis LAN, avec chute hors carte, grenade, bazooka et coupure/réactivation
> du noclip. Vérifier dans `hdradar_diag.log` les deux octets de hook vivants.
> Si le pair avait déjà reçu une mort avant que V39 bloque Die, F10 reste local
> et une désynchronisation est possible : ne pas prétendre que ce cas est
> résolu sans composant sur les deux PC.
>
> ### Mise à jour V40 — F10 autonome
>
> L'essai réel V39 a révélé la ligne
> `F10 refused because targeted protection is not active`. C'était une
> condition du trainer, pas une limite du jeu. V40 la retire : F10 tente
> maintenant la restauration de l'état vivant même lorsque la protection n'a
> pas été cochée avant la mort. Binaire :
> `build\\vs2026-x86\\Release\\HDFinalAdvancedV40_F10_STANDALONE_REVIVE.exe`.
>
> ### Mise à jour V43 — F10 rétablit la vie native locale
>
> Le test V42 a démontré que les deux écritures `stay_mode=1` et `mode=2`
> seules ne suffisent pas : le joueur se déplace mais conserve le squelette et
> une réserve de vie morte. V43 remplace ces écritures externes par un travail
> ponctuel sur le thread du jeu. Un hook temporaire, contrôlé par la signature
> de `C_human::Tick` (`55 8B EC 81 EC 6C 03 00 00`), exécute exactement la
> suite observée du chemin natif : les deux états, puis
> `C_player::cbProc(CB_SET_RESISTANCE=26, 1, 5000, 0)`. Il valide d'abord le
> slot virtuel `vtable+4`, reçoit un acquittement, restaure le prologue et ne
> libère la page que lorsqu'aucun thread ne s'y exécute.
>
> Le résultat attendu est un soldat local avec modèle et santé normaux. Ce
> mécanisme ne forge pas de paquet de résurrection : le PC pair qui a déjà
> accepté `NM_HUMAN_DIE` conservera son état mort. Ne pas présenter la V43
> comme une synchronisation LAN complète ; W/masque de position et la garde de
> mort restent les moyens préventifs avant que le pair ne valide cette mort.
> Binaire :
> `build\\vs2026-x86\\Release\\HDFinalAdvancedV43_FULL_NATIVE_REVIVE.exe`.
>
> ### Mise à jour V44/V45 — l'état actif manquait à V43
>
> Le journal V43 prouve que le hook est exécuté (`completed=1`), mais le
> soldat garde son squelette. Le désassemblage du handler
> `CB_SET_RESISTANCE` à `0x0042D38C` donne la cause : il sort immédiatement
> lorsque `player+0x2B8 == 0`, l'état laissé par la mort. V44 a appelé la
> grande routine `C_player::SetActive(true)` avant `CB_SET_RESISTANCE`, mais
> un test l'a fait crasher : elle prend aussi la propriété de la caméra et de
> la scène. V45 conserve uniquement la condition démontrée : il écrit le byte
> actif `player+0x2B8=1` sur le thread du jeu, puis appelle
> `CB_SET_RESISTANCE`. C'est le correctif minimal, sans bascule de caméra.
> Binaire :
> `build\\vs2026-x86\\Release\\HDFinalAdvancedV45_SAFE_NATIVE_HEALTH.exe`.

Dernière mise à jour : 1 septembre 2026

### V52 — garde de vie synchronisée (à tester en LAN)

### Autorité IA hôte unique — exécutables préparés, test requis

Les sources Deluxe montrent que `C_game_mission` distribue les ennemis avec :
`local_ai = (count == net_index)` puis
`CB_SET_NETWORK_ACTOR(local_ai ? 0 : net_players[count].pid)`. L'assistance
existante `src/ai_authority_helper.cpp` lit les acteurs de mission, limite son
action au type ennemi et réapplique toutes les 100 ms une règle commune :

- `HD_AI_AUTHORITY_HOST.exe`, lancé sur l'hôte : propriétaire `0`, donc IA
  locale pour chaque ennemi ;
- `HD_AI_AUTHORITY_CLIENT.exe`, lancé sur le client : PID de l'hôte, donc
  acteur réseau et aucune IA locale sur le client.

Les deux exécutables sont compilés en Release dans `build\\vs2026-x86\\Release`
et emportent leur runtime C++ statiquement. Ils attendent `hde.exe`, puis une
mission réseau à deux, et affichent `[actif]`. Les lancer avant la mission et
les maintenir ouverts. Ne pas annoncer une correction garantie avant le test :
la réécriture périodique est un prototype opérationnel ; si le moteur réassigne
un ennemi dans une fenêtre de 100 ms, l'amélioration suivante devra intercepter
la création/affectation native au lieu de corriger après coup.

### V53 — fermeture du chemin de mort entrant

Le journal du test V52 est concluant : à `09:44:51`, les crochets
`C_player::Hit` et `C_player::Die` sont actifs, puis le joueur `02540270`
meurt malgré eux. L'événement envoyé par le PC qui contrôle l'ennemi entre
donc par la routine de base `C_human::Die` sans emprunter la surcharge joueur.

V53 maintient les deux garde-fous, toujours limités à l'adresse du joueur
actuel : `C_player::Die` bloque l'émission locale de `NM_HUMAN_DIE`, et
`C_human::Die` bloque le message entrant. La ligne de diagnostic devient :
`hit_live=1 network_die_live=1 base_die_live=1`. Si ces trois valeurs restent
à un et que le joueur meurt encore, le décès ne passe par aucune des trois
routines et il faudra identifier cet appel précis avant toute autre promesse.

Le problème du garde V39 est maintenant identifié précisément. Le crochet
était posé dans `C_human::Die` (`0x004208B0`). Or `C_player::Die` s'exécute
avant lui et diffuse `NM_HUMAN_DIE` avant d'appeler sa base. Cela protégeait
parfois l'état local trop tard, tandis que le pair avait déjà reçu le décès.

V52 lit l'entrée `C_player::Die` depuis la vtable du joueur courant
(`+0x100`, habituellement `0x00421150`) et n'intercepte que cette adresse. Le
stub renvoie avant le corps de la surcharge : ni l'état mort local, ni le
paquet `NM_HUMAN_DIE` ne sont créés. Le garde `C_player::Hit` reste en place,
donc le paquet de dommage entrant n'abaisse pas la résistance. Activée lorsque
la santé est pleine, elle doit donc le rester ; l'autre PC doit conserver
le soldat vivant et visible. Aucun acteur du pair n'est modifié.

Test nécessaire : héberger ou rejoindre une mission LAN, activer **Protection
réseau totale — joueur actuel** avant les tirs, laisser un ennemi attribué au
pair tirer pendant au moins 30 secondes, puis demander au pair de vérifier que
le soldat est toujours vivant et visible. Le journal doit contenir :
`Absolute protection HEARTBEAT: ... hit_live=1 network_die_live=1`.
### Mise à jour V54 — explosions et image squelette

Le test LAN validé avec l'autorité IA hôte a isolé un dernier défaut local :
un obus de tank/bazooka très proche laisse le joueur contrôlable et vivant sur
le PC pair, mais son PC hôte affiche son squelette. Ce n'est pas une nouvelle
mort réseau : les trois gardes V53 restent vivants. L'explosion passe par la
route séparée `C_player::Explode`, slot vtable `+0xDC`, qui appelle
`C_human::Explode` et enlève la résistance avant le garde `Die`.

V54 ajoute un quatrième trampoline strict, uniquement pour le joueur actuel,
au prologue validé `83 EC 44 53 55 56 8B F1 33` de `C_player::Explode`. Il
retourne `false` avant la résistance, la pose de mort et le remplacement visuel
du modèle. Cela couvre les dégâts explosifs : tank, bazooka, grenade, mine et
explosion d'environnement. Le heartbeat doit donner les quatre valeurs :
`hit_live=1 network_die_live=1 base_die_live=1 explosion_live=1`.

F12 est réservé à la récupération visuelle. Pour un joueur toujours vivant,
il lance seulement dans le thread du jeu la transition native
`SetActive(false,false)` → `SetActive(true,true)` sur ce même joueur, afin de
réinscrire modèle, scène, animation et résistance. Il ne forge ni mort ni
paquet réseau. Pour une mort réelle, F12 passe la main à la réanimation F10.

Test obligatoire : cocher « Protection réseau totale — joueur actuel », se
faire exploser par tank/bazooka/grenade, vérifier que le joueur reste normal
sur les deux PC. Si une ancienne session affiche déjà un squelette, appuyer
sur F12 hors noclip et relever les lignes `F12 REPAIR` du journal.

### Mise à jour V55 — rafale Bullet Track sans fenêtre vide

Le journal d'une rafale réelle a établi que Bullet Track force déjà ses dégâts
à environ 24–35 projectiles par 250 ms, mais que le tir ultra-rapide écrivait
`shoot_countdown=0` depuis la boucle de rendu. Une tentative de tir pouvait
donc tomber entre deux écritures en maintenant le bouton. Le même journal
montrait aussi de rares refus `no_player`, créés volontairement pendant que la
publication Bullet Track mettait le joueur à zéro afin de changer de cible.

V55 ne modifie ni la sélection de tête, ni le résolveur, ni le callback de
dégâts. Après que le trampoline `C_actor::Create` a reconnu un vrai projectile
du seul acteur dont l'arme rapide est validée, il écrit son
`shoot_countdown (+0x260)` à zéro dans le thread du jeu. Toutes les autres
armes et tous les tirs ennemis continuent strictement par le code natif.

La publication garde désormais l'ancien `player` actif pendant les trois
écritures de la nouvelle tête/mission/acteur. Les contrôles existants de
cohérence tête↔acteur, de racine et d'appartenance à la mission font retomber
une lecture mixte dans le chemin natif : aucun mauvais destinataire ne peut
être forcé, tandis qu'une rafale ne perd plus de projectile dans la fenêtre
`no_player`.

Test : cocher **Bullet Track tête** et **Tir ultra-rapide**, maintenir le tir
sur un ennemi vivant. Le journal doit faire progresser `forced_damage_count`
à chaque rafale sans progression de `no_player`; les refus `dead` après la
mort effective de la cible sont normaux.

### Mise à jour V56 — chaîne immédiate après une cible morte

Le journal V55 a confirmé que les filtres de Bullet Track ne perdaient plus
aucun projectile (`no_player`, acteur, mission et tête restent à zéro). Le
seul compteur qui progressait pendant une rafale était `dead` : plusieurs
projectiles créés dans les millisecondes qui suivent la mort de la cible
restaient associés à son acteur, jusqu'au prochain rafraîchissement du radar.

V56 conserve la cible principale, puis publie trois réserves classées par
proximité du réticule. Le trampoline d'évaluation exécute cette sélection sur
le thread du jeu et valide chaque candidat dans le vecteur d'acteurs vivant.
Une cible devenue morte, libérée ou invalide n'est jamais déréférencée : il
essaie la réserve suivante. Une réserve épuisée reprend le tir natif. Le
compteur séparé `retarget` prouve ces passages réussis; il ne représente pas
un refus.

Test : cocher **Bullet Track tête** + **Tir ultra-rapide**, maintenir le tir
sur plusieurs ennemis proches. `forced_damage_count` doit continuer sans
hausse de `dead`; à la mort d'un ennemi, `retarget` doit monter et l'ennemi
suivant doit recevoir immédiatement les tirs.

### Mise à jour V57 — verrou natif du tir maintenu

Le test V56 a validé la chaîne : 804 dégâts forcés, tous les compteurs de
refus à zéro et `retarget=50`. Les brefs creux restants avaient donc lieu
avant Bullet Track : le jeu ne créait simplement pas de projectile pendant
que `C_human::UseItem` attendait la fin de son animation complète.

V57 intercepte les cinq octets du test `IsFullAnim()` à `hde.exe+0xA02E`.
Pour l'acteur publié par l'arme rapide validée seulement, le trampoline saute
vers la continuation native prête à tirer et écrit `shoot_countdown=0` dans le
thread du jeu. Pour tout autre acteur, il rejoue exactement `test al,al`, le
saut natif, et l'instruction déplacée `mov eax,[ebx+1BCh]`. La signature
`84 C0 74 18 8B` est obligatoire avant pose; à la désactivation, les cinq
octets sont restaurés et la page distante n'est libérée qu'une fois inutilisée.

Test : fermer V56, lancer V57, cocher **Bullet Track tête** et **Tir
ultra-rapide**, puis maintenir le clic. Le journal doit indiquer
`Rapid fire: native held-trigger animation gate bypass installed` et les
lignes Bullet Track doivent rester sans refus, avec une cadence régulière.

### V46 - transition native complete de F10

Le desassemblage de `C_player::SetActive` a `0x0042A2B0` etablit qu'elle se
termine par `ret 8` et attend deux arguments : `active` et
`restore_resistance`. V44 n'avait fourni que le premier, ce qui corrompait la
pile du thread du jeu. V46 execute donc la transition native complete dans
`C_human::Tick` : `source.SetActive(false, false)`, etat vivant du soldat mort,
puis `target.SetActive(true, true)`. Le moteur reattache lui-meme le modele,
l'animation, la scene, la camera et la resistance. Les deux slots virtuels
`+0x6C` sont verifies contre le prologue
`53 8B 5C 24 08 56 8B F1 57` avant installation.

F10 rend le controle au soldat ressuscite : le moteur ne peut maintenir qu'un
acteur actif et une camera coherente. La synchronisation du PC distant reste
une limite reseau distincte.

Projet : `C:\Users\user\Desktop\HD_Radar_Project`  
Jeu : Hidden & Dangerous Deluxe, `hde.exe` x86  
Révision de `hde.exe` : SHA-256
`5D5EED6174658B8FACFBC1251146B109BBB12AD9A119168AA3489F80979D62D0`

## 1. Situation actuelle

La phase ESP et la première phase armes sont officiellement terminées et
validées par l'utilisateur en jeu. Une nouvelle phase de fonctions joueur,
navigation, IA, ciblage, véhicules et inventaire commence. Les modifications
seront faites par petits incréments et chaque incrément sera inscrit dans ce
fichier immédiatement après son application, avant de passer au suivant.

État général :

- ESP final : terminé et validé ;
- lignes, boîtes, couleurs et visibilité balistique : fonctionnelles ;
- cheats historiques : conservés ;
- phase armes V4 : entièrement validée par l'utilisateur ; rapidité, munitions
  illimitées, absence de recharge, stabilité de la vision et précision sont
  toutes fonctionnelles ;
- phase V12 validée en jeu pour la vitesse véhicule : hooks installés,
  multiplicateurs N/B (plancher 1.0x), restauration et libération de page
  propres — ce chemin est le nouveau comportement de référence à ne pas
  régresser ;
- après le test V13.1 : la carte s'ouvre enfin en jeu par K (maintien
  d'Espace validé) et toute la chaîne de clic fonctionne jusqu'à la
  sélection du sol ; les deux crashs ont ensuite été expliqués par les dumps
  locaux : Fullhands appelait `C_unknown::AddRef` au slot `+0x04` au lieu de
  `C_actor::cbProc` au slot `+0x0C`, ce qui laissait `111 * 12` octets sur la
  pile et déclenchait `FAST_FAIL_INVALID_EXCEPTION_CHAIN` ; la pose appelait
  `I3D_frame::SetPos` comme une méthode `thiscall` alors que l'interface
  installée est `I3DAPI __stdcall`, provoquant la lecture de `0xE4` dans
  `i3d2.dll + 0x2A49` ; correctifs V13.2 consignés en section 15.5 ;
- cause de l'échec V2 confirmée : le résolveur lisait `item + 0x2C` et
  `item + 0x3C`, deux adresses situées hors de l'objet d'inventaire de `0x20`
  octets, puis rejetait systématiquement l'arme avant toute écriture ;
- état actuel : la cadence utilise le compte à rebours signé local
  `actor + 0x260`, les munitions utilisent la réserve `item + 0x18` et le
  chargeur `item + 0x1C`, et la stabilité neutralise les six valeurs de
  dispersion `0x1D–0x22` ainsi que les trois vrais angles de recul
  `0x23–0x25` de l'arme sélectionnée ;
- binaire courant :
  `build\vs2026-x86\Release\HDFinalAdvancedV13_2.exe` (correctifs V13.2
  appliqués ; compilation Release/Debug et contrôles statiques réussis ;
  validation en jeu à faire) ;
- règle de suivi : aucune modification de code ne doit rester non documentée
  dans ce plan.

### Journal — ouverture de la nouvelle phase et règle de suivi (28 août 2026)

- L'utilisateur confirme que la V4 fonctionne désormais parfaitement : les
  deux cases armes précédentes sont le nouveau comportement de référence à ne
  pas régresser.
- Avant toute nouvelle implémentation, la demande complète a été retranscrite
  dans la section 13 de ce fichier.
- À partir de ce point, chaque modification de code, chaque offset confirmé,
  chaque outil de diagnostic ajouté, chaque compilation et chaque résultat de
  test sera ajouté à `plan.md` immédiatement après l'action concernée. La
  documentation ne sera pas reportée à la fin du travail.
- Aucun code de la nouvelle phase n'a été modifié avant cette retranscription.

### Journal — correction V4 du recul visuel (28 août 2026)

#### Résultat du test utilisateur V3

- La case **Tir ultra-rapide et munitions illimitées** est validée : cadence
  rapide fonctionnelle et balles illimitées confirmées en mission, y compris
  sur les pistolets testés.
- La dispersion de la case stabilité était neutralisée, mais la vision montait
  encore après chaque tir. La partie « sans recul » de la V3 n'était donc pas
  fonctionnelle et ne pouvait pas être considérée comme terminée.

#### Cause exacte de la montée de la vision

- Le désassemblage du chemin de tir montre qu'après la création du projectile,
  le jeu lit `actor + 0x254` pour choisir la posture `1`, `2` ou `3`.
- Il sélectionne ensuite un entier dans la table d'armes : propriété `0x23`
  pour la première posture, `0x24` pour la deuxième ou `0x25` pour la troisième.
- Cet entier est converti en angle, puis le jeu copie le vecteur source à
  `actor + 0x1D0`. Il retranche l'angle à l'élévation, ajoute ou retranche aussi
  une composante horizontale aléatoire, normalise le vecteur et appelle la
  méthode virtuelle `actor vtable + 0xF0` avec une durée de `1000 ms`.
- C'est cette rotation animée du vecteur de visée qui fait monter la caméra. Le
  champ `frame + 0xAC` utilisé par la V3 est une direction de frame dérivée ; le
  réécrire après coup ne supprime pas la source de l'animation de recul.
- La propriété entière `0x1C` de la table avait été attribuée à la secousse dans
  la V3 sans preuve directe. Le vrai chemin de recul n'y accède pas ; la V4 ne
  la modifie donc plus.

#### Confirmation en mémoire sur la mission active

- Relevé passif sur `hde.exe` PID `18692`, acteur local `0x0260E4A0`, arme
  sélectionnée ID `16`, table active `0x02524010`.
- Pendant que la stabilité V3 était cochée, les six propriétés de dispersion
  `0x1D–0x22` étaient toutes réellement à zéro. Cela confirme que la résolution
  et les écritures de table fonctionnaient.
- Au même instant, les propriétés de recul restaient non nulles : `0x23 = 3`,
  `0x24 = 2`, `0x25 = 1`. Cette observation correspond exactement aux trois
  accès du désassemblage et explique pourquoi la vision continuait à monter.

#### Correctif V4

- `src/weapon_mods.cpp` neutralise maintenant neuf valeurs réversibles pour
  l'ID sélectionné : six flottants de dispersion `0x1D–0x22` et trois entiers
  d'angle de recul `0x23–0x25`.
- L'écriture de la propriété `0x1C` de la table a été supprimée.
- La correction d'élévation sur `frame + 0xAC`, inefficace et appliquée sur une
  valeur dérivée, a été entièrement retirée. Le recul est désormais supprimé à
  sa source avant que le jeu construise la rotation de visée.
- La capture/restauration conditionnelle existante couvre les neuf valeurs lors
  de la désactivation, du changement d'arme, du changement de mission et de la
  fermeture normale.
- Toute la logique de rapidité, de réserve, de chargeur et de compte à rebours
  validée par l'utilisateur est conservée sans modification fonctionnelle.
- `tools/weapon_probe.ps1` relève maintenant aussi `0x23`, `0x24` et `0x25`.
- `CMakeLists.txt` produit un nom V4 distinct afin de ne pas écraser une V3 en
  cours d'exécution.

#### Compilation V4

- Configuration et compilation Release Visual Studio 2026 Win32/x86 réussies
  sans erreur.
- Binaire :
  `build\vs2026-x86\Release\HDFinalESPWeaponsV4.exe`.
- Vérifications : PE32/x86 (`Machine 0x014C`, magie PE32 `0x010B`), taille
  `397824` octets, SHA-256
  `D077AE71FAF8B134420CE49F30B4E9F9FD3103CE34A06E6912110FAFD424CB70`.
- Aucune occurrence de `CreateRemoteThread`, `VirtualAllocEx`,
  `SetWindowsHookEx`, `SendMessage` ou `PostMessage` dans le binaire final.
- Test restant : fermer normalement la V3 afin qu'elle restaure ses anciennes
  valeurs, lancer uniquement la V4, puis vérifier debout/accroupi/couché que la
  vision ne monte plus avec la stabilité seule et avec les deux cases actives.

### Journal — diagnostic et correction fonctionnelle V3 (historique : rapidité correcte, stabilité incomplète — 28 août 2026)

#### Cause exacte des deux cases sans effet

- Un relevé en lecture seule a été effectué sur la mission active, processus
  `hde.exe` PID `4212`, base module `0x00400000`.
- Joueur local observé : acteur `0x0267ACD0`, frame `0x0D1DDE20`, inventaire
  `0x025D73C0`, index sélectionné `1`, objet `0x025D75B0`.
- L'objet sélectionné est un Luger, ID `14`, avec réserve `11` à `+0x18` et
  chargeur `11` à `+0x1C`.
- L'ancienne validation interprétait `item + 0x2C` comme un type d'arme. La
  valeur réelle était `33`, donc supérieure à sa borne `9` : le résolveur
  retournait immédiatement `false`.
- Elle interprétait ensuite `item + 0x3C` comme un délai. La valeur réelle était
  `0x5F316E61`, car cette adresse appartient à une allocation voisine et non à
  l'objet d'inventaire : la seconde validation aurait également échoué.
- Conséquence confirmée : ni la case stabilité, ni la case rapide n'atteignait
  une seule instruction `WriteMemory` dans la V2.
- Le fichier `.tmp\probe.json` trouvé dans l'état précédent était vide
  (`0` octet) ; les anciens offsets n'avaient donc pas été validés par ce
  fichier malgré ce que la documentation antérieure laissait entendre.

#### Confirmation statique des vrais champs

- Le chemin de tir désassemblé lit l'index à `actor + 0x258`, l'inventaire à
  `actor + 0x5C`, l'ID à `item + 0x08` et décrémente les balles à
  `item + 0x1C`.
- Les fonctions d'inventaire confirment `item + 0x18` comme quantité/réserve et
  `item + 0x1C` comme balles/chargeur ; la taille allouée de l'objet est
  `0x20` octets.
- `actor + 0x260` est un compte à rebours **signé**. En mission au repos il
  valait `0xFFFFFFFB` (`-5`). La V2 le lisait comme non signé et considérait
  toute valeur non nulle comme un tir en cours, ce qui rendait aussi sa logique
  de stabilité incorrecte.
- Le jeu ne bloque un nouveau tir que si ce compteur signé est strictement
  supérieur à zéro. La V3 le remet donc à zéro pendant l'option rapide, sans
  écrire un faux « délai » dans l'objet et sans restaurer un ancien timer devenu
  obsolète.

#### Dispersion et recul confirmés

- La table d'armes active est résolue sans adresse de tas figée depuis
  `hde.exe + 0x10AAD0`, avec sélection de variante par
  `hde.exe + 0x1086E0`.
- La structure `C_table` et ses descripteurs de 8 octets ont été confirmés avec
  la source officielle locale d'Insanity3D (`Tabler2/ITabCore.h`) puis vérifiés
  sur la table vivante.
- La propriété `0x0C` identifie le mode de tir ; sa valeur pour le Luger était
  `2`, ce qui permet à la V3 de refuser les objets qui ne sont pas des armes à
  feu.
- La V3 avait attribué à tort la secousse de tir à l'entier `0x1C` (valeur
  Luger `1`). Les couples de dispersion étaient en revanche correctement
  identifiés comme les flottants `0x1D/0x1E`, `0x1F/0x20` et `0x21/0x22`
  (valeurs relevées `2.5/5`, `3/4`, `1/2`).
- La V3 capture ces sept valeurs puis les met à zéro tant que la case stabilité
  est active. Elle conserve en complément la correction d'élévation de
  `frame + 0xAC`, mais considère maintenant un tir actif seulement si le timer
  signé est positif ou si le bouton gauche est maintenu dans la fenêtre du jeu.
- Les six valeurs de dispersion appartiennent à la définition partagée du type
  d'arme. La V3 ne modifie que la ligne de l'ID actuellement sélectionné et la
  restaure, mais un autre acteur portant exactement le même ID d'arme peut être
  affecté pendant l'activation. Cette limite doit être vérifiée explicitement
  en mission ; elle ne doit pas être décrite comme une écriture strictement
  locale.

#### Nouvelle logique rapide et restauration

- La case rapide maintient séparément la réserve `item + 0x18` et le chargeur
  `item + 0x1C` à `1 000 000`, ce qui évite le chargeur vide et la transition de
  recharge, puis remet `actor + 0x260` à zéro à chaque mise à jour.
- Les munitions originales sont recapturées à chaque transition réelle de la
  case désactivée vers activée. Une désactivation/réactivation sans changement
  d'arme ne restaure donc plus une photographie trop ancienne.
- Aucun ancien compte à rebours n'est restauré : lorsqu'on désactive, le jeu
  reprend naturellement son délai normal au tir suivant.
- Si une écriture rapide échoue, toute écriture partielle réussie sur la réserve
  ou le chargeur est immédiatement annulée avec sa valeur capturée.
- Avant de restaurer les munitions, la V3 revérifie le PID, le pointeur d'objet,
  sa vtable et son ID. Elle ne réécrit donc pas une adresse réutilisée par un
  autre objet.
- Les valeurs de table ne sont restaurées que si elles contiennent encore le
  zéro appliqué par le trainer, afin de ne pas écraser une modification externe
  postérieure.
- Le changement d'arme, la perte du snapshot, le changement de mission, la
  désactivation et la fermeture normale déclenchent la restauration appropriée.
  Un changement de PID abandonne les anciennes adresses sans écrire dans le
  nouveau processus.

#### Outil de diagnostic et compilation

- `tools/weapon_probe.ps1` relève maintenant en lecture seule la fin et le
  nombre d'entrées du vecteur d'inventaire, l'ID sélectionné, la variante de
  table, les descripteurs et les valeurs des propriétés
  `0x08`, `0x0C`, `0x19`, `0x1C` à `0x25` avec la taille correcte de chaque
  type (`enum` sur 1 octet, `int/float` sur 4 octets).
- Le dernier relevé confirme un vecteur valide de `9` objets
  (`0x025D73C0..0x025D73E4`) et un index sélectionné `1`, donc les nouvelles
  bornes du résolveur V3 passent sur la mission active sans supposition.
- `CMakeLists.txt` produit désormais `HDFinalESPWeaponsV3.exe`, ce qui évite le
  verrou posé par l'ancienne instance V2 encore ouverte.
- Configuration et compilation Release Visual Studio 2026 Win32/x86 réussies
  sans erreur.
- Binaire :
  `build\vs2026-x86\Release\HDFinalESPWeaponsV3.exe`.
- Vérifications : PE32/x86 (`Machine 0x014C`, magie PE32 `0x010B`), taille
  `398336` octets, SHA-256
  `553BDE7B23569883631569F3EA7EDECDAD1A265C05F758C871776AB01DFC7438`.
- Recherche statique dans le binaire : aucune occurrence de
  `CreateRemoteThread`, `VirtualAllocEx`, `SetWindowsHookEx`, `SendMessage` ou
  `PostMessage`. L'architecture reste un trainer externe utilisant uniquement
  les lectures/écritures mémoire déjà isolées dans `TrainerProcess`.
- `README.md` référence maintenant la V3, les offsets corrigés et la limite de
  la définition de dispersion partagée.
- Il reste le test utilisateur en mission : chaque case séparément, les deux
  ensemble, plusieurs armes, désactivation, changement d'arme et ennemi portant
  le même modèle d'arme.

### Journal — démarrage de l'implémentation (28 août 2026)

- Ajout de `src/weapon_mods.h` : état `WeaponSettings` avec les deux options
  désactivées par défaut et API séparée de l'ESP.
- Ajout de `src/weapon_mods.cpp` : point d'intégration volontairement sans-op
  pendant la découverte ; aucune écriture mémoire n'est effectuée.
- `CMakeLists.txt` inclut ces deux fichiers et produit désormais
  `HDFinalESPWeapons.exe`, afin de préserver `HDFinalESPBallistic.exe` comme
  binaire de référence.
- Cette étape ne change ni l'ESP, ni les cheats, ni le comportement du jeu.
- `src/main.cpp` expose maintenant une section `Weapons / Armes` avec les deux
  libellés demandés, transmet `WeaponSettings` à la boucle principale et appelle
  le point d'intégration à chaque frame.
- La restauration est également appelée à la fermeture normale du trainer.
- Les cases sont encore sans effet, car `weapon_mods.cpp` reste volontairement
  sans-op tant que les offsets d'arme ne sont pas confirmés.
- Prochaine modification : compiler ce jalon UI, puis ajouter un probe externe
  borné pour relever l'arme locale, ses compteurs et ses timers en lecture seule.

### Journal — jalon UI compilé (28 août 2026)

- Compilation Release Win32/x86 réussie avec Visual Studio 2026, sans erreur.
- Nouveau binaire :
  `build\vs2026-x86\Release\HDFinalESPWeapons.exe`.
- Taille : `393216` octets ; SHA-256 :
  `5E7DD79257090C4283AAB5938952E20E2E0FF4CE3110DA23AF36BB11B8637BAD`.
- Le binaire ESP de référence `HDFinalESPBallistic.exe` n'a pas été écrasé.
- Prochaine modification : ajouter un outil de diagnostic en lecture seule pour
  relever des candidats d'arme sur le joueur local pendant une mission active.

### Journal — probe de découverte ajouté (28 août 2026)

- Ajout de `tools/weapon_probe.ps1`.
- Le probe retrouve la mission via `hde.exe + 0x10AD4C`, parcourt le vecteur
  d'acteurs connu, sélectionne l'acteur local (`actor + 0x2B8 == 1`) et relève
  ses champs sur `0x500` octets.
- Chaque mot de 32 bits est présenté comme entier/flottant ; les valeurs qui
  correspondent à une allocation distante sont signalées comme pointeurs avec
  leur vtable de premier niveau.
- Limites bornées : 4096 acteurs maximum et aucune écriture, injection, hook,
  thread distant ou appel de fonction du jeu.
- Le probe doit être lancé en mission active avec :
  `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\weapon_probe.ps1`.
- Prochaine étape : comparer plusieurs relevés contrôlés (arme rangée,
  équipée, tir, recharge, chargeur vide) avant de choisir un seul offset à
  modifier.

### Journal — vérification du probe (28 août 2026)

- Exécution syntaxique testée avec `ExecutionPolicy Bypass`.
- Le script s'arrête proprement avec le message attendu lorsque `hde.exe` est
  au menu et que le vecteur d'acteurs n'est pas encore initialisé.
- Aucun accès en écriture n'est présent dans l'outil ; un relevé exploitable
  devra être pris après le chargement d'une mission active.

### Journal — probe enrichi pour l'inventaire local (28 août 2026)

- `tools/weapon_probe.ps1` relève maintenant aussi `actor + 0x5C` (table des
  objets d'inventaire), `actor + 0x258` (index sélectionné) et le pointeur de
  l'objet sélectionné.
- Lorsque ce pointeur est valide, ses `0x300` premiers octets sont exportés
  sous forme entière/flottante pour comparer arme rangée, arme équipée, tir et
  recharge.
- Cette extension reste bornée et en lecture seule ; elle n'utilise toujours
  ni `WriteProcessMemory`, ni hook, ni appel distant.
- Prochaine étape : obtenir les relevés en mission active, puis confirmer la
  structure commune avant d'autoriser une première écriture réversible.

### Journal — candidats d'arme attachée ajoutés au probe (28 août 2026)

- Le probe exporte aussi les champs `actor + 0x244`, `+0x248` et `+0x24C`.
- Le désassemblage montre que `+0x248/+0x24C` sont utilisés comme deux slots
  d'objets attachés lors des changements d'équipement ; ils sont donc relevés
  comme candidats, sans être considérés comme confirmés.
- Aucun de ces champs n'est écrit et aucune modification active n'est encore
  appliquée par `weapon_mods.cpp`.

### Journal — contrôle de syntaxe (28 août 2026)

- Analyseur PowerShell : `tools/weapon_probe.ps1` se parse sans erreur.
- Aucun binaire de référence n'a été modifié depuis le jalon UI compilé.
- La prochaine donnée nécessaire est un JSON produit en mission active ; sans
  cette observation, écrire un offset serait une supposition et reste donc
  interdit.

### Journal — analyse statique des symboles d'armes (28 août 2026)

- Le fichier local `data\data.dta` contient une table de symboles historique
  mentionnant notamment `C_inventory::GetItem`, `GetItemAmount`, `SetBullets`,
  `Reload`, `C_human::IsReadyToFire` et `C_human::ShootCountDown`.
- Le désassemblage confirme la présence d'un inventaire rattaché à l'acteur et
  de slots d'objets ; il montre également un champ d'état autour de `+0x2B0`.
- Les adresses de la table de symboles ne correspondent pas uniformément à la
  révision installée (certaines tombent dans d'autres fonctions). Elles servent
  donc uniquement de guide de recherche, pas de cibles d'écriture.
- Décision de sécurité : aucune écriture ne sera activée sur ces champs avant
  comparaison de relevés en mission et validation de la révision exacte.

### État transmis avant l'implémentation mémoire (28 août 2026)

- Code C++ compilé : les deux cases sont visibles dans le nouveau binaire, mais
  leurs callbacks restent sans-op ; cocher/décocher ne modifie donc pas encore
  l'arme.
- Binaire à utiliser pour ce jalon :
  `build\vs2026-x86\Release\HDFinalESPWeapons.exe`.
- Le test nécessaire avant la prochaine modification C++ est de charger une
  mission, puis d'exécuter le probe dans quatre états : arme rangée, arme
  équipée, pendant un tir et pendant une recharge/chargeur vide. Conserver les
  sorties JSON séparément pour permettre une comparaison différentielle.
- Commande :
  `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\weapon_probe.ps1`
- Après réception de ces relevés, la prochaine modification sera inscrite ici
  immédiatement : confirmation des offsets, capture des valeurs originales,
  puis première écriture réversible limitée au joueur local.

### Journal — première implémentation réversible des cases armes (historique, invalidée par la V3 — 28 août 2026)

- `src/weapon_mods.cpp` n'est plus un no-op : il résout l'acteur local et son
  objet sélectionné depuis le snapshot radar, avec des limites strictes sur les
  pointeurs, l'index et les valeurs lues.
- Les offsets utilisés sont ceux confirmés par le désassemblage :
  `actor + 0x260` pour le compte à rebours de tir, `item + 0x1C` pour la
  quantité décrémentée à chaque balle, `item + 0x3C` pour le délai de l'objet,
  et `frame + 0xAC` pour la direction déjà lue par l'ESP.
- La case **Tir ultra-rapide et munitions illimitées** remet le compte à
  rebours à zéro, maintient la quantité à `0x7FFFFFFF` et neutralise le délai
  de l'objet sélectionné. Ces écritures restent limitées au joueur local.
- La case **Stabilité et précision 100 %** conserve les axes horizontaux et
  restaure l'élévation de référence pendant le compte à rebours du tir afin de
  neutraliser le coup de caméra vertical sans modifier les autres acteurs.
- Les valeurs originales du compte à rebours, de la quantité et du délai sont
  capturées puis restaurées lorsque l'arme change, lorsqu'une case est
  désactivée ou à la fermeture normale du trainer.
- Les champs de dispersion balistique interne ne sont pas écrits aveuglément :
  ils sont calculés par des accesseurs de la définition d'arme et ne disposent
  pas encore d'un offset mémoire direct confirmé. Le test utilisateur doit donc
  vérifier séparément le recul visuel, le regroupement des impacts, la cadence,
  la quantité et le retour au comportement normal.

### Journal — correction de compilation (28 août 2026)

- La première compilation a signalé une accolade de namespace manquante dans
  `src/weapon_mods.cpp`.
- L'accolade a été ajoutée ; aucune logique mémoire n'a été modifiée par cette
  correction.

### Journal — correction de liaison (28 août 2026)

- L'édition de liens a ensuite révélé que les deux fonctions publiques étaient
  restées dans le namespace anonyme des helpers.
- Elles ont été replacées dans `namespace hd` (les helpers restent privés), sans
  changement du comportement mémoire.

### Journal — restauration conditionnelle (28 août 2026)

- La revue du premier binaire a détecté que la branche « case décochée »
  réécrivait les valeurs originales à chaque frame.
- Cette logique a été corrigée avec l'état `rapid_values_applied` : les valeurs
  ne sont restaurées qu'une seule fois lors de la désactivation effective, ou
  lors d'un changement d'arme. Le compte à rebours normal n'est donc plus
  écrasé quand l'option est inactive.

### Journal — compilation du binaire de test (28 août 2026)

- Compilation Release réussie en Win32/x86 après ces corrections.
- Comme l'ancien `HDFinalESPWeapons.exe` était encore verrouillé par une
  instance en cours, la sortie de test a été produite sous :
  `build\vs2026-x86\Release\HDFinalESPWeaponsV2.exe`.
- Vérifications du fichier : PE32/x86 (`Machine 0x014C`), taille `395264`
  octets, SHA-256
  `06FA3E75611927EF0984C125CC90CCC96E8AAD5660E38E9933666C4FB6D06B6A`.
- La recherche statique ne trouve aucun `CreateRemoteThread`, hook, allocation
  distante, `LoadLibrary`, `SendMessage` ou `PostMessage`. La seule occurrence
  de `WriteProcessMemory` reste l'implémentation générique appelée par le
  nouveau module d'armes pour les adresses locales capturées.

### Journal — validation de la sentinelle de munitions (28 août 2026)

- La relecture de l'objet sélectionné rejetait initialement la sentinelle
  `0x7FFFFFFF` après sa première écriture, ce qui pouvait couper l'effet à la
  frame suivante.
- La borne accepte maintenant cette seule valeur spéciale, tout en continuant
  à refuser les quantités arbitrairement élevées. L'option rapide reste ainsi
  active pendant toute la durée de son activation.

### Journal — documentation utilisateur (28 août 2026)

- `README.md` décrit maintenant les deux cases du binaire `HDFinalESPWeaponsV2`
  et précise la capture/restauration ainsi que la limite connue sur la
  dispersion interne.

### Journal — compatibilité des deux cases simultanées (28 août 2026)

- La correction de stabilité utilise maintenant le bouton de tir gauche en plus
  du compte à rebours mémoire. Cela évite que la remise à zéro du compte à
  rebours par la case rapide empêche la neutralisation de l'élévation.
- La direction de référence est actualisée hors tir et l'écriture corrective
  reste limitée à l'axe vertical du frame local pendant le tir.

### Journal — compilation finale après correction du tir combiné (28 août 2026)

- Compilation Release Win32/x86 réussie.
- Binaire à tester : `build\vs2026-x86\Release\HDFinalESPWeaponsV2.exe`.
- Vérifications : PE32/x86 (`Machine 0x014C`), taille `395264` octets,
  SHA-256
  `D2CE8EA69AA89A7532379098F46A9F93164C06A7C30FCB5B294BA48C2716CB39`.

### Journal — mise en cohérence du plan (28 août 2026)

- Les paragraphes qui décrivaient encore `WriteMemory` comme « jamais appelée »
  et les cases comme un simple futur emplacement ont été corrigés.
- La section de tests s'applique maintenant explicitement au binaire V2 livré.

## 2. Phase ESP terminée

### 2.1 Fonctions validées

L'utilisateur a testé la version finale et confirme que tout fonctionne :

- ennemis atteignables par un tir : verts ;
- ennemis complètement protégés par un mur solide : rouges ;
- surfaces que les balles du jeu peuvent traverser : elles ne provoquent plus
  de faux rouge ;
- une petite partie touchable suffit pour obtenir le vert, notamment le visage,
  le haut de la tête ou une épaule ;
- faux positifs de la caméra troisième personne corrigés grâce à une origine
  située aux yeux du joueur ;
- alliés bleus ;
- snaplines, boîtes, checkboxes et overlay correctement conservés ;
- aucune régression signalée sur les cheats historiques.

### 2.2 Architecture finale de l'ESP

Le vert exige deux conditions :

1. le visuel de l'ennemi a été rendu dans les 100 dernières millisecondes ;
2. au moins une des 16 zones touchables échantillonnées est accessible depuis
   les yeux du joueur selon le BSP et la logique de matériaux balistiques.

Les 16 zones sont réparties ainsi :

- visage et tête : 9 points ;
- épaules : 3 points ;
- torse : 3 points ;
- abdomen : 1 point.

Le callback balistique `hde.exe!0x00435920` et la table située à
`hde.exe + 0x10AC04` sont reproduits en lecture externe. Dans la mission ayant
servi au diagnostic, les matériaux traversables étaient
`12, 32, 33, 37, 38, 41, 42, 44`.

### 2.3 Binaire ESP de référence

Binaire validé :

```text
C:\Users\user\Desktop\HD_Radar_Project\build\vs2026-x86\Release\HDFinalESPBallistic.exe
```

Contrôles :

- architecture : PE32/x86, `Machine 0x014C` ;
- taille : `393728` octets ;
- SHA-256 :
  `BBFE167A58128260DBB5C85D3D0D2A7DA87A5E60ABFCB50DD477E35CC291CDDF` ;
- compilation Release réussie ;
- aucun import `WriteProcessMemory`, `CreateRemoteThread`, `VirtualAllocEx` ou
  `SetWindowsHookEx` dans ce binaire.

La version précédente reste également conservée :

```text
build\vs2026-x86\Release\HDFinalESPFirstPerson.exe
```

SHA-256 :
`0E7B8E14DD9A62D39A66A5CC9B340660717A6FB7B9E451A2796B018AE7FF8B04`.

### 2.4 Règle de conservation

La phase armes ne doit pas modifier le comportement ESP validé. En particulier,
ne pas réécrire sans nécessité :

- la résolution mission/scène/acteurs ;
- les timestamps de visibilité ;
- l'extraction BSP et le BVH ;
- la table de matériaux balistiques ;
- les 16 zones touchables ;
- le world-to-screen ;
- la géométrie des lignes et boîtes ;
- les couleurs rouge, vert et bleu.

## 3. Nouvel objectif : options d'armes

Deux nouvelles cases à cocher indépendantes doivent être ajoutées au menu. Elles
doivent être désactivées par défaut et ne concerner que le joueur local.

Interface proposée :

```text
Weapons / Armes
[ ] Stabilité et précision 100 % (Sans recul ni dispersion)
[ ] Tir ultra-rapide et munitions illimitées (Sans recharge)
```

Les textes pourront être raccourcis si la largeur du menu l'exige, mais les deux
fonctions doivent rester séparées et compréhensibles.

## 4. Option 1 : stabilité et précision 100 %

### 4.1 Résultat attendu

Quand la première case est activée :

- l'arme ne doit plus remonter verticalement pendant les tirs ;
- aucun déplacement horizontal aléatoire ne doit dévier l'arme ;
- la caméra ou le viseur ne doit pas recevoir de recul perceptible ;
- la dispersion des projectiles doit être supprimée ;
- les balles doivent suivre exactement le point visé ;
- les tirs successifs doivent rester groupés au même endroit ;
- aucune balle ne doit être gaspillée à cause du recul, du bloom ou de la
  dispersion de l'arme.

Cette option regroupe donc deux mécanismes qu'il faudra identifier séparément :

1. `No Recoil` : suppression du mouvement de recul de l'arme/caméra ;
2. `No Spread` ou précision 100 % : suppression de l'angle aléatoire appliqué à
   la trajectoire des projectiles.

Il ne faut pas considérer la fonctionnalité terminée si seul le modèle de
l'arme semble stable alors que les impacts restent dispersés, ou inversement.

### 4.2 Comportement désactivé

Quand la case est décochée, les valeurs originales de l'arme doivent être
restaurées immédiatement. Le recul et la dispersion d'origine doivent revenir
sans obliger à relancer la mission ou le jeu.

## 5. Option 2 : tir ultra-rapide et munitions illimitées

### 5.1 Résultat attendu

Quand la seconde case est activée :

- la cadence de tir doit devenir très rapide ;
- le délai ou cooldown entre deux projectiles doit être fortement réduit ;
- le comportement doit fonctionner avec les pistolets et, si les mêmes
  structures sont partagées, avec toutes les armes à feu du jeu ;
- le chargeur ne doit pas se vider ;
- aucune recharge ne doit interrompre les tirs ;
- les munitions disponibles doivent rester illimitées ;
- le joueur doit pouvoir continuer à tirer rapidement sans attendre une
  animation de recharge.

Cette case combine trois sous-fonctions techniques :

1. `Rapid Fire` : réduction du délai entre les tirs ;
2. `Infinite Ammo` : maintien du chargeur et/ou de la réserve ;
3. `No Reload` : empêcher l'entrée dans l'état de recharge lorsque l'option est
   active.

Le nouvel agent devra vérifier si les armes semi-automatiques tirent en continu
quand le bouton est maintenu. Si le jeu impose un nouveau clic pour chaque tir,
il faudra d'abord identifier le champ de mode de tir ou de verrouillage d'entrée
approprié. Ne pas ajouter arbitrairement une boucle `SendInput` sans avoir
confirmé que c'est nécessaire et sans l'isoler proprement.

### 5.2 Comportement désactivé

Quand la case est décochée :

- cadence originale restaurée ;
- compteur réel de munitions de nouveau utilisé ;
- recharge normale réactivée ;
- aucune valeur artificielle ne doit rester dans l'arme équipée.

## 6. Informations résolues et limite restante

Les champs nécessaires aux deux cases sont maintenant confirmés pour la révision
documentée de `hde.exe` :

- arme équipée : `actor + 0x244` ;
- index sélectionné : `actor + 0x258` ;
- inventaire : vecteur `actor + 0x5C/+0x60` ;
- ID de l'objet : `item + 0x08` ;
- réserve : `item + 0x18` ;
- balles du chargeur : `item + 0x1C` ;
- autorisation temporelle du tir : compte à rebours signé `actor + 0x260` ;
- mode d'arme à feu : propriété de table `0x0C` ;
- dispersion suivant les trois postures : propriétés `0x1D` à `0x22` ;
- recul horizontal/vertical suivant les trois postures : propriétés entières
  `0x23`, `0x24` et `0x25`, appliquées depuis le vecteur `actor + 0x1D0`.

Les adresses de tas ne sont pas figées dans le code. La V4 repart à chaque frame
de l'acteur local du snapshot, valide le vecteur d'inventaire et résout la table
d'armes depuis deux RVA propres à la révision documentée.

La seule limite fonctionnelle connue est que les propriétés `0x1D` à `0x25`
sont stockées dans une définition commune indexée par ID d'arme. La ligne du
modèle sélectionné est capturée et restaurée de façon réversible, mais un acteur
portant exactement le même modèle peut partager temporairement ces paramètres.
Le test en mission doit établir l'impact réel avant de déclarer l'option
strictement locale et la phase terminée.

## 7. Plan de recherche pour le prochain agent

### Étape A — préserver la référence validée

1. Lire entièrement `README.md` et ce fichier avant toute modification.
2. Vérifier que `HDFinalESPBallistic.exe` possède toujours le hash documenté.
3. Ne pas écraser ce binaire pendant les expérimentations.
4. Choisir un nouveau nom de sortie, par exemple
   `HDFinalESPWeapons.exe`, jusqu'à validation utilisateur.

### Étape B — cartographier le chemin de l'arme locale

1. Repartir de l'acteur joueur déjà identifié dans `ReadRadarSnapshot`.
2. Examiner les champs de `C_actor`, de l'inventaire et de l'arme équipée dans
   `hde.exe` par désassemblage et lecture passive.
3. Comparer plusieurs états contrôlés : arme rangée, arme équipée, tir,
   changement d'arme, recharge et chargeur vide.
4. Relever les valeurs avant/après sans écrire dans le processus pendant cette
   phase de découverte.
5. Confirmer chaque offset dans au moins deux armes ou pistolets différents.

Le Pointer Scanner de Cheat Engine a déjà provoqué des blocages/redémarrages du
PC dans une phase précédente. Ne pas demander un nouveau scan massif à
l'utilisateur. Utiliser des probes bornés, le désassemblage local et les
structures déjà résolues.

### Étape C — isoler recul et dispersion

1. Retrouver la fonction qui construit la direction du projectile.
2. Identifier l'ajout aléatoire responsable de la dispersion.
3. Identifier séparément la fonction qui applique le kick à l'arme ou à la
   caméra.
4. Déterminer si les valeurs sont constantes dans la définition de l'arme ou
   recalculées dans son instance à chaque tir.
5. Tester une première écriture minimale et réversible uniquement après double
   confirmation des champs.

### Étape D — isoler cadence, munitions et recharge

1. Identifier le timer/cooldown lu avant qu'un tir soit accepté.
2. Identifier le compteur décrémenté après le tir.
3. Identifier la transition vers l'état de recharge.
4. Vérifier la différence entre chargeur et réserve.
5. Vérifier le comportement d'une arme semi-automatique avec le bouton maintenu.
6. Concevoir une modification commune qui ne dépende pas d'un seul modèle de
   pistolet.

### Étape E — intégrer les deux checkboxes

1. Ajouter deux booléens clairement nommés dans l'état/réglages approprié.
2. Ajouter une section `Weapons / Armes` dans `src/main.cpp` sans déplacer ou
   supprimer les contrôles existants.
3. Garder les cases désactivées par défaut.
4. Appliquer les modifications uniquement au joueur local et à son arme
   actuellement équipée.
5. Détecter les changements d'arme, de mission et de PID.

### Étape F — restauration obligatoire

Avant la première écriture sur une arme, mémoriser ses valeurs originales. Les
restaurer lorsque :

- la checkbox correspondante est décochée ;
- le joueur change d'arme ;
- le joueur meurt ou réapparaît ;
- la mission change ;
- `hde.exe` se ferme ou change de PID ;
- le trainer se ferme normalement.

Ne pas restaurer une ancienne valeur dans une adresse qui a été réutilisée par
un autre objet. La restauration doit vérifier que l'instance et le contexte
sont encore ceux qui ont été sauvegardés.

## 8. Architecture et sécurité de la phase armes

Contrairement à l'ESP, ces effets nécessiteront probablement des écritures
mémoire externes. Cela devra être assumé et isolé :

- utiliser uniquement l'API externe existante de `TrainerProcess` ;
- ne pas injecter de DLL ;
- ne pas installer de hook ;
- ne pas créer de thread distant ;
- ne pas appeler une fonction interne du jeu ;
- limiter cadence et munitions aux champs confirmés de l'acteur/objet local ;
- limiter la stabilité à la ligne de définition de l'ID sélectionné, restaurer
  cette ligne et conserver explicitement la réserve sur son caractère partagé ;
- valider toutes les adresses, tailles et valeurs avant chaque écriture ;
- restaurer les valeurs originales dès que l'option cesse de s'appliquer.

La méthode générique `TrainerProcess::WriteMemory` est maintenant appelée par
`weapon_mods.cpp` uniquement après validation des structures et capture des
valeurs originales. Elle ne sert pas au chemin ESP.

La conception recommandée est devenue le composant séparé
`WeaponModifierState`, chargé de :

- résoudre l'arme courante ;
- capturer les valeurs originales ;
- appliquer seulement les options cochées ;
- détecter un changement d'instance ;
- restaurer proprement ;
- exposer un statut minimal au code principal sans créer un nouvel inspecteur
  mémoire dans l'interface finale.

## 9. Tests obligatoires du binaire V4

### Stabilité et précision

1. Tirer plusieurs balles au même point contre un mur, option désactivée, pour
   observer la dispersion normale.
2. Activer l'option et répéter : impacts groupés au point visé.
3. Vérifier que l'arme et la caméra ne montent plus.
4. Tester tir unique, rafale et tir prolongé.
5. Décocher : recul et dispersion normaux immédiatement restaurés.

### Tir rapide et illimité

1. Mesurer la cadence normale d'un pistolet.
2. Activer l'option : cadence nettement accélérée.
3. Continuer au-delà de la capacité normale du chargeur.
4. Confirmer qu'aucune recharge n'interrompt le tir.
5. Tester au moins deux pistolets et une autre famille d'arme si disponible.
6. Décocher : cadence, munitions et recharge normales restaurées.

### Robustesse et régressions

1. Activer chaque option séparément, puis les deux ensemble.
2. Changer d'arme avec les options actives.
3. Ranger et reprendre l'arme.
4. Mourir/recommencer et changer de mission.
5. Fermer le trainer avec une option active, puis vérifier le retour normal du
   jeu.
6. Vérifier les tirs d'un ennemi portant une arme différente, puis exactement
   le même modèle que le joueur, afin de mesurer la limite de table partagée.
7. Vérifier que l'ESP final reste strictement identique.
8. Vérifier tous les cheats historiques et leurs raccourcis.
9. Vérifier l'absence de crash, compteur négatif, animation bloquée, son en
   boucle ou corruption d'inventaire.

## 10. Fichiers importants pour la reprise

| Fichier | Rôle |
|---|---|
| `src/radar.cpp` | Acteurs, ESP, visibilité balistique et BSP validés |
| `src/radar.h` | Snapshot et réglages ESP |
| `src/main.cpp` | Interface actuelle, cheats et cases armes |
| `src/trainer_process.cpp` | Connexion, lecture et méthode générique d'écriture |
| `src/trainer_process.h` | API du processus distant |
| `src/cheat_sequence.cpp` | Cheats historiques à préserver |
| `CMakeLists.txt` | Build x86 et nom du binaire |
| `tools/visibility_probe.ps1` | Diagnostic acteurs/frames en lecture seule |
| `tools/bsp_probe.ps1` | Diagnostic BSP/matériaux en lecture seule |
| `README.md` | Documentation de la version ESP livrée |
| `plan.md` | État complet et feuille de route de la phase armes |

La source officielle d'Insanity3D utilisée pendant l'analyse est référencée par
le projet et une copie temporaire peut encore exister sous `.tmp\Insanity3D`.
Elle aide pour les structures moteur, mais les classes d'armes propres à
Hidden & Dangerous doivent être confirmées dans `hde.exe`.

## 11. Compilation

Commande Visual Studio 2026 validée :

```powershell
cmake --build --preset release-x86-vs2026 --target HDPhase1
```

Pendant le développement, utiliser un nouveau `OUTPUT_NAME` afin de préserver
`HDFinalESPBallistic.exe` comme référence fonctionnelle.

## 12. Définition de terminé pour la prochaine phase

La phase armes sera terminée uniquement lorsque l'utilisateur aura confirmé :

- stabilité totale de l'arme et de la caméra ;
- trajectoires sans dispersion au point visé ;
- cadence très rapide sur les pistolets demandés ;
- munitions réellement illimitées ;
- aucune recharge forcée ;
- activation et désactivation instantanées par deux checkboxes séparées ;
- restauration correcte des valeurs originales ;
- fonctionnement après changement d'arme et de mission ;
- aucune modification des tirs des autres acteurs ;
- ESP, interface et cheats historiques toujours fonctionnels ;
- aucun crash ni état d'arme ou d'inventaire corrompu.

## 13. Nouvelle phase demandée après validation de la V4

### 13.1 Règles générales de cette phase

- Préserver sans régression l'ESP validé, les cheats historiques, la rapidité,
  les munitions illimitées, l'absence de recharge, la stabilité et la précision.
- Ajouter les nouvelles fonctions dans l'application existante sans supprimer
  les anciennes cases ni le système actuel de sélection d'objet.
- Désactiver les nouvelles cases par défaut.
- Afficher clairement les valeurs réglables et les touches directement dans
  l'interface.
- Détecter les conflits avec les raccourcis F8, F9 et F10 déjà disponibles dans
  les profils configurables historiques ; les fonctions réservées de cette
  phase doivent avoir une priorité explicite et ne doivent pas déclencher deux
  actions sur la même pression.
- Ne pas inventer d'offset. Confirmer les structures par désassemblage et par
  probes bornés en lecture seule avant toute première écriture.
- Capturer et restaurer les valeurs originales lors de la désactivation, d'un
  changement d'acteur/véhicule/mission/PID et à la fermeture normale.
- Après chaque modification, mettre immédiatement à jour le journal de cette
  section avec ce qui a été changé, ce qui a été vérifié et ce qui reste à faire.

### 13.2 Case 1 — Super Run / vitesse du joueur

Résultat demandé :

- ajouter une case dédiée à la vitesse de course du joueur ;
- dès que la case est cochée, afficher directement dans l'application un nombre
  représentant le multiplicateur de vitesse actuel ;
- la valeur minimale est la vitesse normale du joueur, affichée `1.0x` ; elle
  ne doit jamais descendre sous la vitesse normale ;
- `F8` augmente rapidement le multiplicateur, par pressions répétées ou maintien
  de la touche, jusqu'à `80.0x` ;
- `80.0x` est la vitesse maximale absolue, correspondant à quatre-vingts fois la
  vitesse de course normale du joueur ;
- `F9` réduit rapidement le multiplicateur, sans jamais descendre sous `1.0x` ;
- le changement doit être visible immédiatement dans le nombre affiché et dans
  le déplacement du joueur ;
- décocher la case restaure exactement la vitesse normale ;
- la modification doit viser seulement le joueur local et rester fonctionnelle
  après changement de mission ou réapparition.

Tests prévus : marche/course à `1.0x`, valeurs intermédiaires, maintien de F8
jusqu'à `80.0x`, maintien de F9 jusqu'à `1.0x`, désactivation en mouvement,
collisions, escaliers, intérieur/extérieur et changement de mission.

### 13.3 Case 2 — téléportation par carte avec F10

Résultat demandé :

- ajouter une deuxième fonction de téléportation liée à `F10` ;
- lorsqu'elle est disponible et que l'utilisateur appuie sur `F10`, afficher la
  carte de la mission dans l'application/overlay ;
- la carte doit être interactive et conserver une correspondance exacte entre
  ses coordonnées 2D et les coordonnées du monde 3D ;
- l'utilisateur clique n'importe où sur la carte et le joueur local est
  téléporté exactement à l'endroit choisi ;
- le point final doit utiliser une hauteur de sol sûre afin de ne pas placer le
  joueur sous la carte, dans un mur ou en chute ;
- le clic doit produire un retour visuel clair, puis permettre de fermer ou de
  masquer la carte ;
- aucune téléportation ne doit se produire si la mission, la carte ou le point
  de sol n'est pas résolu de façon valide.

Tests prévus : plusieurs zones ouvertes, bâtiments accessibles, différences de
hauteur, bords de carte, annulation sans clic, changement de mission et retour
du contrôle normal après téléportation.

### 13.4 Case 3 — invisibilité complète pour les ennemis

Résultat demandé :

- ajouter une case permanente, sans obligation d'utiliser une touche ;
- dès qu'elle est cochée, tous les ennemis doivent considérer le joueur comme
  invisible et très éloigné ;
- un ennemi ne doit pas détecter le joueur même en vision directe ou à très
  courte distance ;
- il ne doit ni s'alerter, ni viser, ni tirer, ni poursuivre, ni changer d'état
  à cause du joueur ;
- il doit continuer son comportement propre : marche, patrouille, attente ou
  autre état autonome, comme si le joueur n'était pas présent ;
- la fonction doit empêcher la réaction à la vue et à la proximité, pas
  seulement masquer le modèle graphique du joueur ;
- décocher la case doit rendre immédiatement la perception normale aux ennemis,
  sans bloquer définitivement leur IA.

Tests prévus : approche de face et de dos, contact très proche, plusieurs
ennemis, ennemi en patrouille, ennemi déjà alerté avant activation,
activation/désactivation répétée et changement de mission.

### 13.5 Case 4 — Aimbot tête sans tir automatique

Résultat demandé :

- ajouter une case `Aimbot` ;
- lorsqu'elle est cochée, seuls les ennemis déclarés visibles par l'ESP vert
  peuvent être ciblés ;
- la vision du joueur doit s'orienter automatiquement et exactement vers le
  visage/la tête afin de préparer un headshot ;
- l'aimbot ne doit jamais tirer automatiquement : le tir reste entièrement sous
  le contrôle de l'utilisateur ;
- le comportement doit fonctionner en troisième personne et en première
  personne ;
- si plusieurs ennemis verts sont présents, choisir en priorité celui qui est
  le plus proche de la direction de vision/réticule, et non simplement le plus
  proche en distance du joueur ;
- lorsque la cible courante meurt, disparaît, devient inaccessible ou n'est plus
  verte, passer directement à la prochaine cible verte la plus proche de la
  vision ;
- ajouter dans l'application une distance maximale réglable de `40` à
  `220 mètres`, avec une valeur initiale de `180 mètres` ;
- ignorer les ennemis hors de cette distance, même s'ils sont visibles ;
- ne jamais cibler le joueur local, un allié, un mort ou un acteur masqué par la
  visibilité balistique.

Tests prévus : une cible puis plusieurs cibles, ordre par proximité du viseur,
limites `40/180/220 m`, perte de visibilité verte, mort de la cible,
première/troisième personne, absence totale de tir automatique et restauration
immédiate du contrôle manuel à la désactivation.

### 13.6 Case 5 — Bullet Track avec cercle de ciblage

Résultat demandé :

- ajouter une case distincte `Bullet Track`, plus puissante que l'aimbot ;
- afficher un cercle de ciblage réglable à l'écran lorsque la case est active ;
- le centre exact du cercle représente le point actuellement visé par le
  joueur/réticule ;
- permettre de choisir dans l'application la taille du cercle ;
- seuls les ennemis verts de l'ESP dont la projection de tête se trouve dans ce
  cercle peuvent être sélectionnés ;
- tant qu'un ennemi valide se trouve dans le cercle, un tir manuel du joueur
  doit être redirigé/suivi jusqu'à sa tête et lui infliger un headshot, même si
  la direction réelle du tir passe loin de lui ;
- Bullet Track ne doit pas déplacer la vision et ne doit pas déclencher de tir
  automatique ; il agit seulement lorsqu'une balle est effectivement tirée par
  l'utilisateur ;
- s'il existe plusieurs ennemis verts dans le cercle, appliquer la même priorité
  que l'aimbot : tête la plus proche du centre du cercle, puis passer à la
  suivante lorsqu'elle n'est plus valide ;
- la portée doit être bornée et cohérente avec la visibilité/portée configurée,
  sans endommager un acteur hors cercle, un allié ou un ennemi non vert ;
- rechercher et confirmer le chemin exact de création/direction/dégâts du
  projectile avant de choisir entre redirection de projectile et application de
  dégâts ; aucune écriture spéculative n'est autorisée.

Tests prévus : cercle petit/grand, cible au centre et au bord, tir volontairement
à côté, plusieurs cibles, cible rouge hors visibilité, absence de cible,
première/troisième personne, aucun mouvement de caméra et aucun tir automatique.

### 13.7 Case 6 — Super vitesse des véhicules

Résultat demandé :

- ajouter une case de super vitesse pour tous les véhicules utilisables :
  voitures, camions et toute autre famille de véhicule du jeu ;
- reprendre la logique du Super Run : afficher immédiatement un multiplicateur,
  minimum `1.0x`, maximum `80.0x`, augmentation rapide et réduction rapide ;
- restaurer la vitesse normale dès la désactivation ou lorsque le joueur quitte
  ou change de véhicule ;
- ne modifier que le véhicule actuellement contrôlé par le joueur local, sans
  accélérer les véhicules de l'IA ;
- comme aucune paire de touches distincte n'a été imposée dans la demande, la
  première conception sera contextuelle : `F8/F9` règlent le joueur à pied et
  le véhicule lorsqu'il est conduit. Si les deux cases sont actives, chaque
  multiplicateur reste affiché et indépendant ; cette règle devra être validée
  pendant l'intégration et ajustée si le contexte du jeu ne permet pas de
  distinguer proprement les deux états.

Tests prévus : voiture, camion, autres véhicules disponibles, montée/descente,
virages, collisions, valeurs `1.0x/80.0x`, véhicule IA voisin et changement de
mission.

### 13.8 Touche ² — ajout instantané de tout le contenu du jeu

Résultat demandé :

- ajouter une nouvelle commande indépendante sur la touche `²` (touche physique
  sous Échap, résolue côté Windows sans dépendre du caractère saisi) ;
- une seule pression doit ajouter instantanément dans l'inventaire du joueur
  absolument tout ce qui existe dans le jeu : toutes les armes, toutes les
  munitions/objets, toutes les clés, tous les vêtements et toutes les autres
  catégories d'items ;
- l'ajout doit être réellement groupé/direct et immédiat, sans sélectionner les
  objets un par un avec la séquence lente existante ;
- conserver intégralement l'ancienne fonction qui permet de choisir un objet
  précis ;
- conserver également l'ancien mode `CollectAllItems` et ne pas modifier son
  comportement historique ; la touche `²` est une nouvelle méthode rapide et
  séparée ;
- résoudre la taille réelle du catalogue et la capacité de l'inventaire avant
  l'écriture, éviter les doublons destructifs et ne jamais dépasser les vecteurs
  alloués ;
- fournir un résultat visible dans l'application : succès, nombre d'items
  ajoutés ou erreur sûre si la mission/inventaire n'est pas disponible.

Tests prévus : inventaire partiellement rempli, inventaire vide, présence de
toutes les familles (armes, clés, vêtements), vitesse instantanée, conservation
de l'ancien sélecteur, changement de mission, sauvegarde/chargement et absence de
corruption ou de crash.

### 13.9 Ordre d'implémentation et journal obligatoire

Ordre initial, ajustable seulement si une dépendance technique confirmée
l'exige :

1. infrastructure de réglages, affichage et réservation sûre des touches ;
2. Super Run joueur avec `F8/F9` ;
3. carte interactive et téléportation `F10` ;
4. invisibilité de perception IA ;
5. Aimbot tête et curseur de portée `40–220 m` (`180 m` par défaut) ;
6. cercle et sélection de cible Bullet Track, puis interception sûre du tir ;
7. super vitesse contextuelle de tous les véhicules ;
8. ajout instantané de tout le catalogue sur `²` ;
9. compilation x86 sous un nouveau nom, tests de non-régression et livraison.

Après **chaque** ligne d'implémentation logique terminée ci-dessus, ajouter un
journal daté dans cette section avant de commencer la ligne suivante. Chaque
journal doit contenir : fichiers modifiés, structures/offsets confirmés,
comportement ajouté, restauration, compilation/test effectué, limites connues et
prochaine action exacte.

### Journal nouvelle phase — audit initial de l'interface et des touches (28 août 2026)

- Audit en lecture seule terminé avant modification du code.
- `src/main.cpp` autorise actuellement les touches configurables `F3` à `F12`.
  Les cinq cheats historiques utilisent `F3–F7` par défaut, mais un profil
  utilisateur peut actuellement affecter `F8`, `F9` ou `F10` à l'un d'eux.
- La nouvelle phase réserve donc `F8/F9` au multiplicateur contextuel et `F10`
  à la carte. Ces trois touches seront retirées de la liste configurable et un
  ancien profil qui les emploie sera ramené aux valeurs sûres `F3–F7`, afin
  qu'une pression ne déclenche jamais deux actions.
- La touche physique `²` sera détectée avec son code OEM/scancode, séparément
  de la saisie de caractères, pour fonctionner sur la disposition française.
- La boucle principale appelle déjà `ReadRadarSnapshot` une fois par frame puis
  les modificateurs d'arme avant le rendu ImGui. Un nouveau composant isolé de
  réglages/fonctions générales sera branché au même endroit sans modifier les
  composants ESP et armes validés.
- `RadarEntity` expose actuellement position, équipe et visibilité, mais pas
  l'adresse acteur/frame ni la tête exacte. Ces champs devront être enrichis
  avant l'Aimbot/Bullet Track ; ils ne sont pas nécessaires à l'infrastructure
  UI initiale ni au Super Run.
- Le mode inventaire historique `CollectAllItems` est confirmé comme une boucle
  de boîtes de dialogue et de touches (`Home`, `Down`, `Enter`) limitée à
  32 objets. Il sera conservé tel quel ; la future touche `²` ne réutilisera pas
  cette séquence lente.
- Prochaine modification exacte : ajouter le nouvel état de réglages, les
  contrôles UI, la gestion centralisée des touches réservées et un module
  général encore sans écriture mémoire, puis documenter immédiatement ce jalon.

### Journal nouvelle phase — création du module général (28 août 2026)

- Première modification de code de la nouvelle phase terminée et documentée
  avant de poursuivre.
- Ajout de `src/gameplay_mods.h` avec `GameplaySettings`, qui contient les états
  désactivés par défaut et les valeurs initiales demandées : vitesses joueur et
  véhicule `1.0x`, portée Aimbot `180 m`, cercle Bullet Track `180 px`, carte,
  invisibilité et états des deux ciblages.
- Ajout de `GameplayInput` pour centraliser F8/F9/F10/² et de
  `GameplayStatus` pour afficher un résultat sûr de la future commande
  d'inventaire instantané.
- Ajout de `src/gameplay_mods.cpp`. Ce premier jalon borne les multiplicateurs à
  `1.0–80.0x`, la portée à `40–220 m` et le cercle à `40–500 px`. Il gère
  uniquement l'état d'ouverture de la future carte ; il n'effectue encore
  aucune écriture dans `hde.exe`.
- La pression sur `²` est volontairement signalée comme non implémentée tant que
  le catalogue et le vecteur d'inventaire ne sont pas confirmés ; aucune fausse
  réussite ni écriture spéculative n'est produite.
- `CMakeLists.txt` inclut le nouveau module et réserve le nom de sortie
  `HDFinalAdvancedV1.exe`, distinct de la V4 validée.
- Prochaine modification exacte : brancher ces états dans `main.cpp`, ajouter
  les contrôles visibles et réserver réellement F8/F9/F10 dans les profils de
  raccourcis historiques.

### Journal nouvelle phase — branchement UI et touches réservées (28 août 2026)

- `src/main.cpp` inclut maintenant `gameplay_mods.h`, crée les réglages et le
  statut de la nouvelle phase, transmet le snapshot radar au module général à
  chaque frame et appelle sa restauration à la fermeture.
- Une section `Gameplay / Fonctions avancées` affiche désormais toutes les
  commandes demandées : Super Run et sa valeur, téléportation F10, invisibilité
  ennemis, Aimbot avec curseur `40–220 m`, Bullet Track avec cercle
  `40–500 px`, vitesse véhicule et commande `²`.
- Une fenêtre de carte sûre peut déjà être ouverte/fermée par F10 lorsque la
  case est cochée. Elle affiche explicitement que la résolution est en cours et
  n'effectue encore aucune téléportation.
- La boucle centrale détecte F8/F9 en maintien, F10 et `²` sur front de pression,
  seulement lorsque le jeu ou le trainer est au premier plan. La touche `²` est
  branchée via `VK_OEM_3`, correspondant à la touche physique sous Échap.
- La liste des raccourcis configurables historiques ne propose plus F8, F9 ni
  F10. `src/cheat_sequence.cpp` migre automatiquement un ancien profil qui
  utilise F1/F2/F8/F9/F10 vers les valeurs sûres F3–F7.
- Aucun effet gameplay distant n'a encore été ajouté dans ce jalon ; les armes
  et l'ESP ne sont pas modifiés.
- Prochaine action exacte : compiler ce jalon d'infrastructure, corriger toute
  erreur sans ajouter de fonctionnalité, inscrire le résultat ici, puis
  rechercher le multiplicateur de déplacement du joueur.

### Journal nouvelle phase — compilation du jalon infrastructure (28 août 2026)

- Configuration CMake et compilation Release Visual Studio 2026 Win32/x86
  réussies sans erreur.
- Binaire de jalon produit :
  `build\vs2026-x86\Release\HDFinalAdvancedV1.exe`.
- Ce binaire sert uniquement à valider l'intégration de l'interface et des
  touches ; aucune des nouvelles écritures mémoire n'y est encore activée.
- Prochaine action exacte : analyser le chemin de déplacement du joueur local,
  confirmer une valeur réversible de vitesse ou un déplacement par tick, puis
  implémenter uniquement Super Run avant de poursuivre les autres fonctions.

### Journal nouvelle phase — résolution du déplacement joueur (28 août 2026)

- Recherche en lecture seule terminée avant toute nouvelle écriture dans
  `hde.exe`. L'archive publique des sources H&D Deluxe a été placée uniquement
  dans `.tmp\HD_Deluxe_sources` comme documentation de travail ; aucun fichier
  du jeu installé n'a été remplacé.
- La source `Actors.cpp` confirme que `C_human::PlayAnim` charge les trois
  composantes de `move_dir` depuis la table d'animation, puis que
  `C_human::Tick` calcule le déplacement avec `to += dir * (tc.time * .001f)`
  avant d'utiliser le test de collision sphérique du moteur. Il n'existe donc
  pas de multiplicateur joueur global à modifier proprement.
- La disposition de la classe a été recoupée avec les champs déjà confirmés de
  l'exécutable installé : la source Release place `aim_dir` à `+0x1BC` et la
  sélection d'inventaire à `+0x244`, tandis que la version installée validée les
  place respectivement à `+0x1D0` et `+0x258`. Le décalage cohérent de `+0x14`
  place donc `move_dir` à `acteur + 0x1AC` (`x/y/z` à `+0x1AC/+0x1B0/+0x1B4`).
- L'implémentation Super Run multipliera uniquement les composantes
  horizontales locales `x/z`. La composante verticale `y`, la gravité, les
  sauts, les chutes et le chemin de collision natif resteront inchangés.
- Une valeur de base sera capturée pour chaque acteur/pose. Une nouvelle pose
  réinitialise naturellement `move_dir` depuis la table et sera recapturée ; la
  restauration n'écrira l'ancienne valeur que si le champ contient encore la
  dernière valeur posée par le trainer, afin de ne jamais écraser une transition
  d'animation effectuée entre-temps par le jeu.
- `F8/F9` feront varier le multiplicateur en maintien à vitesse constante,
  indépendamment du nombre d'images par seconde, avec bornes strictes
  `1.0x–80.0x`. Décocher la case ramènera l'affichage à `1.0x` et restaurera le
  mouvement en cours.
- Prochaine modification exacte : coder cet état réversible dans
  `src/gameplay_mods.cpp`, exposer un statut d'application dans
  `src/gameplay_mods.h`, puis journaliser immédiatement ces changements avant la
  compilation.

### Journal nouvelle phase — implémentation mémoire Super Run (28 août 2026)

- `src/gameplay_mods.cpp` applique maintenant le multiplicateur demandé au
  `move_dir` du seul acteur local fourni par le snapshot radar. Avant toute
  lecture/écriture, le module valide l'acteur, son frame et le retour
  `frame + 0x80 == acteur`.
- `F8/F9` modifient la valeur en maintien à `40.0x` par seconde avec un temps
  borné par frame ; la variation reste donc rapide et indépendante du nombre
  d'images par seconde. Les limites `1.0x` et `80.0x` sont réappliquées à chaque
  mise à jour.
- Seuls `move_dir.x` à `acteur + 0x1AC` et `move_dir.z` à
  `acteur + 0x1B4` sont multipliés. `move_dir.y`, la gravité et le moteur de
  collision restent intacts.
- Le contexte mémorise PID, acteur, pose, direction originale et dernière
  direction écrite. Un changement d'acteur ou de pose recapture la valeur
  native. Si le jeu remplace la direction entre deux frames, cette nouvelle
  valeur est reconnue comme native au lieu d'être multipliée exponentiellement.
- La désactivation et la fermeture restaurent `x/z` seulement si l'acteur est
  encore valide et si les valeurs présentes correspondent toujours à la
  dernière écriture du trainer. Une transition plus récente du jeu n'est jamais
  écrasée.
- En cas d'écriture partielle, la composante déjà écrite est annulée et le
  statut signale explicitement l'échec. Les directions non finies, poses hors
  plage et valeurs résultantes anormales sont refusées.
- `src/gameplay_mods.h` expose désormais `PlayerSpeedStatus` et
  `PlayerSpeedStatusText` pour distinguer désactivé, attente de mission, actif,
  disposition non reconnue et écriture refusée.
- Prochaine modification exacte : afficher ce statut juste sous la valeur
  Super Run dans `src/main.cpp`, journaliser ce petit branchement, puis compiler
  et contrôler le binaire x86 avant le test en mission.

### Journal nouvelle phase — statut visible Super Run (28 août 2026)

- `src/main.cpp` affiche maintenant sous la valeur `1.0x–80.0x` le statut réel
  retourné par le module : vitesse normale/désactivée, attente d'une mission,
  multiplicateur appliqué, disposition refusée ou échec d'écriture.
- L'utilisateur peut ainsi distinguer immédiatement une case cochée sans joueur
  disponible d'une application mémoire effective ; aucune autre section de
  l'interface n'a été modifiée dans ce jalon.
- Prochaine action exacte : relire les transitions de pose et la restauration,
  corriger tout risque de recapture d'une valeur déjà multipliée, puis compiler
  Release x86 et inscrire le résultat.

### Journal nouvelle phase — sûreté des transitions d'animation (28 août 2026)

- Relecture ciblée terminée et `src/gameplay_mods.cpp` renforcé avant la
  compilation.
- La source montre que `PlayAnim` affecte d'abord `curr_pose`, puis recharge
  `move_dir`. Si une lecture externe tombe dans ce très court intervalle, le
  module attend maintenant que la direction native change au lieu de capturer
  comme nouvelle base la direction multipliée de l'ancienne pose.
- La restauration vérifie désormais aussi que la pose courante correspond à la
  pose capturée. Une désactivation exactement pendant une transition ne peut
  donc pas remettre le vecteur d'une ancienne animation sur la nouvelle.
- Prochaine action exacte : compiler `HDFinalAdvancedV1.exe` en Release x86,
  traiter et journaliser toute erreur éventuelle, puis effectuer les contrôles
  statiques du binaire et préparer le test Super Run en mission.

### Journal nouvelle phase — compilation Super Run (28 août 2026)

- Compilation Release Visual Studio 2026 x86 réussie sans erreur avec le preset
  déclaré `release-x86-vs2026`.
- Binaire produit :
  `build\vs2026-x86\Release\HDFinalAdvancedV1.exe`, taille `402432` octets,
  horodaté le 28 août 2026 à 15:48:52.
- `dumpbin /headers` confirme `14C machine (x86)`, `32 bit word machine` et le
  sous-système Windows GUI. Le trainer reste donc compatible avec le processus
  PE32 du jeu.
- La première invocation de build avait seulement utilisé un nom de preset
  inexistant (`vs2026-x86-release`) ; elle n'a ni compilé ni modifié le projet.
  La relance avec le vrai preset a réussi.
- Limite de validation actuelle : `hde.exe` est connecté mais se trouve hors
  mission (vecteur d'acteurs vide), donc le chemin mémoire n'a pas été écrit
  pendant cette compilation. Le statut prévu sera « en attente » jusqu'à
  l'apparition du joueur local.
- Prochaine action exacte : effectuer une dernière revue statique du diff et des
  appels de restauration, corriger/journaliser toute anomalie, puis livrer ce
  binaire comme jalon de test Super Run avant de passer à la carte F10.

### Journal nouvelle phase — revue finale Super Run (28 août 2026)

- Revue statique complète de `gameplay_mods.cpp/.h`, du branchement dans
  `main.cpp` et de tous les appels de restauration terminée sans anomalie
  supplémentaire.
- Les bornes, validations de pointeurs/frame/pose, détection de changement
  d'animation, prévention de multiplication exponentielle, annulation d'une
  écriture partielle et restauration conditionnelle sont toutes présentes dans
  le chemin compilé.
- Le répertoire n'est pas un dépôt Git ; `git diff --check` n'est donc pas
  disponible. Une recherche directe confirme néanmoins l'absence de marqueurs
  de conflit dans les fichiers concernés, et la compilation `/utf-8` réussie
  valide les littéraux français ajoutés.
- Jalon Super Run prêt pour le test réel : cocher la case, maintenir F8 jusqu'à
  `80.0x`, vérifier marche/course/strafe et collisions, maintenir F9 jusqu'à
  `1.0x`, puis décocher pendant le mouvement et changer de personnage.
- Prochaine étape d'implémentation : résoudre la carte de mission, sa projection
  2D, la sélection au clic, le point de sol valide et le repositionnement sûr
  demandé sur F10. Le Super Run ne sera plus modifié sauf défaut observé en test.

### Journal nouvelle phase — résolution technique de la carte F10 (28 août 2026)

- Recherche en lecture seule terminée avant la première écriture de
  téléportation. `src/radar.cpp` charge déjà toutes les faces collisionnelles
  statiques du BSP de la mission et construit un BVH local pour la visibilité
  balistique. Cette même source sera réutilisée : aucune seconde carte
  approximative ni aucun nouvel offset de scène n'est nécessaire.
- La carte interactive projettera exactement les axes monde `X/Z` dans le
  rectangle ImGui, avec une seule transformation affine et son inverse pour le
  clic. Les limites proviendront des bornes réelles des triangles du BSP, et le
  joueur ainsi que les acteurs connus seront superposés dans le même repère.
- Le point de destination sera obtenu par un rayon vertical sur les triangles
  collisionnels. Seules les faces orientées comme un sol et de pente praticable
  seront acceptées ; le point praticable le plus haut sera choisi et l'acteur
  recevra une petite marge verticale afin d'éviter une insertion dans la face.
- La source officielle d'Insanity3D confirme que `I3D_frame::SetPos` écrit le
  membre local `pos` puis pose `FRMFLAGS_UPDATE_NEEDED`. Sur la révision
  installée, la matrice monde validée commence à `frame + 0x8C` (direction
  monde `+0xAC`, position monde `+0xBC`) ; la disposition de classe place donc
  le membre local `pos` à `frame + 0x14C`. L'écriture ne sera autorisée que si
  cette position locale est finie et correspond à la position monde actuelle,
  ce qui valide aussi que le frame racine n'est pas sous un parent transformé.
- Une demande invalide, un BSP absent, un clic sans sol praticable, un changement
  de mission/PID ou une disposition de frame non reconnue produira un statut
  visible sans écriture. En cas d'échec partiel, la position originale sera
  remise avant de signaler l'erreur.
- Prochaine modification exacte : exposer depuis `radar.h/.cpp` les limites et
  la projection de carte ainsi que la résolution verticale du sol, puis
  journaliser ce jalon avant de brancher l'écriture de position.

### Journal nouvelle phase — carte BSP interactive et sol praticable (28 août 2026)

- `src/radar.h` expose maintenant `TeleportMapResult` et
  `RenderTeleportMapCanvas`, sans modifier l'API ni le rendu ESP existants.
- `src/radar.cpp` calcule les limites `X/Z` depuis les faces de sol du BSP,
  conserve le ratio de la mission, applique la transformation monde/carte
  inverse au clic et dessine une vue topographique décimée à 14 000 triangles
  maximum. Le joueur, les alliés et les ennemis du snapshot sont superposés
  dans exactement le même repère.
- Une face est considérée praticable si sa normale respecte une pente maximale
  de 55 degrés. Le clic effectue un test barycentrique `X/Z` sur les faces
  candidates et retourne le point de sol valide ayant la plus grande hauteur ;
  un clic hors carte ou sans face praticable retourne explicitement
  `NoWalkableGround`.
- Cette modification est encore uniquement locale et en lecture seule : elle
  ne repositionne pas le joueur. Aucun offset ESP/armes ni aucun ancien cheat
  n'a été modifié.
- Limite connue : lorsqu'un bâtiment possède plusieurs étages au même `X/Z`,
  la carte choisit volontairement le sol praticable le plus haut, faute de
  troisième coordonnée dans un clic 2D.
- Prochaine modification exacte : ajouter la requête de destination et le
  statut de téléportation à `gameplay_mods.h`, effectuer l'écriture réversible
  et validée du frame local dans `gameplay_mods.cpp`, puis brancher le canvas et
  son retour visuel dans `main.cpp`.

### Journal nouvelle phase — branchement complet de la téléportation F10 (28 août 2026)

- `src/gameplay_mods.h` contient maintenant une destination de sol en attente,
  l'énumération `TeleportStatus` et son texte de statut visible.
- `src/gameplay_mods.cpp` consomme une demande une seule fois. Il revalide le
  joueur local, `actor + 0x28`, le retour `frame + 0x80`, la position monde
  `frame + 0xBC`, la position locale `frame + 0x14C` et les flags
  `frame + 0x0C`. L'écriture n'est autorisée que si les positions locale et
  monde correspondent à 5 cm près et si toutes les valeurs sont finies.
- La destination reçoit une garde verticale de `0.08 m`, puis le module écrit
  le membre local et pose le bit Insanity3D `FRMFLAGS_UPDATE_NEEDED`
  (`0x00800000`). Si la seconde écriture échoue, la position locale originale
  est immédiatement restaurée ; aucun état persistant n'est laissé par une
  téléportation réussie.
- `src/main.cpp` remplace le texte provisoire par la vraie carte interactive,
  affiche la légende et le statut, transmet le snapshot courant, ferme la carte
  après une sélection valide et met en file la destination pour la frame
  suivante. Un clic sans sol reste ouvert et affiche une erreur explicite.
- Les cases ESP et armes validées ne sont pas modifiées. La compilation et la
  validation syntaxique de ce jalon n'ont pas encore été effectuées.
- Prochaine action exacte : compiler Release x86, corriger et journaliser toute
  erreur, contrôler le binaire, puis seulement commencer la recherche de la
  perception IA pour la case d'invisibilité.

### Journal nouvelle phase — compilation et contrôles du jalon carte F10 (28 août 2026)

- La première commande a signalé uniquement que `cmake` n'était pas dans le
  `PATH`; elle n'a compilé ni modifié aucun fichier. La relance avec le CMake de
  Visual Studio 2026 a réussi sans erreur.
- Binaire produit :
  `build\vs2026-x86\Release\HDFinalAdvancedV1.exe`, taille `408064` octets,
  SHA-256 `5D8E0481A988A87FE06E3C6D2315A34C0078080181DAE828B96369CA8C9C38AE`.
- `dumpbin` confirme `14C machine (x86)`, machine 32 bits et sous-système
  Windows GUI. Aucun marqueur de conflit n'est présent dans les sources.
- Le binaire final ne contient aucune occurrence de `CreateRemoteThread`,
  `VirtualAllocEx`, `SetWindowsHookEx`, `SendMessage` ou `PostMessage` ;
  l'architecture externe existante est conservée.
- Limite de validation : la compilation et les contrôles statiques sont
  terminés, mais un test en mission reste requis pour confirmer visuellement la
  couverture topographique et le repositionnement `frame + 0x14C` sur cette
  mission précise.
- Ligne 3 de l'ordre d'implémentation terminée côté code. Prochaine action
  exacte : analyser dans les sources et le binaire le drapeau d'invisibilité
  utilisé par `C_enemy::IsEnemy`, confirmer son offset installé et sa
  restauration, puis implémenter uniquement la case d'invisibilité IA.

### Journal nouvelle phase — résolution de la relation IA joueur/ennemi (28 août 2026)

- Le relevé passif de la mission active (PID `4740`) confirme le joueur local
  `0x024F9CD0` et sa vtable `0x004F6784`. La fonction virtuelle
  `C_player::IsEnemy` est l'entrée `vtable + 0xA4`, soit `0x0042A260` dans cette
  révision ; son prologue vivant correspond exactement au code source : lecture
  du type de l'acteur demandeur, puis test du groupe ennemi.
- `C_human::WatchHumans` appelle `a->IsEnemy(observer)` avant d'ajouter un acteur
  à la liste de perception. Forcer uniquement le retour de
  `C_player::IsEnemy` à faux retire donc le joueur local de la relation hostile
  sans masquer son modèle, sans déplacer sa position et sans modifier les
  tables/groupes des ennemis ou leur logique autonome.
- Aucun simple drapeau de données du joueur n'existe dans la source pour ce
  comportement : le commentaire « invisible » trouvé dans
  `C_enemy::IsEnemy` concerne le frame de l'ennemi lui-même et ne peut pas
  rendre le joueur invisible. Modifier les groupes partagés changerait les
  relations entre IA et est donc refusé.
- Le correctif choisi sera un patch de cinq octets strictement validé
  (`xor eax,eax; ret 4`) sur cette seule fonction virtuelle. Les octets originaux
  seront capturés, le PID/vtable/prologue seront revérifiés avant pose, et la
  restauration ne se fera que si le patch est encore présent.
- Limite identifiée avant implémentation : une entrée déjà accumulée dans une
  liste de perception peut survivre jusqu'au prochain recalcul IA. Le premier
  jalon empêchera toute nouvelle acquisition ; le test « ennemi déjà alerté »
  déterminera si une remise à zéro ciblée des listes est aussi nécessaire,
  sans deviner leur disposition.
- Prochaine modification exacte : ajouter une écriture temporairement
  protégée et avec vidage du cache d'instructions dans `TrainerProcess`, puis
  implémenter l'état réversible de ce patch dans `gameplay_mods.cpp` et son
  statut visible.

### Journal nouvelle phase — implémentation réversible de l'invisibilité IA (28 août 2026)

- `TrainerProcess` expose maintenant `WriteProtectedMemory`. La méthode change
  temporairement la protection de la plage exacte, réutilise
  `WriteProcessMemory`, restaure la protection originale puis vide le cache
  d'instructions. Elle n'alloue aucune mémoire, ne crée aucun thread, n'injecte
  aucun module et n'installe aucun hook.
- `src/gameplay_mods.cpp` résout `C_player::IsEnemy` uniquement depuis la vtable
  du joueur vivant (`+0xA4`), vérifie que la fonction appartient au module
  principal et compare les treize octets confirmés du prologue avant toute
  pose. Le patch minimal `32 C0 C2 04 00` retourne faux et nettoie correctement
  l'argument `thiscall`.
- L'état conserve PID, adresse et cinq octets originaux. Chaque frame active
  revérifie que le patch est toujours présent. La désactivation, la perte de
  mission, un changement de PID/fonction et la fermeture normale restaurent les
  octets uniquement si la mémoire contient encore exactement le patch du
  trainer ; une modification externe ultérieure n'est jamais écrasée.
- `src/gameplay_mods.h` et `src/main.cpp` ajoutent un statut visible distinct :
  désactivé, attente de mission, actif, révision non reconnue ou écriture
  refusée. La case reste désactivée par défaut.
- Limite connue inchangée : la non-acquisition est immédiate pour les nouveaux
  scans, mais le temps de retrait d'une cible déjà alertée doit être mesuré en
  mission avant d'affirmer que ce cas est totalement terminé.
- Prochaine action exacte : compiler le jalon x86, vérifier sur le processus
  actif la pose et la restauration exactes des cinq octets, journaliser le
  résultat, puis enrichir `RadarEntity` avec acteur/frame/tête pour l'Aimbot.

### Journal nouvelle phase — compilation du jalon invisibilité IA (28 août 2026)

- Compilation Release Visual Studio 2026 Win32/x86 réussie sans erreur ; tous
  les modules, y compris `TrainerProcess`, `gameplay_mods`, le radar et les
  armes, ont été reconstruits et liés dans
  `build\vs2026-x86\Release\HDFinalAdvancedV1.exe`.
- Une instance de `HDFinalESPWeaponsV4.exe` est actuellement ouverte. Le nouveau
  binaire n'a pas été lancé automatiquement afin de ne pas superposer deux
  trainers qui écrivent dans le même processus ; le test dynamique pose/retrait
  sera donc effectué avec une seule instance lors du test utilisateur.
- Les validations de signature et la restauration conditionnelle sont bien
  présentes dans le chemin compilé. Test restant : fermer V4, lancer AdvancedV1,
  cocher/décocher l'invisibilité et confirmer approche, tir, poursuite et ennemi
  déjà alerté.
- Prochaine modification exacte : ajouter aux entités radar les adresses
  acteur/frame, la posture et la position de tête déjà résolue par le parcours
  des frames, sans changer la décision rouge/verte, puis journaliser avant de
  coder la sélection Aimbot.

### Journal nouvelle phase — enrichissement ciblage du snapshot radar (28 août 2026)

- `src/radar.h` ajoute à chaque `RadarEntity` la position de tête, l'adresse de
  l'acteur, l'adresse de son frame et sa posture ; `RadarSnapshot` expose aussi
  le frame du joueur local.
- `src/radar.cpp` remplit ces champs uniquement depuis les acteurs déjà validés
  par `ReadActor`. Les morts restent exclus exactement comme avant. La tête
  utilise le plus haut frame proche résolu par le parcours du squelette ; le
  repli est la hauteur humaine bornée déjà employée par l'ESP.
- La position de tête est ajustée avec le même inset que le test balistique. Le
  bit `directly_visible` rouge/vert, le BVH et le rendu ESP ne changent pas : les
  nouvelles données ne font qu'exposer le résultat confirmé au futur ciblage.
- Cette modification est entièrement en lecture seule et ne change aucune
  valeur dans `hde.exe`. Compilation encore à effectuer avec le jalon Aimbot.
- Prochaine modification exacte : confirmer dans la source et le binaire le
  chemin `aim_dir`/orientation de vue, implémenter la sélection des seuls ennemis
  verts par angle au réticule puis appliquer/restaurer l'orientation sans tir
  automatique.

### Journal nouvelle phase — implémentation Aimbot tête (28 août 2026)

- La source confirme `C_human::aim_dir` à l'offset installé déjà validé
  `actor + 0x1D0`, et `aim_count` à `actor + 0x1E8`. La valeur native
  `AIM_COUNT` est `2500 ms`. `ApplyAimDir` applique ce vecteur au squelette et
  à `game_cam.SetDeltaDir` lorsque le frame du joueur est le focus, ce qui
  couvre les modes première et troisième personne sans déclencher de tir.
- Le snapshot expose maintenant aussi la position de visée du joueur. Le
  sélecteur Aimbot refuse alliés, morts, acteurs invalides, ennemis rouges,
  cibles hors écran et cibles au-delà du curseur `40–220 m`. Parmi les têtes
  restantes, il minimise la distance projetée au centre exact du réticule ; la
  priorité est donc bien l'angle de vision et non la distance monde.
- La direction monde vers la tête est transformée dans la base monde du frame
  joueur lue à `frame + 0x8C`, avec inversion de l'axe vertical identique à
  `C_human::AimedToDir`, puis normalisée et écrite dans `aim_dir` avec le compte
  natif `2500`.
- L'état capture direction et compte originaux. Il recapture une intervention
  manuelle du jeu pendant l'activation, annule toute écriture partielle et ne
  restaure que si les deux champs contiennent encore les dernières valeurs du
  trainer. La perte de cible verte, la désactivation, un changement d'acteur/PID
  et la fermeture rendent immédiatement la visée manuelle.
- `GameplayStatus` et l'interface distinguent désactivé, attente, aucune cible,
  actif, disposition invalide et écriture refusée. Aucun tir automatique ni
  champ de munitions/cadence n'est touché.
- Correction de validation découverte pendant cette modification : les
  positions monde de la carte et des têtes utilisaient par erreur la borne
  `100` réservée à un vecteur de direction. `gameplay_mods.cpp` sépare
  maintenant `IsSaneWorldPosition` (coordonnées jusqu'à dix millions) de
  `IsSaneMoveDirection`; cela autorise notamment la mission active à
  `z = -137` sans affaiblir les validations de vitesse.
- Prochaine action exacte : compiler et contrôler ce jalon, puis ajouter le
  cercle Bullet Track au vrai overlay de jeu et réutiliser la même sélection
  par tête avant d'étudier l'interception du tir.

### Journal nouvelle phase — cercle Bullet Track et analyse du tir (28 août 2026)

- `RadarRenderSettings` contient maintenant l'état et le rayon Bullet Track.
  `main.cpp` les synchronise avec la case et le curseur avant chaque rendu de
  l'overlay réel du jeu.
- `src/radar.cpp` dessine au centre exact du client de `hde.exe` un cercle jaune
  à double contour et son point central, borné à `40–500 px`. L'overlay reste
  visible avec le cercle seul même si les deux catégories ESP sont masquées ;
  la fenêtre demeure transparente aux clics.
- L'analyse de `C_human::UseItem` et `C_gun_shoot` confirme que le projectile
  reçoit `S_gun_shoot_init::dir`, puis que le constructeur appelle
  `Evaluate()` immédiatement pour le premier projectile. `Evaluate` calcule et
  fige `hit_actor`, `hit_frm`, `hit_dest`, la distance et les dégâts avant que
  l'acteur projectile apparaisse dans le vecteur de mission.
- Conséquence importante : modifier un projectile après l'avoir observé dans
  le vecteur serait trop tard pour le premier tir et ne peut pas garantir un
  headshot. Le magnétisme natif `ApplyShootMagnet` ne convient pas non plus : il
  vise le centre de tout acteur vivant, y compris allié, sans reprendre la
  décision verte ni le cercle demandé.
- La partie visuelle est implémentée, mais aucune fausse redirection n'est
  activée. Prochaine action exacte : résoudre dans le binaire le point juste
  avant `mission.CreateActor(ACTOR_GUN_SHOOT)` et concevoir une interception
  minimale, réversible et bornée qui remplace seulement `si.dir` par la
  direction de la tête verte sélectionnée ; si cela exige une allocation ou un
  hook, l'inscrire explicitement avant de modifier l'architecture externe.

### Journal nouvelle phase — dépendance Bullet Track et passage temporaire au véhicule (28 août 2026)

- L'interception correcte du premier projectile exige d'exécuter une petite
  logique dans le thread du jeu avant le constructeur de `C_gun_shoot`, donc une
  allocation distante et un détour de code. Cela changerait explicitement la
  contrainte historique « aucun hook / aucune allocation distante » du projet.
- Aucun hook n'est ajouté silencieusement. La partie redirection de Bullet Track
  reste en attente de cette décision architecturale, tandis que le cercle et la
  sélection de cible sûre peuvent rester actifs sans endommager une mauvaise
  cible.
- Pour continuer les fonctions indépendantes sans bloquer toute la phase,
  l'ordre 6/7 est temporairement inversé : la super vitesse véhicule sera
  implémentée avant le détour Bullet Track, puis l'interception sera reprise.
- La source `Vehicle.cpp` confirme que le joueur expose son objet utilisé dans
  `using_item` (offset installé `actor + 0x250`) et qu'un véhicule pilotable est
  `ACTOR_AUTOMOBIL = 16`. `C_automobil::speed` est un flottant en m/s, mis à
  jour par `SetSpeed`; la disposition de classe place ce champ à
  `automobil + 0x1B0` après les quatre roues et les huit sièges.
- La première implémentation validera type, frame, retour frame/acteur, vitesse
  finie et conducteur local avant écriture. Elle conservera le multiplicateur
  précédent pour démultiplier la vitesse observée et éviter une croissance
  exponentielle entre frames.
- Prochaine modification exacte : ajouter le statut véhicule, rendre F8/F9
  contextuels selon `actor + 0x250`, appliquer/restaurer `vehicle + 0x1B0` avec
  validations et annulation d'écriture, puis compiler.

### Journal nouvelle phase — implémentation Super vitesse véhicule (28 août 2026)

- `gameplay_mods.cpp` résout l'objet utilisé du joueur à `actor + 0x250`, exige
  `ACTOR_AUTOMOBIL (16)`, valide le frame et son retour acteur, puis parcourt les
  huit sièges (`vehicle + 0x12C`, taille `0x10`). La modification n'est active
  que si un siège porte le drapeau conducteur et référence exactement le joueur
  local à `seat + 0x0C` ; un passager et une IA voisine sont donc exclus.
- F8/F9 sont maintenant contextuels : à pied ils changent le multiplicateur
  Super Run joueur, au siège conducteur ils changent celui du véhicule. Les
  deux valeurs restent indépendantes, visibles et bornées `1.0x–80.0x` avec la
  même variation de `40x/s`.
- La vitesse signée en m/s à `C_automobil + 0x1B0` est lue et validée avant
  chaque écriture. Le module divise la valeur observée par le multiplicateur
  précédent avant d'appliquer le nouveau, ce qui préserve marche arrière et
  freinage natifs sans multiplication exponentielle.
- La sortie du siège, le changement de véhicule/PID, la désactivation et la
  fermeture démultiplient la vitesse courante pour rendre la valeur native.
  Les valeurs non finies, supérieures à `5000 m/s` en lecture ou `20000 m/s`
  après calcul sont refusées.
- `GameplayStatus` et l'interface affichent désactivé, attente du conducteur,
  actif, disposition invalide ou écriture refusée. `ACTOR_AUTOMOBIL` couvre
  voitures, camions et la variante bateau de la table véhicule ; les canons
  statiques ne sont volontairement pas traités comme des véhicules roulants.
- Prochaine action exacte : compiler Release x86 et contrôler ce jalon, puis
  résoudre le catalogue et la capacité du vecteur d'inventaire pour la touche
  `²`, sans supprimer l'ancien sélecteur ni `CollectAllItems`.
### Journal nouvelle phase — validation du jalon véhicule (28 août 2026)

- La cible `HDPhase1` a été recompilée avec succès en configuration
  `Release x86`; l'exécutable produit reste un PE 32 bits compatible avec
  `hde.exe`.
- Le trainer V4 historique restant ouvert, aucun second trainer n'a été lancé
  en parallèle : le test dynamique F8/F9 en véhicule reste donc à faire dans
  une session propre.
- Avant l'inventaire, une correction de sûreté est planifiée : suspendre
  explicitement la modification de course lorsque le joueur conduit, et
  revalider type/frame/retour acteur du véhicule avant toute restauration de
  sa vitesse afin d'éviter une écriture dans un objet recyclé.
- Prochaine modification exacte : appliquer ces deux gardes, recompiler, puis
  inspecter `C_inventory::AddItem`, `ProcessCheat` et la longueur réelle de
  `TAB_I_INV_ID` pour concevoir la touche `²` instantanée.
### Journal nouvelle phase — gardes joueur/véhicule validées (28 août 2026)

- Quand le joueur est reconnu comme conducteur, Super Run restaure désormais
  son dernier vecteur natif puis passe à l'état « suspendu en véhicule » ; le
  multiplicateur pédestre reste mémorisé et F8/F9 ne règlent que le véhicule.
- Avant de démultiplier une vitesse à la sortie, la restauration revalide
  désormais que l'adresse conserve le type automobile, un frame sain et le
  retour frame→acteur exact. Une adresse d'objet recyclée n'est plus écrite.
- La cible `HDPhase1` recompilée après ces gardes réussit en `Release x86` et
  produit `build/vs2026-x86/Release/HDFinalAdvancedV1.exe`.
- Prochaine action exacte : analyser les sources et le binaire installés pour
  identifier la taille du catalogue d'objets, les règles de
  `C_inventory::AddItem` et une voie instantanée sûre pour `²`.
### Journal nouvelle phase — inventaire complet réellement résolu (28 août 2026)

- `C_inventory::AddItem(itm, num)` est la voie native sûre : elle fusionne les
  objets empilables, alloue un vrai `S_item`, l'insère dans le vecteur et gère
  son comptage de références. Reconstruire ce vecteur par simples écritures
  externes créerait des objets incomplets et n'est donc pas retenu.
- Le `fullhands` historique de la source diffusée appelle `CB_CHEAT(9)` et ne
  donne qu'une liste fixe de l'ancien jeu, principalement les index `1–32`.
  Cela ne couvre pas tout le catalogue Deluxe et confirme la différence avec
  la nouvelle demande.
- Lecture passive de la table du jeu actif : l'édition courante expose une
  capacité de `1024` lignes mais seulement les lignes nommées `1–115` sont
  réellement définies. Elles incluent les armes Deluxe, les grenades, mines,
  jumelles, caméra, clé, radio, kit médical et toutes les variantes d'uniforme.
  La ligne `30` est « Free hands » et existe déjà par construction.
- Ajouter ces quelque 115 objets instantanément et correctement exige d'appeler
  `C_inventory::AddItem` dans le processus du jeu (une routine distante bornée)
  ou, à défaut, de rouvrir 115 fois la boîte `fullhands`, précisément le chemin
  lent refusé. La première solution change la contrainte historique « aucune
  allocation distante / aucun thread distant », comme le Bullet Track.
- Prochaine action exacte : résoudre par signature l'adresse installée de
  `C_inventory::AddItem` et préparer les validations du catalogue et de
  l'acteur, sans encore activer de code distant silencieusement.
### Journal nouvelle phase — chemin natif `²` confirmé dans le binaire installé (28 août 2026)

- Le binaire actif `hde.exe` est chargé à `0x00400000`. Son chemin
  `fullhands` installé a été désassemblé autour de `0x004A00A5` : il appelle
  `WinSelectItem("Select item", ...)`, puis transmet exactement l'index choisi
  au callback virtuel de l'acteur (`vtable + 0x30`, message `CB_CHEAT = 14`,
  commande `9`). Une seule ligne est donc ajoutée par invocation.
- Cette observation explique le comportement réel du trainer existant et la
  lenteur de `CollectAllItems`; la vieille source où la commande 9 contient une
  liste fixe `1–32` ne correspond pas à cette révision Deluxe installée.
- La voie correcte pour `²` est désormais précise : lire les lignes nommées de
  la table active, puis faire exécuter sur le thread principal du jeu une boucle
  bornée appelant le callback natif pour chaque index utile. Cela conserve les
  constructeurs, les quantités, les références et les particularités de cette
  révision, sans supprimer l'ancien sélecteur.
- Une exécution distante créée en parallèle du moteur n'est pas retenue : elle
  pourrait modifier le `std::vector` d'inventaire pendant son rendu. Il faut un
  détour temporaire sur le thread principal pour `²`; Bullet Track nécessite
  de son côté un détour persistant au moment où la direction du tir est figée.
- Ces deux derniers comportements franchissent explicitement l'ancienne limite
  architecturale « processus externe sans allocation distante et sans hook ».
  Le code courant reste donc inchangé sur ces deux points tant que ce changement
  d'architecture n'est pas autorisé explicitement.
### Journal de contrôle avant décision architecturale (28 août 2026)

- Une recompilation finale sans modification supplémentaire réussit :
  `HDFinalAdvancedV1.exe`, `415232` octets, configuration `Release x86`.
- Le dossier n'est pas un dépôt Git; la vérification repose donc sur le build,
  les validations mémoire intégrées et le journal détaillé de ce fichier.
- Travail restant : autorisation du changement d'architecture, puis
  implémentation/test du détour Bullet Track et du détour temporaire `²`; test
  dynamique en session propre des fonctions déjà compilées.
### Journal — retour du test utilisateur Advanced V1 (28 août 2026)

- **Super Run joueur** est validé parfaitement en jeu. Cette fonction devient
  une référence à préserver et ne doit plus être modifiée.
- **Téléportation** : F10 entre en conflit avec « Load mission » et provoque un
  crash. Le flux demandé est corrigé dans la spécification : une nouvelle
  touche libre doit ouvrir la carte native déjà présente dans le jeu; le joueur
  clique sa destination sur cette carte, sans choisir dans une carte intégrée
  au trainer, puis il est téléporté à cet emplacement exact.
- **Aimbot** : aucun effet observable en première ou troisième personne; il est
  considéré non fonctionnel, pas partiellement validé. La sélection, la base de
  transformation et le véritable chemin caméra/visée doivent être diagnostiqués
  dans la révision installée.
- **Bullet Track** : le cercle s'affiche mais la cible n'est pas verrouillée à
  la tête et il faut encore rapprocher manuellement la vision. Ce résultat est
  cohérent avec le build testé, où seul le cercle était implémenté : la vraie
  interception du tir reste à réaliser.
- **Invisibilité ennemis** : les ennemis voient encore le joueur et tirent. Le
  patch actuel de `C_player::IsEnemy` n'intercepte donc pas le chemin de
  perception utilisé par l'IA de cette révision; il doit être remplacé après
  analyse des appels de détection/réaction.
- **Touche `²`** : aucun inventaire complet n'est ajouté. Ce résultat est aussi
  cohérent avec le build testé, dont le statut restait « unsupported »; le
  chemin natif groupé documenté au jalon précédent reste à implémenter.
- Ordre de correction : carte native et touche libre, invisibilité IA, Aimbot,
  Bullet Track natif, puis inventaire Deluxe complet par `²`. Après chaque
  modification, ce journal sera mis à jour avant de poursuivre.
### Journal — F10 libérée et carte native amorcée (28 août 2026)

- La touche de téléportation passe de F10 à **F11**. F10 n'est plus réservée
  par le trainer et reste disponible pour « Load mission »; les anciens profils
  de hotkeys sont migrés pour interdire F11 au lieu de F10.
- La fenêtre « Carte de téléportation » de l'application a été supprimée du
  flux. F11 n'est accepté que lorsque la fenêtre du jeu est réellement active,
  puis le trainer envoie la commande Carte native par défaut (`Espace`, valeur
  confirmée dans `InitSystem.cpp`). Un second F11 referme la même carte.
- La prochaine partie de ce correctif doit capter le clic dans la carte native,
  lire la caméra/scene du `map_mgr` et convertir ce clic en X/Z monde avant de
  réutiliser l'écriture de téléportation déjà validée.
- Le build Release a compilé les sources mais n'a pas pu remplacer le fichier
  parce que l'ancienne Advanced V1 est encore ouverte (PID 9580). Un build
  `Debug x86` séparé a entièrement réussi, sans fermer la session utilisateur.
- Prochaine modification exacte : corriger la base de transformation Aimbot en
  utilisant `brest->parent->parent`, puis faire partager cette cible corrigée
  au Bullet Track.
### Journal — correction Aimbot et activation fonctionnelle du cercle (28 août 2026)

- La cause principale de l'Aimbot inerte a été corrigée : la source native ne
  transforme pas la direction par le frame racine du joueur, mais par la
  matrice de `brest->parent->parent`. Le trainer lit maintenant `actor + 0x1C8`,
  valide les deux parents (`frame + 0x18`) et utilise leur matrice monde.
- Le calcul de priorité emploie désormais les vraies dimensions en pixels du
  client de `hde.exe`, mémorisées dans `RadarSnapshot`, au lieu d'un écran
  normalisé artificiel `2×2`. La distance au centre correspond donc au viseur
  réellement affiché.
- Bullet Track n'est plus un cercle purement décoratif : lorsqu'il est activé
  seul, une tête ennemie verte doit être dans le rayon réglé en pixels; la même
  direction corrigée est alors imposée vers cette tête, jusqu'à `220 m`. Si
  Aimbot et Bullet Track sont cochés ensemble, une cible présente dans le
  cercle Bullet Track est prioritaire.
- Un statut Bullet Track dédié distingue désactivé, attente, aucune tête dans
  le cercle, verrouillage actif, disposition invalide et écriture refusée.
- Le build `Debug x86` réussit après cette correction. Le vrai détour du
  projectile reste nécessaire si le canon du modèle ne suit pas assez vite la
  direction imposée pendant le test; ce jalon permet déjà de vérifier le vrai
  défaut de transformation indépendamment du hook de tir.
- Prochaine action exacte : remplacer l'invisibilité limitée à `IsEnemy` par un
  chemin qui purge également les cibles IA déjà reconnues, sans désactiver les
  déplacements ou animations normales des ennemis.
### Journal — invisibilité IA étendue aux réactions déjà actives (28 août 2026)

- Le patch de relation `C_player::IsEnemy` est conservé pour bloquer toute
  nouvelle acquisition du joueur.
- À chaque mise à jour active, le trainer parcourt maintenant uniquement les
  acteurs ennemis validés par le radar et vide leur `watch_actors` à
  `enemy + 0x298`. Cette disposition est cohérente avec les champs installés :
  `program + 0x284`, drapeaux `+0x290/+0x291`, compteur `+0x294`, puis vecteur
  de surveillance `+0x298`.
- Les commandes IA dont `S_command::subject` (`command + 0x04`) référence
  exactement le joueur local sont retirées du vecteur de programme, tandis que
  les commandes de patrouille, garde et scénario sans ce sujet sont conservées.
  Les drapeaux de reconstruction sont levés pour que l'ennemi reprenne son état
  propre dès le tick suivant au lieu de poursuivre/tirer pendant plusieurs
  secondes avec une ancienne cible.
- Toutes les bornes du vecteur et adresses de commandes sont validées; aucune
  commande visant un allié ou un autre acteur n'est retirée. Une écriture
  refusée devient visible dans le statut.
- Le build `Debug x86` réussit après ce changement.
- Prochaine action exacte : terminer la conversion du clic de la carte native
  vers le sol de la mission, puis implémenter le lot `²` via la routine native
  sur le thread principal.
### Journal — clic de téléportation sur la carte native (28 août 2026)

- Lorsque F11 a armé la téléportation et que la carte native est active, le
  trainer capte maintenant le premier clic gauche dans le client de `hde.exe`.
- La disposition de `C_game_mission` confirmée par les sources donne
  `map_mgr + 0x98` et `map_active + 0x9C`; la scène propre à la carte est lue à
  `map_mgr + 0x10`. Sa matrice vue-projection à `scene + 0x8C` est inversée en
  convention ligne pour reconstruire le rayon exact du pixel cliqué.
- Le rayon est intersecté avec le BSP de la scène Carte. Son X/Z est ensuite
  reporté sur le BSP de la vraie mission, où le sol praticable le plus haut est
  recherché. Les rotations, le zoom et le déplacement de la carte native sont
  donc pris en compte au lieu d'utiliser une simple proportion 2D fixe.
- Après une sélection valide, la commande Carte native est renvoyée pour
  revenir au jeu; la téléportation existante est appliquée au tick suivant. Un
  clic sans géométrie ou sans sol praticable conserve la carte ouverte et
  affiche l'erreur correspondante.
- Le build `Debug x86` réussit après la correction de validation des pointeurs
  32 bits de la scène Carte.
- Prochaine action exacte : implémenter `²` avec le callback natif groupé de la
  révision installée, puis produire le Release dès que l'ancien exécutable ne
  verrouille plus le fichier de sortie.

### Journal — reprise après interruption pendant l'implémentation de `²` (28 août 2026)

- La relecture du dernier jalon confirme que F11/carte native, l'invisibilité
  étendue et la nouvelle base de visée Aimbot/Bullet Track ont chacun atteint
  un build `Debug x86` réussi avant l'interruption.
- L'interruption a eu lieu après l'écriture, mais avant la compilation, de
  `GrantCompleteDeluxeInventory`. Ce code prépare une page distante et un
  détour temporaire dans le chemin `fullhands` pour appeler le callback natif
  sur les objets `1–115`, sauf `Free hands` déjà présent.
- Cette fonction `²` reste **inachevée et non validée** à cet instant. Aucun
  succès ne sera annoncé avant compilation et audit de la restauration du
  détour sur tous les chemins d'erreur.
- Prochaine action exacte : compiler le code interrompu, corriger les erreurs
  et auditer la restauration/libération du détour temporaire avant de reprendre
  l'interception native du projectile Bullet Track.

### Journal — première compilation et défaut de synchronisation de `²` (28 août 2026)

- La première compilation du code repris réussit en `Debug x86`; la routine
  distante et les nouvelles méthodes `VirtualAllocEx`/`VirtualFreeEx` sont donc
  syntaxiquement et architecturalement compatibles avec la cible 32 bits.
- L'audit avant test a trouvé une course : la routine écrit son marqueur de fin
  juste avant `popad` et le saut vers le jeu. Le trainer pouvait alors libérer
  la page pendant l'exécution de ces derniers octets. Cette version compilée
  n'est pas considérée sûre et ne doit pas être testée telle quelle.
- Prochaine modification exacte : ajouter un protocole de retrait en deux
  temps qui restaure d'abord le point d'entrée, laisse le thread quitter la
  page distante, puis ne réutilise ou libère la page qu'après confirmation;
  revalider aussi les octets couverts par le saut jusqu'à `0x004A00DB`.

### Journal — synchronisation sûre du lot d'inventaire `²` (28 août 2026)

- La routine distante pose maintenant `completion = 1`, puis attend de façon
  bornée `completion = 2`. Le trainer ne donne cet acquittement qu'après avoir
  restauré exactement les cinq octets originaux du chemin `fullhands`.
- `TrainerProcess::IsAnyThreadExecutingRange` énumère les threads du PID,
  suspend chacun le temps de lire son registre d'instruction, puis le reprend
  immédiatement. La page distante n'est libérée que lorsqu'aucun thread ne se
  trouve encore dans ses `0x400` octets et que le hook est déjà retiré.
- En cas de timeout, d'échec de restauration ou d'inspection incomplète, la
  routine d'attente finit quand même par sortir afin de ne pas figer le jeu,
  mais la page est volontairement conservée plutôt que libérée sous un thread.
- Le build `Debug x86` réussit après cette correction. Le test dynamique de
  `²` doit confirmer que les 114 entrées supplémentaires sont acceptées par le
  callback de la mission active et que le statut affiche le nombre ajouté.
- Prochaine action exacte : brancher Bullet Track au point où la structure de
  tir fige sa direction, avec un hook persistant réversible et une cible tête
  partagée mise à jour uniquement depuis le snapshot validé.

### Journal — points d'interception projectile Bullet Track résolus (28 août 2026)

- Le désassemblage de la révision installée localise les deux branches qui
  construisent les tirs humains à `hde.exe + 0x58ACF` et `+ 0x5974D`. Elles
  possèdent la même signature de six octets
  `8B 4D 18 52 6A 03`, puis appellent `C_game_mission::CreateActor(3, &si)`.
- Dans les deux branches, `EDX` référence exactement `S_gun_shoot_init` : la
  position est à `+0x00`, la direction finale à `+0x0C` et le tireur à
  `+0x1C`. Le point est postérieur à `ApplyShootErr` et
  `ApplyShootMagnet`, mais antérieur à `C_gun_shoot::Evaluate`.
- Le hook sera donc limité aux structures dont `si.shooter` est exactement le
  joueur local validé. Il remplacera seulement `si.dir` par la direction de la
  tête verte sélectionnée; les tirs ennemis, alliés, roquettes et tirs sans
  cible dans le cercle resteront natifs.
- Prochaine modification exacte : allouer deux trampolines et une petite zone
  de données partagée, valider/poser les deux sauts, mettre à jour joueur et
  tête à chaque snapshot, puis restaurer les deux signatures à la
  désactivation ou à la fermeture avant de libérer la page.

### Journal — hook projectile Bullet Track implémenté (28 août 2026)

- Une page distante de `0x400` octets contient deux trampolines distincts et
  une zone partagée joueur/cible. Chaque site est validé par les six octets
  attendus avant la pose d'un `jmp rel32`; une pose partielle restaure
  immédiatement tout site déjà modifié.
- Le trampoline compare `si.shooter` au joueur local. En cas d'égalité, il
  calcule `tête - si.pos` directement en flottants x87, écrit les trois
  composantes dans `si.dir`, appelle la normalisation vectorielle native, puis
  rejoue les six instructions déplacées et revient sur l'appel `CreateActor`.
- Quand aucune tête verte n'est dans le cercle, la valeur joueur partagée est
  nulle et le trampoline ne modifie aucun tir. Les deux branches du binaire
  sont couvertes; les projectiles d'autres acteurs ne correspondent jamais au
  pointeur du joueur et restent inchangés.
- La désactivation et la fermeture restaurent les deux signatures, inspectent
  les registres d'instruction des threads et ne libèrent la page qu'une fois
  les deux trampolines inoccupés. Le build `Debug x86` réussit sans avertissement.
- Prochaine action exacte : rendre atomique la publication cible/joueur et
  valider le prologue de la routine `S_vector::Normalize`, puis recompiler et
  produire le bilan complet des fonctions à tester dans une session propre.

### Journal — validation statique finale des corrections demandées (28 août 2026)

- La publication Bullet Track est maintenant ordonnée : le pointeur joueur
  partagé est d'abord mis à zéro, les trois coordonnées de la nouvelle tête
  sont écrites, puis le joueur est publié en dernier. Un projectile ne peut
  donc jamais consommer un mélange entre deux cibles successives.
- La routine native de normalisation à `hde.exe + 0xC12D0` est validée avant
  tout hook par son prologue de huit octets
  `51 D9 01 D9 E1 D9 41 04`. Une autre révision est refusée avec un statut
  d'erreur au lieu d'exécuter une adresse supposée.
- Le chemin `²` a aussi été revalidé dans le désassemblage : le retour
  `0x004A00DB` commence le nettoyage normal de la pile et des objets locaux.
  Le détour évite seulement la boîte de sélection et son callback unique, puis
  laisse l'épilogue natif s'exécuter. La publication de fin en deux temps
  empêche la libération anticipée de sa page.
- Les textes visibles annoncent maintenant explicitement le hook projectile,
  les `114` objets Deluxe et les erreurs de hook. Le dernier texte corrompu par
  un ancien double encodage dans le statut véhicule a été corrigé.
- Une vraie compilation `Release x86` sans avertissement réussit dans
  `build\vs2026-x86\ReleaseCandidate\HDFinalAdvancedV1.exe` : PE x86 GUI,
  `424960` octets, SHA-256
  `01E80C845EF64B36AC828F6B7DBBF2B4C8AAAFA0FF3D49AFD12AA93E1B88AC88`.
- L'ancienne instance Advanced V1 PID `9580`, lancée avant ces corrections,
  est encore ouverte et verrouille le chemin Release habituel. Elle n'a pas
  été fermée automatiquement et le nouveau binaire n'a pas été lancé en
  parallèle afin d'éviter deux trainers modifiant `hde.exe` simultanément.
- Test dynamique restant en session propre : fermer PID `9580`, lancer le
  `ReleaseCandidate`, puis vérifier successivement F11/clic sur la carte native,
  ennemi neuf et déjà alerté avec invisibilité, Aimbot en première/troisième
  personne, projectile Bullet Track vers une tête verte au bord du cercle, et
  enfin `²` avec le statut `114 objets Deluxe ajoutés`.

### Journal — retour de test : crash caméra Aimbot/Bullet Track et conflit F11 (28 août 2026)

- Le test utilisateur invalide entièrement les jalons Aimbot et Bullet Track :
  aucune des deux fonctions ne produit la visée demandée et l'activation fait
  crasher la caméra. Elles sont classées **non fonctionnelles** jusqu'à un
  nouveau test réussi; le simple succès de compilation ne vaut plus validation.
- Le comportement exact exigé pour Bullet Track est reconfirmé : si une tête
  ennemie verte se trouve dans le cercle, chaque balle du joueur doit partir
  dans son visage, même si le joueur vise ailleurs. Bullet Track ne doit donc
  modifier ni la caméra, ni l'animation de visée, ni `aim_dir`.
- Le comportement exact exigé pour Aimbot est reconfirmé : la visée et la
  caméra doivent suivre exactement le visage de l'ennemi vert dont la projection
  est la plus proche du curseur, en première comme en troisième personne.
- F11 est aussi utilisée par le jeu. La téléportation par carte native passe
  donc à **F12**; F11 doit être entièrement rendue au jeu dans le code et dans
  la migration des profils.
- Prochaine modification exacte : passer F11→F12 partout, séparer totalement
  le chemin Bullet Track du chemin Aimbot, retirer toute écriture de caméra du
  mode Bullet Track, puis désassembler les accès réels à `aim_dir`, `aim_count`
  et `game_cam.SetDeltaDir` avant de réintroduire l'Aimbot.

### Journal — cause du crash caméra corrigée et modes séparés (28 août 2026)

- F11 est entièrement libérée. L'interface, la lecture clavier, le statut et la
  migration des profils réservent maintenant **F12** pour la carte native.
- Bullet Track possède désormais une sortie anticipée propre : si Aimbot n'est
  pas coché, il met uniquement à jour le hook projectile, restaure toute ancienne
  écriture Aimbot et retourne sans lire le squelette de visée, sans écrire
  `aim_dir/aim_count` et sans toucher la caméra.
- Le désassemblage confirme que `aim_dir` et `aim_count` sont bien aux offsets
  `+0x1D0` et `+0x1E8`; la panne ne venait pas de ces deux offsets.
- La cause exacte était la lecture de la matrice du parent de poitrine : une
  ligne Insanity3D mesure `0x10` octets, mais l'ancien tableau de `Vector3`
  avançait de `0x0C`. Les axes 2 et 3 mélangeaient donc la composante homogène
  et les lignes voisines, créant une direction locale incohérente qui faisait
  basculer ou bloquer la caméra.
- Les trois axes sont maintenant lus séparément à matrice `+0x00`, `+0x10` et
  `+0x20`. Avant toute écriture, leurs longueurs doivent rester à `1 ± 0.08`
  et leurs trois produits scalaires sous `0.08`; une matrice non orthonormale
  est refusée au lieu d'atteindre la caméra.
- Lorsque les deux modes sont cochés, Aimbot suit maintenant exclusivement la
  tête verte la plus proche du centre du curseur, tandis que Bullet Track garde
  indépendamment la meilleure tête verte dans son cercle. Une cible Bullet
  Track ne détourne plus la caméra Aimbot.
- Le build `Debug x86` réussit après ces corrections. Prochaine action exacte :
  auditer la direction du hook projectile sans appel caméra, corriger les
  statuts combinés, puis produire un nouveau Release candidat pour le test.

### Journal — Release de correction caméra prêt au test (28 août 2026)

- L'audit final confirme deux chemins totalement indépendants. Bullet Track
  publie uniquement joueur et tête au trampoline de projectile; Aimbot seul
  calcule et écrit la direction locale du squelette/caméra.
- Les statuts sont eux aussi indépendants : une absence ou erreur de cible
  Aimbot ne remplace plus l'état d'un hook Bullet Track actif, et inversement.
- La transformation Aimbot correspond maintenant au code natif
  `AimedToDir` : direction monde vers le visage, rotation par l'inverse de la
  base orthonormale du parent de poitrine, inversion Y, normalisation et
  maintien de `aim_count = 2500`. La cible est toujours l'ennemi vert dont la
  tête projetée est la plus proche du centre du viseur.
- Bullet Track conserve la condition stricte du cercle et de l'ESP vert. Une
  fois cette condition vraie, le trampoline recalcule chaque `si.dir` depuis
  la position réelle du projectile vers la tête publiée, après la dispersion
  native et avant `Evaluate`; la direction manuelle du joueur n'intervient pas.
- Les builds `Debug x86` et `Release x86` réussissent sans avertissement. Le
  nouveau binaire est
  `build\vs2026-x86\ReleaseCameraFix\HDFinalAdvancedV1.exe`, `425472` octets,
  SHA-256 `90FF50901412AB8A78BBE8C3B704C05EA576288578D58989EB6D83B83E0FB05A`.
- Cette version corrige la cause mémoire démontrée du crash, mais elle reste à
  valider dynamiquement. Pour un test propre, fermer toute ancienne instance du
  trainer, lancer uniquement `ReleaseCameraFix`, tester d'abord Bullet Track
  seul, puis Aimbot seul, puis les deux ensemble; tester enfin F12 sur la carte.
- Correction finale de conflit : F12 a aussi été retirée de la liste des
  raccourcis configurables de l'interface. F11 y reste disponible, tandis que
  F12 ne peut plus être réattribuée accidentellement à un cheat historique.

### Journal — second retour de test : quatre fonctions toujours invalides (28 août 2026)

- **Bullet Track** reste trop faible : une tête ennemie verte peut être au
  milieu du cercle sans que les balles atteignent exactement son visage. Le
  hook compilé ne sera plus considéré correct tant que le site de tir réel du
  joueur, la structure `S_gun_shoot_init` et le point de tête consommé n'auront
  pas été confirmés sur le processus actif.
- **Aimbot** provoque encore un crash/basculement de caméra lorsque le joueur
  approche d'un ennemi. L'écriture directe continue de `aim_dir/aim_count` est
  donc classée dangereuse et doit être remplacée ou bornée par le chemin natif
  exact de la caméra; la seule correction d'espacement de matrice n'a pas suffi.
- **Touche `²`** n'ajoute toujours pas l'ensemble des armes et objets. Le
  détour `fullhands` doit être contrôlé dans la révision/processus réel, avec
  compteur d'appels et confirmation de mutation d'inventaire, pas seulement un
  marqueur produit par le trampoline lui-même.
- **Super vitesse véhicule** ne fonctionne pas. Les hypothèses `using_item`,
  tableau de sièges et champ `speed + 0x1B0` doivent être comparées aux valeurs
  vivantes d'un véhicule conduit; aucun statut `Active` ne suffit comme preuve.
- **Super Run joueur** reste validé et ne doit pas être modifié.
- Prochaine action exacte : relever en lecture seule PID/module, joueur local,
  frames, direction de visée, signatures des hooks, objet utilisé et candidats
  véhicule/inventaire dans la session active, puis corriger une fonction à la
  fois avec journal et compilation entre chaque jalon.
### Journal — reprise du second diagnostic en jeu (28 août 2026)

- Le premier script de relevé mémoire en lecture seule n'a produit aucune donnée
  exploitable : PowerShell refusait la conversion directe d'une adresse 32 bits
  en `IntPtr` et cette version ne fournit pas `[float]::IsFinite`. Le script n'a
  appelé aucune écriture et n'a donc modifié ni `hde.exe` ni la partie en cours.
- Le relevé est repris dans une aide C# strictement en lecture seule afin
  d'identifier les offsets réellement installés du véhicule avant toute nouvelle
  correction. Le Super Run joueur reste hors de ce chantier.

### Journal — causes structurelles véhicule et inventaire confirmées (28 août 2026)

- La définition source de `C_automobil::S_seat` contient huit membres 32 bits :
  conducteur, frame siège, frame entrée, utilisateur, compteur de réservation,
  réservataire, synchronisation réseau et utilisateur réseau. Sa taille installée
  attendue est donc `0x20`, et non `0x10` comme dans le module actuel. À partir du
  deuxième siège, l'ancien parcours lisait des champs internes au lieu de lire
  l'occupant, ce qui explique l'absence de contexte véhicule.
- Le trampoline actuel de `²` pousse `CB_CHEAT`, sous-commande `9`, puis l'identifiant
  d'objet. Or la sous-commande `9` est le cheat natif `allammo`; son troisième
  paramètre n'ajoute pas l'objet demandé. La boucle répétait donc le même cheat et
  son drapeau de fin ne prouvait aucune mutation complète de l'inventaire.
- Ces deux implémentations sont considérées invalides. Le véhicule sera recalé sur
  la disposition binaire réelle et `²` appellera la vraie fonction native
  `C_inventory::AddItem` avec un compteur de succès vérifiable.

### Journal — correction de la disposition automobile (28 août 2026)

- `gameplay_mods.cpp` utilise maintenant la taille réelle `0x20` de
  `C_automobil::S_seat`. L'utilisateur conducteur reste le membre `+0x0C`, mais
  les huit sièges sont enfin parcourus sur leurs vraies limites.
- Avec `seats` à `automobil + 0x12C`, les huit structures occupent `0x100`
  octets; `fuel` se trouve ensuite à `+0x22C` et `speed` à `+0x230`. L'ancien
  offset `+0x1B0` tombait au milieu du tableau des sièges et ne pouvait pas
  modifier la vitesse physique du véhicule.
- L'offset du frame poitrine humain est également rectifié de `+0x1C8`
  (`frm_head`) vers `+0x1C4` (`brest`) conformément à l'ordre exact des membres
  autour de `aim_dir +0x1D0`. Cette correction seule ne suffit toutefois pas à
  rendre sûre l'ancienne écriture Aimbot, qui sera retirée.

### Journal — cible visage exacte partagée par l'ESP et Bullet Track (28 août 2026)

- Le radar lit désormais directement `C_human::frm_head` à `actor + 0x1C8`, puis
  sa position monde à `frame + 0xBC`, après validation du pointeur et des trois
  coordonnées. Cette position est le frame tête choisi par le jeu lui-même.
- L'ancienne approximation « frame voisin le plus haut moins 8 cm » pouvait
  sélectionner un cheveu, une arme levée ou un autre accessoire du squelette.
  Elle n'est plus utilisée comme cible lorsqu'un vrai `frm_head` est disponible.
- Le même point exact alimente maintenant le test vert, la sélection au curseur,
  l'Aimbot et la donnée distante du Bullet Track. Le hook de projectile vise donc
  le visage réel et non une estimation verticale différente de ce que montre le
  cercle.

### Journal — suppression de la cause du crash caméra Aimbot (28 août 2026)

- L'Aimbot n'écrit plus jamais `C_human::aim_dir`, `aim_count`, une matrice de
  frame ou une donnée caméra. L'ancien chemin était en concurrence avec
  `ApplyAimDir` et `game_cam.SetDeltaDir`; à courte distance, deux systèmes
  imposaient des orientations différentes pendant le même frame.
- La nouvelle commande projette le vrai `frm_head` dans l'écran et pilote le
  mouvement souris natif uniquement lorsque la fenêtre du jeu est active. Une
  boucle proportionnelle refermée recalcule l'erreur à chaque snapshot, la borne
  à 48 comptes par frame pour empêcher tout retournement, puis conserve un compte
  minimal jusqu'à convergence sous 0,75 pixel.
- Le moteur reste seul propriétaire de la caméra, du recul, des limites de
  squelette et des modes première/troisième personne. Désactiver l'Aimbot ne
  restaure plus aucune mémoire puisqu'il n'en modifie plus aucune.
- Le Bullet Track demeure indépendant : il ne déplace pas la caméra et ne change
  que la direction initiale du projectile si une tête verte est dans le cercle.

### Journal — remplacement complet de `²` par l'inventaire natif (28 août 2026)

- Le trampoline temporaire n'appelle plus `CB_CHEAT/allammo`. Il construit en
  mémoire distante un vrai `vector<S_item>` 32 bits contenant les 115 entrées
  Deluxe, avec quantité 50 et munitions 500, puis appelle une seule fois le
  callback joueur `CB_SETINVENTORY` (message 17) sur le thread principal.
- Le jeu exécute alors son chemin normal : fermeture de l'inventaire, suppression
  de l'ancien contenu, `C_inventory::AddItem` pour chaque ligne, rechargement,
  sélection de la meilleure arme et rafraîchissement de l'affichage. Le compteur
  de fin vaut désormais 115; un simple passage dans le trampoline ne peut plus
  être annoncé comme un succès.
- La page distante passe à `0x1000` octets pour contenir les 115 structures de
  12 octets et reste protégée par la restauration du hook, l'acquittement et le
  contrôle qu'aucun thread ne l'exécute avant libération.
- La touche physique `²` est maintenant détectée par son scan code `0x29` traduit
  dans la disposition clavier active, avec replis `VK_OEM_3` et `VK_OEM_7`. Cela
  corrige les claviers français où `²` n'était jamais signalée comme `OEM_3`.

### Journal — suppression de l'écrasement tardif du Bullet Track (28 août 2026)

- `S_gun_shoot_init::dir` à `+0x0C` était correctement redirigé, mais les tirs
  dont `delay +0x20` était non nul entraient ensuite dans
  `C_gun_shoot::Tick`. Ce chemin recalculait `dir` depuis la matrice de l'arme,
  puis ajoutait une dispersion aléatoire sur les trois axes : la correction
  visage était donc perdue après la création du projectile.
- Dans les deux sites de création validés, et seulement lorsque `si.shooter` est
  le joueur verrouillé, le stub normalise maintenant la direction exacte vers
  `frm_head`, met `si.delay` à zéro et annule `si.aim_disp +0x38`. Chaque balle de
  rafale est évaluée immédiatement sur ce vecteur; aucun tick ultérieur ne peut
  la ramener vers le canon ou lui réinjecter la dispersion native.
- Sans cible verte dans le cercle, le joueur partagé vaut zéro : le stub saute
  ces écritures et les tirs conservent intégralement leur comportement normal.
- Le stub préserve explicitement `EDX` autour de l'appel natif `S_vector::Normalize`.
  Cette fonction est autorisée à écraser ce registre volatil; sans cette garde,
  les écritures suivantes de `delay/aim_disp` auraient pu viser une adresse
  étrangère et provoquer un comportement aléatoire. Le pointeur de
  `S_gun_shoot_init` est maintenant restauré avant chaque accès post-normalisation.

### Journal — compilation des quatre correctifs (28 août 2026)

- La cible complète compile sans erreur en `Debug x86`, puis en `Release x86`
  avec Visual Studio 2026. Le binaire produit est
  `build/vs2026-x86/Release/HDFinalAdvancedV1.exe`, 423936 octets, horodaté le
  28 août 2026 à 18:20.
- La vérification statique du résultat confirme la présence de F12, de la
  détection physique `²`, de `CB_SETINVENTORY`, des offsets automobile
  `seat=0x20/speed=0x230`, du frame tête `+0x1C8`, du pilotage souris natif et de
  l'annulation `delay/aim_disp` dans les deux stubs Bullet Track.
- L'ancien trainer PID 960 est encore ouvert sur la session `hde.exe` PID 4864.
  Le nouveau Release n'est volontairement pas lancé en parallèle : le test doit
  commencer après fermeture de cet ancien trainer afin qu'un seul processus
  possède les hooks et écritures du jeu.
- Le Super Run joueur n'a subi aucune modification fonctionnelle pendant ce
  correctif.

### Journal — déclenchement de secours visible pour l'inventaire (28 août 2026)

- La ligne informative de `²` est devenue un bouton ImGui
  `Ajouter tout le catalogue [²]`. La touche physique reste le raccourci
  principal, mais le bouton déclenche exactement la même requête au frame
  suivant; il permet de distinguer immédiatement un problème de clavier d'un
  refus du callback natif.
- La requête UI est consommée une seule fois puis remise à faux, afin qu'un clic
  ne puisse pas relancer le remplacement complet de l'inventaire en boucle.
- Recompilation Release x86 réussie après cet ajout. Le binaire final fait
  désormais 424448 octets et est horodaté le 28 août 2026 à 18:21:31.

### Journal — validation binaire finale de l'automobile (28 août 2026)

- Le désassemblage du `hde.exe` installé confirme indépendamment la disposition
  source : les parcours de sièges incrémentent leurs pointeurs de `0x20`, les
  blocs commencent à `this + 0x12C`, leur fin est référencée à `+0x22C`, et le
  moteur charge explicitement la vitesse par `fld [esi + 0x230]` à `004351C1`.
- Les deux offsets corrigés ne reposent donc plus sur une estimation de classe :
  ils correspondent aux instructions de la révision réellement installée.

### Journal — statut d'inventaire aligné sur la vérification (28 août 2026)

- Le texte de succès n'annonce plus l'ancien total 114 : il exige et affiche
  désormais les 115 objets reconstruits par le callback natif. L'erreur précise
  également que l'acquittement 115 n'a pas été reçu, ce qui rend le prochain test
  directement diagnostiquable dans l'interface.
- Dernière recompilation Release x86 réussie : 424448 octets, horodatage
  18:22:25, SHA-256
  `093A75D3D05C29D9D54031DD4F26EA0E7E8923C8F80641738F28E22D98D5FDCC`.
### Journal — troisième retour de test ciblé (28 août 2026)

- **Aimbot** : la sélection et la hauteur du visage sont enfin validées en jeu;
  les tirs atteignent bien la tête. Défaut restant : dès qu'un ennemi devient
  vert, la caméra effectue des transitions horizontales visibles. La correction
  doit conserver une cible stable et supprimer oscillation/changement de cible,
  sans réintroduire la moindre écriture dans la caméra.
- **Bullet Track** : le résultat reste refusé. Le verrouillage demande une visée
  trop proche du réticule, la limite monde de 220 m contredit le comportement
  longue portée demandé, et une cible en course n'est pas atteinte au visage.
  La prochaine version doit accepter toute cible verte réellement dans le
  cercle, sans limite métrique artificielle, et prédire sa tête au tick du tir.
- **Inventaire `²`** : ni la touche ni le nouveau callback ne produisent encore
  l'inventaire complet. Un acquittement du trampoline ne sera plus assimilé à
  une réussite; le chemin `fullhands`, la signature et la mutation native seront
  contrôlés séparément.
- **Super Run joueur** et les fonctions déjà validées restent hors modification.
- Ordre de travail : stabilisation Aimbot, portée/prédiction Bullet Track,
  diagnostic et remplacement de `²`, avec compilation et journal après chaque
  jalon.

### Journal — verrouillage stable Aimbot et cercle Bullet Track réel (28 août 2026)

- L'Aimbot mémorise désormais l'adresse de la cible acquise. Tant que cet ennemi
  reste vivant, vert, à l'écran et dans la distance Aimbot, l'apparition d'un
  autre ennemi ne peut plus provoquer un changement horizontal de cible. Une
  nouvelle sélection au plus proche du curseur ne se fait qu'après perte réelle
  de la cible verrouillée.
- Le mouvement initial est borné à 160 comptes au lieu de 48 afin de rejoindre
  le visage en un seul rafraîchissement dans les grands écarts. Une zone stable
  de 5 pixels arrête ensuite tout micro-mouvement horizontal; le contrôleur ne
  cherche plus alternativement les deux côtés du même pixel.
- Pour Bullet Track, le test du cercle ne porte plus uniquement sur le point
  tête : il calcule la distance minimale entre le centre du cercle et tout le
  segment vertical pieds-visage. Un ennemi dont le corps est réellement dans le
  cercle est donc accepté même si sa tête se trouve près de son bord.
- La limite métrique de 220 m est supprimée uniquement pour Bullet Track. La
  visibilité verte, la présence à l'écran et le cercle restent obligatoires;
  l'Aimbot conserve son curseur 40–220 m comme demandé initialement.

### Journal — compensation de cible mobile Bullet Track (28 août 2026)

- Le module conserve maintenant, par PID et acteur Bullet Track, la dernière
  position de tête et son horodatage. Une vitesse 3D lissée est calculée seulement
  entre 4 et 250 ms et refusée au-delà de 15 m/s afin qu'une téléportation, une
  mort ou un changement de mission ne devienne jamais une fausse prédiction.
- Le point envoyé au hook avance la tête de 20 ms, soit un frame de lecture/rendu.
  Le moteur évalue la collision du projectile immédiatement : aucune avance
  proportionnelle à la distance n'est ajoutée, car elle dépasserait au contraire
  les ennemis lointains. Cette compensation vise précisément le décalage observé
  sur un ennemi en course.
- L'historique est remis à zéro à la perte de cible, à la désactivation et à la
  restauration générale; aucune vitesse d'un ancien acteur n'est réutilisée.

### Journal — nouveau déclencheur main thread pour l'inventaire `²` (28 août 2026)

- Le défaut restant est isolé au déclenchement : le trampoline d'inventaire
  dépend encore de la saisie de la séquence texte `iwantcheat/fullhands`. Si le
  jeu ne consomme pas cette saisie, le callback natif n'est jamais atteint et
  l'acquittement reste absent.
- Une fonction dédiée prépare maintenant une seule touche physique `X` par
  scancode dans la fenêtre active du jeu. Elle ne sera utilisée qu'après pose
  d'un détour temporaire à l'entrée validée de
  `C_game_mission::ProcessCheat` (`RVA 0x9FC10`, signature
  `B8 5C 10 00 00`). Le jeu appelle ainsi le stub sur son propre thread
  principal, sans dialogue et sans dépendre du mode cheat.
- Prochaine modification exacte : remplacer le hook `fullhands` tardif par ce
  point d'entrée, exécuter une fois `CB_SETINVENTORY` pour les 115 entrées,
  acquitter, restaurer les cinq octets et libérer l'allocation seulement après
  confirmation que le thread du jeu a quitté le stub.

### Journal — remplacement effectif du chemin `²` (28 août 2026)

- `GrantCompleteDeluxeInventory` ne dépend plus de `cheat_sequence`, de
  `iwantcheat`, de `fullhands` ni de la boîte de sélection. Il valide maintenant
  les cinq octets réels de `ProcessCheat` à `RVA 0x9FC10`, pose un saut
  temporaire, puis envoie une seule touche `X` au jeu pour exécuter le stub sur
  son thread principal.
- Le stub préserve drapeaux et registres, appelle une seule fois le callback
  virtuel du joueur `CB_SETINVENTORY=17` avec le vecteur complet des 115 objets,
  écrit l'acquittement 115, attend la libération du trainer et retourne par
  `ret 4` sans exécuter l'ancien traitement de touche ni afficher de dialogue.
- Le trainer restaure les cinq octets originaux avant de libérer le stub. La
  page distante n'est libérée qu'après acquittement, signal de sortie et
  contrôle qu'aucun thread n'y exécute encore du code; en cas d'incertitude elle
  reste volontairement allouée pour ne jamais libérer une adresse de retour.
- Prochaine action exacte : compiler ce jalon en Debug x86, corriger toute erreur
  de génération, puis auditer ensemble le verrouillage Aimbot et les deux hooks
  de tir Bullet Track avant le Release de test.

### Journal — compilation Debug du correctif ciblé (28 août 2026)

- La cible complète compile sans erreur en `Debug x86` après le nouveau chemin
  `ProcessCheat`, la cible Aimbot persistante et la prédiction Bullet Track.
  L'exécutable de contrôle est
  `build/vs2026-x86/Debug/HDFinalAdvancedV1.exe`.
- Aucune instance de ce Debug n'est lancée : le trainer de la session de test
  reste ouvert et deux processus ne doivent jamais poser simultanément les
  mêmes hooks dans `hde.exe`.
- Prochaine action exacte : vérifier statiquement les chemins d'activation et de
  restauration Aimbot/Bullet Track, les adresses partagées du stub et la
  signature `ProcessCheat`, puis construire le Release final.

### Journal — audit statique et restauration durcie (28 août 2026)

- L'audit confirme que l'Aimbot conserve l'acteur acquis par PID et ne
  resélectionne qu'après disparition, mort, perte de visibilité, sortie écran
  ou sortie de la portée Aimbot. Sa désactivation reste sans écriture caméra.
- Les deux sites Bullet Track installés sont confirmés dans le `hde.exe` de
  référence aux RVA `0x58ACF` et `0x5974D`, avec les six octets attendus
  `8B 4D 18 52 6A 03`. La routine de normalisation est également confirmée à
  `0xC12D0` avec sa signature de huit octets. Le SHA-256 du jeu contrôlé reste
  `5D5EED6174658B8FACFBC1251146B109BBB12AD9A119168AA3489F80979D62D0`.
- L'état Bullet Track mémorise désormais les deux sauts complets réellement
  posés. La restauration ne remplace plus n'importe quel opcode `E9` : elle ne
  restaure que l'octet original ou le saut exact appartenant à ce trainer.
- L'état de restauration complet est publié avant la première pose de hook.
  Si `WriteProtectedMemory` signale tardivement un échec après une écriture
  effective, le rollback retrouve donc encore le saut et ne libère jamais son
  stub tant que la restauration et l'inactivité de la page ne sont pas prouvées.
- Le détour temporaire `ProcessCheat` applique la même garantie : son échec de
  pose relit les cinq octets, restaure uniquement son saut exact, vérifie le
  prologue original et contrôle qu'aucun thread n'est dans la page avant toute
  libération. Le chemin normal vérifie aussi les octets restaurés avant de
  donner au stub son signal de sortie.
- Les zones distantes restent disjointes et bornées : deux stubs Bullet Track
  à `+0x000/+0x100`, joueur à `+0x300`, cible 3D à `+0x304` dans `0x400` octets;
  inventaire à `+0x120..+0x684` et acquittement à `+0x700` dans `0x1000` octets.
- Aucun comportement du Super Run, des armes validées, de l'ESP ou des autres
  fonctions n'a été modifié pendant cet audit. Prochaine action : compiler le
  nouveau code en Debug x86 puis produire et valider le Release x86 final.

### Journal — validation Debug après audit (28 août 2026)

- La recompilation `Debug x86` du code audité réussit sans erreur avec Visual
  Studio 2026. L'exécutable produit est
  `build/vs2026-x86/Debug/HDFinalAdvancedV1.exe`.
- Vérifications : PE32/x86 (`Machine 0x014C`, magie `0x010B`, sous-système
  Windows GUI), taille `1 661 952` octets et SHA-256
  `8D0F834F8E78625C787E7259008D7C6A843A5E584573DBE588751E775E3B2201`.
- Ce Debug n'est pas lancé en présence d'une partie ou d'un trainer existant.
  Prochaine action : construire le même état en Release x86 et contrôler son
  architecture, sa taille, son empreinte et les chaînes fonctionnelles finales.

### Journal — versionnement du Release de test (28 août 2026)

- La première édition de liens Release a été refusée par `LNK1104` parce que
  `build/vs2026-x86/Release/HDFinalAdvancedV1.exe` est encore ouvert sous le PID
  `19008`, avec `hde.exe` PID `18852`. La compilation C++ elle-même avait réussi.
- Cette instance n'est volontairement ni arrêtée ni forcée : elle peut encore
  posséder les hooks de la partie et doit effectuer sa restauration normale.
- `CMakeLists.txt` produit désormais `HDFinalAdvancedV2.exe`. Ce nom distinct
  conserve le V1 ouvert, évite tout écrasement et identifie sans ambiguïté le
  correctif Aimbot/Bullet Track/`²` à tester après fermeture normale du V1.
- Prochaine action : relancer la génération/compilation Release x86 sous ce nom,
  puis effectuer les contrôles binaires finaux.

### Journal — Release V2 final prêt au test (28 août 2026)

- La génération puis la compilation `Release x86` réussissent sans erreur sous
  le nouveau nom. Binaire final :
  `build/vs2026-x86/Release/HDFinalAdvancedV2.exe`.
- Vérifications : PE32/x86 (`Machine 0x014C`, magie `0x010B`, sous-système
  Windows GUI), taille `426 496` octets, SHA-256
  `D56C5F447987D835EAF837ED6F82FD1073A67320663E7A3E1D62F57749B1914E`.
- Les chaînes finales Aimbot, Bullet Track, bouton `Ajouter tout le catalogue
  [²]` et acquittement `115 objets` sont présentes dans le Release.
- Audit des imports : `CreateRemoteThread`, `SetWindowsHookEx`, `SendMessage` et
  `PostMessage` sont absents. `VirtualAllocEx`, `VirtualFreeEx`,
  `WriteProcessMemory`, `FlushInstructionCache` et `SendInput` sont présents
  conformément aux stubs temporaires, aux hooks réversibles et à l'entrée native
  déjà documentés.
- Aucun V2 n'a été lancé. Le V1 PID `19008` et le jeu PID `18852` sont encore
  actifs au moment de la livraison. Ordre de test obligatoire : fermer le V1
  normalement, vérifier sa disparition, lancer uniquement le V2, puis charger
  ou reprendre la mission.
- Protocole ciblé : (1) Aimbot sur un ennemi vert puis apparition d'un second,
  en vérifiant l'absence de balayage horizontal; (2) Bullet Track avec le corps
  dans le cercle, cible lointaine puis cible en course; (3) bouton inventaire
  d'abord, puis touche physique `²`, en exigeant le statut de succès à 115.
- Le travail de code et de compilation est terminé. Le seul jalon restant est
  le retour du test en jeu sur ce V2; toute correction éventuelle devra être
  ajoutée à ce journal avant un V3.

### Journal — échec confirmé du test V2 et reprise du diagnostic (28 août 2026)

- Le test utilisateur confirme que les trois défauts du V1 sont encore présents
  dans le V2. Les corrections précédentes ne sont donc pas validées et ne doivent
  plus être décrites comme fonctionnelles.
- **Aimbot** : la balle atteint le visage, mais l'acquisition d'un ennemi vert
  provoque toujours une transition ou oscillation horizontale de la caméra au
  lieu d'un verrouillage immédiatement stable.
- **Bullet Track** : la redirection exige encore un curseur trop proche, reste
  insuffisante à grande distance et ne place pas les balles au visage d'un ennemi
  en course.
- **Inventaire `²`** : le nouveau détour ne produit toujours pas l'inventaire
  complet; l'appel supposé de `CB_SETINVENTORY` n'est donc pas une preuve de
  mutation réelle dans la révision installée.
- Nouvelle méthode demandée : confronter chaque chemin au moteur/source et à des
  informations en ligne, observer l'état réel de la session, puis corriger les
  causes confirmées une par une avec journal et compilation après chaque tâche.

### Journal — causes V2 confirmées par sources en ligne, source moteur et binaire (28 août 2026)

- La documentation Microsoft sur les mouvements souris Win32 confirme que les
  déplacements relatifs envoyés sans `MOUSEEVENTF_ABSOLUTE` sont transformés par
  la vitesse et les deux seuils d'accélération Windows; un mouvement peut être
  multiplié jusqu'à quatre fois. Le contrôleur Aimbot V2 convertissait pourtant
  directement une erreur en pixels en comptes `SendInput` avec un gain fixe.
  C'est la cause de ses transitions et dépassements horizontaux. Source :
  `https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-mouse_event`.
- La mise à disposition publique des sources de H&D Deluxe est également
  confirmée en ligne par la communauté du jeu, et le dépôt Insanity3D précise
  que ce moteur a été utilisé par Hidden & Dangerous. Sources :
  `https://hidden-and-dangerous.net/board/viewtopic.php?t=2574` et
  `https://github.com/mice777/Insanity3D`.
- La source locale issue de cette publication montre que la caméra première
  personne consomme directement `C_game_camera::delta_dir`, puis transforme ce
  vecteur relatif par la matrice de `focus_dir`. Le binaire installé confirme
  `C_game_camera::Tick` à la RVA `0x5E510`, le prologue
  `55 8B EC 81 EC 64 02 00 00` et `delta_dir` à `this+0x14`. Le remplacement
  Aimbot doit donc écrire ce vecteur sur le thread caméra, sans souris relative.
- Pour le Bullet Track, `C_gun_shoot` appelle `Evaluate()` immédiatement dans
  son constructeur lorsque `delay=0`; `Evaluate()` lance alors le rayon de
  collision sur la direction reçue. L'avance artificielle de 20 ms vise donc
  devant la collision actuelle d'une cible en course. La vraie correction est
  de conserver le frame tête et de lire `frame+0xBC` dans le stub au tick exact
  du tir. Le test `restrict_to_bullet_circle` explique séparément pourquoi le
  curseur devait encore être proche malgré la suppression de la limite métrique.
- Pour `²`, le désassemblage du chemin natif `CB_SETINVENTORY` à `0x4A0AA4`
  montre l'appel réel `call [vtable+0x04]`, précédé de quatre paramètres empilés.
  Le stub V2 appelait au contraire `[vtable+0x30]` avec seulement trois
  paramètres, puis écrivait malgré tout 115 dans son acquittement. Il pouvait
  donc annoncer la fin sans avoir appelé `C_player::cbProc` ni muté l'inventaire.
- Corrections décidées : hook caméra principal avec vecteur relatif local,
  sélection Bullet Track verte sans dépendance au cercle et position de tête
  lue en direct, puis appel inventaire conforme à la vtable/ABI installée.

### Journal — correction Aimbot par le tick caméra interne (28 août 2026)

- Le chemin `SendInput` n'est plus utilisé par l'Aimbot. Un détour réversible
  de neuf octets est posé sur `C_game_camera::Tick` (`RVA 0x5E510`) et écrit
  `delta_dir` à `this+0x14` sur le thread même de la caméra.
- La direction monde joueur→visage est convertie dans les trois axes locaux du
  frame joueur puis normalisée. Il n'existe donc plus de gain souris, seuil
  d'accélération Windows, interpolation horizontale ou dépassement en pixels.
- La cible acquise reste mémorisée par adresse acteur; si elle disparaît, le
  hook reste installé mais désactivé jusqu'à une nouvelle cible valide. La page
  distante et les neuf octets originaux ont un chemin de restauration contrôlé.

### Journal — correction Bullet Track sans cercle ni prédiction (28 août 2026)

- `RadarEntity` transporte maintenant l'adresse exacte du frame tête validé.
  Une cible sans ce frame est refusée par le sélecteur Bullet Track.
- La restriction au cercle et toute limite métrique sont retirées de la
  sélection : l'ennemi vert visible le plus proche du centre est admissible
  même si le curseur est loin.
- Le stub des deux points de création de projectile ne reçoit plus une copie de
  position vieille d'un snapshot et n'ajoute plus 20 ms. Il déréférence le frame
  tête et lit ses coordonnées monde `+0xBC/+0xC0/+0xC4` au moment exact du tir,
  recalcule la direction, la normalise et annule délai et dispersion.

### Journal — correction de l'ABI inventaire `²` (28 août 2026)

- Le stub main-thread empile désormais les quatre arguments observés dans
  `GameBegin` : zéro réservé, `prm2=0`, pointeur du vecteur et message 17.
- L'appel virtuel vise `[vtable+0x04]`, exactement comme le binaire installé,
  au lieu du mauvais slot `[vtable+0x30]`. L'acquittement 115 n'est écrit
  qu'après le retour de ce callback.
- Le nom du prochain exécutable est passé à `HDFinalAdvancedV3` afin de ne pas
  écraser le V2 encore ouvert pendant la génération.
- Prochaine tâche : compiler en Debug x86, corriger les éventuelles erreurs,
  puis inscrire le résultat avant la génération Release.

### Journal — validation Debug x86 du V3 (28 août 2026)

- La configuration CMake a été régénérée avec le nouveau nom, puis la cible
  réelle `HDPhase1` a compilé sans erreur en Debug x86.
- Le résultat de contrôle est
  `build/vs2026-x86/Debug/HDFinalAdvancedV3.exe`; il contient ensemble le hook
  caméra, le suivi direct du frame tête et l'ABI inventaire corrigée.
- Aucun exécutable Debug n'est lancé. Prochaine tâche : auditer les octets émis,
  les références résiduelles et la restauration, puis construire le Release V3.

### Journal — audit statique final des trois chemins V3 (28 août 2026)

- La source `GameCam.cpp` confirme l'ordre exact de la caméra première personne :
  copie de `delta_dir`, inversion de Y, puis `RotateByMatrix(focus_dir)`. Le V3
  effectue l'opération inverse avant d'écrire le vecteur, ce qui produit
  directement la direction monde joueur→visage sans interpolation.
- Les anciennes références `PredictBulletTarget`, `g_bullet_motion` et
  `TrackAimbotTargetWithNativeMouse` ont toutes disparu. Le code machine du stub
  charge d'abord le pointeur de frame, puis les trois floats monde de la tête,
  avant l'appel à la normalisation native vérifiée à la RVA `0xC12D0`.
- La source déclare bien `CB_SETINVENTORY` comme le remplacement complet de
  l'inventaire et son implémentation commence par `DeleteAllItems`, puis parcourt
  le vecteur reçu et recharge chaque entrée. Le slot et le nombre d'arguments
  retenus restent ceux constatés dans l'appel du binaire installé, qui prime sur
  une supposition tirée d'une autre compilation des sources.
- Les hooks publient leur état de restauration avant toute écriture protégée et
  ne libèrent leur page qu'après vérification qu'aucun thread ne l'exécute.
- Prochaine tâche : générer le Release x86 V3 et vérifier son identité PE, sa
  taille, son hash et l'absence de lancement concurrent avec le V2.

### Journal — livraison Release x86 V3 (28 août 2026)

- La cible principale compile sans erreur en Release x86. Exécutable final :
  `build/vs2026-x86/Release/HDFinalAdvancedV3.exe`.
- Vérification PE : machine `0x14C (x86)`, application Windows GUI, taille
  `427520` octets, SHA-256
  `FF05E05956046197BDBDA2A50034E08279C1C9DAB0269A8887018763292264F2`.
- CTest ne déclare aucun test. La cible auxiliaire `HDVisibilityTest` ne peut pas
  être reconstruite parce que CMake référence deux fichiers déjà absents du
  dépôt (`src/visibility_bridge.cpp` et `src/visibility_bridge_test.cpp`); cela
  n'affecte pas la compilation de l'exécutable principal et n'a pas été masqué.
- Le V3 n'a pas été lancé : `HDFinalAdvancedV2.exe` PID 1120 est toujours actif.
  Il faut fermer normalement ce V2, puis lancer uniquement le V3 afin d'éviter
  que deux processus tentent de gérer les mêmes détours dans le jeu.
- Test en jeu à effectuer sur le V3 : apparition d'un ennemi vert sans balayage
  horizontal; Bullet Track avec curseur loin et ennemi en course; enfin `²` en
  exigeant la mutation visible de l'inventaire, pas seulement le statut 115.
- Tous les correctifs demandés, l'audit et la livraison sont terminés; seul le
  retour d'exécution dans `hde.exe` reste nécessaire, car le jeu n'était pas
  actif durant cette génération.

### Journal — retour réel V3 et périmètre V4 (28 août 2026)

- **Aimbot validé en jeu** : le verrou visage est désormais très puissant,
  exact et stable. Ce chemin caméra doit être conservé. Seul manque un geste
  utilisateur logique : avec deux ennemis verts, un mouvement horizontal rapide
  vers la droite ou la gauche doit transférer le verrou vers l'ennemi situé dans
  ce sens.
- **Invisibilité non validée** : les ennemis continuent à voir le joueur malgré
  le statut actif. Le patch actuel doit être confronté au vrai chemin de
  perception/hostilité du moteur.
- **Bullet Track non validé** : toutes les balles doivent atteindre exactement
  le visage d'un ennemi vert même lorsque le joueur tire vers le ciel. Le hook
  V3 n'intercepte donc toujours pas le dernier vecteur réellement évalué.
- **Inventaire `²` en crash** : le callback V3 corrompt ou utilise mal la pile;
  toute nouvelle tentative doit d'abord corriger l'ABI, puis n'acquitter qu'une
  mutation réellement terminée.
- **Application** : l'ESP ennemis doit être décoché à chaque nouveau démarrage.
  Le trainer doit aussi se connecter immédiatement à un `hde.exe` déjà ouvert;
  le joueur ne doit plus supprimer/redémarrer le jeu pour charger la nouvelle
  version.
- Ordre V4 : changement volontaire de cible Aimbot, invisibilité réelle, dernier
  vecteur Bullet Track, crash `²`, valeurs de démarrage et rattachement à chaud;
  mise à jour de ce journal et compilation après chaque tâche.

### Journal — changement volontaire de cible Aimbot (28 août 2026)

- Le verrou caméra V3 validé reste inchangé. Son stub copie maintenant, avant
  l'écrasement, le `delta_dir` que le jeu vient de produire depuis la souris et
  incrémente une séquence partagée; aucun mouvement Windows artificiel n'est
  réintroduit.
- Le trainer compare cette intention native à la dernière direction visage. Une
  variation horizontale rapide supérieure à `0.10` déclenche, avec un délai
  anti-rebond de 220 ms, la recherche de l'ennemi vert suivant dans ce sens.
- Le candidat doit être à au moins 24 pixels à droite ou à gauche de la cible
  verrouillée, rester à l'écran et dans la portée Aimbot. Sans candidat valide,
  le verrou actuel est conservé; une petite correction de souris ne le casse pas.
- La cible choisie est ensuite reprise par le même hook stable déjà validé. La
  compilation Debug x86 réussit sans erreur après ce jalon.
- Prochaine tâche : remplacer l'invisibilité qui annonce faussement « actif »
  par un contrôle du chemin de perception réellement consommé par l'IA.

### Journal — interception Bullet Track déplacée sur le créateur central (28 août 2026)

- Le désassemblage confirme que les anciennes RVA `0x58ACF/0x5974D` sont deux
  appels particuliers à `C_game_mission::CreateActor`. Elles ne garantissent pas
  de couvrir toutes les créations de balle et peuvent rester occupées par une
  ancienne instance du trainer.
- Le V4 pose désormais un seul détour à l'entrée centrale de `CreateActor`
  (`RVA 0x37EB0`, prologue vérifié `56 57 8B F9 8B 4C 24 0C`). Il filtre
  `ACTOR_GUN_SHOOT == 3`, puis exige que `S_gun_shoot_init::shooter +0x1C` soit
  exactement le joueur local; les balles ennemies ne sont jamais redirigées.
- Pour chaque balle du joueur, quel que soit son appelant, le stub lit le frame
  tête vert partagé, remplace `si.dir +0x0C`, normalise avec la routine native,
  force `delay +0x20` à zéro et retire `aim_disp +0x38`. Tirer vers le ciel ne
  participe plus à la direction évaluée.
- La restauration est simplifiée à un unique prologue de huit octets et une
  page distante contrôlée. La compilation Debug x86 réussit sans erreur.
- Prochaine tâche : rendre l'invisibilité effective puis traiter séparément le
  crash de pile de `²`.

### Journal — invariant natif corrigé pour l'inventaire `²` (28 août 2026)

- Le désassemblage complet confirme que le callback installé à `[vtable+0x04]`
  reçoit bien quatre arguments et que `ProcessCheat` termine par `ret 4`; ces
  deux conventions V3 sont conservées et ne sont pas la cause du crash.
- La cause structurelle restante est l'ordre du vecteur : le constructeur de
  `C_human` ajoute toujours `Free hands` (ligne 30) en premier, et plusieurs
  chemins natifs considèrent l'index d'inventaire zéro comme cette sentinelle.
  Le V3 reconstruisait au contraire les lignes `1..115`, plaçant les mains à
  l'index 29 puis demandant au callback d'activer l'index zéro.
- Le vecteur V4 place désormais la ligne 30 en premier avec quantité 1 et zéro
  munition, puis ajoute toutes les autres lignes 1–115 en sautant 30. Le total
  reste exactement 115 et aucune famille du catalogue n'est supprimée.
- La compilation Debug x86 réussit après cette correction. Prochaine tâche :
  renforcer l'invisibilité, puis corriger le démarrage/handoff de l'application.

### Journal — blocage continu de la perception ennemie (28 août 2026)

- La simple purge ponctuelle de `watch_actors` laissait une course avec
  `C_human::WatchHumans` : l'IA pouvait reconstruire sa liste au cycle suivant
  et conserver le joueur comme cible malgré le statut « actif ».
- Le V4 remet maintenant à zéro, à chaque rafraîchissement du trainer et pour
  chaque ennemi vivant, le compteur natif de balayage situé à `C_human+0x294`.
  La routine de perception n'atteint donc plus son seuil de 1000 ms et ne peut
  plus réinsérer le joueur entre deux purges de la liste surveillée.
- Le drapeau `holding_fire` à `C_human+0x292` est également relâché afin qu'un
  ennemi déjà en train de tirer ne conserve pas une détente verrouillée. Le
  patch `IsEnemy` et le nettoyage des commandes visant le joueur restent en
  place comme protections complémentaires.
- La désactivation ne fige aucune donnée permanente : le compteur recommence
  naturellement et l'IA retrouve sa perception au cycle suivant. La compilation
  Debug x86 réussit sans erreur après ce jalon.
- Prochaine tâche : décocher l'ESP ennemis par défaut et garantir le rattachement
  à un jeu déjà ouvert avec fermeture propre de l'ancienne version du trainer.

### Journal — démarrage propre et rattachement à chaud (28 août 2026)

- `RadarRenderSettings::show_enemy_esp` vaut maintenant `false` à chaque nouveau
  lancement : la case « Afficher ESP Ennemis » apparaît décochée par défaut.
- La découverte de `hde.exe` passe désormais par l'instantané système des
  processus, indépendamment de la présence immédiate d'une fenêtre visible. Le
  handle du jeu peut donc être ouvert si le jeu était lancé avant le trainer;
  sa fenêtre est retrouvée ensuite dès qu'elle devient disponible.
- Tant que le processus du jeu vit, une fenêtre momentanément absente ou recréée
  ne provoque plus une déconnexion. Le balayage est ramené de 1000 à 250 ms afin
  de rendre le rattachement perceptiblement immédiat.
- Au lancement, le V4 repère l'ancienne fenêtre du trainer, lui envoie `WM_CLOSE`
  et attend la fin réelle de son processus jusqu'à quatre secondes. Cette attente
  laisse l'ancien exécutable restaurer ses hooks avant que le nouveau les pose;
  aucun arrêt forcé n'est utilisé. S'il refuse de finir, le V4 affiche une alerte
  et s'arrête au lieu de faire coexister deux versions dangereuses.
- Le cercle Bullet Track et son curseur ont été retirés de l'interface car ils
  n'ont plus aucune fonction de sélection : le texte indique maintenant que
  l'acquisition verte est globale, même si le joueur vise le ciel.
- La compilation Debug x86 réussit après l'ensemble de ce jalon. Prochaine tâche :
  produire le Release V4 séparé et vérifier l'exécutable livré.

### Journal — livraison Release V4 (28 août 2026)

- Le nom de sortie devient `HDFinalAdvancedV4.exe`, séparé du V3 encore actif;
  aucune tentative n'a été faite pour écraser, lancer ou tuer un processus de la
  session de jeu pendant la génération.
- La compilation `Release x86` réussit sans erreur. `dumpbin` confirme la machine
  `14C (x86)`, le sous-système `Windows GUI` et une taille de 429568 octets.
- Exécutable livré :
  `build/vs2026-x86/Release/HDFinalAdvancedV4.exe`.
- SHA-256 :
  `FD442C42183D7344AB4A61D8D849C5EB75A55EFD1A46A6CE300C2E2594157FFC`.
- CTest ne déclare toujours aucun test automatisé dans ce projet. Le jeu
  `hde.exe` PID 2696 et le V3 PID 18024 sont restés intacts pendant la livraison.
  Au premier lancement manuel du V4, son nouveau handoff doit fermer proprement
  ce V3, attendre sa restauration, puis se rattacher au jeu déjà ouvert.
- Validation en jeu attendue : geste rapide droite/gauche entre deux cibles,
  absence de perception et de tir ennemis, tirs vers le ciel redirigés sur une
  tête verte, puis `²` sans crash avec `Free hands` en index zéro.

### Journal — régression critique signalée sur le V4 (29 août 2026)

- Le premier retour réel indique que `HDFinalAdvancedV4.exe` plante et entraîne
  également la fermeture de `hde.exe`. Le V4 ne doit plus être relancé avant
  identification de la faute.
- Les changements à auditer en priorité sont le handoff automatique V3→V4 et le
  nouveau détour central Bullet Track; l'inventaire `²` ne peut être responsable
  au démarrage tant que sa commande n'a pas été demandée.
- Prochaine action : relever les modules fautifs et codes d'exception dans le
  journal Application de Windows, contrôler les processus restants, puis retirer
  ou corriger uniquement le chemin démontré responsable avant toute recompilation.

### Journal — cause exacte du crash V4 établie (29 août 2026)

- Deux dumps locaux ont été analysés : `hde.exe.2696.dmp` tombe à
  `hde+0xD97ED` en écriture sur l'adresse `0x00000008`, et
  `hde.exe.15204.dmp` à `hde+0xD92D8` en lecture d'un lien de bloc invalide.
  Les deux piles traversent l'allocateur MSVC6 interne : le tas du jeu avait été
  corrompu avant que l'exception soit levée.
- La faute primaire de `²` est confirmée dans le stub : `C_actor::cbProc` reçoit
  trois arguments explicites (`msg`, `prm1`, `prm2`), mais le V4 en poussait
  quatre. Son `ret 0x0C` laissait donc quatre octets parasites avant `popad`, qui
  restaurait ensuite les registres et la pile depuis de mauvaises adresses.
- Une seconde faute existait dans le contenu : le V4 passait les lignes 1–115 à
  `CB_SETINVENTORY`, alors que le vrai `fullhands` du code source appelle
  `CB_CHEAT` avec l'identifiant 9 et n'ajoute qu'une liste native contrôlée de
  29 objets/munitions. L'ordre `Free hands` ne pouvait pas rendre sûrs les 115
  identifiants arbitraires.
- Correction décidée : supprimer entièrement le faux vecteur distant et appeler
  sur le thread principal le vrai `CB_CHEAT(9, 0)`, avec exactement trois pushes.
  Les écritures directes dans les bornes des vecteurs IA seront aussi retirées :
  elles peuvent courir contre le thread moteur et ne sont pas acceptables après
  une preuve de corruption du tas.

### Journal — suppression des deux chemins de corruption (29 août 2026)

- Le trampoline `²` compilé pousse désormais uniquement `prm2=0`,
  `prm1=CHEAT_FULLHANDS(9)` et `msg=CB_CHEAT(14)`. Le `ret 0x0C` natif dépile
  exactement ces douze octets avant `popad`; aucun décalage de pile ne subsiste.
- Le faux `vector<S_item>` de 115 entrées est entièrement exclu de la compilation.
  Le moteur exécute sa propre liste fullhands de 27 entrées, via ses méthodes
  `ItemIndex`, `SetItemAmount` et `AddItem`, sans recevoir de mémoire fabriquée
  par `VirtualAllocEx`.
- Le mécanisme de perception n'écrit plus ni `begin`, ni `end`, ni commandes dans
  les `std::vector` des ennemis depuis le thread externe. Seul le patch de code
  `IsEnemy` déjà borné et restaurable reste actif pour l'instant; l'invisibilité
  doit être renforcée plus tard par un appel natif sur le thread du jeu, jamais
  par une mutation concurrente de conteneur.
- Les textes de l'interface annoncent maintenant « Fullhands natif sûr » et 27
  entrées au lieu de promettre à tort 115 lignes arbitraires.
- La compilation Debug x86 réussit après ces suppressions. Prochaine action :
  renommer la livraison en V5, construire le Release et vérifier son architecture
  et son empreinte sans lancer le jeu ni le trainer.

### Journal — livraison corrective V5 après analyse des dumps (29 août 2026)

- Microsoft documente `0xC0000005` comme une lecture/écriture/exécution d'adresse
  invalide et recommande précisément l'examen du type d'accès, de l'adresse, des
  registres et de la pile; cette méthode a été appliquée aux deux dumps locaux.
  La documentation PageHeap confirme également qu'une faute peut n'être levée
  qu'au contrôle/libération ultérieur d'un bloc déjà endommagé.
- Le titre et la sortie deviennent `HD Final Advanced V5` et
  `build/vs2026-x86/Release/HDFinalAdvancedV5.exe`; le V4 dangereux n'est ni
  écrasé ni relancé.
- La compilation Release réussit. `dumpbin` confirme `14C (x86)` et le
  sous-système `Windows GUI`; taille : 428544 octets.
- SHA-256 :
  `AD0CDBF53B256904551DA42A5E2E2B7980626390EE527F78B7089C93599FDB6A`.
- CTest ne contient aucun test automatisé. Aucun `hde.exe` ne restait actif au
  moment du contrôle; le V4 PID 11680 est resté intact. Au lancement manuel, le
  handoff V5 lui demandera de fermer proprement avant toute connexion au nouveau
  processus du jeu.
- Le crash prouvé est corrigé statiquement, mais la validation finale doit être
  faite dans un nouveau processus `hde.exe`, car un tas déjà corrompu ne peut pas
  être réparé après coup. Tester d'abord V5 sans option, puis `²`, puis Bullet
  Track; l'invisibilité reste volontairement limitée au patch `IsEnemy` tant que
  son reset natif main-thread n'est pas validé.

### Journal — retour réel V5 et périmètre V6 (29 août 2026)

- **Aimbot et Bullet Track validés en jeu** : les deux fonctions sont décrites
  comme très puissantes et fonctionnent parfaitement. Leurs hooks deviennent des
  chemins gelés et ne doivent plus être modifiés pendant ce cycle.
- **Vitesse véhicule** : la modification ne répond pas correctement et `F8/F9`
  sont déjà utilisées. Elles seront remplacées par deux touches sans conflit,
  avec vérification de la détection du véhicule réellement contrôlé.
- **Invisibilité** : le patch `IsEnemy` seul, volontairement conservé après le
  retrait des écritures de vecteurs dangereuses, ne suffit pas. La nouvelle
  solution doit exécuter le reset de perception par une routine native sur le
  thread du jeu, sans mutation externe concurrente.
- **Téléportation carte** : l'ouverture/clic carte ne produit pas la mutation
  attendue. Le chemin d'entrée, la conversion carte→monde et l'écriture du frame
  joueur doivent être audités séparément.
- **`²` plante encore le jeu en V5** : ne pas supposer que la correction de pile
  était suffisante. Les nouveaux événements/dumps seront comparés aux précédents
  avant de modifier l'appel natif.
- Ordre V6 : touches/contexte véhicule, invisibilité native, téléportation carte,
  crash `²`, compilation et journal après chacun; Aimbot/Bullet Track hors scope.

### Journal — cause V5 et remplacement sûr de `²` (29 août 2026)

- Deux événements V5 consécutifs à 00:20 et 00:28 montrent la même violation
  d'accès `0xC0000005` à `0x26091C39`, une adresse hors de `hde.exe` située dans
  la page de trampoline allouée par le trainer. Le crash est donc attribué au
  stub `²`, sans implication des hooks Aimbot/Bullet Track désormais gelés.
- Le désassemblage du binaire installé tranche l'erreur d'objet virtuel : la
  branche native fullhands `0049FE4D..0049FE55` appelle
  `C_game_mission::BroadcastMessage` par `[vtable+0x30]` sur la mission dans
  `ECX`. Le V5 chargeait le joueur puis appelait `[vtable+0x04]`; les constantes
  14, 9 et 0 étaient correctes, mais la méthode et l'objet ne l'étaient pas.
- Le nouveau stub conserve l'instance mission fournie à l'entrée de
  `ProcessCheat` et reproduit exactement `BroadcastMessage(CB_CHEAT, 9, 0)`.
  La boucle active de synchronisation a été supprimée du thread du jeu : le
  stub acquitte puis retourne immédiatement, tandis que le trainer restaure le
  prologue et vérifie qu'aucun thread n'occupe plus la page avant de la libérer.
- La compilation `Debug x86` réussit sans erreur. Cette correction n'a modifié
  ni l'Aimbot ni le Bullet Track. Prochaine tâche : réparer la détection et
  l'écriture de vitesse du véhicule avec de nouvelles touches sans conflit.

### Journal — acquisition véhicule renforcée et touches remplacées (29 août 2026)

- `F8/F9` ne sont plus utilisées. Page Haut (`VK_PRIOR`) augmente désormais le
  multiplicateur contextuel et Page Bas (`VK_NEXT`) le réduit; les deux libellés
  Super Run et Super vitesse véhicule indiquent explicitement ces touches.
- Les offsets installés restent ceux déjà validés dans le binaire : sièges à
  `+0x12C`, siège conducteur zéro, utilisateur à `+0x0C` et vitesse à `+0x230`.
  L'écriture de vitesse n'a donc pas été déplacée vers un champ hypothétique.
- La cause de non-détection la plus fragile est supprimée : `using_item` à
  `player+0x250` reste le chemin rapide, mais s'il est nul pendant une transition
  d'animation, le module parcourt le vecteur acteur natif borné de la mission et
  recherche le véhicule dont le siège zéro référence exactement le joueur local.
- `ACTOR_AUTOMOBIL (16)` et `ACTOR_AUTO_CANNON (8)` sont acceptés, puisque le
  second hérite de la même disposition automobile. Type, frame, retour acteur,
  drapeau conducteur et utilisateur local sont tous validés avant écriture.
- La compilation `Debug x86` réussit sans erreur. Aimbot et Bullet Track n'ont
  pas été modifiés. Prochaine tâche : rendre l'invisibilité effective par une
  intervention native dans le thread du jeu, sans toucher aux vecteurs IA.

### Journal — purge native de la perception et des attaques existantes (29 août 2026)

- Le défaut du V5 est séparé en deux états : le patch `C_player::IsEnemy=false`
  empêche correctement les nouvelles acquisitions, mais une entrée déjà reconnue
  dans `watch_actors` et un `PRG_ATTACK` déjà construit restent utilisables sans
  rappeler `IsEnemy`. C'est pourquoi les ennemis déjà alertés continuaient à voir
  et tirer sur le joueur.
- Le désassemblage installé corrige définitivement la disposition MSVC6 : le
  compteur de scan est à `enemy+0x298`, le vecteur `watch_actors` à `+0x29C`, le
  début/fin du programme à `+0x288/+0x28C`, `S_command::subject` à `+0x04` et
  `prg_item` à `+0x2C`. Les anciens offsets externes décalés de quatre octets ne
  sont plus compilés dans le chemin actif.
- À la première activation, un trampoline temporaire exécuté sur le thread
  principal parcourt seulement les ennemis vivants du snapshot. Il appelle leur
  méthode virtuelle native `DelProgram` (`vtable+0x28`) pour chaque
  `PRG_ATTACK` dont le sujet est exactement le joueur et retire le
  `PRG_MOVE/MR_ATTACK_REACH` précédent lorsqu'il forme la paire de poursuite.
- Le même trampoline relâche `holding_fire`, fixe le compteur de rescan à 1000
  puis appelle l'effacement de plage natif installé à `hde+0x39CB0` sur
  `watch_actors`. Les destructeurs/déplacements du conteneur restent donc gérés
  par le moteur, sans écriture concurrente de `begin/end` depuis le trainer.
- Les signatures de `ProcessCheat` et de la routine vectorielle sont vérifiées
  avant pose; le prologue est restauré et l'absence de thread dans la page est
  contrôlée avant libération. Un échec est retenté au plus une fois par seconde.
- La compilation `Debug x86` réussit. Aimbot et Bullet Track restent inchangés.
  Prochaine tâche : corriger le chemin F12/clic/conversion/pose de la
  téléportation via carte native.

### Journal — offsets et état natifs de la téléportation carte (29 août 2026)

- La lecture seule de la session ouverte a démontré que les anciens champs
  `mission+0x98/+0x9C` ne sont pas `map_mgr/map_active` : `+0x9C` contenait le
  pointeur `spr_cross` et son premier octet valait `0xE0`, impossible pour un
  booléen. Le code déclarait donc la carte indisponible avant toute conversion
  ou écriture de position.
- La disposition installée est désormais confirmée à `map_mgr = +0xA0` et
  `map_active = +0xA4`. Le décalage provient des deux `std::vector` MSVC6
  précédents, chacun doté d'un champ allocateur de quatre octets absent d'une
  représentation moderne à trois pointeurs. Le vrai gestionnaire mène à une
  scène de carte et une matrice vue-projection cohérentes.
- L'état `teleport_map_open` n'est plus inversé dès que `SendInput` accepte la
  barre d'espace. À chaque tick, il reflète maintenant le booléen natif et
  exige un gestionnaire sain lorsque la carte est active; un clic n'est donc
  capturé qu'après confirmation effective de l'ouverture par le moteur.
- Le même relevé valide séparément le dernier maillon : le frame du joueur est
  sain et ses positions monde `+0xBC` et locale `+0x14C` sont identiques. La
  pose contrôlée de la destination et le drapeau `FRMFLAGS_UPDATE_NEEDED`
  restent conformes à `I3D_frame::SetPos` dans les sources du moteur.
- Un outil de diagnostic strictement en lecture seule, `tools/map_probe.ps1`,
  conserve la vérification de cette disposition sans lancer un second trainer
  ni modifier la mémoire du jeu.
- La compilation `Debug x86` réussit après la correction de `radar.cpp` et
  `gameplay_mods.cpp`. Aimbot et Bullet Track n'ont pas été modifiés. Prochaine
  tâche : produire, identifier et contrôler la Release x86 V6.

### Journal — livraison Release x86 V6 (29 août 2026)

- Le titre et la sortie deviennent `HD Final Advanced V6` et
  `build/vs2026-x86/Release/HDFinalAdvancedV6.exe`. Le V5 encore ouvert n'a
  été ni fermé, ni écrasé, ni remplacé pendant la compilation.
- La cible principale compile sans erreur en Release. `dumpbin` confirme la
  machine `14C (x86)`, la magie `10B (PE32)` et le sous-système `Windows GUI`.
  Taille finale : `435712` octets.
- SHA-256 final :
  `61DDC4617AD4F06A0BF95E9FA2D3BC24DF798E53CDBD4E46D3AAD33728C2C0D7`.
- Le contrôle des chaînes confirme ensemble la téléportation carte F12, le
  fullhands natif sûr, l'Aimbot, le Bullet Track et le réglage véhicule Page
  Haut/Page Bas. Un ancien texte d'état mentionnant encore F8/F9 a été détecté
  pendant le premier contrôle, corrigé, puis la Release a été reconstruite; la
  chaîne obsolète est absente du binaire final.
- CTest ne déclare aucun test automatisé. Aucune instance V6 n'a été lancée;
  `hde.exe` PID 19176 et `HDFinalAdvancedV5.exe` PID 15448 sont restés intacts,
  et le diagnostic de carte sur la session a été strictement en lecture seule.
- Au lancement manuel, le handoff existant doit demander au V5 de se fermer
  proprement, attendre la restauration de ses hooks, puis rattacher le V6 au
  jeu déjà ouvert. Validation en jeu recommandée dans l'ordre : démarrage sans
  option, Page Haut/Page Bas en siège conducteur, invisibilité face à un ennemi
  déjà alerté, F12 puis clic carte, et enfin `²`. Aimbot et Bullet Track restent
  les chemins validés et gelés.

### Journal — invisibilité V6 validée en jeu (29 août 2026)

- Le retour utilisateur confirme que l'invisibilité ennemie fonctionne bien en
  situation réelle. La purge native de `watch_actors` et des programmes
  d'attaque existants, combinée au blocage des nouvelles acquisitions par
  `IsEnemy`, est donc validée en jeu.
- Ce chemin rejoint l'Aimbot et le Bullet Track parmi les fonctions gelées :
  aucune modification de son hook, de ses offsets ou de sa restauration ne
  doit être faite sans nouvelle régression observée.
- Aucun changement de code ni nouvelle compilation n'a été nécessaire pour ce
  retour de validation.

### Journal — retour de F8/F9 pour les vitesses contextuelles, Release V7 (29 août 2026)

- À la demande de l'utilisateur, Page Haut/Page Bas sont remplacées par `F8`
  pour augmenter et `F9` pour réduire. Les entrées Win32 utilisent désormais
  `VK_F8/VK_F9`, et tous les libellés visibles ont été mis en cohérence.
- Les deux fonctions partagent volontairement ces touches : à pied, elles
  modifient le multiplicateur Super Run; lorsqu'un siège conducteur valide est
  détecté, le Super Run est suspendu et les mêmes touches modifient uniquement
  le multiplicateur du véhicule. Si les deux options sont cochées, le contexte
  choisit donc automatiquement la destination du réglage.
- La sortie devient
  `build/vs2026-x86/Release/HDFinalAdvancedV7.exe` afin de préserver le V6
  encore ouvert. La compilation Release réussit sans erreur.
- Vérifications : machine `14C (x86)`, magie `10B (PE32)`, sous-système
  `Windows GUI`, taille `435712` octets et SHA-256
  `6706EFC499788FDA76A9E41482BFA2DD6E5031D78366FBBE5FB456169408EBFA`.
- Les trois chaînes F8/F9 attendues sont présentes et aucun texte Page
  Haut/Page Bas ne subsiste dans le binaire. La V7 n'a pas été lancée;
  `HDFinalAdvancedV6.exe` et `hde.exe` sont restés actifs et intacts pendant la
  construction.

### Journal — cause exacte du nouveau crash `²` en V7 (29 août 2026)

- Le nouvel événement Application à 01:10:42 est une violation d'accès
  `0xC0000005` dans une page distante à `0x05CB1FFD`. Le dump correspondant est
  `%LOCALAPPDATA%/CrashDumps/hde.exe.9872.dmp`.
- Les octets du dump identifient sans ambiguïté la page comme le trampoline de
  purge d'invisibilité, et non celui du fullhands. `²` ne faisait que fournir
  la touche utilisée pour déclencher `ProcessCheat` au moment où cette purge
  était encore en attente.
- L'instruction fautive est `mov ecx,[eax+edi*4]`. Le contexte d'exception
  contient `eax=0x645F` et `edi=0x3FFFE6E7`, donnant l'adresse invalide
  `0xFFFFFFFB`. Un acteur ennemi obsolète ou réutilisé exposait donc un vecteur
  programme incohérent avec `end < begin`.
- Le stub vérifiait seulement que `begin` était non nul avant de calculer
  `(end-begin)/4`; il ne contrôlait ni les bornes des pointeurs, ni leur
  alignement, ni l'ordre `begin/end`, ni un nombre maximal de commandes. La
  correction doit ajouter ces gardes dans le thread du jeu avant toute lecture
  indexée, ainsi que des gardes équivalentes avant l'effacement de
  `watch_actors`.

### Journal — trampoline d'invisibilité borné avant le déclenchement `²` (29 août 2026)

- Le parcours de `program` valide désormais dans le thread principal : pointeurs
  compris entre `0x10000` et `0x7FFFFFFF`, `end >= begin`, différence alignée
  sur quatre octets et maximum de 128 commandes. Le cas exact du dump V7 est
  redirigé vers la fin du bloc avant la soustraction dangereuse.
- Chaque pointeur `S_command` courant ou précédent est aussi contrôlé avant la
  lecture de `prg_item`, `subject` ou `move_reason`. Une commande incohérente
  est ignorée sans empêcher le traitement des autres ennemis.
- Avant l'appel natif d'effacement, `watch_actors` exige maintenant des bornes
  saines, un ordre et un alignement valides, avec un maximum de 4096 entrées.
  Aucun couple `begin/end` incohérent ne peut atteindre `hde+0x39CB0`.
- Le fullhands conserve son appel natif `BroadcastMessage(CB_CHEAT, 9, 0)`; le
  défaut prouvé ne se trouvait pas dans cet appel. C'est le trampoline de purge
  que la touche `²` déclenchait au même point `ProcessCheat` qui est sécurisé.
- La compilation `Debug x86` réussit sans erreur. Aimbot, Bullet Track, Super
  Run, téléportation et logique effective d'invisibilité ne sont pas modifiés.
  Prochaine tâche : observer le camion réellement conduit et corriger son
  acquisition ou son champ de vitesse selon les valeurs relevées.

### Journal — disposition automobile installée relevée en direct (29 août 2026)

- Le relevé strictement en lecture seule de la mission courante trouve trois
  automobiles et trois auto-canons. Leur disposition est cohérente entre les
  instances : `num_of_seats` est à `+0x13C`, le premier `S_seat` commence à
  `+0x140`, puis les huit sièges de `0x20` octets se terminent à `+0x240`.
- Dans chaque siège, le drapeau conducteur est à `+0x00`, les frames de siège
  et d'entrée à `+0x04/+0x08`, et l'utilisateur à `+0x0C`. Le joueur conducteur
  doit donc être comparé à `vehicle+0x14C`, et non à l'ancien `+0x138`.
- Le mot à `vehicle+0x240` est un flottant de carburant plausible (`5`, `30`
  ou `31` litres selon l'instance), le pointeur son suit à `+0x248`; cela place
  sans ambiguïté `speed` à `+0x244`. L'ancien `+0x230` visait la fin du dernier
  siège et restait généralement nul.
- La cause du retour utilisateur est donc double mais unique dans son origine :
  `ResolveControlledVehicle` cherchait les sièges à `+0x12C` et ne passait
  jamais en contexte véhicule; même forcée, l'écriture à `+0x230` n'atteignait
  pas la vitesse. Le correctif doit employer `+0x140/+0x244` et conserver le
  choix contextuel F8/F9 déjà validé pour le Super Run.
- `tools/vehicle_probe.ps1` conserve ce relevé reproductible sans aucune
  écriture dans `hde.exe` et sans lancer un second trainer.

### Journal — acquisition et vitesse camion corrigées (29 août 2026)

- `kAutomobileSeatsOffset` passe de l'adresse erronée `+0x12C` à `+0x140`.
  La garde existante reconnaît donc maintenant le conducteur par le drapeau
  de `seat[0]` et par `seat[0].user == local_player` à `vehicle+0x14C`.
- `kAutomobileSpeedOffset` passe de `+0x230`, qui appartenait encore à la zone
  des sièges, au membre réel `speed` à `+0x244`. La multiplication, les bornes,
  le suivi et la restauration existants agissent désormais sur le bon flottant.
- F8/F9 gardent leur logique contextuelle : en siège conducteur avec l'option
  véhicule active, ils ajustent uniquement le multiplicateur véhicule et le
  Super Run est suspendu; à pied, ils ajustent uniquement le multiplicateur du
  joueur. Aucun nouveau conflit de touches n'est introduit.
- L'outil de lecture seule a été aligné sur les deux offsets corrigés. La
  compilation `Debug x86` réussit et produit
  `build/vs2026-x86/Debug/HDFinalAdvancedV7.exe` sans erreur.
- Aimbot, Bullet Track, invisibilité, téléportation, fullhands et Super Run
  n'ont reçu aucune modification fonctionnelle pendant ce correctif.

### Journal — livraison Release x86 V8 (29 août 2026)

- Le titre devient `HD Final Advanced V8` et la sortie finale est
  `build/vs2026-x86/Release/HDFinalAdvancedV8.exe`. Le changement de nom
  préserve la V7 encore ouverte et évite tout remplacement de son exécutable.
- La compilation Release réussit sans erreur. L'en-tête final confirme la
  machine `0x014C (x86)`, la magie `0x010B (PE32)` et le sous-système
  `0x0002 (Windows GUI)`. Taille : `437760` octets.
- SHA-256 final :
  `035B0625C07DE857DB483334FAD3DDFE8382A1A04DCCEA0DB4899A84A75976CF`.
- Les chaînes UTF-8 confirment les deux libellés F8/F9 du Super Run et de la
  vitesse véhicule; aucune mention Page Haut/Page Bas ne subsiste. Le titre
  V8 est présent et le titre V7 est absent du nouveau binaire.
- La V8 regroupe le bornage du trampoline d'invisibilité qui supprimait le
  crash déclenché au passage par `²`, ainsi que la disposition automobile
  installée `seats+0x140`, `seat[0].user+0x14C`, `speed+0x244`.
- `ctest` ne trouve aucun test automatisé déclaré. La Release n'a pas été
  lancée : `hde.exe` PID 18152 et `HDFinalAdvancedV7.exe` PID 8340 sont restés
  ouverts et intacts pendant la construction et les contrôles.
- Validation manuelle attendue : lancer la V8, cocher la vitesse véhicule,
  entrer comme conducteur, maintenir F8 tout en accélérant puis F9 pour
  réduire; enfin activer l'invisibilité et presser `²` pour confirmer que le
  fullhands se déclenche sans fermeture du jeu.

### Journal — reprise V9 et demandes de validation en jeu (29 août 2026)

- Le retour V8 confirme que l'invisibilité bloque correctement les tirs, mais
  relève encore les réactions vocales des ennemis. Le prochain correctif doit
  donc neutraliser aussi l'acquisition/réaction, sans modifier les chemins
  Aimbot et Bullet Track déjà validés.
- La vitesse automobile agit actuellement sur le membre `speed`, tandis que la
  boîte de vitesses native continue à démarrer au premier rapport. Le besoin V9
  est de sélectionner immédiatement le dernier rapport valide afin que F8/F9
  règlent une accélération disponible dès le départ.
- Le bouton avancé `Fullhands natif sûr [²]` et le cheat configurable
  `Inventaire Complet` sont deux chemins distincts. Le premier ouvre encore la
  boîte native `Select item`; la V9 doit proposer un choix explicite `All` qui
  accorde l'ensemble des entrées proposées, sans fabriquer un conteneur depuis
  le thread du trainer.
- L'invisibilité doit conserver un seul interrupteur principal, suivi de deux
  choix mutuellement exclusifs : joueur actuellement contrôlé ou escouade
  entière. La portée sélectionnée devra être utilisée à la fois pour bloquer
  l'acquisition et pour purger les réactions déjà engagées.
- Chaque diagnostic, correctif et compilation de ce jalon sera ajouté au plan
  immédiatement après la tâche correspondante. La sortie finale sera une V9
  séparée afin de préserver la V8.

### Journal — dernier rapport immédiat pour la vitesse véhicule V9 (29 août 2026)

- La source installée confirme que `C_automobil::transl` représente la boîte
  native (`-1` marche arrière, `0` neutre, `1..4` rapports avant) et que
  `Tick1` choisit sa limite dans `wanted_speed[transl+1]`. Le désassemblage
  confirme le membre `transl` à `vehicle+0x258`.
- Lorsque l'option vitesse véhicule est active, le sens avant engagé et le
  multiplicateur supérieur à `1x`, le trainer valide désormais `speed`,
  `transl` et `proposed_forward`, puis place directement `transl=4`. Il n'est
  donc plus nécessaire d'attendre les passages séquentiels 1→2→3→4 avant que
  le réglage F8/F9 agisse sur le dernier rapport.
- La marche arrière (`transl=-1`) n'est jamais remplacée. À `1x`, la boîte
  native n'est pas modifiée. Toute valeur hors de la plage installée `-1..4`
  ou tout booléen avant incohérent produit l'état disposition non supportée au
  lieu d'une écriture.
- La compilation `Debug x86` réussit et produit
  `build/vs2026-x86/Debug/HDFinalAdvancedV8.exe`. Le jeu n'étant plus ouvert,
  aucune écriture ou validation dynamique n'a été effectuée; le comportement
  devra être confirmé lors du test manuel de la Release V9.

### Journal — choix Fullhands « objet / All » sécurisé V9 (29 août 2026)

- Le diagnostic sépare définitivement les deux comportements : la boîte native
  `WinSelectItem` ne peut retourner qu'un index existant, tandis que le message
  natif `CB_CHEAT/CHEAT_FULLHANDS` accorde directement les 27 entrées prévues
  par le jeu. Ajouter un faux index `All` dans la liste du jeu aurait exposé le
  code appelant à un accès hors tableau.
- Le bouton `Inventaire Complet` et son raccourci F5 ouvrent maintenant un petit
  dialogue contrôlé par le trainer avec deux choix exclusifs. `Choisir un objet`
  conserve la boîte native inchangée; `All` appelle le chemin natif global déjà
  borné et restauré.
- Le bouton avancé est renommé `All - tout le Fullhands [²]`. Un clic ou la
  touche physique `²` déclenche directement le même chemin `All`, sans devoir
  parcourir successivement les lignes de la boîte native.
- L'ancien curseur arbitraire de nombre d'items est retiré de l'interface afin
  qu'« All » signifie toujours l'ensemble natif exact. Les réglages historiques
  restent lisibles pour compatibilité, mais ne déterminent plus l'action All.
- La compilation `Debug x86` réussit sans erreur ni avertissement après le
  nettoyage du paramètre d'interface devenu inutile. Le jeu n'était pas ouvert;
  aucun dialogue ou inventaire réel n'a été modifié pendant ce contrôle.

### Journal — arrêt demandé et état intermédiaire exact de l'invisibilité V9 (29 août 2026)

- À la demande de l'utilisateur, le travail est arrêté avant la fin du jalon
  invisibilité afin de préserver la limite disponible. Aucune compilation ni
  Release n'a été lancée après les changements intermédiaires ci-dessous.
- Le diagnostic source et désassemblage est terminé : `WatchHumans` appelle
  `C_player::IsEnemy` sur chaque joueur candidat, ce qui explique que l'ancien
  patch commun rende toute l'escouade invisible. La voix courante d'un ennemi
  est le pointeur installé `enemy+0x280`; le jeu l'arrête lui-même avec
  `I3D_sound::SetOn(false)` à `vtable+0x68`.
- La disposition de `watch_actors` est aussi confirmée : chaque
  `S_watch_actor` mesure `0x20` octets et son premier champ est le pointeur de
  l'acteur observé. Une portée locale doit donc effacer uniquement l'élément
  dont ce champ désigne le joueur contrôlé; elle ne doit pas vider le vecteur
  entier comme le mode escouade.
- Modifications déjà écrites mais **encore incomplètes et non compilées** :
  `EnemyInvisibilityScope` a été ajouté avec les valeurs `ControlledPlayer` et
  `WholeSquad`; `GameplaySettings` mémorise cette portée (mode escouade par
  défaut pour conserver le comportement historique); l'interface affiche deux
  boutons radio mutuellement exclusifs sous la case d'activation; enfin l'état
  interne du patch possède les futurs champs `protected_player`, `remote`,
  `remote_size`, `patch` et `scope`.
- Travail restant avant toute validation : construire le trampoline
  `IsEnemy` conditionnel qui ne retourne faux que pour le joueur contrôlé en
  mode local; conserver le retour faux global en mode escouade; adapter la
  purge des `PRG_ATTACK` et de `watch_actors` à la portée; arrêter la voix
  uniquement lorsqu'une attaque correspondante est supprimée; gérer sûrement
  le changement de portée ou de joueur et la restauration/libération du code
  distant.
- Après cette implémentation, il restera à compiler en `Debug x86`, corriger
  tout avertissement ou erreur, journaliser le jalon invisibilité, renommer le
  titre et la sortie en V9, compiler la `Release x86`, contrôler PE32/GUI,
  chaînes, taille, SHA-256, absence de lancement et résultat CTest, puis
  finaliser ce plan. Les fichiers actuels ne doivent donc pas être considérés
  comme une version testable tant que la compilation Debug n'a pas réussi.

### Journal — invisibilité sans réaction et portée exclusive terminées V9 (29 août 2026)

- Le patch global fixe de cinq octets a été remplacé par un trampoline x86
  réversible. En portée `ControlledPlayer`, il compare `ECX` — le `this` de
  `C_player::IsEnemy` — à l'adresse du joueur actuellement contrôlé : seul ce
  joueur retourne alors « non ennemi », tandis que le prologue natif complet
  est rejoué pour les autres membres de l'escouade. En portée `WholeSquad`, le
  retour faux global historique est conservé.
- Les deux boutons radio ajoutés précédemment sont donc maintenant réellement
  fonctionnels et mutuellement exclusifs. Un changement de portée ou de joueur
  contrôlé restaure le prologue, attend qu'aucun thread ne se trouve dans la
  page distante, libère celle-ci, puis construit le trampoline adapté.
- Le nettoyage d'IA ne se limite plus au tir. Pour chaque ennemi valide, il
  supprime les `PRG_ATTACK` visant la portée protégée et leur éventuel
  `PRG_MOVE/MR_ATTACK_REACH`, relâche `holding_fire`, force un nouveau scan et
  retire l'état de surveillance. En mode local, seule l'entrée `S_watch_actor`
  de `0x20` octets dont le premier champ est le joueur contrôlé est effacée ;
  en mode escouade, la liste entière est effacée comme dans le comportement V8
  validé.
- La voix à `enemy+0x280` est arrêtée par sa méthode native
  `I3D_sound::SetOn(false)` à `vtable+0x68` lorsqu'une attaque ou une entrée de
  surveillance protégée est effectivement supprimée. L'objectif est donc bien
  « aucune réaction, comme si la cible était absente ou hors de portée », et
  pas seulement l'absence de tirs.
- Les bornes du trampoline temporaire restent strictes : types d'acteurs,
  pointeurs, ordre et alignement des vecteurs, maximum de 128 commandes et de
  4096 structures de surveillance. La taille maximale de `watch_actors` est
  corrigée selon le pas réel `0x20`, au lieu de l'ancien pas de pointeur `4`.
- La restauration vérifie désormais l'exact saut installé, confirme le retour
  des octets originaux et l'absence de thread dans la page avant de la libérer.
  Une écriture inconnue n'est jamais écrasée et une page potentiellement active
  n'est jamais libérée.
- Trois compilations incrémentales `Debug x86`, dont la compilation finale
  après le texte d'état mis en cohérence, réussissent sans erreur ni
  avertissement. Binaire de travail :
  `build/vs2026-x86/Debug/HDFinalAdvancedV8.exe`. Ni `hde.exe` ni ce binaire
  n'ont été lancés ; la validation dynamique reste à faire sur la Release V9.

### Journal — vrai boost de départ véhicule et profils AZERTY V9 (29 août 2026)

- Le nouveau retour utilisateur a permis d'isoler la limite du modèle V8 : le
  trainer écrivait `target = vitesse_native × multiplicateur`, puis divisait
  la valeur relue par ce même multiplicateur au cycle suivant. L'incrément
  ajouté entre-temps par `C_automobil::Tick1` était ainsi divisé puis
  remultiplié, donc non amplifié. Au départ, `speed == 0` donnait toujours
  `0 × multiplicateur == 0`. Une pause/reprise pouvait seulement exposer un
  gros delta temporel natif et produire le bond observé par l'utilisateur.
- Le code ne multiplie plus la vitesse totale. Il établit d'abord une ligne de
  base, attend qu'un vrai tick du véhicule modifie `speed`, puis ajoute pendant
  que l'accélérateur physique `Z` est maintenu une accélération bornée de
  `(multiplicateur - 1) m/s²`. Le départ devient donc rapide dès le premier
  tick moteur réel, même depuis zéro, sans amplification exponentielle.
- Le temps pris en compte par mise à jour est plafonné à `50 ms` et le boost
  exige une modification réelle de `speed` depuis la dernière écriture. La
  pause ne peut donc plus accumuler en secret une impulsion gigantesque. La
  vitesse boostée est plafonnée à `120 m/s` ; marche arrière, freinage et mode
  `1x` restent natifs.
- Le dernier rapport `transl=4` n'est maintenant sélectionné que lorsque les
  quatre conditions sont réunies : option active, `Z` réellement maintenu,
  sens avant natif confirmé et multiplicateur supérieur à `1x`. Le véhicule
  au repos n'est plus laissé artificiellement en quatrième sans demande
  d'accélération.
- La désactivation ne divise plus la vitesse physique courante par le dernier
  multiplicateur. Elle arrête simplement les futures accélérations ajoutées et
  laisse l'inertie, le freinage et la décélération au moteur du jeu, ce qui
  évite une chute artificielle propre à l'ancien modèle multiplicatif.
- Les touches sont résolues par leurs positions physiques sous Windows afin de
  respecter le clavier français AZERTY : scan code `0x11` pour `Z`, puis
  `0x32..0x35` pour `,`, `;`, `:` et `!`. En siège conducteur, ces quatre
  touches choisissent directement les profils `10x`, `25x`, `50x` et `80x`.
  F8/F9 restent disponibles pour le réglage progressif contextuel existant.
- L'interface affiche maintenant explicitement « maintenir Z : départ
  boosté » et les quatre profils. La compilation finale `Debug x86` réussit
  sans erreur ni avertissement et produit
  `build/vs2026-x86/Debug/HDFinalAdvancedV8.exe`. Aucun processus du jeu ou du
  trainer n'a été lancé ; le démarrage, la pause/reprise et les quatre profils
  devront être validés manuellement avec la Release V9.

### Journal — livraison et contrôles finaux Release x86 V9 (29 août 2026)

- Le titre devient `HD Final Advanced V9` et la propriété CMake produit une
  sortie séparée :
  `build/vs2026-x86/Release/HDFinalAdvancedV9.exe`. La Release V8 de référence
  n'a pas été écrasée.
- La reconfiguration CMake et la compilation `Release x86` réussissent sans
  erreur ni avertissement. L'en-tête PE final confirme la machine
  `0x014C (x86)`, la magie optionnelle `0x010B (PE32)` et le sous-système
  `0x0002 (Windows GUI)`.
- Taille finale : `442880` octets. SHA-256 final :
  `691093746F202FE4B8423871CE9C9F65917794E04DBCA46A51AAB31DB88968EF`.
- Le contrôle binaire trouve le titre V9 et exclut le titre V8. Il confirme
  aussi les chaînes du départ avec `Z`, des profils AZERTY `, ; : !`, des
  portées `Joueur contrôlé uniquement` / `Escouade entière`, de l'état sans
  perception/poursuite/attaque/voix, du Fullhands `All` et du réglage F8/F9.
- La V8 reste présente avec sa taille historique `437760` octets et son hash
  inchangé
  `035B0625C07DE857DB483334FAD3DDFE8382A1A04DCCEA0DB4899A84A75976CF`.
- Aucun `HDFinalAdvancedV9.exe` n'a été lancé. Au contrôle final, l'ancienne V8
  était déjà ouverte (PID `13796`, démarrée à `01:45:24`) et `hde.exe` était
  déjà ouvert (PID `7868`, démarré à `09:38:08`, donc avant la création V9 à
  `09:57:17`). Ces processus n'ont été ni lancés, ni fermés, ni pilotés pendant
  la construction. Le hash du jeu ouvert reste exactement la révision attendue
  `5D5EED6174658B8FACFBC1251146B109BBB12AD9A119168AA3489F80979D62D0`.
- `ctest -C Release --output-on-failure` s'exécute correctement et indique
  qu'aucun test automatisé n'est déclaré. La compilation et les contrôles
  statiques sont terminés ; seule la validation manuelle en mission reste
  nécessaire.
- Ordre de test recommandé : lancer la V9 et laisser le handoff fermer/restaurer
  la V8 ; vérifier un démarrage sans option ; tester l'invisibilité locale face
  à un ennemi déjà alerté puis confirmer que les coéquipiers restent ciblables ;
  tester ensuite la portée escouade ; conduire avec l'option active, maintenir
  `Z`, essayer successivement `,`, `;`, `:` et `!`, puis refaire le test avec
  pause/reprise ; enfin vérifier le dialogue Fullhands `Choisir un objet / All`.
  Aimbot, Bullet Track et armes doivent faire l'objet d'un contrôle de
  non-régression, leurs chemins fonctionnels n'ayant pas été volontairement
  modifiés dans ce jalon.

### Journal — retour de validation V9 et spécification corrigée pour V10 (29 août 2026)

- La validation utilisateur confirme définitivement l'invisibilité V9 : les
  ennemis ne réagissent plus à la portée protégée. Ce chemin devient la
  référence à préserver et ne doit pas être modifié pendant les correctifs
  suivants.
- Quatre échecs restent ouverts : le boost véhicule ne produit toujours pas le
  comportement attendu, la téléportation via la carte native ne fonctionne
  pas, le choix `All` ajouté au chemin F5 ne fonctionne pas et le raccourci
  physique `²` n'accorde toujours pas l'inventaire complet.
- La spécification inventaire est corrigée sans ambiguïté : F5 doit retrouver
  uniquement son comportement naturel, directement dans le jeu, avec la boîte
  native permettant de choisir un seul objet. Il ne doit afficher aucun popup
  `All` du trainer et ne doit pas faire sortir le joueur du jeu.
- `All` appartient exclusivement au raccourci physique `²` et au bouton avancé
  correspondant. Son destinataire doit être le joueur actuellement contrôlé,
  y compris après changement de membre, et son contenu doit couvrir le
  catalogue maximal réellement accepté par la version Deluxe et la mission :
  armes, pistolets, munitions, clés et vêtements disponibles.
- À la demande de l'utilisateur, aucun nouveau correctif ne sera écrit avant un
  diagnostic approfondi. La recherche utilisera en priorité les sources
  complètes locales, le désassemblage exact de la révision installée et des
  relevés strictement en lecture seule sur `hde.exe`/V9 actuellement ouverts.
  Chaque cause devra être prouvée séparément avant implémentation.
- Après le diagnostic, chaque correction sera compilée et ajoutée
  immédiatement à ce fichier avant de passer à la suivante. La livraison sera
  une V10 séparée afin de préserver la V9 validée pour l'invisibilité.
### Journal — diagnostic avancé des quatre régressions V9 (29 août 2026)

- La validation en jeu de l'utilisateur est l'autorité fonctionnelle :
  l'invisibilité V9 est confirmée correcte et son code est désormais gelé. Les
  échecs restants sont la vitesse véhicule, le téléport par carte, le chemin
  F5 et le chemin physique `²`.
- Le comportement demandé est maintenant fixé sans ambiguïté : F5 doit saisir
  le cheat `fullhands`, ouvrir uniquement la sélection native du jeu et rendre
  immédiatement le contrôle à la fenêtre du jeu. Aucun popup `All` du trainer
  ne doit intercepter F5. `All` appartient exclusivement à la touche physique
  AZERTY `²` (scan code `0x29`) et au bouton avancé correspondant.
- Le premier probe carte exécuté dans l'écran menu a lu `68` à `+0xA4`, mais
  cet objet courant n'était pas une `C_game_mission`; ce relevé ne prouve donc
  aucun offset de mission. L'audit ultérieur de la vraie fonction installée
  `SwitchToMap` à `0x004A7363` confirme au contraire `map_mgr+0xA0` et
  `map_active+0xA4`. La tentative intermédiaire `+0xB4/+0xB8` a été annulée
  avant compilation. La cause du téléport reste à établir dans la conversion
  du clic et la collision, sur une vraie mission avec carte active.
- Le diagnostic inventaire confirme que le chemin V9 `All` ne répond pas au
  besoin : il diffuse `CB_CHEAT, 9` à la mission entière puis annonce 27 sans
  contrôler le résultat. La routine source correspondante ne contient que 27
  identifiants historiques, tandis que `inventry_1.tab` de l'installation
  Deluxe contient aussi de nombreuses armes supplémentaires, des clés et des
  uniformes. La V10 doit cibler seulement le joueur contrôlé, exécuter la
  mutation sur le thread principal du jeu et compter les entrées réellement
  traitées.
- La documentation Win32 officielle confirme que `GetKeyboardLayout(0)` vise
  le thread appelant et qu'un scan code physique reste indépendant de la
  disposition. La détection V10 de `²` et des commandes véhicule ne doit donc
  plus dépendre de la disposition du thread du trainer : elle utilisera les
  scan codes physiques AZERTY demandés (`²`, `,`, `;`, `:`, `!`) et l'état haut
  de `GetAsyncKeyState`, jamais son bit « pressed since last call » non fiable.
- Aucune correction n'a été écrite pendant ce diagnostic. Les prochaines
  étapes sont, dans l'ordre et avec journal immédiat : restaurer F5 natif,
  remplacer `²` par le vrai inventaire maximal ciblé, corriger la carte,
  corriger la vitesse sur données véhicule validées, puis produire une V10
  séparée et testable.

### Journal — F5 restauré au comportement natif uniquement (29 août 2026)

- Le popup trainer `Choisir un objet / All` ajouté en V9 a été entièrement
  retiré. Le bouton `Inventaire Complet` utilise de nouveau le contrôle cheat
  standard et F5 n'est plus intercepté ni annulé dans la boucle principale.
- Toute demande `Fullhands` provenant de F5 ou du bouton force désormais
  `InventoryMode::ChooseSpecificItem` au moment de l'appel. Un ancien réglage
  `CollectAllItems` éventuellement conservé dans le registre ne peut donc plus
  transformer F5 en parcours automatique.
- Le champ intermédiaire `fullhands_dialog_requested` a été supprimé de
  `GameplaySettings`. Le chemin avancé `²` reste séparé via
  `grant_all_items_requested` et n'est pas invoqué par F5.
- La compilation `Debug x86` réussit sans erreur et produit provisoirement
  `build/vs2026-x86/Debug/HDFinalAdvancedV9.exe`. Ce binaire intermédiaire
  n'est pas la livraison V10 et n'a pas été lancé.

### Journal — `²` séparé et catalogue Deluxe ciblé sans vecteur externe (29 août 2026)

- Le désassemblage installé du vrai choix Fullhands (`0x004A00F5`) établit
  l'ABI utile : le jeu appelle directement le callback du joueur avec
  `cbProc(CB_CHEAT=14, 9, selected_catalog_row)`. Le dernier paramètre est la
  ligne réellement choisie dans la table, et non zéro comme dans la V9.
- Le trampoline temporaire V10 cible exclusivement
  `snapshot->player_object_address`, valide sa vtable et le slot `+0x30` dans
  l'image de `hde.exe`. L'audit final du désassemblage `0x0049FE8A` montre que
  le sélecteur parcourt les 1024 lignes possibles et conserve seulement les
  catégories différentes de `0` et `4`; le trampoline reproduit maintenant ce
  filtre exact avant chaque appel. Il ne diffuse rien à l'escouade et suit
  automatiquement le membre actuellement contrôlé au moment de l'appui.
- Le chemin dangereux `CB_SETINVENTORY`/`vector<S_item>` reste définitivement
  exclu : aucun conteneur STL ni objet d'inventaire n'est fabriqué hors du tas
  du jeu. Chaque arme, munition, clé ou uniforme passe par le callback Fullhands
  natif déjà utilisé par la sélection unique.
- L'acquittement contient le nombre réellement filtré et n'est publié qu'après
  le retour de tous les callbacks. Le trainer restaure ensuite les cinq octets
  de `ProcessCheat`, vérifie qu'aucun thread n'exécute la page distante, puis
  seulement la libère. Le statut de succès annonce désormais toutes les entrées
  réellement proposées par Fullhands, pas les 27 entrées de l'ancien
  `allammo`.
- La touche physique `²` (scan code `0x29`) est traduite avec la disposition du
  thread de la fenêtre `hde.exe`, et non plus `GetKeyboardLayout(0)` du trainer.
  Le bouton avancé utilise le même chemin; F5 n'y conduit jamais.
- La compilation `Debug x86` réussit sans erreur. Le jeu est actuellement dans
  un écran sans joueur (`actor_count=0`), donc aucune mutation dynamique n'a été
  déclenchée pendant ce contrôle; le test visible du contenu reste obligatoire
  sur la V10 finale.

### Journal — clic natif de carte aligné sur le curseur interne V10 (29 août 2026)

- La source complète de `C_map_manager_imp::ProcessInput` confirme que la carte
  appelle `scene->UnmapScreenPoint(mouse_x, mouse_y, ...)` avec son propre
  curseur accumulé depuis les mouvements relatifs. Elle confirme aussi que les
  modèles de carte recopient directement les positions monde de la mission :
  les axes `X/Z` ne demandent aucune conversion d'échelle supplémentaire.
- La V9 utilisait au contraire `GetCursorPos`/`ScreenToClient`. Sous la capture
  souris du jeu, ce curseur Win32 peut rester centré ou diverger du curseur
  réellement dessiné par la carte; le rayon pouvait donc viser un autre point
  ou ne toucher aucune géométrie.
- `src/radar.cpp` repère maintenant le bloc natif exact
  `mode/world_box/map_zoom_max/mouse_x/mouse_y/look_at/look_dist/pan_speed`
  dans le gestionnaire de carte. La recherche est bornée à `0x600` octets et
  valide les invariants exacts calculés par `Init` (bornes, zoom et vitesse de
  panoramique), ce qui évite d'inventer un offset absolu dépendant des classes
  MSVC6 de cette révision.
- `ResolveNativeMapClick` emploie obligatoirement ces `mouse_x/mouse_y` avant
  d'inverser la matrice vue-projection et d'intersecter le BSP Carte, puis le BSP
  de la mission. En l'absence d'un bloc parfaitement validé, aucune écriture de
  téléportation n'est tentée.
- La compilation `Debug x86` réussit sans erreur. Le jeu étant resté dans un
  écran sans mission active, ce jalon est validé statiquement par la source, le
  désassemblage et le build; le clic réel devra encore être confirmé dans la
  V10 avant de qualifier la fonction de validée en jeu.

### Journal — départ véhicule débloqué dès le maintien de Z V10 (29 août 2026)

- La lecture complète de `C_automobil::UseAuto` et `Tick1` explique le défaut
  observé : au démarrage, `proposed_forward` reste faux pendant la mise en route
  du moteur et la transmission reste au neutre. La V9 exigeait simultanément
  `proposed_forward=true` et un changement natif de `speed` par rapport à la
  dernière écriture; à zéro, elle attendait donc précisément l'événement qu'elle
  devait provoquer. Pause/reprise finissait par fournir ce delta et masquait le
  verrou initial.
- Le boost additif V10 dépend maintenant directement du maintien de la touche
  physique AZERTY `Z` et du multiplicateur choisi. Il peut créer le premier
  incrément depuis `speed=0`, même pendant la séquence moteur/neutre; `Tick1`
  déplace ensuite nativement tout véhicule dont la vitesse est non nulle.
- Le temps ajouté reste borné à 50 ms par mise à jour et la vitesse à 120 m/s.
  Aucun temps bloqué ne peut devenir une écriture unique non bornée. La marche
  arrière n'est jamais boostée (`speed<0`) et le dernier rapport n'est forcé
  qu'après engagement réel du sens avant par le jeu.
- La détection emploie les scan codes physiques français : `Z=0x11`, puis
  `,=0x32`, `;=0x33`, `:=0x34`, `!=0x35`; les profils directs restent
  respectivement `10x`, `25x`, `50x` et `80x`.
- La compilation `Debug x86` réussit sans erreur. Aucun véhicule n'est présent
  dans l'écran actuel du jeu, donc le démarrage réel reste à confirmer avec la
  V10 finale; cette limite est distincte de la cause statique maintenant
  supprimée.

### Journal — cause certaine de Fullhands et touche M V11 (29 août 2026)

- L'audit croisé de la déclaration `C_actor`, de la vtable `C_player` fournie
  avec les sources Deluxe et du désassemblage installé invalide le chemin V10 :
  `C_player::cbProc` est le deuxième slot virtuel, donc `+0x04`. Le slot `+0x30`
  observé dans le sélecteur F5 appartient à l'objet `C_game_mission` et y
  désigne `BroadcastMessage`; l'appeler sur le joueur exécutait une autre
  méthode tout en incrémentant à tort le compteur de succès.
- `GrantCompleteDeluxeInventory` valide et appelle maintenant le slot joueur
  `+0x04`. Le filtre natif Ultimate reste inchangé : lignes `1..1023`, en
  excluant les catégories `0` et `4`, et chaque identifiant est adressé
  uniquement au joueur actuellement contrôlé.
- La commande physique `²` est supprimée de ce chemin. Le raccourci et le
  libellé avancé utilisent désormais la touche physique AZERTY `M` (scan code
  `0x27`). F5 reste strictement le sélecteur natif d'un seul objet.
- La compilation `Debug x86` de ce jalon réussit sans erreur. Le binaire garde
  provisoirement le nom V10 jusqu'au changement de version final; aucun appel
  inventaire n'a été déclenché depuis l'écran menu sans joueur actif.

### Journal — ancrages natifs de vitesse automobile V11 (29 août 2026)

- Le désassemblage exact d'Ultimate confirme les deux instructions de `Tick1`
  qui gouvernent la physique : calcul de la vitesse cible à RVA `0x4F1F3`
  (`wanted_speed[transl+1]`) et calcul de l'accélération à RVA `0x4F22C`.
  Le nouvel état de patch conserve les signatures originales de 7 et 6 octets,
  les deux trampolines et leurs adresses distantes pour permettre une
  restauration vérifiable.
- Le tableau natif `wanted_speed={-3.5,0,2,8,16,32,50,72}` prouve aussi que la
  boîte accepte jusqu'à six rapports avant. La garde V10 limitée à `4` est
  remplacée par la borne installée `6`; elle pouvait désactiver le boost sur
  les véhicules Ultimate possédant cinq ou six rapports.
- L'écriture externe répétée de `vehicle+0x244` et le forçage arbitraire du
  rapport 4 sont supprimés. Deux trampolines reproduisent d'abord les
  instructions FPU originales, puis multiplient la vitesse cible et
  l'accélération seulement si `ESI` est l'adresse du véhicule contrôlé.
- Le multiplicateur distant vaut le profil choisi uniquement pendant le
  maintien physique de `Z`; au relâchement il repasse à `1x`. Les autres
  véhicules, le moteur, les changements de rapport, le freinage, les collisions
  et `MoveMobil` restent dans leur chemin natif.
- La désactivation restaure les deux signatures originales, les relit, attend
  qu'aucun thread ne se trouve dans la page distante, puis seulement libère
  cette page. Une révision d'exécutable dont les octets diffèrent est refusée
  sans écriture.
- Les quatre profils physiques sont maintenant `C=0x2E` (10x), `V=0x2F`
  (25x), `B=0x30` (50x) et `N=0x31` (80x). Les anciens scan codes
  `, ; : !` sont retirés du traitement et le libellé de l'interface est aligné.
- La compilation `Debug x86` du nouveau chemin `Tick1` et des touches
  `C/V/B/N` réussit sans erreur. Aucun hook n'a été posé pendant ce contrôle,
  car `hde.exe` est toujours dans un écran sans automobile conduite; la
  validation dynamique reste donc à faire dans une mission.

### Journal — curseur et collision de carte natifs V11 (29 août 2026)

- Le clic ne dépend plus de `GetCursorPos/ScreenToClient`. La carte H&D capture
  la souris et accumule son propre `mouse_x/mouse_y`; exiger d'abord que le
  curseur Windows soit dans la fenêtre pouvait supprimer le clic avant même la
  lecture de ces coordonnées natives.
- Le cache BSP distingue maintenant deux usages. La visibilité des ennemis
  conserve le filtre balistique du jeu, tandis que le rayon de carte et la
  recherche du sol gardent toutes les faces collisionnables. La V10 retirait
  auparavant les matériaux traversés par les balles, bien que
  `C_map_manager_imp::ProcessInput` appelle `scene->TestCollision` sans ce
  filtre; une carte ou un terrain composé de ces matériaux pouvait donc ne
  produire aucun impact.
- Le raccourci de bascule demandé devient la touche physique AZERTY `K` (scan
  code `0x25`) et tous les textes utilisateur remplacent F12 par K. La touche
  envoyée au jeu reste Espace, qui est la commande native de
  `SwitchToMap`; K est uniquement le raccourci réservé du trainer.
- La lecture de K est placée après la construction du traducteur de scan codes
  lié à la disposition du thread du jeu; elle ne dépend donc ni de la
  disposition clavier du trainer ni d'un symbole produit par Shift/AltGr.
- L'application finale n'écrit plus directement `frame+0x14C` et ne suppose
  plus que la position locale est identique à la position monde. Après
  fermeture de la carte, un trampoline temporaire exécuté sur le thread du jeu
  appelle exactement `I3D_frame::SetPos` (vtable `+0x0C`) puis
  `I3D_frame::Update` (vtable `+0x50`), comme le chemin `debugport` des sources.
- Le trampoline valide la signature installée de `ProcessCheat`, les deux slots
  du frame et l'acquittement distant. Il restaure le prologue, attend la sortie
  de tous les threads de la page, puis la libère; aucun succès n'est annoncé
  avant cette séquence complète.
- La compilation `Debug x86` des corrections carte réussit sans erreur. Le
  processus actif étant au menu, aucune carte, aucun BSP de mission et aucun
  frame joueur ne sont disponibles pour un test dynamique; le build valide le
  chemin statique, pas encore le résultat visible en mission.

### Journal — recoupement Ultimate Mod 5.0 (29 août 2026)

- La page ModDB fournie identifie la révision comme **H&D Deluxe Ultimate Mod
  5.0**, publiée le 26 août 2026. Aucun dépôt de code source public du patch
  n'y est fourni; les sources Deluxe locales et le désassemblage du
  `hde.exe` installé restent donc les autorités techniques pour les ABI et RVA.
- La même page avertit que l'installation sous `Program Files (x86)` peut faire
  manquer du contenu personnalisé, notamment des véhicules. L'installation
  locale auditée se trouve précisément dans ce dossier. Cela n'explique pas les
  erreurs de slot/hook corrigées dans le trainer, mais devra être gardé à
  l'esprit si un véhicule ou une table Ultimate manque encore dans le jeu.

### Journal — hygiène du dépôt et livraison V11 (29 août 2026)

- Les 20 fichiers vides parasites créés à la racine par une commande mal
  redirigée (tokens de code tels que `AddLine(`, `(0x48445631U)`,
  `kMaximumSourceActors)`) ont été supprimés. La racine ne contient plus que
  les cinq fichiers du projet.
- Tous les binaires des générations précédentes ont été déplacés hors du
  répertoire de build : Release vers `archive\bin`, Debug vers
  `archive\bin-debug`. Les dossiers `ReleaseCameraFix` et `ReleaseCandidate`
  ainsi que les cibles supprimées du CMakeLists (`HDVisibility`,
  `HDVisibilityTest`) ont été retirés du build.
- Le nom de sortie passe de `HDFinalAdvancedV10` à `HDFinalAdvancedV11`
  (`OUTPUT_NAME` du CMakeLists et titre de la fenêtre `kWindowTitle`). Le
  README est réécrit pour l'état réel : touches physiques AZERTY K (carte) et
  M (Fullhands), profils véhicule Z/C/V/B/N, hooks réversibles (l'ancien
  README affirmait à tort « aucune injection, aucun hook »).
- Le dépôt Git est initialisé à la racine (`main`), avec `.gitignore` étendu :
  `/.tmp/` (sources originales du jeu, référence seule) et `/archive/`
  (binaires historiques) restent hors version. Un script `build.ps1` unique
  couvre configure + compilation Release/Debug.
- L'état attendu de validation V11 reste celui du journal précédent : la
  compilation Debug des corrections carte était verte, mais aucune mission
  n'a encore été testée dynamiquement avec la V11.

### Journal — retour de test V11 et corrections V12 (29 août 2026)

- Retour utilisateur : la vitesse véhicule et la téléportation carte ne
  fonctionnent pas en mission. Les touches demandées pour le véhicule sont
  **N** (accélérer) et **B** (réduire), sans maintien de Z, avec un minimum
  égal à la vitesse par défaut du jeu.
- Les trois signatures de hook (véhicule `0x4F1F3`/`0x4F22C`, ProcessCheat
  `0x9FC10`) ont été relues **dans le hde.exe installé** (Take2, Program
  Files (x86)) : les octets attendus correspondent exactement. Les trampolines
  peuvent donc s'installer ; l'échec venait de la logique d'entrée et des
  étages silencieux du clic de carte.
- Les sources officielles du jeu (Insanity3D, publiées sous Apache en 2020/2022,
  présentes localement dans `.tmp`) confirment : `GKEY_MAP` = Espace par défaut
  (InitSystem.cpp, contrôle 38), `ProcessCheat` est appelé **à chaque tick**
  de `C_game_mission::Tick`, et la carte accumule son propre
  `mouse_x/mouse_y` utilisé par `scene->UnmapScreenPoint`.
- **Vitesse véhicule V12** : le maintien de Z et les profils C/V/B/N sont
  supprimés. Le multiplicateur est persistant (1.0x à 80.0x) : chaque appui
  sur la touche physique N ajoute 10x, chaque appui sur B retire 10x, plancher
  1.0x = vitesse native du jeu. Le hook `Tick1` applique ce multiplicateur
  sans aucune condition de maintien.
- **Téléportation V12** : chaque étage du clic de carte renvoie désormais un
  statut distinct (carte non détectée, bloc curseur introuvable, rayon sans
  impact, sol non praticable) affiché dans l'interface, et un journal
  `hdradar_diag.log` (à côté de l'exécutable) trace toutes les étapes :
  bascule K, état natif de la carte, bloc curseur trouvé et son offset,
  valeurs du rayon, résultat du trampoline et de chaque écriture.
- Compilation **Debug et Release x86** V12 vérifiées sans erreur
  (`HDFinalAdvancedV12.exe`). La validation dynamique en mission reste à faire
  par l'utilisateur ; le journal de diagnostic rend le prochain retour décisif.

### Journal — crash de démarrage V12 et correction SRWLOCK (29 août 2026)

- Retour utilisateur : l'application ne s'ouvre plus. Six crashs `0xC0000005`
  relevés dans l'Observateur d'événements (ntdll.dll, offset `0x5fb03`), en
  Debug comme en Release, et sans création du journal de diagnostic.
- Bissection : sans l'appel `LogDiagnostic` de démarrage, l'application
  démarre ; un programme de test isolé reproduit le crash exactement sur
  `EnterCriticalSection` appliqué à un `CRITICAL_SECTION` zéro-initialisé —
  alors qu'un CS passé par `InitializeCriticalSection` et qu'un `SRWLOCK`
  zéro-initialisé fonctionnent. Le ntdll installé (`10.0.19041.6456`)
  durcit donc l'initialisation paresseuse des sections critiques.
- Correction : le verrou du journal utilise désormais un `SRWLOCK`
  (`AcquireSRWLockExclusive`/`ReleaseSRWLockExclusive`), sûr par contrat à
  l'état zéro. Debug et Release recompilés et **vérifiés au lancement** ; le
  journal `hdradar_diag.log` est bien créé à côté de l'exécutable.
- Les fichiers de test provisoires (scratch) et les artefacts `.obj` égarés à
  la racine ont été supprimés.

### Journal — second retour de test V12 et corrections ciblées (29 août 2026)

- Le journal de diagnostic du test utilisateur montre : carte native **ouverte**
  (Espace reconnu), bloc curseur trouvé (`manager+0x170`, souris native lue),
  mais échec `map scene geometry unavailable` : la scène de la carte de cette
  révision n'a **pas de BSP** à `scene+0x1C4`. Le rayon est désormais lancé sur
  le **BSP de la scène de mission**, qui contient la même géométrie monde (les
  modèles de carte recopient les positions de la mission sans conversion) ;
  les étages de lecture du BSP journalisent leurs échecs.
- Côté véhicule, le journal montre les multiplicateurs N/B fonctionnels mais
  **aucune installation de hook**. Le désassemblage du hde.exe installé
  confirme `speed` à `+0x244` et `transmission` à `+0x258`, mais **aucun accès
  à `+0x264`** : l'ancien `proposed_forward` (hérité des sources) n'existe pas
  dans ce binaire et faisait échouer la validation à chaque tick. La garde est
  supprimée ; la validation repose sur les deux champs confirmés et les
  échecs sont journalisés avec leurs valeurs.
- Debug et Release recompilés et vérifiés au lancement. Prochain test
  utilisateur attendu : véhicule (hook installé, accélération réelle) et
  clic de carte (rayon mission, sol, trampoline).

### Journal — ouverture de la phase V13 : téléportation carte et Fullhands M (29 août 2026)

- Retour utilisateur après le test complet de la V12 finale : la vitesse
  véhicule fonctionne enfin (validée), mais la téléportation carte et le
  Fullhands `All` avec M ne fonctionnent toujours pas. Ces deux fonctions
  deviennent le périmètre exclusif de la phase V13.
- Analyse du journal `hdradar_diag.log` (sessions cumulées) :
  - les sessions 1–2 proviennent de l'exécutable antérieur à la dernière
    correction : elles contiennent l'ancienne chaîne
    `map scene geometry unavailable`, absente du binaire final ;
  - pendant ces sessions, la carte s'est ouverte une fois (Espace reconnue),
    le bloc curseur a été trouvé (`manager+0x170`, souris interne
    `(836, 165)`) puis le clic a échoué sur l'ancien rayon BSP de la scène
    carte — l'échec exact que le commit `dbaa158` remplace par le rayon sur
    le BSP de la scène de mission ;
  - les sessions 3–6 ne contiennent que des chaînes du binaire final
    (`hooks installed`, `waiting for a controlled vehicle`) : les hooks
    véhicule s'installent, appliquent les multiplicateurs et se restaurent
    proprement (une seule validation de disposition échouée en transition
    de véhicule, récupérée immédiatement) ;
  - pendant la session finale (conduite véhicule), la carte ne s'est jamais
    ouverte : plus de trente pressions de K ont envoyé Espace sans erreur
    (`SendInput` acquitté) mais aucun état `native map opened` n'a été
    journalisé. Le nouveau chemin de rayon BSP mission n'a donc jamais été
    exercé en jeu ;
  - aucune ligne concernant l'inventaire dans aucune session : le chemin
    Fullhands n'émet aucun diagnostic et l'appui M lui-même n'est pas
    journalisé.
- Constats de code confirmés dans les sources :
  - `main.cpp` ne remplit jamais `native_map_client_x/y` : le clic arrive
    toujours à `(0, 0)` dans `ResolveNativeMapClick`. Ces deux flottants y
    sont ensuite écrasés par référence par `ReadNativeMapCursor` avec la
    souris interne du bloc curseur, puis servent au calcul NDC : le rayon
    utilise donc bien les coordonnées internes du jeu, mais ce flux
    accidentel doit être rendu explicite (sorties nommées) ;
  - `SendNativeMapKey` n'active pas la fenêtre du jeu avant d'envoyer
    Espace, contrairement au déclencheur d'inventaire ;
  - hypothèse principale pour la carte jamais ouverte en session véhicule :
    Espace est consommée par le contexte de conduite (frein à main) et
    n'atteint pas la bascule carte ; le test V13 devra ouvrir la carte à
    pied ;
  - `GrantCompleteDeluxeInventory` n'appelle jamais `LogDiagnostic` : ses
    étages (signature `0x9FC10`, configuration `0x1086E0`, tables
    `0x10AAD0`, requête de table, slot joueur `+0x04`, allocation,
    écriture du hook, déclencheur, complétion, restauration, libération)
    échouent tous en silence. Impossible de savoir quel étage a refusé.
- Aucun code n'a été modifié avant cette retranscription ; la section 14
  définit les incréments de correction.

## 14. Plan de correction V13 — téléportation carte et Fullhands M

### 14.1 Périmètre et règle

- Le chemin véhicule validé est gelé : aucune modification du hook `Tick1`
  ni des touches N/B.
- Chaque incrément est inscrit dans le journal immédiatement après son
  application, avant de passer au suivant.
- Toute nouvelle étape d'échec doit être nommée dans `hdradar_diag.log` :
  le prochain test doit produire l'étage fautif, pas un échec muet.

### 14.2 Incrément 1 — instrumentation complète du Fullhands M

- Journaliser l'appui M dans `main.cpp` (`Fullhands: M pressed`, bouton ou
  touche) afin de distinguer « touche jamais détectée » des étages suivants.
- Dans `GrantCompleteDeluxeInventory`, ajouter un `LogDiagnostic` par étage
  avec les valeurs lues : connexion/snapshot/acteur, module, signature
  attendue et lue à `0x9FC10`, configuration et index de table, pointeur de
  table et vtable, requête de table dans le module, slot joueur `+0x04`
  dans le module, allocation distante, écritures de la complétion et du
  code, écriture protégée du hook (avec sa restauration de repli),
  déclencheur (activation fenêtre, `SendInput`), complétion lue et nombre
  de boucles, octets restaurés, attente d'inactivité des threads,
  libération, compte accordé.
- Question ouverte tranchée par l'instrumentation : le déclencheur envoie
  la touche produisant X dans la disposition du thread du jeu. Si
  `ProcessCheat` est réellement appelé à chaque tick de
  `C_game_mission::Tick`, le déclencheur est accessoire et la complétion
  doit s'écrire en un tick ; si la complétion reste à zéro après dix
  secondes, le hook n'est pas exécuté et la RVA devra être revalidée sur la
  session.

### 14.3 Incrément 2 — téléportation carte : coordonnées, Espace et rayon

- Rendre explicite le flux des coordonnées : `ReadNativeMapCursor` doit
  retourner la souris interne par des sorties nommées sans écraser les
  arguments d'entrée ; journaliser la souris interne retenue pour le NDC et
  ne plus laisser le clic Win32 `(0, 0)` apparaître comme la position
  utilisée.
- Journaliser dans `SendNativeMapKey` le résultat de l'envoi et la fenêtre
  active ; après le premier test instrumenté, si la carte ne s'ouvre
  toujours pas à pied, activer la fenêtre du jeu avant l'envoi (miroir de
  `SendInventoryMainThreadTrigger`).
- Le rayon sur le BSP de la scène de mission (`dbaa158`) est conservé tel
  quel : il n'a pas encore été exercé en jeu et ses étages sont déjà
  journalisés (`Geometry: BSP unavailable`, `ray missed`,
  `ground selected`).

### 14.4 Incrément 3 — compilation et contrôles

- Reconstruire Debug et Release x86 sous le nom `HDFinalAdvancedV13.exe`
  (`OUTPUT_NAME` du CMakeLists et titre de la fenêtre), vérifier PE32/x86,
  l'absence de primitives d'injection externes et la présence des nouvelles
  chaînes de diagnostic dans le binaire.
- Mettre à jour le README (binaire à tester, chaînes de diagnostic) une
  fois le binaire construit.

### 14.5 Test V13 demandé

- À pied : K ouvre la carte, un clic sur la carte doit journaliser
  `ground selected` puis le succès du trampoline `SetPos`/`Update`, et le
  joueur doit apparaître sur le sol choisi ; en cas d'échec, chaque statut
  affiché doit correspondre à une ligne nommée du journal.
- En mission : M doit donner l'inventaire complet ; le journal montre
  l'appui M, chaque étage franchi et le compte accordé ; sinon, l'étage
  d'échec est nommé.
- Véhicule : N/B inchangés (référence validée) ; fermeture du trainer sans
  crash et restauration complète des valeurs d'origine.

### Journal — incrément V13 n° 1 : instrumentation Fullhands M appliquée (29 août 2026)

- `main.cpp` journalise désormais chaque appui M (`Fullhands: M pressed`,
  bouton ou touche, avec `reserved_active`). La capture du clic Win32
  remplit aussi `native_map_client_x/y` (`GetCursorPos` + `ScreenToClient`
  sur l'edge du clic) pour que le diagnostic de la carte ne soit plus
  toujours `(0, 0)`.
- `SendInventoryMainThreadTrigger` journalise chacun de ses étages :
  fenêtre indisponible, activation refusée, scan code manquant, échec
  `SendInput`.
- `GrantCompleteDeluxeInventory` émet un `LogDiagnostic` à chaque étage :
  disponibilité (connexion, snapshot, joueur), module, bornes RVA,
  configuration et index de table, pointeur de table, vtable, requête de
  table dans le module, vtable joueur, slot `+0x04` dans le module,
  signature `ProcessCheat` (octets attendus et lus, puis vérifiée),
  allocation distante, écriture du code distant, échec d'écriture du hook
  avec rollback (page libérée ou conservée), déclencheur, complétion et
  nombre de polls, restauration vérifiée, refus final
  (`completed/released/idle`) et compte accordé.
- Aucune modification fonctionnelle du chemin d'octroi : seuls des appels
  de journal ont été ajoutés.

### Journal — incrément V13 n° 2 : coordonnées carte et envoi Espace appliqués (29 août 2026)

- `ResolveNativeMapClick` lit la souris interne du bloc curseur dans des
  sorties nommées `native_cursor_x/y` et construit le NDC avec ces
  valeurs ; les arguments Win32 ne sont plus écrasés par référence et ne
  servent plus qu'au diagnostic (`win32=(…) internal=(…)`).
- `SendNativeMapKey` prend maintenant le `TrainerProcess` et active la
  fenêtre du jeu avant l'envoi d'Espace (miroir du déclencheur
  d'inventaire), puis journalise chaque échec : fenêtre indisponible,
  activation refusée, scan code manquant, `SendInput` refusé. Les deux
  points d'appel transmettent `game_process`.
- `gameplay_mods.h` documente le rôle purement diagnostique des
  coordonnées Win32 du clic.

### Journal — incrément V13 n° 3 : compilation et contrôles V13 (29 août 2026)

- `CMakeLists.txt` produit `HDFinalAdvancedV13` ; le titre de la fenêtre et
  la ligne de démarrage du journal passent à V13 ; le commentaire de
  `build.ps1` est aligné.
- Release x86 compilée sans erreur : PE32/x86 (`Machine 0x014C`, magie
  `0x010B`, sous-système GUI), taille `460288` octets, SHA-256
  `465C69F9A2DBDC9AF5ADF751BDD1243FCFE65379D8B0C37818B421BD1DE8BC5E`.
- Chaînes contrôlées dans le binaire : titre V13 présent ; chaînes de
  diagnostic Fullhands et téléportation présentes ; `HD Final Advanced
  V12`, `Session start (… V12)` et l'ancienne chaîne
  `map scene geometry` absentes.
- Debug x86 compilée sans erreur ; l'exécutable Debug V12 et son journal
  ont été archivés dans `archive\bin-debug` (journal renommé
  `hdradar_diag_V12.log`).
- L'exécutable Release V12 n'a pas été déplacé : il est encore en cours
  d'exécution (PID `11712`) avec `hde.exe` (PID `16208`). Il sera archivé
  après la prochaine session ; le lancement de V13 ferme automatiquement
  l'instance précédente (recherche par nom de classe).
- Le V13 n'a volontairement pas été lancé pendant cette étape pour ne pas
  perturber la session de jeu en cours ; la vérification au lancement fait
  partie du test utilisateur.
- README mis à jour : binaire V13, journal de diagnostic incluant le
  Fullhands, test carte à pied (Espace consommée par la conduite en
  véhicule), vitesse véhicule notée comme validée.

### Journal — retour de test V13 et corrections V13.1 (29 août 2026)

- Retour utilisateur : le Fullhands `All` (M) et la téléportation carte
  échouent toujours ; en mission, la carte n'apparaît jamais.
- Analyse du journal de la session V13 :
  - Fullhands : l'appui M est bien détecté (`reserved_active=1`),
    configuration `2`, index de table `0`, puis échec systématique
    `inventory table query outside module (query=006E3030 module=[00400000,
    0051B000])`. Le désassemblage du sélecteur natif `0049FE8A` montre que
    le jeu appelle lui-même `[table_vtable+0x30]` sans aucun contrôle de
    plage ; la contrainte « dans le module principal » du trainer était
    donc fausse : `0x006E3030` vit légitimement hors de `hde.exe` (DLL du
    jeu) ;
  - téléportation : K est détecté, Espace est envoyé sans erreur, mais
    `native map opened` n'apparaît jamais. Les sources et le registre
    tranchent : aucune clé `Controls` n'existe (les défauts s'appliquent),
    donc GKEY_MAP (slot 38) est bien `K_SPACE` ; la touche envoyée est la
    bonne. La cause est la synchronisation : le jeu échantillonne
    `keyboard_map[]` (rempli par son hook `WH_KEYBOARD`) une fois par tick,
    et le couple down+up envoyé dos-à-dos pouvait tomber entièrement entre
    deux échantillons — d'où une ouverture fortuite sur ~9 essais dans
    l'ancienne session et aucune dans la dernière.
- Correctifs V13.1 appliqués :
  - `GrantCompleteDeluxeInventory` : les deux cibles d'appel (requête de
    table `+0x30`, cbProc joueur `+0x04`) ne sont plus contraintes au
    module principal ; nouvelle méthode
    `TrainerProcess::IsReadableCodeTarget` (pointeur sain et 8 premiers
    octets lisibles) — miroir du contrat natif du sélecteur ;
  - `SendNativeMapKey` : Espace est maintenant maintenu environ 150 ms
    (down, pause, up) pour garantir qu'au moins un tick du jeu observe
    l'état enfoncé, à l'ouverture comme à la fermeture de la carte.
- Le trainer V13 en cours d'exécution (PID `18916`) verrouillait la sortie
  de compilation ; il a été fermé proprement avant la recompilation.
- Release et Debug recompilées sans erreur : PE32/x86 (`0x014C`/`0x010B`),
  taille `460288` octets, SHA-256
  `EE8D9239DA7C1FB77561BA5D59BDCD853686FACB7838B0495CD921B7024AC288` ;
  chaînes des deux nouveaux diagnostics présentes.
- Prochain test : K en mission à pied doit ouvrir la carte, un clic doit
  journaliser le rayon mission et téléporter ; M doit donner l'inventaire
  complet ; sinon le journal nomme l'étage fautif.

### Journal — retour de test V13.1 : carte ouverte validée, deux crashs précis (29 août 2026)

- Retour utilisateur : un appui sur M fait planter le jeu (disparition de
  la fenêtre) ; après redémarrage, K ouvre la carte parfaitement, mais le
  choix d'un endroit fait planter le jeu de nouveau.
- Preuves du journal `hdradar_diag.log` (session V13.1) :
  - M n° 1 : signature vérifiée, trampoline installé, complétion `111`
    écrite dès le premier poll, hook restauré, `grant complete, 111 rows
    confirmed` — puis le processus meurt ;
  - M n° 2 (après redémarrage du jeu) : séquence identique, nouvel octroi
    complet `111`, nouveau crash ;
  - K : `native map opened` — **le maintien d'Espace ~150 ms est validé en
    jeu, la carte s'ouvre** ;
  - clic : position Win32 réelle `(960, 540)`, bloc curseur trouvé, souris
    interne `(904, 229)`, rayon et sol résolus (`ground selected (-5.1,
    7.1, 66.4)`) — toute la chaîne de lecture fonctionne ;
  - pose : `Teleport: trampoline did not complete (triggered=1 completed=0
    restored=0 idle=0)` — la complétion n'a jamais été écrite et la
    restauration du hook a échoué, le processus étant déjà mort.
- Preuves de l'Observateur d'événements (Application, `hde.exe`) :
  - crash 1 (PID `0x3F50`, 15:12:10) et crash 2 (PID `0x288`, 15:12:30) :
    exception `0xC0000409` (dépassement de pile détecté — échec de cookie
    de sécurité `/GS`) — ces deux instants correspondent exactement aux
    deux octrois Fullhands complets ;
  - crash 3 (PID `0x884`, 15:12:46) : exception `0xC0000005` dans
    **`i3d2.dll`, décalage `+0x2A49`** — cet instant correspond à la
    fenêtre d'exécution du trampoline de pose (SetPos/Update).
- Constat technique majeur sur le trampoline de pose : la déclaration
  publique du moteur (`Insanity3D/Include/I3D/I3D2.h`, classe
  `I3D_frame`) ne contient **aucune méthode `Update`** (la ligne est
  commentée). Dans cette disposition, `vtable + 0x0C` est bien `SetPos`,
  mais `vtable + 0x50` est `GetData` — le second appel du trampoline
  appelle donc une méthode qui n'est pas celle attendue et qui a planté
  dans `i3d2.dll`. De plus, le cheat `debugport` des sources
  (`Actors.cpp`, `GameMission.cpp`) ne fait ni `SetPos` ni `Update` : le
  trampoline V11 n'avait pas de chemin natif de référence.
- Constat technique majeur sur le Fullhands : l'octroi en rafale (111
  appels `cbProc` en un seul tick, pendant `ProcessCheat`) s'exécute
  jusqu'au bout, mais le jeu meurt ensuite d'un dépassement de pile
  détecté par cookie. Le sélecteur natif n'accorde qu'un objet à la fois
  dans son flux d'interface ; la convention d'appel exacte de
  `C_player::cbProc` (nombre d'arguments, nettoyage de pile) reste à
  revérifier sur le site d'appel natif du désassemblage.
- Aucune implémentation n'est entreprise dans cette étape : la section 15
  enregistre l'analyse et le plan de correction V13.2.

## 15. Plan de correction V13.2 — éliminer les deux crashs

### 15.1 État acquis après V13.1

- Validés en jeu : ouverture de la carte par K (maintien d'Espace),
  capture du clic (Win32 et souris interne), bloc curseur, rayon sur le
  BSP de mission, sélection du sol praticable, vitesse véhicule (référence
  inchangée), chemin de lecture complet de l'inventaire (M détecté,
  configuration, tables, signature, octroi exécuté).
- En échec : l'octroi Fullhands fait mourir le jeu après complétion ;
  la pose de la destination fait mourir le jeu pendant le trampoline.

### 15.2 Crash Fullhands — `0xC0000409` après octroi

- Faits : le compteur du trampoline atteint 111, le hook est restauré, puis
  le processus meurt de manière reproductible deux fois. Ce compteur ne
  confirmait pas l'ajout des objets : il comptait les retours de l'appel
  virtuel, quel que soit le slot appelé.
- Les dumps `hde.exe.16208.dmp` et `hde.exe.648.dmp` portent
  `0xC0000409` avec le paramètre rapide `0x15` (21), soit
  `FAST_FAIL_INVALID_EXCEPTION_CHAIN`. Un échec de cookie de pile aurait
  le sous-code `2`. Les seconds dumps montrent ensuite une exécution à
  l'adresse nulle pendant la gestion récursive de la première faute.
- Cause exacte : `C_player` hérite de `C_unknown`, dont la vtable commence
  par destructeur `+0x00`, `AddRef +0x04`, `Release +0x08`. Le premier
  virtual de `C_actor`, `cbProc`, est donc `+0x0C`. Le V13.1 appelait
  `+0x04`, donc `AddRef`, après avoir poussé les trois arguments de
  `cbProc`. `AddRef` ne les dépile pas : 111 appels laissaient exactement
  1332 octets parasites avant `popad/popfd/ret`, détruisant la pile de
  contrôle et la chaîne SEH.
- Correction V13.2 : résolution et appel du slot joueur `+0x0C`, avec les
  trois arguments natifs et le `ret 0x0C` correspondant. Le filtre de
  catalogue, le hook temporaire, l'acquittement et les gardes de libération
  restent inchangés.

### 15.3 Crash téléportation — `i3d2.dll + 0x2A49` pendant la pose

- Faits : contexte trampoline valide, hook `ProcessCheat` installé,
  déclencheur envoyé, complétion jamais écrite, puis lecture invalide dans
  `i3d2.dll + 0x2A49`.
- Le désassemblage de l'`i3d2.dll` installé place à cet RVA l'instruction
  `mov esi,[edx]` de `SetPos`; le dump donne `EDX=0x000000E4` et une
  exception de lecture de `0xE4`. Le crash survient donc dans le premier
  appel, avant l'ancien second appel `+0x50`.
- Cause exacte : le header Deluxe installé déclare `I3DAPI __stdcall`.
  Les appels natifs poussent `&destination`, puis le pointeur `frame` ; le
  V13.1 ne poussait que `&destination` et mettait `frame` dans `ECX` comme
  pour un `thiscall`. `SetPos` recevait ainsi la destination comme objet et
  `0xE4` comme pointeur de vecteur.
- Erreur latente distincte : `vtable + 0x50` est `GetData`, pas `Update`.
  Cet appel n'a pas causé le dump présent parce que `SetPos` a planté avant,
  mais il aurait été invalide au retour.
- Correction V13.2 : pousser explicitement `&destination`, puis `frame`,
  appeler uniquement `vtable + 0x0C` et supprimer totalement `+0x50`.
  La cible de code `SetPos` doit aussi être lisible avant installation du
  trampoline.

### 15.4 Règles et prochain test

- Ne pas régresser : ouverture de la carte par K, chaîne de clic, vitesse
  véhicule, invisibilité, ESP, armes.
- Chaque correctif est inscrit dans ce journal immédiatement après son
  application ; chaque étage de la pose et de l'octroi continue d'être
  journalisé dans `hdradar_diag.log`.
- Test utilisateur final attendu : M en mission → inventaire complet sans
  crash ; K → carte → clic → téléportation sur le sol sans crash ;
  fermeture du trainer propre et sans crash du jeu.

### 15.5 Implémentation corrective V13.2

- `GrantCompleteDeluxeInventory` lit et valide maintenant
  `player_vtable + 0x0C`; le code distant appelle le même slot. Les
  commentaires et diagnostics ne désignent plus `+0x04` comme `cbProc`.
- `SetFramePositionOnMainThread` ne résout plus aucun faux slot `Update`.
  Son stub pousse `&destination`, puis `frame`, et appelle `SetPos +0x0C`
  selon l'ABI `I3DAPI __stdcall` confirmée par le header et le binaire
  installés. Le journal annonce désormais `stdcall SetPos`.
- Une pose en attente n'est plus exécutée au tick suivant sans contrôle :
  `map_active` doit être lisible et nul pendant trois mises à jour
  consécutives. L'attente est bornée à 3000 ms ; au-delà, la pose est
  annulée sans appel moteur. Un échec d'envoi de la touche de fermeture
  annule aussi immédiatement la pose pendante.
- Un nouvel état UI `WaitingForMapClose` indique cette transition. L'état
  interne est remis à zéro après succès, annulation, changement de PID ou
  restauration du trainer.
- `CMakeLists.txt` produit un fichier distinct
  `HDFinalAdvancedV13_2.exe`; le titre de fenêtre et le début du journal
  utilisent `HD Final Advanced V13.2`. `build.ps1` et `README.md` pointent
  vers ce nouveau jalon sans écraser le V13 testé.
- Le README documente la confirmation de fermeture sur trois mises à jour
  et le slot natif `C_actor::cbProc` corrigé.
- Compilation **Release et Debug Win32/x86** réussie sans erreur avec Visual
  Studio 2026. Les exécutables produits sont :
  - Release : `build\vs2026-x86\Release\HDFinalAdvancedV13_2.exe`,
    `460800` octets, SHA-256
    `96E7ABC9D4227E7694BE146DEB66D7E9D610CD3871FCFC0FC5E861BEE333BD92` ;
  - Debug : `build\vs2026-x86\Debug\HDFinalAdvancedV13_2.exe`,
    `1719296` octets, SHA-256
    `A621E9402C566DD041B35E532AD12418EBCB606245237E42F0CFBFBD3D8FD75F`.
- `dumpbin /headers` confirme pour la Release : machine `0x14C` x86,
  format `PE32` (`magic 0x10B`) et sous-système Windows GUI.
- Les assertions statiques confirment les séquences générées : téléportation
  `push &destination`, `push frame`, appel `vtable + 0x0C` sans aucun appel
  `+0x50`; Fullhands résout et appelle `player_vtable + 0x0C`, sans ancien
  chemin `+0x04`; la fermeture de carte exige trois relevés fermés.
- `git diff --check` ne relève aucune erreur de diff (uniquement les
  avertissements de normalisation LF vers CRLF du dépôt). Le binaire Release
  contient les diagnostics V13.2 attendus (`stdcall SetPos`, slot `+0C`,
  attente et confirmation de fermeture de carte).
- Limite de validation : les deux causes de crash sont corrigées et compilées,
  mais le test dynamique reste à effectuer dans une nouvelle session du jeu :
  M une fois, puis K, clic sur la carte et retour au jeu.

## 16. Retour de test V13.2 et correction V13.3

### 16.1 Résultat fonctionnel communiqué le 29 août 2026

- La téléportation par carte ne fait plus crasher le jeu et la pose est
  exécutée, ce qui valide la correction d'ABI de `I3D_frame::SetPos`.
- La destination obtenue ne correspond toutefois pas au point visuel cliqué
  sur la carte. Le chemin clic Win32 → coordonnées internes → rayon carte →
  sol mission doit être recalibré à partir du dernier journal V13.2.
- La touche M ne fait plus disparaître le jeu, mais aucun objet n'est reçu.
  Le compteur de lignes parcourues ne doit plus être assimilé à une preuve
  d'ajout réel. L'objectif est l'inventaire réellement complet : armes,
  munitions, grenades, lance-roquettes et toutes les entrées utilisables du
  catalogue de la révision installée.
- Aucune garantie fonctionnelle finale ne sera inscrite sur la seule base
  d'une compilation. La V13.3 devra fournir une preuve d'appel natif et un
  résultat observable, puis être confirmée par un test en mission.

### 16.2 Ordre de travail V13.3

1. Lire le dernier `hdradar_diag.log` et quantifier chaque étape de la
   conversion de carte afin d'identifier précisément l'origine du décalage.
2. Désassembler le chemin natif Fullhands et vérifier le sens exact de
   `C_actor::cbProc`, des paramètres et du catalogue ; ne compter comme
   succès qu'un changement d'inventaire mesurable.
3. Corriger les deux chemins, ajouter les diagnostics nécessaires et produire
   un exécutable V13.3 distinct.
4. Compiler Debug/Release x86, contrôler le PE et définir un test dynamique
   court qui confirme la destination et l'inventaire réel.

### 16.3 Causes exactes confirmées avant implémentation

- Fullhands V13.2 appelle bien une fonction et conserve une pile équilibrée,
  mais le slot est encore faux. Le site natif installé `004A00F5–004A0104`
  pousse une ligne de catalogue (`prm2`), `9`, `14`, puis appelle
  `[player_vtable + 0x30]` avec le joueur dans `ECX`.
  La V13.2 appelait `+0x0C`; cette autre méthode retourne sans ajouter
  d'objet. Les 111 unités du journal étaient uniquement les lignes de table
  filtrées par le trampoline, pas des objets confirmés.
- Le code source Deluxe de `C_human::cbProc(CB_CHEAT, 9)` contient la liste
  native complète du cheat « all ammo » : 28 identifiants utilisables avec
  leurs quantités, dont armes, pistolets, grenades, mines, couteau,
  lance-roquettes et munitions. Un seul appel `cbProc +0x30` est requis.
- La V13.3 devra relire le vecteur d'inventaire `actor +0x5C/+0x60` et
  vérifier réellement chacun des 28 identifiants et sa quantité avant
  d'afficher un succès. Le compteur d'exécution du stub ne sera plus utilisé
  comme preuve fonctionnelle.
- Pour la carte, le curseur natif à `manager +0x170` est cohérent, tandis que
  le clic Win32 reste capturé au centre. L'écart provient du rayon reconstruit
  manuellement depuis `scene +0x8C`. Le jeu appelle directement
  `map_scene->UnmapScreenPoint(mouse_x, mouse_y, from, dir)` au slot I3D
  `+0x34`, avec cinq paramètres `__stdcall`.
- Les objets vivants de la session confirment `scene_vtable +0x34` comme
  cible lisible dans `i3d2.dll`. Le tick de carte installé est à
  `hde.exe +0xB1AD0` et commence par la signature exacte
  `A1 E0 AA 50 00`. La correction utilisera temporairement ce tick pour
  exécuter l'unmapping natif sur le thread du jeu, puis restaurera les cinq
  octets avant toute fermeture de la carte.

### 16.4 Implémentation V13.3

- `radar.cpp` contient maintenant un trampoline temporaire et borné sur
  `C_map_manager_imp::Tick`. Il vérifie la signature des cinq octets et la
  cible `map_scene` vtable `+0x34`, appelle le vrai
  `I3D_scene::UnmapScreenPoint(x, y, &origin, &direction)` sur le thread du
  jeu, restaure le prologue puis attend qu'aucun thread ne se trouve dans la
  page distante avant de la libérer.
- L'appel fournit un diagnostic complet : coordonnées internes, résultat
  I3D, origine/direction obtenues, restauration et inactivité de la page.
  `ResolveNativeMapClick` utilise désormais exclusivement ce rayon natif ;
  l'inversion manuelle de `scene +0x8C`, source du décalage, est supprimée.
- `GrantCompleteDeluxeInventory` n'énumère plus 1024 lignes de catalogue et
  ne confond plus un compteur de boucle avec des objets reçus. Le trampoline
  reproduit l'ABI installé : `ECX=player`, `push prm2`, `push 9`, `push 14`,
  puis `call [player_vtable + 0x30]`, une seule fois sur le thread du jeu.
  Comme la branche 9 n'utilise jamais `prm2`, la V13.3 lui passe zéro.
- Avant et après cet appel, la V13.3 relit le vecteur `player +0x5C/+0x60`.
  Elle ne déclare le succès que si les 28 identifiants de la liste officielle
  sont présents et si `S_item::amount` à `+0x18` atteint chaque quantité
  prévue. Chaque objet absent ou insuffisant est inscrit dans le journal.
- Le jalon produit désormais un exécutable distinct
  `HDFinalAdvancedV13_3.exe`; le titre, le début du journal, `build.ps1` et
  `README.md` ont été alignés sur V13.3 sans écraser la V13.2 testée.

### 16.5 Validation de livraison V13.3

- Les compilations **Release et Debug Win32/x86** réussissent sans erreur
  avec Visual Studio 2026. CTest s'exécute correctement mais le dépôt ne
  définit actuellement aucun test automatisé.
- Exécutables produits :
  - Release : `build\vs2026-x86\Release\HDFinalAdvancedV13_3.exe`,
    `461824` octets, SHA-256
    `3068E2BE0B15487A6F97FB7897D98285259FF7665CC4FCFD086DB66B3FEC17AF` ;
  - Debug : `build\vs2026-x86\Debug\HDFinalAdvancedV13_3.exe`,
    `1723392` octets, SHA-256
    `F388370280BA26E0FCBAC6B49465086E9E364D846E8EB1C788EF5BF3789DB81C`.
- `dumpbin /headers` confirme pour la Release : machine `0x14C` x86,
  format `PE32` (`magic 0x10B`) et sous-système Windows GUI.
- Les assertions statiques confirment : un seul appel Fullhands au slot
  `+0x30`, aucun appel `+0x0C` dans cette fonction, validation des 28 objets,
  appel carte au slot `+0x34`, absence de l'ancienne inversion matricielle et
  restauration du tick. `git diff --check` ne signale aucune erreur de diff
  (seulement les avertissements LF/CRLF du dépôt).
- La session existante est restée intacte pendant la livraison : `hde.exe`
  PID `10576` et trainer V13.2 PID `11700` n'ont pas été arrêtés. La V13.3
  n'a donc pas été lancée automatiquement. La confirmation fonctionnelle
  finale exige un essai en mission de M puis K/clic avec ce nouvel exécutable.

> **IMPORTANT — état dépassé :** la validation 16.5 correspond au premier
> prototype V13.3 limité à la liste Deluxe de 28 objets. L'audit vivant plus
> profond décrit en 16.6 a démontré que son receiver Fullhands était faux et
> que cette liste ne couvre pas les ajouts d'Ultimate Mod. Les empreintes de
> 16.5 ne sont donc **pas** celles de la future livraison finale. Ne pas livrer
> ni lancer ce prototype comme version corrigée définitive.

### 16.6 Handoff immédiat — audit vivant Fullhands Ultimate Mod (29 août 2026)

#### État utilisateur et contraintes de session

- Test utilisateur V13.2 : la carte s'ouvre et téléporte sans crash, mais le
  point d'arrivée ne correspond pas au point cliqué. M ne crashe plus, mais
  aucun objet n'est reçu.
- La session de jeu est toujours ouverte : `hde.exe` PID `10576`. Le trainer
  V13.2 est toujours ouvert : PID `11700`. Aucun des deux n'a été arrêté ni
  modifié pendant l'audit et les compilations.
- Ne pas lancer automatiquement V13.3 : son mécanisme ferme l'ancien trainer
  et interromprait l'état de test actuel. Demander/laisser l'utilisateur
  lancer la livraison finale lorsqu'elle sera reconstruite.
- Une garantie « 100 % » ne peut pas être annoncée honnêtement avant un test
  dynamique en mission. La compilation et le désassemblage prouvent l'ABI,
  pas le résultat visuel/fonctionnel complet.

#### Téléportation — cause et correction déjà implémentée

- Le curseur Win32 reste généralement au centre `(960,540)` à cause de la
  capture relative, mais le curseur natif lu dans le bloc
  `C_map_manager_imp +0x170` varie correctement. Le décalage V13.2 venait de
  l'inversion manuelle de la matrice `map_scene +0x8C`, qui ne reproduit pas
  exactement les règles d'Insanity3D (viewport, caméra orthogonale,
  normalisation et aspect ratio).
- Le code source officiel `I3D_scene::UnmapScreenPoint` confirme qu'il utilise
  le viewport interne, met à jour les matrices caméra, traite séparément la
  caméra orthogonale et normalise la direction. Son retour `I3D_OK` vaut zéro.
- La vtable vivante de `map_scene` confirme `UnmapScreenPoint` au slot `+0x34`
  dans `i3d2.dll`. L'ABI installée est `__stdcall` avec cinq valeurs empilées,
  dans cet ordre de construction : `&dir`, `&origin`, `y`, `x`, `scene`.
- `src/radar.cpp` contient maintenant
  `UnmapNativeMapPointOnMainThread`. Il pose temporairement un JMP sur
  `C_map_manager_imp::Tick`, RVA `hde.exe +0xB1AD0`, après vérification des
  cinq octets exacts `A1 E0 AA 50 00`. Au tick suivant, le stub appelle
  `[scene_vtable +0x34]`, publie résultat/origine/direction, restaure le
  prologue, attend qu'aucun thread ne soit dans la page distante, puis exige
  aussi la libération réussie de cette page.
- `ResolveNativeMapClick` n'utilise plus du tout l'ancienne inversion
  `Matrix4x4 inverse`. Il arrondit les coordonnées du curseur natif, appelle
  le helper ci-dessus, puis conserve le raycast BSP mission et la sélection
  du sol praticable. La pose `SetPos` reste différée jusqu'à trois relevés
  consécutifs confirmant la fermeture de la carte (correction V13.2).
- Cette partie a compilé en Release et Debug x86 avant le dernier changement
  Fullhands. Elle doit être recompilée avec la correction Fullhands finale,
  puis testée par K → clic sur plusieurs points éloignés et comparables.

#### Fullhands — découverte exacte qui remplace les hypothèses de 16.3/16.4

- **Les affirmations précédentes disant que `player_vtable +0x30` est le
  callback Fullhands sont fausses et remplacées par cette section.** Un probe
  vivant en lecture seule donne :
  - joueur local `0x0252E750`, vtable `0x004F6784` ;
  - `player_vtable +0x30 -> 0x00429D90` ; cette fonction n'est pas
    `C_actor::cbProc` et un appel direct `(14,9,...)` ne suit pas le chemin
    natif attendu ;
  - mission `0x024D64C0`, vtable `0x004FC520` ;
  - `mission_vtable +0x30 -> 0x004A57D0`, fonction confirmée comme
    `C_game_mission::BroadcastMessage`.
- Désassemblage exact du choix Fullhands installé :
  - `ProcessCheat` commence à `0x0049FC10`, signature
    `B8 5C 10 00 00` ; son `this` est sauvegardé dans `EDI` puis
    `[esp+0x48]` ;
  - `0x0049FE8A–0x004A009F` parcourt les 1024 lignes de la table d'inventaire
    et conserve les lignes dont la catégorie (champ natif 3) n'est ni `0`
    ni `4` ; dans la session V13.2 ce filtre produit **111 lignes** ;
  - après le dialogue, `0x004A00F5` recharge `ECX=[esp+0x48]`, donc l'objet
    mission/ProcessCheat et **pas le joueur** ;
  - `0x004A00F9` charge la ligne choisie depuis le tableau local ;
  - il pousse `row`, `9`, `14`, puis `0x004A0104` appelle
    `[mission_vtable +0x30]`.
- `C_game_mission::BroadcastMessage` installé à `0x004A57D0` parcourt le
  vecteur d'acteurs mission `+0x68/+0x6C`. Pour chaque acteur, il empile
  quatre arguments (`0`, `prm2`, `prm1`, `msg`) et appelle
  `[actor_vtable +0x04]`, qui retourne par `ret 0x10`.
- Cette couche explique tous les résultats antérieurs :
  - V13.1 appelait directement `player +0x04` avec seulement trois arguments,
    alors que ce callback acteur en retire quatre : déséquilibre de pile et
    crash `0xC0000409` ;
  - V13.2 appelait `player +0x0C`, une autre méthode : pile équilibrée mais
    aucun objet ;
  - le premier prototype V13.3 appelait `player +0x30`, également mauvais ;
    sa vérification des 28 objets Deluxe ne devait pas être livrée.
- Le code source Deluxe original contient bien un cheat `allammo` de 28 IDs,
  mais Ultimate Mod charge un catalogue beaucoup plus large. L'exemple
  utilisateur « M416 » indique précisément qu'il faut suivre les lignes
  utilisables de la table installée, pas figer la liste originale de 28 IDs.
- Le chemin correct à terminer est donc : reproduire dans le stub le filtre
  natif des 1024 lignes, puis appeler pour chaque ligne
  `mission->BroadcastMessage(14, 9, row)` au slot mission `+0x30`. Ce chemin
  ajoute lui-même le quatrième argument acteur et évite le déséquilibre ABI.
- Pour une preuve réelle, le stub doit aussi copier chaque ligne retenue dans
  une zone distante. Après retour et restauration du hook, le trainer relit
  cette liste dynamique et vérifie que chaque ID se trouve réellement dans
  le vecteur d'inventaire du joueur `+0x5C/+0x60`.
- `C_inventory::S_item` est confirmé : ID `+0x08`, quantité/réserve `+0x18`,
  balles dans le chargeur `+0x1C`, taille `0x20`. Pour fournir des réserves
  utiles, le plan actuel est de porter `amount` à au moins `100` pour chaque
  objet créé, puis de relire et vérifier cette valeur. Ne pas écrire une
  valeur arbitraire dans `bullets_in_stack` sans connaître la taille de
  chargeur propre à chaque arme.
- Un inventaire complet Ultimate Mod peut dépasser 64 objets. La limite de
  validation Fullhands doit être `256`. Vérifier aussi
  `src/weapon_mods.cpp`, qui conserve actuellement
  `kMaximumInventoryItems = 64` : elle devra passer à `256` pour que les mods
  d'armes continuent à résoudre l'arme équipée après Fullhands.

#### État exact du code au moment de ce handoff

- `src/radar.cpp` : correction native `UnmapScreenPoint` complète et compilée.
- Version/documentation : CMake, titre, journal, `build.ps1` et README pointent
  déjà vers `HDFinalAdvancedV13_3`.
- `src/gameplay_mods.cpp` est **à mi-migration Fullhands et ne doit pas être
  considéré compilable/livrable dans cet état précis** :
  - les constantes ont déjà été changées vers `kRemoteSize=0x2000`,
    `kCatalogRowsOffset=0x400`, `kCompletionOffset=0x1800`, table/configuration,
    capacité catalogue `0x400`, inventaire max `256` et quantité cible `100` ;
  - `ExpectedItem[28]` a été supprimé et remplacé par `InventoryItemView` ;
  - le reste de la fonction référence encore temporairement `kExpectedItems`
    et contient encore le stub direct joueur `+0x30`. La compilation suivante
    échouera tant que ces blocs ne sont pas remplacés.
- Travaux immédiats à effectuer dans `GrantCompleteDeluxeInventory` :
  1. restaurer la résolution de configuration/table de la V13.2 et valider
     `table_vtable +0x30` pour lire la catégorie ;
  2. résoudre/valider `snapshot->entity_list_object_address` comme mission et
     `mission_vtable +0x30` comme cible lisible ;
  3. remplacer le stub direct par la boucle filtrante ; écrire chaque `row`
     retenue à `remote +0x400 + count*4`, appeler mission `+0x30`, incrémenter
     le compteur, puis publier ce compteur à `remote +0x1800` ;
  4. après déclenchement, restaurer et vérifier le hook, attendre la page
     inactive, lire le compteur (attendu 111 dans cette session) et le tableau
     des IDs **avant** de libérer la page distante ;
  5. relire l'inventaire réel, trouver chaque ID dynamique, mettre `amount`
     à au moins 100, relire une seconde fois et n'annoncer le succès que si
     tous les IDs/quantités sont confirmés ;
  6. passer la limite de `src/weapon_mods.cpp` de 64 à 256 ;
  7. compiler Release et Debug x86, refaire les assertions statiques, PE,
     tailles et SHA-256, puis remplacer les résultats provisoires de 16.5.
- Encodage/édition : utiliser `apply_patch` exclusivement. Le worktree était
  déjà sale avec les changements V13.2/V13.3 ; préserver tous les changements
  existants et ne pas faire de reset/checkout destructif.

#### Test final minimal à demander

1. Fermer volontairement l'ancien trainer V13.2, lancer uniquement la Release
   V13.3 avec les mêmes droits que le jeu et garder une mission solo ouverte.
2. Appuyer une seule fois sur M. Le journal doit montrer la signature, le
   nombre dynamique de lignes, la restauration/inactivité/libération de page,
   puis `verified=N/N`. Vérifier visuellement plusieurs armes d'origine et du
   mod (pistolets, grenades, bazooka/lance-roquettes, M416 si ce nom existe
   réellement dans la table installée) ainsi que leurs réserves.
3. Appuyer sur K, cliquer successivement près de trois repères très éloignés.
   Chaque entrée de journal doit montrer `UnmapScreenPoint result=00000000`,
   `restored=1 idle=1 freed=1`, puis une position cohérente avec le repère.
4. En cas d'écart, récupérer immédiatement le nouveau `hdradar_diag.log` ; ne
   jamais annoncer 100 % avant ces observations dynamiques.

### 16.7 Reprise et finalisation demandée

- La migration Fullhands a repris. La fonction résout maintenant la table
  d'inventaire correspondant à la configuration installée, valide son query
  virtuel `+0x30`, puis résout séparément la mission vivante et valide
  `mission_vtable +0x30` comme `BroadcastMessage`. Les anciennes résolutions
  directes `player_vtable +0x30` ont été supprimées.
- État courant : la boucle distante, la liste dynamique et la vérification
  post-appel ont maintenant été remplacées : le stub filtre les catégories
  `!=0/4`, conserve chaque ID dans `remote+0x400`, appelle
  `mission->BroadcastMessage(14,9,row)` et publie le nombre à `remote+0x1800`.
- Après restauration et inactivité du hook, le trainer lit et valide la liste
  distante avant de libérer la page. Il exige ensuite chaque ID dans
  l'inventaire du joueur, porte sa réserve `amount` à au moins 100, relit le
  vecteur et ne réussit que sur `verified=N/N`. Les IDs dupliqués ou hors de
  `1..1023` sont rejetés.
- `src/weapon_mods.cpp` accepte désormais jusqu'à 256 objets afin que la
  résolution de l'arme équipée reste disponible après un Fullhands Ultimate
  Mod dépassant 64 lignes.
- Prochaine étape immédiate : compiler et corriger toute erreur statique,
  puis renouveler les binaires et leurs contrôles.
- La première compilation **Release x86** après cette migration réussit sans
  erreur (`gameplay_mods.cpp` et `weapon_mods.cpp` reconstruits). La Debug,
  les contrôles PE/statique et les nouvelles empreintes restent à exécuter.
- Le README ne décrit plus l'ancien appel joueur ni la liste fixe de 28 IDs ;
  il documente désormais le catalogue Ultimate Mod dynamique, la diffusion
  mission `+0x30`, la vérification complète et les réserves minimales de 100.
- La migration signalée « à mi-migration » en 16.6 est maintenant terminée :
  aucune référence compilée à `ExpectedItem`, `kExpectedItems` ou à une
  vtable joueur Fullhands ne subsiste.
- Les compilations finales **Release et Debug Win32/x86** réussissent sans
  erreur avec Visual Studio 2026. CTest ne trouve aucun test, car le dépôt ne
  définit toujours aucune suite automatisée.
- Livraison reconstruite :
  - Release : `build\vs2026-x86\Release\HDFinalAdvancedV13_3.exe`,
    `466432` octets, SHA-256
    `E2FB2B15EED66DA30B909FECB621F45D4401766642908F8AD6036C3F2D09C5A7` ;
  - Debug : `build\vs2026-x86\Debug\HDFinalAdvancedV13_3.exe`,
    `1731584` octets, SHA-256
    `A4D50D71CAD2305417456C608A587D85F5BC35B82E8EE0871E0359DDDF5F8DC2`.
- `dumpbin /headers` confirme pour la Release : machine `0x14C` x86,
  format PE32 (`magic 0x10B`) et sous-système Windows GUI.
- Les assertions finales confirment : receiver mission, résolution du slot
  `mission +0x30`, filtre dynamique des catégories `!=0/4`, enregistrement
  des IDs, lecture de cette liste avant libération, absence de liste fixe 28,
  `verified == completed` et réserves 100. Pour la carte : appel natif
  `UnmapScreenPoint +0x34`, aucune inversion matricielle manuelle et succès
  conditionné à la restauration, l'inactivité et la libération de page.
- `git diff --check` ne relève aucune erreur de diff, uniquement les
  avertissements de normalisation LF/CRLF. La session existante reste intacte
  (`hde.exe` PID `10576`, V13.2 PID `11700`) : la V13.3 finale n'a pas été
  lancée automatiquement et doit encore recevoir la validation en mission.

## 17. Retour de test V13.3 et correction V13.4

### 17.1 Résultat utilisateur reçu

- Le bouton Fullhands dans l'application fonctionne très bien. Ce test valide
  en jeu le catalogue Ultimate Mod dynamique, le receiver mission
  `BroadcastMessage +0x30`, l'ABI à quatre arguments côté acteurs et la
  vérification d'inventaire introduits en V13.3.
- La touche physique M ne déclenche toutefois aucune action, alors que le
  bouton appelle correctement la même fonction. Le défaut restant se situe
  donc avant `GrantCompleteDeluxeInventory`, dans la détection/traduction ou
  le filtrage de la touche M.
- La téléportation ne crashe pas, mais la destination reste différente du
  point choisi. Le nouvel `UnmapScreenPoint` s'exécute peut-être avec des
  coordonnées internes relevées au mauvais instant ou dans un repère de
  viewport différent ; le journal V13.3 doit trancher avant modification.

### 17.2 État de travail immédiat

1. Lire le dernier `hdradar_diag.log` V13.3 et confirmer si un événement
   « M key pressed » existe, puis auditer la chaîne de hotkey physique.
2. Comparer les coordonnées curseur, le résultat `UnmapScreenPoint`, le rayon,
   l'impact BSP et le sol sélectionné du dernier clic carte.
3. Corriger séparément M puis la conversion/pose carte, en documentant chaque
   mini-tâche dans cette section.
4. Produire un exécutable V13.4 distinct, compiler Debug/Release x86 et fournir
   un protocole de test ciblé. Aucune garantie fonctionnelle totale ne sera
   annoncée avant le retour dynamique.

### 17.3 Lecture du journal V13.3

- Fullhands bouton est confirmé par le journal : catalogue `111`, inventaire
  `112`, `present_after_native=111`, `verified=111/111`.
- Le journal contient aussi plusieurs activations physiques réussies
  (`Fullhands: M key pressed`) suivies du même `verified=111/111`, mais le
  dernier événement de la session est uniquement `button pressed`. La logique
  Fullhands n'est pas en cause ; le défaut intermittent de M est situé dans
  la condition de focus/lecture de hotkey et doit être rendu déterministe.
- Deux clics carte montrent `UnmapScreenPoint result=00000000`, restauration,
  inactivité et libération toutes validées. Exemples : curseur interne
  `(893,458)` → sol `(-10.3,3.7,95.4)` et `(1046,301)` →
  `(-15.3,4.1,137.4)`. Le moteur reçoit donc bien des coordonnées variables.
  L'erreur restante est après l'unmapping : impact BSP, choix du sol ou pose.
- Mini-tâche collecte/journal terminée. Prochaine étape : auditer en parallèle
  la condition `reserved_input_active`/M et `FindHighestWalkableGround`.

### 17.4 Correction de la touche M

- Le polling précédent ne testait que l'état haut de la touche physique
  `scan 0x27` traduite avec le layout du thread du jeu. Il pouvait manquer une
  frappe courte entre deux frames et dépendait entièrement de ce layout.
- `main.cpp` accepte maintenant à la fois la position physique M AZERTY et le
  virtual-key logique `M`. Le bit de transition `GetAsyncKeyState & 1`
  complète l'état maintenu `&0x8000`, tout en conservant le déclenchement sur
  front et la restriction de focus jeu/application.
- Le diagnostic de déclenchement inscrit désormais le virtual-key traduit et
  les deux états bruts. La mini-tâche correction M est implémentée ; sa
  compilation et son test dynamique restent à faire dans la future V13.4.

### 17.5 Correction de la destination carte

- L'hypothèse restante a été confirmée dans le désassemblage installé : juste
  après `UnmapScreenPoint`, le jeu construit un `I3D_collision_data` avec les
  flags exacts `0x01040000` (`EXACT|RAY`) puis appelle
  `map_scene->TestCollision` au slot vtable `+0x6C`. La V13.3 remplaçait cette
  étape par un raycast sur le BSP de la mission, qui contient des surfaces
  différentes de celles copiées/visibles dans la scène carte.
- Le trampoline carte V13.4 valide maintenant les deux cibles `+0x34` et
  `+0x6C`, fournit directement les sorties Unmap au `I3D_collision_data`, puis
  exécute le vrai `TestCollision` sur le même thread et le même tick du jeu.
  La structure reproduit les offsets installés : `from +0x04`, `dir +0x10`,
  `flags +0x1C`, `closest_hit +0x40`, valeur initiale `1e16f`.
- Le point cliqué est calculé selon le getter officiel :
  `from + dir * closest_hit / |dir|`. Le journal donnera désormais un bloc
  unique `native map pick` avec résultats Unmap/collision, distance et point
  d'impact exact.
- L'ancien raycast mission manuel après Unmap est supprimé. Seule la recherche
  verticale de sol praticable conserve les coordonnées X/Z exactes de
  l'impact natif de la carte. Mini-tâche implémentée ; compilation et test
  dynamique restent à faire.

### 17.6 Première compilation après corrections

- `main.cpp` et `radar.cpp` compilent, mais l'édition de liens V13.3 échoue
  avec `LNK1104` parce que l'exécutable Release V13.3 est actuellement lancé
  (PID `14344`) et Windows verrouille ce fichier. Ce n'est pas une erreur C++
  ni une raison d'arrêter la session utilisateur.
- La résolution prévue est de passer immédiatement la sortie à V13.4 afin de
  produire un nouveau fichier distinct sans fermer le trainer V13.3 actif.

### 17.7 Version et documentation V13.4

- CMake produit désormais `HDFinalAdvancedV13_4.exe`. Le titre de fenêtre,
  l'en-tête de journal et `build.ps1` utilisent `HD Final Advanced V13.4`.
- Le README pointe vers la Release V13.4 et documente la détection M double
  (scan AZERTY/VK logique avec frappes courtes) ainsi que la chaîne carte
  exacte `UnmapScreenPoint + TestCollision(EXACT|RAY)`.
- Cette mini-tâche est terminée. Il reste à compiler Debug/Release V13.4 et à
  exécuter les contrôles statiques/PE avant livraison.

### 17.8 Validation de build V13.4 — progression

- La compilation **Release Win32/x86** V13.4 réussit et produit le nouveau
  fichier distinct `build\vs2026-x86\Release\HDFinalAdvancedV13_4.exe` sans
  interrompre V13.3.
- La compilation **Debug Win32/x86** réussit également. CTest s'exécute mais
  ne trouve aucun test, le dépôt ne définissant pas de suite automatisée.
  Les contrôles source, PE, diff et empreintes sont maintenant terminés.
- Assertions statiques V13.4 toutes validées : M physique et logique, bit de
  transition et garde de focus ; carte slots `+0x34/+0x6C`, flags
  `0x01040000`, offsets collision `from +4`, `dir +0x10`,
  `closest_hit +0x40`, absence de l'ancien raycast BSP mission et libération
  de page exigée. Les nouveaux diagnostics sont présents dans la Release.
- `dumpbin /headers` confirme machine `0x14C` x86, format PE32
  (`magic 0x10B`) et sous-système Windows GUI. `git diff --check` ne signale
  aucune erreur, uniquement les avertissements LF/CRLF du dépôt.
- Binaires finaux :
  - Release : `build\vs2026-x86\Release\HDFinalAdvancedV13_4.exe`,
    `466944` octets, SHA-256
    `CC680E11DF0042399282F16F3FE63CB48CB23C4AADF8088B72604701FA69B2D9` ;
  - Debug : `build\vs2026-x86\Debug\HDFinalAdvancedV13_4.exe`,
    `1732608` octets, SHA-256
    `F9AE6C72D03094A9F7C144250F8C79F4715C7FD9A772097A8B4CF007AFD785B2`.
- La session utilisateur n'a pas été interrompue : `hde.exe` PID `16308` et
  V13.3 PID `14344` sont toujours actifs. Les problèmes sont corrigés dans le
  code et les builds ; le test dynamique V13.4 de M et de plusieurs clics
  carte reste la seule étape non réalisée.

## 18. V15 — inventaire limité et ancre exacte du joueur

- Le FullHands de 112 objets est abandonné, car le menu natif charge un modèle
  3D par entrée et peut planter sous cette charge.
- La nouvelle sélection est plafonnée à 50 objets au total, inventaire déjà
  présent compris : jusqu'à 25 armes automatiques ou en rafale, jusqu'à cinq
  lanceurs/bazookas, quatre objets par autre famille tactique, ainsi que les
  vêtements et clés dans une réserve dédiée. Les pistolets lents de types 9
  et 13 sont explicitement exclus.
- La sélection est déterminée depuis les propriétés du catalogue Ultimate Mod
  sur le thread du jeu (`ID`, puissance, mode de tir et pose), puis chaque ligne
  retenue reste vérifiée après l'appel natif.
- Le décalage initial de carte venait de l'origine de l'acteur située aux pieds
  du modèle. La V15 lit le head-frame du joueur actuellement contrôlé et place
  le curseur au centre visuel entre ses pieds et sa tête. Un ancrage torse de
  secours est utilisé si le head-frame est indisponible.
- Le point de téléportation reste le résultat exact de
  `UnmapScreenPoint + TestCollision(EXACT|RAY)` ; seule l'initialisation du
  curseur change.
- La Release x86 V15 compile avec succès. Les contrôles PE/source sont validés ;
  le test fonctionnel doit être réalisé avec un nouveau processus `hde.exe`
  afin de repartir d'un inventaire propre et de ne pas conserver les 112 objets
  déjà injectés par la V14.
- Binaire : `build\vs2026-x86\Release\HDFinalAdvancedV15_CURATED50_PLAYER_ANCHOR.exe`,
  `479232` octets, PE32 x86/Windows GUI, SHA-256
  `5D030AD73ADF655CFC84C389FA12A788251F278D3505F018E47C4A030B4FC52F`.
- CTest s'exécute correctement mais le dépôt ne contient toujours aucune suite
  automatisée ; la validation finale dépend donc du journal dynamique V15.

## 19. V16 — joueur contrôlé, cache inventaire et centre natif K

### Recherche dans les sources officielles Deluxe

- `Inventory.cpp::LoadModels` recrée et recharge un modèle 3D pour chaque
  entrée à chaque ouverture. `ReleaseModels` remet ensuite chaque pointeur
  `items[i]->model` à `NULL`. Le blocage qui revient après une fermeture
  réelle de l'inventaire vient donc de ce rechargement différé, et non d'une
  seconde activation de M.
- `Actors.cpp::C_actor::cbProc(CB_CHEAT, 9, ...)` traite l'inventaire d'un
  acteur joueur. La diffusion par `C_game_mission::BroadcastMessage` touchait
  logiquement tous les équipiers ; V16 appelle désormais directement le slot
  `cbProc +0x04` du seul `player_object_address` contrôlé.
- `Map_man.cpp::Activate` choisit l'acteur dont `IsActive()` est vrai.
  `SetActive` copie sa position X/Z dans `look_at`, puis `PositionCamera`
  pointe exactement la caméra sur ce `look_at`. La coordonnée carte exacte du
  joueur contrôlé est donc le centre du viewport. `mouse_x/mouse_y` n'étaient
  toutefois centrés que dans `Init`, ce qui expliquait la position conservée
  des ouvertures précédentes.

### Corrections réalisées

- Fullhands reste plafonné à 50 objets, avec jusqu'à 25 armes rapides : modes
  automatique/rafale, ainsi que les armes à tir unique dont le délai natif est
  inférieur ou égal à 300 ms. Les quotas lanceurs, équipement, vêtements et
  clés restent bornés.
- L'octroi utilise le callback du joueur actuellement contrôlé et ne diffuse
  plus à l'escouade. M et le bouton sont consommés après un succès ; le bouton
  devient immédiatement désactivé. Une erreur réarme l'action afin de pouvoir
  réessayer.
- Le cache persistant déjà préparé est maintenant installé avant l'octroi :
  il conserve les modèles lors de `ReleaseModels` et les réutilise au prochain
  `LoadModels`. La page de cache reste volontairement valide jusqu'à la fin du
  processus `hde.exe`.
- Une session de téléportation possède désormais un état `armed_by_k`. Une
  carte ouverte nativement avec Espace accepte les clics du jeu mais aucun
  clic du trainer. K seul ouvre et arme la session.
- À l'ouverture par K, V16 écrit le centre exact dans les champs natifs
  `mouse_x/mouse_y` du gestionnaire de carte. L'écriture est répétée pendant
  une fenêtre bornée de 180 ms (toutes les 35 ms) afin d'absorber le dernier
  delta DirectInput en attente, puis rend immédiatement le contrôle normal de
  la souris.

### Validation statique

- La Release Win32/x86 V16 compile avec Visual Studio 2026.
- `git diff --check` ne signale aucune erreur de whitespace (uniquement les
  avertissements CRLF existants). Aucun test CTest n'est défini dans le
  projet ; les vérifications PE, symboles et hash du binaire complètent la
  validation avant test réel dans le jeu.
- Binaire :
  `build\vs2026-x86\Release\HDFinalAdvancedV16_PLAYER_ONLY_CACHE_NATIVE_CENTER.exe`,
  `480768` octets, PE32 x86/Windows GUI, SHA-256
  `8B820D48FEC13F6BD110785FD1B991D7B3C3B4019FC820CE02DCBDE6B0BE64FA`.

## 20. V17 — correction verticale widescreen et suppression du cache dangereux

### Preuves du test V16

- Le journal V16 confirme trois octrois complets avant arrêt : sélection
  `fast=25`, `essential=10`, total `36`, puis vérification `36/36`. Le callback
  direct du joueur contrôlé et la sélection ne sont donc pas la cause de
  l'arrêt.
- Immédiatement après chaque octroi, Windows enregistre le même crash de
  `hde.exe` : exception `0xC0000096`, aux adresses `0x024300F2`,
  `0x024500F2` et `0x025500F2`. Le seul changement persistant restant actif
  après la fin du trampoline était le cache de modèles installé dans
  `LoadModels/ReleaseModels`.
- Le cache conservait des smart pointers que la scène native s'attend à
  libérer. Lors de l'ouverture/fermeture suivante, le jeu finissait par
  exécuter via un modèle devenu invalide. V17 n'installe plus ce cache et
  respecte intégralement la durée de vie native des modèles.

### Cause exacte du décalage vertical

- Le journal V16 prouve que le gestionnaire recevait et relisait bien
  `(960,540)`. Le X était exact, mais le joueur voyait toujours la flèche plus
  bas.
- La lecture de `Map_man.cpp::Tick` révèle la conversion manquante :
  `spr_arrow->SetScreenPos(mouse_x, mouse_y /
  (screen_aspect_ratio * 1.33333333f))`, avec
  `screen_aspect_ratio = hauteur / largeur`.
- En 1920×1080, le facteur vaut `0.75` : écrire `mouse_y=540` dessine donc la
  flèche à `720`. C'est exactement le décalage vertical signalé, tandis que le
  X reste inchangé.

### Correction V17

- L'origine du modèle du joueur contrôlé, copiée telle quelle par
  `C_map_manager_imp::Activate`, est projetée avec la matrice exacte de la
  caméra native de carte.
- Avant écriture, le Y projeté est multiplié par le facteur widescreen. Ainsi,
  un joueur projeté à Y=540 produit `mouse_y=405`, et la flèche native affiche
  de nouveau `405/0.75=540`.
- Au clic, l'opération inverse est appliquée avant
  `UnmapScreenPoint`: le rayon utilise le même point que la flèche réellement
  visible. La correction ne se contente donc pas de déplacer graphiquement le
  curseur ; elle conserve l'identité entre curseur, rayon et destination.
- La séparation K/Espace et l'appel Fullhands limité au joueur contrôlé sont
  conservés.

### Validation de build V17

- Release Win32/x86 compilée avec succès ; `dumpbin` confirme machine `0x14C`
  et sous-système Windows GUI.
- La chaîne `Fullhands cache: installed` est absente du binaire final, tandis
  que les deux opérations inverses du Y (`projection * facteur`, puis
  `mouse_y / facteur`) sont présentes dans les sources compilées.
- CTest ne trouve toujours aucune suite automatisée. `git diff --check` ne
  remonte aucune erreur, uniquement les avertissements CRLF historiques.
- Binaire :
  `build\vs2026-x86\Release\HDFinalAdvancedV17_SAFE_FULLHANDS_ASPECT_MAP.exe`,
  `480256` octets, SHA-256
  `F11374CB15F1D58D5A7EDC197F0A27FAF0869C011F33B3F3FD6A8F8591666158`.

## 21. V18 — correction définitive de la pile Fullhands

### Cause confirmée après le test V17

- V17 ne contient plus le cache de modèles, mais `hde.exe` s'arrête encore
  juste après un octroi pourtant validé `36/36`. Le cache n'était donc pas la
  cause de cette sortie.
- Le désassemblage de l'exécutable Ultimate Mod installé montre que le callback
  acteur situé à `0x0042B9B0` termine tous ses chemins par `ret 0x10` : il
  dépile exactement quatre paramètres.
- Le trampoline direct du joueur contrôlé n'en poussait que trois. Chaque objet
  faisait ainsi avancer ESP de quatre octets supplémentaires ; après 36 objets,
  `popad/ret` utilisait une pile décalée de 144 octets et sautait dans la page
  distante. Cela correspond exactement à l'exception Windows `0xC0000096`
  observée aux adresses de forme `0x02xx00F2`.

### Correction et sécurité V18

- L'appel direct pousse désormais les quatre paramètres conformes à
  `BroadcastMessage` : zéro réservé, ligne catalogue, cheat Fullhands et
  message callback. Le callback `ret 0x10` retrouve donc une pile équilibrée.
- Le trampoline enregistre ESP avant la boucle, mesure ESP après celle-ci et
  restaure systématiquement sa valeur de référence avant `popad`. Le trainer
  refuse ensuite le résultat si le journal ne confirme pas
  `stack_before == stack_after`. Une future divergence d'ABI est ainsi rejetée
  sans laisser le jeu retourner sur une adresse corrompue.
- La sélection bornée (25 armes rapides, vêtements, clés et équipement) et
  l'octroi au seul joueur contrôlé sont conservés. La carte V17, maintenant
  validée en jeu, n'a reçu aucune modification.

### Validation de build V18

- Release Win32/x86 compilée avec succès. `dumpbin` confirme machine `0x14C`
  et sous-système Windows GUI. Aucun test CTest n'est défini dans le dépôt.
- Binaire :
  `build\vs2026-x86\Release\HDFinalAdvancedV18_FULLHANDS_ABI4_MAP_VALIDATED.exe`,
  `480256` octets, SHA-256
  `DF0E96F0AE28D2C78E5F24F699D97BC9128297C56D2411AB4D5C9040AEE6CF16`.

## 22. V19 — toutes les armes validées et téléportation du véhicule

### Fullhands sans entrées inconnues

- La sélection n'est plus limitée aux 25 armes rapides. Toutes les catégories
  d'armes officielles sont éligibles : armes à feu, grenades, mines, flare
  guns, bazookas, panzers, bombes à retardement, couteaux et colts. Les lignes
  ajoutées par le mod sont incluses lorsqu'elles utilisent ces catégories
  natives, quel que soit leur mode ou délai de tir.
- Avant tout callback, chaque ligne doit fournir un ID réel, un nom non vide,
  un modèle de scène et un modèle d'inventaire. Les noms commençant par
  `unknown` ainsi que les lignes vides ou incomplètes sont rejetés.
- Le plafond de sécurité est fixé à 80 objets : jusqu'à 64 armes validées,
  dix vêtements/clés et six objets utiles. Ce plafond reste nettement inférieur
  aux 112 modèles chargés par l'ancien Fullhands brut. Le callback ABI4 et le
  contrôle `stack_before == stack_after` de V18 sont conservés.

### Téléportation avec automobile

- `Vehicle.cpp` confirme que le modèle du joueur assis est lié au frame de son
  siège (`model->LinkTo(seat)`). Déplacer le frame racine de l'automobile
  transporte donc le conducteur et tous les passagers sans les détacher.
- Après fermeture de la carte, V19 réutilise `ResolveControlledVehicle`. Si le
  joueur contrôlé conduit, la cible de `SetPos` devient le frame du véhicule ;
  sinon la cible reste strictement le frame du joueur comme dans V18.
- Le trampoline appelle d'abord le chemin natif
  `CB_USE_AUTO(13)` pour arrêter proprement moteur/vitesse, avec les quatre
  paramètres attendus par `cbProc`, puis place le véhicule à 0,65 m au-dessus
  du sol sélectionné. La vérification post-pose surveille désormais le frame
  effectivement déplacé.
- La projection verticale, le rayon de carte et la séparation K/Espace déjà
  validés ne sont pas modifiés.

### Validation de build V19

- Release Win32/x86 compilée avec succès. `dumpbin` confirme machine `0x14C`
  et sous-système Windows GUI. `git diff --check` ne relève aucune erreur ; le
  dépôt ne définit toujours aucun test CTest.
- Binaire :
  `build\vs2026-x86\Release\HDFinalAdvancedV19_VALID_WEAPONS_VEHICLE_TELEPORT.exe`,
  `480768` octets, SHA-256
  `E17AFF7282C5B95B4D9D12FC687896801A56A24096B72EB36EA595E89A05A6EC`.

## 23. V20 — registres Fullhands et secteur natif du véhicule

### Diagnostic dynamique V19

- Le journal V19 montre trois délais Fullhands avec `completion=0`. La page
  distante encore lisible dans `hde.exe` contient pourtant les quotas atteints :
  `64 armes`, `10 essentiels`, `6 utilitaires`, et une pile équilibrée.
- La liste de lignes ne contient que 35 cases non nulles et plusieurs valeurs
  répétées (`44`, `113`, `114`). Les appels de propriétés chaîne du tableau
  modifiaient donc les registres EBX/EBP utilisés comme ligne et compteur. Le
  callback ajoutait une partie des objets, puis le compteur final revenait à
  zéro ; ce n'était ni un manque de catégories ni une entrée inconnue précise.
- La voiture est correctement détectée (`target=vehicle`) et `SetPos` modifie
  sa position locale, mais le journal relit immédiatement l'ancienne position
  monde. Le frame racine restait lié au secteur de scène d'origine. Les
  écritures directes de vérification masquaient ce problème sans déplacer la
  hiérarchie de l'automobile et de ses sièges.

### Corrections V20

- Chaque requête du catalogue et chaque callback acteur sauvegarde/restaure
  explicitement EBX, ESI, EDI et EBP. Le marqueur de fin vaut désormais `1`
  indépendamment du nombre sélectionné ; ce nombre est publié dans un champ
  séparé. Le journal affiche `completion_state=1 selected=...`.
- Le plafond passe à 112 objets, avec jusqu'à 96 armes validées, dix
  vêtements/clés et six utilitaires. Les contrôles nom/modèles et l'exclusion
  `unknown` restent obligatoires.
- Pour un véhicule, le trampoline exécute désormais : arrêt natif
  `CB_USE_AUTO(13)`, première pose, `I3D_scene::SetFrameSectorPos` au point
  monde choisi, puis seconde pose dans le nouveau secteur. La vérification ne
  simule plus un succès par une simple écriture de coordonnées ; elle rejoue
  le chemin natif complet si la position monde est encore rétablie.

### Validation de build V20

- Release Win32/x86 compilée sans erreur. `dumpbin` confirme machine `0x14C`
  et sous-système Windows GUI. Aucun test CTest n'est défini dans le dépôt.
- Binaire :
  `build\vs2026-x86\Release\HDFinalAdvancedV20_FULL_WEAPONS_SECTOR_VEHICLE.exe`,
  `482304` octets, SHA-256
  `34DA708EDA4ACC0B70B53A64BCE8676EE00DD3CDC8F2474707830250C2A54D3D`.

## 24. V21 — catalogue nommé complet et mise à jour monde du véhicule

### Diagnostic du test V20

- Le journal utilisateur prouve que Fullhands terminait normalement mais avec
  `completion_state=1 selected=0`. Les propriétés supposées « modèle scène »
  et « modèle inventaire » éliminaient donc toutes les lignes avant le
  callback ; ce n'était pas une limite de capacité.
- La première transaction véhicule V20 terminait (`freed=1`) et écrivait la
  destination locale, tandis que la lecture immédiate de `frame+0xBC`
  conservait l'ancienne valeur monde mise en cache. La vérification lançait
  alors une deuxième transaction. Le rapport Windows confirme ensuite la
  sortie fatale de `hde.exe` avec `STATUS_SINGLE_STEP (0x80000004)` pendant
  cette répétition.

### Corrections V21

- Fullhands accepte désormais toutes les lignes dont la catégorie appartient
  aux familles d'armes officielles et dont le nom est réel, non vide et ne
  commence pas par `unknown`. Les propriétés de modèle mal indexées ne sont
  plus interrogées ; le callback Fullhands natif reste seul responsable de la
  construction de l'objet. Le plafond total passe à 128 et le quota armes à
  112, ce qui couvre les 111 lignes utilisables observées tout en gardant les
  vêtements, clés et utilitaires.
- Après la seconde `SetPos` du véhicule, le trampoline appelle une seule fois
  `I3D_frame::GetWorldPos` au slot officiel `+0x24`. Cet appel force le calcul
  différé de la matrice monde avant la vérification.
- La vérification véhicule est strictement en lecture seule : elle ne réappelle
  jamais `CB_USE_AUTO`, `SetFrameSectorPos` ou `SetPos`. L'ancien fallback qui
  réécrivait directement les caches monde/local et déclenchait une transaction
  récursive est supprimé pour les automobiles. La téléportation à pied et les
  calculs de carte validés ne sont pas modifiés.

### Validation de build V21

- Les configurations Release et Debug Visual Studio 2026 Win32/x86 compilent
  sans erreur. CTest ne trouve aucune suite automatisée définie dans le dépôt.
  `git diff --check` ne relève aucune erreur de patch.
- `dumpbin /headers` confirme pour la Release : machine `0x14C` (x86), PE32
  (`magic 0x10B`) et sous-système Windows GUI.
- Release :
  `build\vs2026-x86\Release\HDFinalAdvancedV21_SAFE_ALL_WEAPONS_VEHICLE_WORLD_UPDATE.exe`,
  `482816` octets, SHA-256
  `DF7102F29FDC2C9397143782E0D085FF7D5D5C271F6F9848A5B2201BF5E1E8C1`.

## 25. V22 — véritable Update I3D du modèle automobile

### Diagnostic dynamique V21

- Fullhands est déclaré fonctionnel par le test utilisateur et reste inchangé.
- Pour l'automobile, la transaction unique V21 terminait et libérait sa page,
  mais le journal relisait `local=destination` avec `world=ancienne_position`.
  Environ 1,5 seconde plus tard, `hde.exe` quittait. Le nouveau rapport WER
  confirme une violation d'accès `0xC0000005`, cohérente avec la physique
  traitant un frame local/monde incohérent.
- Le source officiel `Vehicle.cpp` montre la séquence exacte :
  `model->SetPos`, `model->Update`, mise à jour des roues, puis
  `scene->SetFrameSector(model)`. `GetWorldPos` n'est pas un substitut à
  `Update`.
- Le désassemblage de l'`i3d2.dll` installé localise la fonction non virtuelle
  `I3D_frame::Update` au RVA `0x34D0`; elle lit son frame explicite sur la pile,
  recopie `pos +0x14C` vers la translation de matrice locale `+0xFC`, invalide
  les matrices hiérarchiques puis retourne par `ret 4`.

### Correction V22

- `TrainerProcess::GetModuleInfo` résout désormais un module distant par nom.
  Avant toute téléportation automobile, le trainer exige `i3d2.dll`, le RVA
  `0x34D0`, une cible exécutable et les 15 octets exacts de la fonction
  installée. Une autre révision est refusée sans appel.
- La cible automobile vient du véritable champ `C_actor::model` à `actor+0x24`
  confirmé par le layout source, et non plus du frame générique `actor+0x28`.
- Le trampoline n'appelle plus `SetFrameSectorPos` ni `GetWorldPos`. Après
  l'arrêt natif, il exécute une seule fois `SetPos(model)`, l'Update I3D direct,
  puis `I3D_scene::SetFrameSector(model)` au slot officiel `+0x60`. La
  vérification ultérieure demeure uniquement en lecture.

### Validation de build V22

- Release et Debug Visual Studio 2026 Win32/x86 compilent sans erreur et
  `git diff --check` ne relève aucune erreur. `dumpbin` confirme PE32,
  machine `0x14C` et sous-système Windows GUI.
- Release :
  `build\vs2026-x86\Release\HDFinalAdvancedV22_SAFE_ALL_WEAPONS_NATIVE_VEHICLE_UPDATE.exe`,
  `482816` octets, SHA-256
  `5AAE6902D99858254144AD6B77DA1BB0168D6CC23B58E3C3B95EE14F80EAE051`.

## 26. Plan V23 — mode Noclip contrôlé et réversible

### Objectif utilisateur

- Ajouter une nouvelle case `Noclip` dans l'application et une touche de
  bascule, proposée initialement sur **V**.
- Premier appui : activer/cocher Noclip. Second appui : désactiver/décocher
  automatiquement Noclip et restaurer immédiatement le comportement normal du
  joueur.
- Pendant Noclip, conserver les actions non locomotrices du joueur, notamment
  le tir et l'utilisation normale de l'inventaire, mais remplacer les commandes
  de marche du jeu par un déplacement spatial libre :
  - **Z** : avancer selon la direction de vue ;
  - **S** : reculer ;
  - **A** : translation latérale gauche ;
  - **E** : translation latérale droite ;
  - **H** : monter verticalement ;
  - **B** : descendre verticalement ;
  - **F8** : augmenter la vitesse Noclip ;
  - **F9** : diminuer la vitesse Noclip.
- Lorsque le mode est actif, empêcher les commandes natives avancer/reculer et
  rotation droite/gauche de produire simultanément un second mouvement. À la
  sortie du mode, restaurer ces commandes sans laisser de patch permanent.

### Audit obligatoire avant implémentation

1. Relever toutes les affectations actuelles de V, Z, S, A, E, H, B, F8 et F9
   dans `main.cpp`, le système de hotkeys et les profils véhicule. Identifier
   notamment le conflit connu de **B** avec la réduction de vitesse véhicule et
   tout usage existant de **V**.
2. Vérifier dans le source H&D Deluxe et dans le binaire installé le chemin
   exact des entrées locomotrices, la représentation de la caméra et la méthode
   sûre de mise à jour de position du joueur. Ne pas réutiliser une écriture de
   frame non synchronisée avec le moteur.
3. Choisir une architecture qui reste compatible avec le tir : neutralisation
   ciblée de la locomotion uniquement, sans bloquer le clavier entier ni le
   callback d'arme.
4. Définir explicitement le comportement en véhicule. Par défaut de sécurité,
   l'activation Noclip sera refusée/suspendue pendant la conduite tant qu'un
   déplacement cohérent du véhicule n'est pas demandé et validé.

### Architecture prévue

- Ajouter à `GameplaySettings` l'état Noclip, sa vitesse bornée, la pose de
  référence et les informations nécessaires à une restauration complète.
- Ajouter à `GameplayInput` des états maintenus (avant, arrière, gauche, droite,
  haut, bas) et des fronts pour V/F8/F9, lus uniquement lorsque la fenêtre du
  jeu est active.
- Calculer à chaque tick un vecteur 3D à partir de la direction de caméra :
  projection horizontale normalisée pour Z/S/A/E et axe monde Y pour H/B.
  Normaliser les diagonales afin qu'elles ne soient pas plus rapides.
- Déplacer le joueur sur le thread du jeu par une primitive I3D vérifiée, avec
  delta dépendant du temps et vitesse bornée. Maintenir la position lorsque
  aucune touche Noclip n'est pressée afin d'éviter gravité/chute.
- Neutraliser seulement les champs/commandes natives de locomotion pendant le
  mode et les restaurer à zéro/état normal lors de la désactivation, d'un
  changement de mission, d'une perte de processus ou de la fermeture du
  trainer.
- Afficher la case, la vitesse courante, les touches et un statut précis dans
  l'interface. Journaliser activation, désactivation, refus véhicule, position,
  direction, delta, vitesse et résultat de chaque mise à jour significative.

### Gestion des conflits proposée

- **V** ne sera retenue que si l'audit confirme qu'elle n'est pas déjà une
  commande indispensable. Sinon, déplacer l'ancien raccourci configurable ou
  choisir une touche libre tout en laissant le bouton UI disponible.
- Pendant Noclip, **B** commande exclusivement la descente et ne réduit pas la
  vitesse véhicule ; hors Noclip, son comportement véhicule actuel reste
  inchangé.
- Pendant Noclip, **F8/F9** règlent exclusivement sa vitesse. Hors Noclip, leurs
  fonctions existantes restent inchangées.

### Critères de validation et livraison

1. Activation/désactivation répétée par V et par la case sans crash, dérive ni
   commande bloquée après désactivation.
2. Six directions et diagonales testées ; vitesse indépendante de la fréquence
   de tick, F8/F9 bornées et journalisées.
3. Tir, changement d'arme et ouverture d'inventaire fonctionnels pendant le
   mode ; aucune marche/rotation native mélangée au déplacement libre.
4. Activation en véhicule refusée proprement ; téléportation carte V22 et
   Fullhands restent inchangés et doivent passer leurs tests de non-régression.
5. Restauration testée après changement de mission, mort/recréation du joueur,
   perte de focus, fermeture du trainer et arrêt de `hde.exe`.
6. Compilation Release et Debug Win32/x86, `git diff --check`, contrôle PE32,
   SHA-256 et création d'une Release V23 distincte. Le journal de cette section
   devra être complété avec les adresses/signatures réellement confirmées et
   le résultat des builds.

### Implémentation V23 réalisée

- La case Noclip et la bascule physique V sont intégrées. Un hook clavier
  `WH_KEYBOARD_LL` mémorise directement les fronts/états physiques avant de
  filtrer V lorsque le jeu est actif, puis les scans AZERTY A/Z/E/S/H/B et les
  anciennes commandes latérales Q/D pendant le mode. Si son installation
  échoue, l'activation est annulée : aucune exécution dégradée ne mélange les
  commandes natives et Noclip.
- Les six états maintenus sont transmis à `GameplayInput`. Le mouvement utilise
  l'axe horizontal de la caméra active, son axe droit et l'axe monde Y. Les
  diagonales sont normalisées, le delta est borné à 50 ms et la vitesse à
  `1..80 m/s`.
- Le déplacement ne lance aucun thread distant et ne touche pas au callback de
  tir. Il neutralise `actor+0x1AC`, écrit la position souhaitée dans
  `frame+0x14C`, puis ajoute `FRMFLAGS_UPDATE_NEEDED (0x00800000)` à
  `frame+0x0C`. Le tick natif appelle ainsi lui-même `I3D_frame::Update` et
  propage l'invalidation hiérarchique.
- V, la case UI, un changement d'acteur/processus, une erreur d'écriture, la
  conduite et la fermeture du trainer passent tous par une restauration
  commune qui remet le vecteur de locomotion à zéro et supprime l'état Noclip.
- Pendant Noclip, F8/F9 modifient exclusivement sa vitesse et B ne peut pas
  diminuer le multiplicateur véhicule. Hors Noclip, Super Run et N/B gardent
  exactement leur comportement V22.
- Le journal contient l'installation du filtre, activation/désactivation,
  refus véhicule, contexte acteur/frame, déplacements bornés et erreurs.

### Validation statique V23 et Release livrée

- Configuration CMake VS 2026 Win32/x86 : réussie avec le SDK Windows
  `10.0.26100.0` et une cible minimale Windows 10 `10.0.19045`.
- Builds **Release** et **Debug** : réussis sans erreur pour la cible
  `HDPhase1`.
- `ctest -C Release --output-on-failure` : infrastructure exécutée, aucun test
  automatisé n'est déclaré dans ce projet ; les essais dans H&D Deluxe restent
  donc nécessaires pour valider le mouvement réel et les non-régressions.
- `git diff --check` : aucune erreur d'espace ; uniquement les avertissements
  de conversion LF vers CRLF déjà propres à la copie de travail Windows.
- Contrôle du binaire : machine `14C (x86)`, format `PE32`, sous-système
  `Windows GUI`.
- Release :
  `build\vs2026-x86\Release\HDFinalAdvancedV23_NOCLIP_SAFE_ALL_WEAPONS_NATIVE_VEHICLE.exe`,
  `487424` octets, SHA-256
  `AFA3E6864530695BD6C71A6EF3933919E734D2C2C89441AA83CD7DDF8715BABB`.

## 27. Correctif V23.1 — stabilité verticale du Noclip

### Cause confirmée

- Le test réel montre une oscillation verticale : le joueur tombe sous l'effet
  de la physique native, puis le trainer le replace à la hauteur Noclip au tick
  suivant.
- `Actors.cpp::C_human::Tick` confirme que le moteur soustrait
  `fall_speed * tsec` à la position et augmente ensuite `fall_speed` quand
  aucune collision de sol n'est trouvée. Réécrire seulement `frame+0x14C` ne
  désactive donc pas la gravité.
- Le désassemblage installé confirme `fall_speed` à `actor+0x1B8`, l'état
  `falling` à `actor+0x264` et `test_col_count` à `actor+0x274`.

### Correction prévue

- À l'activation, valider et mémoriser les trois champs physiques du seul
  joueur contrôlé.
- Pendant Noclip, maintenir `fall_speed=0`, `falling=false` et un compteur de
  collision positif, en plus du vecteur de marche nul. Le tick natif ne peut
  ainsi plus produire une chute entre deux poses Noclip.
- À la désactivation, à la fermeture ou au changement d'acteur, remettre
  `fall_speed` à sa valeur initiale sûre (`0.01`), `falling=false` et
  `test_col_count=0` afin que le moteur refasse immédiatement son contrôle de
  sol et reprenne une chute normale si le joueur est réellement dans les airs.
- Produire une Release V23.1 distincte, compiler Release/Debug x86 et conserver
  dans le journal les résultats et le SHA-256.

### Implémentation et validation V23.1

- `UpdateNoclip` valide maintenant les trois champs physiques confirmés avant
  toute écriture. Chaque tick actif maintient `fall_speed=0`, `falling=0` et
  `test_col_count=1000`, puis pose la position Noclip et le drapeau de mise à
  jour du frame.
- La restauration commune remet le vecteur locomoteur à zéro,
  `fall_speed=0.01`, `falling=0` et `test_col_count=0`. Cette dernière valeur
  demande au moteur de refaire son test de sol au tick suivant.
- Builds **Release** et **Debug** VS 2026 Win32/x86 : réussis sans erreur.
- `ctest -C Release` : exécuté, aucun test automatisé déclaré. Le maintien réel
  en vol doit être confirmé en mission par l'utilisateur.
- `git diff --check` : aucune erreur d'espace ; seulement les avertissements
  Windows LF/CRLF de la copie de travail.
- Contrôle PE : `14C (x86)`, `PE32`, sous-système `Windows GUI`.
- Release :
  `build\vs2026-x86\Release\HDFinalAdvancedV23_1_NOCLIP_STABLE_ALL_WEAPONS_NATIVE_VEHICLE.exe`,
  `487936` octets, SHA-256
  `F76A54442709CE0A7D7FDCDAD7D3376B22F864424EE20EFB8D004E1F6B2E741F`.

## 28. Correctif V23.2 — fluidité horizontale du Noclip

### Cause confirmée

- La pose Noclip est correcte, mais la boucle principale est synchronisée par
  `D3DPRESENT_INTERVAL_ONE`. Elle ne publie donc qu'environ 60 positions par
  seconde, indépendamment de la fréquence d'affichage du jeu.
- Le delta Noclip emploie en plus `GetTickCount64`, limité à la milliseconde :
  à faible vitesse, l'alternance des pas temporels reste perceptible dans les
  directions avant/arrière/gauche/droite.

### Correction prévue

- Passer le petit périphérique Direct3D du trainer en présentation immédiate,
  puis cadencer explicitement la boucle à 60 Hz hors Noclip et 240 Hz pendant
  Noclip avec un waitable timer haute résolution. Cette limite évite une boucle
  CPU illimitée.
- Calculer le delta du déplacement avec `std::chrono::steady_clock` et conserver
  la borne de sécurité de 50 ms.
- Ne modifier ni la vitesse en m/s, ni les axes, ni le blocage de gravité validé
  en V23.1. Compiler Release/Debug x86 et livrer une V23.2 distincte.

### Implémentation et validation V23.2

- Le périphérique Direct3D utilise désormais la présentation immédiate ; la
  fonction `PaceTrainerLoop` impose explicitement 60 Hz hors Noclip et 240 Hz
  pendant Noclip avec `CreateWaitableTimerExW` haute résolution et un fallback
  compatible si ce type de timer n'est pas disponible.
- Le déplacement emploie `std::chrono::steady_clock` et un delta flottant en
  secondes, toujours borné à 50 ms. Les pas Noclip sont ainsi environ quatre
  fois plus petits sans modifier la vitesse en m/s.
- Builds **Release** et **Debug** VS 2026 Win32/x86 : réussis sans erreur après
  correction d'une déclaration d'horloge initialement placée dans la mauvaise
  portée par le patch mécanique.
- `ctest -C Release` : exécuté, aucun test automatisé déclaré.
- `git diff --check` : aucune erreur d'espace ; uniquement les avertissements
  LF/CRLF attendus sous Windows.
- Contrôle PE : `14C (x86)`, `PE32`, sous-système `Windows GUI`.
- Release :
  `build\vs2026-x86\Release\HDFinalAdvancedV23_2_NOCLIP_SMOOTH_ALL_WEAPONS_NATIVE_VEHICLE.exe`,
  `488960` octets, SHA-256
  `69BF5078CB2CE37BB30AEB6437DE6A4424F99E9E01C3632C3107692A998B13D1`.

## 29. Correctif V23.3 — déplacement synchronisé au tick natif

### Résultat du test V23.2

- Le passage de 60 à 240 écritures externes ne supprime pas les saccades. La
  fréquence du trainer n'est donc pas la cause finale : le jeu et sa caméra
  consomment la transformation sur leur propre tick, avec une phase différente.
- Toute nouvelle augmentation de fréquence externe serait inutile et
  augmenterait seulement la charge CPU et les risques de concurrence mémoire.

### Architecture retenue

- Installer un trampoline vérifié à l'entrée de `C_human::Tick`, confirmée à
  `module+0x1A560` par la signature native
  `55 8B EC 81 EC 6C 03 00 00`.
- Le trampoline ne traite que l'adresse exacte du joueur contrôlé publiée dans
  son bloc partagé. Il emploie directement `S_tick_context::time` du jeu pour
  intégrer le vecteur Noclip, écrit la pose et maintient les champs de gravité
  V23.1 sur le thread natif avant que la caméra consomme la transformation.
- La boucle externe ne déplacera plus le frame : elle publiera seulement
  acteur, direction normalisée, vitesse et activation. Le hook restera inactif
  pour tous les autres humains et lorsque Noclip est décoché.
- Refuser l'installation si la signature ou le trampoline diffère, restaurer
  exactement les neuf octets à la fermeture et ne libérer la page distante
  qu'après vérification qu'aucun thread ne l'exécute.
- Revenir à la cadence normale du trainer : la fluidité doit désormais venir
  du tick H&D, pas d'une boucle externe accélérée. Livrer une V23.3 distincte
  après builds Release/Debug x86 et contrôle PE/hash.

### Implémentation et validation V23.3

- Le trampoline x86 est installé uniquement après lecture exacte des neuf
  octets de `C_human::Tick`. Il sauvegarde flags/registres, vérifie
  `active==1` et `ECX==controlled_actor`, puis borne `tc.time` à 50 ms.
- La position partagée est intégrée par x87 avec
  `position += velocity * tc.time * 0.001`. Le même stub neutralise
  `move_dir`, `fall_speed`, `falling` et maintient `test_col_count`, écrit
  `frame+0x14C` et pose `FRMFLAGS_UPDATE_NEEDED` avant de rejouer les neuf
  octets originaux.
- Le trainer publie uniquement le vecteur vitesse calculé depuis la caméra et
  les touches. L'ancienne intégration externe, son delta haute précision et la
  cadence artificielle 240 Hz ont été retirés ; Direct3D revient à son VSync
  normal.
- À l'arrêt, le bloc partagé est désactivé avant restauration exacte du hook.
  La page distante n'est libérée que si aucun thread ne l'exécute.
- Builds **Release** et **Debug** VS 2026 Win32/x86 : réussis sans erreur.
- `ctest -C Release` : exécuté, aucun test automatisé déclaré.
- `git diff --check` : aucune erreur d'espace ; seulement les avertissements
  LF/CRLF attendus sous Windows.
- Contrôle PE : `14C (x86)`, `PE32`, sous-système `Windows GUI`.
- Release :
  `build\vs2026-x86\Release\HDFinalAdvancedV23_3_NATIVE_NOCLIP_ALL_WEAPONS_NATIVE_VEHICLE.exe`,
  `493568` octets, SHA-256
  `67128E5BB918EF8D2D7644F8EF044A51FA0FCF7ACE4303437BBDD63322AA02DD`.

## 30. Correctif V23.4 — Bullet Track longue portée et invisibilité complète

### Diagnostic confirmé

- `GunShoot.cpp` fixe `DEFAULT_SHOOT_DIST` à `300.0f`. Le rayon de collision
  natif n'est pas volontairement limité à 300 m, mais lorsque le volume
  dynamique d'un ennemi très lointain n'est pas présent dans le test, la
  branche « aucune collision » crée seulement un trajet visuel de 300 m et ne
  renseigne ni `hit_actor` ni `hit_frm`. Corriger uniquement la direction du
  tir ne pouvait donc pas produire un impact lointain garanti.
- Le hook Bullet Track appelait en outre le RVA `0xC12D0` comme s'il s'agissait
  de `S_vector::Normalize`. Le désassemblage installé montre qu'il s'agit
  d'une routine angulaire 2D qui laisse un résultat x87 ; cet appel était
  inutile puisque le constructeur de `C_gun_shoot` normalise déjà `si.dir`.
- L'invisibilité V9 filtrait `C_player::IsEnemy(ennemi)`, ce qui bloque
  `WatchHumans`, mais `C_game_mission::EmitListen` utilise la relation inverse
  `C_enemy::IsEnemy(émetteur)`. Les bruits `LISTEN_HUMAN` du joueur pouvaient
  donc encore créer une rotation, une recherche et une voix à courte distance
  sans que l'ennemi tire.

### Corrections implémentées

- Bullet Track conserve son hook central `CreateActor` et ajoute un second
  trampoline vérifié uniquement sur la branche native « aucune collision » de
  `C_gun_shoot::Evaluate`, RVA `0x44D36`, signature exacte de 14 octets. Pour
  un tir du joueur contrôlé et une tête publiée valide, ce trampoline renseigne
  `hit_dest`, `hit_norm`, `dist` et `hit_frm`, ajoute la référence du frame,
  puis rejoint le chemin natif qui remonte le frame jusqu'à `hit_actor`.
- Si le moteur a réellement rencontré un mur, un objet ou un acteur, cette
  branche n'est jamais exécutée : la collision native reste donc prioritaire
  et le Bullet Track ne traverse pas les obstacles. Sans cible, pour un autre
  tireur ou avec la case désactivée, les 14 octets originaux sont rejoués et le
  repli normal de 300 m est conservé.
- La distance publiée est calculée jusqu'à la tête avec une marge de trajet de
  1 m afin de couvrir la différence entre l'œil radar et la bouche réelle de
  l'arme. La destination de dégâts reste la position exacte du frame de tête.
  L'ancien faux appel de normalisation a été supprimé ; le constructeur natif
  effectue seul la normalisation prévue par le jeu.
- La restauration Bullet Track traite désormais les deux sites comme une seule
  transaction : leurs octets courants doivent être soit le patch exact, soit
  l'original exact ; les deux prologues sont restaurés avant l'attente
  d'inactivité et la libération de la page distante.
- L'invisibilité installe un second trampoline réversible sur
  `C_enemy::IsEnemy` (`vtable+0xA4`, prologue installé
  `51 56 8B F1 57`). En portée locale, seul l'émetteur égal au joueur contrôlé
  retourne « non ennemi ». En portée escouade, seuls les acteurs de type
  joueur sont filtrés ; les relations ennemi/civil et ennemi/ennemi continuent
  dans la fonction native.
- Lors de la purge initiale, chaque ennemi remet aussi la fin de son vecteur
  POD `listen_list` (`enemy+0x40`) sur son début (`enemy+0x3C`) et coupe sa voix
  courante. Les sons déjà mémorisés sont donc retirés, tandis que le nouveau
  filtre réciproque empêche leur réinsertion. Les programmes d'attaque,
  poursuites et listes de surveillance continuent d'être purgés par le chemin
  V9 validé.
- Désactivation, changement de joueur, changement de portée, changement de PID
  et fermeture restaurent les deux fonctions `IsEnemy`, attendent qu'aucun
  thread n'exécute leurs pages, puis libèrent les allocations correspondantes.

### Validation statique et Release

- Les sources officielles Deluxe et le désassemblage du `hde.exe` installé ont
  confirmé `DEFAULT_SHOOT_DIST=300`, la branche installée à `0x44D36`, les
  champs `C_gun_shoot` utilisés et le prologue de `C_enemy::IsEnemy`.
- Configuration CMake VS 2026 Win32/x86 réussie avec le SDK Windows
  `10.0.26100.0` et la cible Windows 10 `10.0.19045`.
- Builds **Debug** et **Release** x86 réussis sans erreur.
- `ctest -C Release --output-on-failure` exécuté : aucun test automatisé n'est
  déclaré. Le tir réel au-delà de 300 m et l'approche silencieuse d'un ennemi
  doivent donc être confirmés en mission.
- `git diff --check` ne signale aucune erreur d'espace ; seulement les
  avertissements LF/CRLF attendus de la copie Windows.
- Contrôle PE : machine `14C (x86)`, format `PE32`, sous-système
  `Windows GUI`.
- Release :
  `build\vs2026-x86\Release\HDFinalAdvancedV23_4_LONG_RANGE_BULLETTRACK_COMPLETE_INVISIBILITY.exe`,
  `499712` octets, SHA-256
  `77AC1C0F561B8AC6F8A07FC33A508B306147F74DA53310D4835DBE7DA2693BD9`.

## 31. Correctif V23.5 — dégâts Bullet Track illimités et invisibilité totale

### Résultat du test V23.4 et causes exactes

- Le test réel a confirmé que la direction visuelle suivait bien une tête verte
  très lointaine, mais sans dégâts. Le second hook V23.4 ne s'exécutait que dans
  la branche « aucune collision ». Une collision native quelconque plus loin
  sur le rayon suffisait donc à éviter ce repli, même si elle ne correspondait
  pas à l'ennemi sélectionné.
- `C_gun_shoot::Finish()` appelle `CB_HIT` uniquement lorsque son champ
  `hit_actor` est non nul. Publier seulement `hit_frm`, `hit_dest` et `dist` ne
  garantit pas ce champ, notamment lorsque la résolution native est sautée par
  l'état réseau du projectile.
- La sonde sur la mission active a mesuré le vrai `C_enemy::IsEnemy` à
  `0x00436350`, avec le prologue complet
  `56 8B F1 57 8B 46 28`. La signature V23.4
  `51 56 8B F1 57` était fausse : le hook réciproque était refusé, puis la
  transaction restaurait aussi le filtre visuel, ce qui explique que les
  ennemis recommençaient à tirer.
- Après correction de la signature sur sept octets, un second défaut a été
  éliminé avant Release : le déplacement du `jmp rel32` était calculé depuis
  `function+7` alors que l'instruction de saut se termine à `function+5`.

### Corrections réalisées

- Le hook dégâts est déplacé au point commun post-collision de
  `C_gun_shoot::Evaluate`, RVA `0x44E4B`, signature vérifiée
  `8B 46 34 85 C0`. Il s'exécute donc après une collision réussie comme après
  le repli natif.
- Pour un tir du joueur contrôlé avec une cible verte publiée, le trampoline
  remplace transactionnellement l'ancien frame de collision, maintient son
  comptage de références, copie la position tête en direct, fixe la distance
  complète du trajet et renseigne explicitement `hit_actor` avec son `AddRef`.
  La résolution native suivante est neutralisée pour ne pas écraser cette
  cible. `Finish()` possède ainsi toujours le destinataire réel nécessaire au
  callback `CB_HIT`, quelle que soit la distance métrique.
- La sélection Bullet Track reste sans limite de mètres et sans cercle. Elle
  reste volontairement limitée aux ennemis verts, actifs, projetés à l'écran
  et disposant d'un frame tête valide.
- L'invisibilité utilise désormais les sept vrais octets installés de
  `C_enemy::IsEnemy` et un saut calculé depuis la fin réelle de `E9 rel32`.
  Le hook joueur bloque l'acquisition visuelle ; le hook ennemi bloque la
  relation inverse utilisée par `EmitListen`. La purge initiale retire en plus
  attaques, surveillance, sons mémorisés et voix en cours : aucun tir, geste,
  poursuite ou parole ne doit subsister.
- `tools/visibility_probe.ps1` affiche maintenant, pour chaque acteur, sa
  vtable, l'adresse `IsEnemy` et les seize premiers octets de la fonction afin
  qu'une éventuelle autre révision du jeu soit diagnostiquée sans hypothèse.
- Le journal ajoute une confirmation unique lors de l'installation du hook
  réciproque d'invisibilité et des deux hooks Bullet Track.

### Validation et nouvelle Release

- Configuration CMake VS 2026 Win32/x86 réussie, puis builds **Debug** et
  **Release** réussis sans erreur.
- `ctest -C Release --output-on-failure` exécuté : aucun test automatisé n'est
  déclaré ; la distance extrême et le silence ennemi restent à confirmer dans
  une mission réelle.
- `git diff --check` ne signale aucune erreur d'espace, seulement les
  avertissements LF/CRLF attendus.
- Contrôle PE : machine `14C (x86)`, format `PE32`, sous-système
  `Windows GUI`.
- Release :
  `build\vs2026-x86\Release\HDFinalAdvancedV23_5_UNLIMITED_BULLETTRACK_TOTAL_INVISIBILITY.exe`,
  `499712` octets, SHA-256
  `69789D134C5E0163D88DA66D3A1AB6348072B583505B3BC6954E179767B9F977`.

## 32. Correctif V23.6 — crash Bullet Track, ESP instantané et portée d'invisibilité

### Preuves du crash V23.5

- Le journal de test s'arrête après l'installation Bullet Track, puis détecte
  la disparition de `hde.exe`. Le journal Windows Application confirme un
  `APPCRASH`, exception `0xC0000005`, RVA fautive `hde.exe+0x939EA` le
  30 août 2026 à 18:16:52.
- Cette RVA appartient à une libération de référence exécutée plus tard par le
  jeu, et non au saut du hook. La V23.5 publiait séparément le frame tête et
  l'acteur cible depuis le thread du trainer. Un changement de cible entre ces
  écritures pouvait donc donner au projectile un acteur et un sous-frame issus
  de deux ennemis différents, puis corrompre le traitement de l'impact.
- Le journal montre aussi des pertes transitoires du pointeur mission
  (`mission=0`) pendant que le trainer prend le premier plan. Chaque perte
  provoquait la restauration de l'ESP et des hooks, puis leur réinstallation,
  ce qui explique le besoin apparent de relancer le jeu.
- Le changement du bouton radio d'invisibilité déclenchait deux restaurations
  synchrones alors que H&D était en arrière-plan. Un thread du jeu pouvait
  rester suspendu dans une petite page trampoline et bloquer l'interface.

### Corrections réalisées

- Le hook de dégâts ne consomme plus un frame partagé indépendamment. Il capture
  d'abord l'acteur ennemi sur le thread du jeu, valide `type=2`, `stay_mode!=4`,
  son frame racine et son retour `frame+0x80`, puis relit `frm_head` directement
  à `actor+0x1C8`. Après `AddRef`, toutes les lectures suivantes repartent du
  champ `hit_actor` déjà possédé par le projectile : acteur et tête restent
  donc indissociables pendant l'impact.
- Un compteur distant est incrémenté pour chaque dégâts Bullet Track réellement
  forcé. Le journal écrit désormais
  `[TEST BULLET TRACK] forced_damage_count=... delta=...`, ce qui permettra de
  distinguer un tir non sélectionné d'un impact entré dans le trampoline.
- Le dernier snapshot radar complet est conservé pendant une courte perte
  transitoire uniquement lorsque le trainer, et non le jeu, possède le premier
  plan. Les modifications de gameplay ne perdent plus immédiatement leur
  contexte lorsque H&D suspend brièvement son pointeur mission.
- L'overlay ESP accepte maintenant comme premier plan soit `hde.exe`, soit la
  fenêtre du trainer. Lors du cochage, il est rendu immédiatement au-dessus du
  client du jeu mais derrière le panneau du trainer ; une autre application le
  masque toujours.
- Un changement « joueur contrôlé / escouade entière » est mémorisé sans
  restaurer les trampolines pendant que le trainer est au premier plan. La
  nouvelle portée est installée au premier frame où H&D redevient actif, quand
  ses threads peuvent quitter proprement les anciennes pages. Aucun appel de
  focus forcé n'est effectué par ce choix.

### Validation et Release

- Configuration CMake VS 2026 Win32/x86 réussie ; builds **Debug** et
  **Release** réussis sans erreur.
- `ctest -C Release --output-on-failure` exécuté : aucun test automatisé n'est
  déclaré. Le tir réel reste à valider dans le moteur installé.
- `git diff --check` ne signale aucune erreur d'espace, uniquement les
  avertissements LF/CRLF attendus.
- Contrôle PE : machine `14C (x86)`, format `PE32`, sous-système
  `Windows GUI`.
- Release :
  `build\vs2026-x86\Release\HDFinalAdvancedV23_6_CRASH_SAFE_BULLETTRACK_LIVE_ESP_INVISIBILITY.exe`,
  `502272` octets, SHA-256
  `9549E49A5ADD536332D91313CE2C734D6D8759185C3E444353707F995DB884EE`.

## 33. Correctif V23.7 — distance exacte dans le trampoline et traçage des dégâts

### Point de départ : travail V23.6 laissé incomplet

- La Release V23.6 (`9549E49A5ADD536332D91313CE2C734D6D8759185C3E444353707F995DB884EE`)
  a été produite à 18:28, mais `src/gameplay_mods.cpp` a été modifié à 18:29
  sans recompilation : la suppression de `kTargetDistanceOffset` et le calcul
  de la distance dans le trampoline n'existaient dans aucun binaire. La V23.7
  termine et vérifie ce travail.

### Vérification des offsets sur le binaire installé

- Le désassemblage de `hde.exe` confirme la disposition utilisée par les deux
  trampolines : `00444E7B: mov [esi+98h],eax` suivi de l'incrément de
  `[eax+4]` prouve `hit_actor` à `0x98` ; `lea eax,[esi+60h]` prouve `dir` à
  `0x60` ; `[esi+88h]` est `shoot_item`. On en déduit `pos=0x54`,
  `do_smoke=0x6C`, `hit_dest=0x70`, `hit_norm=0x7C`, `dist=0x8C`,
  `shooter=0x9C`, `hit_frm=0xA0`.
- Attention pour les prochaines révisions : les sources Deluxe publiées
  décrivent `hit_actor` un dword plus tôt (`0x94`). Le binaire installé
  contient un membre supplémentaire ; seule la disposition du binaire fait foi.
- Le `xor ecx,ecx` posé avant le prologue rejoué est validé par le code natif :
  `00444E52: test ecx,ecx / je 00444E88` saute exactement la résolution native
  de `hit_actor`, et `ECX` est rechargé depuis `[esi+B8h]` à `00444E88`.

### Corrections réalisées

- La distance du trajet est calculée entièrement dans le trampoline, sur le
  thread du jeu, par la racine de la somme des carrés de `hit_dest - pos`
  (`fld`/`fsub`/`fmul st,st`/`faddp`/`fsqrt`/`fstp [esi+8Ch]`). La pile x87
  reste équilibrée (profondeur maximale 2). Le trainer ne publie plus aucune
  valeur métrique : `kTargetDistanceOffset` est supprimé.
- Chaque tir refusé par le trampoline incrémente désormais son propre compteur
  distant avant de rejoindre le chemin natif, pour les huit motifs
  `no_player`, `wrong_shooter`, `no_actor`, `wrong_type`, `dead`, `no_root`,
  `wrong_root`, `no_head`. Le journal écrit la ligne complète
  `[TEST BULLET TRACK] forced_damage_count=... delta=... reject ...`, avec les
  totaux et les deltas, au maximum une fois par 250 ms.
- Un test qui ne produit aucun dégât devient donc interprétable : soit le tir
  n'a jamais été sélectionné (`no_player`/`wrong_shooter`), soit un filtre
  précis a refusé la cible publiée, soit le trampoline a bien forcé les dégâts
  (`forced_damage_count` progresse) et le problème est ailleurs.
- La page distante passe de `0x400` à `0x600` octets pour loger les compteurs
  sans réduire l'espace du trampoline de repli : stub de repli à `0x180`
  (416 octets utilisés sur 896 disponibles), bloc de données à `0x500`
  (joueur, tête, acteur, compteur de dégâts puis les huit compteurs de rejet,
  contigus pour une seule lecture). La taille surveillée par
  `IsAnyThreadExecutingRange` suit la même constante partagée
  `kBulletTrackRemoteSize`.

### Validation et Release

- Builds **Debug** et **Release** x86 réussis, aucun avertissement.
- Contrôle PE : machine `14C (x86)`, format `PE32`, sous-système
  `Windows GUI`.
- `git diff --check` ne signale aucune erreur d'espace, uniquement les
  avertissements LF/CRLF attendus.
- Release :
  `build\vs2026-x86\Release\HDFinalAdvancedV23_7_EXACT_DISTANCE_BULLETTRACK_DAMAGE_TRACE.exe`,
  `503296` octets, SHA-256
  `2D70E3119E31441DE0EB2495FC4E981F1676DDC1BDE18293E72A00A58AA51CDE`.

### Points connus restant à traiter après ce test

- Perte transitoire `mission=00000000` observée dans le journal V23.6 alors que
  le jeu est au premier plan : les hooks sont restaurés puis réinstallés en
  boucle. Le garde-fou actuel ne couvre que le cas « trainer au premier plan ».
- Fullhands (**M**) a répondu `completion_state=1 selected=0 released=0 idle=0`
  → `status=4 granted_count=0` lors du test V23.6 : aucune arme accordée.

## 34. Correctif V23.8 — acteur vivant, ESP instantané et fin du cycle de hooks

### Résultat du test V23.7

- Les dégâts Bullet Track fonctionnent : `forced_damage_count` est passé à
  3, 12, 24 puis 33, avec tous les compteurs de rejet cible à zéro. Le
  `wrong_shooter` qui progresse correspond aux tirs des ennemis, évalués par le
  même point de collision.
- Le jeu a planté après un certain temps de tir en noclip. Dernière ligne du
  journal à 18:53:11.361 (`delta=6`), rapport WER à 18:53:12.

### Cause exacte du crash

- WER : `hde.exe`, `c0000005`, décalage `0x000D97ED`, soit `0x4D97ED`. La
  fonction commence à `0x4D95C7` et est appelée depuis `0x4D7C62`, dans le
  `malloc`/`realloc` du runtime C : c'est `__sbh_alloc_block`, l'allocateur de
  petits blocs. L'instruction fautive `mov [ecx+8],edi` détache un bloc d'une
  liste de blocs libres avec un pointeur corrompu. Le crash est donc **différé**
  et signale une corruption du tas antérieure.
- La corruption vient de `inc dword ptr [actor+4]` (l'AddRef) exécuté sur un
  acteur que le jeu avait déjà détruit. Le trainer publie `target_actor` depuis
  un snapshot radar asynchrone ; entre cette publication et le tir, l'ennemi
  peut avoir été libéré, ce qui arrivait souvent puisque le test consistait
  précisément à tuer des ennemis.
- Les gardes de champs étaient inopérants, et même trompeurs : la libération
  d'un petit bloc du CRT n'écrase que ses **huit premiers octets**. `type`
  (`+0x1C`), `stay_mode` (`+0x254`), le frame racine (`+0x28`) et `frame+0x80`
  survivent intacts, donc les quatre validations réussissaient sur un objet
  mort, puis l'incrément tombait exactement sur le lien de liste libre situé
  en `+4` — le champ déréférencé plus tard par `__sbh_alloc_block`.

### Corrections réalisées

- Avant toute lecture de champ et avant toute prise de référence, le trampoline
  vérifie **sur le thread du jeu** que l'acteur publié appartient encore au
  vecteur d'acteurs vivants de la mission (`mission+0x68`, `begin`/`end`), là
  où la liste est cohérente. Le parcours est borné à `0x4000` octets pour
  qu'un vecteur incohérent ne puisse jamais boucler. Le trainer publie
  désormais aussi le pointeur de mission.
- La référence est prise **une seule fois par projectile** : si `hit_actor` est
  déjà notre acteur, aucun second AddRef n'est effectué ; s'il appartient à un
  autre destinataire, le trampoline ne touche à rien. `Evaluate` peut ainsi
  s'exécuter plusieurs fois sur le même tir sans déséquilibrer le comptage.
- Quatre nouveaux motifs de rejet sont journalisés : `no_mission`,
  `bad_vector`, `stale_actor` et `busy_actor`.
- ESP : `EnsureInitialized` est désormais idempotent par étape. L'ancienne
  version, après un `CreateDevice` refusé, recréait la fenêtre **et** l'objet
  `Direct3D9` à chaque image, en fuyant les précédents. C'est ce qui laissait
  l'ESP mort tant que H&D détenait l'adaptateur, jusqu'à ce que l'utilisateur
  sorte du jeu et revienne.
- ESP : `TestCooperativeLevel` est interrogé à chaque image avant le rendu.
  `D3DERR_DEVICELOST` fait patienter, `D3DERR_DEVICENOTRESET` déclenche un
  `Reset` immédiat. La reprise ne dépend plus d'un changement de taille de
  client ni d'un `Present` en échec.
- ESP : la fenêtre est toujours insérée à `HWND_TOPMOST`. La V23.6 la plaçait
  derrière la fenêtre du trainer lorsque celle-ci avait le premier plan, ce qui
  **retire** `WS_EX_TOPMOST` (règle Win32) et laissait l'ESP sous le jeu.
- ESP : une ligne `[TEST ESP]` est écrite à chaque changement d'état seulement
  (`visible`, `hidden` + raison, `waiting`, `recovered`, `failed`), jamais par
  image.
- Pertes transitoires `mission=00000000` : le dernier snapshot complet est
  conservé pendant 2 s quelle que soit la fenêtre active, ce qui met fin au
  cycle restauration/réinstallation des hooks observé six fois dans le test
  V23.7. Ce snapshot est marqué `from_cache` et **ni le Bullet Track ni
  l'aimbot ne sélectionnent de cible dessus** : aucune adresse d'acteur
  potentiellement périmée n'est publiée pendant ces trous.

### Validation et Release

- Builds **Debug** et **Release** x86 réussis, aucun avertissement.
- Contrôle PE : machine `14C (x86)`, format `PE32`, sous-système
  `Windows GUI`.
- `git diff --check` ne signale aucune erreur d'espace, uniquement les
  avertissements LF/CRLF attendus.
- Release :
  `build\vs2026-x86\Release\HDFinalAdvancedV23_8_LIVE_ACTOR_BULLETTRACK_INSTANT_ESP.exe`,
  `505856` octets, SHA-256
  `860516F638986499BCF3277B8A253AB2D2CACDE20959C2169BE9EA45AE300FF5`.

### Reste ouvert

- Fullhands (**M**) : `completion_state=1 selected=0` → `granted_count=0`. Non
  traité par cette version.

## 35. Correctif V23.9 — résolveur natif pour les dégâts et ESP en GDI

### Ce que le test V23.8 a prouvé

- **Cycle de hooks : réglé.** Une seule installation Bullet Track (contre 6 en
  V23.7), une seule activation noclip, 3 installations d'invisibilité
  correspondant aux 3 vrais changements de portée, et **aucune** ligne
  `mission=00000000` pendant toute la partie (les 4 restantes sont postérieures
  à la mort du jeu).
- **ESP : jamais affiché.** Aucune ligne `ready:` ni `visible:` de toute la
  session ; l'état est resté `waiting: overlay CreateDevice refused (the game
  owns the adapter)` pendant les 42 secondes de jeu. Les 3 lignes `off:` sont
  antérieures au démarrage du jeu, donc la case est bien restée cochée.
- **Crash : toujours présent**, 2 s après le dernier dégât forcé.

### Cause réelle du crash, reclassée

- WER V23.8 : `hde.exe+0x94C89` → `0x494C89`. La fonction commence à
  `0x494C70` ; c'est un destructeur atteint par vtable qui libère un
  `vector<C_actor*>` situé en `this+0x0C`, puis appelle `free`.
- La séquence fautive est **exactement** celle du crash V23.5 à `0x4939EA` :
  `mov eax,[ecx+4] / dec eax / mov [ecx+4],eax` — la libération d'un
  `C_actor` sur un pointeur invalide. Le crash V23.7 dans `__sbh_alloc_block`
  était le symptôme différé de la même corruption.
- La validation d'acteur vivant de la V23.8 fonctionne (`stale_actor=0`,
  `bad_vector=0`, `no_mission=0`, `busy_actor=0` sur toute la session), donc
  le pointeur périmé ne venait pas du trainer. C'est **l'écriture manuelle de
  `hit_actor` avec son `inc [actor+4]`** qui laissait une référence pendante :
  elle court-circuitait le résolveur natif `00444E52..00444E85`, lequel ne se
  contente pas d'incrémenter le compteur mais remonte le frame via
  `call [eax+5Ch]` puis `call [ecx+80h]` avant de renseigner `hit_actor`.

### Corrections réalisées

- Le trampoline **n'écrit plus jamais `hit_actor`** et ne fabrique plus
  d'`AddRef`. Il substitue le frame tête à la géométrie touchée, puis place ce
  frame dans `ECX` avant de rejoindre le code natif : le résolveur du jeu le
  remonte jusqu'à son acteur et prend la référence lui-même, exactement comme
  pour une collision réelle. Le `xor ecx,ecx` de la V23.5 est supprimé.
- Le frame tête traverse `popad` par un emplacement de travail écrit et relu
  par la même exécution du stub, sur le thread du jeu.
- Le trampoline n'agit que si le projectile n'a pas encore de destinataire
  (`hit_actor == 0`), sinon il compte `busy_actor` et ne touche à rien.
- L'acteur validé contre la liste vivante est conservé dans `EBX`, préservé par
  convention d'appel à travers les `AddRef`/`Release` virtuels, au lieu d'être
  relu depuis `hit_actor`.
- Pour un tir réseau (`[esi+0x34] != 0`), le résolveur natif est sauté comme
  dans le jeu d'origine : plus rien n'est forcé dans ce cas.
- **ESP entièrement réécrit en GDI.** Plus aucun périphérique Direct3D 9 :
  fenêtre calque à clé de transparence, DIB 32 bits, tracé au crayon GDI puis
  `BitBlt`. La création du device ne peut donc plus être refusée par le pilote.
  Le noir pur est la clé de transparence, aucun tracé ne l'utilise. L'alpha
  demandé est replié dans la couleur, avec un plancher qui empêche une ligne
  atténuée de devenir transparente.

### Validation et Release

- Builds **Debug** et **Release** x86 réussis, aucun avertissement.
- Contrôle PE : machine `14C (x86)`, format `PE32`, sous-système
  `Windows GUI`.
- `git diff --check` ne signale aucune erreur d'espace.
- Release :
  `build\vs2026-x86\Release\HDFinalAdvancedV23_9_NATIVE_RESOLVER_GDI_ESP.exe`,
  `505344` octets, SHA-256
  `B863BF4A1F9BFADA531010E1345A19954ACF5A4E8B4487665CD1D9F6C906A4CC`.

### Reste ouvert

- Fullhands (**M**) : `completion_state=1 selected=0` → `granted_count=0`.

## 36. Correctif V23.10 — raccourci V facultatif, et troisième adresse de crash

### Résultat du test V23.9

- **ESP : réglé.** `[TEST ESP] visible: rendering over the game` apparaît pour
  la première fois et reste stable. Le passage en GDI supprime bien la
  dépendance à l'adaptateur Direct3D.
- **Bullet Track : fonctionne toujours** avec le résolveur natif —
  `forced_damage_count` progresse et tous les compteurs de rejet cible restent
  à zéro.
- **Crash : toujours là.**

### Le crash n'est pas l'écriture de hit_actor

- La V23.8 plantait à `0x494C89` (`mov [ecx+4],eax`), la V23.9 plante à
  `0x494C85` (`mov eax,[ecx+4]`) : **la même boucle, à quatre octets près**,
  alors que la V23.9 n'écrit plus du tout `hit_actor`. L'hypothèse de
  l'écriture manuelle est donc réfutée.
- L'objet détruit par cette boucle est alloué en `0x495D12` (`malloc(0x24)`
  puis constructeur `0x494C40`) depuis une routine de **mise en page de
  texte**. Le vecteur en `this+0x0C` contient des ressources de rendu, pas des
  acteurs : la séquence `dec [x+4]` puis `call [vtable[0]](1)` est le modèle
  générique de libération des objets à compteur de références du moteur.
- Session suivante : nouvelle adresse encore différente, `0x447B81`
  (`mov ecx,[eax]` avec `eax = [ebx+0x54]`), avec seulement
  `forced_damage_count=2`.
- Trois structures sans rapport, trois déréférencements de pointeurs qui
  auraient dû être valides : ce sont des victimes. Il y a une **corruption de
  tas** dans `hde.exe` et le site du crash est arbitraire.
- Point à retenir pour la suite : le trampoline de repli est posé sur un point
  commun de `C_gun_shoot::Evaluate`, donc il s'exécute **à chaque tir du jeu**,
  y compris ceux des ennemis (`wrong_shooter=20` sur une seule session). Le
  nombre de dégâts forcés ne mesure pas son activité réelle.

### Correction réalisée

- Le raccourci **V** devient facultatif, via une case « Raccourci touche V »
  **décochée par défaut**. Jusqu'ici la touche V basculait le noclip dès que la
  fenêtre du jeu était active, sans aucune condition sur la case du noclip, et
  le filtre clavier l'avalait **inconditionnellement** : le jeu ne recevait
  jamais V, et une pression involontaire activait le noclip.
- Tant que la case est décochée, V est intégralement laissée au jeu et ne peut
  plus rien basculer. Le journal écrit une ligne
  `Noclip: V hotkey enabled=...` à chaque changement d'état.

### Validation et Release

- Builds **Debug** et **Release** x86 réussis, aucun avertissement.
- Contrôle PE : machine `14C (x86)`, format `PE32`, sous-système
  `Windows GUI`.
- `git diff --check` ne signale aucune erreur d'espace.

### Essai d'isolation demandé

- Décocher **Bullet Track et aimbot** : le trainer désinstalle alors les deux
  trampolines de tir. Voler en noclip, tirer, tuer. Si le jeu plante quand
  même, le Bullet Track est hors de cause ; sinon la corruption vient bien de
  ce chemin.

- Release :
  `build\vs2026-x86\Release\HDFinalAdvancedV23_10_OPTIONAL_V_HOTKEY.exe`,
  `505344` octets, SHA-256
  `CC89039A8001F66FDD8CAA293504F5884196A7859716868C066D96ECC8F4E74B`.

## 37. Correctif V23.11 — ne plus substituer hit_frm

### Le test d'isolation a tranché

- Bullet Track décoché pendant environ trois minutes et demie : vol en noclip,
  tirs, ennemis tués, **aucun plantage**, et aucune ligne `[TEST BULLET TRACK]`
  dans le journal (les compteurs n'existent pas tant que la page distante n'est
  pas allouée).
- `20:05:29.384` : `Bullet Track: unlimited damage hooks installed`, compteurs
  repartis de zéro.
- `20:05:34.127` → `20:05:36.051` : neuf dégâts forcés en deux secondes.
- `20:05:37.68` : le jeu disparaît. WER `c0000005` à `hde.exe+0x939EA`, soit
  **`0x4939EA` — exactement l'adresse du crash V23.5**.

Le Bullet Track est donc bien la cause, comme l'utilisateur le pensait.

### Mécanisme exact

- `0x4939E0` est le destructeur d'une instance de son : compteur de références
  en `+4`, acteur source en `+0x60` (pris avec `inc [eax+4]` dans le
  constructeur `0x493870`), objet I3D en `+0x58`, index de ligne en `+0x50`
  dans la table de 0x30 octets recopiée en `0x494C07`. Le crash est la
  libération de l'acteur en `+0x60`.
- `C_gun_shoot::Finish` a été désassemblé : après `cbProc(CB_HIT)` en
  `0x4456C5`, il reprend `hit_frm` en `0x4456C8`, appelle `GetType`
  (`[vtable+8]`), puis selon le type `GetCollisionMaterial`
  (`[vtable+0xE4]` ou `[vtable+0xE8]`), et enfin
  `mat_table->ItemS(TAB_S12_SND_GUNSHOOT, mat_id, 12)` en `0x44574F`. Le
  pointeur renvoyé est **déréférencé immédiatement** en `0x445752`, puis passé
  à `PlaySound` avec `hit_dest`.
- En substituant `hit_frm` par le frame de tête de l'ennemi, on donnait à ce
  chemin un identifiant de matériau de peau de personnage, hors plage pour
  cette table. `ItemS` lisait donc au-delà de la table, et l'instance de son
  était construite à partir de données arbitraires — l'objet même dont le
  destructeur plante une à deux secondes plus tard.
- Cela explique aussi la variabilité des adresses de crash entre les versions
  (`0x4D97ED`, `0x494C85/89`, `0x447B81`) : la lecture hors table produit un
  dégât différent à chaque fois.

### Correction réalisée

- Le trampoline **n'écrit plus `hit_frm`** et ne fait plus aucun `AddRef` ni
  `Release` de frame. Si le rayon natif a touché de la géométrie, son matériau
  réel est conservé ; sinon `hit_frm` reste nul et `Finish` saute tout le bloc
  matériau en `0x4456D4`.
- Sont conservés : la validation de l'acteur contre la liste vivante de la
  mission, `hit_dest` sur la tête, `hit_norm`, la distance exacte calculée dans
  le stub, `do_smoke = false`, et surtout `ECX` = frame tête remis au résolveur
  natif, qui reste le seul à écrire `hit_actor`.
- Conséquence assumée : `S_CB_hit::sub_frame` n'est plus le frame de tête, donc
  la zone d'impact n'est plus signalée comme tête. Les dégâts s'appliquent
  toujours, par le destinataire que le moteur a résolu lui-même.

### Validation et Release

- Builds **Debug** et **Release** x86 réussis, aucun avertissement.
- Release :
  `build\vs2026-x86\Release\HDFinalAdvancedV23_11_NO_HITFRM_SUBSTITUTION.exe`,
  `504832` octets, SHA-256
  `2674044EFB368EFA60D68DB05C466D97FB7F4E1B16B14FBB047A496F2B546E7D`.

## 38. Correctif V23.12 — ne jamais vider la liste de programmes d'un ennemi

### Résultat du test V23.11

- Le correctif `hit_frm` a fait effet : **78 dégâts forcés** encaissés sur une
  longue session, contre 9 avant le plantage de la V23.10. La lecture hors
  table de matériaux était donc bien une cause réelle.
- Le jeu a néanmoins fini par sortir à `21:07:44.7`. WER : `c0000005` à
  `hde.exe+0xC284`, soit **`0x40C284`** — encore une adresse nouvelle, mais
  cette fois dans la mise à jour de l'IA ennemie.

### Deuxième cause, distincte de la première

- `0x40C262` : `mov eax,[esi+0x288]` (début du vecteur de programmes), puis
  `0x40C276` : `mov eax,[eax]` (programs[0]), puis `0x40C284` :
  `mov edi,[eax+0x44]` — le crash.
- Ce chemin **ne vérifie ni la nullité ni la vacuité** du vecteur. Le chemin
  voisin le fait pourtant : `0x40C19E` teste le pointeur de début et
  `0x40C1AE` teste `end - begin`. Le choix entre les deux se fait en
  `0x40C192` sur l'octet `[esi+0x295]` ; quand il est nul, l'IA saute
  directement au chemin non protégé.
- La purge d'invisibilité supprime les programmes d'attaque visant un joueur
  protégé. Lorsqu'elle supprimait **le dernier** programme, le vecteur devenait
  vide et la mise à jour suivante de cet ennemi lisait au-delà de
  l'allocation, une à deux secondes plus tard.
- C'est ce qui explique le lien apparent avec le Bullet Track sans qu'il en
  soit la cause : un ennemi rendu invisible ne crée jamais de programme
  d'attaque, donc la purge ne supprime rien. Forcer des dégâts à distance le
  fait réagir malgré l'invisibilité, la purge supprime ce programme, et si
  c'était le seul, le vecteur se vide.

### Correction réalisée

- Avant chaque suppression, le trampoline vérifie
  `[esi+0x28C] - [esi+0x288] >= 8`, c'est-à-dire au moins deux entrées. La
  suppression est abandonnée quand il n'en reste qu'une, pour la commande
  d'attaque comme pour le déplacement apparié.
- Conséquence assumée : si le programme d'attaque est le seul de l'ennemi, il
  est conservé et cet ennemi peut poursuivre sa réaction. Un moteur vivant vaut
  mieux qu'une invisibilité parfaite.
- L'intitulé de la case du raccourci devient « Raccourci clavier V pour le
  noclip » pour lever l'ambiguïté avec la case du noclip elle-même.

### Validation et Release

- Builds **Debug** et **Release** x86 réussis, aucun avertissement.
- Release :
  `build\vs2026-x86\Release\HDFinalAdvancedV23_12_KEEP_LAST_ENEMY_PROGRAM.exe`,
  `505344` octets, SHA-256
  `F24B83FB7AD8ABDE98C93E315745825CED8117829F35BFA555BBCC1CEF9040CC`.

## 39. Correctif V23.13 — la distance de vol revient au moteur

### Résultat du test V23.12

- Crash à `21:51:45`, soit 1,1 s après le dernier dégât forcé
  (`forced_damage_count=30` à `21:51:43.889`). WER : `c0000005` à
  `hde.exe+0xD97ED`, soit `0x4D97ED`, de nouveau dans `__sbh_alloc_block`.
- L'utilisateur précise la condition exacte : l'ennemi visé est **très loin,
  dans une zone qui n'est pas encore visible**.

### Ce que `dist` est réellement

- `C_gun_shoot::Tick` a été désassemblé en `0x4455BF` :

  ```
  fild [ecx]                ; delta de temps
  fmul [4F6558]             ; constante
  fmul [esi+94h]            ; speed
  fstp [ebp-4]              ; trajet parcouru cette frame
  fld  [esi+8Ch]            ; dist
  fsub [ebp-4]
  fst  [esi+8Ch]            ; dist -= trajet
  fcomp [4F6554]            ; <= 0 ?
  mov byte [esi+0A4h],1     ; -> le tir se termine
  ```

- `dist` n'est donc **pas** un paramètre de dégâts : c'est la **longueur de vol
  restante**. Le projectile vit `dist / speed` secondes, et sa traînée est
  alimentée à chaque tick en `0x445602`.
- En publiant la distance réelle jusqu'à une tête située bien au-delà des
  `DEFAULT_SHOOT_DIST = 300 m` du moteur, le projectile et sa traînée
  restaient vivants très au-delà de ce pour quoi le moteur les dimensionne.
  Le délai observé entre le tir et le plantage — une à deux secondes —
  correspond exactement à ce temps de vol supplémentaire.
- `Finish()` n'a besoin que de `hit_actor`, renseigné par le résolveur natif.
  La distance native, qu'elle vienne d'une collision ou du repli de 300 m,
  suffit donc et reste dans le contrat du moteur.

### Corrections réalisées

- Le trampoline **n'écrit plus `dist`**, et tout le calcul x87 correspondant
  est supprimé du stub. Il ne reste que `do_smoke = false`, `hit_dest`,
  `hit_norm` et `ECX` = frame tête remis au résolveur natif.
- Conséquence visible : la traînée s'arrête à la distance native alors que les
  dégâts s'appliquent bien à la cible lointaine.
- Les deux cases du noclip sont **fusionnées en une seule**, « Noclip spatial
  (touche V) ». La cocher active le déplacement libre et arme le raccourci V ;
  V bascule ensuite le vol sans se désarmer, donc elle peut aussi le rallumer.
  La décocher rend intégralement la touche V au jeu.

### Validation et Release

- Builds **Debug** et **Release** x86 réussis, aucun avertissement.
- Release :
  `build\vs2026-x86\Release\HDFinalAdvancedV23_13_NATIVE_FLIGHT_DISTANCE.exe`,
  `504832` octets, SHA-256
  `239B3462B04E49D11EFA6A970EA93241B3D3533B0F8F0E3E11B950E7A2F956C6`.

## 40. Correctif V23.14 — Bullet Track réduit à son noyau

### L'invisibilité est innocentée

- Test demandé exécuté : Bullet Track et noclip cochés, invisibilité décochée.
  Le journal ne contient **aucune** ligne
  `Enemy invisibility: reciprocal IsEnemy hook installed` — l'essai est donc
  valide — et le jeu a **quand même** planté à `22:08:35`, environ 1,5 s après
  le dernier dégât forcé (`forced_damage_count=24` à `22:08:34.335`).
- L'hypothèse « purge d'invisibilité contre pointeur de programme mis en
  cache » est donc écartée. Les deux crashes dans la mise à jour d'IA
  (`0x40C284`, `0x40D031`) étaient des victimes, pas la cause.

### Adresse du crash

- WER : `c0000005` à `hde.exe+0x939EA`, soit `0x4939EA` — le destructeur
  d'instance de son, sur la libération de l'acteur référencé en `this+0x60`.
  L'acteur a donc disparu alors qu'un son le référençait encore.

### Correction réalisée

- Le trampoline est ramené à son noyau irréductible. Il ne reste que :
  validation de l'acteur contre la liste vivante de la mission, contrôle que le
  projectile n'a pas encore de destinataire, compteur de diagnostic, et remise
  du frame tête au résolveur natif via `ECX`.
- Sont supprimées les trois dernières écritures : `do_smoke`, `hit_dest` et
  `hit_norm`. La position et la normale d'impact restent celles du rayon natif.
  `hit_dest` alimente `mission.PlaySound` dans `Finish` et
  `S_CB_hit::hit_pos` ; or l'objet qui plante en `0x4939EA` est précisément une
  instance de son.
- Le stub n'impose donc plus **aucune** valeur au projectile. Il se contente de
  désigner la cible que le moteur doit résoudre lui-même.

### Validation et Release

- Builds **Debug** et **Release** x86 réussis, aucun avertissement.
- Release :
  `build\vs2026-x86\Release\HDFinalAdvancedV23_14_MINIMAL_BULLETTRACK.exe`,
  `504320` octets, SHA-256
  `40D770C9814D5098405C541D4B661989136BABC53FD73EAA84854E484CA36045`.

### Si le crash persiste

- Il ne resterait alors qu'une seule action de notre part : désigner un acteur
  lointain comme destinataire. La conclusion serait que le moteur ne sait pas
  traiter la mort d'un ennemi dans un secteur non chargé, son propre rayon
  s'arrêtant à 300 m. Le remède serait alors une limite de portée réglable,
  contraire à la demande initiale mais seule garantie de stabilité.

### Résultat du test V23.14 : validé

- Test réel confirmé par l'utilisateur : vol en noclip, Bullet Track actif,
  tirs sur ennemis lointains dans une zone non chargée — **aucun plantage**.
- La cause du crash était donc bien une des trois dernières valeurs que le
  trampoline imposait au projectile (`do_smoke`, `hit_dest`, `hit_norm`), et
  non la désignation de la cible elle-même : le résolveur natif continue de
  recevoir le frame tête et d'appliquer les dégâts.
- Les trois ayant été retirées ensemble, laquelle exactement reste indéterminée.
  `hit_dest` est la plus probable : `C_gun_shoot::Finish` la passe à
  `mission.PlaySound` et à `S_CB_hit::hit_pos`, et l'objet qui plantait en
  `0x4939EA` est précisément une instance de son. Pour trancher, il suffirait
  de réintroduire `hit_dest` seule et de refaire le même tir.
- Conséquence visible acceptée : la position et la normale d'impact sont celles
  du rayon natif, donc la traînée et le bruit d'impact ne se produisent pas sur
  la cible lointaine. Les dégâts, eux, s'appliquent bien.

## 41. Correctif V23.15 — Fullhands : la colonne des noms est une colonne texte

### Sonde en lecture seule sur le jeu en cours

- Les accesseurs de la table d'objets ne sont pas dans `hde.exe` : la table
  vivante est en `0x023758C0`, sa vtable en `0x00AC8174`. Ces adresses
  appartiennent à **`itabler2.dll`**, relogée à la base `0x00AC0000`
  (base préférée `0x10000000`, taille `0xC000`).
- Désassemblage d'`itabler2.dll` :
  - `vtable+0x20` = `GetColumnType(this, colonne)`, `ret 8`.
  - `vtable+0x28` = accesseur octet, `base + ligne`, `ret 0Ch`.
  - `vtable+0x30` = accesseur 4 octets, `base + ligne * 4`, `ret 0Ch`.
  - `vtable+0x48` = accesseur texte, `base + ligne * pas`, quatre arguments,
    `ret 10h`.
- Descripteurs de colonnes en `[table+0x20]`, 8 octets chacun : type en `+4`,
  taille d'entrée en `+5`, nombre de lignes en `+6`. La table installée
  déclare **55 colonnes**, 1024 lignes.
- Colonne 0 : type 5 (texte), **32 octets par entrée**. Colonne 3 : type 2
  (entier 32 bits). Lecture directe des données : ligne 1 `Binoculars` type 12,
  ligne 13 `M1A1 Carbine` type 1, ligne 24 `Panzerfaust` type 7, ligne 30
  `Free hands` type 4, ligne 34 `Tank missile` type 0. Les constantes du
  trainer (`kUniformItemType=10`, `kKeyItemType=14`, `kCameraItemType=9`,
  familles d'armes `{1,2,3,5,6,7,8,11,13}`) correspondent toutes.

### Cause exacte de `selected=0`

- Le type d'objet était lu correctement : colonne 3, accesseur `vtable+0x30`,
  qui est bien l'accesseur 4 octets.
- Le **nom** était lu avec le même accesseur `vtable+0x30`, donc à
  `base + ligne * 4` au lieu de `base + ligne * 32`. Pour presque toutes les
  lignes, cette adresse tombe dans le remplissage à zéro d'une entrée
  précédente : le filtre « nom vide » rejetait alors chaque ligne, d'où
  `selected=0` et `granted_count=0`.

### Correction réalisée

- Ajout de `emit_string_query`, qui appelle `vtable+0x48` avec le pas en
  quatrième argument, et utilisation de cet accesseur pour la colonne 0.
- Le pas n'est pas codé en dur : il est relu dans le descripteur de colonne
  avant la construction du trampoline, avec vérification que la colonne est
  bien de type texte et que le pas est compris entre 4 et 64 octets. Une table
  modifiée par un autre mod sera donc suivie automatiquement, ou refusée
  proprement.
- Le journal indique désormais `Fullhands: name column type=5 stride=32 bytes.`

### Validation et Release

- Builds **Debug** et **Release** x86 réussis, aucun avertissement.
- Release :
  `build\vs2026-x86\Release\HDFinalAdvancedV23_15_FULLHANDS_STRING_COLUMN.exe`.
  `504832` octets, SHA-256
  `C26D26FF239B0588C5AB625F266B9DF3AA97E50BF1A27328A0E42741F7ABDC99`.

## 42. V23.16 — vitesse Noclip proportionnelle et sonde du catalogue Fullhands

### Vitesse du Noclip

- La montée était linéaire à 12 m/s par seconde maintenue : atteindre 80 m/s
  depuis 6 m/s demandait plus de six secondes d'appui sur F8.
- La rampe devient proportionnelle : `30 m/s²` plus `120 %` de la vitesse
  courante par seconde. À 12 m/s la montée est d'environ 44 m/s par seconde,
  à 60 m/s d'environ 100 : le palier haut est atteint en une à deux secondes,
  et la descente par F9 est tout aussi rapide.
- La vitesse par défaut passe de 6 à 12 m/s.

### Sonde du catalogue Fullhands

- Le test V23.15 a montré que la lecture de la colonne des noms est réparée
  (`Fullhands: name column type=5 stride=32 bytes.`), mais que la sélection
  choisit encore zéro ligne (`selected=0`, `granted_count=0`).
- Les 93 objets constatés dans l'inventaire ne viennent donc pas de notre
  sélection : ils proviennent du cheat interne du jeu, déclenché au passage.
  Le journal le prouve en mesurant l'inventaire avant chaque tentative,
  `items=5` puis `items=93`.
- Le trampoline enregistre désormais, pour les 24 premières lignes du
  catalogue, le type et les quatre premiers caractères du nom **tels que la
  requête les renvoie**, dans une fenêtre distante en `0x1600`. Le journal les
  restitue sous la forme
  `[TEST FULLHANDS] row=N type=T name4=XXXXXXXX 'abcd'`.
- Comparées au relevé en lecture seule de la table
  (ligne 1 `Binoculars` type 12, ligne 13 `M1A1 Carbine` type 1, ligne 24
  `Panzerfaust` type 7), ces lignes diront immédiatement si le rejet vient de
  la requête ou des filtres de quota.
- Aucune décision n'est modifiée par cet enregistrement.

### Séries M / P / O demandées

- L'objectif est de répartir le catalogue en trois séries attribuées à trois
  touches distinctes, ces touches n'ouvrant jamais l'inventaire.
- Cela suppose que **notre** distribution fonctionne, puisqu'il faut choisir
  quelles lignes sont accordées. Tant que la sélection renvoie zéro et que
  c'est le cheat interne qui distribue tout, aucune répartition n'est
  possible. La sonde ci-dessus est donc le préalable.

### Release

- Builds Debug et Release x86 réussis, aucun avertissement.
- `build\vs2026-x86\Release\HDFinalAdvancedV23_16_FAST_NOCLIP_FULLHANDS_PROBE.exe`,
  `505344` octets, SHA-256
  `E7C017208AF8EA89EDB12C8BCC8FEEB21C9F0DED0FABA869C4707942BE8CD727`.

## 43. V23.17 — Fullhands : cause de `selected=0` et séries sur M

### Ce que la sonde a montré

- Les requêtes du catalogue sont exactes. Le journal V23.16 rend
  `row=1 type=12 'Bino'`, `row=13 type=1 'M1A1'`, `row=23 type=1 'ZB26'`,
  identiques au relevé en lecture seule de la table. La correction de la
  colonne des noms de la V23.15 est donc bien effective.
- Le rejet ne venait ni de la requête, ni des filtres.

### Cause réelle de `selected=0`

- Le déclencheur est une **séquence de touches** : `ProcessCheat`, et donc
  notre trampoline, s'exécute **une fois par touche** de la séquence.
- La première exécution accorde les objets et remplit les quotas ; toutes les
  suivantes ne sélectionnent légitimement plus rien.
- Le stub terminait par `mov [selected_count],ebp`, donc la dernière exécution
  **écrasait le total réel par son propre zéro**. Le trainer voyait
  `selected=0`, refusait, et ré-armait M — alors que l'inventaire avait bel et
  bien été rempli. C'est aussi ce qui permettait de réappuyer sur M et
  d'accumuler des doublons.
- Corrigé en `add [selected_count],ebp` : le compte s'accumule sur les
  exécutions d'un même déclenchement, la page étant réallouée à zéro pour
  chaque tentative.

### Séries sur la touche M

- Le trampoline numérote les lignes éligibles et n'accorde que celles dont le
  rang modulo 3 correspond à la série publiée par le trainer pour cet appui
  (`eligible_index`, `series_slot`).
- Le compteur de série avance après chaque distribution acceptée et boucle :
  1ᵉʳ appui série 1, 2ᵉ série 2, 3ᵉ série 3, 4ᵉ retour à la série 1. Il repart
  à la première série à chaque nouveau `hde.exe`.
- **M** reste armée après un succès, ce qui est nécessaire au cycle, et elle
  n'ouvre toujours pas l'inventaire : elle est interceptée avant le jeu.
- Le journal indique `Fullhands: granting series N of 3.` puis
  `Fullhands: series N granted, next press will grant series M.`
- Effet attendu sur la lenteur signalée : environ un tiers des objets à la
  fois, donc bien moins de modèles 3D à charger à l'ouverture de l'inventaire.

### Release

- Builds Debug et Release x86 réussis, aucun avertissement.
- `build\vs2026-x86\Release\HDFinalAdvancedV23_17_FULLHANDS_SERIES.exe`,
  `505856` octets, SHA-256
  `E0EE2C0A4E4993E426FCF3A7F0ACF64038A125AE8F8CBAC49D55453528598AC7`.

## 44. V23.18 — Fullhands : compteur de sélection continu entre les exécutions

### Ce que le test V23.17 a montré

- `selected=43` : l'accumulation a bien débloqué le compte, qui valait zéro
  depuis le début.
- Mais la distribution était encore refusée :
  `Fullhands: invalid/duplicate catalog row 0 at index 31.`
- **Aucun rapport de plantage Windows n'a été émis pendant ce test.** Ce que
  l'utilisateur décrit comme un plantage est le gel de l'écran d'inventaire,
  pas une sortie du jeu.

### Cause

- `selected_count` était devenu un cumul, alors que la liste des lignes est
  écrite en `[rows + ebp*4]` avec un `ebp` remis à zéro **à chaque exécution**
  du stub. Or `ProcessCheat` est appelé une fois par touche de la séquence de
  déclenchement.
- La première exécution écrivait les entrées 0 à 30, la suivante réécrivait
  par-dessus à partir de l'entrée 0, et la queue de la liste — les entrées 31
  et suivantes — restait à zéro. Le trainer relisait donc une ligne nulle à
  l'index 31 et refusait par sécurité.
- Le refus ré-armait M, l'utilisateur réappuyait, et l'inventaire enflait
  (`items=5` puis `45`) sans que rien ne soit jamais validé : d'où le gel de
  plus en plus long à l'ouverture.

### Correction

- Le stub reprend le total en cours au lieu de repartir de zéro :
  `mov ebp,[selected_count]` à l'entrée, `mov [selected_count],ebp` à la
  sortie. Les index de la liste de lignes sont donc continus sur toutes les
  exécutions d'un même déclenchement, et le total reste exact.
- Les quotas et `eligible_index` persistaient déjà dans la page distante ; la
  répartition en séries reste donc cohérente d'une exécution à l'autre.

### Release

- Builds Debug et Release x86 réussis, aucun avertissement.
- `build\vs2026-x86\Release\HDFinalAdvancedV23_18_FULLHANDS_SERIES_STABLE.exe`,
  `505856` octets, SHA-256
  `81D44896E8665F6B7B92561D64825755FC09E83C78F8F4B8E845488E99CEB8BA`.

## 45. V23.19 — Fullhands validé, et nombre de séries réglable

### Le test V23.18 valide enfin la distribution

```
Fullhands: completion_state=1 selected=43
Fullhands: curated selection total=43 limit=123 weapons=112 utility=6
           essential=10 stack_before=0019FDF4 stack_after=0019FDF4 balanced=1.
Fullhands: native catalog=43, inventory before=5 native_after=45 final=45
           present_after_native=43 verified=43/43.
[TEST FULLHANDS #1] RESULT status=1 granted_count=43.
Fullhands: series 1 granted, next press will grant series 2.
```

- `granted_count=43`, pile équilibrée, 43 objets vérifiés présents, et le
  cycle avance : la fonction est opérationnelle.
- **Aucun rapport de plantage Windows n'a été émis**, ni pendant ce test ni
  pendant le précédent. Le jeu était toujours vivant plusieurs minutes après.
  Ce qui est décrit comme un plantage est le gel de l'écran d'inventaire.

### Pourquoi le gel demeure

- Trois séries donnaient environ 43 objets par appui, l'inventaire passant de
  5 à 45. Le moteur recharge un modèle 3D par objet détenu à chaque ouverture
  de l'écran, et le cache persistant reste volontairement désactivé depuis la
  V19 pour ne pas conserver de pointeurs de modèles.
- Le gel est donc proportionnel au nombre d'objets détenus : c'est la seule
  variable sur laquelle on puisse agir sans réintroduire le cache.

### Correction

- Le nombre de séries devient réglable dans l'interface, de 2 à 12, par défaut
  **6** : environ 21 objets par appui au lieu de 43. La valeur est bornée puis
  intégrée en littéral dans le trampoline, reconstruit à chaque appui.
- Le bouton devient « Armes et équipement, une série par appui [M] », avec le
  curseur « Séries (appuis pour tout obtenir) » et un rappel explicite du
  compromis.

### Release

- Builds Debug et Release x86 réussis, aucun avertissement.
- `build\vs2026-x86\Release\HDFinalAdvancedV23_19_TUNABLE_SERIES.exe`,
  `506368` octets, SHA-256
  `A7F60FBE551CD37080B8CFDB174872608A1A798C91E50679389722F27D9E3664`.

## 46. V23.20 — préparation de la rétention des modèles d'inventaire

### Mécanisme du gel, établi sur les sources officielles

`Inventory.cpp` :

```cpp
void C_inventory::LoadModels(PI3D_scene is, C_poly_text &texts){
   if(IsVisible()) return;                       // rien à faire si déjà chargé
   for(i...){ model = driver->CreateModel();
              model_cache.Open(model, name, is, ...);
              model->LinkTo(is->GetPrimarySector());
              items[i]->model = model; }
}
void C_inventory::ReleaseModels(){
   for(i...) items[i]->model = NULL;             // tout est détruit à la fermeture
   inv_scene = NULL;
}
```

Chaque fermeture détruit les modèles, chaque ouverture les reconstruit un par
un. Le coût est donc strictement proportionnel au nombre d'objets détenus, et
la garde `if(IsVisible()) return;` montre que conserver l'état supprimerait
entièrement le rechargement.

### Recherche de `ReleaseModels` : non concluante à ce stade

- Ancrages tentés sans succès : requêtes de chaînes de pas 16 sur la table
  d'inventaire (`0x50AAD0`), colonnes de type 5 sous-type 16, appels
  `[vtable+0x48]` avec `push 10h`. Les sites trouvés appartiennent à d'autres
  sous-systèmes (`0x50AC04`, code éditeur/console).
- Aucune adresse n'a donc été retenue, et **aucun octet n'a été écrit**. Poser
  un trampoline sur une adresse non identifiée est exactement la manière de
  casser une fonctionnalité existante.

### Voie retenue, sans hook de code

Les champs suffisent : `items.begin/end` sont déjà connus à `player+0x5C/0x60`,
donc `inv_scene` et le champ `model` de chaque entrée sont dans la même
sous-structure. En les relevant, la rétention devient réalisable uniquement
par écritures mémoire et une prise de référence, sans jamais patcher de code :
conserver une référence sur chaque modèle, puis restaurer `inv_scene` et les
pointeurs `model` avant l'ouverture suivante, ce qui fait sortir `LoadModels`
immédiatement. La désactivation remet les champs à zéro et relâche les
références.

### Ce que fait la V23.20

- `ObserveInventoryState` relève en **lecture seule** les seize dwords à partir
  de `player+0x50` et journalise le bloc à chaque changement, au plus toutes
  les 250 ms et vingt-quatre fois par joueur. Ouvrir puis fermer l'inventaire
  une fois enregistre donc les deux états.
- Aucune écriture, aucun hook, aucun changement de comportement.

### Release

- Builds Debug et Release x86 réussis, aucun avertissement.
- `build\vs2026-x86\Release\HDFinalAdvancedV23_20_INVENTORY_OBSERVER.exe`,
  `506880` octets, SHA-256
  `1BDFAD196871DA1F61AFF62A0CCE3046B64500653C374BA3D00AF3080667BC1C`.

## 47. V23.21 — disposition de C_inventory confirmée

### Ce que l'observation V23.20 a donné

L'en-tête officiel `H&D.h` fixe la disposition, et le relevé mémoire la
confirme champ par champ :

```cpp
class C_inventory{
   C_game_menu &game_menu;                 // player+0x58
   vector<C_smart_ptr<S_item> > items;     // begin 0x5C, end 0x60, cap 0x64
   C_smart_ptr<I3D_scene> inv_scene;       // player+0x68  (NULL = non visible)
   int active_item;                        // player+0x6C
};
struct S_item : public C_unknown {         // vtable + compteur = 8 octets
   int itm;                                // +0x08
   C_smart_ptr<I3D_model> model;           // +0x0C
   C_smart_ptr<I3D_interpolator> intp;     // +0x10
   C_smart_ptr<C_text> num;                // +0x14
};
```

Le relevé montre bien `player+0x68` basculant entre un pointeur et zéro, et
`player+0x6C` décroissant au défilement des objets : ce sont `inv_scene` et
`active_item`.

### Point établi sur la question posée

Les objets sont ajoutés **au moment de l'appui sur M**, pas à l'ouverture de
l'inventaire : le journal V23.18 le prouve avec `verified=43/43` immédiatement
après l'appui. Le gel n'est donc pas la distribution, c'est la reconstruction
des modèles 3D par `LoadModels`.

### Conception retenue pour la rétention

- Relever les pointeurs `model` de chaque entrée tant que l'inventaire est
  ouvert, puis, après la fermeture, réécrire ces pointeurs et `inv_scene` pour
  que `LoadModels` sorte sur sa garde `if(IsVisible()) return;`.
- La scène elle-même appartient à `C_game_menu` et survit à la fermeture :
  `C_inventory::inv_scene` n'en est qu'une copie servant d'indicateur de
  visibilité.
- **Danger identifié** : si des objets sont ajoutés pendant que la rétention
  est active, `LoadModels` ne s'exécute pas et les nouvelles entrées gardent
  `model = NULL`, que `C_inventory::Tick` déréférence sans test. La rétention
  doit donc être invalidée à chaque changement du nombre d'objets, donc après
  chaque appui sur M.
- Reste à vérifier avant toute écriture : les modèles survivent-ils à
  `ReleaseModels` grâce à la référence détenue par le secteur de la scène, ou
  faut-il prendre une référence explicite au préalable.

### Ce que fait la V23.21

- L'observation est resserrée sur les champs utiles et journalise
  `items`, `scene`, `visible`, `active`, `item0`, `model`, `vtable`, `alive`.
- `alive` indique si le pointeur de modèle relevé reste lisible **après** la
  fermeture : c'est la réponse à la question ci-dessus.
- Toujours strictement en lecture seule : aucune écriture, aucun hook.

### Release

- Builds Debug et Release x86 réussis, aucun avertissement.
- `build\vs2026-x86\Release\HDFinalAdvancedV23_21_INVENTORY_FIELDS.exe`,
  `506880` octets, SHA-256
  `FCE8F676A2B059A5FAE059F97B12B55163CAB20A8D0212B87CA2B1C9D429CAAF`.

## 48. V23.22 — ce que le relevé V23.21 établit, et la dernière inconnue

### Erreur d'étiquette de la V23.21, corrigée

La lecture partait de `player+0x5C` sur quatre dwords, soit begin, end,
**capacity** et **inv_scene**. Les colonnes journalisées `scene` et `active`
correspondaient donc en réalité à la capacité du vecteur et à `inv_scene`.
Les données restent exploitables une fois réétiquetées.

### Acquis certains

- `inv_scene` alterne entre `0` et **la même adresse `0x0F6A1BD0` à chaque
  ouverture**. La scène d'inventaire appartient donc bien à `C_game_menu`,
  survit aux fermetures, et n'est pas recréée. C'est conforme à
  `GameMenu.cpp::SetInventory`.
- Le champ étiqueté `scene` changeait de valeur (`02505F10`, `024F9490`,
  `025005F0`, `024FCFF0`) au fur et à mesure que le nombre d'objets passait de
  5 à 16, 27 puis 37 : c'est la capacité, réallouée à chaque croissance du
  vecteur. Cohérent.
- **Le modèle du premier objet a une adresse différente à chaque ouverture**
  (`12BD32A0`, `132A3D00`, `0F625300`, `0F3A3930`, `0AEF2730`, …). Le moteur
  reconstruit donc bien tous les modèles à chaque fois, exactement comme
  `LoadModels` le décrit. La vtable est constamment `0x0AD476C0`, ce qui donne
  un critère de validité fiable pour un modèle vivant.
- Inventaire fermé, le champ `model` de chaque entrée vaut zéro.

### La dernière inconnue

Le test `alive` de la V23.21 lisait le pointeur **courant**, nul une fois
fermé : il ne pouvait donc rien dire. La question reste entière : l'objet
modèle survit-il à `ReleaseModels` grâce à la référence détenue par le secteur
de la scène ?

### Ce que fait la V23.22

L'observation mémorise le dernier pointeur de modèle vu pendant que l'écran
était ouvert, ainsi que sa vtable, puis **re-sonde cette adresse précise après
la fermeture** :

- `survives=1` : la vtable attendue est toujours là, l'objet a survécu. La
  rétention se réduit alors à réécrire les pointeurs et `inv_scene`, sans
  toucher aux compteurs de références.
- `survives=0` : l'objet est détruit ; il faudra prendre une référence avant la
  fermeture, via un trampoline sur le thread du jeu.

Toujours strictement en lecture seule.

### Release

- Builds Debug et Release x86 réussis, aucun avertissement.
- `build\vs2026-x86\Release\HDFinalAdvancedV23_22_MODEL_SURVIVAL_PROBE.exe`,
  SHA-256
  `59438E86229F58C02EAE827A076071142233EB0D5465A67FED6E707CE5F2D14D`.

## 49. V23.23 — rotation des séries d'inventaire

### Réponse définitive de la sonde V23.22

```
open=1  model=0C0766D0
open=0  model=00000000  closed_probe=0A7E73AC  expected=0A7E76C0  survives=0
```

Après la fermeture, l'adresse du modèle ne porte plus sa vtable mais une autre :
la mémoire est déjà réattribuée. Résultat identique sur les cinq cycles.
**Les modèles ne survivent pas à `ReleaseModels`.** Réécrire les anciens
pointeurs viserait de la mémoire libérée et recyclée. La rétention sans prise
de référence est donc impossible, et l'option retenue est la rotation.

*(Les lignes `survives=1` correspondent à l'instant d'ouverture, où les modèles
sont créés avant que `inv_scene` soit renseigné : ce n'est pas l'état fermé.)*

### Rotation

- Nouvelle case « Remplacer la série précédente (rotation) », cochée par
  défaut. Chaque appui sur M libère la série précédente avant d'accorder la
  suivante, donc l'inventaire ne contient jamais qu'une série.
- La libération se réduit à **une seule écriture** : `items.end` est ramené à
  `items.begin + 4`, ne laissant que la première entrée, qui est la ligne
  permanente « Free hands ». Aucun compteur de références n'est manipulé.
- L'écriture est émise **dans le trampoline**, donc sur le thread du jeu, et
  gardée par un drapeau en `kSelectionStateOffset + 40` : `ProcessCheat`
  s'exécutant une fois par touche de la séquence, sans ce drapeau la deuxième
  exécution effacerait ce que la première venait d'accorder.
- La rotation n'est appliquée que si `inv_scene` vaut zéro, c'est-à-dire écran
  d'inventaire fermé. Les modèles sont alors déjà libérés et les `S_item`
  abandonnés ne contiennent plus que des entiers : rien de rattaché à la scène
  n'est laissé derrière. Le journal indique
  `Fullhands: rotation=1 (requested=1 inventory_open=0 items=55).`
- `grant_limit` est calculé sur l'inventaire d'après rotation, ce qui évite de
  brider la distribution par des objets sur le point d'être libérés.

### Release

- Builds Debug et Release x86 réussis, aucun avertissement.
- `git diff --check` propre.
- `build\vs2026-x86\Release\HDFinalAdvancedV23_23_ROTATING_SERIES.exe`,
  `507392` octets, SHA-256
  `DCE979D05A4ACFC10CD21052FC34E9A91B1433CFF0FA865FAB01414A489B9676`.

## 50. V23.24 — mesure du gel plutôt qu'hypothèses

### Méthode

À la demande de l'utilisateur, on cesse d'éprouver des solutions successives :
on instrumente pour identifier la cause exacte.

Le moteur horodate sa scène avec l'instant du dernier rendu, à
`scene+0x18C`. Tant que le thread du jeu est bloqué, cet horodatage cesse
d'avancer. En l'échantillonnant à chaque image du trainer, en même temps que
`inv_scene` et le nombre d'objets, on obtient la **durée réelle de chaque
blocage**, l'état de l'inventaire à cet instant, et le rang de l'ouverture
depuis le dernier appui sur M.

### Ce que le journal produira

```
[TEST STALL] inventory opened: items=12 open_index_since_M=1.
[TEST STALL] game thread stalled 940 ms (inventory_open=1 items=12 open_index_since_M=1).
[TEST STALL] inventory closed: items=12.
[TEST STALL] inventory opened: items=12 open_index_since_M=2.
```

Les questions auxquelles ces lignes répondent sans ambiguïté :

- le blocage se produit-il à l'ouverture, à la fermeture, ou à l'appui sur M ;
- sa durée exacte en millisecondes, au lieu d'« un peu » ;
- s'il décroît d'une ouverture à l'autre pour une même série, donc si le cache
  de géométrie du moteur se remplit réellement ;
- s'il dépend du nombre d'objets portés.

Seuil de détection à 120 ms, au plus soixante lignes par joueur, strictement en
lecture seule.

### Release

- Builds Debug et Release x86 réussis.
- `build\vs2026-x86\Release\HDFinalAdvancedV23_24_STALL_METER.exe`,
  `507392` octets, SHA-256
  `8EAFAA1EBE455A1003E96AA43DF4A33508D02A6AB6B0D7F5E1D0D0D71BF2239C`.

## 51. V23.25 — fluidité obtenue par la mesure

### Chiffres relevés par la V23.24

```
00:29:18  inventory opened: items=5  open_index_since_M=1     (aucun blocage)
00:29:28  inventory opened: items=5  open_index_since_M=2     (aucun blocage)
00:29:35  granting series 1 of 6 -> granted_count=22
00:29:38  inventory opened: items=23 open_index_since_M=1
00:29:38  game thread stalled 469 ms (inventory_open=0 items=23)
```

- **5 objets : aucun blocage détectable**, sous le seuil de 120 ms, ni à
  l'ouverture ni à la fermeture.
- **23 objets : 469 ms**, soit environ **20 ms par objet**.
- `inventory_open=0` pendant le blocage : il se produit donc **pendant**
  `LoadModels`, avant que `inv_scene` soit renseigné en fin de fonction. Le
  coût est bien la construction des modèles, pas l'affichage.
- La relation est linéaire et le levier est unique : le nombre d'objets portés.

Le blocage de 38 s enregistré ensuite était un faux positif : H&D cesse de
rendre quand il perd le premier plan. La mesure ignore désormais les périodes
où la fenêtre du jeu n'est pas active.

### Correction

- Le curseur de séries va jusqu'à **32** au lieu de 12, et sa valeur par défaut
  passe à **16**. À 16 séries, environ 8 objets par appui, soit d'après la
  mesure de l'ordre de 160 ms — et 24 séries donnent environ 5 objets, valeur
  pour laquelle aucun blocage n'a pu être détecté.
- Le panneau affiche la mesure pour que le réglage soit fait en connaissance de
  cause : « 23 objets = 469 ms, 5 objets = aucune ; environ 20 ms par objet ».
- Le catalogue reste intégralement accessible, en davantage d'appuis.

### Release

- Builds Debug et Release x86 réussis.
- `build\vs2026-x86\Release\HDFinalAdvancedV23_25_MEASURED_FLUIDITY.exe`.

## 52. V23.26 — une case par idée de fluidité

### La mesure V23.25 a invalidé le modèle linéaire

```
granting series 2 of 16 -> granted_count=8
inventory opened items=9 open_index_since_M=1 -> stalled 532 ms
inventory opened items=9 open_index_since_M=2 -> aucun blocage
granting series 3 of 16 -> granted_count=8
inventory opened items=9 open_index_since_M=1 -> stalled 219 ms
inventory opened items=9 open_index_since_M=2 et 3 -> aucun blocage
```

Neuf objets coûtent 532 ms, vingt-trois en coûtaient 469 : le coût **ne dépend
pas du nombre d'objets**. Le « 20 ms par objet » déduit de deux points était une
coïncidence. La loi réelle est :

> le coût est payé une seule fois, à la **première** ouverture suivant un
> changement de contenu ; toutes les ouvertures suivantes sont gratuites.

Il décroît aussi d'une série à l'autre (532 puis 219 ms), ce qui correspond au
remplissage progressif du cache de géométrie du moteur.

### Crash du test

Plantage réel à `00:34:08`, `0xC0000005`, signature non résolue dans
`hde.exe` donc située dans une DLL du moteur, une seconde et demie après une
série d'ouvertures et fermetures rapides. La rotation est le suspect : c'est la
seule écriture brute dans un conteneur du moteur, et elle abandonne les
anciennes entrées sans les libérer. Elle passe donc **décochée par défaut**.

### Les trois idées, chacune sur sa case

- **Idée 1 — précharger juste après M.** Le trainer bascule lui-même la touche
  d'inventaire du jeu, attend 900 ms, la rebascule. L'attente est payée à
  l'instant de l'appui ; toutes les ouvertures manuelles suivantes sont
  gratuites. Aucune écriture mémoire, aucun hook : c'est le chemin `SendInput`
  déjà éprouvé pour la touche Espace de la carte.
- **Idée 2 — rotation.** Conservée telle quelle, décochée par défaut.
- **Idée 3 — préchauffer tout le catalogue au premier appui.** Le premier M de
  la session accorde toutes les séries et précharge une fois ; les appuis
  suivants reprennent le réglage courant.
- Un sélecteur « Touche d'inventaire du jeu » (I, M, B, TAB, F1, F2) alimente
  les idées 1 et 3. Sans la bonne touche, elles ne peuvent rien déclencher.

Le compteur de blocages reste actif : chaque essai sera jugé sur des
millisecondes, pas sur une impression.

### Release

- Builds Debug et Release x86 réussis.
- `build\vs2026-x86\Release\HDFinalAdvancedV23_26_FLUIDITY_IDEAS.exe`,
  `514560` octets, SHA-256
  `9CF624A98223D9FDCD3172488670508DD06D8399D464D3644A9C8F018C6EF854`.

## 53. V23.27 — case « All lots »

### Idée de l'utilisateur

Donner tout le catalogue sans passer par M, de sorte que l'inventaire soit déjà
complet et ne change plus jamais.

### Évaluation

Le coût mesuré n'est pas lié au nombre d'objets mais au **changement de
contenu** : la première ouverture qui suit un changement coûte de 200 à 550 ms,
toutes les suivantes coûtent zéro. Ne plus jamais modifier l'inventaire est donc
la bonne réponse : il ne reste qu'une seule attente, à la toute première
ouverture de la mission.

Une réserve : la distribution passe par `C_actor::cbProc` du joueur contrôlé,
qui n'existe pas tant qu'aucune mission n'est chargée. « Avant d'entrer en
mission » est donc impossible au sens strict ; l'équivalent pratique est de le
faire automatiquement dès que la mission tourne, ce que l'utilisateur ne
distingue pas.

### Réalisation

- Case « All lots : tout donner automatiquement au début de la mission ».
- Quand elle est cochée, le catalogue complet est accordé **une fois par
  mission**, sans aucun appui sur M, dès qu'un pointeur de mission et un joueur
  contrôlé valides sont disponibles et que le snapshot n'est pas un cache.
  L'identité de la mission sert de garde, donc une seule distribution par
  mission ; décocher la case réarme le mécanisme.
- La distribution force `series_count = 1` et désactive la rotation pour cet
  appel : tout est donné d'un coup, et rien ne modifie l'inventaire ensuite.
- Le chemin utilisé est exactement celui de M, déjà validé
  (`granted_count`, vérification `verified=N/N`, contrôle d'équilibre de pile,
  refus sûr en cas d'anomalie). Aucun nouveau mécanisme d'écriture.
- Journalisé par `Fullhands: All lots — granting the whole catalogue for
  mission %08X.`

### Release

- Builds Debug et Release x86 réussis.
- `build\vs2026-x86\Release\HDFinalAdvancedV23_27_ALL_LOTS.exe`.

## 54. V23.28 — rotation sécurisée, et loi complète du figement

### Loi établie par la mesure « All lots »

```
50 objets : ouverture 1 -> 1344 ms
            ouvertures 2 a 6 -> 734, 734, 735, 703, 750 ms
```

Le coût **ne disparaît pas** après la première ouverture : il se stabilise. En
recoupant avec les relevés à 9 et 23 objets, la loi est :

> environ **15 ms par objet, à chaque ouverture**, plus un supplément unique de
> 400 à 600 ms la première fois pour la lecture disque de la géométrie.

Vérification : 9 objets → 132 + 400 = 532 ms observé ; 23 objets → 338 + 130 =
469 ms observé ; 50 objets → 735 ms observé. La formule tient sur tous les
points mesurés.

Conséquence : « All lots » plaçait l'utilisateur dans le pire cas. Et sans
rotation, les séries s'accumulent, donc le figement grandit d'environ 60 ms
tous les quatre objets. C'est la cause de la dégradation progressive constatée
depuis le début.

Aucun rapport de plantage Windows n'a été émis pendant cette session : il
s'agit bien d'un figement, pas d'une sortie du jeu.

### Correction de sécurité de la rotation

La rotation est la seule réponse au problème d'accumulation. Sa seule faiblesse
identifiée était une fenêtre de course : `inv_scene` était lu par le trainer,
puis l'écriture avait lieu jusqu'à 140 ms plus tard dans le trampoline. Si
l'écran s'ouvrait entre-temps, des modèles rattachés à la scène d'inventaire
pouvaient être orphelins.

Le trampoline **re-teste maintenant `inv_scene` lui-même**, sur le thread du
jeu, à l'instant exact de l'écriture. Si l'écran est ouvert, la rotation est
purement et simplement sautée. La fenêtre de course est fermée.

### Release

- Builds Debug et Release x86 réussis.
- `build\vs2026-x86\Release\HDFinalAdvancedV23_28_SAFE_ROTATION.exe`.

## 55. V23.29 — un seul contrôle Fullhands, et invisibilité sans vol du focus

### Simplification demandée

Toutes les pistes de fluidité explorées cette nuit sont retirées de
l'interface. Il ne reste **qu'un seul bouton**, « Armes et équipement — série
suivante [M] », qui combine la distribution et la rotation :

- découpage fixé à **28 séries**, soit environ quatre objets par appui ;
- rotation **toujours active** : chaque appui libère la série précédente ;
- plus aucun curseur, aucune case d'idée, aucun sélecteur de touche.

Ces deux valeurs sont désormais des constantes du code, choisies d'après la
mesure : quatre objets s'ouvrent sans blocage détectable, cinquante coûtent
735 ms à chaque ouverture. C'est la seule configuration relevée à zéro.

Retirés : « All lots », « Idée 1 : précharger après M », « Idée 3 :
préchauffer le catalogue », le sélecteur de touche d'inventaire et le curseur
de séries, ainsi que les réglages correspondants de `GameplaySettings`.

### Invisibilité : le focus n'est plus volé

Cocher « Invisible pour les ennemis » ramenait immédiatement la fenêtre du jeu
au premier plan. La cause est identifiée : la purge des sons, poursuites et
voix mémorisés s'exécute par le déclencheur de cheat
(`SendInventoryMainThreadTrigger`), qui envoie des touches à la fenêtre active
et appelle donc `SetForegroundWindow` sur H&D.

La purge attend maintenant que le jeu soit **déjà** actif de lui-même. Les deux
trampolines `IsEnemy` sont de simples écritures mémoire posées immédiatement,
donc l'invisibilité est effective dès le cochage ; seule la purge de l'état
déjà mémorisé est différée jusqu'au retour naturel dans le jeu. Aucun appel de
focus n'est plus émis par cette case.

### Release

- Builds Debug et Release x86 réussis, aucun avertissement.
- `build\vs2026-x86\Release\HDFinalAdvancedV23_29_SINGLE_CONTROL.exe`,
  `507392` octets, SHA-256
  `9DD0CEB8E632160FB208FC03F054EFA1EEB44013B1E77ED379A71E6E34D7C8A0`.

## 56. V23.30 — la rotation était annulée à chaque appui

### Symptôme

L'utilisateur constate que les objets s'accumulent malgré la rotation intégrée.
Le journal donne la raison directement :

```
Fullhands: rotation=0 (requested=1 inventory_open=1 items=44).
Fullhands: rotation=0 (requested=1 inventory_open=1 items=47).
Fullhands: rotation=0 (requested=1 inventory_open=1 items=50).
```

`requested=1` mais `rotation=0` : la garde « écran d'inventaire fermé »,
évaluée côté trainer, répondait « ouvert » à chaque appui et annulait donc la
libération de la série précédente. D'où l'accumulation, de 44 à 53 objets.

### Correction

Cette lecture précédait l'écriture de jusqu'à 140 ms et s'est révélée non
fiable en pratique. Elle est supprimée. La vérification qui compte est celle
que le trampoline effectue **lui-même, sur le thread du jeu, à l'instant exact
de l'écriture** — ajoutée en V23.28 : si l'écran est ouvert à ce moment précis,
la rotation est sautée et rien n'est orphelin.

La sécurité est donc conservée, sans la garde parasite qui bloquait tout. Le
journal indique désormais `inventory_open_seen=` à titre informatif, en
précisant que le trampoline reteste lui-même.

### Release

- Builds Debug et Release x86 réussis, aucun avertissement.
- `build\vs2026-x86\Release\HDFinalAdvancedV23_30_ROTATION_FIXED.exe`,
  SHA-256 à vérifier ci-dessous.
  `CBE3670885F2369386F4EADE2C57482153E1FFB009DDD35F62C0C505B6942F1F`.

## 57. V23.31 — portée « escouade entière » alignée sur la portée locale

### Vérification demandée

Les deux crochets ont été relus. Le crochet réciproque `C_enemy::IsEnemy` est
correct dans les deux portées : la portée locale compare l'émetteur au joueur
contrôlé, la portée escouade teste `[acteur+0x1C] == 1`, soit le type joueur,
confirmé par `kActorTypePlayer = 1` dans le radar.

Le changement de portée est bien appliqué : il est mémorisé puis installé au
premier retour naturel dans le jeu, mécanisme de la V23.6 conservé.

### Asymétrie corrigée

Le crochet visuel `C_player::IsEnemy` n'était pas symétrique :

- portée locale : `cmp ecx, joueur protégé` puis, si différent, exécution du
  code natif ;
- portée escouade : **aucun test**, `xor al,al ; ret 4` renvoyé
  inconditionnellement pour tout objet atteignant ce point.

La portée escouade applique désormais exactement la même logique que la portée
locale, avec sa propre condition : `cmp [ecx+0x1C], 1`, c'est-à-dire « acteur
de type joueur ». Tout ce qui n'est pas un joueur repart dans la fonction
native au lieu d'être filtré aveuglément.

### Release

- Builds Debug et Release x86 réussis, aucun avertissement.
- `build\vs2026-x86\Release\HDFinalAdvancedV23_31_SQUAD_SCOPE.exe`,
  SHA-256 `AC3270DC6CCF18794F1797454B10EC8272725D29E2E29AEF3A0A0AD0C03E5991`.

## 58. V24.0 — balles à travers les murs, et panneau allégé

### Balles à travers les murs

La fonctionnalité n'a demandé **aucun nouveau mécanisme** : elle réutilise
exactement ce qui a été construit et validé pour le Bullet Track.

Le trampoline place le frame tête de la cible dans `ECX` puis laisse le
résolveur natif de `C_gun_shoot::Evaluate` remonter ce frame jusqu'à son acteur
et lui appliquer les dégâts — **quelle que soit la géométrie réellement touchée
par le rayon**. Traverser un mur était donc déjà possible ; ce qui l'interdisait
était uniquement le filtre de sélection, qui n'acceptait que les cibles vertes,
c'est-à-dire celles disposant d'un trajet balistique dégagé.

`SelectAimbotTarget` reçoit un paramètre `require_visible`, que le chemin Bullet
Track passe à faux quand la case est cochée. Les ennemis rouges deviennent alors
sélectionnables. Aucune écriture supplémentaire dans le jeu, aucun hook de plus,
et la validation de l'acteur contre la liste vivante de la mission continue de
s'appliquer.

La case est placée dans la section **Armes**, sous « Tir ultra-rapide », comme
demandé.

### Panneau allégé

La section « Journal de test » et son bouton sont retirés de l'interface. Le
journal `hdradar_diag.log` continue d'être créé à chaque lancement et rempli
normalement ; il s'ouvre simplement depuis le dossier de l'exécutable.

### Release

- Builds Debug et Release x86 réussis, aucun avertissement.
- `build\vs2026-x86\Release\HDFinalAdvancedV24_0_FINAL.exe`,
  `506880` octets, SHA-256
  `87A9479A7E2AD0B06AF68989567143B55E6C0005F62584B7B96BEA626A86BAC1`.

## 59. V24.1 — rotation retirée, et dégâts à travers les murs

### Plantage de M : la rotation, enfin confondue

Trois rapports WER, **tous à la même adresse**, `hde.exe+0x1F1D7` soit
`0x41F1D7` :

```asm
0041F1B5  lea eax,[esi+54h]      ; l'objet vecteur
0041F1C0  mov edi,[ecx+0Ch]      ; items.end
0041F1CF  mov eax,[ecx+8]        ; items.begin
0041F1D2  lea eax,[eax+edi*4]    ; &items[count-1]
0041F1D5  mov edx,[eax]          ; items[i]
0041F1D7  cmp [edx+8],ebx        ; <- pointeur d'objet invalide
```

Le moteur parcourt le vecteur d'objets **à l'envers**, du dernier au premier,
et déréférence chaque entrée. Notre rotation ramenait `items.end` sur
`begin + 4` en abandonnant les pointeurs des emplacements au-delà : ce sont eux
que ce parcours retrouve.

Ce plantage n'était jamais apparu auparavant parce que la rotation **ne
s'exécutait jamais** : la garde côté trainer répondait `inventory_open=1` à
chaque appui et l'annulait. Elle n'a réellement tourné qu'à partir de la
V23.30, et les plantages sont apparus exactement là.

La rotation est donc retirée. Les objets s'accumulent de nouveau, environ cinq
par appui — c'est-à-dire le comportement qui fonctionnait.

### Balles à travers les murs : pourquoi elles ne tuaient pas

Le journal montrait `forced_damage_count=164` : la cible cachée était bien
sélectionnée et les dégâts bien forcés. Mais `hit_dest` n'était plus écrit
depuis la V23.14, donc il conservait la valeur native — le point d'impact du
rayon, c'est-à-dire **le mur**, à des dizaines de mètres de l'ennemi.

`Finish` transmet cette position au destinataire dans `S_CB_hit::hit_pos`, qui
s'en sert pour situer l'impact sur le corps. Un point d'impact perdu dans un
mur ne blesse personne.

`hit_dest` est donc réécrit, et lui seul, avec la position du frame tête.
`hit_frm` reste intouché : c'est lui qui alimentait la table de matériaux et
provoquait les plantages de la V23.10, pas `hit_dest`.

### Release

- Builds Debug et Release x86 réussis, aucun avertissement.
- `build\vs2026-x86\Release\HDFinalAdvancedV24_1_WALLSHOT_DAMAGE.exe`,
  SHA-256 `9F485B673DD60E7E969FA9B4FE9784C82EAA7DDADC98FB10D92E2B2160BA5558`.

## 60. V24.2 — rotation rendue sûre, et traçage de la sélection de cible

### Pourquoi la rotation plantait, et comment elle devient sûre

Le parcours fautif en `0041F1D5` lit `items[i]`. En ramenant simplement
`items.end` en arrière, les emplacements abandonnés **conservaient leurs
anciens pointeurs**. Le `push_back` du moteur écrit par affectation dans ces
emplacements, ce qui libère l'objet qu'ils nomment encore ; après une seconde
rotation, cet objet avait déjà disparu, d'où le pointeur invalide.

La rotation **efface maintenant chaque emplacement abandonné** avant de
déplacer `end` : une boucle écrit zéro de la deuxième entrée jusqu'à l'ancienne
fin. Une affectation ultérieure ne libère donc plus rien. Les `S_item`
abandonnés fuient, quelques dizaines d'octets par appui, ce qui est sans
conséquence et infiniment préférable à une double libération.

Le tout reste dans le trampoline, sur le thread du jeu, gardé par le drapeau
d'exécution unique et par le test de `inv_scene` fait sur place.

### Traçage de la sélection Bullet Track

L'utilisateur constate que le tir à travers les murs n'atteint pas tous les
ennemis. La sélection impose trois conditions, dans cet ordre : frame tête
valide, cible visible (levée par la case « traverser les murs »), et
**projection dans le rectangle de l'écran**.

Un compteur par motif est désormais journalisé dès qu'il change :

```
[TEST TARGET] enemies=4 rejected: no_head=0 hidden=0 off_screen=3 selected=1.
```

Il dira sans ambiguïté si les ennemis manqués sont écartés faute de frame tête,
parce qu'ils restent filtrés comme cachés, ou parce que leur tête ne se projette
pas à l'écran — le seul cas où il resterait quelque chose à décider.

### Release

- Builds Debug et Release x86 réussis, aucun avertissement.
- `build\vs2026-x86\Release\HDFinalAdvancedV24_2_SAFE_ROTATION_TARGET_TRACE.exe`,
  SHA-256 `C6C58B56F372B3AABDA305D8831C76EA78C0F5CB3BF8F812A305DD26CD20538F`.

## 61. V24.3 — rotation abandonnée, fenêtre de tir élargie

### Rotation : deux tentatives, deux fois la même adresse

Le journal confirme que la rotation s'exécutait bien (`items=56` puis
`rotation=1` puis `items=1`), et WER confirme que le jeu plantait toujours à
`hde.exe+0x1F1D7`. Effacer les emplacements abandonnés avant de déplacer
`items.end` n'a rien changé : le parcours en `0041F1D5` lit `items[count-1]`
puis déréférence `[edx+8]` **sans test de nullité**, donc la moindre
incohérence dans ce vecteur est fatale.

La conclusion est nette : manipuler le conteneur d'objets du moteur depuis
l'extérieur ne peut pas être rendu sûr. La rotation est retirée
définitivement. Les objets s'accumulent, environ cinq par appui, ce qui est
le comportement qui fonctionnait.

### Tir à travers les murs : le filtre était la projection

Le traçage a répondu exactement :

```
[TEST TARGET] enemies=44 rejected: no_head=0 hidden=0 off_screen=25 selected=1.
[TEST TARGET] enemies=44 rejected: no_head=0 hidden=0 off_screen=40 selected=1.
```

- `no_head=0` : toutes les têtes sont résolues.
- `hidden=0` : la case « traverser les murs » est bien prise en compte, aucun
  ennemi n'est plus écarté pour cause d'occlusion.
- `off_screen=25 à 40` sur 44 : **c'est là que tout se perd.** Un ennemi dans
  une maison se projette souvent hors du rectangle client — sous une ligne de
  fenêtre, derrière un montant de porte — et se faisait écarter.

La marge de projection est donc élargie d'une demi-largeur et d'une
demi-hauteur d'écran de chaque côté, uniquement quand le tir à travers les murs
est actif. La cible reste celle dont la tête est la plus proche du réticule :
c'est toujours la visée qui décide.

`forced_damage_count=434` montrait que le mécanisme de dégâts fonctionnait
déjà ; ce qui manquait était la sélection.

### Release

- Builds Debug et Release x86 réussis, aucun avertissement.
- `build\vs2026-x86\Release\HDFinalAdvancedV24_3_WIDE_WALLSHOT.exe`,
  SHA-256 `99C9184A9F3C27B2D7005488AA56947517E4A92175A25C624BA078A00EBA8E0F`.

## 62. V24.4 — le tir à travers les murs devient un tir à la tête

### Ce que le traçage a établi

L'élargissement de la fenêtre a bien agi : `off_screen` est tombé de 25-40 à
9-18 sur 47 ennemis, une cible est toujours choisie, `hidden=0`, `no_head=0`,
et `forced_damage_count` progresse. La balle atteignait donc l'ennemi.

Ce qui manquait était ailleurs : `S_CB_hit::sub_frame` reçoit `hit_frm`, que le
stub laissait à la valeur native — c'est-à-dire **le mur**. L'ennemi encaissait
donc un impact générique au lieu d'une balle dans la tête, et ne mourait pas.

### Pourquoi ce n'était pas fait plus tôt

Écrire `hit_frm` avec le frame tête est exactement ce qui plantait en V23.10 :
`Finish` réutilise ce champ après le callback de dégâts, en `004456C8`, pour
interroger la table de matériaux, et un frame de tête y répond par un
identifiant hors plage.

### Solution : écrire hit_frm, puis le retirer avant la table

Deux pièces complémentaires :

1. Le trampoline d'`Evaluate` publie le frame tête dans `hit_frm`, en
   échangeant la référence exactement comme le moteur le fait en `00444C52` :
   libération du frame déjà détenu, puis `AddRef` du nouveau. Uniquement quand
   la case « traverser les murs » est cochée, via un drapeau distant.
2. Un troisième trampoline, posé sur `004456C8` — l'entrée du bloc matériau
   dans `Finish`, dont la signature `8B 83 A0 00 00 00` est vérifiée — remet
   `hit_frm` à zéro pour nos tirs, après que le callback de dégâts s'en soit
   déjà servi comme `sub_frame`. Le moteur prend alors sa propre branche
   « pas de frame » et saute entièrement la table de matériaux.

Le callback reçoit donc une tête, la table n'est jamais interrogée avec un
identifiant invalide, et le champ est restauré à la désactivation comme les
deux autres hooks.

### Rotation

Retirée définitivement, voir §61. Les objets s'accumulent d'environ cinq par
appui.

### Release

- Builds Debug et Release x86 réussis, aucun avertissement.
- `build\vs2026-x86\Release\HDFinalAdvancedV24_4_HEADSHOT_THROUGH_WALLS.exe`,
  SHA-256 `FE6A28BF3C1F8494FF608205684E5A56DB604763B8D6D180007931428E31E9E0`.

## 63. V24.5 — plus aucune libération faite par le trainer, et plafond d'objets

### Crash en rafale

Le tir à travers les murs fonctionne, confirmé par l'utilisateur. Mais une
rafale a tué le jeu : WER `0xC0000005` à `hde.exe+0x205A8`, soit `0x4205A8` :

```asm
004205A4  mov eax,[ecx+4]
004205A7  dec eax
004205A8  mov [ecx+4],eax     <- l'objet visé n'existe plus
```

C'est une affectation de pointeur intelligent qui libère l'ancienne valeur, sur
un objet déjà détruit. Plusieurs projectiles vivaient simultanément et le stub
appelait `Release` sur le frame précédemment détenu par chacun d'eux.

**Toute libération faite par le trampoline est supprimée.** Il n'effectue plus
un seul décrément : il écrit `hit_frm` et fait l'`AddRef` du frame tête, rien
d'autre. Le moteur conserve sa propre référence sur la géométrie touchée ; le
coût est que cette référence n'est plus rendue, donc une fuite — jamais une
double libération. C'est la même règle qui a stabilisé le Bullet Track :
n'imposer aucune durée de vie au moteur.

### Plafond d'objets portés

La rotation étant définitivement écartée (§61), la seule façon sûre d'empêcher
l'inventaire de croître sans fin est de cesser d'y ajouter. Au-delà de
**30 objets portés**, l'appui sur M est refusé proprement : rien n'est écrit,
le vecteur d'objets du moteur n'est jamais touché, et le journal l'indique par
`Fullhands: carried ceiling reached`. Recharger la mission repart de zéro.

Le découpage reste fixé à 28 séries, soit environ quatre à cinq objets par
appui : le plafond est donc atteint après six ou sept appuis, ce qui laisse
l'écran d'inventaire dans la zone rapide mesurée.

### Release

- Builds Debug et Release x86 réussis, aucun avertissement.
- `build\vs2026-x86\Release\HDFinalAdvancedV24_5_NO_RELEASE_CEILING.exe`,
  SHA-256 `F070AD57682D26A4BBC745166E4AC12808734954F1E06D43E51568A94F284539`.

## 64. V24.6 — la balle traverse réellement le mur

### La règle tirée des fonctionnalités qui marchent

Noclip, invisibilité, armes et vitesse véhicule n'écrivent que des **nombres**
ou ne renvoient qu'un **booléen**. La partie stable du Bullet Track se contente
de placer un frame dans `ECX` et de **laisser le moteur travailler**. Aucune ne
prend jamais possession d'un objet du moteur.

Les deux mécanismes qui plantaient touchaient au contraire à la propriété des
objets : la rotation manipulait un conteneur de pointeurs intelligents, et
l'écriture de `hit_frm` un pointeur à compteur de références. C'est la règle
qui a été violée, et elle explique tous les plantages de cette série.

### La solution conforme à cette règle

`GunShoot.cpp` donne le mécanisme exact :

```cpp
I3D_collision_data cd(pos, dir, I3DCOL_RAY|I3DCOL_TEXTURE|I3DCOL_JOINT_VOLUMES,
                      shooter_frame, FlyColRespGeo);
if(mission.GetScene()->TestCollision(cd)){ ... hit_frm = cd.GetHitFrm(); ... }
```

`FlyColRespGeo` est une **réponse de collision** : pour chaque objet rencontré,
elle répond si la balle s'y arrête. Le désassemblage la situe à `0x0043D950`,
convention `ret 4`, résultat dans `al`, et son cas « verre » sort par
`xor al,al ; ret 4` — donc **false signifie « la balle continue »**.

Le trampoline installé à son entrée répond `false` pour toute géométrie
n'appartenant à aucun acteur, en remontant quatre niveaux de parents comme le
moteur le fait lui-même en `00444E62`. Tout ce qui est rattaché à un acteur
retombe sur la réponse native, donc les ennemis arrêtent les balles
normalement.

Il n'agit que sur les tirs du joueur contrôlé : la donnée de collision porte le
frame du tireur en `cd+0x34`, comparé au frame publié par le trainer.

Conséquence : le moteur touche réellement l'ennemi. C'est **lui** qui remplit
`hit_frm`, `hit_actor`, la position et le matériau, exactement comme pour un
tir à découvert — cas qui n'a jamais planté. Le trainer n'écrit plus rien de
tout cela.

### Retiré

- L'écriture de `hit_frm` par le trampoline et le garde posé dans `Finish` :
  tous deux devenus inutiles, et tous deux touchaient à la propriété.
- Il ne reste, du côté du trainer, que des lectures, des écritures de nombres,
  et la remise du frame tête au résolveur natif.

### Release

- Builds Debug et Release x86 réussis, aucun avertissement.
- `build\vs2026-x86\Release\HDFinalAdvancedV24_6_TRUE_WALLSHOT.exe`,
  SHA-256 `3E070E881BA840308454099894A5F1179039535EF901A094E322CA41F77E9AB7`.

## 65. V24.7 — la réponse de collision devient sûre par défaut

### Régression introduite par la V24.6

L'utilisateur signale que les balles ne tuent plus, **même à découvert**, dès
que le tir à travers les murs est actif. La cause est dans le trampoline posé
sur `FlyColRespGeo` : il remontait quatre niveaux de parents à la recherche
d'un acteur et, si la recherche n'aboutissait pas, **répondait « la balle
continue »**. Un volume de collision d'ennemi dont la chaîne de parents dépasse
quatre niveaux tombait donc dans ce cas : la balle traversait l'ennemi lui-même.

Passer outre en cas de doute était le mauvais choix par défaut.

### Correction

- La profondeur passe de quatre à huit niveaux.
- Surtout, l'issue par défaut est inversée : la balle n'est autorisée à
  traverser que si la remontée atteint **le sommet de la hiérarchie sans
  rencontrer d'acteur**, ce qui prouve qu'il s'agit de géométrie du monde.
  Profondeur épuisée, frame illisible, doute quelconque : la réponse native
  reprend la main et la balle s'arrête normalement.

Le comportement à découvert est donc rigoureusement celui du jeu, et seule une
géométrie prouvée inerte laisse passer le projectile.

### Release

- Builds Debug et Release x86 réussis, aucun avertissement.
- `build\vs2026-x86\Release\HDFinalAdvancedV24_7_SAFE_WALLSHOT.exe`,
  SHA-256 `0D4FBD55E6E99DBC4A816B5627633F9C917B0B4BD0DF0BE7D80169A5406070FF`.

## 66. V24.8 — retour à un état stable, et limites assumées

### Crochet de collision retiré

Le trampoline posé sur `FlyColRespGeo` provoquait encore des plantages après
plusieurs tirs, et sa correction de sécurité (V24.7) n'a pas suffi. Il est
retiré intégralement. Le chemin de tir ne contient plus que ce qui a été
mesuré comme stable : le trampoline `CreateActor`, le trampoline post-collision
qui remet le frame tête au résolveur natif, et rien d'autre.

La case « traverser les murs » conserve son seul effet sûr : la sélection
accepte les ennemis rouges et élargit la fenêtre de projection. L'ennemi
derrière un mur encaisse donc les dégâts, mais l'impact n'est pas signalé comme
une balle dans la tête.

### Ce qui n'a pas pu être livré, et pourquoi

**Balle dans la tête à travers un mur.** Elle exige que `S_CB_hit::sub_frame`
soit le frame tête, donc que `hit_frm` le soit. Deux voies ont été essayées :
écrire `hit_frm` (fonctionne, mais tue le jeu en rafale — `0x004205A8`,
libération d'un objet déjà détruit) et faire traverser le rayon natif
(plantages après plusieurs tirs). Les deux touchent au chemin le plus fragile
du moteur.

**Rotation de l'inventaire.** Elle exige de retirer des objets. Trois voies
essayées : déplacer `items.end` (crash `0x0041F1D7`), effacer d'abord les
emplacements abandonnés (même crash, même adresse), et déléguer à une fonction
native — `C_inventory::DeleteAllItems` est introuvable dans le binaire, très
probablement inlinée, et l'unique fonction d'effacement de vecteur repérée
(`0x00439CB0`) est câblée pour des éléments de 32 octets, donc inutilisable
pour un vecteur de pointeurs de 4 octets.

Le plafond de 30 objets portés reste le garde-fou : au-delà, l'appui sur M est
refusé sans rien écrire.

### Release

- Builds Debug et Release x86 réussis, aucun avertissement.
- `build\vs2026-x86\Release\HDFinalAdvancedV24_8_STABLE.exe`,
  SHA-256 `1EA418E7A6ECB4027DA062D146F44264D5DD684F3D2B944D46334224D13C6D1D`.

## 67. V24.9 — rotation rétablie, et cibles limitées à ce que le moteur simule

### Erreur d'attribution corrigée

La rotation a bien fonctionné. Journal de la V23.23 :

```
Fullhands: rotation=1 (requested=1 inventory_open=0 items=5)
before native call items=23 -> rotation=1 -> granted_count=22 -> items reste 23
```

Elle tournait depuis la V23.23 et l'utilisateur avait confirmé que c'était
fluide et sans plantage. Les plantages `0x0041F1D7` puis `0x0041FB13` sont
apparus avec la **V24.0**, celle qui a introduit le tir à travers les murs, et
**tous deux se situent dans l'IA ennemie**. Les avoir imputés à la rotation
était une erreur d'attribution de ma part.

La rotation est rétablie dans la forme exactement mesurée fonctionnelle en
V23.23 : la fin du vecteur revient sur la première entrée permanente, et rien
d'autre n'est touché. L'ajout d'effacement des emplacements abandonnés, qui
n'avait jamais rien résolu, est retiré.

### Vraie cause probable des plantages d'IA

Le point commun des deux adresses est le code de décision de l'ennemi, et le
changement qui les a fait apparaître est la sélection des ennemis **cachés**.
Forcer des dégâts sur un ennemi que le moteur ne met plus à jour laisse son IA
avec des références qu'elle ne nettoie jamais.

Le radar publie donc un nouvel indicateur, `rendered_recently`, vrai dès que le
moteur a rendu l'acteur récemment, indépendamment du test balistique. En mode
« traverser les murs », la sélection exige désormais cet indicateur : le trajet
peut être bouché, mais l'ennemi doit rester **simulé**. Un ennemi trop lointain
ou dans un secteur en veille n'est plus visé, et c'est aussi la réponse à la
question de la portée.

### Release

- Builds Debug et Release x86 réussis, aucun avertissement.
- `build\vs2026-x86\Release\HDFinalAdvancedV24_9_ROTATION_LIVE_TARGETS.exe`,
  SHA-256 `15E11548247A2ACF261C7D7548DB0FC5E4CBA199DD9065FD25160C62C6813D70`.

## 68. V24.10 — les deux gardes restaurés

### Plantage à l'appui sur M

```
Fullhands: rotation=1 (requested=1 inventory_open_seen=1 items=6)
[TEST FULLHANDS #7] RESULT status=4 granted_count=0
```

`rotation=1` **alors que l'écran d'inventaire est ouvert**. La V23.30 avait
supprimé la garde côté trainer sous prétexte qu'elle bloquait tout, ne laissant
que le test fait dans le trampoline. Une pression est ainsi passée écran
ouvert, et tronquer le vecteur à ce moment-là laisse orphelins les modèles
rattachés à la scène d'inventaire.

La garde côté trainer est rétablie : rotation uniquement si `inv_scene == 0`,
**et** vérification refaite dans le trampoline sur le thread du jeu. C'est
exactement la configuration de la V23.23, celle qui fonctionnait.

### Balles qui ne traversent plus

```
[TEST TARGET] enemies=55 rejected: no_head=0 hidden=55 off_screen=0 selected=0.
```

Le nouvel indicateur `rendered_recently` introduit en V24.9 n'est presque
jamais renseigné : il rejetait 55 ennemis sur 55, donc plus aucune cible
derrière un mur. Le chemin qui le renseigne dépend de la lecture de
l'horodatage de rendu du visuel de l'acteur, qui n'aboutit que pour une
minorité d'entre eux.

Le mode « traverser les murs » accepte de nouveau tous les ennemis, comme dans
les versions où il atteignait réellement ses cibles.

### Release

- Builds Debug et Release x86 réussis, aucun avertissement.
- `build\vs2026-x86\Release\HDFinalAdvancedV24_10_BOTH_GUARDS.exe`,
  SHA-256 `13F8DDFB554EB1C1AA7F26E2591B821E5A3103449E80FB251B6A57447ABB8C71`.

## 69. V24.11 — garde de rotation fondée sur les modèles, et limiteur de rafale

### Rotation refusée à chaque appui

```
rotation=0 (requested=1 inventory_open_seen=1 items=6)  -> items 11
rotation=0 (requested=1 inventory_open_seen=1 items=11) -> items 16
rotation=0 (requested=1 inventory_open_seen=1 items=16) -> items 21
```

`inv_scene` était vu non nul à chaque appui alors que l'utilisateur jouait
normalement, écran fermé. Ce champ n'est donc pas un indicateur fiable de
l'état de l'écran depuis l'extérieur.

La garde teste désormais **le fait qui compte** : le champ `model` de la
première entrée de l'inventaire (`items[0] + 0x0C`). Nul signifie que
`ReleaseModels` a été exécuté et qu'aucun modèle n'est rattaché à la scène
d'inventaire, donc que tronquer est sans danger. Le trampoline continue de
retester `inv_scene` sur le thread du jeu comme seconde barrière.

Le journal publie maintenant `inv_scene=`, `model0=` et `models_released=`, ce
qui permettra de vérifier la décision au lieu de la supposer.

### Limiteur de rafale

Le plantage `0x0041FB13` est reproductible et se situe dans l'IA ennemie
(`cmp [esi+0x254],4` puis `mov eax,[edi]` / `mov ecx,[eax]`). Il n'apparaît que
lorsqu'un flot de balles force des dégâts sur plusieurs ennemis en quelques
images.

Une cible n'est donc plus publiée qu'au plus toutes les 120 ms. Une rafale ne
se transforme plus en avalanche de morts simultanées. C'est une atténuation,
pas une guérison : elle ne coûte qu'une comparaison et ne touche à aucun état
du moteur, conformément à la règle qui a stabilisé le reste.

### Ciblage multiple

Le Bullet Track ne désigne **qu'une cible à la fois**, celle dont la tête est
la plus proche du réticule. Tirer une rafale n'atteint donc pas plusieurs
ennemis derrière un mur : toutes les balles vont au même. C'est le
fonctionnement voulu de la visée, pas un défaut.

### Documentation

`README.md` : la section Armes décrit désormais les 28 séries, la libération de
la série précédente et le plafond de 30 objets portés.

### Release

- Builds Debug et Release x86 réussis, aucun avertissement.
- `build\vs2026-x86\Release\HDFinalAdvancedV24_11_MODEL_GUARD_BURST.exe`,
  SHA-256 `712799A99E15CA0FA7F86E242D7DCDF903C2BFBE5EE1114D1AC10E387C3675A0`.

## 70. V25.0 — cause exacte du crash, headshot transitoire et 7 lots sans accumulation

### Le dump invalide le diagnostic « IA surchargée » de V24.11

Le dernier dump Windows (`hde.exe.15776.dmp`) donne une violation d'accès en
lecture à `0x0041FB13`, avec `EAX=0` et la chaîne de retours suivante :

```text
0041FB13  <- callback acteur
004370FF
004456C8  <- C_gun_shoot::Finish, juste après l'appel du callback de dégâts
004455B4
0049E0EB
```

Dans `C_gun_shoot::Finish`, `0x00445677` copie le `projectile.hit_frm`
persistant dans le premier champ du `S_CB_hit` local (`[ebp-4C]`). Le résolveur
forcé avait correctement rempli `hit_actor`, mais laissait `hit_frm` nul pour
ne pas perturber la table de matériaux. Le callback recevait donc la
combinaison incohérente « acteur vivant + sub_frame nul », puis exécutait :

```asm
0041FB10  mov eax,[edi]   ; S_CB_hit::sub_frame == 0
0041FB13  mov ecx,[eax]   ; crash exact
```

Le limiteur de 120 ms ne pouvait pas corriger cette cause. Pire, la publication
commençait par mettre `shared_player` à zéro puis revenait avant de le republier
pendant la majorité des images : il expliquait les balles qui ne suivaient pas
toutes la cible. Il est retiré.

### Headshot sans propriété d'objet

Un troisième trampoline signé est posé exactement à `0x00445677`. Pour les
projectiles du joueur contrôlé dont le destinataire natif est un ennemi vivant,
il place le frame tête courant uniquement dans l'`EAX` que `Finish` allait
copier dans son callback local. Si la tête manque, le frame racine validé sert
de repli non nul.

Le champ persistant `[projectile+0xA0]` n'est jamais écrit : la table de
matériaux à `0x004456C8` relit donc la valeur native inchangée. Aucun `AddRef`,
`Release` ou remplacement de smart pointer n'est effectué par le trainer.
L'acteur cible est aussi validé dans le vecteur vivant de la mission avant que
le trampoline de création ne déréférence sa tête. Les trois crochets sont
restaurés et la page distante libérée ensemble.

La sélection reste la tête projetée la plus proche du centre du viseur. Le
mode murs autorise les ennemis rouges/occlus, `maximum_distance=0` conserve
l'absence de plafond métrique, et chaque image fraîche republie la cible : une
rafale entière suit donc la même tête tant que le viseur ne change pas de
cible.

### Inventaire : sept lots et aucun chemin d'accumulation

Le journal V24.11 montrait que la garde annulait la rotation mais continuait
ensuite le callback Fullhands, ce qui ajoutait le nouveau lot à l'ancien. Le
champ `inv_scene` n'était pas faux : la touche M native ouvrait ou finissait de
fermer l'inventaire au même moment.

La V25.0 réserve M au trainer avec le hook clavier déjà utilisé pour le noclip :
le jeu ne reçoit plus la touche et ne peut plus ouvrir l'écran pendant le
changement. Avant le grant, deux lectures consécutives `inv_scene == 0` sont
exigées. Le trampoline refait ce test sur le thread du jeu; si la scène s'est
rouverte, il renvoie `completion_state=2` **avant rotation et avant grant**.
L'appui entier est alors refusé et la page temporaire est libérée proprement.

Le catalogue complet est distribué par rang modulo **7**. Avec les 128 lignes
valides observées, les lots contiennent 18 ou 19 objets; avec 91 lignes, ils en
contiennent 13.

Les sources Deluxe et le désassemblage du binaire Ultimate ont permis de
retrouver la vraie fonction `C_inventory::DeleteAllItems` à `0x00462E80`
(signature `55 8B E9 8B 45 14 85 C0 74 05`). Les appels natifs lui passent
`player+0x54`, exactement le sous-objet utilisé ici. Avant cet appel, la V25.0
exécute aussi `C_human::SetSelectedInvItem(0,false)` à `0x004076E0` pour
holster l'arme dont l'objet va disparaître, puis remet l'index visuel à zéro.
Le moteur conserve l'entrée permanente et efface lui-même le reste par son
`vector<C_smart_ptr<S_item>>::erase`; aucune durée de vie n'est simulée par le
trainer.

La série n'avance qu'après une relecture finale prouvant : tous les IDs
attendus présents, aucun ID de l'ancien lot après l'entrée permanente, taille
au plus `lot + 1`, sélection et index actif remis à zéro. L'ancien cheat « tout
l'inventaire » est retiré du panneau et de ses raccourcis afin qu'aucun chemin
ne contourne cet invariant.

### Validation

- Compilation Release x86 réussie sans avertissement.
- Compilation Debug x86 réussie sans avertissement.
- `git diff --check` propre (hors avertissements CRLF informatifs).
- Binaire : `build\vs2026-x86\Release\HDFinalAdvancedV25_0_FINAL.exe`.

## 71. V25.1 - preload d'inventaire : le bon mecanisme, le mauvais endroit

### Le symptome n'etait pas un plantage

Le journal de la session V25.0 ne contient aucun rapport Windows, et le dernier
`AppCrash_hde.exe` datait de 03:57, soit la V24.11. La mesure etait explicite :

```text
[TEST STALL] game thread stalled 156 ms (inventory_open=0 items=19
             open_index_since_M=1)
```

156 ms, a la premiere ouverture apres un changement de contenu, et a elle
seule.

### La cause, lue dans les sources

`C_inventory::LoadModels` reconstruit chaque objet porte via `model_cache`, un
cache par nom de modele qui survit a `ReleaseModels` : d'ou une premiere
ouverture couteuse et des suivantes gratuites. Le cout n'est paye qu'une fois
par nom, a sa premiere construction.

### Le mecanisme trouve

`C_game_menu::SetInventory(C_inventory*)` est a **`0x0045FDC0`**, identifiee par
l'unique reference au litteral `"inventory camera"` (`0x00502A90`), signature
`83 EC 20 56 8B 74 24 28 57 8B F9 39 77 34`. Elle est atteinte comme le font
tous les appels natifs : `mov ecx,[actor+18h] / add ecx,1FCh`. Sa queue appelle
`C_inventory::ReleaseModels` (`0x00463380`) puis
`C_inventory::LoadModels(scene, texts)` (`0x00462F20`).

Un appel avec l'inventaire suivi d'un appel avec `NULL` construit donc tous les
modeles du lot puis les relache, en laissant le cache chaud.

### Ce qui a ete livre, et pourquoi c'est retire

La V25.1 emettait ces deux appels a la fin du trampoline de distribution, sur
le thread du jeu, l'ecran prouve ferme, le pointeur de menu resolu et valide
cote hote (vecteur d'acteurs de la mission coherent avec celui du radar,
`curr_inv == 0`, `inv_scene` nulle ou saine).

**Cela a fonctionne exactement comme prevu** - le journal le montre :

```text
Fullhands: preload armed (mission=0254E170 game_menu=0254E36C
           curr_inv=00000000 menu_scene=00000000)
Fullhands: completion_state=1 selected=19 after 35 polls preload=2
Fullhands: series 1/7 replaced exactly; next press=2/7
```

Le passage de 1 a 35 scrutations mesure le cout deplace dans l'appui sur M,
et le remplacement est reste exact.

Et le jeu est mort quand meme :

```text
Code de l'exception : c0000005
Module defaillant   : StackHash_dd55  ->  PCH_6F_FROM_ntdll
```

Cette signature n'est pas une faute localisee : c'est une corruption du tas
detectee par le systeme. `ProcessCheat` est appele depuis le traitement
clavier, pas depuis la boucle du moteur; y creer une scene 3D et charger des
modeles depuis le disque n'est pas un point valide pour du travail driver.

**La lecon, qui complete la regle du projet** : deleguer au moteur est
necessaire mais pas suffisant. L'appel doit aussi partir de l'endroit d'ou le
moteur lui-meme l'emet. Les trois fonctions natives d'inventaire deja
utilisees (`DeleteAllItems`, `SetSelectedInvItem`, le callback Fullhands) sont
de la logique pure; `SetInventory` touche le driver graphique, et c'est ce qui
la distingue.

Le preload est entierement retire. L'adresse et la raison de l'echec restent
consignees en commentaire dans `src/gameplay_mods.cpp` pour qu'aucune
tentative future ne reprenne ce chemin sans le savoir.

### Ce qui est conserve de cette iteration

Une garde qui manquait depuis toujours : le stub et la table des lots
partagent une meme page distante (code en `0x0000`, lots en `0x0400`). La
taille emise est desormais journalisee et comparee a cette limite avant
ecriture - le journal indique `stub 622 bytes of 1024 available` - et l'appui
est refuse plutot que de laisser le code deborder sur les lots qu'il
s'apprete a lire.

### Validation

- Compilation Release x86 reussie sans avertissement.
- Compilation Debug x86 reussie sans avertissement.
- Binaire : `build\vs2026-x86\Release\HDFinalAdvancedV25_2_STABLE.exe`,
  516 096 octets, SHA-256
  `8ADDE7D504788FB5293DC67C3AFE7F015C9C505F5196E91C6B6128D173725051`.
- Comportement identique a la V25.0 valide en jeu : 7 lots, remplacement exact,
  aucune accumulation, et le figement de 156 ms de retour a la premiere
  ouverture apres M.

## 72. V25.2 / V25.3 - retour a la stabilite, puis 14 lots

### V25.2 : le preload est retire

Tout ce que la V25.1 avait ajoute au trampoline est supprime : l'appel a
`SetInventory`, la resolution du `C_game_menu` cote hote, sa signature, son
emplacement de diagnostic dans la page distante et la ligne de journal
correspondante. Le stub redevient identique a celui de la V25.0, la version
validee en jeu.

L'adresse `0x0045FDC0`, le chemin `[actor+18h]+1FCh` et la raison exacte de
l'echec restent consignes en commentaire dans `src/gameplay_mods.cpp`, au-dessus
de `series_count`, pour qu'aucune tentative future ne reprenne ce chemin sans
savoir qu'il a deja tue le jeu.

Une seule chose est conservee de cette iteration, parce qu'elle est
independante et manquait depuis le debut : le stub et la table des lots
partagent une page distante (code en `0x0000`, lots en `0x0400`), et la taille
emise n'etait jamais comparee a cette limite. Elle l'est desormais avant
ecriture, et le journal l'affiche : `stub 622 bytes of 1024 available`.

### V25.3 : le seul levier sur pour la fluidite

Le figement d'ouverture ne depend que du nombre d'objets portes, a environ
8 ms par modele reconstruit :

| Lots | Objets par appui | Figement |
|---|---|---|
| 7 | 18-19 | 156 ms mesures |
| 14 | 9-10 | ~75 ms attendus |
| 21 | 6 | ~50 ms |
| 28 | 4-5 | mesure a zero en V23 |

`series_count` passe donc de 7 a **14**. Avec les 128 lignes eligibles du
catalogue installe, la repartition par rang modulo donne 9 ou 10 objets par
lot; un catalogue standard de 91 lignes en donnerait 6 ou 7. Rien d'autre ne
change : meme rotation native, meme verification de remplacement exact, meme
garde d'ecran ferme.

Les textes derives suivent : panneau, ligne de statut, journal de remise a
zero de serie (desormais formate depuis `series_count` plutot qu'ecrit en dur)
et README.

### Validation

- Compilation Release x86 reussie sans avertissement.
- Compilation Debug x86 reussie sans avertissement.
- V25.2 : `HDFinalAdvancedV25_2_STABLE.exe`, 516 096 octets, SHA-256
  `8ADDE7D504788FB5293DC67C3AFE7F015C9C505F5196E91C6B6128D173725051`.
- V25.3 : `HDFinalAdvancedV25_3_14_LOTS.exe`, 516 096 octets, SHA-256
  `1B2F0CB033E015A9CFDF6C839FB3D9D14418E9080DD60025CF392A9F593622E9`.

Au test, le journal doit indiquer `granting series N of 14`, `rotation=1`,
`replacement exact=1`, un inventaire stable a 10 ou 11 objets (le lot plus
l'entree permanente), et une ligne `[TEST STALL]` autour de 75 ms au lieu de
156 - ou aucune, si le seuil de journalisation n'est plus atteint.

## 73. V26.0 - noclip au volant, attache fiable, et plus aucun vol de focus

### 1. Noclip en vehicule

Le refus n'etait pas un defaut mais une consequence : le trampoline de noclip
est pose sur **`C_human::Tick`** et ne s'active que si `ecx` vaut l'acteur
publie. Un vehicule est un `C_automobile`, sa fonction de tick est une autre :
le hook ne pouvait jamais le voir.

`C_player::Tick` appelle `C_human::Tick(tc)` dans toutes ses branches, y compris
quand le joueur occupe un siege. Le declencheur reste donc valable au volant :
ce qui devait changer, c'est uniquement la frame deplacee.

Le hook ne derive plus la frame de l'acteur (`mov edx,[edx+28h]`) : il lit une
frame publiee par le trainer. A pied c'est celle du joueur, au volant celle du
vehicule (`vehicule + kActorModelOffset`, validee par sa back-reference comme
le fait deja la teleportation). Le joueur assis est un enfant de cette frame et
suit la voiture sans qu'on le touche.

Point critique : les champs de physique neutralises a chaque passe
(`move_dir` `0x1AC`, `fall_speed` `0x1B8`, `falling` `0x264`, compteur de
collision `0x274`) sont des offsets **`C_human`**. Ils restent ecrits sur le
joueur et **jamais** sur le vehicule, dont la disposition memoire est
differente : les ecrire la aurait corrompu l'objet. Le vehicule ne recoit que
sa position locale et le drapeau `0x00800000`, exactement ce que le moteur
ecrit lui-meme.

Un compteur de passes est publie et journalise (`hook_passes=`), pour prouver
que `C_human::Tick` tourne bien pendant la conduite plutot que le supposer.
`SuspendedInVehicle` ne signifie plus un refus mais un vehicule non
reconnaissable.

### 2. Attache avant le lancement du jeu

`RefreshConnection` ne reelisait la fenetre que si son handle avait ete
detruit. Or attacher hde.exe pendant son demarrage - c'est-a-dire exactement ce
que fait un trainer lance en premier - peut retenir une fenetre de demarrage
qui reste un `HWND` valide pour toute la session. Tout ce qui depend de la
fenetre (declencheurs clavier, tests de premier plan, ESP) visait alors une
fenetre morte, en silence.

La fenetre est desormais reelue des qu'elle cesse d'etre une fenetre
principale visible et sans proprietaire, pas seulement quand elle est detruite.
Chaque changement de PID ou de fenetre est journalise avec la base et la taille
du module :

```text
Process: pid=... window=... module_base=... module_size=... (was pid=... window=...)
```

### 3. Plus aucun vol de focus depuis le panneau

Cocher un cheat appelait `ApplySingleCheat`, qui bloquait les entrees, emettait
un bip, appelait `SetForegroundWindow` sur H&D puis tapait `iwantcheat` suivi du
code. D'ou le basculement vers le jeu et les touches simulees au moment meme ou
l'utilisateur remplissait le panneau.

La sequence exige maintenant que le jeu soit **deja** actif et ne le met plus
jamais au premier plan. `main.cpp` conserve la demande et la rejoue des que le
joueur revient dans le jeu de lui-meme. Une demande faite par raccourci en
cours de partie s'execute donc sur la meme image qu'avant; une demande faite
depuis le panneau ne coute rien jusqu'au retour. Le statut affiche
`[ATTENTE] Demande enregistree...` au lieu d'une erreur.

C'est la generalisation du report deja applique a la purge d'invisibilite en
V23.29.

### Nettoyage, et une erreur a signaler

`PreloadInventoryModels` - le vestige de l'idee 1 abandonnee, jamais appele et
lui aussi voleur de focus - est supprime. Cette suppression a emporte
`ReadNativeMapOpen`, qui se trouvait dans la meme region; la fonction a ete
restauree depuis la version commitee (`dbaa158`), signature et offsets
`0xA0`/`0xA4` identiques a ceux encore declares dans le fichier. Elle merite un
controle au test : c'est elle qui detecte l'ecran de carte ouvert avant une
teleportation.

### Validation

- Compilation Release x86 reussie sans avertissement.
- Compilation Debug x86 reussie sans avertissement.
- Binaire : `build\vs2026-x86\Release\HDFinalAdvancedV26_0_VEHICLE_NOCLIP.exe`,
  517 120 octets, SHA-256
  `0E914B2FC44F8A9308FF321785D4266BD330D092F479FF382B7BB4E63510D9BC`.

## 74. V26.1 - le noclip vehicule passe par C_automobile::Tick

### Le compteur a tranche la V26.0

La V26.0 pariait que `C_human::Tick` continuait de s'executer pendant la
conduite. Le compteur ajoute au meme moment a repondu sans ambiguite :

```text
Noclip: activated target=vehicle actor=025DBCD0 vehicle=024D59D0 frame=0FF61700
Noclip: move target=vehicle position=(-588.157,1.102,246.890) ... hook_passes=0
```

Le vehicule etait correctement reconnu, sa frame validee, la vitesse publiee -
et la position ne bougeait pas d'un millimetre parce que le trampoline
n'executait aucune passe.

Les sources donnent la raison exacte. Dans `C_player::Tick`, le cas
`using_item->GetType()` du vehicule se termine par :

```cpp
   if(gun) gun->Tick(tc.time);
   ShootCountDown(tc.time);
}
i=0;
break;
```

Aucun appel a `C_human::Tick`. Toutes les autres branches en font un, celle du
vehicule non : au volant, le joueur ne tique tout simplement pas comme un
humain.

### Le bon declencheur

`C_automobile::Tick` est a **`0x0044E540`**, atteinte en remontant les
appelants depuis les correctifs de vitesse vehicule (`0x0044F1F3` et
`0x0044F22C`), qui se trouvent dans son corps. Prologue :

```text
0044E540: 81 EC C8 00 00 00   sub esp,0C8h
0044E546: 53 55 56            push ebx / ebp / esi
0044E549: 8B F1               mov esi,ecx        ; esi = l'automobile
0044E54C: 8B BC 24 DC 00 00 00  mov edi,[esp+0DCh] ; contexte de tick
```

`thiscall`, donc `ecx` est la voiture a l'entree, et son argument retombe a
`[esp+28h]` une fois le trampoline passe - exactement l'offset qu'utilise deja
le hook de `C_human::Tick`, ce qui rend l'extraction du temps identique.

### Deux trampolines, une seule page de donnees

Le second trampoline partage tous les emplacements de la page allouee par le
hook humain : `active`, `velocity`, `position`, `elapsed`, `body_frame`. Les
deux integrent donc la meme position avec la meme vitesse, et un seul peut
correspondre a la fois - l'emplacement `vehicle` vaut 0 a pied, et une voiture
ne tique jamais comme un humain.

Difference essentielle avec le chemin humain : **aucun champ d'acteur n'est
ecrit sur la voiture** en dehors du flottant de vitesse `0x244`, deja lu et
valide par la fonction de vitesse vehicule. Les offsets de physique
`C_human` (`0x1AC`, `0x1B8`, `0x264`, `0x274`) tomberaient n'importe ou dans
un `C_automobile`. La voiture ne recoit que sa position locale, le drapeau
`0x00800000`, et cette mise a zero de vitesse qui l'empeche d'integrer son
propre mouvement par-dessus le notre.

Le hook est restaure et sa page liberee avec celle du hook humain, apres
verification qu'aucun thread n'y execute encore du code.

### Journal

`hook_passes` devient deux compteurs distincts :

```text
Noclip: move target=vehicle ... human_passes=0 vehicle_passes=1287
```

C'est `vehicle_passes` qui doit monter au volant. S'il reste a zero, le
trampoline n'est pas atteint et la cause est ailleurs que dans la
disposition memoire.

### Validation

- Compilation Release x86 reussie sans avertissement.
- Compilation Debug x86 reussie sans avertissement.
- Binaire : `build\vs2026-x86\Release\HDFinalAdvancedV26_1_AUTOMOBILE_TICK.exe`,
  521 728 octets, SHA-256
  `9C23B651DAB0A096F073AC2ADB55430A114790899775F8FA7DA97B272DA687F7`.

## 75. V26.2 - vehicule indestructible, sans un seul hook

### Le point d'etranglement est unique

Les sources Deluxe donnent tout le mecanisme dans `C_version::HitExplode` :

```cpp
if(v_resistance && curr_version<version_list.size()-1){
   const int thresh = tab_resistance->ItemI(TAB_I_RSET_THRESH, res_set);
   if(power < thresh) return false;
   if((v_resistance -= power) <= 0){
      SetVersion(curr_version+1, true, true);
      ...
      mission.CreateActor(ACTOR_EXPLOSION, (dword)&ei);
```

Tout passe par la : le feu ennemi, les collisions, et les chutes - le code du
vehicule s'inflige lui-meme `HitExplode(this, 4000, NULL, true, ...)` a
l'atterrissage.

La fonction a ete localisee a **`0x0044CF50`** en cherchant les appels a
`CreateActor` (`0x00437EB0`) precedes de `push 0Eh` (`ACTOR_EXPLOSION`), puis en
retenant celle dont l'entree reproduit exactement la garde de la source :

```asm
0044CF59: 8B 46 6C   mov eax,[esi+6Ch]    ; v_resistance
0044CF5C: 85 C0      test eax,eax
0044CF5E: 0F 84 ...  je 0044D289          ; -> return false
0044CF64: 8B 4E 5C   mov ecx,[esi+5Ch]    ; version_list.begin
0044CF72: 8B 43 08   mov eax,[ebx+8]      ; version_list.end
0044CF77: C1 F8 02   sar eax,2            ; size()
0044CF7A: 8B 4E 70   mov ecx,[esi+70h]    ; curr_version
0044CF7D: 48         dec eax
0044CF7E: 3B C8      cmp ecx,eax
0044CF80: 0F 83 ...  jae 0044D289         ; -> return false
```

D'ou la disposition : `v_resistance` en `+0x6C`, `curr_version` en `+0x70`,
`version_list` en `+0x58` (begin `+0x5C`, end `+0x60`).

### La correction tient en un entier

Une resistance nulle fait sortir `HitExplode` **avant** toute soustraction et
**avant** la creation de l'acteur explosion. Il suffit donc d'ecrire 0 dans
`vehicule+0x6C`.

Aucun trampoline, aucune signature a poser, aucun objet possede : exactement la
famille d'interventions qui n'a jamais fait tomber le jeu. La valeur d'origine
est memorisee et remise en place au decochage, et seulement si le champ vaut
encore le zero qu'on y avait mis - le moteur peut legitimement l'avoir rearme
par `AddResist`.

### Validation de disposition a chaque passe

Avant la moindre ecriture, la cible doit encore prouver qu'elle est un
vehicule : type `ACTOR_AUTOMOBIL` ou `ACTOR_AUTO_CANNON`, resistance dans une
plage plausible, vecteur `version_list` coherent et index courant contenu
dedans. Un acteur libere ou recycle echoue ce controle et n'est jamais ecrit.

La protection suit le vehicule conduit, et **continue de s'appliquer a celui
qu'on vient de quitter** tant qu'il valide : sans cela, sauter d'une voiture en
pleine chute la ferait exploser a l'instant ou le siege cesse de nommer le
joueur.

### Validation

- Compilation Release x86 reussie sans avertissement.
- Compilation Debug x86 reussie sans avertissement.
- Binaire : `build\vs2026-x86\Release\HDFinalAdvancedV26_2_INDESTRUCTIBLE_CAR.exe`,
  523 264 octets, SHA-256
  `76885C28E6F84CE0D5DF9C8DF41B990BC107AAC9780C72155768184959D3A54C`.

## 76. V26.3 - le plantage du noclip vehicule, et la voiture qui s'epave sans exploser

### 1. Le plantage : un entier lu comme un pointeur

La V26.1 accrochait `0x0044E540` en supposant, comme pour `C_human::Tick`, que
son premier argument etait un `S_tick_context`. C'est faux. La fonction est
`C_automobil::Tick1(int time, byte net_route_bits)` et son argument **est** la
duree en millisecondes. Le desassemblage le montre sans ambiguite :

```asm
0044E54C: mov edi,[esp+0DCh]   ; premier argument
0044E566: sub ebx,edi          ; arithmetique
0044E58C: add eax,edi          ; arithmetique
```

Le stub faisait `mov eax,[esp+28h]` puis `mov eax,[eax]` : il dereferencait un
entier de l'ordre de 30. D'ou la sortie immediate du jeu des l'activation du
noclip au volant.

Le dereferencement est retire; la valeur est utilisee directement, avec le meme
plafond de 50 ms que le chemin humain. Le test `jz` devient `jle`, qui couvrait
deja le cas nul.

### 2. L'epave sans explosion

Mettre `v_resistance` a zero ne ferme que la voie de l'explosion. Le code de
collision du vehicule a deux branches :

```cpp
if(curr_speed > destroy_speed){
   if(curr_speed > explode_speed) HitExplode(this, 4000, NULL, true, 1, bits);
   else                           Destroy(false, true, 1, bits);
}
```

Une chute assez violente pour detruire mais trop lente pour exploser prend la
seconde branche et appelle **`Destroy` directement**, sans jamais passer par
`HitExplode`. L'eau et le champ de mines l'appellent aussi directement. C'est
exactement ce que decrit le rapport : plus d'explosion, mais la voiture
s'epave et la mission d'escorte echoue.

`C_automobil::Destroy(bool, bool, dword, byte)` est a **`0x0044FC80`**, lue au
site d'appel `0x0045176E` qui suit immediatement le `call 0044CF50` deja
identifie comme `C_version::HitExplode` - ce qui confirme au passage cette
premiere adresse.

Un trampoline de quatorze octets y est pose :

```asm
9C              pushfd
3B 0D <slot>    cmp ecx,[vehicule protege]
75 04           jne native
9D              popfd
C2 10 00        ret 10h        ; sortie avant tout le corps
9D              popfd          ; native
<6 octets d'origine>
E9 <retour>
```

Rien n'est ecrit, rien n'est possede : pour la voiture protegee la fonction
n'est simplement jamais executee, donc `mode` ne devient jamais
`MODE_DESTROYED` et `MotorOn(false)` n'est jamais appele. La voiture reste
pilotable.

Le deplacement du saut est calcule par le code plutot que compte a la main :
la premiere version tombait un octet trop loin et aurait saute le `popfd`,
laissant les drapeaux sur la pile.

### Validation

- Compilation Release x86 reussie sans avertissement.
- Compilation Debug x86 reussie sans avertissement.
- Binaire : `build\vs2026-x86\Release\HDFinalAdvancedV26_3_NOCLIP_FIX.exe`,
  525 312 octets, SHA-256
  `8A8E020947014E6FC9497A95D6E4F98E484DBF75290FA48E03A9C2E18DDC7113`.

## 77. V26.4 - la voiture bouge en temps reel

### Le symptome designait la cause

Le rapport est precis : au volant, le noclip deplace bien quelque chose, mais la
voiture reste affichee a sa place, et elle se retrouve d'un coup a la nouvelle
position au moment ou l'on decoche. La position etait donc bien ecrite; ce qui
manquait, c'etait la reconstruction de la matrice monde.

Le trampoline se contentait d'ecrire la position locale et de poser le bit
`0x00800000` a `frame+0x0C`. Sur le chemin humain cela suffit, parce que le
corps de `C_human::Tick` s'execute ensuite et consomme ce bit. Le corps de
`C_automobil::Tick1`, lui, ne le consomme pas pour un vehicule pilote de
l'exterieur : le bit restait en attente jusqu'a ce que la physique native
reprenne au decochage, d'ou le saut.

### La sequence complete du moteur

Le code de teleportation, deja valide en jeu sur des vehicules, reproduit
exactement ce que fait `Vehicle.cpp` pour deplacer une voiture :

```text
frame->SetPos(destination)
I3D_frame::Update(frame)            // i3d2.dll +0x34D0
scene->SetFrameSector(frame)        // vtable de la scene +0x60
```

`I3D_frame::Update` teste precisement le bit qu'on posait :

```asm
8B 4C 24 04   mov ecx,[esp+4]        ; le frame, __stdcall
83 EC 0C      sub esp,0Ch
8B 41 0C      mov eax,[ecx+0Ch]
A9 00 00 80 00 test eax,00800000h    ; le drapeau du trampoline
```

Le trampoline vehicule execute maintenant les trois etapes a chaque passe :
l'ecriture de position tient lieu de `SetPos`, puis `Update` reconstruit la
matrice monde, puis la scene relie le frame a son secteur BSP. La scene de
mission est republiee a chaque image dans un emplacement partage, lue comme le
fait la teleportation (`entity_list + 0x10`); a zero, la reliaison est
simplement sautee.

L'adresse d'`Update` est verifiee par signature dans l'`i3d2.dll` installee
avant que le hook soit pose - un echec refuse le vol en vehicule au lieu
d'appeler une adresse non verifiee.

### Validation

- Compilation Release x86 reussie sans avertissement.
- Compilation Debug x86 reussie sans avertissement.
- La constante `kI3dFrameUpdateRva` locale a la teleportation est remontee au
  niveau du fichier : les deux chemins utilisent desormais la meme.
- Binaire : `build\vs2026-x86\Release\HDFinalAdvancedV26_4_LIVE_VEHICLE.exe`,
  525 824 octets, SHA-256
  `D28C4F91E9382F93303A3371558D111DD8B230A5AE39CE83754ECBCCE1FE6781`.

## 78. V26.5 - la voiture rapide redevient pilotable

Multiplier la vitesse sans toucher au reste laissait deux constantes du moteur
inchangees, et ce sont elles qui rendaient la conduite impossible.

### 1. La direction

`cbProc(CB_USE_AUTO, 4)` fait `SetWheelTurn(IntAsFloat(prm2) * 0.6f)`, et
`wheel_turn` s'accumule vers sa butee +-1. Le joueur envoie
`(PI/2) * tc.time * .001` par image, soit environ 0,028 apres le facteur 0.6 :
il faut donc plus d'une seconde pour atteindre la butee. A 40x, la voiture a
deja traverse la carte pendant ce temps - d'ou l'impression de ne plus pouvoir
tourner.

Le facteur est une instruction unique :

```asm
004552E9: D8 0D C0 66 4F 00   fmul dword ptr ds:[004F66C0h]   ; 0.6f
004552EF: D9 86 68 02 00 00   fld  dword ptr [esi+00000268h]  ; wheel_turn
```

Un trampoline de 33 octets y compare `esi` a la voiture publiee : elle seule
recoit `0.6 * multiplicateur` (plafonne a 12.0, au-dela la butee est atteinte
en une image), tout autre vehicule garde le `0.6f` du moteur. Le trafic geré
par l'IA braque donc exactement comme avant.

### 2. Le freinage

`case 2` fait :

```cpp
SetSpeed(Max(0.0f, speed - tab->ItemF(TAB_F_AUTO_BRAKE) * prm2 * .001f), ...)
```

`TAB_F_AUTO_BRAKE` est une constante de table, non multipliee. Une voiture
lancee a 40x met donc 40 fois plus longtemps a s'arreter, et B ne faisait que
baisser le plafond en laissant la vitesse courante redescendre toute seule.

Baisser le multiplicateur reduit desormais la vitesse courante dans le meme
rapport : le trainer relit le flottant `+0x244`, ecrit
`vitesse * (nouveau / ancien)` et le journalise. Un appui sur B est donc senti
immediatement, et revenir a 1.0x rend la vitesse native au lieu d'y tendre.
C'est une simple ecriture sur le champ que la fonction de vitesse lit et valide
deja.

### Portee

Le crochet de direction est optionnel : s'il ne peut pas etre pose, la vitesse
continue de fonctionner et le journal l'indique, plutot que de perdre toute la
fonctionnalite. Il est retire et sa page liberee avec le reste de la vitesse
vehicule.

### Validation

- Compilation Release x86 reussie sans avertissement.
- Compilation Debug x86 reussie sans avertissement.
- Deplacement du saut du stub verifie par simulation : 12, de +9 vers +21.
- Binaire : `build\vs2026-x86\Release\HDFinalAdvancedV26_5_HANDLING.exe`,
  527 872 octets, SHA-256
  `D9A74148539A437834295C845E5DACF413488D5C2597BBA20C035E07BB171C67`.

## 79. V26.6 - Destroy etait inlinee, et la direction reprend son propre reglage

### 1. Pourquoi la voiture etait encore comptee detruite

Le journal montre que les deux protections etaient bien armees :

```text
Vehicle invulnerability: holding vehicle=0BB4F038 original resistance=2050 at +6C
Vehicle invulnerability: Destroy hook installed at 0044FC80 protecting 0BB4F038
```

Il n'y a pourtant pas eu d'explosion, et la voiture a quand meme ete comptee
detruite. La cause est visible dans le binaire.

Le champ d'etat est `mode`, en `+0x74`. Le `switch` au debut de `Destroy` le
prouve :

```asm
0044FCA2: 8B 46 74      mov eax,[esi+74h]
0044FCA5: 83 F8 04      cmp eax,4
0044FCA8: 77 07         ja  0044FCB1
0044FCAA: FF 24 85 ...  jmp [eax*4+0044FF64h]     ; cinq cas : 0..4
```

Cinq etats, donc `MODE_DESTROYED` = 4, exactement comme l'enumeration des
sources. Et l'ecriture `mode = 4` apparait a **quatre** adresses :

```text
0044FEE6   dans Destroy elle-meme (couverte par le crochet)
00450976   \
00452BEE    >  Destroy inlinee par le compilateur
00454805   /
```

`C_automobil::Destroy` est definie dans la classe, donc implicitement `inline` :
le compilateur en a garde une copie hors ligne pour neuf sites d'appel et en a
integre trois autres. Accrocher la copie hors ligne ne pouvait donc pas suffire,
et le `3` que j'avais d'abord pris pour `MODE_DESTROYED` etait en realite le
`MODE_STOP` du `MotorOn(false)` inline juste avant.

La V26.6 epingle le champ lui-meme : le dernier `mode` observe pendant que la
voiture etait intacte est memorise, et remis en place des que quoi que ce soit
la marque detruite. Cela couvre les quatre sites, y compris ceux qu'on n'aurait
pas trouves. Le crochet sur `Destroy` est conserve : lui seul evite aussi la
coupure du moteur et l'extinction des roues.

### 2. La direction reprend son propre reglage

Lier le taux de braquage au multiplicateur de vitesse donnait `rate=12.00` a
80x - la butee etait atteinte en une seule image, d'ou une voiture qu'on ne
pouvait plus doser, seulement faire pivoter.

Le facteur devient un reglage a part entiere, independant de la vitesse :
curseur **Sensibilite de direction**, de 1.0x (braquage d'origine du jeu) a
8.0x, par defaut 2.0x. Il multiplie le `0.6f` de `0x004552E9` pour la seule
voiture conduite; le trafic gere par l'IA garde le taux du moteur.

### Validation

- Compilation Release x86 reussie sans avertissement.
- Compilation Debug x86 reussie sans avertissement.
- Binaire : `build\vs2026-x86\Release\HDFinalAdvancedV26_6_MODE_GUARD.exe`,
  528 896 octets, SHA-256
  `E3E65879153EF87A6BD59D99DE60A4658CC8225BA71573EB88BBD788DC1DA770`.

## 80. V26.7 - la sensibilite de direction passe sur U et I

Le curseur imposait de basculer vers le trainer pour regler une valeur qui ne
se juge qu'au volant. Il est supprime du panneau et remplace par deux touches
physiques, lues exactement comme N et B :

```text
I  (scan 0x17)  direction plus vive
U  (scan 0x16)  retour vers le braquage d'origine
```

Memes positions physiques en AZERTY et en QWERTY, pas de 0.5x par appui, plage
inchangee de 1.0x a 8.0x. Le reglage reste rattache a la case **Super vitesse
vehicule** : il ne repond que lorsqu'elle est cochee et qu'un vehicule est
conduit, comme N et B.

Plus aucune valeur de sensibilite n'est affichee dans l'application; le
panneau ne garde qu'une ligne d'aide. Chaque changement reste tracable dans le
journal :

```text
Vehicle: steering sensitivity 2.0x -> 2.5x (U/I pressed).
```

### Validation

- Compilation Release x86 reussie sans avertissement.
- Compilation Debug x86 reussie sans avertissement.
- Binaire : `build\vs2026-x86\Release\HDFinalAdvancedV26_7_STEERING_KEYS.exe`,
  529 408 octets, SHA-256
  `CED5DC36F996817D7AC5B771C0E927588AA0582F0CF33944AF17CB9088AFCB71`.

## 81. V27 - la visibilite ennemie en partie reseau

Constat du joueur : en LAN a deux, l'invisibilite marche parfois et parfois
l'ennemi tue. Deux facons de jouer, heberger ou rejoindre. Le diagnostic a
trouve trois causes distinctes, pas une panne aleatoire.

### 1. L'autorite de simulation appartient a l'hebergeur

`hde.exe` garde son role reseau dans deux variables de sa propre table de
commandes (entrees `.rdata:0x004F9E98` et `0x004F9EA8`) :

```text
net_host   octet a 0x0050ABE4   (RVA 0x0010ABE4)
net_join   octet a 0x0050ABE5   (RVA 0x0010ABE5)
net_num    int32 a 0x0050ABE8
net_player char* a 0x0050ABF0
```

Ce sont bien des etats vivants, pas des drapeaux de ligne de commande :
quatre sites (`0x0047C1B3`, `0x0048E98A`, `0x0049DD90`, `0x004A6601`) remettent
les deux a zero en quittant une session, et le moteur branche dessus en
`0x004713AD` et `0x004716EA` pour choisir le chemin hote ou le chemin client.

Le point decisif est en `0x004731BB` :

```text
A0 E5 AB 50 00      mov al,[net_join]
84 C0               test al,al
0F 84 1D 02 00 00   jz  +0x21D        ; saute la simulation autoritaire
```

Quand le processus a *rejoint* une partie, il prend une sortie courte et
n'execute pas les ~541 octets qui font tourner la simulation. La perception et
les attaques ennemies sont donc decidees sur le PC de l'hebergeur. Un trainer
lance seulement chez le client ne peut pas aveugler cette IA-la. C'est la
raison exacte du « parfois ca marche, parfois l'ennemi me tue » : ca marchait
quand le joueur hebergeait.

### 2. Le joueur protege changeait plusieurs fois par seconde

L'acteur local etait choisi par `actor + 0x2B8 == 1`, et **le dernier acteur
correspondant gagnait**, sans arret de boucle. Le drapeau n'est pas toujours
porte par exactement un acteur : un releve du probe montre trois acteurs de
type joueur avec `active_player = 0`, et le journal montre 14 adresses
protegees differentes sur une seule session, dont deux a 0,7 seconde
d'intervalle :

```text
14:03:56.870  protected=02559820 scope=0
14:03:57.580  protected=0255ACD0 scope=0
```

Chaque changement demontait le hook et le reconstruisait. Entre les deux, plus
aucun filtre : le joueur redevenait perceptible.

### 3. Le demontage etait inutile en portee escouade

Seuls les trampolines `ControlledPlayer` encodent l'adresse protegee en
immediat. Ceux de `WholeSquad` comparent le type d'acteur (`[ecx+0x1C] == 1`)
et ne referencent jamais cette adresse. Reconstruire le hook parce que
l'acteur controle avait change etait donc du pur gaspillage, avec une fenetre
sans protection a chaque fois.

### Correctif

- `RadarSnapshot` transporte `network_role` (`Offline` / `Host` / `Client`),
  lu depuis `net_host` / `net_join` via `module.base_address + RVA`.
- Selection du joueur local stabilisee : un seul candidat est accepte tel
  quel; s'il y en a zero ou plusieurs, le choix precedent est conserve tant
  qu'il reste un acteur joueur vivant. Plus de bascule d'une image a l'autre.
- En session reseau, la portee effective est forcee sur `WholeSquad`. Elle
  couvre l'escouade **et** le joueur humain distant, qui est aussi un
  `C_player`, et ne depend plus de l'acteur controle.
- Le demontage sur changement d'adresse protegee ne s'applique plus qu'a la
  portee `ControlledPlayer`, la seule qui en depende.
- Statuts distincts : `ActiveNetworkHost` quand ce PC heberge,
  `HostAuthorityRequired` quand il a rejoint. Le panneau ne dit plus
  « actif » quand l'IA tourne ailleurs.
- Les deux pastilles de portee sont remplacees par une pastille fixe en
  reseau, la portee n'y etant plus un choix.

### Ce que le correctif ne peut pas faire

Si vous rejoignez la partie d'un ami, l'IA est calculee chez lui. Pour un
comportement identique au solo des deux cotes, **l'hebergeur doit lancer le
trainer** : sa portee escouade couvre alors les deux joueurs.

### Validation

- Compilation Debug x86 reussie sans avertissement.
- Compilation Release x86 reussie sans avertissement.
- Binaire : `build\vs2026-x86\Release\HDFinalAdvancedV27_NETWORK_ROLE_V1.exe`,
  4 495 872 octets, PE32 / i386 / GUI, SHA-256
  `538CD319862721EDA18C4ADEDB509659E8CAA010979FA42EBD0B28758FF7FD81`.
- Chaines de statut, format de journal et titre V27 verifies dans le binaire.

## 82. V27.2 - le premier test LAN invalide la detection par variables console

Le test reel a tranche, et pas dans le sens prevu. Pendant la partie reseau,
le journal a ecrit une seule ligne :

```text
network role=offline scope=1 player_actors=4 active_flags=1
```

puis des poses de hook en `scope=0` jusqu'a la fin. Lecture directe de la
memoire du jeu pendant que le processus tournait encore :

```text
0050ABE4 = 00   net_host
0050ABE5 = 00   net_join
0050ABE8 = 01   net_num
```

Conclusion : **le menu multijoueur n'ecrit pas ces variables**. Elles ne sont
assignees que par la console et la ligne de commande. La detection de la V27
ne se declenchait donc jamais, et la portee restait sur `ControlledPlayer` :
un seul soldat protege sur quatre, et aucune adaptation reseau.

### Fausse piste ecartee : le port UDP

`hde.exe` tient le port UDP 2302 sur toutes les interfaces. Tentant comme
signal, mais **faux** : un releve d'une session solo anterieure montre
exactement les memes liaisons. Le port est ouvert au demarrage, en solo comme
en reseau. Ce detecteur a ete ecrit puis retire avant compilation.

### Correctif : supprimer la dependance au role

Plutot que de continuer a deviner le role, la V27.2 supprime le besoin d'en
avoir un. La portee est desormais **fixe sur escouade**, dans tous les modes :

- Elle ne reference aucune adresse d'acteur, donc plus aucune reconstruction
  de hook quand le joueur controle change - la cause des 14 adresses
  protegees relevees dans une seule session.
- Elle compare le type d'acteur, donc elle couvre l'escouade entiere **et**
  tout joueur humain connecte, qui est aussi un `C_player`.
- Elle satisfait la demande de design faite plus tot : plus de choix a la
  main dans les cheats, valeur fixee par defaut.

Les deux pastilles de portee disparaissent du panneau et les deux boutons
radio de la fenetre de debug aussi. `net_host` / `net_join` restent lus et
journalises comme indice, sans rien piloter.

### Ce qui reste vrai et non corrigeable ici

Si vous **rejoignez** la partie d'un ami, l'IA est calculee chez lui
(branchement `0x004731BB`, sortie courte cote client). Aucun patch pose
uniquement sur votre PC ne peut l'aveugler. L'hebergeur doit lancer le
trainer.

### Validation

- Compilation Debug x86 reussie sans avertissement.
- Compilation Release x86 reussie sans avertissement.
- Binaire : `build\vs2026-x86\Release\HDFinalAdvancedV27_2_SQUAD_FIXED.exe`,
  4 495 360 octets, SHA-256
  `4FE2005DEE573541C2AF3F594AA3992078728355ADAF19FD5A98B6AF7998C56D`.
- Non verifie en jeu : un second test LAN reste necessaire.

## 83. V28 - la vraie cause : le filtre etait retire pendant les blocages reseau

Le journal instrumente de la V27.3 a tranche en une session. Extrait :

```text
17:12:01.543  HEARTBEAT: visual_applied=1 live=1 fn=0042A260 first=E9 | hearing live=1
17:12:02.657  REMOVING visual hook function=0042A260 protected=02612690 scope=0
17:12:02.726  status -> waiting (checkbox=1 scope=0 snapshot=0 player=00000000)
17:12:12.915  FULLHANDS ... mission=0258C980 player=02612690
```

Une seconde apres un battement parfaitement sain, les deux hooks sont retires.
La raison est `snapshot=0` : `ReadRadarSnapshot` a echoue plus de deux secondes,
donc `main.cpp` a passe `nullptr`, et la branche `WaitingForMission` de
`UpdateEnemyInvisibility` restaurait les prologues. Dix secondes plus tard le
jeu est toujours dans sa mission avec le meme joueur - rien n'etait casse cote
jeu, seul le trainer avait laché.

La cause du trou de lecture est dans le meme journal :

```text
[TEST STALL] game thread stalled 2187 ms
```

Le thread du jeu se bloque au-dela de deux secondes. C'est rare en solo et
frequent en reseau, ou la machine attend l'autre. La periode de grace de
2000 ms de `main.cpp` etait donc franchie regulierement **en reseau et
seulement en reseau** - ce qui explique exactement « en solo aucun probleme,
en reseau l'ennemi me tue », hebergeur compris.

### Correctif

La branche sans mission ne restaure plus rien tant que le processus est le
meme et que les hooks sont poses. Elle rapporte `HeldThroughMissionGap` et
laisse le filtre en place.

C'est sur : aucun des deux trampolines ne touche a quoi que ce soit appartenant
au trainer pendant le trou. Le variant `ControlledPlayer` compare une valeur de
pointeur sans la dereferencer, le variant `WholeSquad` lit l'acteur que le jeu
lui a passe. Les prologues ne sont restaures que si la case est decochee ou si
le processus change.

### Diagnostic conserve

- Battement toutes les 5 s relisant les octets reels des deux fonctions.
- Chaque transition d'etat, avec case a cocher, portee et snapshot.
- Pose, retrait et perte de patch journalises, y compris le hook visuel qui
  etait totalement muet jusqu'a la V27.3.

### Regression corrigee

Les deux pastilles de portee, supprimees a tort en V27.2, sont retablies dans
le panneau et dans la fenetre de debug.

### Validation

- Compilation Debug x86 reussie sans avertissement.
- Compilation Release x86 reussie sans avertissement.
- Binaire : `build\vs2026-x86\Release\HDFinalAdvancedV28_HOLD_FIX.exe`,
  4 497 920 octets, SHA-256
  `19AF9BDB734DCC6159196540522B73913F0D5DC22A75A5A5D45E11F40B516557`.

## 84. V29 - le snapshot mourait pour le reste de la mission

La V28 a tenu sa promesse : pendant 70 secondes le journal repete

```text
HOLDING through mission gap fn=0042A260 first=E9 live=1 scope=0
```

Les hooks restent poses. Mais le meme journal montre que ce n'etait pas un
blocage passager :

```text
17:17:49  status -> held-through-mission-gap (snapshot=0 player=00000000)
17:18:59  HOLDING ... (toujours)
```

Soixante-dix secondes sans snapshot, et une ligne `FULLHANDS` au milieu avec
`radar=0`. Ce n'est pas un a-coup de deux secondes : `ReadRadarSnapshot`
echouait en continu.

### Pourquoi le filtre ne protegeait plus rien malgre `live=1`

Deux consequences, invisibles jusque-la :

- `NeutralizeEnemyAwareness` ne tourne jamais sans snapshot. Les attaques et
  les entrees de surveillance deja enregistrees ne sont donc plus purgees.
- En portee `ControlledPlayer`, l'adresse protegee est figee dans le
  trampoline. Si l'acteur du joueur change - une reapparition le reloge - le
  hook protege une adresse morte et le joueur reel est parfaitement visible.

C'est exactement le cas teste : case « joueur actuel » cochee.

### La cause du blocage de lecture

`RadarReadState` existait depuis longtemps mais n'etait **jamais journalise**.
Le suspect est `LocalPlayerUnavailable` : la selection exige `actor + 0x2B8 == 1`,
or le jeu ecrit lui-meme cet octet (`mov [esi+2B8],bl` en `0x0042A383`) et un
releve du probe montre trois acteurs joueurs avec le drapeau a zero en meme
temps. Si en plus l'adresse memorisee disparait, plus aucun candidat n'existe
et la lecture echoue definitivement.

### Correctif

- Chaque etape de lecture est nommee dans le journal
  (`Radar read state -> local-player-unavailable`), avec un recensement
  toutes les 5 s : acteurs, acteurs joueurs, drapeaux, adresse memorisee.
- La selection ne peut plus mourir. Ordre : un seul drapeau, sinon l'adresse
  precedente si elle vit encore, sinon le plus proche de la derniere position
  connue, sinon l'unique acteur joueur. La position est retenue justement
  parce qu'une reapparition change l'adresse mais pas l'endroit.

### Validation

- Compilation Debug x86 reussie sans avertissement.
- Compilation Release x86 reussie sans avertissement.
- Binaire : `build\vs2026-x86\Release\HDFinalAdvancedV29_PLAYER_RECOVERY.exe`,
  4 499 968 octets, SHA-256
  `B79113169B75FE970798C3EDC9FB90DC8F59DE7325344808E720E259AFC760A2`.
- Non verifie en jeu.

## V75 - Bullet Track LAN crash correction

- Crash-dump evidence: `hde.exe+0x452AC` dereferenced the projectile's cannon
  sender after the first LAN shot. It contained `0x00004E20`, the injected
  damage value, rather than a valid actor pointer.
- The constructor maps `S_gun_shoot_init+0x34` to that sender. It maps
  `S_gun_shoot_init+0x3C` to the field later passed as `CB_HIT` damage.
- V75 changes only the lethal write to `+0x3C`; the existing `+0x20` delay-zero
  write remains unchanged. Bullet Track, V2 and rapid fire remain enabled.
- Both x86 Release and Debug builds completed successfully. Host/Client helper
  executables are not involved in this correction.

## V76 - LAN player scope and safe Client death mirror

- The bazooka Client crash dump executed at `remote+0x1B` inside the Client
  helper's `C_player::Explode` death-guard trampoline. That hook is removed;
  the existing local host protection still handles explosive damage.
- The UDP helper command is now version 3 and carries stable actor network
  ids. Raw actor addresses cannot cross PCs. The Client applies visual masking
  and the optional death mirror only to that selected id. Id zero explicitly
  means the user chose **Escouade entiere** for the visual W mask.
- The two scope pills are visible even while the checkbox is off and the
  default label is **Joueur actuel (defaut)**.
- A completed host F12 native revive sends a monotonic sync sequence to the
  Client. It is intentionally limited to resynchronisation; it never invokes
  the removed explosive trampoline.
- Build outputs: `HDFinalAdvancedV76_LAN_PLAYER_SCOPE_SYNC.exe`,
  `HD_AI_AUTHORITY_HOST_V76.exe`, `HD_AI_AUTHORITY_CLIENT_V76.exe`.

## V77 - complete explosion instruction and squad protection

- The old explosion guards copied only nine bytes of `C_player::Explode`.
  Byte nine (`0x33`) is the beginning of `xor ecx, ecx`; V77 copies all ten
  bytes (`33 C9`) and patches ten bytes with a full jump/NOP tail. This fixes
  the corrupted native path that produced the Client bazooka crash/skeleton.
- **Protection reseau totale** now has two real scopes: **Joueur actuel** and
  **Escouade entiere**. Squad scope accepts only `C_player` actors with
  `network_owner == 0`, i.e. the host's local squad. The joined player's actor
  is never protected by this option.
- When total protection is active, the host automatically enables its Client
  life mirror. Squad scope is sent as a dedicated all-host-players target, so
  the Client does not apply death/skeleton transitions to a host actor that
  the host still keeps alive.
- Build outputs: `HDFinalAdvancedV77_LAN_EXPLOSION_SYNC.exe`,
  `HD_AI_AUTHORITY_HOST_V77.exe`, `HD_AI_AUTHORITY_CLIENT_V77.exe`.

## V79 - Client damage mirror and F12 native revive sync

- Protocol V4 has a separate targeted `suppress_host_player_damage` bit.
  With total protection active, the Client now guards `C_player::Hit` in
  addition to `Die` and `Explode`; remote bullet and explosion messages cannot
  lower the selected host actor's Client-side health or begin its skeleton.
- With only **Retour a la vie local** checked, the host remains alive while
  the Client is allowed to display its native temporary damage/death state.
  The host's completed F12 scene rebuild sends a sequence command. The Client
  performs `C_player::SetActive(true, true)` on H&D's own tick thread for the
  matching remote actor, restoring its model and live state without a trainer
  thread call.
- Build outputs: `HDFinalAdvancedV79_LAN_REVIVE_SYNC.exe`,
  `HD_AI_AUTHORITY_HOST_V79.exe`, `HD_AI_AUTHORITY_CLIENT_V79.exe`.

## V81 - Protection reseau totale, escouade complete

- Cause corrigee : le mode **Escouade entiere** lisait les slots virtuels
  `Hit`, `Die` et `Explode` du seul soldat actuellement controle. Les soldats
  ajoutes par Ultimate Mod peuvent employer une table virtuelle differente ;
  leurs degats et morts restaient donc natifs.
- Le trainer hote enumere chaque `C_player` local (`network_owner == 0`),
  deduplique les adresses de fonctions et pose un garde verifie par entree
  distincte. Le Client applique le meme inventaire aux copies des soldats de
  l'hote, pour eviter degats, squelette et mort distants pour toute l'escouade.
- Build outputs: `HDFinalAdvancedV81_SQUAD_TOTAL_PROTECTION.exe`,
  `HD_AI_AUTHORITY_HOST_V81.exe`, `HD_AI_AUTHORITY_CLIENT_V81.exe`.

## V82 - Selection brute des joueurs locaux

- V81 dependait encore de `RadarEntity::team == Ally`; un joueur Ultimate Mod
  classe temporairement `Unknown` etait absent de la protection d'escouade.
  V82 enumere tous les acteurs du snapshot puis retient exclusivement le type
  brut `C_player` avec `network_owner == 0`.
- **Retour a la vie local** ignore desormais la pastille d'escouade conservee
  dans l'interface et maintient uniquement le joueur actuellement controle.
- Un heartbeat indique `local_players`, `remote_players` et le nombre exact de
  gardes Hit/Die/Explode afin que le test LAN soit mesurable.
- Build outputs: `HDFinalAdvancedV82_SQUAD_ACTOR_FIX.exe`,
  `HD_AI_AUTHORITY_HOST_V82.exe`, `HD_AI_AUTHORITY_CLIENT_V82.exe`.

## V83 - Immunite native par instance C_player

- Le test LAN V82 a invalide l'hypothese des vtables multiples : le journal
  comptait bien `local_players=3`, `remote_players=1` et un seul exemplaire
  vivant de chacun des trois hooks Hit/Die/Explode pendant toute la portee
  escouade. La selection des acteurs et la tenue des hooks n'etaient donc plus
  la cause du defaut restant.
- La source officielle et le binaire Ultimate installe ont ete confrontes :
  `C_player::cbProc(CB_CHEAT, 14)` a `0x0042CEDB` bascule exactement l'octet
  `C_player + 0x2D4`; le chemin `CB_HIT` a `0x0042D3C7` lit ce meme octet et
  retourne avant tout degat lorsqu'il vaut 1. C'est le champ natif
  `C_player::no_hit_cheat` du code source, utilise par le cheat officiel
  `immortality`.
- La protection totale applique et relit maintenant cette immunite native sur
  chaque instance locale selectionnee : un acteur en portee Joueur actuel, ou
  tous les `C_player` avec `network_owner == 0` en portee Toute l'equipe.
- La valeur originale de chaque instance est memorisee puis restauree quand la
  portee change ou quand la protection est decochee. Ainsi le trainer ne coupe
  pas un cheat officiel qui etait deja actif avant lui. Une adresse devenue
  invalide a la fin de mission n'est jamais reecrite.
- Les hooks existants restent actifs en couches complementaires. C'est
  necessaire parce que le code source prouve que `C_human::Explode` soustrait
  la resistance avant `CB_HIT`, tandis que `Crash` et les messages reseau
  peuvent entrer directement dans Die/Hit. Ils couvrent donc explosion fatale,
  chute/mort directe et representation distante, alors que l'octet natif
  couvre le chemin normal des balles pour chaque soldat.
- Le heartbeat escouade journalise desormais `native_no_hit=N/N`; le heartbeat
  joueur actuel journalise `native_no_hit=1`. Une version ne doit pas etre
  declaree valide en LAN si ces compteurs ne sont pas complets pendant le tir.
- Build outputs : `HDFinalAdvancedV83_NATIVE_SQUAD_IMMORTALITY.exe`,
  `HD_AI_AUTHORITY_HOST_V83.exe`, `HD_AI_AUTHORITY_CLIENT_V83.exe`.

## V84 - Verrou de session de la protection reseau

- Le test V83 a prouve `native_no_hit=3/3`, puis le journal a montre le
  retrait des hooks Hit/Die/Explode alors que `hde.exe` restait ouvert. La
  protection ne doit jamais etre demontee par un etat UI transitoire pendant
  une mission LAN.
- Apres un clic explicite d'activation, V84 garde la protection et la portee
  choisie armees pour le PID courant. Une valeur UI transitoire a zero est
  restauree automatiquement. Seul un nouveau clic explicite sur la carte
  "Protection reseau totale" desarme la fonction; fermer le trainer restaure
  toujours les octets du jeu.
- Build outputs : `HDFinalAdvancedV84_PROTECTION_SESSION_LOCK.exe`,
  `HD_AI_AUTHORITY_HOST_V84.exe`, `HD_AI_AUTHORITY_CLIENT_V84.exe`.

## Contrat final des deux modes de vie reseau (demande utilisateur)

### 1. Protection reseau totale

- Cette case et `Retour a la vie local` sont mutuellement exclusives : activer
  l'une decoche immediatement l'autre, sur l'hote et dans le paquet de
  synchronisation Client.
- Portee `Joueur actuel` : seul le `C_player` actuellement controle par
  l'hote ne recoit aucun degat ni mort, qu'ils viennent d'un ennemi ou du
  joueur Client. Les autres soldats hote et le joueur Client restent natifs :
  degats et morts normaux.
- Portee `Escouade entiere` : la meme immunite s'applique a tous les
  `C_player` dont `network_owner == 0`; le joueur Client reste toujours hors
  de la portee.
- Le Client doit suivre le changement coche/decoche en temps reel (heartbeat
  LAN court), pas seulement au lancement de la mission.

### 2. Retour a la vie local (F12)

- Portee fixe : seulement le soldat actuellement controle par l'hote.
- Sans protection totale, les degats, barre de vie et mort restent visibles
  localement et chez le Client comme une partie native. Le jeu considere donc
  reellement le soldat mort jusqu'a F12; ce choix est necessaire pour que les
  deux PC aient exactement la meme animation et le meme etat sans fabriquer
  un faux second modele fragile.
- F12 doit restaurer simultanement le modele vivant et la vie visible chez
  l'hote et le Client. Le Client ne doit jamais rester sur une image squelette
  apres la restauration.
- Cette voie est separee de l'immunite totale : elle ne doit pas armer
  `no_hit_cheat`, ni proteger l'escouade, ni modifier les degats du joueur
  Client.

### Verification obligatoire

- Test A : protection actuelle, tirer avec le Client sur le soldat actif puis
  sur un autre soldat : seul le soldat actif survit.
- Test B : protection escouade, tirer sur chaque soldat hote : tous survivent;
  le joueur Client reste vulnerable normalement.
- Test C : aucune protection, tous les degats/morts sont natifs.
- Test D : retour a la vie seul, provoquer la mort visible du soldat actif,
  verifier la meme mort sur les deux PC, puis F12 : modele et vie reviennent
  sur les deux PC, sans squelette persistant.

## V85 - Modes exclusifs protection / retour a la vie

- La carte Protection reseau totale et la carte Retour a la vie local sont
  maintenant exclusives dans l'interface. L'activation de l'une desarme
  explicitement l'autre, y compris le verrou de session de V84.
- Retour a la vie ne maintient plus les hooks de protection totale. Les
  degats, barre de vie, mort et squelette passent donc nativement a l'hote et
  au Client; seul F12 transmet ensuite une demande de reanimation native vers
  le meme `network_id` sur le Client.
- F12 detecte la mort precedente memorisee avant le changement automatique de
  soldat et la redirige vers la reanimation deux-acteurs existante. Il ne
  reconstruit donc plus par erreur le nouveau soldat vivant.
- Build outputs : `HDFinalAdvancedV85_EXCLUSIVE_LIFE_MODES.exe`,
  `HD_AI_AUTHORITY_HOST_V85.exe`, `HD_AI_AUTHORITY_CLIENT_V85.exe`.

## V86 - Cible de garde Client stable

- Le journal V85 a isole une panne du seul executable Client : apres un
  changement de portee, il tentait de reposer une garde sur une fonction deja
  sautee vers son propre trampoline (`E9`). Il affichait alors en boucle
  `signature C_player::Die non reconnue` / `C_player::Hit non reconnue` et
  l'etat visuel de vie pouvait diverger.
- Chaque trampoline Client conserve maintenant le `network_id` cible dans une
  case de donnees modifiable. Le changement `Joueur actuel` <-> `Escouade
  entiere` met a jour cette case sans retirer ni reinstaller le hook en cours.
  `FFFF` signifie tous les joueurs hote; une autre valeur signifie le seul
  soldat cible. Les fonctions restantes sont toujours retirees proprement
  lorsqu'une classe n'est plus concernee.
- Le journal attendu en V86 ne doit plus contenir aucune ligne `signature ...
  non reconnue` pendant les changements de portee. Il ne doit afficher
  `garde ... active` qu'au premier armement d'une fonction nouvelle.
- Build outputs : `HDFinalAdvancedV86_CLIENT_GUARD_RETARGET.exe`,
  `HD_AI_AUTHORITY_HOST_V86.exe`, `HD_AI_AUTHORITY_CLIENT_V86.exe`.

## V87 - Gardes Client garees, jamais retirees en mission

- Le test suivant a montre que V86 eliminait le changement de cible mais pas
  une autre course : un paquet LAN transitoire en mode natif lancait encore
  `RestoreRemoteHostGuard`. Si un thread jouait le trampoline a cet instant,
  les octets natifs etaient deja restaures mais l'etat du helper restait
  `applied`; le rearmement suivant retombait sur une signature `E9` et
  affichait la boucle `signature ... non reconnue`.
- V87 ne retire plus aucune garde Hit/Die/Explode pendant une mission active.
  Chaque trampoline comprend maintenant trois cibles : `FFFF` = escouade
  hote, un `network_id` = joueur hote actuel, `FFFE` = garde garee. Une garde
  garee execute strictement le prologue natif et ne bloque aucun degat ou
  mort. Le changement de cases est donc immediat et natif sans modifier le
  code de la fonction en cours d'execution.
- La restauration reelle des octets et de la memoire distante reste uniquement
  reservee a la fin de la mission, a la fermeture du Client ou a la fin de
  `hde.exe`, lorsque le helper peut attendre un trampoline vide.
- Build outputs : `HDFinalAdvancedV87_CLIENT_GUARD_PARK.exe`,
  `HD_AI_AUTHORITY_HOST_V87.exe`, `HD_AI_AUTHORITY_CLIENT_V87.exe`.

## V88 - Trace LAN croisee Hote / Client

- Chaque changement de commande emis par le trainer est journalise dans le
  journal hote sous la forme `LAN->Client command`: visuel, garde mort,
  garde degat, portee (`life_id`) et sequence F12. Ainsi le journal hote
  donne l'intention exacte au moment de chaque clic.
- Le Client V88 TRACE est identifiable dans sa fenetre de console et ecrit
  aussi `HD_AI_AUTHORITY_CLIENT_V88_TRACE.log` a cote de son exe. Pour chaque
  paquet UDP valide, ce fichier capture l'adresse emettrice, la taille,
  hide, death_guard, damage_guard, `visual_id`, `life_id`, sequence F12 et
  si ces valeurs changent reellement. Une perte du signal est egalement
  ecrite avec son delai. Il devient donc possible de comparer directement ce
  que l'hote envoie avec ce que le Client applique.
- Le test V87 prouve que le trainer a retire toutes ses gardes a 20:38:07
  (`native no-hit restore entries=3 success=1`) puis que le joueur est mort a
  20:40:30. Le cas « reste protege apres decochage » doit maintenant etre
  compare aux deux traces au lieu d'etre deduit du seul rendu.
- Build outputs : `HDFinalAdvancedV88_LAN_TRACE.exe`,
  `HD_AI_AUTHORITY_HOST_V88_TRACE.exe`,
  `HD_AI_AUTHORITY_CLIENT_V88_TRACE.exe`.

## V89 - F12 reel et garde Client simplifiee

- La trace V88 prouve que les paquets recus suivent les cases du trainer :
  `1/1` arme les protections, `0/0` les desarme et `0/0 + life_id` est le
  mode Retour a la vie seul.
- `C_human::Die` est la seule garde Client qui echoue en boucle alors que les
  gardes virtuelles C_player::Die, Hit et Explode couvrent deja les joueurs.
  V89 ne pose donc plus cette garde redondante sur le Client.
- F12 est maintenant observe par le hook clavier global et journalise
  `F12 EDGE` avant de declencher le retour natif / la sequence Client.
- Build outputs : `HDFinalAdvancedV89_F12_CLIENT_FIX.exe`,
  `HD_AI_AUTHORITY_HOST_V89_TRACE.exe`, `HD_AI_AUTHORITY_CLIENT_V89_TRACE.exe`.

## V90 - Synchronisation du bon soldat apres F12

- Les traces ont montre des changements rapides de `life_id` entre les
  paquets F12 (`8260`, `8259`, `8321`, `8261`). Le Client memorisait seulement
  la sequence de reanimation puis lisait le `life_id` courant au prochain
  tick : il pouvait donc reconstruire un autre soldat que celui revele par
  l'hote.
- Chaque sequence F12 conserve desormais son propre `life_id` au moment de sa
  reception. Le Client execute `SetActive` uniquement sur ce meme acteur et
  logue `synchronisation native recue sequence=... soldat=...`.
- Build outputs : `HDFinalAdvancedV90_REVIVE_TARGET_SYNC.exe`,
  `HD_AI_AUTHORITY_HOST_V90_TRACE.exe`, `HD_AI_AUTHORITY_CLIENT_V90_TRACE.exe`.

## V91 - Protection appliquee seulement par l'hote

- Le journal hote prouve que les hooks de protection et `no_hit_cheat` sont
  correctement restaures des le decochage. Le blocage restant sur les tirs du
  joueur Client venait donc du second miroir Hit/Die/Explode pose sur son PC,
  qui n'est pas necessaire pour la sante autoritaire de l'hote.
- V91 ne pose plus de garde de degats ou de mort dans le Client. L'hote est le
  seul PC qui protege ses propres soldats quand Protection totale est cochee;
  des qu'elle est decochee, les tirs du joueur Client passent nativement sans
  trampoline Client. La console affiche `miroir Client passif` pour confirmer
  ce choix.
- La synchronisation F12 V90 est conservee : le Client garde chaque `life_id`
  associe a sa sequence de reanimation.
- Build outputs : `HDFinalAdvancedV91_HOST_ONLY_PROTECTION.exe`,
  `HD_AI_AUTHORITY_HOST_V91_TRACE.exe`, `HD_AI_AUTHORITY_CLIENT_V91_TRACE.exe`.

## V92 - Autorite de protection au niveau du protocole natif (historique invalide par les essais du 2 septembre 2026)

- Les deux PC ont confirme l'empreinte SHA-256 exacte de `hde.exe` :
  `5D5EED6174658B8FACFBC1251146B109BBB12AD9A119168AA3489F80979D62D0`.
  Les adresses de V92 ne sont valides que pour cette revision.
- L'analyse du binaire installe a identifie `C_human::NetCodeIn` a
  `0x00405FD0`, avec le prologue verifie `81 EC E4 00 00 00`. Cette entree
  recoit les messages natifs `NM_HUMAN_HIT=8` et `NM_HUMAN_DIE=5` pour les
  joueurs distants.
- Le companion Client ne hooke plus `C_player::Hit`, `C_player::Die` ni
  `C_player::Explode`. Il pose un seul trampoline sur ce recepteur de messages
  et filtre uniquement les messages de degat/mort destines aux joueurs de
  l'Hote, lorsque l'Hote a active la protection.
- Au decochage, le trampoline est gare avec la cible `FFFE` et les deux
  drapeaux a zero : chaque message passe immediatement par le prologue natif.
  Aucun octet de fonction n'est restaure ou repose pendant la mission, ce qui
  elimine la course qui pouvait laisser une garde de degat activee.
- Le Client V92 journalise `messages Hote filtres par le protocole` a
  l'activation et `messages Hote natifs` au decochage. Le fichier detaille est
  `HD_AI_AUTHORITY_CLIENT_V92_PROTOCOL.log` a cote de son executable.
- Build outputs : `HDFinalAdvancedV92_PROTOCOL_AUTHORITY.exe`,
  `HD_AI_AUTHORITY_HOST_V92_PROTOCOL.exe`,
  `HD_AI_AUTHORITY_CLIENT_V92_PROTOCOL.exe`.

### Hypothese de « correctif final » de la case Protection (invalidee le 2 septembre 2026)

- La case est l'unique source de verite. Le latch de session qui pouvait
  re-activer la protection apres un decochage a ete supprime.
- Au decochage, le trainer restaure les octets `no_hit` originaux et parque
  les gardes; le paquet LAN publie aussi `damage_guard=0, death_guard=0`.
- Le client laisse alors passer nativement `NM_HUMAN_HIT` et
  `NM_HUMAN_DIE` sans retirer ni reposer de hook a chaud.

## V93 - Vitesse du jeu (base V88 restauree)

- Point de depart : les sources exactes de V88, reconstruites depuis le journal
  de session complet. Le binaire recompile avant tout ajout ne differait de
  `HDFinalAdvancedV88_LAN_TRACE.exe` que par les 6 octets d'horodatage du PE :
  code, donnees et ressources identiques. V89 a V92 ne sont pas dans cette
  branche.
- Nouveaute unique : une case « Vitesse du jeu », reglee par les touches
  physiques 9 (augmente) et 8 (reduit), de 1.0x a 5.0x par pas de 0.5x.
- Mecanisme : la boucle principale de `hde.exe` moyenne le temps ecoule sur
  huit images, puis `sar eax,3` (RVA `0x000CD787`) suivi de
  `mov [esp+14h],eax` fixe le pas de temps que `tick_class->Tick(tc)` distribue
  a toute la simulation. Le trainer remplace ces 7 octets par un `jmp` vers un
  trampoline de 32 octets qui refait le decalage, applique
  `imul eax,[facteur]` puis `sar eax,8` (virgule fixe 8.8), borne le resultat a
  1 ms minimum, reecrit `[esp+14h]` et revient.
- La mesure reelle du temps, faite par `igraph2.dll`, n'est pas touchee : seule
  la valeur distribuee a la simulation est multipliee, donc aucune derive
  d'horloge ne s'accumule et le pas revient exactement a sa valeur native des
  que le multiplicateur repasse a 1.0x.
- Le plafond 5.0x est deliberé : le moteur borne deja une image a 100 ms
  (MIN_FPS), et 5x sur une image de 16 ms reste sous ce plafond, donc la
  physique reste dans le domaine qu'elle connait.
- Le multiplicateur retombe a 1.0x des que la case est decochee, et le
  trampoline n'est libere qu'apres verification qu'aucun thread du jeu n'y
  execute encore du code (meme protocole que les hooks vehicule).
- A eviter en partie reseau : les deux machines ne suivraient plus le meme
  temps.
- Build outputs : `HDFinalAdvancedV93_GAME_SPEED.exe`, avec les companions
  inchanges `HD_AI_AUTHORITY_HOST_V88_TRACE.exe` et
  `HD_AI_AUTHORITY_CLIENT_V88_TRACE.exe`.

## V94 - Vitesse du jeu poussee jusqu'a 100x

- Meme mecanisme que V93, meme trampoline, memes touches 9 et 8 : seul le
  plafond change, de 5.0x a 100.0x.
- Le pas est desormais gradue pour que le haut de l'echelle reste atteignable :
  0.5x en dessous de 5x, 2.5x en dessous de 20x, 10x au-dela. Vingt-deux
  pressions sur 9 vont de 1.0x a 100.0x, et la descente par 8 repasse
  exactement par les memes valeurs.
- Marge de calcul : la moyenne glissante du moteur reste dans [5, 100] ms, donc
  le pire cas de l'imul du trampoline est 100 x 25600 = 2 560 000, tres loin
  des 2 147 483 647 d'un entier 32 bits. Aucun debordement possible.
- Au-dela d'une dizaine de fois, le moteur travaille sur des pas de temps qu'il
  ne voit jamais normalement (160 ms a 10x, 1600 ms a 100x sur une image de
  16 ms) : collisions traversees, animations saccadees et scripts imprevisibles
  sont attendus. C'est assume et laisse au joueur.
- La sortie reste instantanee : le facteur est relu par le trampoline a chaque
  image, le trainer ecrit dans la memoire du jeu depuis son propre processus,
  donc 8 continue de repondre meme quand le jeu rame, et decocher la case
  restaure le pas natif exact.
- Build outputs : `HDFinalAdvancedV94_GAME_SPEED_MAX.exe`, avec les companions
  inchanges `HD_AI_AUTHORITY_HOST_V88_TRACE.exe` et
  `HD_AI_AUTHORITY_CLIENT_V88_TRACE.exe`.

## V95 - Chaque case retrouve sa logique, et les bascules deviennent instantanees

Livree le 3 septembre 2026, 17:40:27 (heure locale). Base : V94, dont rien d'autre
n'a change. Sauvegarde de l'etat precedent : `_GOLD_V94/` (binaires + sources).
Sauvegarde de cet etat : `_GOLD_V95/`. Paquet : `release/V95_SCOPE_LOGIC/`.

### Ce que le test du 3 septembre 2026 a montre

Rapport du joueur, en partie LAN avec lui comme hote :

- case « Invisible pour les ennemis » cochee, dans **les deux** portees : les
  soldats des PC amis ne meurent plus jamais, meme en portee « Joueur
  actuel » ou ils restent parfaitement visibles et pris pour cible ;
- case « Protection reseau totale » en portee « Escouade entiere » : aucun
  effet sur les soldats des PC amis, seul le soldat pilote est protege.

### Cause exacte, verifiee dans le code V94

Les deux cases avaient la meme condition ecrite a l'envers l'une de l'autre.

`UpdateEnemyInvisibilityImpl` installait, a chaque image et quelle que soit la
portee, une garde sur `C_player::Hit` avec une cible nulle
(`gameplay_mods.cpp`, appel `InstallPlayerDamageHook(process,
player_hit_function)`). La cible nulle selectionnait la branche historique du
trampoline, qui ne contient qu'un seul test :

```asm
cmp dword ptr [ecx+1Ch], 1   ; l'acteur est-il de type JOUEUR ?
jne native
xor al, al                   ; oui -> aucun degat
ret 14h
```

Aucun test de proprietaire. Sur le PC hote, les soldats locaux portent
`network_owner == 0` et ceux des amis `network_owner != 0`, mais les deux sont
de type 1. Comme le compagnon HOTE force `network_owner = 0` sur chaque ennemi,
c'est cette machine qui resout le tir contre le soldat d'un ami, appelle `Hit`
sur sa copie locale et n'emet donc jamais le paquet de degats. D'ou
l'immortalite observee, dans les deux portees, sans que l'interface l'annonce.

Les gardes de la protection, eux, portaient un test de plus :

```asm
cmp dword ptr [ecx+1Ch], 1
jne native
cmp dword ptr [ecx+34h], 0   ; proprietaire local uniquement
jne native
```

et `CollectLocalSquadVirtualEntries` ecartait les acteurs distants avant meme
de lire leur vtable. « Escouade entiere » signifiait donc « tous les soldats
que cette machine possede », jamais « tous les joueurs de la partie ».

### Correction 1 - l'invisibilite ne traite plus que la perception

- La case ne lit plus `C_player::Hit` et n'y installe plus rien.
- `RestoreEnemyInvisibility` ne touche plus `g_player_damage` : ce crochet
  appartient exclusivement a Protection reseau totale.
- Effet de bord supprime au passage : en V94, armer la protection en portee
  escouade **avant** de cocher l'invisibilite faisait echouer cette derniere
  (`[ERREUR] Fonction de perception non reconnue`), parce que le prologue de
  `Hit` etait deja remplace par un `E9`. Les deux cases ne se disputent plus
  aucune fonction, dans aucun ordre d'activation.

### Correction 2 - « Escouade entiere » couvre les joueurs connectes ou l'hote a autorite

Le partage est fait porte par porte, selon la machine qui decide reellement :

| Verrou | Joueurs distants | Raison |
|---|---|---|
| `C_player::Hit` | **couverts** | le degat est calcule ici avant son envoi reseau |
| `C_player::Explode` | **couverts** | la grenade de l'IA hote est resolue ici |
| `C_player::Die` | exclus | la mort d'un soldat distant est decidee par sa machine |
| `C_human::Die` | exclus | idem, la bloquer ici desynchroniserait les ecrans |
| chute (`falling` 0x264) | exclus | la physique d'un acteur distant est simulee par son proprietaire |
| octet natif `no_hit` 0x2D4 | exclus | etat d'instance, il ne se replique pas |
| ordre LAN `0xFFFF` | inchange | il protege les soldats de l'hote tels qu'affiches chez l'ami |

Concretement : `CollectLocalSquadVirtualEntries` retient desormais aussi les
entrees `Hit` et `Explode` des acteurs `network_owner != 0`, le test de
proprietaire a ete retire du trampoline `Hit` en portee escouade, et
`InstallDeathHook` recoit un parametre `include_remote_players` que seul le
garde `Explode` active.

Limite assumee et ecrite dans le LISEZ_MOI : une chute, une explosion
declenchee par le client lui-meme ou une mort scriptee restent hors de portee
de l'hote. Une immortalite garantie du cote ami demanderait un garde
symetrique `network_owner == 0` dans le compagnon CLIENT, qui n'existe pas.

### Correction 3 - application immediate des cases

Trois attentes ont ete supprimees.

1. **Bascule de portee.** La portee et l'acteur protege etaient compiles en dur
   dans les trampolines `IsEnemy`, donc tout changement imposait de retirer le
   crochet puis de le reposer. Comme cette operation est dangereuse pendant que
   le trainer a le focus, elle etait **reportee jusqu'au retour dans le jeu**.
   Les deux trampolines lisent maintenant leur mode et leur cible dans deux
   mots de leur propre page (`kEnemyFilterScopeOffset = 0xC0`,
   `kEnemyFilterTargetOffset = 0xC4`) : les deux chemins coexistent dans le
   code et un seul mot decide lequel est emprunte. Changer de portee, ou de
   soldat controle, coute une ecriture de 4 octets, appliquee sur-le-champ,
   sans la moindre image sans filtre. C'est le mecanisme deja eprouve par le
   compagnon CLIENT pour sa cible reseau.
2. **Retrait d'un crochet.** Chaque restauration attendait la liberation de sa
   page distante : jusqu'a 250 sondages espaces de 2 ms, et chaque sondage
   suspend tous les threads de `hde.exe`. Decocher une case pouvait donc figer
   l'interface plus d'une seconde. Les octets d'origine, eux, etaient deja
   reposes immediatement. Les pages passent maintenant par une file
   (`QueueRemotePageRelease`) videe par une seule sonde non bloquante par
   image (`ProcessPendingRemoteReleases`), et la fermeture du trainer insiste
   au maximum 100 ms pour ne rien laisser alloue.
3. **Purge des perceptions memorisees.** L'intervalle entre deux tentatives
   passe de 1000 ms a 150 ms. Elle exige toujours que le jeu soit au premier
   plan, parce qu'elle s'execute sur son propre thread; mais au retour dans le
   jeu elle part quasi instantanement au lieu d'attendre jusqu'a une seconde.
   Une bascule de portee ou de soldat rearme cette purge, ce que faisait
   auparavant la reconstruction du crochet.

### Verification

- Reconstruction complete Release x86 (`--clean-first`), zero avertissement,
  code de sortie 0.
- Verification manuelle des sauts relatifs des quatre trampolines regeneres
  (visuel, auditif, `Hit` escouade, `Explode` escouade) : chaque `rel32` tombe
  bien sur l'etiquette voulue.
- SHA-256 `HDFinalAdvancedV95_SCOPE_LOGIC.exe` :
  `C2F030F6DFB9FC1AF4188AF31A6C4A956335FE5DA644C2A561803B020722F70E`.
- Compagnons inchanges (code source identique a la V88, seuls les six
  octets d'horodatage du PE different) : `HD_AI_AUTHORITY_HOST_V88_TRACE.exe`
  (`531BAC63BE28E99BADCC0BAF8471AE19B7E48A0FEF93EE9DD10418B9C3CB76A3`) et
  `HD_AI_AUTHORITY_CLIENT_V88_TRACE.exe`
  (`6BA0A18821D0AE5881923FF166CE4A5BC74A97B5A6FF0FE2FF265FFC3DE0094C`).
- **Non teste en jeu a cette date.** Le protocole de test A a E est dans
  `release/V95_SCOPE_LOGIC/LISEZ_MOI.txt`.

### Comportement attendu apres cette version

| Reglage | Vous | Vos soldats IA | Les joueurs amis |
|---|---|---|---|
| Invisible seule, « Joueur actuel » | ignore, mortel | mortels | visibles et **mortels** |
| Invisible seule, « Escouade entiere » | ignore, mortel | ignores, mortels | ignores, mortels |
| Protection seule, « Joueur actuel » | intouchable | mortels | mortels |
| Protection seule, « Escouade entiere » | intouchable | intouchables | proteges des balles et explosions de l'IA hote |
| Les deux, « Escouade entiere » | ignore et intouchable | ignores et intouchables | ignores et proteges des memes degats |

## V96 - Re-balayage ennemi rendu a la portee, et W qui rend invisible

Livree le 3 septembre 2026 a 18:04:26 (heure locale). Base : V95.
Sauvegardes : `_GOLD_V96/` (cet etat), `_GOLD_V95/`, `_GOLD_V94/`.
Paquet : `release/V96_ENEMY_RESCAN/`. Compagnons inchanges depuis la V88.

### Defaut 1 - les amis restaient ignores des ennemis en debut de mission

Rapport du joueur, le 3 septembre 2026 : avec « Invisible pour les ennemis »
cochee en portee « Joueur actuel » des le debut d'une partie, ses amis
restaient eux aussi ignores des ennemis; un decochage suivi d'un recochage
retablissait un comportement correct pour toujours.

**Cause, prouvee par la source du moteur.** `MaintainEnemyAwarenessSuppression`
ecrivait, a chaque image et pour chaque ennemi, `enemy+0x298 = 0`. Ce champ est
`C_human::actor_enum_count` et `Actors.cpp` l'utilise ainsi :

```cpp
void WatchHumans(int time, float care_factor){
   if((actor_enum_count += time) >= WATCH_ENUM_COUNT){
      actor_enum_count = 0;
      ...
      mission.EnumActors(S_hlp::cb, (dword)&hlp);
   }
   ...
}
// S_hlp::cb :
//   if(!a->IsEnemy(t.actor)) return true;      <- notre crochet repond ici
//   ...
//   t.watch_actors->push_back(...);            <- unique push_back du fichier
```

Deux faits en decoulent :

1. l'unique `watch_actors.push_back` de tout `Actors.cpp` est situe apres le
   test `a->IsEnemy(observateur)`. Tant que le crochet `C_player::IsEnemy` est
   pose, un acteur protege **ne peut pas** etre reinscrit dans une liste de
   surveillance, meme si l'enumeration tourne librement. Le gel n'etait donc
   pas necessaire a la protection;
2. maintenir l'accumulateur a zero empeche la condition `>= WATCH_ENUM_COUNT`
   d'etre atteinte, donc l'enumeration ne se produit **jamais** : l'ennemi ne
   peut plus decouvrir aucun acteur nouveau, protege ou non. C'est exactement
   le symptome rapporte, et cela explique aussi pourquoi un decochage le
   corrigeait : pendant ce court instant l'enumeration reprenait et inscrivait
   les amis, que la portee « Joueur actuel » ne purge jamais.

**Correction.** Le gel devient propre a la portee. En « Escouade entiere » il
est conserve tel quel : tous les joueurs y sont filtres, il n'y change rien
d'observable. En « Joueur actuel », seule la liberation de `holding_fire`
(`enemy+0x296`) est maintenue et l'enumeration native reprend son cours. Le
journal expose l'etat : `rescan_frozen=0|1` dans la ligne
`Enemy invisibility: continuous awareness hold`.

**Verification a faire en jeu** : le recensement periodique
`Enemy AI census: ... watching_protected=..` doit rester a 0 pour le soldat
protege alors meme que `rescan_frozen=0`. C'est la preuve directe que le
crochet `IsEnemy` suffit sans le gel.

### Defaut 2 - « Masquer ma position reseau » et l'invisibilite

Jusqu'ici le premier W posait `settings.enemy_invisibility_enabled = true` et
ne le retirait jamais. Deux consequences non voulues : la case dediee se
retrouvait cochee sans que le joueur l'ait demande, et elle s'appliquait avec
la portee qui s'y trouvait - « Escouade entiere » le cas echeant - alors que le
masque de position ne concerne que le soldat pilote.

**Correction.** Le filtre de perception est desormais demande directement par
l'etat cache du masque :

```cpp
const bool mask_requests_filter = NetworkPositionMaskHidesLocalPlayer(process);
const bool filter_enabled =
    settings.enemy_invisibility_enabled || mask_requests_filter;
const EnemyInvisibilityScope requested_scope =
    settings.enemy_invisibility_enabled
    ? settings.enemy_invisibility_scope
    : EnemyInvisibilityScope::ControlledPlayer;
```

- Le masque n'ecrit plus jamais dans les reglages de la case dediee.
- Quand la case est cochee, c'est sa portee qui gagne : un choix explicite du
  joueur n'est jamais reduit.
- Quand seule la demande du masque est active, la portee est toujours
  « Joueur actuel », et le filtre disparait au W qui republie la position
  reelle. `UpdateNetworkPositionMask` s'executant avant
  `UpdateEnemyInvisibility` dans la meme image, la bascule est immediate.
- La carte « Invisible pour les ennemis » continue d'afficher l'etat de sa
  propre case; c'est la carte du masque qui annonce l'invisibilite en cours.
  Le journal marque `mask_driven=1` dans la ligne
  `Enemy invisibility: status -> ...`.

### Question ouverte - les amis ne voient pas les traits de balles ennemies

Analyse faite, correction **non** appliquee faute de preuve suffisante.

- La replication du tir est native et inconditionnelle : `GunShoot.cpp`
  `C_gun_shoot::Evaluate` envoie `NM_GAME_CREATE_SHOOT` des que
  `net && !network_actor`, et `S_gun_shoot_init` est memset a zero, donc un tir
  cree localement a bien `netw_actor == 0`. Cote reception,
  `GameMission.cpp:2750` recree l'acteur et le trace fumigene (`GreateSmokeRay`)
  est construit dans `Evaluate` pour les acteurs reseau comme locaux. Rien dans
  le trainer ni dans les compagnons ne touche ce chemin.
- Deux causes possibles restent, et le defaut 1 en fait partie :
  1. **le gel du re-balayage** : un ennemi qui ne decouvre personne n'a
     personne a viser, donc ne tire pas. La correction ci-dessus peut donc
     suffire a faire reapparaitre les traits;
  2. **l'autorite IA a 100 % sur l'hote** : nativement H&D repartit les ennemis
     entre les deux PC, et chaque machine dessinait localement les tirs des
     ennemis qu'elle possedait. Avec les compagnons V88, tous les ennemis
     appartiennent a l'hote, donc chaque trait doit desormais traverser le
     reseau. C'est un compromis assume de l'autorite hote, pas une regression.
- Action : retester apres la V96 avant d'instrumenter le chemin reseau.

### Verification

- Reconstruction Release x86 du trainer, zero avertissement, code de sortie 0.
  Les deux compagnons n'ont pas ete relies : leur executable etait verrouille
  par une session de test en cours, et leur code source n'a pas change depuis
  la V88. Les binaires livres sont ceux de la V95, bit pour bit.
- SHA-256 `HDFinalAdvancedV96_ENEMY_RESCAN.exe` :
  `2A30619B5C04E9B530C638D0A0E2E38E16DE8ED63371F42DFD2315B235DB5B4B`.
- **Non teste en jeu a cette date.** Protocole A a C dans
  `release/V96_ENEMY_RESCAN/LISEZ_MOI.txt`.

## V97 - Les cheats deviennent natifs, l'aimbot voit loin

Livree le 3 septembre 2026 a 19:10:09 (heure locale). Base : V96.
Sauvegarde : `_GOLD_V97/`. Paquet : `release/V97_NATIVE_CHEATS/`.
Compagnons inchanges depuis la V88 - rien a redistribuer.

### Constat de depart : deux cheats sur cinq etaient morts en reseau

`GameMission.cpp` appelle `ProcessCheat` a chaque image, sans condition de
reseau : la frappe est donc bien reconnue en LAN. Ce sont les gestionnaires qui
refusent d'agir.

| Cheat | Indice | Gestionnaire | En reseau |
|---|---|---|---|
| `ironman` | 8 | `C_player::cbProc`, `if(!net){ SetResistance(init_resistance = 20000); }` | **rien** |
| `fullhands` | 9 | `C_human::cbProc`, `if(!net) if(type==ACTOR_PLAYER)` | **rien** |
| `skipmission` | 5 | `SetGameState(MS_DONE_SUCCESS)` -> `net_sync->BeginEvent` | fonctionne, et synchronise |
| `immortality` | 14 | `no_hit_cheat = !no_hit_cheat`, aucun garde | fonctionne |
| `funnyhead` | 2 | `CHEAT_BIGHEAD`, aucun garde | fonctionne, cosmetique |

Les noms sont stockes obfusques dans `cheats2[]` : une lettre sur deux, lue a
l'envers (`"n0a1m2n3o4r5i6"` = `ironman`). La liste n'est deverrouillee qu'apres
`iwantcheat`, ce que la sequence du trainer tapait deja a chaque fois.

### Sante Max : les deux offsets de vie

Le binaire de reference `source/hde/bin/HDE.exe` (13 mai 2002) est livre avec
`HDE.map`. Ses adresses de fonctions ne sont pas celles du jeu installe, mais
la definition de classe est la meme, donc les offsets de champs sont
transposables. Le site du cheat y a ete localise par la constante 20000 :

```
004275F5  a1 20 b3 4f 00        mov  eax,[net]
004275FA  85 c0                 test eax,eax
004275FC  0f 85 9c 13 00 00     jnz  fin           ; if(net) -> ne rien faire
00427602  8b 96 9c 02 00 00     mov  edx,[esi+29Ch]
00427608  8b 4e 18              mov  ecx,[esi+18h]
0042760B  b8 20 4e 00 00        mov  eax,20000
00427610  68 00 00 80 3f        push 1.0f
00427615  52                    push edx
00427616  81 c1 fc 01 00 00     add  ecx,1FCh
0042761C  89 86 c4 02 00 00     mov  [esi+2C4h],eax  ; init_resistance
00427622  89 46 2c              mov  [esi+2Ch],eax   ; resistance
00427625  e8 78 09 03 00        call SetHealth
```

Le bloc `zombie` juste au-dessus ecrit la meme paire avec 5000, ce qui confirme
l'appariement. Trois champs deja cartographies par le trainer donnent le
decalage entre ce build et la revision Deluxe installee :

| champ | HDE.exe 2002 | Deluxe installe | ecart |
|---|---|---|---|
| `stay_mode` | 0x240 | 0x254 | +0x14 |
| `mode` | 0x2A0 | 0x2B4 | +0x14 |
| `no_hit_cheat` | 0x2C0 | 0x2D4 | +0x14 |

D'ou `init_resistance = 0x2C4 + 0x14 = 0x2D8`. `resistance`, a +0x2C, se trouve
dans l'en-tete de `C_actor`, entre `frame` (+0x28) et `network_owner` (+0x34),
deux offsets identiques dans les deux builds : il n'est pas decale.

La deduction reste une deduction. `ValidateExtendedHealthLayout` relit les deux
entiers avant toute ecriture et refuse d'agir hors de
`0 < init_resistance <= 6000` avec `0 <= resistance <= init_resistance` - la vie
native vaut `200 + endurance*1400`, donc au plus 1600. Un mauvais offset se
traduit par un statut d'erreur et **aucune ecriture**.

Comportement retenu, conforme a la demande : l'activation etend ET remplit; les
images suivantes ne retouchent plus la vie, qui descend normalement; la
desactivation rend la vie d'origine, pleine.

### Sortie forcee de vehicule (F3)

`C_human::cbProc(CB_USE_AUTO, 0, prm2)` est le chemin natif d'entree et de
sortie. Avec `prm2` nul, le moteur delie le soldat du siege, demande au vehicule
sa position de descente (`CB_USE_AUTO, 7`), teste trois positions de degagement
et repose le soldat. C'est donc la routine du jeu, pas une reimplementation.

Le rappel est joue sur le thread du jeu par le mecanisme deja eprouve de
l'ancien F3 : creneau permanent de la garde de chute quand la protection est
armee, crochet temporaire sur `C_human::Tick` sinon. Ce mecanisme a ete
generalise - message et premier parametre vivent maintenant dans deux mots de
la page (`0x18C` et `0x190`) au lieu d'etre compiles en dur.

**Garde obligatoire** : dans `case 0`, lorsque le vehicule passe a NULL, le
moteur ecrit `using_item->cbProc(...)` sans aucun test de nullite. Le trainer
verifie donc qu'un vehicule est reellement occupe avant d'appeler.

### Aimbot

Le plafond de 220 m n'etait qu'une constante d'interface : la distance est un
simple filtre au carre sur les entites du radar, et la valeur 0 y signifie deja
« aucune limite ». Plafond porte a 1000 m, case « Illimité » ajoutee qui grise
le curseur et transmet 0.

### Masquer ma position

La pastille de portee de cette carte pilote maintenant aussi l'invisibilite aux
ennemis qu'elle declenche : « Escouade entiere » couvre tous les acteurs de type
joueur, donc les soldats des PC amis egalement.

### Ce qui reste, et pourquoi

| Point | Blocage |
|---|---|
| Sante Max chez les amis | la vie appartient a la machine proprietaire : nouveau CLIENT + champ UDP |
| Position masquee des amis | leur machine publie leur position : meme nouveau CLIENT |
| F6 fenetre des vehicules | selection in-game a construire; le moteur ne cree pas de vehicule ex nihilo (`CreateActor(ACTOR_AUTOMOBIL)` sans donnees, acteur accroche a un frame deja charge par la mission) |
| G reparation de vehicule | reutilise le placement de frame de la teleportation, non encore cable |

### Verification

- Build Release x86, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV97_NATIVE_CHEATS.exe` :
  `DFE2D19E599E528EF809237DBA8D20BA8E1E5F299808690238640B634D899FDD`.
- **Non teste en jeu a cette date.** Protocole A a E dans le LISEZ_MOI.

## V98 - Sante d'equipe, position masquee partagee, vehicules

Livree le 3 septembre 2026 a 19:26:55 (heure locale). Base : V97.
Sauvegarde : `_GOLD_V98/`. Paquet : `release/V98_TEAM_HEALTH/`.
**Le compagnon CLIENT doit etre redistribue a tous les amis.**

### Protocole LAN, version 5

`PeerVisualCommandWire` passe de 18 a 20 octets et de la version 4 a la 5 :

```
magic(4) version(2) hide(1) death(1) damage(1)
extend_client_health(1) mask_client_position(1) reserved(1)
visual_id(2) life_id(2) revive(4)
```

Les deux nouveaux octets portent sur les soldats que le CLIENT possede
lui-meme. La disposition est identique des deux cotes, verrouillee par un
`static_assert(sizeof == 20)` dans chaque fichier. Un ancien CLIENT compare la
taille recue avant tout et rejette donc le paquet proprement, sans le mal
interpreter.

### Pourquoi ces deux ordres ne pouvaient pas rester chez l'hote

La vie d'un soldat et la position qu'il publie appartiennent a la machine qui
le possede. Une ecriture faite sur la copie de l'hote est soit ignoree, soit
ecrasee par la prochaine mise a jour reseau du proprietaire. Les deux
fonctions sont donc executees par le CLIENT, sur ordre.

### Sante Max chez l'ami

`UpdateClientExtendedHealth` reprend exactement la logique de l'hote : lecture
de controle (`0 < init_resistance <= 6000`, `0 <= resistance <= init`), memoire
des valeurs d'origine, ecriture de 20000/20000 au front montant, puis plus
aucune retouche pour que la vie descende normalement. Le relachement rend
`init_resistance` d'origine et remplit la barre. Le delai de securite de 1,5 s
deja present sur le canal restaure tout si l'hote se tait.

### Gel de la position publiee par le CLIENT

Port exact du crochet de l'hote, a `kHumanPositionSendRva = 0x0001BD97`,
signature `8B 4D F4 8D 83 10 02 00 00`. Le trampoline compare EBX au contenu
d'une table de seize octets par entree `{acteur, x, y, z}` et, en cas de
correspondance, reecrit les trois locaux `[ebp-10h/-0Ch/-08h]` que le code natif
serialise juste apres. L'ancre est capturee une seule fois, a l'arrivee de
l'ordre, depuis `frame + 0xBC`.

### Vehicules

`CollectMissionVehicles` parcourt le vecteur d'acteurs de la mission
(`mission+0x68/0x6C`), retient les types 16 (automobile) et 8 (canon), lit leur
frame et leur position monde, et trie par distance. Le placement reutilise
`SetFramePositionOnMainThread`, deja eprouve par la teleportation : il arrete le
vehicule par `CB_USE_AUTO(13)`, appelle `I3D_frame::SetPos`, `Update` puis
`SetFrameSector`. Le point vise est calcule depuis `heading_radians`, defini par
`atan2(direction.x, direction.z)` dans le radar, donc l'avant vaut
`(sin, 0, cos)`.

La remise en etat ecrit `curr_version = 0` (`+0x70`), remonte la resistance
(`+0x6C`) si elle etait nulle, et ramene `mode` (`+0x74`) de 4 (epave) a 3.

**Limite structurelle**, ecrite aussi dans le LISEZ_MOI : le moteur ne cree
jamais un vehicule a partir de rien. `CreateActor(ACTOR_AUTOMOBIL)` ne prend
aucune donnee et l'acteur est accroche a un frame que la carte a deja charge
(`GameMission.cpp:1759`, chargement `CT_ACTOR`). La liste ne peut donc contenir
que les vehicules presents dans la mission en cours.

L'affichage passe par la fenetre superposee existante (`SnaplineOverlay`,
GDI, `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE`), avec les memes
primitives de texte que la notification du masque de position. La navigation
lit les fleches, Entree et Echap uniquement lorsque la liste est ouverte et que
le jeu est au premier plan. La touche G, etant une lettre, n'est lue que dans
les memes conditions.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 :
  - `HDFinalAdvancedV98_TEAM_HEALTH.exe`
    `A14AC047B190CDAEDEEABA8754B985FC73B129C9D3D3B51EF814B5E1177D9337`
  - `HD_AI_AUTHORITY_HOST_V98.exe`
    `2B0A60689861CDE4E0D1F202CF5630E8B7C6934BFDE487665BD6EF6B21B197EE`
  - `HD_AI_AUTHORITY_CLIENT_V98.exe`
    `FE4F42E937FAE1F3061D7CA5CB4747C086BC80EF58AF64923C7739FC004BCA03`
- Le HOTE n'a aucun changement de code; seul son nom de sortie change.
- **Non teste en jeu a cette date.** Protocole A a E dans le LISEZ_MOI.

## V99 - Corrections issues du test du 3 septembre 2026

Livree le 3 septembre 2026 a 19:53:55 (heure locale). Base : V98.
Sauvegarde : `_GOLD_V99/`. Paquet : `release/V99_FIXES/`.
Protocole LAN inchange (version 5) : les compagnons V98 et V99 se comprennent.

### Retour de test

1. F3 fait bien sortir du vehicule, mais repose le joueur **la ou il avait pris
   la voiture**, pas a cote d'elle.
2. G ne redresse pas une carrosserie enfoncee.
3. Sante Max n'atteint pas les soldats des PC amis.

### 1. La position de descente est un cache

`Vehicle.cpp:2208`, gestionnaire `CB_USE_AUTO` du vehicule :

```cpp
case 7:                 //get seat entry pos
   S_seat *sp = GetUserSeat((LPC_actor)prm2);
   if(sp)
      return (dword)&(sp->entry ? sp->entry : sp->seat)->GetWorldPos();
```

`GetWorldPos()` rend une reference sur la position monde **memorisee** du frame
d'entree, recalculee seulement quand la hierarchie du vehicule est mise a jour.
Le chemin de sortie natif (`Actors.cpp:11384`) part de cette valeur pour choisir
la place de descente : si le cache date de l'embarquement, le soldat retourne au
point de ramassage. C'est exactement le symptome.

Correction : lire `frame + 0xBC` du vehicule **avant** l'appel natif, laisser la
descente native se faire (elle delie le soldat du siege et le relie au monde),
puis reposer le soldat a trois metres devant, via
`SetFramePositionOnMainThread(process, player_frame, destination)` - le meme
appel, avec les memes parametres nuls, que la teleportation d'un joueur a pied
(`gameplay_mods.cpp:4216`).

### 2. Ecrire curr_version ne pouvait pas reparer

`Vehicle.cpp:117`, `C_version::SetVersion` :

```cpp
version_list[curr_version]->EnumFrames(cbShow, false, LIGHT|SOUND|VISUAL);
version_list[curr_version]->EnumFrames(cbVol,  NULL,  VOLUME);
version_list[new_v      ]->EnumFrames(cbShow, true,  LIGHT|SOUND|VISUAL);
version_list[new_v      ]->EnumFrames(cbVol,  scene, VOLUME);
version_list[curr_version]->SetOn(false);
version_list[new_v      ]->SetOn(true);
... migration de l'acteur, reset d'animation, envoi reseau
curr_version = new_v;
```

L'entier n'est ecrit qu'a la fin, apres avoir montre et cache des hierarchies
entieres et re-inscrit les volumes de collision dans la scene. L'ecrire seul
depuis l'exterieur laisse donc l'epave affichee, avec ses volumes.

Deux impasses pour l'atteindre :
- `SetVersion` n'est pas virtuelle, donc son adresse n'est pas derivable de la
  table du vehicule, contrairement a `cbProc` que le trainer appelle deja;
- le seul rappel qui y mene, `CB_SIGNAL` / `SIGNAL_DETECTOR` sous-type 0
  (`Vehicle.cpp:319`), est garde par `if(curr_version < version_list.size()-1)`
  et n'avance que d'un cran vers **plus** abime.

Comportement retenu pour G, sur et immediat :
- a pied, il amene devant le joueur le vehicule **intact** (`curr_version == 0`)
  le plus proche jusqu'a 120 m, arrete par `CB_USE_AUTO(13)` et repose par le
  chemin de placement deja eprouve; a defaut, le plus proche a moins de 40 m;
- au volant, il remet l'etat mecanique (resistance, etat d'epave) et publie le
  statut `BodyNotRepairable`, qui dit explicitement de sortir puis d'appuyer
  sur G;
- la liste F6 marque desormais « (abime) » toute entree dont `curr_version`
  n'est pas nul.

Reparer un modele en place resterait possible en localisant `SetVersion` dans le
binaire installe - travail non entrepris a ce jour, et non promis.

### 3. Sante Max chez l'ami : instrumentation des deux cotes

Relecture complete de la chaine, aucun defaut trouve : l'hote alimente bien
`g_peer_extend_client_health` depuis `settings.extended_health_enabled`, le
champ entre dans `state_changed` et dans la condition de battement, et le
compagnon applique la meme logique que le trainer avec la meme lecture de
controle. La cause la plus probable est un PC ami reste sur un compagnon d'une
generation precedente.

Les deux extremites le disent maintenant :
- hote, toutes les 5 s : `LAN->Client heartbeat: client_health=.. client_position=..`;
- compagnon, a chaque changement puis toutes les 5 s :
  `[vie locale] ordre actif : N de vos soldats etendus, M refus, K memorises.`;
- compagnon, si le message est d'une autre version ou d'une autre taille :
  `[attention] message de l'hote refuse : version X, Y octets; ce compagnon
  attend la version 5 sur 20 octets.`

Ces trois lignes separent les trois causes possibles - ordre non emis, paquet
non recu, offsets refuses - sans avoir a deviner.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 :
  - `HDFinalAdvancedV99_FIXES.exe`
    `0E9F77745F8FD241033063CEAB27A01A82E7EE7B9596C49EB04BEA1F8C7130DC`
  - `HD_AI_AUTHORITY_HOST_V99.exe`
    `1CAD76FB0D79732FAB4459BBE6308F79EF953B0DE210E469CBDAB795AF640523`
  - `HD_AI_AUTHORITY_CLIENT_V99.exe`
    `B85B097FDFCF4B431841EABDF99F687507A4FE7B171C4E674DD21C1EB5618BF5`
- **Non teste en jeu a cette date.** Le retour de test qui a motive cette
  version portait sur la V98, qui ne contenait aucune de ces corrections.

## V100 - Le canal LAN atteint enfin le PC ami

Livree le 3 septembre 2026 a 21:19:59 (heure locale). Base : V99.
Sauvegarde : `_GOLD_V100/`. Paquet : `release/V100_LAN_REACH/`.
Protocole LAN inchange (version 5).

### La preuve apportee par le journal du PC ami

Extrait fourni par le joueur, compagnon V99 :

```
[ok] canal visuel LAN ... en ecoute (UDP 48217).
[trace] receiver started protocol=5 UDP=48217.
[ok] hde.exe detecte (PID 9820).
[actif] 67 ennemis: 33 correction(s), proprietaire IA=7AEAD722
```

`ClientTrace` ecrit a la fois dans la console et dans son fichier, et la ligne
`[trace] RX ...` est emise pour CHAQUE paquet - accepte comme refuse. Son
absence totale prouve qu'aucun datagramme n'atteignait le compagnon. Cela
elimine d'un coup les deux hypotheses precedentes : generation du compagnon et
offsets de vie.

### Cause : une seule adresse de diffusion

`PublishPeerVisualCommand` n'ecrivait que vers `INADDR_BROADCAST`
(255.255.255.255), la diffusion limitee. Elle n'est pas routee, et de nombreuses
configurations ne la delivrent pas : adaptateurs de LAN virtuel (Hamachi,
Radmin, ZeroTier), points d'acces Wi-Fi isolant les clients, filtrages
pare-feu. Le jeu lui-meme n'en souffre pas : il utilise ses propres adresses.

`CollectBroadcastTargets` enumere desormais les interfaces locales par
`WSAIoctl(SIO_GET_INTERFACE_LIST)`, retient celles qui sont montees, non
bouclees et capables de diffusion, et calcule leur diffusion dirigee
`adresse | ~masque`. Le message est envoye a chacune, plus a la diffusion
limitee. Sur un adaptateur Hamachi en 25.x.y.z/8, cela donne 25.255.255.255,
qui traverse. La liste est relue toutes les cinq secondes et sa taille apparait
dans le battement journalise (`adresses=N`).

### G : reparation visuelle de la carrosserie, au volant

`C_version::SetVersion` reste hors de portee (non virtuelle). Mais sa partie
visible est reproductible : `version_list` est un vecteur de frames a
`+0x5C/+0x60`, et `SetOn(false)` n'est rien d'autre que l'effacement du bit
prive `FRMFLAGS_ON`, `0x00020000` a `frame+0x0C`. Le compagnon CLIENT ecrit deja
ce bit directement depuis la V63 pour masquer un soldat.

`RepairVehicleBodyInPlace` lit `curr_version` et la liste, verifie la coherence
(2 a 16 versions, index dans les bornes), efface le bit de la version enfoncee,
pose celui de la version 0, puis ecrit `curr_version = 0`. Le joueur reste
assis; rien ne disparait ni ne reapparait.

**Limite ecrite dans le LISEZ_MOI** : `SetVersion` re-inscrit aussi les volumes
de collision dans la scene (`EnumFrames(cbVol, scene, ENUMF_VOLUME)`), ce qui
n'est pas reproductible sans l'EnumFrames natif. La forme de collision peut donc
rester celle de l'epave.

Sur demande explicite du joueur, G ne s'applique plus qu'a l'interieur d'un
vehicule; a pied il ne fait rien et le statut le dit.

### Deux demandes refusees, avec leur raison

**Cloner un vehicule (F6).** `CreateActor(ACTOR_AUTOMOBIL)` ne prend aucune
donnee; l'acteur est accroche a un frame deja charge par la carte, puis ses
parametres sont lus depuis le bloc de mission par `MissionLoad`. Il n'existe
aucun catalogue global de vehicules comparable a celui de l'inventaire. Un clone
exigerait de dupliquer une hierarchie 3D dans la scene et de reconstruire ses
tables depuis l'exterieur du processus : risque de plantage juge inacceptable.

**Traits de balles ennemies.** Le test du joueur tranche : les degats traversent
le reseau, le tir visuel non. C'est la consequence directe de l'attribution de
100 % des ennemis a l'hote par les compagnons - nativement chaque machine
dessinait localement les tirs de sa moitie. Corriger demande d'instrumenter
`NM_GAME_CREATE_SHOOT` en emission et en reception. Non entrepris.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 :
  - `HDFinalAdvancedV100_LAN_REACH.exe`
    `35C0200A8DC275875F40AABF0FF0AE51A608E0F3F066AF637E8E820FEF14E177`
  - `HD_AI_AUTHORITY_HOST_V100.exe`
    `4D99F78A7F4425A36EC05846CB01E0F3851F650D9FF4A9747283D3172AE4C33F`
  - `HD_AI_AUTHORITY_CLIENT_V100.exe`
    `59671D8E134F805FEF749A31D783DB4F9CE3CBCFE8275156C54F68CE0EA3ED6A`
- **Non teste en jeu a cette date.**

## V101 - La vie etendue couvre les deux copies du meme soldat

Livree le 3 septembre 2026 a 21:40:09 (heure locale). Base : V100.
Sauvegarde : `_GOLD_V101/`. Paquet : `release/V101_TEAM_LIFE/`.
Protocole LAN inchange (version 5).

### Ce que le journal du PC ami a etabli

La correction de diffusion de la V100 fonctionne :

```
[trace] RX hote=C0A80B6F bytes=20 ...
[vie locale] sante etendue appliquee a 1 de vos soldats (0 refus).
[vie locale] ordre actif : 1 de vos soldats etendus, 0 refus, 1 memorises.
```

L'ordre part, arrive, est accepte, et la vie du soldat distant vaut 20000 sur sa
propre machine. Le soldat mourait pourtant comme avant.

### Cause : deux compteurs de vie pour un seul soldat

Le commentaire du crochet de degats du trainer contenait deja la reponse :

> `C_human::Hit` is the virtual slot used by both locally simulated bullets and
> NM_HUMAN_HIT packets.

Chaque machine execute donc `Hit` sur SA copie de l'acteur et y soustrait la
resistance. Il existe deux compteurs :

| copie | vie apres F4 (avant V101) | consequence |
|---|---|---|
| chez l'ami, acteur local | 20000 | encaisse |
| chez l'hote, copie distante | vie normale (200 a 1600) | atteint zero, declenche la mort, l'annonce |

C'est la copie de l'hote qui tuait le soldat de l'ami. Symetriquement, la copie
que l'ami garde des soldats de l'hote pouvait faire de meme.

### Correction

La selection ne filtre plus sur `network_owner`. Des deux cotes - le trainer
dans `CollectLocalPlayers`, le compagnon dans `UpdateClientExtendedHealth` -
la vie etendue couvre **toutes** les copies d'acteur de type joueur presentes
dans la mission. La restauration suit la meme regle : elle ne verifie plus que
le type, plus le proprietaire, afin de pouvoir rendre sa vie d'origine a une
copie distante.

Le message du compagnon est reformule en consequence : « N copies de soldat
etendues (vos soldats et ceux des autres PC) ». Sur un PC ami, N doit valoir le
nombre total de soldats de la mission, plus 1.

Le premier refus observe au chargement de la mission n'etait pas une erreur : la
lecture de controle rejetait des valeurs pas encore initialisees, et l'essai
suivant reussissait. Le message ne s'annonce plus comme une erreur.

### Restes connus, non corriges

- La barre de vie affichee ne se rafraichit qu'au prochain degat :
  `C_human::SetResistance` appelle `game_menu.SetHealth`, que nous ne pouvons
  pas declencher sans appeler la methode. Purement cosmetique.
- Les traits de balles ennemies restent invisibles chez les amis.
- G repare la carrosserie mais ne re-inscrit pas les volumes de collision.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 :
  - `HDFinalAdvancedV101_TEAM_LIFE.exe`
    `D6FA77BF7E6643C4353404D98FBEB171723439339C3AEFE9EFF591EDD43F154F`
  - `HD_AI_AUTHORITY_HOST_V101.exe`
    `624A3B45FF49AB598892FDCEBEBB14C9866AE25DF15BF8BB7D3D4ACF0C7F2BA2`
  - `HD_AI_AUTHORITY_CLIENT_V101.exe`
    `609C91FA5AC1F7B501383948DCD660648030EC9332DCA2CE1F942119104E374D`
- **Non teste en jeu a cette date.**

## V102 - La meme vie exacte sur les deux ecrans

Livree le 3 septembre 2026 a 21:50:30 (heure locale). Base : V101.
Sauvegarde : `_GOLD_V102/`. Paquet : `release/V102_LIFE_SYNC/`.
**Protocole LAN en version 6 : le compagnon CLIENT doit etre redistribue.**

### Constat

La V101 empechait bien la mort, mais le joueur a observe que la vie affichee
pour le soldat d'un ami n'etait pas la meme sur les deux ecrans.

### Cause

Etendre les deux copies ne les synchronise pas : ce sont deux compteurs
independants. Chaque machine soustrait la resistance sur SA copie dans
`C_human::Hit`. Tant que la source du degat est commune - une balle ennemie,
qui voyage en `NM_HUMAN_HIT` - les deux baissent ensemble. Des qu'une source est
locale a une seule machine (chute, feu, explosion declenchee sur place,
correction de collision), une seule copie baisse. Rien dans le moteur ne recale
ensuite ces deux valeurs, donc l'ecart est definitif.

### Correction : une seule verite, celle de l'hote

Le message LAN passe en version 6 et grandit de 20 a 70 octets :

```
... 20 octets existants ...
health_count(1) health_reserved(1)
health[8] de { network_id(2) resistance(4) }        // 48
```

L'hote remplit la table a chaque image, depuis les memes acteurs que ceux qu'il
etend, en les identifiant par `actor+0x20` (numero reseau, stable entre les
machines, contrairement aux adresses). Chaque compagnon parcourt la table,
retrouve l'acteur portant ce numero - quel que soit son proprietaire - et
recopie la resistance publiee si elle differe de la sienne.

Le compte des recalages apparait dans le journal du compagnon :
`ordre actif : N copies etendues, M alignees sur l'hote, ...`.

### Limite assumee

La barre affichee ne se redessine qu'au prochain degat. `C_human::SetResistance`
est la seule routine qui appelle `game_menu.SetHealth`, et une ecriture memoire
ne la traverse pas. La valeur est donc identique des deux cotes en permanence,
mais chaque interface se met a jour au premier coup encaisse. Corriger cela
demanderait d'appeler `cbProc(CB_SET_RESISTANCE, 1, valeur)` sur le thread du
jeu de chaque machine - possible cote hote, ou le mecanisme existe deja pour la
reanimation, mais pas encore cote compagnon.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- `static_assert(sizeof(PeerVisualCommandWire) == 70)` present dans les deux
  fichiers : une divergence de disposition ne peut pas compiler.
- SHA-256 :
  - `HDFinalAdvancedV102_LIFE_SYNC.exe`
    `BFD6D50D29819E19C1FE6E6C09D08164A11FF38D353222A2356D695A30FA04D7`
  - `HD_AI_AUTHORITY_HOST_V102.exe`
    `524A9A678F7586FF21F053C2AFD4CF9572854FAC68386751DBF9999A3D17437F`
  - `HD_AI_AUTHORITY_CLIENT_V102.exe`
    `A0E62A2E519E939FDC45A7C3D0050960EB2A043D2AF393F4DF6BC504958377B4`
- **Non teste en jeu a cette date.**

## V103 - G et F3 corriges, et la sonde qui conditionne la suite

Livree le 3 septembre 2026 a 22:58:55 (heure locale). Base : V102.
Sauvegarde : `_GOLD_V103/`. Paquet : `release/V103_CREATEACTOR_PROBE/`.
Protocole LAN inchange (version 6).

### F3 - sortir du vehicule n'importe ou

Le chemin natif (`Actors.cpp:11384`) demande au vehicule sa position de
descente, teste trois positions de degagement autour d'elle, et **renonce** si
aucune ne convient : vehicule en vol, coince contre un decor, ou en mouvement.
Le trainer se contentait du retour du rappel et annoncait une reussite.

1. Le vehicule est **arrete et raccroche a son secteur** juste avant la
   descente, en reutilisant `SetFramePositionOnMainThread` sur sa propre
   position : ce stub commence par `CB_USE_AUTO(13)`, l'arret natif, puis
   rejoue `SetPos` / `Update` / `SetFrameSector`.
2. Le trainer **relit `using_item`** apres l'appel. Un rappel qui reussit sans
   que le soldat ait quitte son siege n'est plus compte comme une sortie. Le
   journal distingue les deux : `rappel=1 sorti=0`.

### G - reparation de vehicule

`ResolveControlledVehicle` exige que le joueur soit assis **en place zero**,
celle du conducteur, et que `frame+0x80` pointe en retour sur l'acteur. En
passager, ou pendant l'animation d'entree, elle echoue - et G repondait
« montez dans un vehicule » alors que le joueur y etait. Repli ajoute sur
`using_item` (`acteur+0x250`), avec verification du type (16 automobile,
8 canon), tracé dans le journal.

### Sonde `C_game_mission::CreateActor`

Toute la suite du cahier des charges - soldats allies crees, leur fenetre, leur
controle, leurs ordres, le clone de vehicule - depend de cette seule fonction.
Elle n'est pas virtuelle : aucune table ne la donne.

Empreinte retenue, tiree du binaire de reference `source/hde/bin/HDE.exe`
(13 mai 2002, livre avec `HDE.map`, fonction a `0x004316A0`) :

```
56 57 8B F9 8B 4C 24 0C 8B C1 83 E8 00
```

Validateur, dans les 0x60 octets suivants :

```
8B 47 6C 8D 4F 64        ; mov eax,[edi+6Ch] / lea ecx,[edi+64h]
```

C'est l'inscription de l'acteur cree dans le vecteur de la mission. Le trainer
utilise deja `kMissionActorBeginOffset = 0x68` et `kMissionActorEndOffset =
0x6C` : la disposition de la mission est donc identique entre les deux builds,
ce qui fait de ce couple d'instructions un validateur fiable.

**Verification faite** : appliquee au binaire de reference, l'empreinte trouve
**une seule candidate, exactement a l'offset attendu**. Le prologue seul y est
deja unique dans tout le fichier.

La recherche tourne une fois par processus de jeu et journalise l'un des trois
verdicts : TROUVEE, INTROUVABLE, ou plusieurs candidates - auquel cas elle
s'annule d'elle-meme, appeler la mauvaise fonction planterait le jeu.

### Pourquoi la suite n'est pas dans cette version

Le cahier des charges (`CAHIER_DES_CHARGES_V103.md`, chapitre 8) place cette
recherche en **phase 0, bloquante**. Sans l'adresse, la creation d'acteurs est
impossible, et avec elle tout ce qui en decoule. Le binaire du joueur n'etant
pas disponible sur la machine de developpement, seul un lancement chez lui peut
produire le verdict.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 :
  - `HDFinalAdvancedV103_CREATEACTOR_PROBE.exe`
    `438FF02AFC30D2900B633831DA6E2E367321BEDB0C8805FF353473120CE2C844`
  - `HD_AI_AUTHORITY_HOST_V103.exe`
    `6D0E861970D47C3C38092410E4B2CCEE830B18620D4FF19E129F83784EFDE1E2`
  - `HD_AI_AUTHORITY_CLIENT_V103.exe`
    `15488EAA4AB63925DA8C557C6D6C99180D8779CB8FA7D144E47EE5C6D4796D01`
- **Non teste en jeu a cette date.**

## V104 - Commander ses soldats par la carte

Livree le 3 septembre 2026 a 23:19:11 (heure locale). Base : V103.
Sauvegarde : `_GOLD_V104/`. Paquet : `release/V104_SQUAD_ORDERS/`.
Protocole LAN inchange (version 6).

### Le systeme d'ordres, livre et operationnel

Il agit sur des acteurs **existants**, donc il ne depend d'aucune adresse a
retrouver, contrairement a la creation. Il est ecrit une fois pour les soldats
actuels **et** pour les soldats crees a venir, qui seront eux aussi des acteurs
de type joueur appartenant a cette machine.

- **G a pied** ouvre la fenetre des soldats; **G au volant** repare le
  vehicule. Le partage se fait dans `UpdateVehicleRepair` : quand aucun
  vehicule n'est occupe, la demande est reroutee vers la fenetre.
- La fenetre est dessinee par la fenetre superposee existante, avec un titre
  propre; navigation fleches / Entree / Echap.
- La destination vient de la **carte native** du jeu, ouverte par K, dont le
  trainer sait deja convertir un clic en point du monde. La carte n'est donc
  jamais affichee d'office.
- `g_soldier_order_targets` non vide detourne le clic carte vers un ordre; vide,
  la teleportation d'origine s'applique telle quelle.

Mecanisme, celui du moteur lui-meme :

```cpp
AddProgram(0, PRG_MOVE, S_prg_add((dword)&position, true));
```

- `AddProgram` : `vtable+0x24`. Releve dans la vtable de `C_human` du binaire de
  reference, ou `DelProgram` occupe `+0x28` - l'emplacement que le trainer
  utilise deja pour effacer les poursuites. Le decalage est donc **verifie**.
- `S_prg_add` (`H&D.h:556`) : cinq dwords, `d[0]` = adresse de la position,
  `d[1] = 1`.
- `PRG_MOVE` = 0, valeur que le trainer connaissait deja sous `kProgramMove`.

L'appel est joue sur le thread du jeu par le crochet ponctuel du site de cheat,
le meme mecanisme que la purge des perceptions, avec restauration des octets et
liberation differee de la page.

### Etat exact de la creation de soldats

| Element | Emplacement | Comment il est etabli |
|---|---|---|
| `AddProgram` | vtable+0x24 | recoupe avec `DelProgram` +0x28 deja utilise |
| `SetActive` | vtable+0x6C | deja utilise par le trainer |
| `SetFrame` | vtable+0x80 | vtable de `C_player` du binaire de reference |
| `CreateModel` | driver vtable+0x68 | 17 sites l'appellent sur `driver`, aucun autre global |
| `Duplicate` | frame vtable+0x38 | en-tete `I3D2.h`, ancre sur `SetPos` +0x0C connu du trainer |
| `S_prg_add`, `PRG_MOVE` | - | source du moteur |
| `CreateActor` | **empreinte** | hors de toute table |
| `driver` | **empreinte** | hors de toute table |

Les deux empreintes, validees sur le binaire de reference :

```
CreateActor : 56 57 8B F9 8B 4C 24 0C 8B C1 83 E8 00
              validee par 8B 47 6C 8D 4F 64 dans les 0x60 octets suivants
driver      : A1 <global> 50 8B 08 FF 51 68     (driver->CreateModel())
```

Chacune y trouve **une seule candidate, a l'adresse exacte attendue**.

`ResolveSpawnCapability` les resout a chaque lancement et journalise toutes les
dix secondes :

```
Creation de soldats - aptitude: CreateActor=.. driver=.. CreateModel=..
Duplicate=.. mission=.. PRET=..
```

**L'execution n'est pas branchee tant que ce rapport n'a pas confirme, sur la
machine du joueur, que les six adresses se resolvent.** La sequence de creation
enchaine six appels au moteur sur son propre thread, dont deux vers des adresses
reconnues par empreinte : une seule fausse ne produit pas un message d'erreur
mais un plantage de la partie. Le rapport d'aptitude est le seul moyen de le
savoir sans risquer la partie du joueur, et il ne peut etre produit que chez
lui.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 :
  - `HDFinalAdvancedV104_SQUAD_ORDERS.exe`
    `F80AFE29698A575E5560BB6CC77D03973F7890D7E7AB901FFC1DC805F1B6676A`
  - `HD_AI_AUTHORITY_HOST_V104.exe`
    `D754D4EFDA7C3AABFE3E8EF146DA8E88AC88A1633DDFB324C242B090A2FE254A`
  - `HD_AI_AUTHORITY_CLIENT_V104.exe`
    `1BC0204702F592ED9E3D796431C4E77A5E2B713E78A7F6180461E2CFD105BA52`
- **Non teste en jeu a cette date.**

## V105 - Version finale : soldats crees, controle, ordres, clone de vehicule

Livree le 3 septembre 2026 a 23:30:28 (heure locale). Base : V104.
Sauvegarde : `_GOLD_V105/`. Paquet : `release/V105_FINAL/`.
Protocole LAN inchange (version 6).

### La creation d'acteurs, branchee

`SpawnActorsOnGameThread` rejoue sur le thread du jeu la sequence que le moteur
suit lui-meme :

```cpp
PI3D_model mod = driver->CreateModel();      // GameMission.cpp:3018
mod->Duplicate(source);
act = mission.CreateActor(type, 0);          // GameMission.cpp:1759
act->SetFrame(mod);
mod->SetPos(&destination);
```

Le stub boucle sur le nombre demande, abandonne proprement des qu'un pointeur
revient nul, memorise les acteurs crees dans une table de sa page et publie un
drapeau d'achevement. Le site de cheat sert de point d'entree, comme pour la
purge des perceptions, avec restauration des octets et liberation differee.

Le modele source est celui du soldat pilote, d'ou l'uniforme exact de la
mission sans aucune connaissance des tenues. Le type d'acteur est un parametre :
`ACTOR_PLAYER` pour les soldats, `ACTOR_AUTOMOBIL` pour le clone de vehicule.

### Emplacements utilises, et comment chacun est etabli

| Element | Emplacement | Etablissement |
|---|---|---|
| `AddProgram` | vtable+0x24 | vtable de `C_human` du binaire de reference, ou `DelProgram` occupe +0x28 - deja utilise par le trainer |
| `SetActive` | vtable+0x6C | deja utilise par le trainer |
| `SetFrame` | vtable+0x80 | vtable de `C_player` du binaire de reference |
| `SetPos` | frame vtable+0x0C | deja utilise par le trainer |
| `CreateModel` | driver vtable+0x68 | 17 sites l'appellent sur `driver`, aucun autre global |
| `Duplicate` | frame vtable+0x38 | en-tete `I3D2.h`, ancre sur `SetPos` +0x0C |
| `CreateActor` | empreinte | prologue + ecriture du vecteur d'acteurs |
| `driver` | empreinte | `A1 <global> 50 8B 08 FF 51 68` |

### Fenetre des soldats

Touche **G a pied**; **G au volant** conserve la reparation, le partage se
faisant dans `UpdateVehicleRepair` quand aucun vehicule n'est occupe.

- `CREER n soldats` - n reglable de 1 a 50 par les fleches gauche/droite. Les
  soldats apparaissent en grille devant le joueur pour ne pas s'empiler.
- `TOUS` et `GROUPE (les 4 premiers)` arment l'ordre; **K** ouvre la carte
  native et le clic envoie la destination par
  `AddProgram(0, PRG_MOVE, S_prg_add(&position, true))`.
- Une ligne de soldat prend son controle par `SetActive(false)` sur le courant
  puis `SetActive(true)` sur le choisi - exactement `PlayerSwitch`.

Sans selection, le clic carte teleporte comme avant.

### F6 : deux actions par ligne

`Entree` deplace le vehicule de la mission (comportement valide par le joueur),
`C` en cree une copie. La copie est conduisible : `C_automobil::SetFrame`
(`Vehicle.cpp:1784`) retrouve roues, articulations, volant, sieges, moteur et
collision par les noms des sous-objets de la hierarchie dupliquee.

### Limites, dites franchement

1. **Non essaye en jeu.** Le binaire du joueur n'est pas sur la machine de
   developpement. Le LISEZ_MOI demande de creer **un** soldat d'abord, apres
   sauvegarde.
2. **Pas de portrait dans le bandeau** pour les soldats crees :
   `pmenu[MAX_PLAYERS]` est fixe a quatre et y ecrire au-dela corromprait la
   memoire. Ils sont pilotables et commandables, sans jauge en haut d'ecran.
3. **La replique avant de tirer** est un comportement d'acteur ennemi; les
   soldats crees etant de type joueur, ils n'en ont pas. Les vrais ennemis
   gardent la leur, comme demande.
4. **Volumes de collision** non re-inscrits apres une reparation de carrosserie
   (limite deja connue).
5. **Traits de balles ennemies** chez les amis : toujours ouvert.

### Verification

- Build Release x86 des trois cibles avec `--clean-first`, zero avertissement,
  code de sortie 0.
- SHA-256 :
  - `HDFinalAdvancedV105_FINAL.exe`
    `CD1075F60EECD097691CCE3C9FA948A78A704EB60BEDC0CA2F637582E166D9D7`
  - `HD_AI_AUTHORITY_HOST_V105.exe`
    `CD2CA0613C5FC47F11FFD77FE8D3F185D43F2DD211D0E858D47CEBB447DAE9D2`
  - `HD_AI_AUTHORITY_CLIENT_V105.exe`
    `486E385735E4F9D191E7DB6DD0D26D4AC89D59441D35C09348ECBEC9EC8DBAB6`

## V106 - Toutes les touches affichees dans le panneau

Livree le 3 septembre 2026 a 23:59:08 (heure locale). Base : V105.
Sauvegarde : `_GOLD_V106/`. Paquet : `release/V106_KEYS/`.
Seul le trainer change; les compagnons V105 sont repris tels quels, leur code
n'ayant pas bouge. Protocole LAN inchange (version 6).

### Ce qui manquait

Les touches etaient dispersees : certaines dans le libelle d'une carte, d'autres
seulement dans un LISEZ_MOI, d'autres nulle part. Le joueur devait chercher.

Une section **TOUCHES** a ete ajoutee sous la grille des cheats, listant les
vingt-trois commandes du trainer avec leur role. Le releve a ete fait dans le
code, pas de memoire : table des raccourcis de `CheatHotkeys`, lectures de
`GetAsyncKeyState` et de `physical_key_down` dans la boucle principale, et
touches des fenetres in-game.

| Touche | Rôle |
|---|---|
| **F3** | sortir du véhicule, même en vol ou coincé |
| **F4** | Santé Max : un appui étend et remplit, un autre annule |
| **F5** | Fullhands : série d'armes suivante |
| **F6** | liste des véhicules de la mission (dans le jeu) |
| **F7** | passer la mission (synchronisé sur les deux PC) |
| **F10** | ranimer le soldat contrôlé |
| **F12** | restaurer l'image du soldat (squelette après explosion) |
| **G** | au volant : réparer le véhicule — à pied : ouvrir la fenêtre des soldats |
| **K** | carte native : destination d'un ordre, sinon téléportation |
| **W** | masquer / montrer ma position réseau |
| **M** | Fullhands : série suivante, sans ouvrir l'inventaire |
| **V** | noclip : activer ou couper le vol (si la case est armée) |
| **Z S A E** | noclip : avancer, reculer, gauche, droite |
| **H B** | noclip : monter, descendre |
| **F8 F9** | vitesse du joueur : augmenter, réduire |
| **N B** | vitesse du véhicule : augmenter, réduire |
| **I U** | sensibilité de direction : augmenter, réduire |
| **9 8** | vitesse du jeu : accélérer, ralentir (1.0x à 100x) |
| **↑ ↓** | fenêtres in-game : changer de ligne |
| **← →** | nombre de soldats à créer (1 à 50) |
| **Entrée** | valider : créer, armer un ordre, ou prendre le contrôle |
| **C** | liste des véhicules : recréer une copie conduisible |
| **Échap** | fermer la fenêtre |

### Etat du projet a cette date

**Fait et valide par les tests du joueur**
- Invisible pour les ennemis : perception seule, deux portees
- Protection reseau totale : deux portees, couvre les amis
- Sante Max : ses soldats et ceux de ses amis, meme valeur sur les deux ecrans
- F6 : liste des vehicules de la mission
- Aimbot : portee jusqu'a 1000 m, ou illimitee
- Le canal LAN atteint les PC amis (diffusion dirigee par interface)

**Fait, pas encore essaye en jeu**
- Creation de soldats allies avec l'uniforme du joueur, jusqu'a 50
- Prise de controle d'un soldat cree (`SetActive`, comme `PlayerSwitch`)
- Ordres par la carte : un soldat, un groupe de quatre, ou tous
- F6 : seconde action, recreer une copie conduisible
- F3 et G corriges

**Ouvert, non resolu**
- Traits de balles ennemies invisibles chez les amis : consequence des 100 %
  d'ennemis donnes a l'hote; correction = instrumenter `NM_GAME_CREATE_SHOOT`
- Bug de deplacement du joueur : trois verifications en attente (badge de
  vitesse a 1.0x, case Noclip decochee, defaut persistant trainer ferme)
- Pas de portrait de bandeau pour les soldats crees : `pmenu[MAX_PLAYERS]` est
  fixe a quatre dans le moteur
- Volumes de collision non re-inscrits apres reparation de carrosserie

### Verification

- Build Release x86 du trainer, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV106_KEYS.exe` :
  `F9ED695C4B0B16F9EF9F1A927F05DCE6416191DB699AC6A7C8AF598D51678BF3`.

## V107 - La creation de soldats ne fait plus sortir du jeu

Livree le 4 septembre 2026 a 01:54:08 (heure locale). Base : V106.
Sauvegarde : `_GOLD_V107/`. Paquet : `release/V107_SPAWNFIX/`.
Seul le trainer change; les compagnons V105 sont repris tels quels, leur code
n'ayant pas bouge. Protocole LAN inchange (version 6).

### Le diagnostic, tire du journal du joueur

```
Creation de soldats - aptitude: CreateActor=00437EB0 driver=0AD55A00
  CreateModel=0ABF0CE0 Duplicate=0AB92CE0 mission=0260A6B0 PRET=1.
Creation d'acteurs (type 1): demandes=5 declenche=1 execute=0 crees=0.
```

Les deux empreintes avaient donc bien abouti chez le joueur et l'aptitude etait
prete. Le stub etait declenche et ne se terminait jamais : le code s'executait
et se perdait en chemin. Trois defauts, tous dans la meme sequence.

### 1. `Duplicate` au mauvais rang - la cause directe

Les rangs des methodes d'`I3D_frame` etaient comptes avec une expression qui ne
reconnaissait que la forme `I3DMETHOD(Nom)` et laissait passer
`I3DMETHOD_(type,Nom)`. Trente-sept methodes sur cinquante etaient sautees.
`Duplicate` etait annonce a +0x38, ou se trouve en realite
`GetRot1(S_vector &axe, float &angle)` : une methode qui ECRIT a travers deux
pointeurs, dont le second etait pris au hasard sur la pile. Ecriture en memoire
quelconque, sortie immediate.

Le comptage refait sur les cinquante methodes tombe sur `SetPos` au rang 3,
soit +0x0C : exactement l'emplacement dont le trainer se sert depuis des
versions pour teleporter, et que le jeu valide donc a chaque utilisation.
L'ancre etant juste, le reste de la table l'est aussi.

| Methode | Rang | Emplacement |
|---|---|---|
| `SetPos` | 3 | +0x0C (ancre, deja validee en jeu) |
| `SetOn` | 26 | +0x68 |
| `LinkTo` | 33 | +0x84 |
| `Duplicate` | 38 | +0x98 (etait +0x38) |

### 2. Les appels au-dela de +0x7F partaient n'importe ou

Le stub emettait toujours `FF 51 <octet>`, dont le deplacement est SIGNE :
+0x98 s'y lit -104. Le point 1 corrige seul n'aurait donc rien donne. Un
encodeur choisit maintenant la forme courte ou la forme longue
(`FF 91 <mot de 32 bits>`) selon le rang.

### 3. `SetFrame` devine sur le mauvais binaire

Les vtables du binaire de reference et de celui du joueur divergent :

| Methode | Reference | Chez le joueur | Ecart |
|---|---|---|---|
| `SetActive` | +0x6C | +0x6C | 0 |
| `SetFrame` | +0x80 | ? | zone incertaine |
| `IsEnemy` | +0x9C | +0xA4 | +8 |
| `Explode` | +0xD0 | +0xDC | +0xC |
| `Die` | +0xE8 | +0x100 | +0x18 |
| `Hit` | +0xEC | +0x104 | +0x18 |

Deluxe a insere des methodes virtuelles au fil de la table. `SetFrame` tombe
entre l'ecart 0 et l'ecart +8 : son rang chez le joueur vaut +0x80, +0x84 ou
+0x88. `ResolveActorSetFrame` le reconnait a son prologue - celui de
`C_player::SetFrame` a 0x00426380 dans le binaire de reference - en deux
paliers, le prologue entier puis sa premiere moitie, et n'accepte un palier
que s'il designe une SEULE candidate. Sans candidate unique, `capability.ready`
reste faux, rien ne s'execute, et le journal releve les premiers octets de
chaque rang de +0x70 a +0xA0.

### `LinkTo` et `SetOn`, qui manquaient

Le modele duplique n'etait raccroche a aucune branche et restait eteint. Il est
desormais lie au parent de la frame source - lu par le trainer, qui se sert
deja de ce champ - puis allume, comme le moteur le fait apres chaque
duplication.

### Le nombre de soldats, enfin lisible

Le joueur a demande ou cliquer : cela ne se clique pas, cela se regle au
clavier, et la fenetre ne le disait pas assez.

- La premiere ligne affiche `<   CREER  5  soldats   >`.
- Une ligne d'aide permanente est dessinee sous chaque fenetre in-game :
  `Haut/Bas : changer de ligne   Gauche/Droite : nombre de soldats (1 a 50)
  Entree : valider   Echap : fermer`. La fenetre des vehicules a la sienne,
  avec `Entree : voler ce vehicule` et `C : en recreer une copie`.
- La liste DEFILE. Avec cinquante soldats elle compte cinquante-trois lignes,
  n'en montrait que douze, et la selection disparaissait sous le bas de la
  fenetre. Elle se recentre desormais sur la ligne choisie.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 :
  - `HDFinalAdvancedV107_SPAWNFIX.exe`
    `3A3E6D0CF839CB3BEDDCA46C962838D691C38C1FCBDE374D179881E9DB599B41`
  - `HD_AI_AUTHORITY_HOST_V105.exe`
    `CD2CA0613C5FC47F11FFD77FE8D3F185D43F2DD211D0E858D47CEBB447DAE9D2`
  - `HD_AI_AUTHORITY_CLIENT_V105.exe`
    `486E385735E4F9D191E7DB6DD0D26D4AC89D59441D35C09348ECBEC9EC8DBAB6`

Le correctif n'a PAS pu etre essaye en jeu : le binaire du joueur n'est pas sur
la machine de developpement. La difference avec la V105 est qu'un echec ne peut
plus faire sortir du jeu - la garde refuse d'executer et journalise.

## V108 - Nombre tape au clavier, groupe au choix, et la raison du "rien"

Livree le 4 septembre 2026 a 02:08:04 (heure locale). Base : V107.
Sauvegarde : `_GOLD_V108/`. Paquet : `release/V108_SOLDIERS/`.
Seul le trainer change; les compagnons V105 sont repris tels quels. Protocole
LAN inchange (version 6).

### Ce que le journal du joueur a etabli sur la V107

```
[02:02:06] CreateActor: module principal illisible.
[02:04:08] SetFrame acteur: rang +0x88 (0042B120), 17 octets reconnus sur 17.
[02:04:10] Creation de soldats - aptitude: CreateActor=00000000
           driver=0A6F5A00 CreateModel=0A560CE0 Duplicate=0A505B70
           mission=0257D290 SetFrame=+0x88 PRET=0.
```

1. **`SetFrame` est resolu, et vaut +0x88.** Dix-sept octets de prologue sur
   dix-sept. La V105 utilisait +0x80, releve sur le binaire de reference : le
   rang etait donc bien faux, et la reconnaissance par empreinte etait la
   bonne reponse. Les deux methodes virtuelles inserees par Deluxe entre
   `SetActive` (+0x6C, ecart 0) et `IsEnemy` (+0xA4, ecart +8) se trouvent
   donc AVANT `SetFrame`.
2. **`CreateActor` valait zero**, donc `PRET=0`, donc la garde a refuse. Rien
   n'apparaissait, et rien ne plantait : la garde a fait exactement son
   travail.

### Le defaut : un echec de recherche etait retenu pour toujours

`FindCreateActor` et `FindDriverGlobal` posaient `searched = true` avant meme
de savoir si la recherche aboutissait, et ne relisaient jamais. La recherche
partant a l'ouverture du trainer - avant l'attachement au jeu - elle tombait
sur « module principal illisible », et le vide etait fige pour la session
entiere. Deux minutes plus tard le jeu etait pret, la mission chargee, le
joueur identifie : l'empreinte n'etait jamais recherchee a nouveau.

Desormais seul un SUCCES est retenu. Un echec est reessaye, espace de deux
secondes (`kSignatureRetryDelayMs`) pour ne pas relire une image de plusieurs
dizaines de mega-octets a chaque image affichee.

`ResolveActorSetFrame` memorise egalement sa reussite, par couple
(processus, vtable) : le journal recevait la meme ligne quinze fois par
seconde.

### Saisie directe du nombre

Le joueur a demande a choisir librement le nombre plutot que d'appuyer sur une
fleche. Les chiffres sont lus sur VK_0..VK_9 ET VK_NUMPAD0..9 - sur un clavier
AZERTY le code virtuel reste VK_1..VK_0 quelle que soit la majuscule, la
frappe passe donc dans les deux dispositions.

- Le premier chiffre frappe REMPLACE la valeur, les suivants s'ajoutent a
  droite; un zero en tete est ignore.
- Retour arriere efface le dernier chiffre.
- Les fleches restent acceptees pour un cran, et closent la saisie en cours.
- La saisie va A LA LIGNE CHOISIE : CREER, ou GROUPE.

Les touches 8 et 9 sont aussi celles de la vitesse du jeu, lues par leur code
de touche physique (0x09 / 0x0A). Elles sont neutralisees tant que la fenetre
des soldats est ouverte, sans quoi la vitesse aurait change sous les doigts du
joueur pendant sa saisie.

### GROUPE : un effectif choisi

`soldier_group_count` remplace les quatre premiers imposes. La valeur se tape
sur sa propre ligne et est bornee a l'effectif reel. La ligne TOUS et la ligne
GROUPE ont ete echangees pour que les deux lignes portant un nombre se
suivent.

### La fenetre, reecrite

Chaque ligne annonce ce qu'elle fait ET ce que Entree y declenche. La ligne
d'aide sous la fenetre est adaptee a la saisie. La liste defile en se
recentrant sur la ligne choisie - avec cinquante soldats elle compte
cinquante-trois lignes et n'en montrait que douze.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 :
  - `HDFinalAdvancedV108_SOLDIERS.exe`
    `AACF335FD538CD4159E2253A07A1EBFFFCC78C36C250C84A18C88E463B00844D`
  - `HD_AI_AUTHORITY_HOST_V105.exe`
    `CD2CA0613C5FC47F11FFD77FE8D3F185D43F2DD211D0E858D47CEBB447DAE9D2`
  - `HD_AI_AUTHORITY_CLIENT_V105.exe`
    `486E385735E4F9D191E7DB6DD0D26D4AC89D59441D35C09348ECBEC9EC8DBAB6`

La creation elle-meme n'a toujours pas pu etre observee en jeu : sur la V107
la garde l'a refusee avant de s'executer. La V108 leve la cause de ce refus.

## V109 - Placement, retour au soldat d'origine, fenetre lisible

Livree le 4 septembre 2026 a 02:28:34 (heure locale). Base : V108.
Sauvegarde : `_GOLD_V109/`. Paquet : `release/V109_SOLDIERS/`.
Compagnons V105 repris tels quels. Protocole LAN inchange (version 6).

### Ce que le journal etablit

```
[02:10:42] CreateActor: TROUVEE a 00437EB0 (une seule candidate valide).
[02:10:51] SetFrame acteur: rang +0x88, 17 octets reconnus sur 17.
[02:10:53] Creation d'acteurs (type 1): demandes=5 declenche=1 execute=1 crees=5.
[02:15:21] Creation d'acteurs (type 1): demandes=2 declenche=1 execute=1 crees=2.
```

Le correctif de reessai de la V108 fonctionne : `CreateActor` est retrouve des
que le jeu est attache. Et le stub cree EXACTEMENT le nombre demande - le
"nombre illimite" rapporte par le joueur est le bandeau et la liste qui se
remplissent sans jamais se vider, chaque soldat cree restant dans l'effectif
jusqu'a la fin de la mission.

### Le parent, demande au moteur

`LinkTo` recevait un parent lu a `frame+0x18`, emplacement suppose. S'il ne
designe pas le parent, le modele duplique est raccroche n'importe ou dans la
scene : c'est la description exacte du defaut rapporte, des soldats apparaissant
tres loin du joueur.

Le stub appelle desormais `source->GetParent()` (frame vtable +0x80) et ne
raccroche rien si le moteur rend un parent nul. Aucun offset de CHAMP n'est
plus suppose dans cette sequence; seuls des rangs de vtable interviennent.

Cette enumeration est maintenant confirmee par le jeu : les soldats crees
portaient l'uniforme du joueur, ce qui exige que `Duplicate` (rang 38, +0x98)
ait reellement ete appele. Le rang 38 etant juste, l'ancrage sur `SetPos`
(rang 3, +0x0C) l'est aussi, et avec lui `GetParent`, `LinkTo` et `SetOn`.

### Retour au soldat d'origine

`g_original_soldier` retient l'acteur pilote avant le premier changement. La
fenetre porte une ligne `REVENIR a mon soldat d'origine`, affichee seulement
lorsque le joueur n'y est pas deja. Elle emploie `SetActive` (+0x6C), le seul
rang que le jeu valide a chaque partie.

### Fenetre : lignes courtes et table d'actions

Les libelles de la V108 depassaient la largeur utile et etaient coupes. Ils
sont courts; le detail des touches figure une seule fois en pied de fenetre.
Aucune mention de fleche ne subsiste : le nombre se TAPE, ce que le joueur a
demande deux fois.

Les lignes passent par une table `SoldierMenuEntryData` (action + rang) au lieu
d'indices en dur dans la validation. Ajouter ou retirer une ligne ne decale
plus rien - c'etait une categorie d'erreurs a elle seule.

### Ce qui n'est PAS livre, et pourquoi

Le moteur enchaine, a la creation d'un acteur de mission :

```cpp
act = CreateActor(type);
act->SetFrame(frm);
act->MissionLoad(&chunk, 0);   // GameMission.cpp:1783
```

`MissionLoad` initialise l'acteur : posture, animation, identite, place dans le
bandeau, inventaire. Sans elle, l'acteur existe et porte le bon uniforme mais
n'a ni posture - il est ECRASE AU SOL - ni identite, d'ou sa presence parasite
dans le bandeau du bas. Les deux defauts rapportes par le joueur n'en font donc
qu'un.

La variante courte, `TableUpdate`, ne peut pas etre appelee telle quelle : son
prologue dans le binaire de reference

```
51 56 8B F1 57 6A 49 8B 86 90 01 00 00 50 8B 08 FF 51 2C
```

lit une table a `[acteur+0x190]` et appelle une methode dessus. Sur un acteur
qui vient d'etre cree cette table est nulle : l'appel ferait sortir du jeu.
Il faut d'abord ouvrir la table de l'acteur.

Son rang reste par ailleurs ambigu. En comptant les methodes virtuelles de
`C_actor` a partir de `SetFrame = +0x88`, on retombe exactement sur
`IsEnemy = +0xA4`, que le trainer emploie et que le jeu valide - l'ancrage est
donc bon. Mais le meme comptage donne `Explode = +0xD8` alors que le trainer
emploie `+0xDC`, egalement valide en jeu : Deluxe a insere UNE methode entre
les deux, et `TableUpdate` vaut donc +0xBC ou +0xC0 sans qu'on puisse trancher
par le comptage. Il sera resolu par empreinte, comme `SetFrame` l'a ete.

Le choix d'arme demande par le joueur repose sur la meme piece : l'inventaire
fait partie de cette initialisation.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV109_SOLDIERS.exe`
  `D4CA970F19DA8B1415EC0BD8908530EF648607D00C7C439C628353E6C3BC12DC`

## V110 et V111 - Nombre exact, position juste, fenetre lisible

Livrees le 4 septembre 2026 a 02:38:06 (heure locale). Base : V109.
Sauvegardes : `_GOLD_V110/` et `_GOLD_V111/`.
Paquets : `release/V110_SOLDIERS/` et `release/V111_SOLDIERS/`.
Compagnons V105 repris tels quels. Protocole LAN inchange (version 6).

### V110 - deux defauts reels, confirmes corriges par le joueur

**1. Reentree du trampoline.** Le stub de creation est pose sur le site de
traitement des cheats, que le jeu traverse a chaque image.
`SendInventoryMainThreadTrigger` ne provoque que le PREMIER passage; tous les
passages suivants, pendant la seconde ou le trampoline reste en place,
reexecutaient la boucle entiere. Le joueur demandait quatre soldats et en
recevait des dizaines.

Une garde de reentree compare le drapeau d'achevement des l'entree et saute
directement aux octets d'origine s'il est deja pose.

**2. Le journal masquait le defaut.** `created_count` etait plafonne a la
valeur demandee avant d'etre journalise :

```cpp
if (created_count > count)
    created_count = count;   // ne peut JAMAIS depasser le demande
```

Le journal affichait donc fidelement `crees=4` quel que soit le nombre reel.
Le plafond est retire; une ligne d'ANOMALIE est emise si le compteur brut
depasse la demande, et le compteur brut figure dans la ligne de resultat.

**3. La position.** `SetPos` place une frame RELATIVEMENT a son parent, alors
que la position que le trainer connait est une position de MONDE. Les deux ne
coincident que si le parent est a l'origine. `SetPos` a ete retire du stub; le
placement se fait apres coup avec `SetFramePositionOnMainThread`, la routine
deja employee pour les teleportations et le deplacement des vehicules de F6,
donc validee par le jeu a chaque utilisation. La frame de l'acteur se lit a
`acteur+0x28`.

### V111 - la fenetre, et pourquoi elle etait illisible

Ce n'etait pas une question de formulation mais un defaut de dessin, mesurable :

| Grandeur | Valeur |
|---|---|
| Hauteur de la boite de texte | 48 px |
| Hauteur de la police | 30 px |
| Interligne employe | **26 px** |

Chaque ligne empietait de vingt-deux pixels sur la suivante : les lignes se
chevauchaient. Le joueur avait raison, et deux fois.

`EspCanvas` recoit deux methodes : `Panel` (fond opaque) et `MenuLine`
(interligne EGAL a la hauteur de boite, police a chasse fixe Consolas,
alignement a gauche, `DT_END_ELLIPSIS`). Le dessin de la fenetre est refait :
panneau opaque centre, titre, marqueur `->` devant la ligne choisie, quatorze
lignes visibles, defilement recentre, pied de fenetre.

### V111 - la saisie du nombre, rendue verifiable

Chaque chiffre frappe est journalise :

```
Fenetre soldats: chiffre 1 frappe, nombre = 1 (max 50).
Fenetre soldats: chiffre 0 frappe, nombre = 10 (max 50).
```

Si le joueur constate encore que la saisie ne repond pas, le journal tranche
sans discussion. La ligne choisie affiche `<  tapez le nombre`, et les libelles
sont alignes en colonnes grace a la police a chasse fixe.

### Le choix d'armes : localise, mais pas livre, et dans cet ordre

`C_inventory::AddItem(int itm, dword amount)` est reperee dans le binaire de
reference (`?AddItem@C_inventory@@QAEHHK@Z`, 0x0045A3B0). Deux pieces manquent :
son adresse chez le joueur, a etablir par empreinte, et le decalage de la base
`C_inventory` a l'interieur d'un `C_human`.

Mais l'ordre compte davantage que les adresses. Les soldats crees sont ecrases
au sol parce qu'il leur manque `MissionLoad`, l'etape qui leur donne posture,
animation, identite, place dans le bandeau ET INVENTAIRE. Donner une arme a un
soldat sans inventaire n'aurait aucun effet visible. L'initialisation vient
donc d'abord; elle reglera d'un coup les soldats ecrases, leur presence
parasite dans le bandeau, et rendra le choix d'armes possible.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 :
  - `HDFinalAdvancedV110_SOLDIERS.exe`
    `93EC1B363520D2EEB43170AFDD9CA8AA86C35623D3B60575832B6BE2D737E8C9`
  - `HDFinalAdvancedV111_SOLDIERS.exe`
    `F36FCDFC59300476CB04A4B645A617167FE43A466BFF1A96CE49B7E0735B348A`

## V112 et V113 - Le jeu ne s'arrete plus sous le feu ennemi

Livrees le 4 septembre 2026 a 02:50:12 (heure locale). Base : V111.
Sauvegardes : `_GOLD_V112/` et `_GOLD_V113/`.
Paquets : `release/V112_SOLDIERS/` et `release/V113_SOLDIERS/`.
Compagnons V105 repris tels quels. Protocole LAN inchange (version 6).

### Le plantage sous le feu ennemi

Chaine complete, etablie dans la source du moteur :

```cpp
// Actors.cpp:12161 - appele des qu'un joueur encaisse
void SetResistance(int r){
   resistance = r;
   mission.game_menu.SetHealth(menu_id, (float)resistance/(float)init_resistance);
}

// GameMenu.cpp:761 - aucune borne
void C_game_menu::SetHealth(dword plr_id, float f){
   assert(pmenu[plr_id]);
   if(!pmenu[plr_id]) return;
   pmenu[plr_id]->SetHealth(f);
}

// Actors.cpp:13040 - dans le constructeur de C_player
menu_id = mission.game_menu.AddPlayerMenu(1);

// GameMenu.cpp:674
if(num_players==MAX_PLAYERS) return -1;
```

Au-dela de quatre joueurs, `menu_id` vaut -1. `plr_id` etant un `dword`, -1
devient 0xFFFFFFFF : `pmenu[0xFFFFFFFF]` lit quatre octets AVANT le tableau, et
l'appel virtuel qui suit part sur ce que la memoire contenait. Le jeu s'arrete.

Coupure employee, offerte par le moteur sur le chemin exact du degat :

```cpp
case CB_HIT:  if(no_hit_cheat) return 0;   // Actors.cpp:13462
```

Chaque soldat cree recoit `no_hit_cheat` (+0x2D4, champ deja employe par
l'immortalite du trainer, donc etabli et valide en jeu) ainsi que
`resistance` et `init_resistance` (+0x2C et +0x2D8) portees a 20000 - la
division ci-dessus n'ayant aucun sens avec un `init_resistance` nul.

**Reserve assumee** : `C_player::Explode` (Actors.cpp:12909) atteint
`SetResistance` par un autre chemin, que `no_hit_cheat` ne coupe pas. Une
grenade sur un soldat cree peut donc encore arreter le jeu. Le remede definitif
est un vrai `menu_id`, qui fait partie de l'initialisation.

### Touches 1 2 3 4 : diagnostic

| Chemin | `id` | Filtre applique | Soldats crees |
|---|---|---|---|
| Touche « suivant » | -1 | vivant OU actif | ecartes, la touche marche |
| Touches 1 2 3 4 | 0..3 | aucun (`\|\| id != -1`) | inclus, la touche casse |

Deux mecanismes s'additionnent : le tri par `GetMenuID()` place les `-1` en
tete, et `if(!slist[id]->IsAlive()) return -1` rejette ensuite, `IsAlive()`
valant `stay_mode != SM_DEAD`.

Verification de securite faite au passage : `AddPlayerMenu` commence par
`if(num_players==MAX_PLAYERS) return -1;`. Creer plus de quatre joueurs
n'ecrit donc PAS hors du tableau `pmenu[]`.

### Suppression des soldats crees

`FindDestroyActor` reconnait `C_game_mission::DestroyActor` a son prologue
`83 EC 18 55 8B E9 57 89 6C 24 10 8B 45 68 85 C0` et n'accepte qu'une candidate
unique. `DestroyActorsOnGameThread` rejoue la suppression sur le thread du jeu,
avec la meme garde de reentree que la creation. Le joueur pilotant un soldat
cree est d'abord ramene a son soldat d'origine.

### Placement en couronnes

La grille s'etirait sur une vingtaine de metres en reutilisant la hauteur du
joueur pour tous, le trainer ne sachant pas interroger le relief. Douze soldats
par couronne, premiere a 2 m, pas de 1,5 m, decalage d'une demi-place entre
couronnes : cinquante soldats tiennent dans huit metres. Tout soldat non place
est nomme dans le journal.

### Les armes : pieces etablies, feature non livree

| Piece | Etat |
|---|---|
| `C_inventory::AddItem(int, dword)` | prologue releve, quatre premiers octets a masquer (adresse absolue) |
| Decalage de la base `C_inventory` | mesurable : 4 des 12 sites d'appel la precedent d'un `lea ecx,[objet+0x54]`; la mesure sera refaite sur le binaire du joueur |
| Liste des armes | manquante : les numeros viennent de `tables\inventory.tab`, pas du code |

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 :
  - `HDFinalAdvancedV112_SOLDIERS.exe`
    `9F198EE9C2C894B761424EE5D9988B4658685ABFB6E8F6E7BD13BCCEBC7BE441`
  - `HDFinalAdvancedV113_SOLDIERS.exe`
    `0C26083C601E4C9937C69E2A1686503754CC92D9C3D880480BD7EAE5EC761CCD`

## V114 - Placement en un passage, mortalite retablie par un `menu_id` mesure

Livree le 4 septembre 2026 a 03:04:31 (heure locale). Base : V113.
Sauvegarde : `_GOLD_V114/`. Paquet : `release/V114_SOLDIERS/`.
Compagnons V105 repris tels quels. Protocole LAN inchange (version 6).

### Placement : un seul passage

`SetFramePositionsOnMainThread` remplace les appels repetes a
`SetFramePositionOnMainThread`. Un trampoline, un declenchement, une attente,
et une boucle qui appelle `SetPos` sur chaque frame d'une table. La sequence
precedente - installation, declenchement, attente jusqu'a une seconde,
restauration, par soldat - s'etirait avec douze soldats ou plus et certains
appels n'aboutissaient pas; le soldat concerne restait ou le moteur l'avait
mis. C'est ce que le joueur decrivait par « certains a cote et les autres
loin ».

### `menu_id` : mesure, pas supposition

La V113 posait `no_hit_cheat` sur chaque soldat cree pour couper le chemin
`SetResistance` -> `SetHealth(menu_id)` -> `pmenu[-1]`. Le plantage
disparaissait, mais les soldats devenaient immortels - defaut immediatement
constate par le joueur.

`MeasurePlayerMenuIdOffset` cherche l'emplacement de `menu_id` par une
propriete verifiable : sur les soldats du joueur, ce champ porte des valeurs
DISTINCTES comprises entre 0 et 3. On balaie les emplacements de +0x20 a
+0x400 et on ne retient que ceux qui presentent cette propriete sur TOUS les
soldats; il faut au moins trois soldats, et une seule candidate, sans quoi la
mesure est refusee et journalisee.

Chaque soldat cree recoit ensuite le plus GRAND identifiant valide :

| Effet | Consequence |
|---|---|
| `menu_id` dans les bornes | `SetHealth` ne lit plus hors de `pmenu[]` : plus de plantage |
| `no_hit_cheat` retire | les soldats crees encaissent et meurent |
| identifiant le plus grand | ils se rangent en fin du tri de `PlayerSwitch`, les touches 1 2 3 retrouvent les soldats du joueur |

Contrepartie : ils partagent la jauge du dernier soldat, qui bougera quand ils
sont touches. Repli si la mesure echoue : protection de la V113.

### Ce qui reste

**Choix d'armes par groupe et pour tous.** Une seule piece manque : la liste
des armes. Les numeros d'objets viennent de `tables\inventory.tab`, pas du
code. Les deux autres pieces sont etablies - `C_inventory::AddItem` reconnue a
son prologue, et le decalage de l'inventaire mesurable sur le binaire du joueur
(quatre des douze sites d'appel la precedent d'un `lea ecx,[objet+0x54]`).

**Exclusion complete des touches du jeu.** Le plus grand `menu_id` y contribue
deja. Une exclusion totale demanderait de changer le type d'acteur, or
`ACTOR_PLAYER` vaut 1 et `ACTOR_ENEMY` vaut 2 : les soldats deviendraient des
ennemis, l'inverse de ce que le joueur demande. Rien ne sera change sur ce
champ sans avoir etabli ce que le moteur en fait ailleurs.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV114_SOLDIERS.exe`
  `B44B6B8E6367815C1BA06F8596C2AE4ECC08478EDEF83D5174CFE831AA98C9AC`

## V115 - Armes en miroir, touches 1 2 3 4 rendues, soldats nommes et mortels

Livree le 4 septembre 2026 a 11:40:07 (heure locale). Base : V114.
Sauvegarde : `_GOLD_V115/`. Paquet : `release/V115_SOLDIERS/`.
Compagnons V105 repris tels quels. Protocole LAN inchange (version 6).

### `menu_id` mesure par le code

La V114 cherchait le champ en comparant les valeurs portees par les soldats
vivants du joueur; il en fallait au moins trois, et son journal a montre que la
condition n'etait pas remplie :

```
Creation d'acteurs: 5 soldat(s) sur 5 regles - menu_id non mesurable,
protection maintenue (immortels, mais le jeu ne s'arrete pas).
```

Le repli posait donc `no_hit_cheat` et laissait `menu_id` a -1 : soldats
immortels, touches directes volees par le tri, plantage a la grenade. Trois
symptomes, un seul defaut.

`MeasurePlayerMenuIdOffset` ne depend plus de l'etat de la partie :

1. `AddPlayerMenu` est reconnue par empreinte
   `51 57 8B F9 83 7F ?? ?? 75 08 83 C8 FF 5F 59 C2 04 00`, les deux octets
   portant le rang de `num_players` etant masques.
2. Ses appelants sont reperes par leur `E8` relatif.
3. Dans les vingt-quatre octets qui suivent chaque appel, on lit le
   `mov [reg+deplacement], eax` - forme courte `89 4x dd` ou longue
   `89 8x dddd`. Les deplacements sont tallies; il faut une valeur dominante
   unique, sans quoi la mesure est refusee et journalisee.

Dans le binaire de reference, les deux appelants donnent `+0x29C`.

Le plus GRAND identifiant valide porte par un soldat du joueur est ensuite pose
sur chaque soldat cree, ce qui donne trois effets a la fois : bornes respectees
donc plus de plantage, `no_hit_cheat` retire donc mortalite retablie, et rang
en fin de tri donc touches 1 2 3 4 rendues.

### Armes en miroir

| Piece | Origine |
|---|---|
| inventaire, debut / fin | `acteur+0x5C` / `+0x60`, deja employes par `weapon_mods` |
| index selectionne | `acteur+0x258` |
| objet : identifiant, reserve, balles | `+0x08`, `+0x18`, `+0x1C` |
| `C_inventory::AddItem(int, dword)` | empreinte, les quatre octets d'adresse absolue du `A1` initial etant ignores |
| base `C_inventory` dans un soldat | `+0x54`, recoupee deux fois |
| `SetSelectedItem` | RVA deja employee par la rotation d'armes, verifiee par son prologue |

Le recoupement du `+0x54` merite d'etre dit : quatre des douze sites qui
appellent `AddItem` dans le binaire de reference la precedent d'un
`lea ecx,[objet+0x54]`, et par ailleurs le vecteur d'inventaire occupe le rang
8 de `C_inventory`, or `0x54 + 0x08 = 0x5C` - exactement l'emplacement que le
trainer emploie depuis des versions et que le jeu valide a chaque tir. Deux
mesures independantes, meme resultat.

`GiveWeaponOnGameThread` rejoue en un seul passage, pour chaque soldat :

```
int index = AddItem(objet, quantite);   // ecx = acteur + 0x54
if(index >= 0) SetSelectedItem(index);  // ecx = acteur
```

`UpdateWeaponMirror` lit a chaque tour l'arme tenue par le joueur et ne
redistribue qu'au CHANGEMENT d'identifiant. La creation de nouveaux soldats
remet ce souvenir a zero, pour qu'ils soient armes sans attendre que le joueur
change d'arme.

**Touche J** : bascule le miroir. Coupe, les soldats gardent leur arme. L'etat
est aussi une ligne de la fenetre G, donc basculable sans quitter la fenetre.

### Noms

`NameCreatedSoldiersOnGameThread` pose « Soldat 1 », « Soldat 2 »... sur chaque
frame par `I3D_frame::SetName` (rang 28 de l'enumeration ancree sur `SetPos`,
celle que la duplication a validee en jeu). Les chaines sont ecrites dans la
page distante, a cote du code.

### Fenetre

Deux lignes par soldat : le piloter, ou l'envoyer SEUL sur un point de la carte
(`SendOne`). `GROUPE` et `TOUS` conservent l'ordre collectif. Une ligne `ARME`
affiche et bascule le miroir. Le pied rappelle la sequence complete : Entree,
puis K, puis un clic sur la carte.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV115_SOLDIERS.exe`
  `CFA297BE87EC5E7EE2B7B01C51F2BC3CDA52EBB6D5DDD13AE645A9A709528EAC`

Non essaye en jeu : le binaire du joueur n'est pas sur la machine de
developpement. Chaque piece non mesurable est refusee et journalisee plutot que
tentee.

## V116 - Le plantage a la creation : une erreur de quatre octets

Livree le 4 septembre 2026 a 11:51:02 (heure locale). Base : V115.
Sauvegarde : `_GOLD_V116/`. Paquet : `release/V116_SOLDIERS/`.
Compagnons V105 repris tels quels. Protocole LAN inchange (version 6).

### Ce que le journal du joueur etablit

```
CreateActor: TROUVEE a 00437EB0 (une seule candidate valide).
SetFrame acteur: rang +0x88, 17 octets de prologue reconnus sur 17.
Placement groupe: 5 frame(s) declenche=1 execute=1.
Creation d'acteurs (type 1): demandes=5 ... crees=5 (compteur brut 5).
Nom des soldats: 5 nomme(s) declenche=1 execute=1.
menu_id: MESURE a +0x2B0 (AddPlayerMenu=00460C20, 2 appelants, 2 d'accord).
Creation d'acteurs: 5 sur 5 regles - menu_id valide pose, ils peuvent mourir.
AddItem: TROUVEE a 00463DE0 (une seule candidate).
Miroir d'arme: objet 71 x368 donne a 5 soldat(s) execute=1.        <- fin
```

Deux resultats acquis, a retenir :

- **`menu_id` vaut +0x2B0 sur le binaire du joueur**, mesure par la methode du
  code (deux appelants de `AddPlayerMenu`, deplacements d'accord). La reference
  2002 donnait +0x29C : l'ecart de +0x14 est exactement celui deja constate sur
  `no_hit_cheat` (0x2C0 -> 0x2D4). La mesure et le decalage connu concordent.
- Creation, placement groupe et nommage aboutissent tous.

### La cause du plantage

`AddItem` doit etre appelee sur la partie `C_inventory` d'un soldat. La V115
employait +0x54, releve sur le binaire de reference par les
`lea ecx,[objet+0x54]` precedant les appels. Transposition fausse de quatre
octets.

```cpp
class C_inventory{
   class C_game_menu &game_menu;              // 4 octets
   vector<C_smart_ptr<S_item> > items;        // debut, fin, capacite
```

`items` etant le second membre, son debut est a base + 4. Le debut du vecteur
est en +0x5C - valeur employee par `weapon_mods` et validee par le jeu a chaque
tir. Donc base = +0x58.

Avec +0x54, `AddItem` manipulait le vecteur quatre octets trop bas, c'est-a-dire
en pleine memoire du soldat. Le constat est desormais code ainsi :

```cpp
constexpr std::uintptr_t kInventoryBaseOffset =
    kActorInventoryBeginOffset - sizeof(std::uint32_t);
```

La base est DEDUITE, plus transposee.

### Une inconnue retiree plutot que deux corrigees a l'aveugle

Le meme passage appelait `SetSelectedItem`, fonction ecrite pour le soldat
pilote et touchant vraisemblablement l'affichage. L'appel est supprime; l'index
choisi est ecrit directement dans `acteur+0x258` apres le passage, le dernier
objet ajoute etant le dernier du vecteur. Le journal compte les armes designees.

Consequence possible, assumee : sans l'appel natif, l'arme peut etre detenue
sans etre affichee immediatement. L'appel sera remis une fois etabli que la
creation ne plante plus - une correction a la fois.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV116_SOLDIERS.exe`
  `5A99542434607301673322490CD4437913D3C405F9533850E6553301D1A0B104`

## V117 - Les cheats natifs refuses sur un soldat cree

Livree le 4 septembre 2026 a 11:56:26 (heure locale). Base : V116.
Sauvegarde : `_GOLD_V117/`. Paquet : `release/V117_SOLDIERS/`.
Compagnons V105 repris tels quels. Protocole LAN inchange (version 6).

### Ce que le journal etablit

```
Miroir d'arme: objet 101 x978 donne a 5 soldat(s) execute=1, 5 designee(s).
Miroir d'arme: objet  14 x200 donne a 5 soldat(s) execute=1, 5 designee(s).
Miroir d'arme: objet  30 x200 donne a 5 soldat(s) execute=1, 5 designee(s).
[TEST FULLHANDS #1] INPUT source=M ...
Fullhands: trigger sent, waiting for completion.
Fullhands: completion_state=0 selected=0 after 1000 polls.
Fullhands: hook restoration unverified.
Process: pid=0 (was pid=10700)
```

Le miroir d'arme distribue trois armes successives sans incident : la
correction de la base d'inventaire (+0x58, deduite au lieu d'etre transposee)
est validee en jeu. Le jeu meurt sur `Fullhands`, dont le stub n'est jamais
revenu.

### La cause

`Fullhands` s'applique a `snapshot->player_object_address`, c'est-a-dire au
soldat PILOTE. La fenetre G permettant de piloter un soldat cree, le cheat a
ete lance sur un acteur non initialise par `MissionLoad`. Il rejoue un chemin
du moteur qui lit la table de l'acteur, son entree de bandeau et son inventaire
de depart - donnees absentes sur un soldat cree.

Le risque n'est pas propre a `Fullhands` : tous les cheats natifs le partagent.

### La garde

`IsCreatedSoldier(actor)` est declaree tot dans le fichier, avec
`g_spawned_soldiers`, pour etre disponible aux cheats natifs situes plus haut.
Deux points de refus :

| Point | Portee |
|---|---|
| entree de `Fullhands` | la touche M |
| entree de `ApplyNativeActorCallback` | F3, ranimation, restauration d'image, et tout rappel natif passant par ce point |

Le refus est journalise avec la marche a suivre : revenir a un soldat d'origine
par la ligne `REVENIR` de la fenetre G, ou par les touches 1 2 3 4.

### Etat de l'affichage des armes

Le journal montre que les soldats RECOIVENT les armes. Leur affichage dans les
mains reste incertain : la V116 avait retire l'appel natif de designation, une
inconnue parmi deux sur le meme passage. L'autre - la base d'inventaire - est
desormais prouvee. Si le joueur confirme que les armes ne sont pas visibles,
l'appel sera remis isolement, une correction a la fois.

### Offsets acquis sur le binaire du joueur

| Element | Valeur | Etablissement |
|---|---|---|
| `SetFrame` | vtable +0x88 | empreinte, 17 octets sur 17 |
| `menu_id` | +0x2B0 | deplacement lu apres `call AddPlayerMenu`, 2 appelants d'accord |
| base `C_inventory` | +0x58 | deduite de +0x5C, valide en jeu |
| `CreateActor` | 00437EB0 | empreinte |
| `AddItem` | 00463DE0 | empreinte |
| `AddPlayerMenu` | 00460C20 | empreinte |

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV117_SOLDIERS.exe`
  `78C96ECB8AC653E26FDF3FD92233990EE238DA7E432E78CE40D39651224F6EFC`

## V118, V119 et V120 - Le miroir d'arme : quatre causes, puis un refus assume

Livrees le 4 septembre 2026 a 12:05:25 (heure locale). Base : V117.
Sauvegardes : `_GOLD_V118/`, `_GOLD_V119/`, `_GOLD_V120/`.
Paquets : `release/V118_SOLDIERS/`, `V119_SOLDIERS/`, `V120_SOLDIERS/`.

### Quatre causes distinctes, toutes reelles

| Version | Cause corrigee | Preuve |
|---|---|---|
| V116 | base `C_inventory` +0x54 -> +0x58 | `items` est le 2e membre, apres une reference de 4 octets; le debut du vecteur est +0x5C, valide en jeu |
| V118 | index ecrit a la main -> fonction native | `S_item::model` est nul tant que `LoadModels` n'a pas tourne |
| V119 | quantite 200..1000 -> 1 | le moteur passe `tab->ItemI(TAB_I_HUM_INV_AMOUNT, i)` |
| V119 | `Reload` ajoutee | `Actors.cpp:7360` la fait apres chaque `AddItem` |

### Le resultat, inchange

```
Reload: TROUVEE a 00463F20 (une seule candidate).
Miroir d'arme: objet 101 x1 donne a 5 soldat(s) execute=1,
               5 soldat(s) la tiennent.
Process: pid=0                              <- 400 ms plus tard
```

Quatre journaux successifs montrent le MEME decalage : la distribution
reussit, le jeu meurt quand il se sert de l'arme.

### Conclusion et decision

Ce n'est pas la facon de donner l'arme qui est en cause, c'est l'acteur qui la
recoit. Les soldats crees n'ont pas ete initialises par `MissionLoad`
(`GameMission.cpp:1783`) - la meme piece manquante qui les laisse ecrases au
sol et sans identite dans le bandeau.

`UpdateWeaponMirror` refuse desormais de distribuer, avec une ligne de journal
espacee de dix secondes rappelant les quatre corrections deja faites et la
raison du refus. La ligne `ARME` de la fenetre affiche
« indisponible sur cette version ». Le code reste entier.

Quatre sessions du joueur ont ete perdues sur ce passage; une cinquieme
tentative n'avait aucune raison d'aboutir la ou quatre corrections fondees ont
echoue.

### Ce qui reste : une seule piece pour trois defauts

`MissionLoad`, ou l'ouverture de la table de l'acteur suivie de `TableUpdate`,
debloque a la fois les soldats ecrases au sol, leur identite dans le bandeau,
et le port des armes. `TableUpdate` lit une table a `[acteur+0x190]` et appelle
une methode dessus; sur un acteur neuf cette table est nulle. Il faut donc
d'abord creer et ouvrir cette table - deux adresses restant a etablir sur le
binaire du joueur.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 :
  - `HDFinalAdvancedV118_SOLDIERS.exe`
    `40BDBAB5E9A4DD288EBF135F3B9AE6C593B4AFA0CA95710A4EFBB0DF4BF7A7C3`
  - `HDFinalAdvancedV119_SOLDIERS.exe`
    `6C6604A3289130D7C92B588D8668EAC4D992A72069F4950BC9369964F1597328`
  - `HDFinalAdvancedV120_SOLDIERS.exe`
    `AB216173B7CEDEEB90D9CF9A25279C51FAE33561A87B3510E259B9BADB190387`

## V121 - Sante identique a celle du joueur, et verification du nom

Livree le 4 septembre 2026 a 12:14:18 (heure locale). Base : V120.
Sauvegarde : `_GOLD_V121/`. Paquet : `release/V121_SOLDIERS/`.
Compagnons V105 repris tels quels. Protocole LAN inchange (version 6).

### La sante

Observation du joueur : ses soldats crees mettaient plus de temps a mourir que
lui. Exacte, et imputable au trainer.

`kExtendedHealthValue` (20000) etait ecrit dans `init_resistance` et
`resistance` de chaque soldat cree. La sante normale d'un soldat vaut
`200 + endurance * 1400`, soit quelques milliers au plus : ils encaissaient
plusieurs fois ce que le joueur encaisse.

Le besoin technique etait reel - `init_resistance` vaut zero sur un acteur
neuf, et `C_player::SetResistance` divise par cette valeur - mais 20000 etait
arbitraire.

La sante est desormais COPIEE depuis le soldat pilote, relue a chaque creation.
Repli si la lecture echoue : 1600, soit une valeur normale, jamais une valeur
d'invulnerabilite. Le choix est journalise :

```
Creation d'acteurs: sante copiee sur votre soldat (init=1600, courante=1600).
```

### Le nom, verifie et non suppose

`C_actor::GetName()` rend `frame->GetName()` (`H&D.h:885`). Poser le nom sur la
frame par `I3D_frame::SetName` (rang 28, +0x70) devrait donc suffire, et le
journal annoncait « 5 nomme(s) execute=1 ». Le joueur voit pourtant toujours
« unknown ».

Deux explications possibles, mutuellement exclusives : le rang de `SetName`
n'est pas +0x70 sur son binaire, ou le nom affiche ne vient pas de la frame.
Plutot que de trancher au hasard - ce qui a deja coute plusieurs sessions - le
stub RELIT le nom immediatement apres l'avoir pose, par `GetName` (rang 29,
+0x74), range le pointeur rendu dans la page distante, et le trainer lit la
chaine :

```
Nom des soldats: le moteur rend « Soldat 1 » pour le premier soldat
                 (attendu « Soldat 1 »).
```

ou, si le pointeur n'est pas lisible :

```
Nom des soldats: relecture impossible (pointeur XXXXXXXX). Le rang de SetName
                 ou de GetName n'est pas celui attendu.
```

La reponse du joueur designera l'un des deux cas sans ambiguite.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV121_SOLDIERS.exe`
  `EC9B5FBD851A5D00A9587841B4EB7A7106EDB4A0661458441BCA960EE1BD20EB`

## V122 - Case de bandeau libre, noms durables, arme sans equipement force

Livree le 4 septembre 2026 a 12:27:50 (heure locale). Base : V121.
Sauvegarde : `_GOLD_V122/`. Paquet : `release/V122_SOLDIERS/`.
Compagnons V105 repris tels quels. Protocole LAN inchange (version 6).

### Ce que le journal etablit

Aucune arme, aucun cheat, aucune creation au moment des deux morts du jeu. En
revanche, chaque mort suit une creation NOMBREUSE :

```
12:23:31  Creation d'acteurs (type 1): demandes=20 ... crees=20   -> mort 11 s
12:24:31  Creation d'acteurs (type 1): demandes=15 ... crees=15   -> mort 65 s
```

Avec trois ou cinq soldats, les sessions tenaient. La difference tient au
nombre d'acteurs crees laisses dans le monde - et tous partageaient la meme
case de bandeau.

### La case de bandeau libre

Desassemblage de `AddPlayerMenu` :

```
83 7F 18 04     cmp dword [edi+0x18], 4      ; num_players
75 08           jne suite
83 C8 FF        or eax,-1                    ; return -1
...
8D 47 08        lea eax,[edi+0x08]           ; pmenu[]
...             boucle : cherche une case nulle
```

et les deux fonctions dangereuses commencent par `if(!pmenu[plr_id]) return;`.
Une case NULLE est donc inoffensive.

`MeasureGameMenuLayout` lit les deux deplacements dans les instructions
elles-memes, sur le binaire du joueur. `FindFreeMenuSlot` obtient le pointeur
du bandeau par `C_inventory::game_menu`, premier membre de la partie
inventaire - donc a `acteur + kInventoryBaseOffset`, la meme base que pour les
armes - puis cherche la premiere case nulle parmi quatre.

Repli si les quatre cases sont prises : le plus grand identifiant valide, comme
en V114, avec portrait partage mais sans arret du jeu.

### Les noms

Deux fautes superposees, toutes deux de moi :

1. La relecture de la V121 se faisait APRES `QueueRemotePageRelease`. Le
   pointeur nul mesurait cette erreur, pas `SetName`.
2. Les chaines etaient ecrites dans la page du stub, rendue aussitot. Si
   `I3D_frame::SetName` retient le pointeur au lieu de copier la chaine, le nom
   devenait pendant - ce qui expliquerait le « unknown » persistant.

Les noms occupent desormais une page dediee (`EnsureSoldierNamePage`), allouee
une fois par processus et jamais rendue. La relecture precede toute liberation.

### L'arme, sans equipement force

| Version | Facon de faire tenir l'arme | Resultat |
|---|---|---|
| V116/V117 | ecriture directe de `acteur+0x258` | mort 400 ms apres |
| V118/V119 | appel natif de designation | mort 400 ms apres |
| V122 | **aucune designation** | a verifier |

Raisonnement : les mises a jour du bandeau sont conditionnees par `if(active)`
dans le moteur, et un soldat cree n'est pas actif tant que le joueur ne le
pilote pas - ce n'est donc pas le bandeau qui plantait. Reste l'equipement,
qui demande au moteur d'animer un squelette que l'initialisation d'acteur n'a
jamais prepare.

Le stub se limite donc a `AddItem` puis `Reload`. L'appel de designation reste
ecrit, entoure d'un `if (false)`, pret a etre repris.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV122_SOLDIERS.exe`
  `E87A8965899E5565A801D1B41467CE7E238FF9CB675275AAABCFDE314CBACDDB`

## V123 - Jamais la case de bandeau d'un vrai soldat

Livree le 4 septembre 2026 a 12:36:29 (heure locale). Base : V122.
Sauvegarde : `_GOLD_V123/`. Paquet : `release/V123_SOLDIERS/`.
Compagnons V105 repris tels quels. Protocole LAN inchange (version 6).

### Deux acquis etablis par le journal

```
Nom des soldats: page permanente allouee a 04E70000.
Nom des soldats: le moteur rend « Soldat 1 » pour le premier soldat.
Miroir d'arme: objet  71 x1 DEPOSE ET CHARGE chez 5 soldat(s) ...
Miroir d'arme: objet  30 x1 DEPOSE ET CHARGE chez 5 soldat(s) ...
Miroir d'arme: objet 101 x1 DEPOSE ET CHARGE chez 5 soldat(s) ...
```

1. **Les noms.** `I3D_frame::SetName` au rang 28 (+0x70) est bon, et
   `GetName` au rang 29 (+0x74) aussi. Le « unknown » venait de la page rendue
   trop tot : les chaines devenaient des pointeurs pendants. La page dediee de
   la V122 regle le probleme.
2. **Les armes.** Trois distributions successives sans equipement force, et le
   jeu survit vingt-trois secondes de plus avant de tomber pour une autre
   cause. Les versions V116 a V119 mouraient 400 ms apres CHAQUE distribution.
   L'equipement force etait donc bien le coupable, et la troisieme voie de la
   V122 - deposer l'arme chargee sans la designer - est la bonne.

### Le squelette : ce qui m'avait echappe

```
Creation d'acteurs: menu_id 1 attribue (aucune case libre, portrait partage).
```

Le constructeur de `C_player` appelle lui-meme `AddPlayerMenu`. Avec deux
soldats d'origine, les cases 0 et 1 sont prises, mais les deux PREMIERS
soldats crees s'attribuent aussitot les cases 2 et 3. `FindFreeMenuSlot` ne
trouve donc plus rien, et le repli de la V122 - « le plus grand identifiant
valide » - designait la case 1, celle du soldat 2 du joueur. D'ou le squelette
et la musique de mort.

Nouvelle regle, par ordre de preference :

| Rang | Case attribuee | Motif |
|---|---|---|
| 1 | celle que le constructeur a donnee au soldat cree | elle est a lui; son squelette s'affiche sur son propre portrait |
| 2 | une case nulle | `SetHealth` et `SetDeathFace` la traversent sans effet |
| 3 | la case d'un AUTRE soldat cree | jamais celle d'un soldat d'origine |

Les cases des soldats d'origine sont relevees en premier et exclues dans tous
les cas (`forbidden`, `is_forbidden`). Le journal annonce le releve et la case
de repli retenue.

Cinq soldats crees ecrivant tous dans le `C_plr_menu` d'un vrai soldat - a
chaque degat, a chaque changement d'arme - est egalement un candidat serieux
pour le plantage « apres un instant » rapporte par le joueur. La correction
l'elimine, sans qu'on puisse encore affirmer que c'etait la seule cause.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV123_SOLDIERS.exe`
  `FA730393DF1BB38789D856426DAE32981410F1EE2796663EE43150EA3AE6492B`

## V124 - Le cas « quatre soldats », et la fenetre des armes

Livree le 4 septembre 2026 a 12:44:56 (heure locale). Base : V123.
Sauvegarde : `_GOLD_V124/`. Paquet : `release/V124_SOLDIERS/`.
Compagnons V105 repris tels quels. Protocole LAN inchange (version 6).

### Quatre soldats d'origine : un trou dans la regle de la V123

Le bandeau compte quatre cases. Avec quatre soldats d'origine elles sont toutes
au joueur, donc toutes interdites, et `AddPlayerMenu` rend -1 a chaque soldat
cree. `fallback` restait a -1, aucune ecriture n'avait lieu, `menu_id` demeurait
-1, et le premier degat relisait `pmenu[0xFFFFFFFF]`.

Aucune troisieme voie n'existe :

| Choix | Consequence |
|---|---|
| partager la case d'un vrai soldat | son squelette s'affiche sur le portrait de celui-ci - refuse par le joueur en V123 |
| ne pas encaisser | soldats crees invulnerables |

Le second est retenu POUR CE CAS SEUL (`g_created_soldiers_invulnerable`), et
il est annonce - au journal et dans la fenetre G - plutot que subi. Avec trois
soldats d'origine ou moins, une case reste attribuable et les soldats crees
sont mortels.

### La fenetre des armes (touche J)

Forme proposee par le joueur, et meilleure que le miroir : une cible, une arme,
une validation.

```
ARMES DE VOS SOLDATS

CIBLE      TOUS  (5 soldats crees)
Gauche / Droite pour changer de cible

Arme 1    objet 71     978 balles
Arme 2    objet 30     200 balles
Arme 3    objet 101    120 balles

Gauche/Droite : la cible   Haut/Bas : l'arme   Entree : la donner
Echap : fermer
```

- Cible : `TOUS`, `GROUPE` (les n premiers), ou `Soldat N`.
- Armes : celles de l'inventaire du JOUEUR. Leur identifiant est donc
  forcement valide, et le joueur reconnait ce qu'il porte; le numero d'objet
  est affiche a cote, comme il l'avait demande.
- Distribution : `AddItem` puis `Reload`, sans equipement force - le chemin que
  son journal a valide (trois distributions successives sans arret du jeu).

`UpdateWeaponMenu` collecte l'inventaire a chaque image, construit les
libelles, et route les touches. La fenetre partage le rendu des deux autres
(panneau opaque, interligne egal, defilement).

### Les noms

Le journal du joueur etablit que le nom de l'acteur est bon :

```
Nom des soldats: le moteur rend « Soldat 1 » pour le premier soldat.
```

`C_actor::GetName()` rend `frame->GetName()` (H&D.h:885), et le moteur,
interroge juste apres l'ecriture, rend « Soldat 1 ». Le « unknown » observe
vient donc d'une autre source - vraisemblablement le bandeau, qui affiche
l'identite de mission posee par `MissionLoad`, absente chez un soldat cree.
Determiner l'endroit exact ou le joueur le lit evitera de corriger au hasard.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV124_SOLDIERS.exe`
  `38634261C62E96FAC45180D0B5E5E3FFD5EDC3F8FBEBF77F5AC1C2B0D09E7E36`

## V125 - Lecture complete du journal : quatre defauts

Livree le 4 septembre 2026 a 12:52:54 (heure locale). Base : V124.
Sauvegarde : `_GOLD_V125/`. Paquet : `release/V125_SOLDIERS/`.
Compagnons V105 repris tels quels. Protocole LAN inchange (version 6).

Les 131 lignes du journal ont ete regroupees par frequence, apres suppression
des horodatages et normalisation des nombres, afin qu'aucun message ne soit
noye dans la repetition.

### Defaut 1 - le court-circuit qui annulait la garde

```
Bandeau: 4 case(s) reservee(s) a vos soldats d'origine, case de repli = -1.
Bandeau: ... Les soldats crees sont donc rendus INVULNERABLES ...
Creation d'acteurs: 0 soldat(s) sur 5 regles ...
```

Zero sur cinq. Les ecritures etaient chainees :

```cpp
ok = ok && fallback >= 0 && WriteMemory(menu_id, fallback);
ok = ok && WriteMemory(no_hit_cheat, no_hit);
```

`fallback` valant -1 en mission a quatre soldats, `ok` devenait faux et le `&&`
suivant court-circuitait l'ecriture de `no_hit_cheat`. La protection annoncee
n'etait jamais posee : `menu_id` restait a -1 ET l'acteur restait vulnerable.
Le premier degat relisait `pmenu[0xFFFFFFFF]`.

Les trois ecritures - sante, case, garde - sont desormais independantes, et
chacune est comptee :

```
Creation d'acteurs sur 5 : sante posee chez 5, case de bandeau chez 0,
garde posee chez 5. Les 5 soldat(s) sans case sont invulnerables.
```

La garde depend maintenant du seul fait d'avoir ou non une case
(`has_slot`), et plus du succes des ecritures precedentes.

### Defaut 2 - un message de journal contradictoire

« menu_id valide pose, ils peuvent mourir » etait emis juste apres le message
d'invulnerabilite, et alors qu'aucune ecriture n'avait abouti. Le message
decrivait une intention, pas un fait. Il compte desormais les ecritures
reelles.

### Defaut 3 - la fenetre des armes ouverte pendant le declenchement

```
Miroir d'arme: objet 101 x1 ... chez 11 soldat(s) declenche=1 execute=0
Fenetre armes: arme 7 (objet 101) donnee a 11 soldat(s) resultat=0.
```

Le miroir, qui agit pendant le jeu normal, aboutissait toujours. La fenetre,
elle, etait encore ouverte quand on demandait au jeu de traverser le point
d'entree. Elle se referme desormais avant, une image est laissee au jeu, et une
seconde tentative a lieu si la premiere echoue.

### Defaut 4 - deux lignes pour un appui

`G REPARATION VEHICULE: demandee.` suivi de `G a pied: ouverture de la fenetre
des soldats.`, cinq fois. La premiere ligne laissait croire qu'une reparation
avait ete tentee alors que le joueur etait a pied. Supprimee de `main.cpp`;
`UpdateVehicleRepair` dit ce qui a reellement eu lieu.

### Verifie, et non defectueux

| Message | Occurrences | Lecture |
|---|---|---|
| `Radar read state -> *-unavailable` | 16 | etats passagers; retour a `ready` neuf fois |
| `CreateActor: module principal illisible` | 1 | au lancement, avant attachement; reessaye et aboutit |
| `[TEST TELEPORT] enabled=0 accepted=0` | 12 | teleportation decochee |
| `[TEST ESP] off` | 7 | aucune case ESP cochee |

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV125_SOLDIERS.exe`
  `4F638862507AE4CE3F23439986237E94713744DEC01E484EC33D3D52189F3330`

## V126 et V127 - Garde sur le bandeau, equipement abandonne

Livrees le 4 septembre 2026 a 14:16:22 (heure locale). Base : V125.
Sauvegardes : `_GOLD_V126/`, `_GOLD_V127/`.
Paquets : `release/V126_SOLDIERS/`, `release/V127_SOLDIERS/`.
Compagnons V105 repris tels quels. Protocole LAN inchange (version 6).

### Le journal, rendu lisible

`Game speed: module info unavailable for hook install.` comptait 6569
occurrences pour 6222 lignes - 97 %% du fichier, 740 Ko - emis a chaque image
des que le jeu n'est plus attache. Le message est bride a un par dix secondes,
ainsi que son jumeau cote vehicule. Le journal suivant fait 57 lignes.

### La garde sur le bandeau

Quatre defauts, une seule cause : `C_game_menu::SetHealth` et
`C_game_menu::SetDeathFace` lisent `pmenu[identifiant]` sans borne.

```
SetHealth     8B 44 24 04     mov eax,[esp+4]
              83 EC 2C 53 56
              8B 74 81 08     mov esi,[ecx+eax*4+8]
SetDeathFace  83 EC 10 56 57
              8B 7C 24 1C     mov edi,[esp+0x1C]
              8B F1
              8B 44 BE 08     mov eax,[esi+edi*4+8]
```

`InstallMenuBoundsGuard` les reconnait par ces prologues - candidate unique
exigee - et pose a l'entree de chacune :

```
cmp dword [esp+4], 4
jb  suite
ret <taille des arguments>
suite: <octets d'origine> ; jmp vers la suite
```

Les octets repris (7 et 5) ne contiennent ni adresse absolue ni saut relatif :
ils se deplacent tels quels.

Consequence : `kCreatedSoldierMenuId = 1000` devient inoffensif, et il resout
quatre choses a la fois.

| Defaut | Pourquoi il tombe |
|---|---|
| touches 1 2 3 4 volees | 1000 range les crees EN DERNIER dans le tri de `PlayerSwitch` |
| soldats crees invulnerables | plus de lecture hors tableau, donc `no_hit_cheat` = 0 |
| squelette sur un vrai portrait | `SetDeathFace(1000)` rend la main sans rien faire |
| plantage a la grenade | meme chemin, meme garde |

Repli si l'une des deux fonctions n'est pas reconnue de facon certaine : rien
n'est pose, les soldats crees restent proteges du tir, et le journal le dit.

### L'equipement force : le pari et son dementi

La V126 avait conditionne l'equipement natif a la presence de la garde,
supposant que les arrets des V116 a V119 venaient de la jauge pointant hors du
tableau. Le journal du joueur a tranche :

```
Garde bandeau: POSEE.
Creation d'acteurs sur 5 : ... identifiant 1000 pose chez 5 ...
Fenetre armes: arme 1 (objet 30) donnee a 5 soldat(s) resultat=1.
Process: pid=0                                   <- juste apres
```

La garde etait en place et le jeu s'est arrete quand meme. L'equipement
lui-meme est donc en cause : mettre l'arme dans la main demande au moteur
d'animer un squelette que `MissionLoad` n'a jamais prepare.

Cinq facons d'y parvenir ont ete essayees, toutes fatales :

| Version | Methode | Resultat |
|---|---|---|
| V116/V117 | ecriture directe de `acteur+0x258` | arret 400 ms apres |
| V118/V119 | appel natif de designation | arret 400 ms apres |
| V119 | + `Reload`, quantite 1 | arret 400 ms apres |
| V126 | + garde du bandeau | arret juste apres |
| V122/V127 | **pas d'equipement** | **le jeu tient** (valide 4 fois) |

`kForceEquip = false`, constante documentee, avec le code conserve. Il ne sera
retente qu'une fois l'initialisation d'acteur disponible.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 :
  - `HDFinalAdvancedV126_SOLDIERS.exe`
    `642E77767C41E64FE171D8A2EBFBA3F9A713C83F4DB50EE9E2EB815F961F784F`
  - `HDFinalAdvancedV127_SOLDIERS.exe`
    `5CBF7EC13D3B8C7FB49D9CCB9EBE2CF16B27C7B081E98ACD9A7D29852947E457`

## V128 - Le « unknown » trouve a la ligne pres, garde etendue

Livree le 4 septembre 2026 a 14:32:07 (heure locale). Base : V127.
Sauvegarde : `_GOLD_V128/`. Paquet : `release/V128_SOLDIERS/`.
Compagnons V105 repris tels quels. Protocole LAN inchange (version 6).

### Le nom affiche vient du numero de visage

```cpp
// Actors.cpp:12417
int i = tab->ItemI(TAB_I_HUM_FACE);
if(!i)
   return "unknown";
i = GT_GAME_MENU_SOLDIER_NAME + i - 1;
return all_txt[i];
```

Ni la frame, ni le bandeau : le NUMERO DE VISAGE. La table d'un soldat cree est
ouverte par le constructeur de `C_human` avec `templ_hum_prop`, ou ce numero
vaut zero - et zero rend litteralement le mot « unknown ».

`SetName` sur la frame etait correct depuis la V122, comme le journal l'a
confirme a chaque creation (« le moteur rend "Soldat 1" »). Il ne servait
simplement pas a l'affichage du nom.

| Element | Valeur | Origine |
|---|---|---|
| pointeur de table dans l'acteur | +0x190 | prologue de `TableUpdate` |
| `TAB_I_HUM_FACE` | propriete 65 | `Tables.h` |
| nombre de proprietes | table +0x0C | `weapon_mods` |
| descripteurs | table +0x20 | `weapon_mods` |
| donnees | table +0x24 | `weapon_mods` |
| taille des donnees | table +0x28 | `weapon_mods` |
| descripteur | 8 octets : offset, type, taille chaine, longueur | `weapon_mods` |
| type entier | 2 | `weapon_mods` |

`NameCreatedSoldiersByFace` releve les numeros dans les tables des soldats du
joueur - forcement valides - et les repartit en boucle sur les soldats crees.
Si aucun n'est lisible, rien n'est ecrit : inventer un numero enverrait le
moteur chercher un nom hors de `all_txt`.

### La garde du bandeau : cinq fonctions

Relecture de toutes les methodes publiques de `C_game_menu` prenant un
identifiant de joueur :

| Fonction | Acces | Verdict |
|---|---|---|
| `DestroyPlayerMenu` | `83 F8 04 / jae` | protegee par le moteur |
| `SetPrgList` | `83 F8 04 / jae` | protegee par le moteur |
| `SetActive` | parcourt `pmenu[]` | pas d'indexation directe |
| `SetHealth` | `8B 74 81 08` | **sans borne** - garde V126 |
| `SetDeathFace` | `8B 44 BE 08` | **sans borne** - garde V126 |
| `SetPlayerFace` | `8B 4C BE 08` | **sans borne** - garde V128 |
| `SetPrgKeyColor` | `8B 44 91 08` | **sans borne** - garde V128 |
| `GetPrgKeyColor` | `8B 4C 81 08` | **sans borne** - garde V128 |

`SetPlayerFace` est declenchee par `CB_SETFACE`, c'est-a-dire par le correctif
du nom lui-meme : les deux changements sont indissociables. Les cinq gardes
sont posees d'un bloc, ou aucune ne l'est.

### L'arme non degainee : piste nouvelle, non activee

`C_human::SetSelectedInvItem` appelle `SetGun(nom)`, qui contient :

```cpp
PI3D_frame hand = model->FindChildFrame("gun*", ...);
if(!hand) return;
gun = driver->CreateModel();
...
assert(!hand->NumChildren());     // le moteur ATTEND une main vide
gun->LinkTo(hand);
```

Les soldats crees sont des copies d'un joueur qui tenait une arme : leur main
contient deja une arme dupliquee, sans proprietaire. Le moteur ne s'y attend
pas.

Piste serieuse, mais piste. Elle ne sera activee qu'apres mesure - la regle
etablie apres le dementi de la V126 tient : on ne livre active que ce qu'un
journal a valide.

Note ecartee au passage : l'affichage des munitions, que j'avais soupconne, est
entoure de `if(active)` dans le moteur; un soldat cree n'etant pas actif, ce
n'etait pas ce chemin.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV128_SOLDIERS.exe`
  `D06A50FBA9CD7B93F269FE62F0D33D0E94BFE36C4ED4E7EA99BA53C0E038FBFD`

## V129 - Transposer au lieu de mesurer : la meme faute, trois fois

Livree le 4 septembre 2026 a 14:43:37 (heure locale). Base : V128.
Sauvegarde : `_GOLD_V129/`. Paquet : `release/V129_SOLDIERS/`.
Compagnons V105 repris tels quels. Protocole LAN inchange (version 6).

### La regression du switch, et sa cause

```
Garde bandeau: SetPlayerFace INTROUVABLE (0 candidates).
Garde bandeau: REFUSEE.
Creation d'acteurs sur 5 : ... identifiant 1000 pose chez 0 ...
```

La V128 exigeait les CINQ fonctions du bandeau. Quatre etaient trouvees, une
seule manquait, et le « tout ou rien » a annule la pose entiere : `menu_id`
est reste a -1, donc les soldats crees sont repasses EN TETE du tri de
`PlayerSwitch` - touches 1 2 3 4 volees - et sont redevenus invulnerables.

Regression introduite par moi, en liant la pose des cinq gardes.

### Trois corrections de methode

**1. Garde independante.** Chaque fonction est posee separement
(`GuardPlan::essential`). Une fonction non identifiee est sautee et
journalisee. Le verdict qui gouverne `kCreatedSoldierMenuId` ne depend plus que
de `SetHealth` et `SetDeathFace`, les deux seules sur le chemin des degats.

**2. `SetPlayerFace` cherchee par forme.** Elle partage sa forme avec
`SetDeathFace` :

```
83 EC 10 56 57 8B 7C 24 ?? 8B F1 8B ?? BE 08
```

Deux octets libres - deplacement de l'argument et registre de destination. La
candidate retenue est celle qui n'est pas `SetDeathFace`; si plusieurs se
presentent, aucune n'est retenue.

**3. Emplacement de la table mesure, avec auto-verification.**
`MeasureActorTableOffset` balaie `+0x40` a `+0x600` et ne retient un
emplacement que si, pour TOUS les soldats du joueur :

| Condition | Ce qu'elle valide |
|---|---|
| pointeur sain | l'emplacement |
| nombre de proprietes, descripteurs, donnees, taille aux offsets employes par `weapon_mods` | la disposition de l'objet table |
| descripteur de la propriete 65 de type entier | l'indice de propriete |
| valeur lue = numero de visage plausible et NON NUL | les trois a la fois |

La derniere condition est la plus forte : si l'emplacement, la disposition ou
l'indice etaient faux, cela ne s'alignerait pas sur quatre soldats a la fois.

**4. Inventaire relu et journalise.**

```
Inventaire du soldat 1 : 2 objet(s), selectionne=0,
identifiants = 30 14 (l'arme demandee etait 14).
```

`resultat=1` ne prouvait que l'execution du code, pas son effet. La mesure
manquait pour corriger l'equipement sans tatonner.

### Ce que la lecture complete du chemin de l'arme a montre

`SetSelectedInvItem` -> `SetGun(nom)` :

```cpp
if(!model) return;
PI3D_frame hand = model->FindChildFrame("gun*", ...);
if(!hand) return;
gun = driver->CreateModel();
model_cache.Open(gun, name, mission.GetScene(), ...);   // charge un FICHIER
assert(!hand->NumChildren());                           // main VIDE attendue
gun->LinkTo(hand);
```

Trois faits, tires du code et non supposes :

- `model` EST renseigne sur un soldat cree : `C_actor::SetFrame` le pose quand
  la frame est un modele (`Actors.cpp:546`). `SetGun` s'executerait donc en
  entier.
- Le moteur attend une main VIDE, alors que les soldats crees sont des copies
  d'un joueur arme : leur main contient deja une arme dupliquee.
- `model_cache.Open` CHARGE UN FICHIER depuis le disque, au milieu du point
  d'entree employe pour executer du code sur le thread du jeu. C'est autrement
  plus lourd qu'ajouter un objet a une liste, et c'est un candidat serieux
  pour les arrets constates.

L'equipement reste desactive : la regle etablie apres le dementi de la V126
tient - on ne livre active que ce qu'un journal a valide.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV129_SOLDIERS.exe`
  `39944F8E31884D5C83D3B58C2FEC1270CEF8A18596EFA37F1CA47132C110B86B`

## V130 - L'arme n'arrivait jamais : `AddItem` casse par une fausse deduction

Livree le 4 septembre 2026 a 14:58:06 (heure locale). Base : V129.
Sauvegarde : `_GOLD_V130/`. Paquet : `release/V130_SOLDIERS/`.
Compagnons V105 repris tels quels. Protocole LAN inchange (version 6).

### Ce que la mesure d'inventaire a revele

```
Inventaire du soldat 1 : 1 objet(s), selectionne=0,
identifiants = 30 (l'arme demandee etait 14).
```

Un seul objet : le 30, ajoute par le constructeur de `C_human`
(« free hands - has every human », Actors.cpp:3413). `AddItem` n'ajoutait rien.

### La cause : une deduction fausse, faite par moi en V116

La V116 avait remplace `kInventoryBaseOffset = 0x54` par
`kActorInventoryBeginOffset - 4 = 0x58`, en raisonnant ainsi : `items` est le
second membre de `C_inventory`, apres une reference de quatre octets, donc son
premier champ - le pointeur de debut - se trouve a `base + 4`.

Le desassemblage d'`AddItem` montre que le vecteur ne commence pas par son
pointeur :

```
8B E9         mov ebp,ecx          ; ebp = this (C_inventory*)
...
8B 4D 08      mov ecx,[ebp+8]      ; items.begin = this + 8
8B 45 0C      mov eax,[ebp+0x0C]   ; items.end   = this + 0x0C
```

L'ancien MSVC place un membre allocateur de quatre octets devant les trois
pointeurs. Le debut du vecteur est donc a `this+8`, et non `this+4`.

| Element | Valeur | Etablissement |
|---|---|---|
| debut du vecteur dans l'acteur | +0x5C | employe par `weapon_mods`, valide en jeu a chaque tir |
| debut du vecteur dans `C_inventory` | +0x08 | **lu dans les instructions d'`AddItem`** |
| base de `C_inventory` dans l'acteur | **+0x54** | 0x5C - 0x08 |

C'est la valeur que les sites d'appel du binaire de reference donnaient depuis
le debut. Elle etait juste; je l'ai cassee en croyant la corriger, puis j'ai
cherche ailleurs pendant quatorze versions.

### Limite sur le nombre total de soldats vivants

```
Fenetre armes: arme 2 (objet 14) donnee a 116 soldat(s) resultat=0.
Process: pid=0
```

116 soldats crees presents simultanement - 50, 21, puis quatre fois 10, puis 5.
`kMaximumSpawnedSoldiers` ne bornait qu'une creation. La borne porte desormais
sur `g_spawned_soldiers.size()` cumule, et le refus renvoie vers la ligne
SUPPRIMER.

### La garde du bandeau : la correction de la V129 fonctionne

```
Garde bandeau: 4 fonction(s) sur 5 protegee(s), dont les deux indispensables.
Garde bandeau: POSEE.
Creation d'acteurs sur 5 : ... identifiant 1000 pose chez 5 ...
```

`SetPlayerFace` reste non identifiee - une seule forme trouvee, qui est
`SetDeathFace` elle-meme - mais elle n'est plus bloquante.

### La table d'acteur : mesuree dans le code, avec releve

Le balayage de la V129 n'a rien trouve. Plutot que d'elargir ses criteres au
hasard, l'emplacement est lu dans le code :

```
51 56 8B F1 57 6A ?? 8B 86 <deplacement sur 32 bits>
   = mov eax,[esi+deplacement]   ; le pointeur de table
```

Le releve est ensuite verifie par la meme auto-verification (numero de visage
plausible chez tous les soldats). En cas d'echec, deux lignes de journal
donnent la table, le nombre de proprietes, les descripteurs, les donnees, et le
descripteur de la propriete 65 avec la valeur lue - de quoi savoir laquelle des
trois hypotheses est fausse.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV130_SOLDIERS.exe`
  `C3D2895F7546B8428837C2AC53BC71414DBFADE20E5493CD37DAF87F7409C9E7`

## V131 - L'arme arrive, et la liste des soldats crees est purgee

Livree le 4 septembre 2026 a 15:09:36 (heure locale). Base : V130.
Sauvegarde : `_GOLD_V131/`. Paquet : `release/V131_SOLDIERS/`.
Compagnons V105 repris tels quels. Protocole LAN inchange (version 6).

### L'arme arrive enfin

```
Inventaire du soldat 1 : 3 objet(s), identifiants = 30 30 14   (demandee 14)
Inventaire du soldat 1 : 4 objet(s), identifiants = 30 30 14 71
Inventaire du soldat 1 : 5 objet(s), identifiants = 30 30 14 71 101
```

La correction de la base d'inventaire a +0x54, lue dans les instructions
d'`AddItem`, est validee par le jeu. Quatre distributions successives, l'arme
demandee presente a chaque fois. `selectionne=0` : ils ne la sortent pas, ce
qui reste conditionne a l'initialisation d'acteur.

### La liste jamais purgee

```
Miroir d'arme: objet 14 x1 ... chez 1 soldat(s) declenche=1 execute=0
Fenetre armes: seconde tentative echouee elle aussi.
Process: pid=0
```

`g_spawned_soldiers` accumulait les pointeurs sans jamais les verifier. Un
soldat mort et detruit par le moteur, ou efface au rechargement d'une mission,
y restait - et `AddItem` finissait par ecrire dans un acteur libere.

`PruneCreatedSoldiers` verifie chaque entree : table de methodes a l'interieur
du module du jeu, et champ de type annoncant toujours `ACTOR_PLAYER`. Un acteur
libere ne presente presque jamais ces deux proprietes a la fois. La purge est
appelee a chaque tour depuis `UpdateSoldierMenu`, et une seconde fois juste
avant toute distribution d'arme; les cibles disparues sont retirees et, si plus
rien ne reste, la distribution est refusee proprement.

### Les noms : un critere faux pour le jeu en reseau

```
Table d'acteur: NON MESURABLE (0 emplacement(s) possible(s) sur 4 soldat(s)).
```

Le moteur (Actors.cpp:12410) :

```cpp
if(net && mode==PLRMODE_ACTIVE){
   net->GetPlayerName(pid, buf, sizeof(buf));
   return buf;                       // le nom vient du RESEAU
}
int i = tab->ItemI(TAB_I_HUM_FACE);
if(!i) return "unknown";
```

Le joueur joue en reseau : le nom du soldat actif vient du reseau, pas de la
table, et ses soldats peuvent porter un numero de visage nul en toute
legitimite. Exiger un numero non nul chez les quatre rejetait forcement tous
les emplacements - le critere lui-meme etait faux, pas la mesure.

Le critere ne verifie plus que la structure : table coherente, propriete 65
annoncee entiere, valeur dans les bornes. Chaque candidat est journalise avec
le numero qu'il donne, pour trancher sur donnees et non sur hypothese.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV131_SOLDIERS.exe`
  `772CA3412BAA2BA3DD6F818BA3B849F631B9ACDBAD1277807AA22B8F5C78CF8E`

## V132 - Les munitions, et la main qui n'etait pas vide

Livree le 4 septembre 2026 a 15:20:01 (heure locale). Base : V131.
Sauvegarde : `_GOLD_V132/`. Paquet : `release/V132_SOLDIERS/`.
Compagnons V105 repris tels quels. Protocole LAN inchange (version 6).

### Les munitions : une erreur sur la nature du nombre

La V119 avait pose `amount = 1` en raisonnant que le moteur passe « une arme ».
`S_item` dit autre chose :

```cpp
struct S_item{
   int itm;
   dword amount;            // la RESERVE de munitions
   dword bullets_in_stack;
};
```

`weapon_mods` lit d'ailleurs cette meme `amount` (`kItemReserveOffset = 0x18`)
comme la reserve de l'arme, pour la munition illimitee - une valeur que le jeu
valide depuis des versions. En passant 1, le trainer donnait une balle.

La quantite est desormais `reserve + balles` de l'arme du joueur, avec un
minimum de trente. Le releve d'inventaire journalise les munitions :

```
Inventaire du soldat 1 : 3 objet(s), identifiants = 30(0+0) 71(120+8) ...
```

### La main qui n'etait pas vide

`C_human::SetGun` :

```cpp
PI3D_frame hand = model->FindChildFrame("gun*", ENUMF_WILDMASK|ENUMF_ALL);
if(!hand) return;
gun = driver->CreateModel();
model_cache.Open(gun, name, mission.GetScene(), ...);
assert(!hand->NumChildren());        // le moteur attend une main VIDE
gun->LinkTo(hand);
```

Un soldat cree est la copie du soldat du joueur, dupliquee alors que celui-ci
tenait une arme : sa main contient deja une arme copiee, sans proprietaire.
`EmptySoldierHandsOnGameThread` la detache a la creation.

| Methode | Rang | Emplacement |
|---|---|---|
| `SetPos` | 3 | +0x0C - confirme par les teleportations |
| `LinkTo` | 33 | +0x84 |
| `NumChildren` | 34 | +0x88 |
| `GetChild` | 35 | +0x8C |
| `FindChildFrame` | 37 | +0x94 |
| `SetName` / `GetName` | 28 / 29 | +0x70 / +0x74 - confirmes, le moteur rend « Soldat 1 » |
| `Duplicate` | 38 | +0x98 - confirme, les soldats portent l'uniforme du joueur |

Trois points de l'enumeration sont valides par le jeu; les rangs
intermediaires s'en deduisent arithmetiquement. Ce n'est pas une transposition
du binaire de reference. Chaque etape du stub se garde : un resultat nul
arrete la sequence pour ce soldat sans toucher aux autres, et le nombre de
mains reellement videes est journalise - zero dirait que la cause est ailleurs.

### Ce que le journal du joueur confirme par ailleurs

```
Inventaire du soldat 1 : 3 objet(s), identifiants = 30 71 101
Fenetre armes: arme 7 (objet 101) donnee a 10 soldat(s) resultat=1.
```

Les armes arrivent, et le jeu ne s'arrete plus - y compris dans la sequence qui
l'avait tue en V130 : creer des soldats en plusieurs fois, puis n'en viser
qu'un. La purge de la V131 tient.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV132_SOLDIERS.exe`
  `0F97625B4BAFA15FE98FC992F73F51C0EB35881FCA9DE0C14875CE5B78603F2D`

## V133 - Les bons drapeaux, et le premier essai valable de l'equipement

Livree le 4 septembre 2026 a 15:25:23 (heure locale). Base : V132.
Sauvegarde : `_GOLD_V133/`. Paquet : `release/V133_SOLDIERS/`.
Compagnons V105 repris tels quels. Protocole LAN inchange (version 6).

### Les munitions, validees par le journal

```
Inventaire du soldat 1 : 3 objet(s), identifiants = 30(1+0) 14(79+32) 71(368+20)
```

Reserve et chargeur, identiques a ceux du joueur. La correction de la V132 -
`amount` est la reserve, pas un nombre d'exemplaires - est confirmee en jeu.

### « 0 main videe » : une mesure faussee par moi

```
Mains des soldats: 0 main(s) videe(s) sur 5 soldat(s) declenche=1 execute=1.
```

Le stub s'est execute proprement et n'a rien trouve. Conclure que la main est
vide aurait ete une erreur de plus : la mesure elle-meme etait fausse.

```c
#define ENUMF_ALL       0x0ffff
#define ENUMF_WILDMASK  0x10000
```

`SetGun` emploie `ENUMF_WILDMASK | ENUMF_ALL` = 0x1FFFF. La V132 passait
0xFFFFFFFF, en croyant « tout accepter » - ce qui active une quinzaine de bits
de filtrage au-dela de 0x1FFFF et fait rejeter toutes les frames. Corrige aux
drapeaux exacts du moteur.

### L'equipement : pourquoi les cinq echecs precedents ne comptent pas

| Version | Condition reelle au moment de l'essai |
|---|---|
| V116/V117 | base d'inventaire a +0x58 : `AddItem` ecrivait a cote |
| V118/V119 | idem, memoire deja corrompue avant l'equipement |
| V126 | `menu_id` a -1 sur certains soldats |

Le journal de la V130 a etabli le fait decisif :

```
Inventaire du soldat 1 : 1 objet(s), identifiants = 30
```

Un seul objet, et pas l'arme. Les cinq tentatives demandaient au moteur
d'equiper une arme absente de l'inventaire : aucune n'etait un essai de
l'equipement, elles testaient toutes un inventaire casse.

Conditions verifiees aujourd'hui, chacune par une ligne du journal du joueur :

| Condition | Preuve |
|---|---|
| l'arme entre dans l'inventaire | `3 objet(s), 30 71 101` |
| avec ses munitions | `71(368+20)` |
| garde du bandeau posee | `4 fonction(s) sur 5 protegee(s)` |
| `menu_id` valide | `identifiant 1000 pose chez 5` |
| acteurs detruits purges | `Soldats crees: N entree(s) retiree(s)` |
| pas d'arret a la distribution | quatre distributions successives |

`kForceEquip = true`. Ce n'est pas une supposition de plus : c'est le premier
essai portant sur une situation saine.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV133_SOLDIERS.exe`
  `CCF0913E5089AADF2C960861BD02836E745AB7AC45339B8D5DCF045C2CBCB532`

## V134 - L'equipement fonctionne; la main manque

Livree le 4 septembre 2026 a 17:54:41 (heure locale). Base : V133.
Sauvegarde : `_GOLD_V134/`. Paquet : `release/V134_SOLDIERS/`.
Compagnons V105 repris tels quels. Protocole LAN inchange (version 6).

### Ce que la V133 a etabli

```
Inventaire du soldat 1 : 2 objet(s), selectionne=1,
identifiants = 30(1+0) 71(368+20)
Fenetre armes: arme 6 (objet 71) donnee a 5 soldat(s) resultat=1.
Process: pid=0
```

`selectionne=1` : l'equipement a REUSSI. Les cinq tentatives precedentes
n'avaient jamais atteint cet etat, faute d'inventaire.

L'arret suit immediatement, et la ligne voisine l'explique :

```
Mains des soldats: 0 main(s) videe(s) sur 5 soldat(s) execute=1.
```

`C_human::SetGun` :

```cpp
PI3D_frame hand = model->FindChildFrame("gun*", ENUMF_WILDMASK|ENUMF_ALL);
if(!hand) return;                    // abandon ici
```

et `SetSelectedInvItem` pose `selected_inv_item = indx` AVANT d'appeler
`SetGun`. Le soldat annonce donc tenir une arme dont aucun modele n'a ete
attache; le moteur la dereference au tour suivant. `kForceEquip = false` de
nouveau, avec la cause etablie et non supposee.

### Le temoin

Un resultat nul ne prouve rien tant que la mesure n'est pas verifiee. Le
soldat du joueur tient une arme : sa main existe. Il est ajoute en derniere
position du tableau, releve, et exclu du detachement - il n'est pas question de
lui retirer son arme.

| Lecture | Conclusion |
|---|---|
| temoin >= 2, copies = 0 | la recherche marche; `Duplicate` ne reproduit pas la main |
| temoin = 0 | la recherche elle-meme est fausse (rang de `FindChildFrame`) |

Le stub enregistre `1 + NumChildren` par frame, ou zero si la main n'est pas
trouvee. Une seule creation de soldats suffit a trancher.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV134_SOLDIERS.exe`
  `DB3B4497B64BB9A29137ED61763D235FB8EDB7AE770002567DF123F3ADC3FF9F`

## V135 - Le temoin tranche; la sortie d'arme devient volontaire

Livree le 4 septembre 2026 a 19:24:05 (heure locale). Base : V134.
Sauvegarde : `_GOLD_V135/`. Paquet : `release/V135_SOLDIERS/`.
Compagnons V105 repris tels quels. Protocole LAN inchange (version 6).

### Ce que le temoin etablit

```
Mains: soldats crees = 1 1 1 1 1 1 | VOTRE soldat = 2.
```

| Lecture | Conclusion |
|---|---|
| temoin = 2 | main trouvee avec son arme : la recherche et le rang +0x94 sont bons |
| copies = 1 | main trouvee et VIDE : `Duplicate` la reproduit, et le moteur y verrait une main conforme |

Le diagnostic de la V134 - « la main n'est pas trouvee » - etait faux. Il
reposait sur la mesure de la V133, elle-meme faussee par des drapeaux
incorrects. Deux mesures fausses de suite avant d'obtenir la bonne : c'est
precisement pourquoi le temoin etait necessaire.

Il s'ensuit qu'en V133 `SetGun` allait jusqu'au bout :

```cpp
gun = driver->CreateModel();
model_cache.Open(gun, name, mission.GetScene(), ...);   // chargement disque
gun->SetAnimation(0, NULL);
gun->LinkTo(hand);
```

C'est ce chemin qui arrete le jeu, non l'absence de main.

### La sortie d'arme devient une action a part

`EquipLastItemOnGameThread` demande a la cible choisie de sortir le DERNIER
objet recu, et rien d'autre. Elle est appelee depuis une ligne dediee de la
fenetre J - « SORTIR l'arme - essai, peut arreter le jeu » - au lieu d'etre
enchainee a la distribution.

Consequence : la creation de soldats et la distribution d'armes, toutes deux
validees par le journal du joueur, ne portent plus aucun risque. Seul l'essai
volontaire peut arreter le jeu, et le joueur en decide le moment. Le conseil
donne est d'essayer d'abord sur UN soldat : si un passe et six arretent le jeu,
c'est une question de nombre et non d'etat.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV135_SOLDIERS.exe`
  `23C7E0F3E57DAA629116767B8F66B64E4A5EB4C2F3683C68B04F3004DE541BF6`

## V136 - Pourquoi les soldats crees ne peuvent pas porter l'arme

Livree le 4 septembre 2026 a 19:30:08 (heure locale). Base : V135.
Sauvegarde : `_GOLD_V136/`. Paquet : `release/V136_SOLDIERS/`.
Compagnons V105 repris tels quels. Protocole LAN inchange (version 6).

### Le raisonnement, appuye sur les mesures

| Fait | Preuve |
|---|---|
| l'arme entre dans l'inventaire | `4 objet(s), 30(1+0) 14(79+32) 71(368+20) 101(978+0)` |
| la main existe et est vide | temoin : soldats crees `1`, soldat du joueur `2` |
| l'equipement aboutit | `selectionne=1` (V133) |
| le jeu meurt APRES l'operation | V116/V117 et V133, deux chemins opposes |

Le dernier point est decisif. La V116 ecrivait l'index a la main, sans appeler
le moteur; la V133 appelait la fonction native complete, qui a reussi. Les deux
ont tue le jeu APRES coup. Ce n'est donc pas l'operation qui est en cause, mais
l'ETAT qu'elle laisse : un acteur non initialise qui declare tenir une arme.

Porter une arme engage la posture de visee et l'animation. `MissionLoad`
(`GameMission.cpp:1783`) est l'etape qui prepare ces donnees, et elle n'est pas
rejouee - c'est deja la cause des corps ecrases au sol. La meme piece manque
pour les deux symptomes, plus l'identite dans le bandeau.

### Ce qui change dans cette version

Le libelle de la ligne d'essai passe de « peut arreter le jeu » - un
avertissement vague - a « arrete le jeu sur cette version », qui est ce que les
mesures etablissent. Le joueur decide en connaissance de cause.

### Ce qu'il faudrait

Ouvrir la table de l'acteur et la lui appliquer. Deux adresses restent a
etablir sur le binaire du joueur, et elles ne seront pas posees au jugement -
la regle etablie apres quatre regressions tient.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV136_SOLDIERS.exe`
  `7ED990CF78F12B01B53EC7CC92D110D1359E43C7CF370C1CD4F3D49C405A8756`

## V137 - Mesure de la table par le groupe d'ennemi

Livree le 4 septembre 2026 a 19:46:19 (heure locale). Base : V136.
Sauvegarde : `_GOLD_V137/`. Paquet : `release/V137_SOLDIERS/`.
Compagnons V105 repris tels quels. Protocole LAN inchange (version 6).

### Pourquoi les trois mesures precedentes echouaient

Le critere retenu etait « le numero de visage est plausible chez tous les
soldats du joueur ». Le moteur le contredit :

```cpp
if(net && mode==PLRMODE_ACTIVE){
   net->GetPlayerName(pid, buf, sizeof(buf));
   return buf;                       // le nom vient du RESEAU
}
int i = tab->ItemI(TAB_I_HUM_FACE);
if(!i) return "unknown";
```

En partie reseau, le nom du soldat actif vient du reseau; son visage peut
valoir zero legitimement. Le critere rejetait donc tous les emplacements. La
mesure etait juste, le critere etait faux.

### Le nouveau critere

```cpp
// C_player::IsEnemy(asker)  - Actors.cpp:12707
case ACTOR_ENEMY:
   return (asker->GetTable(0)->ItemI(TAB_I_ENM_GROUP) == 0);

// C_enemy::IsEnemy(asker)   - Actors.cpp:15045
int my_group = tab->ItemI(TAB_I_ENM_GROUP);
case ACTOR_PLAYER:  return (my_group == 0);
case ACTOR_ENEMY:   if(my_group==2 || his_group==2) return false;
                    return (my_group != his_group);
```

`TAB_I_ENM_GROUP` vaut 74 (compte des declarations de `Tables.h`, ancre sur
`TAB_S_HUM_NAME = 64`). Ses valeurs possibles : 0 allemand, 1 russe, 2 civil.

`CollectReferenceEnemies` releve jusqu'a huit ennemis dans le snapshot du
radar. `MeasureActorTableOffset` n'accepte un emplacement que si le groupe y
tient dans ces bornes chez tous, en plus des verifications structurelles
existantes.

### Les trois voies que cette mesure ouvre

| Voie | Mecanisme | Certitude |
|---|---|---|
| le nom | ecrire le visage d'un vrai soldat dans la table du soldat cree | haute |
| recopie de table | copier le bloc de donnees d'un vrai soldat; les chaines sont INLINE (les descripteurs portent `max_string_size`), donc rien n'est partage | haute pour la copie, moyenne pour `TableUpdate` |
| rallier un ennemi | ecrire 1 dans son groupe : il change de camp des deux cotes, garde arme, posture, animations, nom et IA | tres haute |

### Franchise sur les limites

`TableUpdate` appelle `SetSelectedInvItem(1)`, la fonction qui arrete
aujourd'hui le jeu. Le pari est qu'elle echouait faute de table remplie. Si le
pari est perdu, le mur est le meme.

Et `TableUpdate` ne touche pas aux animations : la posture des soldats crees ne
sera pas reglee par cette voie.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV137_SOLDIERS.exe`
  `1312F1B848AD76CBFCFE7B96F8B7040181D336BE211F1DFB0BC889A661A8566C`

## V165 - Le journal ne coute plus d'images

Livree le 6 septembre 2026 a 01:45:20 (heure locale). Base : V164.
Sauvegarde : `_GOLD_V165/`. Paquet : `release/V165_JOURNAL_RAPIDE/`.

### 1. La mesure

Le journal du dernier essai : 1855 lignes, dont **1473 identiques dans la MEME
seconde** (`Enemy invisibility: player damage signature mismatch at 004219B0`,
18:06:53). Une rafale, pas une croissance lente.

### 2. Pourquoi chaque ligne coutait cher

`LogDiagnostic` faisait trois acces disque par ligne : `GetFileAttributesExW`
(taille), `CreateFileW` (ouvrir), `CloseHandle` (fermer). Ouvrir/fermer 1473
fois dans une seconde, sur le fil qui porte aussi l'overlay et les hooks.

### 3. La source de la rafale

`InstallPlayerDamageHookState` compare le prologue de `C_player::Hit` a
`83 EC 6C 53 55`. Une fois le hook pose, ce prologue commence par `E9` : la
comparaison ne peut plus jamais reussir, et l'echec etait journalise a chaque
image. Le message est conserve mais dit une seule fois par adresse.

### 4. Les trois mesures dans diagnostics.cpp

- le fichier reste OUVERT (un seul `WriteFile` par ligne), partage en lecture;
- le chemin calcule une fois, la taille verifiee toutes les 2 s;
- les lignes identiques consecutives sont comptees puis resumees par
  `(ligne precedente repetee N fois de suite)`.

### 5. Ce qui a ete retire

Le travail reseau commence puis interrompu (`NormalizeRalliedAiOwnership`,
`MaintainRalliedAiAuthority`) n'a jamais ete compile ni teste. Il est retire de
cette version et conserve dans `V165_reseau_non_teste.patch`.

`radar.cpp` conserve en revanche son anti-rebond sur `Radar read state`, qui va
dans le meme sens.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV165_JOURNAL_RAPIDE.exe`
  `810D9911E4C806D1B2F31FABD9EA12B8C2D2BC3C1FD9CE9A83AD99ED13E8C4AA`
- Aucune fonction de jeu modifiee : le diff ne touche que la journalisation.

## V164 - FEU ALL : ils enchainent les cibles sans empiler les ordres

Livree le 5 septembre 2026 a 17:04:53 (heure locale). Base : V163.
Sauvegarde : `_GOLD_V164/`. Paquet : `release/V164_FEU_ALL/`.

### 1. Le piege

`AddProgram(0, ...)` INSERE en tete. Redonner l'ordre a tout le monde toutes les
deux secondes ferait grossir sans fin la liste de programmes de chaque homme.

### 2. La regle

`g_fire_all_target` retient le couple homme -> cible. Toutes les 2 s :

- plus d'allie ou plus d'hostile -> le mode s'arrete en le disant;
- on ne renvoie QUE les allies dont la cible ne figure plus parmi les hostiles;
- `BuildOpenFirePairs` prend un parametre `only` pour se restreindre a eux;
- leurs limites de poursuite sont levees, puis le couple [MOVE, ATTACK] part.

Tant que chacun a une cible vivante, RIEN n'est envoye - aucun passage par le
site de triche.

### 3. Interface

Ligne `FEU ALL` placee juste au-dessus de `FEU`, bascule par Entree. La ligne
`FEU` et son nombre restent inchanges.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV164_FEU_ALL.exe`
  `CF3CE780780ED4EAE00DDF1096D819D992CF6A96FAADA4C11DC66C5F7199DAFB`

## V163 - Seuls les chasseurs partaient : les reglages d'IA sont dans la fiche

Livree le 5 septembre 2026 a 16:49:33 (heure locale). Base : V162.
Sauvegarde : `_GOLD_V163/`. Paquet : `release/V163_TOUS/`.

### 1. Ce n'etait pas le trainer

`Engagement: 10 homme(s) recoivent le COUPLE` - les dix recevaient l'ordre. Le
refus vient de `MayHunt` (`Actors.cpp:3787`) : trois conditions issues des
reglages de mission de chaque homme (poursuite permise, distance a son poste,
rayon de poursuite). Un garde statique abandonne l'ordre.

### 2. Ces reglages sont dans la fiche deja ecrite

`AI_e`/`AI_f` lisent `tab`, la fiche de l'acteur (`Actors.cpp:4855`), la meme
qu'a `+0x1A4`. `Tables.h` reserve les proprietes 96 a 127 a l'IA.

| propriete | role | valeur |
|---|---|---|
| 96 `TAB_E_AI_HUNT` | poursuivre | 2 (oui) |
| 97 `TAB_F_AI_HUNT_MAX_DIST` | rayon autour du poste | 500 m |
| 98 `TAB_F_AI_WATCH_MAX_DIST` | portee de vision | 120 m |

`FreeAllyAiLimits` n'ecrit que ce qui existe; le journal compte les proprietes
absentes. La chaine de repli de `AI_e` ne consulte la mission que si la valeur de
l'acteur vaut zero : ecrire 2 l'emporte sans toucher aux autres ennemis.

### 3. Le nombre au choix

La ligne FEU porte un nombre saisi au clavier, borne au nombre de rallies vivants.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV163_TOUS.exe`
  `BDC5B0B01128E59407BDDF527719B07189D282BDA8E5B122ACC9A7A910A53B17`

## V162 - C'etaient mes ordres qui empechaient leur IA

Livree le 5 septembre 2026 a 16:42:12 (heure locale). Base : V161.
Sauvegarde : `_GOLD_V162/`. Paquet : `release/V162_ENGAGE/`.

### 1. L'observation du joueur

« Dans la V153 les ennemis de mon equipe frappaient les autres ennemis. »

La V153 employait le groupe 1 et surtout N'AJOUTAIT AUCUN ORDRE aux rallies.
Leur IA tournait normalement. Les V158, V160 et V161 ont ajoute des ordres pour
resoudre un probleme que ces ordres creaient en partie.

### 2. Le deplacement seul etait malforme

`PRG_MOVE` avec `MR_ATTACK_REACH` isole. Le moteur ne s'en sert jamais seul
(`Actors.cpp:9711`) : il l'insere TOUJOURS devant un `PRG_ATTACK` deja present.
Et son traitement lit `next_key.GetSubject()` (`Actors.cpp:9360`), puis
`del = 2; //remove MOVE and ATTACK` (`9329`).

### 3. La correction

Deux `AddProgram` par homme : l'attaque, puis le deplacement insere devant elle.
La liste devient `[MOVE, ATTACK, ...]`. Corps de boucle verifie : 68 octets.

### 4. Le groupe 1 revient quand la mission le permet

`ChooseAllyGroup` retient, parmi 1 puis 3, la premiere valeur qu'aucun ennemi en
face n'utilise. Ennemis allemands -> groupe 1, exactement la V153.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV162_ENGAGE.exe`
  `8DABA60385CF60E1817D07025EDD9E7074732DA70413577B35F881071F9C13CB`

### Lecon

Quand une version anterieure se comportait mieux, comparer ce qui a change passe
avant toute nouvelle lecture de code, et avant tout nouvel ajout.

## V161 - A cent metres, personne ne voit personne : ils vont au contact

Livree le 5 septembre 2026 a 16:17:37 (heure locale). Base : V160.
Sauvegarde : `_GOLD_V161/`. Paquet : `release/V161_CONTACT/`.

### 1. La donnee etait dans le journal depuis la V143

```
Ralliement: ... le premier est a 104 metres de vous.
```

`WatchHumans` borne strictement l'ajout a la liste de surveillance
(`Actors.cpp:3920`) : au-dela de `AI_f(TAB_F_AI_WATCH_MAX_DIST)`, l'acteur n'est
MEME PAS AJOUTE. Aucune visibilite, aucun `IsSeen()`, aucun `PRG_ATTACK`.

### 2. Deux corrections inutiles, et pourquoi

- V158 : `PRG_ATTACK` -> supprime faute de `subject_seen` et parce que
  `dist >= fire_dist`.
- V160 : `PRG_WATCH` vers l'ennemi -> ils regardaient dans le vide.

Les deux corrigeaient l'aval d'un probleme dont la cause etait la distance.

### 3. La solution du moteur lui-meme

`Actors.cpp:9711` : quand il perd sa cible, il va la CHERCHER.

```cpp
AddProgram(++i, PRG_MOVE, S_prg_add((dword)&sub_pos, true, MR_ATTACK_REACH), true);
```

Ordre desormais pose : `PRG_MOVE` vers l'ennemi le plus proche, en courant,
`MR_ATTACK_REACH` (= 3, `Actors.h:54`). Automatique des le ralliement, rejouable
par la ligne FEU.

### 4. Ce qui ne peut pas etre contourne

Aucun soldat du jeu ne voit ni ne tire au-dela de la portee de vision de la
mission. Deux groupes a cent metres ne s'engageront jamais sans deplacement.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV161_CONTACT.exe`
  `3F53D88432BC776CD4BF539D0544EA8AD6483F2906BA65721C753E4AEC6215AB`

### Lecon

Trois versions sur le meme symptome, dont deux corrections sans effet. La mesure
decisive etait deja journalisee. Une donnee deja au journal doit etre confrontee
au symptome avant toute lecture de code.

## V160 - L'ordre d'attaque etait jete : il fallait les faire regarder

Livree le 5 septembre 2026 a 16:10:00 (heure locale). Base : V159.
Sauvegarde : `_GOLD_V160/`. Paquet : `release/V160_ENGAGEMENT/`.

### 1. `execute=1` et pourtant personne ne tire

Cause dans l'execution de `PRG_ATTACK` (`Actors.cpp:9581`) :

```cpp
if(dist >= fire_dist){
   if(MayHunt(sub_pos, key) && subject_seen){ ... }
   del = true;                 // sinon l'ordre est SUPPRIME
   break;
}
```

Plus, au-dessus : `if(!gun && !gun_mode){ ... break; }`.

Un ordre d'attaque sur une cible pas encore `subject_seen` est jete a la premiere
image. La V158 posait donc le mauvais ordre.

### 2. Le bon ordre : `PRG_WATCH`

`Actors.cpp:10290` tourne l'homme vers un point, l'y fait regarder en balayant,
et appelle `AI_solution` a chaque image avec une vigilance elevee. Des que la
cible est vue, l'IA ajoute son `PRG_ATTACK` elle-meme (`Actors.cpp:5172`).

`S_prg_add` : `d[0]` = pointeur sur la destination, `d[1]` = phases (4). Le stub
calcule `base + rang * 12` et l'ecrit dans `d[0]` avant chaque appel.

### 3. Automatique des le ralliement

`SetEnemyGroup` reussi -> `BuildOpenFirePairs` puis
`OrderWatchTowardOnGameThread`. La ligne FEU refait la meme chose a la demande.

### Verification

- Corps de boucle du stub : 36 octets (limite 127 des sauts courts).
- `AddProgram` rang +0x24, `ret 16`, quatre arguments empiles.
- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV160_ENGAGEMENT.exe`
  `7B5D77150EDC554D350977C07523088E61351398D80E92E97048426540C2D6B4`

## V159 - Les fenetres se rouvrent sur le dernier choix

Livree le 5 septembre 2026 a 15:58:52 (heure locale). Base : V158.
Sauvegarde : `_GOLD_V159/`. Paquet : `release/V159_MEMOIRE/`.

### 1. La demande

« Quand j'ouvre la fenetre je me retrouve en haut a chaque fois... meme logique
pour la touche J. »

### 2. On retient l'ACTION, pas le numero de ligne

Le contenu de la fenetre G varie : les lignes des allies rallies n'existent que
s'il y en a, « rendre le pilotage » que si un allie est en main, « SUPPRIMER »
que si des soldats ont ete crees. Un index garde tel quel designerait une action
differente d'une ouverture a l'autre.

Le trainer retient donc l'action validee plus son `index` pour les lignes propres
a un homme, et retrouve la ligne correspondante a la reouverture. Si elle a
disparu, il retombe sur la premiere.

### 3. Fenetre J

Structure fixe (trois lignes d'en-tete puis les armes), mais la liste des armes
depend de l'inventaire. On retient le RANG DE L'ARME et non le numero de ligne;
la selection est replacee a `3 + rang`, bornee. La cible etait deja conservee
par `settings.weapon_menu_target`.

L'essai volontaire de sortie d'arme n'est pas memorise : il ne doit jamais
devenir le choix par defaut.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV159_MEMOIRE.exe`
  `4E0BAF0D68DD27243B381B3359F9EF1CF8F25402C4A79EF138178780B7D282E1`

## V158 - Le declenchement, pas l'hostilite : la ligne FEU

Livree le 5 septembre 2026 a 15:52:05 (heure locale). Base : V157.
Sauvegarde : `_GOLD_V158/`. Paquet : `release/V158_FEU/`.

### 1. Le camp etait bon

Journal : `camps encore en face -> groupe 0 : 11`, les rallies au groupe 3, et
`GetRelation` rend `RELATION_ENEMY`. L'hostilite est correcte depuis la V157.

### 2. Le moteur ne tire que sur ce qu'il a VU

`AI_solution` (Actors.cpp:4906) n'ajoute `PRG_ATTACK` que si `first_seen` est non
nul, c'est-a-dire apres cone de vision, portee, ligne de vue et accumulation de
`seen_amount` dans `WatchHumans`.

Or un rallie et ses anciens camarades SE TOURNENT LE DOS : rien ne leur donne de
raison de se retourner. Deux groupes immobiles ne se verront jamais.

Fausses pistes ecartees : `TAB_E_AI_ENABLED` est teste sous `#ifdef EDITOR`
(inactif dans le jeu vendu); et un programme vide n'empeche pas l'IA de tourner
(`if(!program.size()){ if(AI_solution(...)) break; }`).

### 3. La ligne FEU

Pose l'ordre que l'IA se serait donne, meme fonction et meme forme :
`AddProgram(0, PRG_ATTACK, S_prg_add((dword)cible, partie), true)`.

`S_prg_add` = `dword d[5]` (H&D.h:556). Insere EN TETE comme le fait l'IA; le
programme n'est pas vide, l'IA prend le relais des qu'ils se sont vus.

Chaque rallie recoit l'ennemi hostile le plus proche de lui.

### Verification

- `AddProgram` rang +0x24, `ret 16`, quatre arguments empiles.
- Chaque couple revalide par `IsOrderableActor` avant l'ordre.
- Corps de boucle du stub : 30 octets (limite 127).
- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV158_FEU.exe`
  `1F9C836CB7F95A56BC253438808804BAB1D88DE8D54AFD19FB5ECACAF7C5B6EA`

## V157 - Un camp propre au joueur, et le switch trie comme le jeu

Livree le 5 septembre 2026 a 15:42:38 (heure locale). Base : V156.
Sauvegarde : `_GOLD_V157/`. Paquet : `release/V157_CAMP/`.

### 1. « Dans une autre mission ils ne tirent pas sur les autres ennemis »

`C_enemy::IsEnemy` decide par DIFFERENCE de camp (`Actors.cpp:15070`) :
`return (my_group != his_group);`

Le groupe 1 (russe) de la V142 marche tant que les ennemis sont allemands
(groupe 0). Dans une mission ou ils sont DEJA russes, les rallies se
retrouvaient dans le meme camp qu'eux : personne ne se bat. La V142 avait
raisonne sur une seule mission.

### 2. La correction : groupe 3

| condition | consequence |
|---|---|
| != 0 | pas nos ennemis, et ils ne nous visent pas |
| != 2 | ils ne sont pas « amis de tous » : ils combattent |
| != 0, 1 et 2 | hostiles a tous les camps, quelle que soit la mission |
| identique entre eux | ils ne se tirent pas dessus |

Surete verifiee : `Map_man.cpp:2050` fait `type = eg==0 ? 1 : 2` - une
comparaison, pas un indice de tableau.

### 3. `LIBERER` rend le vrai camp

Le camp d'origine est note (`g_rallied_origin`) au ralliement et restitue a la
liberation. Rendre 0 systematiquement aurait fait, en mission russe, des
ennemis du joueur.

### 4. Le switch ne bouclait jamais

`UpdateSwitchCycle` listait les soldats dans l'ordre de la liste d'acteurs,
alors que `PlayerSwitch` les TRIE par `menu_id` (`GameMission.cpp:3062`). Le
test « du dernier au premier » portait sur un ordre etranger a celui du jeu.
Tri par `menu_id` ajoute, avec l'offset deja mesure (+0x2B0).

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV157_CAMP.exe`
  `8838F6593C98E70C55D7935AD331CF08BF79AFBCAE13FEF75F4CAAAA6629C139`

## V156 - Le numero prouve par son appelant, et le switch etendu aux allies

Livree le 5 septembre 2026 a 15:32:14 (heure locale). Base : V155.
Sauvegarde : `_GOLD_V156/`. Paquet : `release/V156_SWITCH/`.

### 1. Pourquoi la camera ne bougeait pas

`declenche=1 execute=1` et aucun effet : le message 137 tombait hors des bornes
de l'aiguillage (`cmp eax,0x3F`, messages 1..64), et `ja default` rendait la
valeur par defaut.

L'erreur etait dans la lecture du prologue : c'est `48` (`dec eax`, index =
msg-1) et non un `add eax,imm` pris plus loin dans la fenetre. D'ou un decalage
de 116.

### 2. Le numero etabli par preuve

`C_game_camera::SetFocus` localisee a `0045DEB0` par son prologue exact repris
du binaire 2002 - une seule occurrence. Cinq appelants; l'un d'eux, `0041F2B8`,
tombe A L'INTERIEUR du cas numerote 21 par la table.

`CB_SETFOCUS = 21`, la valeur de 2002. L'enumeration n'avait pas change; la V155
affirmait le contraire, cette version l'annule sur ce point. Reste vrai : Deluxe
prend QUATRE arguments (`ret 16`) contre trois en 2002 (`ret 12`).

Lecon : identifier un message par la FORME d'un cas est une deduction; par un
APPELANT CONNU, c'est une preuve.

### 3. Le switch etendu

Aucune touche interceptee - 1 2 3 4 restent donc intactes par construction. Le
trainer observe le soldat actif :

| observe | action |
|---|---|
| du DERNIER soldat au PREMIER | entree dans le tour des allies, allie 1 |
| changement suivant, en mode allie | allie suivant |
| plus d'allie | sortie, camera rendue |

Faux positif possible (« 1 » depuis le dernier soldat) neutralise par un delai
de 400 ms apres toute frappe de chiffre. Ligne `SWITCH ...` pour couper.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV156_SWITCH.exe`
  `61CEBB4801DA18C7D0F20195A23B1D8083DC7B8836694D4F1D1ACFEDD61C4C8F`

## V155 - La camera sur l'homme pilote

Livree le 5 septembre 2026 a 15:15:40 (heure locale). Base : V154.
Sauvegarde : `_GOLD_V155/`. Paquet : `release/V155_MAIN_LIBRE/`.

### 1. Le mecanisme

`cbProc(CB_SETFOCUS, 1, 0, 0)` sur l'allie pris en main; sur le soldat du joueur
quand il rend le pilotage. Le moteur fait alors
`mission.game_cam.SetFocus(frm_head ? frm_head : frame, frame, this)`.

### 2. Deux pieges evites par lecture

**Arguments** : 2002 declare trois parametres (`?cbProc@C_actor@@UAEKKKK@Z`,
`ret 12`); Deluxe finit par `ret 16` - QUATRE arguments. Signature changee entre
les versions. Meme piege que la V151, desamorce par la regle posee alors.

**Numero du message** : `CB_SETFOCUS` = 21 en 2002; sur Deluxe la table
d'aiguillage couvre 117..180. Etabli en decodant la table a deux niveaux :

| msg | code | identification |
|---|---|---|
| 138 | `mov edx,[ecx+0x278]` ; `mov ecx,[esi+0x28]` ; `cmp` ; `sete al` | `CB_IS_FOCUSED` |
| 137 | `mov eax,[ebp+0x0C]` ; `sub eax,0` ; `je` ; `dec eax` ; `je` | `CB_SETFOCUS` |

Les deux se confirment mutuellement (consecutifs dans l'enumeration).

### 3. La touche switch : impossible, prouve

`PlayerSwitch` filtre `ACTOR_PLAYER` ET appelle `SetActive`, vide pour `C_enemy`.
Deux murs. Limite deja annoncee en V142.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- Methode verifiee au rang +0x04 et dans le module avant tout appel.
- SHA-256 `HDFinalAdvancedV155_MAIN_LIBRE.exe`
  `DEB7092F377E9EC4FECB13293AD3A549258A294E1F584BBFE03B62A0876F72BE`

### Reste

- la direction de visee de l'allie : `aim_dir` reste a situer dans l'objet.

## V154 - Main libre : piloter un allie comme on se pilote soi-meme

Livree le 5 septembre 2026 a 14:54:12 (heure locale). Base : V152 restauree.
Sauvegarde : `_GOLD_V154/`. Paquet : `release/V154_MAIN_LIBRE/`.

### 1. La V153 est retiree

« non je ne veux pas la comme ca, restaure la derniere version ». Les sources
repartent de `_GOLD_V152`; le mode « PLACER un par un » n'existe plus.

### 2. La demande

« comme le noclip qui existe deja, meme si je ne controle pas ce soldat mais je
peux le deplacer dans la map comme je veux n'importe ou ».

### 3. Pourquoi c'est possible sans rien inventer

Le crochet du noclip est pose sur `C_human::Tick`, que TOUS les humains
executent, et le trampoline compare l'acteur en cours a celui publie :

```
3B 0D <adresse>    cmp ecx,[acteur publie]
```

`UpdateNoclip` publiait `snapshot->player_object_address` EN DUR. La
modification tient en un choix d'acteur; toute la mecanique de vol est celle
deja validee.

### 4. Interface

```
MAIN LIBRE piloter l'allie   3    <  tapez le numero
MAIN LIBRE -> rendre le pilotage a mon soldat
```

### 5. Suretes

- Revalidation a chaque image (vtable dans le module, type, vivant); liberation
  automatique s'il meurt ou quitte la liste des rallies.
- Reseau : le crochet de publication compare lui aussi l'acteur
  (`cmp ebx,[acteur]`), il ne publiera jamais la position de l'allie a la place
  de celle du joueur.

### Verification

- SHA-256 `HDFinalAdvancedV154_MAIN_LIBRE.exe`
  `4DA0C94555741DA639A5BBF248DF3D4EE1C9DAF8BF15031C64D69D00DF0F4DF9`

## V152 - L'ordre de deplacement vide la liste au lieu de s'y inserer

Livree le 5 septembre 2026 a 14:41:34 (heure locale). Base : V151.
Sauvegarde : `_GOLD_V152/`. Paquet : `release/V152_RALLIEMENT/`.

### 1. Le constat

« quand je clique sur VENIR il ne s'arrete pas a cette place, il reste bouger ».

### 2. La cause

`AddProgram(0, PRG_MOVE, ...)` : le premier argument est une POSITION
D'INSERTION, pas un remplacement. L'ordre etait pose en tete d'une liste
contenant deja la patrouille de mission de l'ennemi, qui reprenait donc sa ronde
apres etre venu.

### 3. La correction

Le moteur vide la liste avant tout ordre neuf (`ClrProgram`, Actors.cpp:14293).
`ClrProgram` n'est pas virtuelle, mais `DelProgram(pos, net_send)` l'est. Le stub
retire l'ordre de tete douze fois, puis ajoute le sien. `PRG_READY` etant
automatique en fin de liste, l'homme arrive reste sur place.

### 4. Verifie dans le binaire

```
vtable +0x24 -> 00429480   ret 16  (4 arguments)   AddProgram
vtable +0x28 -> 004294C0   ret 8   (2 arguments)   DelProgram
```

Encodage rejoue hors du trainer : `jnz -14` correct, corps de boucle 39 octets.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV152_RALLIEMENT.exe`
  `11A2381DC5DC51EDCA5E2F8AFED0B1AC293F1A2238361D6E4198511CE230FAE7`

## V151 - Un argument manquant a `SetSelectedInvItem` : la cause, depuis huit versions

Livree le 5 septembre 2026 a 14:24:39 (heure locale). Base : V150.
Sauvegarde : `_GOLD_V151/`. Paquet : `release/V151_RALLIEMENT/`.

### 1. La cause

```cpp
void SetSelectedInvItem(int indx, bool net_send = true)
```

Deux parametres. Desassemblage sur le binaire du joueur :

```
004076E0  8b 44 24 04   mov eax,[esp+4]      ; indx
          39 86 58 02   cmp [esi+0x258],eax
+0x1AD    c2 08 00      ret 8                ; DEPILE HUIT OCTETS
```

Le stub n'empilait qu'un argument. Chaque appel laissait `esp` quatre octets trop
haut, en plein dans les registres sauves par le `pushad`. Au `popad`, le jeu
restaurait des valeurs decalees et repartait vers une adresse arbitraire.

### 2. Ce que cela explique

- « parfois ca marche, parfois ca tue » : hasard pur.
- Temoin V148 « jamais entre dans le code » : le processus etait deja detruit.
- V149 sans effet : la course sur le site de triche n'etait pas en cause.
- **Les huit echecs sur les soldats CREES avaient la meme cause.** Leur fiche
  manquante n'a peut-etre jamais ete le probleme.

### 3. La correction

Les deux sites d'appel empilent les deux arguments, `net_send = false` (sinon le
moteur emettrait un message reseau bati sur une valeur ramassee au hasard).

Sauts courts revérifiés : corps de boucle a 100 octets, sous la limite de 127.

### 4. Audit des autres appels natifs

`AddItem`, `Reload`, `AddProgram`, `SetActive`, `TableUpdate` : tous corrects.
`SetSelectedInvItem` etait le seul faux.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV151_RALLIEMENT.exe`
  `DA66C0E1636D8A0EE8E0C1802ED502BEC184663F1B510F3BE4D41623BE523590`

### Lecon

La regle « on ne pose une valeur qu'apres l'avoir lue dans une instruction du jeu »
n'avait jamais ete appliquee aux CONVENTIONS D'APPEL. Tout appel natif ajoute au
trainer doit desormais voir son `ret N` verifie dans le binaire.

## V150 - La sortie d'arme est coupee : bilan de l'enquete

Livree le 5 septembre 2026 a 14:06:49 (heure locale). Base : V149.
Sauvegarde : `_GOLD_V150/`. Paquet : `release/V150_RALLIEMENT/`.

### 1. Ce que la V149 a appris malgre son echec

`PatchCodeSafely` a refuse d'ecrire pendant CINQ secondes avant la remise en
etat. Dans son implantation, un `OpenThread`/`SuspendThread` en echec fait rendre
faux - ce qui arrive quand des threads disparaissent. Le jeu MOURAIT DEJA. La
suspension n'a pas empeche la mort, elle l'a rendue visible.

### 2. Bilan des hypotheses

| hypothese | version | verdict |
|---|---|---|
| munitions trop nombreuses | - | fausse (`Inventory.cpp:360`, et le 1er passage reussit) |
| deux distributions dans la meme image | V146 | fausse |
| pointeurs vers acteurs morts | V147 | fausse, aucun homme rejete |
| `AddItem`/`Reload`/`SetSelectedInvItem` | V148 | elimines : `etape 0` |
| course sur le site de triche | V149 | fausse |

La cause reste inconnue. Cinq versions dépensees; la sixieme ne sera pas une
sixieme supposition.

### 3. Decision

- Coupe : `SetSelectedInvItem` (mettre l'arme en main).
- Conserve : `AddItem` + `Reload` (l'arme entre dans le sac, chargee).

Seul chemin n'ayant jamais arrete le jeu, deja valide par le joueur. Perte
faible pour un rallie : il porte deja son arme.

### 4. Porte laissee ouverte

La derniere ligne de la fenetre J reste un essai VOLONTAIRE, un homme a la fois,
avec un temoin qui distingue enfin « temoin a zero » de « lecture impossible » -
deux situations opposees que la V148 confondait. Compteurs de lectures reussies
et echouees, et instant du premier echec.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV150_RALLIEMENT.exe`
  `120B2981C5BD175F56CC950EE86BE4DC0AE98448D9D33FDF467453A8D2303DA3`

## V149 - Ecriture sur du code en cours d'execution : la cause reelle

Livree le 5 septembre 2026 a 13:57:25 (heure locale). Base : V148.
Sauvegarde : `_GOLD_V149/`. Paquet : `release/V149_RALLIEMENT/`.

### 1. Le temoin a tranche

```
TEMOIN: ... etape 0 = aucune - le jeu n'est jamais entre dans le code.
Temoin lu : non - jamais rien lu.
```

Le code injecte n'a jamais execute une seule instruction. Le jeu meurt AVANT,
pendant l'installation du saut. Trois versions avaient cherche la cause dans le
CONTENU du code; elle etait dans la FACON DE L'INSTALLER.

### 2. La cause

Le site de triche est traverse a chaque image - etabli des la V107, qui a du y
poser un garde de re-entree pour cette raison. Y ecrire cinq octets pendant que
le processeur les execute est une course : gagnee presque toujours, perdue de
temps en temps, et le jeu execute alors une instruction a moitie reecrite.

| observation | explication |
|---|---|
| 1er passage reussi, 2e fatal, toujours | hasard; chaque passage est une chance de perdre |
| V145 : miroir + J dans la meme image | deux installations coup sur coup |
| V146 et V147 sans effet | elles corrigeaient le contenu, pas l'installation |
| anciens plantages sur soldats crees | meme cause, jamais expliquee |

### 3. La correction

`TrainerProcess::PatchCodeSafely` : suspend TOUS les threads du jeu, refuse si un
`Eip` est dans la zone visee ou dans les 15 octets qui la precedent, ecrit, puis
relance.

- `InstallHookSafely` : 24 tentatives a 4 ms, puis abandon SANS ECRIRE.
- `RestoreHookSafely` : 250 tentatives (~1 s), puis ecriture inconditionnelle -
  laisser le saut en place serait un arret certain.

Applique aux 12 installations et aux 19 remises en etat : creation de soldats,
ordres, teleportation, vehicule, armes.

### 4. Les soldats crees quittent les fenetres d'armes

Demande explicite du joueur, et fondee : sans fiche, ils ne peuvent pas soutenir
l'etat « je tiens une arme ». `UpdateWeaponMenu` et le miroir ne visent plus que
`g_rallied_enemies`. L'ancien chemin est conserve sous `#if 0`.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV149_RALLIEMENT.exe`
  `E4539C15CF9099A7438984A0088A3F932BC17C53C0A81149DB3CD446FF767A05`

### Lecon

Trois versions depensees a supposer. La quatrieme a pose un temoin, et la reponse
est venue au premier essai, en designant un endroit qu'aucune supposition
n'aurait atteint.

## V148 - Deux diagnostics faux : on instrumente au lieu de supposer

Livree le 5 septembre 2026 a 13:44:07 (heure locale). Base : V147.
Sauvegarde : `_GOLD_V148/`. Paquet : `release/V148_RALLIEMENT/`.

### 1. Les V146 et V147 se sont trompees

- V146 : « le miroir et la fenetre J distribuent dans la meme image ». Corrige,
  le jeu a plante a l'identique.
- V147 : « le trainer ecrit dans des acteurs morts ». Corrige, le jeu a plante a
  l'identique - et le journal ne porte AUCUNE ligne « morts ou disparus » : la
  verification n'a rejete personne.

### 2. Ce que les journaux etablissent

```
paquet 1 ... resultat=1        (les deux sessions)
paquet 2 ... resultat=0        (les deux sessions)
Process: pid=0                 (16 ms puis 39 ms apres)
```

`resultat=0` n'est pas un refus : le drapeau de fin est ecrit par la DERNIERE
instruction du code injecte. Le jeu entre dans ce code et n'en ressort pas.
Jamais au premier passage, toujours au second, quel que soit l'effectif.

### 3. Deux temoins

| offset | contenu |
|---|---|
| `+0x21C` | rang de l'homme en cours (`mov [temoin], esi`) |
| `+0x220` | etape : 1 `AddItem`, 2 `Reload`, 3 `SetSelectedInvItem`, 4 termine |

Relus toutes les 2 ms pendant l'attente, conserves, et journalises si le code ne
rend pas la main.

Codage verifie hors du trainer : les 36 octets de temoins portent le corps de
boucle a 96 octets, sous la limite de 127 des sauts courts. Un depassement aurait
ete tronque silencieusement par le cast en `uint8_t`.

### 4. Trois securites

- `kEquipBatchSize = 1` : un homme par passage, temoin sans ambiguite.
- `kEquipBatchRestMs = 400`.
- Arret de la file au premier echec, au lieu d'enchainer sur un jeu mourant.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- Distances des sauts courts recalculees : 96 / 127 au maximum.
- SHA-256 `HDFinalAdvancedV148_RALLIEMENT.exe`
  `9764BE47C404D6144C24425410098FDC9872F435D7F2DAFB55AD33AA62BD5701`

### Attendu du prochain essai

Une ligne `TEMOIN:` designant l'homme et l'etape exacts.

## V147 - Des pointeurs vers des acteurs morts au combat

Livree le 5 septembre 2026 a 13:34:42 (heure locale). Base : V146.
Sauvegarde : `_GOLD_V147/`. Paquet : `release/V147_RALLIEMENT/`.

### 1. L'hypothese du joueur (munitions trop nombreuses) est ecartee

`Inventory.cpp:360` : `AddItem` range `num` dans un champ entier
(`items[i]->amount += num`), il n'alloue jamais `num` objets. Et le journal le
dement directement : le PREMIER paquet a reussi avec les memes 978 munitions et
le meme objet 101.

### 2. `resultat=0` ne veut pas dire « refuse »

Le drapeau de fin est ecrit par la derniere instruction du code injecte.
`execute=0` signifie donc que le jeu est entre dans ce code et n'en est jamais
ressorti. Seize millisecondes plus tard, le processus avait disparu.

### 3. La cause

```
13:28:42  cinquante ennemis rallies
13:29:01  envoyes au combat par la carte
13:29:08  fenetre G ouverte      <- dernier nettoyage
13:29:35  distribution d'arme    <- vingt-sept secondes plus tard
```

`CollectMissionEnemies` - seule fonction qui retirait les rallies disparus - est
appelee ligne 19242, APRES la garde de la ligne 19126 qui rend la main quand la
fenetre G est fermee. Fenetre fermee, aucun nettoyage.

Le trainer a donc garde vingt-sept secondes durant les adresses d'hommes morts
au combat. Premier paquet : quatre vivants, reussi. Second : un mort, et
`AddItem` a ecrit dans une memoire liberee.

Meme accident que la V131 sur les soldats crees, rendu possible par la file de
la V146 qui etale la distribution sur plusieurs secondes.

### 4. Les corrections

- `PruneRalliedEnemies` tourne a CHAQUE image (throttle 250 ms), fenetre ouverte
  ou non.
- `IsRalliedActorUsable` verifie chaque homme au moment ou il entre dans le
  paquet : vtable dans le module, type ACTOR_ENEMY, `resistance > 0`.
- `IssueMoveOrderOnGameThread` filtre pareillement avec `IsOrderableActor`
  (joueur ou ennemi, vivant) : un ordre arme peut etre consomme plusieurs
  secondes plus tard.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV147_RALLIEMENT.exe`
  `EFEFD002726FD12040CF8E047890D116535F1DBCA720194893F1BA2AF8DBB3E7`

### Lecon

Tout pointeur d'acteur conserve plus d'une image doit etre revalide avant usage,
et la revalidation doit tourner independamment de l'interface qui l'a produite.

## V146 - L'arret du jeu a la distribution d'arme : file unique et petits paquets

Livree le 5 septembre 2026 a 13:28:09 (heure locale). Base : V145.
Sauvegarde : `_GOLD_V146/`. Paquet : `release/V146_RALLIEMENT/`.

### 1. L'ordre par la carte est valide

« parfait pour K qui les envoie dans une position ». Le decouplage de la V145
est confirme en jeu.

### 2. Le journal date l'arret a la milliseconde

```
13:21:12.981  Miroir d'arme: objet 14 ... chez 25 soldat(s) (25 l'ont deja en main)
13:21:12.981  Fenetre armes: 25 allie(s) ... recoivent l'arme 14 ET la sortent
13:21:15.029  Process: pid=0
```

Trois faits dans la meme milliseconde : le miroir ET la fenetre J ont distribue
sur les memes 25 acteurs, chacun posant son detour sur le site de triche; et les
25 TENAIENT DEJA cette arme. Le travail etait entierement inutile.

Or `C_human::SetGun` (`Actors.cpp:6318`) libere le modele courant, en cree un
neuf, le CHARGE par `model_cache.Open` et l'accroche a la main. Cinquante
chargements de modele depuis un detour, dans une seule image.

C'est la V145 qui a rendu ce cumul possible, en etendant le miroir aux rallies
sans le serialiser avec la fenetre J.

### 3. Les trois corrections

- **Une seule file** : `QueueEquipRequest` / `ProcessPendingEquip`. Le miroir et
  la fenetre J l'alimentent tous deux; une demande nouvelle remplace la
  precedente.
- **Petits paquets** : `kEquipBatchSize = 4`, `kEquipBatchRestMs = 250`, au plus
  un paquet par image.
- **Saut des hommes deja armes** : `ReadHeldWeapon` avant chaque paquet. Dans le
  cas du journal, cela aurait supprime la totalite du travail.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV146_RALLIEMENT.exe`
  `A4F7001D873C88B32C04C031DB7DBD31F41BE08B4253E64F7DC7C1C1775C1BA3`

### Lecon

Deux mecanismes qui ecrivent au meme endroit du jeu doivent partager une file,
pas s'ignorer.

## V145 - L'ordre par la carte detache de la teleportation, miroir d'arme etendu

Livree le 5 septembre 2026 a 13:11:09 (heure locale). Base : V144.
Sauvegarde : `_GOLD_V145/`. Paquet : `release/V145_RALLIEMENT/`.

### 1. Le ralliement est valide par le joueur

« OUI ca marche parfaitement » - les rallies se battent de son cote, VENIR les
rappelle, la fenetre J leur donne une arme et ils la sortent.

### 2. « Je ne peux pas l'envoyer sur carte dans une position »

Cause, en une ligne de son journal :

```
[TEST TELEPORT #1] LBUTTON_EDGE enabled=0 map_open_cached=0 accepted=0
```

TOUTE la machinerie de la carte etait conditionnee par `teleport_map_enabled`,
la case « teleportation par la carte ». Or `ApplyPendingTeleport` traite deja
l'ordre EN PRIORITE sur la teleportation : les deux fonctions sont
independantes.

Un drapeau `map_usable = teleport_map_enabled || map_serves_orders` remplace la
case dans les cinq points de controle. `g_soldier_order_targets` n'etant vide
qu'a la consommation de l'ordre, l'armement survit jusqu'au clic.

### 3. « Ils ne portent pas la meme arme que moi »

Le miroir d'arme ne visait que `g_spawned_soldiers`. Il couvre desormais les
deux troupes, avec la distinction du chapitre 41 : les crees recoivent l'arme
dans le sac, les rallies la SORTENT.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV145_RALLIEMENT.exe`
  `4D01825EB30864655401449613613201DD78C4870204EECFBE2D8F392A930DA6`

## V144 - La limite de trente-cinq hommes, et le compte fausse

Livree le 5 septembre 2026 a 12:58:16 (heure locale). Base : V143.
Sauvegarde : `_GOLD_V144/`. Paquet : `release/V144_RALLIEMENT/`.

### 1. Le journal valide le ralliement

```
1 / 25 / 40 ennemi(s) demande(s), autant passe(s) de votre cote (groupe RELU)
Ordre de deplacement: 1 puis 26 soldat(s) ... execute=1
67 ennemis comptes dans la mission, aucun plantage
```

### 2. Bug 1 - l'ordre de deplacement plafonnait a 35 hommes

`IssueMoveOrderOnGameThread` ecrivait son code DEROULE, 21 octets par soldat,
et le garde refusait tout code atteignant `0x300` = 768 octets. Plafond reel :
35 hommes. 26 passait, 66 etait refuse avec un simple `resultat=0`.

Le code devient une BOUCLE sur un tableau d'acteurs.

| | avant | apres |
|---|---|---|
| taille du stub | 21 octets x N | 74 octets, constants |
| plafond | 35 hommes | 768 |

`esi` et `ebx` traversent l'appel (convention MSVC). Emission rejouee hors du
trainer : quatre sauts justes, retour sur `0049FC15`.

### 3. Bug 2 - « 66 sur 63 ennemis »

La purge ne retirait un rallie que si son TYPE n'etait plus `ACTOR_ENEMY`, or un
ennemi detruit garde son type. Les disparus restaient comptes, et retiraient de
`EnemiesStillHostile()` des elements qui n'existaient plus : le joueur a rallie
40 hommes la ou 37 restaient.

Le critere devient la PRESENCE dans la liste d'acteurs du jeu.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV144_RALLIEMENT.exe`
  `5E649B802093D576E62058D8577A938FC0743CF0FC05BB485742215F7DB8B4B2`

### Reste a tester

- la distribution d'armes aux rallies (aucune ligne `Fenetre armes` au journal)
- est-ce qu'un rallie se bat reellement pour le joueur
- le comportement chez les amis en LAN

## V143 - Le plus proche d'abord, et « venir a moi »

Livree le 5 septembre 2026 a 12:45:10 (heure locale). Base : V142.
Sauvegarde : `_GOLD_V143/`. Paquet : `release/V143_RALLIEMENT/`.

### 1. La question du joueur a revele un defaut

« Quand je selectionne 1, est-ce qu'il se place a cote de moi ? » Non : le
ralliement n'ecrit qu'un entier, l'acteur ne se deplace pas.

Mais la question a mis au jour un vrai defaut de la V142 : `EnemiesStillHostile()`
rendait les ennemis dans l'ordre de la liste d'acteurs du moteur, sans rapport
avec la distance. Rallier un seul pouvait designer quelqu'un a l'autre bout de la
carte - le joueur n'aurait rien vu et aurait conclu a un echec.

### 2. Les trois corrections

- **Classement par distance** : `CollectMissionEnemies` retient la position de
  chaque ennemi et trie du plus proche au plus loin.
- **Distance affichee** : `RALLIER 1 ennemis (le plus proche a 23 m)`, et le
  journal rappelle qu'un rallie ne se deplace pas.
- **Ligne `VENIR`** : les rallies viennent a la position du joueur, par le meme
  `IssueMoveOrderOnGameThread` - donc toujours `AddProgram(0, PRG_MOVE, ...)`.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV143_RALLIEMENT.exe`
  `AC2DB407536694275EEA178E9C2B81F908C469F20E09A275E94A3841B850A0C9`

## V142 - Rallier les ennemis : comptage, choix du nombre, armes et ordres

Livree le 5 septembre 2026 a 12:38:22 (heure locale). Base : V141.
Sauvegarde : `_GOLD_V142/`. Paquet : `release/V142_RALLIEMENT/`.

### 1. Decision : la mise en main des soldats CREES est abandonnee

Le joueur l'a tranche apres le test de la V141. Un acteur cree n'a pas de fiche,
et `MissionLoad` ne s'execute qu'au chargement d'une mission. Huit tentatives,
huit echecs. Ce qui est acquis sur les soldats crees est conserve.

### 2. Le ralliement est d'une autre nature

Un ennemi de la mission a ete fabrique PAR LE JEU. Le faire changer de camp
n'ecrit qu'un entier - `TAB_I_ENM_GROUP`, propriete 74 - lu par les deux
fonctions d'hostilite (`Actors.cpp:12708` et `15045`) :

```cpp
// cote joueur : lui, est-il mon ennemi ?
case ACTOR_ENEMY:  return (his_group == 0);         // groupe 1 -> non
// cote lui : qui est mon ennemi ?
case ACTOR_PLAYER: return (my_group == 0);          // ne vise plus le joueur
case ACTOR_ENEMY:  return (my_group != his_group);  // 1 != 0 -> les Allemands
```

Et l'IA suit : `C_human::WatchHumans` (`Actors.cpp:3937`) construit sa liste de
surveillance avec ce meme `IsEnemy`.

Groupe 1 (russe) et non 2 (civil) : le civil est « friend of everyone », il ne
combattrait pour personne.

### 3. Livre

- comptage des ennemis de la mission, a chaque image, par le TYPE d'acteur
- fenetre G : `ENNEMIS`, `RALLIER` (nombre tape), `LIBERER`, `ALLIES`
- fenetre J : les deux troupes, avec sortie d'arme pour les rallies seulement
- ordres par la carte, en groupe et un par un
- radar : un rallie passe en allie, et sort des aides a la visee

### 4. Deux troupes, deux traitements pour l'arme

`GiveWeaponOnGameThread` prend un parametre `force_equip`. Les soldats crees ne
sortent pas l'arme (pas de fiche); les rallies la sortent par les fonctions
natives. La distinction porte exactement sur la cause etablie.

### 5. Limite annoncee

Piloter un rallie au clavier est impossible : `SetActive` n'est surchargee que
par `C_player` (`Actors.cpp:12730`), `C_enemy` herite d'une version vide
(`H&D.h:944`). Changer le type ou la vtable a ete ecarte - les dispositions
memoire des deux classes different.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- Le groupe ecrit est RELU avant d'etre compte; le journal donne le nombre reel.
- Un rallie dont le type n'est plus ACTOR_ENEMY est retire a l'image suivante.
- SHA-256 `HDFinalAdvancedV142_SOLDIERS.exe`
  `73DED65EA5F203C000C91CC225F58E277D6BE9CAC76EDF3FCCE538846A77FA63`

### Reste

- comportement chez les amis en LAN : a tester a deux
- posture des soldats crees
- traits de balles ennemies invisibles chez les amis du joueur
- bug de deplacement du joueur : trois verifications toujours sans reponse
- volumes de collision apres reparation de carrosserie

## V141 - L'arme en main, par la fiche et `TableUpdate`

Livree le 4 septembre 2026 a 20:36:03 (heure locale). Base : V140.
Sauvegarde : `_GOLD_V141/`. Paquet : `release/V141_SOLDIERS/`.

### 1. Sept tentatives, une seule erreur

Sept facons de mettre l'arme dans la main des soldats crees ont ete essayees,
toutes fatales, et toutes procedaient de la meme maniere : forcer l'etat depuis
l'exterieur. Le moteur ne procede jamais ainsi (`GameMission.cpp:1758`) :

```cpp
act = CreateActor(type);
act->SetFrame(frm);
act->MissionLoad(&lc.ck, 0);      // <- jamais executee par le trainer
```

et `MissionLoad` d'un humain se reduit a `tab->Open(handle, TABOPEN_FILEHANDLE |
TABOPEN_UPDATE)` (`H&D.h:973`). **La seule chose qui manque a un soldat cree est
le contenu de sa fiche.**

`TABOPEN_UPDATE` declenche `TableUpdate` (`Actors.cpp:7343`), qui vide
l'inventaire, y remet ce que la fiche indique, et **designe le premier poste** -
`if(!i) SetSelectedInvItem(1, false)`. C'est le moteur qui met l'arme en main.

### 2. La recopie de fiche est obligatoire

`C_player::TableUpdate` (`Actors.cpp:12790`) recalcule la sante :

```cpp
float endurance = (tab->ItemI(TAB_I_HUM_BAR5_ENDURANCE)-30)/70.0f;
resistance = Max(1, 200 + (int)(endurance*1400.0f));
```

Fiche vide -> endurance 0 -> `Max(1, -400)` = **1 point de vie**. Sans recopie
prealable, cette voie serait nuisible.

### 3. `TableUpdate` a +0xBC, lu et non suppose

| rang | adresse | contenu lu |
|---|---|---|
| +0xB4 | `004257D0` | `mov eax,[ecx+0x1A4]` — `GetTable` |
| +0xB8 | `004257F0` | `mov eax,004FF118; ret 4` — constante, `GetTemplate` |
| +0xBC | `0042A550` / `00425800` / `004364A0` | trois implantations distinctes |

### 4. Ce que fait la fenetre J desormais

1. recopie du bloc de fiche d'un vrai soldat (identite, visage, endurance);
2. ecriture de l'arme choisie au premier poste de `TAB_I_HUM_INV_LIST` (8) et de
   sa reserve dans `TAB_I_HUM_INV_AMOUNT` (9);
3. appel de `TableUpdate(0, true)` sur le thread du jeu.

Repli sur `GiveWeaponOnGameThread` si l'une des trois etapes echoue.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- Emission du stub rejouee hors du trainer : les quatre sauts tombent juste, le
  retour atterrit sur `0049FC15`, taille 83 octets.
- Garde d'appel : chaque acteur vise doit presenter au rang +0xBC une adresse
  lisible dans le module du jeu; un seul manquant et tout est refuse.
- SHA-256 `HDFinalAdvancedV141_SOLDIERS.exe`
  `9726513D0965FF57377A67643F86C87C2AF13E73A4FAFAFB36EC30E7B8E3C142`

### Correction de la V140

La V140 affirme que le moteur ne range aucune taille de donnees. Inexact :
`data_size` existe en `+0x28` (`ITabCore.h:131`), seule `Item` ne le lit pas. Ce
qui a debloque la mesure est la lecture de `+0x1A4` dans `GetTable`.

### Reste

- posture des soldats crees
- traits de balles ennemies invisibles chez les amis du joueur
- bug de deplacement du joueur : trois verifications toujours sans reponse
- volumes de collision apres reparation de carrosserie

## V140 - La fiche mesuree a +0x1A4, et la garde qui etait deja posee

Livree le 4 septembre 2026 a 20:15:59 (heure locale). Base : V139.
Sauvegarde : `_GOLD_V140/`. Paquet : `release/V140_SOLDIERS/`.

### 1. `itabler2.dll` a donne la disposition de la fiche

Le dossier `source`, que le joueur a demande d'examiner, contient la
bibliotheque des fiches. `C_table::Item` desassemblee en entier :

```
8B 48 20   descripteurs a +0x20
8B 50 24   donnees a +0x24
8B 78 0C   nombre d'entrees a +0x0C
8B 04 F1   descripteur, pas de 8 octets
03 C2      donnees + decalage
```

Trois offsets sur quatre etaient justes. **Le quatrieme n'existe pas** : le
moteur ne lit aucune taille de donnees. `ResolveActorTableInteger` exigeait
pourtant un champ de taille en `+0x28`, repris de la table des armes, et
rejetait tout candidat sur ce critere. Cause unique des quatre mesures ratees.

### 2. La fiche est a +0x1A4

```
Table d'acteur: LUE DANS `GetTable` (vtable rang +0xB4, fonction 004257D0)
-> la table est a +0x1A4. Reference 2002 : +0x190.

Nom des soldats: numero de visage pose chez 6 soldat(s) sur 6
(4 numero(s) repris de vos soldats).
```

### 3. La garde du bandeau etait deja posee - la mortalite etait defaite

Le journal montrait les cinq fonctions « INTROUVABLE (0 candidates) », donc des
soldats crees rendus invulnerables. La mesure de `menu_id` trouvait pourtant
`AddPlayerMenu` trois millisecondes avant, dans la meme zone.

Lecture de la memoire vive du jeu :

```
SetDeathFace    e9 db e8 f3 04 | 8b 7c 24 1c 8b f1 8b 44 be 08
SetHealth       e9 9b e6 f3 04 | 90 90 53 56 8b 74 81 08
AddPlayerMenu   51 57 8b f9 ...                        (intacte)
```

La garde etait deja installee par une execution precedente du trainer, sur un
jeu non relance. La recherche porte desormais sur la **queue** du prologue,
qu'un detour ne recouvre jamais, puis remonte de `stolen` octets. Un `E9` en
tete signifie « deja gardee » et la fonction n'est pas redetournee.

Verifie par simulation sur une image de la memoire vive : une seule candidate
pour chacune des cinq.

### 4. `SetPlayerFace` trouvee apres onze versions

```
2002    83 ec 10    56 57  8b 7c 24 1c  8b f1  8b 4c be 08  81 c1 fc ...
Deluxe  83 ec 10 55 56 57  8b 7c 24 20  8b f1  8b 4c be 08  81 c1 fc ...
```

Deluxe empile un registre de plus. Sixieme echec de la transposition au
passage : `SetHealth` et `SetDeathFace` sont a exactement `0x99C0` de leur
adresse de 2002, mais ce meme decalage designe `00461210` pour `SetPlayerFace`,
ou il n'y a que du code sans rapport. L'adresse retenue, `00461290`, vient de
la lecture de `8B 4C BE 08 81 C1 FC 00 00 00`, unique dans l'executable.

### Adresses etablies

| Element | Valeur | Methode |
|---|---|---|
| fiche d'un acteur | `+0x1A4` | lue dans `GetTable` |
| `C_table` nombre / descripteurs / donnees | `+0x0C` / `+0x20` / `+0x24` | lus dans `itabler2.dll` |
| `C_table` taille des donnees | n'existe pas | absente de `Item` |
| `SetHealth` | `00461960` | queue de prologue |
| `SetDeathFace` | `00461820` | queue de prologue |
| `SetPlayerFace` | `00461290` | partie distinctive, lue |
| `SetPrgKeyColor` | `00461CD0` | queue de prologue |
| `GetPrgKeyColor` | `00461D00` | queue de prologue |

### Verification

- Build Release x86 des trois cibles, code de sortie 0.
- Recherche des cinq gardes simulee sur une image de la memoire vive du jeu :
  une candidate unique pour chacune, quatre reconnues « deja gardees »,
  `SetPlayerFace` reconnue intacte.
- SHA-256 `HDFinalAdvancedV140_SOLDIERS.exe`
  `AD5B95EB83BA465B7B3061679F2BADE30CB65F2E275663F233428F48D87593CA`

### Reste

- recopie de fiche : suspendue au releve de l'etendue du bloc, que V140
  effectue en lecture pure au prochain lancement
- posture des soldats crees
- traits de balles ennemies invisibles chez les amis du joueur
- bug de deplacement du joueur : trois verifications toujours sans reponse
- volumes de collision apres reparation de carrosserie

## V138 - L'offset de la table lu dans `GetTable`

Livree le 4 septembre 2026 a 19:52:25 (heure locale). Base : V137.
Sauvegarde : `_GOLD_V138/`. Paquet : `release/V138_SOLDIERS/`.
Compagnons V105 repris tels quels. Protocole LAN inchange (version 6).

### Le constat

```
Table d'acteur: le code de TableUpdate designe +0x000 (0 forme(s)).
Table d'acteur: NON MESURABLE (0 emplacement(s) possible(s)).
```

Quatre tentatives : transposition (+0x190), balayage avec critere « visage non
nul », balayage avec critere « groupe d'ennemi », recherche de la forme de
`TableUpdate`. Toutes ont echoue.

### La reponse etait dans une fonction de six instructions

```
C_human::GetTable(int index)
  8B 44 24 04          mov eax,[esp+4]
  85 C0                test eax,eax
  75 09                jnz
  8B 81 90 01 00 00    mov eax,[ecx+0x190]   ; return tab
```

Le deplacement de la derniere instruction est l'emplacement cherche. La lecture
se fait sur le binaire du joueur :

1. lire la vtable de son soldat;
2. pour chaque rang de +0xB0 a +0xC0 - ou `GetTable` se trouve forcement
   d'apres l'enumeration ancree sur `SetFrame = +0x88` -, lire les quatorze
   premiers octets de la fonction;
3. exiger la forme `8B 44 24 04 85 C0 75 ?? 8B 81 <deplacement>`;
4. retenir le deplacement, borne a 0x1000.

La forme est trop particuliere pour se produire par hasard, et la fenetre de
rangs est etroite.

### La lecon, notee

C'est la cinquieme fois que lire le code remplace avantageusement une
deduction :

| Element | Ce qui a marche |
|---|---|
| `SetFrame` | reconnaissance du prologue |
| `menu_id` | lecture du `mov [reg+disp],eax` apres `call AddPlayerMenu` |
| base d'inventaire | lecture du `mov ecx,[ebp+8]` dans `AddItem` |
| groupe d'ennemi | lecture des deux fonctions d'hostilite |
| emplacement de la table | lecture du `mov eax,[ecx+disp]` dans `GetTable` |

Et cinq fois qu'une deduction ou une transposition a echoue.

### Verification

- Build Release x86 des trois cibles, zero avertissement, code de sortie 0.
- SHA-256 `HDFinalAdvancedV138_SOLDIERS.exe`
  `F62218B8919196029C94DE93D2FA8CFDBE6617D39E7C7428EB43650C6A490D01`
