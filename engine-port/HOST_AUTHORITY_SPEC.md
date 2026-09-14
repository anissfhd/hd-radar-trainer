# H&D Deluxe — autorité réseau de l'hôte

> ## Etat d'implementation -- 2 septembre 2026
>
> Cette specification est la cible, pas une declaration que le moteur est deja
> pret. Le premier passage source (IA hote, `NM_AUTH_INPUT`, validation hote et
> rejet des resultats `NM_GAME` emis par le client) compile. Il ne couvre pas
> encore tous les domaines de la table ci-dessous, notamment les snapshots,
> ACK/reprise, les menus/inventaires/vehicules et les commandes trainer
> host-only. Le binaire compile n'est pas encore package de maniere executable.
> Voir `BUILD_PORT_STATUS.md` avant toute installation ou test.
>
> **Complement du 2 septembre 2026 (audit).** Le protocole cible decrit
> plus bas compte 8 messages ; **1 seul existe** dans le code. La section
> « Verification de conformite specification / code » en fin de document
> mesure l'ecart reel et corrige le canal annonce pour les codes
> `NM_AUTH_*`. Protection totale et F12 y sont explicitement bloques.

## Cible finale au 2 septembre 2026 (non encore entierement implementee)

Dans le nouveau mode réseau, l'hôte est l'unique source de vérité. Le client
ne décide ni du résultat d'une action ni de l'état final d'un acteur. Il envoie
uniquement ses intentions de jeu (touches, visée, tir, changement d'arme,
interaction). L'hôte les valide, simule la mission, puis réplique l'état
officiel. Les deux PC doivent utiliser exactement le même binaire moteur et le
même protocole `HOST_AUTHORITY_VERSION` ; les versions mélangées seront
refusées avant le début de mission.

Cette règle concerne **tous** les systèmes de partie, pas seulement la vie :

| Domaine | Client | Hôte |
| --- | --- | --- |
| Déplacement, position, collision, animation | envoie l'intention | valide et simule |
| Tir, projectile, impact, dégâts | demande le tir | calcule et publie le résultat |
| Vie, mort, chute, explosion, retour à la vie | affiche l'état reçu | seul décide |
| Soldats, IA, ennemis, objectifs et scripts | affiche l'état reçu | simule et décide |
| Inventaire, armes, véhicules | demande une action | valide et modifie |
| Fonctions du trainer qui changent la partie | demande si autorisée | applique ou refuse |
| ESP, radar, fenêtre et diagnostics | local, lecture seule | sans effet sur la mission |

## Constats vérifiés dans les sources

Le réseau historique est distribué, ce qui explique les désynchronisations :

* `GameMission.cpp` répartit les ennemis entre les participants
  (`network_actor`) ; chacun peut donc simuler une partie de l'IA.
* `C_game_mission::NetIn` transmet directement les paquets à l'acteur visé.
* `C_human::NetCodeIn` applique immédiatement les paquets de position,
  impact, mort et inventaire reçus.
* Les messages de création de tir, roquette, explosion, grenade, mine et
  ressource utilisent l'expéditeur du paquet comme propriétaire.

Ce comportement pair-à-pair doit être remplacé pour le nouveau mode. Les
anciens helpers et hooks externes ne feront pas partie de ce mode.

### Compatibilité de compilation constatée

Le projet source est un projet Visual C++ 6 (`Hidden And Dangerous.dsw/.dsp`).
Le binaire `hde.exe` livré avec cet arbre a le SHA-256
`1C6712221236402F3D1D34F5D3982B62322490680B5E01959E4626B04C2104AD`, tandis que
les deux installations testées utilisent
`5D5EED6174658B8FACFBC1251146B109BBB12AD9A119168AA3489F80979D62D0`.

On ne doit donc jamais copier un exécutable compilé sans une phase de
compatibilité complète. La livraison finale sera un nouveau paquet moteur,
construit depuis une révision identifiée, et **le même paquet** sera installé
sur l'hôte et sur le client. Au lancement, le mode autoritaire vérifiera cette
identité avant d'accepter une partie.

## Inventaire du trainer et destination correcte

| Fonction actuelle | Destination dans le mode hôte |
| --- | --- |
| Protection réseau totale | règle de dégâts/mort évaluée uniquement par l'hôte |
| Retour à la vie F12 / réparation F10 | commande hôte ; l'hôte réanime puis réplique |
| Invisibilité aux ennemis | règle IA hôte, jamais filtre local client |
| Noclip, super-run, téléportation | commande hôte ; position/collision officielles publiées par l'hôte |
| Masquage de position réseau | supprimé du gameplay autoritaire ; ne peut pas falsifier l'état officiel |
| Précision, cadence, munitions, aimbot, Bullet Track | modificateurs de tir évalués côté hôte |
| Balles à travers murs | règle de collision/tir hôte |
| Vitesse et invulnérabilité véhicule | règles véhicule hôte |
| Don d'inventaire / Fullhands | commande hôte et inventaire hôte |
| ESP, radar, overlay, journal | affichage/diagnostic local uniquement |

## Protocole à implémenter

Le protocole sera ajouté au canal `NM_GAME` déjà présent dans
`H&D.h`/`GameMission.cpp`. Les codes historiques 1..0x12 sont réservés ; les
nouveaux codes seront nommés et versionnés, sans réutiliser silencieusement un
code existant.

1. **Négociation** : l'hôte annonce la version, le rôle et le hash du binaire.
   Un client incompatible reste au menu.
2. **Intentions client → hôte** : numéro de séquence, soldat contrôlé,
   commandes de déplacement, visée, tir et interaction. Aucune position,
   dégât, mort, inventaire ou état d'IA fourni par le client n'est accepté.
3. **Simulation hôte** : l'hôte est propriétaire de tous les `network_actor`.
   Il simule joueurs, IA, projectiles, véhicules et scripts.
4. **Instantané hôte → client** : numéro de tick et état canonique des
   acteurs concernés : identité, position, orientation, animation, vie,
   inventaire, véhicule, état de mort/réanimation et événements fiables.
5. **Accusé de réception / reprise** : le client accuse le dernier tick ;
   l'hôte renvoie un instantané complet après perte ou arrivée tardive.
6. **Contrôle hôte** : toute option trainer est une commande hôte explicitement
   journalisée (`acceptée`, `refusée`, raison). Le client n'a aucune case qui
   modifie directement la mémoire ou la simulation.

### Trame précise proposée (version 1)

Les codes seront ajoutés après `NM_CHAT` dans
`C_game_mission::E_NETWORK_MESSAGE_GAME`, avec une plage réservée explicite :

| Code | Sens | Émetteur admis | Destinataire | Données minimales |
| --- | --- | --- | --- | --- |
| `NM_AUTH_HELLO` | vérification de version | hôte ou client | pair | version, hash, rôle |
| `NM_AUTH_INPUT` | intention de contrôle | client | hôte | séquence, soldat, touches, visée, actions |
| `NM_AUTH_COMMAND` | demande trainer | client ou hôte UI | hôte | séquence, commande, paramètres |
| `NM_AUTH_COMMAND_RESULT` | décision | hôte | client | séquence, acceptée/refusée, raison |
| `NM_AUTH_SNAPSHOT` | état canonique | hôte | client | tick, acteurs, position, vie, animation |
| `NM_AUTH_EVENT` | événement fiable | hôte | client | tick, tir, impact, mort, inventaire, objectif |
| `NM_AUTH_ACK` | dernier tick reçu | client | hôte | tick/après événement |
| `NM_AUTH_RESYNC` | demande d'instantané complet | client | hôte | dernier tick connu |

Les paquets `NM_AUTH_INPUT` et `NM_AUTH_COMMAND` arrivant à un client seront
ignorés. Les paquets `NM_AUTH_SNAPSHOT`, `NM_AUTH_EVENT` et
`NM_AUTH_COMMAND_RESULT` arrivant à l'hôte seront ignorés. Chaque paquet
incorrect (version, rôle, séquence ou acteur) sera journalisé et rejeté sans
modifier la scène.

Le client n'envoie jamais : coordonnées, résistance, mort, inventaire, dégâts,
état d'ennemi ou état de véhicule. Même si un paquet de ce type est fabriqué,
l'hôte le rejette car ces champs n'existent pas dans la trame d'intention.

## Travaux source, dans l'ordre

1. Ajouter les structures versionnées et le contrôle de rôle dans les sources
   réseau, avec journal commun hôte/client.
2. Modifier l'initialisation de mission : tous les ennemis et toute simulation
   sont affectés à l'hôte, jamais répartis entre clients.
3. Ajouter la file d'intentions et faire appliquer les entrées du client par
   l'hôte au soldat client.
4. Ajouter les instantanés hôte et faire du client un consommateur d'état pour
   les acteurs non locaux.
5. Migrer, une à une, les fonctions qui modifient le gameplay vers des
   commandes hôte. Les anciennes écritures directes du trainer sont coupées en
   réseau autoritaire.
6. Ajouter tests : protection activée/désactivée, mort, F12, projectile,
   inventaire, téléportation, véhicule, perte réseau et reconnexion.
7. Construire un paquet identique pour hôte et client, puis vérifier son hash
   avant lancement.

## Etat detaille de la migration -- 2 septembre 2026

| Element | Etat au 2 septembre 2026 | Condition avant validation |
| --- | --- | --- |
| IA ennemie hote | Premier code implemente | Partie LAN executable a deux PC |
| Entrees client -> hote | Premier code implemente avec sequence/PID/delai | Deplacement, tir et arret apres perte reseau verifies |
| Rejet des resultats client | Premier filtre `NM_GAME` cote hote | Verifier qu'aucun flux historique indispensable n'est casse |
| Snapshots, ACK, resynchronisation | A faire | Client rendu uniquement depuis l'etat officiel |
| Protection totale | A faire dans le moteur, host-only | Activation et decochage identiques sur les deux ecrans |
| Retour a la vie F12 | A faire dans le moteur, commande hote | Mort et reapparition identiques sur les deux ecrans |
| Toutes les fonctions trainer gameplay | A migrer une par une | Commande acceptee/refusee par l'hote, aucune ecriture client |
| Paquet moteur deployable | Bloque par le packaging | Lancement local propre avant test LAN |

## Garde-fous de test

Un test n'est validé que si les journaux des deux PC ont le même numéro de
tick, le même identifiant d'acteur et le même résultat officiel. En cas
d'écart, le client demande un instantané complet ; il ne corrige jamais la
mission par un hook local. Ainsi, une case décochée ne peut pas laisser une
protection cachée : l'hôte publie explicitement la règle `off` et l'état de
vie suivant devient la seule référence.

## Verification de conformite specification / code -- audit du 2 septembre 2026

Cette section confronte la specification ci-dessus au code reellement present
dans `source/hde/_src`. Elle ne remplace pas la cible : elle mesure l'ecart. Les
tableaux precedents restent la cible a atteindre.

### R11 -- la specification et le code divergent deja sur le canal

La specification annonce plus haut : « Le protocole sera ajoute au canal
`NM_GAME` deja present dans `H&D.h`/`GameMission.cpp` ». **L'implementation a
fait l'inverse** : `NM_AUTH_INPUT = 0x5700` (`Net.h:215`) est un code de
**premier niveau**, au meme rang que `NM_GAME = 0x5600`, et non un sous-code du
chunk `NM_GAME`.

Le choix du code est **meilleur** que la specification : il echappe au filtre
d'hote decrit en R1, qui jette tout le chunk `NM_GAME` venant d'un client. Mais
la specification doit etre corrigee, sinon les sept messages restants seront
concus au mauvais endroit.

Decision a acter : les codes `NM_AUTH_*` sont des codes de premier niveau de
`E_NETWORK_MESSAGE`, alloues a partir de `0x5700`, et non des sous-codes de
`C_game_mission::E_NETWORK_MESSAGE_GAME`.

### Etat reel du protocole : 1 message sur 8

Verifie par recherche sur `Net.h`, `Net.cpp` et `GameMission.cpp`.

| Code de la specification | Present dans le code ? | Emplacement |
| --- | --- | --- |
| `NM_AUTH_HELLO` | Non | -- |
| `NM_AUTH_INPUT` | **Oui** | `Net.h:215`, `Net.cpp:227-330`, `GameMission.cpp:2702-2725` |
| `NM_AUTH_COMMAND` | Non | -- |
| `NM_AUTH_COMMAND_RESULT` | Non | -- |
| `NM_AUTH_SNAPSHOT` | Non | -- |
| `NM_AUTH_EVENT` | Non | -- |
| `NM_AUTH_ACK` | Non | -- |
| `NM_AUTH_RESYNC` | Non | -- |

Consequence directe : **aucun instantane, aucun accuse de reception, aucune
resynchronisation, aucun handshake version/hash, aucune commande trainer
host-only n'existe aujourd'hui.** La regle « les deux PC doivent utiliser
exactement le meme binaire moteur et le meme protocole `HOST_AUTHORITY_VERSION`,
les versions melangees seront refusees avant le debut de mission » n'est portee
par aucun code : deux binaires differents peuvent encore se connecter.

### Etat reel par domaine de la table d'autorite

Confrontation de la table « Cible finale » avec le code du 2 septembre 2026.

| Domaine | Cible | Etat reel verifie |
| --- | --- | --- |
| Deplacement, position, collision, animation | hote valide et simule | Partiel. L'hote rejoue le `Tick` natif du soldat client (`Actors.cpp:13771-13793`), mais le client garde des chemins de simulation locale -- voir R2 et R3 |
| Tir, projectile, impact, degats | hote calcule et publie | Partiel. Passe par le `Tick` natif cote hote, mais le client emet toujours ses propres resultats (R5) ; seule la reception hote les rejette |
| Vie, mort, chute, explosion, retour a la vie | hote seul decide | **Non couvert.** Aucune regle hote, aucun instantane de vie. Le client applique encore `NetCodeIn` localement |
| Soldats, IA, ennemis, objectifs, scripts | hote simule | IA ennemie attribuee a l'hote (`GameMission.cpp:1302-1310`), jamais executee. Objectifs et scripts non couverts |
| Inventaire, armes, vehicules | hote valide et modifie | **Non couvert.** Aucun code |
| Fonctions du trainer qui changent la partie | hote applique ou refuse | **Non couvert.** Aucune ligne dans le moteur |
| ESP, radar, fenetre, diagnostics | local, lecture seule | Hors moteur, inchange |

### Le client n'est pas encore un simple afficheur

C'est l'ecart le plus important entre cette specification et le code.

- **R2** -- `Actors.cpp:13766-13770` : la demotion du joueur client en acteur
  distant est **temporaire**. `network_actor` passe a `GetHostPID()` puis est
  restaure a `0` immediatement apres le `Tick`. Tout le reste du code client --
  rendu, `NetCodeIn`, `C_human::Hit`, inventaire, voix, programme -- voit
  toujours le joueur comme local.
- **R3** -- la branche client n'est active que si `mode == PLRMODE_ACTIVE`, alors
  que le constructeur initialise `mode(PLRMODE_PROGRAM)` (`Actors.cpp:13024`).
  En `PLRMODE_PROGRAM`, `PLRMODE_DYING` et `PLRMODE_DEAD`, le client reprend sa
  simulation locale et cesse d'envoyer ses intentions. C'est exactement la
  fenetre ou naissent les divergences « vivant chez l'hote, mort chez l'ami ».
- **R5** -- les sites d'emission `NM_HUMAN_POS`, `NM_HUMAN_HIT`, `NM_HUMAN_DIE`
  (`Actors.cpp:5418`, `5592`, `11019`) sont gardes par un parametre `net_send`
  qui signifie « faut-il repliquer cette action », pas « suis-je proprietaire ».
  L'autorite est donc obtenue par **filtrage en reception**, pas par
  construction.
- **R4** -- le filtre d'hote ne s'applique que si `IsHost()`. Avec 3 ou 4 joueurs
  (`NET_MAX_PLAYERS 4`), un client applique toujours directement les `NM_GAME`
  d'un autre client. La regle « le client ne decide jamais » est fausse au-dela
  de deux machines.

Tant que R2, R3, R4 et R5 sont ouverts, la phrase « le client affiche l'etat
recu » de la table ci-dessus decrit une intention, pas le comportement du
binaire.

### Defauts de robustesse du seul message implemente

- **R6** -- `Net.cpp:293-296` : le controle de sequence de `AuthorityAcceptInput`
  n'est applique que si `old.sender_pid == input.sender_pid`. Un PID different
  ecrase l'entree sans controle. La securite repose entierement sur la
  verification faite chez l'appelant (`GameMission.cpp:2712`).
- **R7** -- `HD_AUTHORITY_INPUT_SLOTS = 64` alors que `GKEY_LAST == 65`
  (`H&D.h:1863`). Seul `GKEY_DEBUG` est tronque aujourd'hui, mais la marge est
  nulle : ajouter une touche de jeu fera disparaitre une action silencieusement.
- **R8** -- comportement au timeout non specifie. Le paquet de test promet « le
  joueur cesse de recevoir les anciennes touches ». Verifie dans le code : apres
  `AUTHORITY_INPUT_STALE_MS = 500`, `AuthorityGetInput` renvoie `false`, la
  branche hote ne s'execute pas, et l'acteur retombe en acteur distant sans mise
  a jour. Arret net, glissade ou animation figee : le comportement reel n'est ni
  defini ni observe. **A specifier avant l'etape de test LAN.**

### Regle de blocage sur Protection totale et Retour a la vie F12

Ajoutee le 2 septembre 2026. Ces deux fonctions n'existent pas dans le moteur :
recherche sur les fichiers reseau et mission, aucune occurrence de protection,
garde de mort, garde de degats, reanimation ou commande hote.

Elles **ne doivent etre ni testees, ni annoncees, ni presentees comme
fonctionnelles** tant que les deux conditions suivantes ne sont pas remplies :

1. `NM_AUTH_SNAPSHOT`, `NM_AUTH_EVENT`, `NM_AUTH_ACK` et `NM_AUTH_RESYNC`
   existent, et le client rend l'etat des acteurs non locaux **uniquement**
   depuis l'etat officiel recu ;
2. `NM_AUTH_COMMAND` et `NM_AUTH_COMMAND_RESULT` existent, et chaque commande
   est journalisee cote hote et cote client avec `acceptee` / `refusee` et sa
   raison.

Les migrer avant reproduirait exactement l'echec de V92 : une commande qui
arrive, un journal qui dit « actif », et deux ecrans qui ne montrent pas la meme
chose. La table « Inventaire du trainer et destination correcte » ci-dessus
reste la cible, mais aucune de ses lignes n'est commencee.

### Etat detaille de la migration -- revision du 2 septembre 2026 (audit)

Ce tableau remplace celui de la section precedente, qui reste consultable comme
etat anterieur.

| Element | Etat verifie au 2 septembre 2026 | Constat lie | Condition avant validation |
| --- | --- | --- | --- |
| IA ennemie hote | Code present, jamais execute | -- | Partie LAN executable a deux PC |
| Entrees client -> hote | Code present, jamais execute | R6, R7, R8 | Deplacement, tir et arret apres perte reseau verifies sur les deux ecrans |
| Rejet des resultats client | Code present, **filtre trop large** | **R1**, R4, R5 | Restreindre aux sous-codes de resultat avant tout test LAN |
| Client simple afficheur | Non atteint | R2, R3 | Demotion permanente, tous modes |
| Snapshots, ACK, resynchronisation | Aucun code | E2 | Client rendu uniquement depuis l'etat officiel |
| Handshake version/hash | Aucun code | E3 | Refus effectif d'un binaire different avant mission |
| Commandes trainer host-only | Aucun code | E4 | Journal accepte/refuse avec raison, des deux cotes |
| Protection totale | Aucun code | Regle de blocage | Activation **et** decochage identiques sur les deux ecrans |
| Retour a la vie F12 | Aucun code | Regle de blocage | Mort et reapparition identiques sur les deux ecrans |
| Menus, inventaire, vehicules, objectifs | Aucun code | E5 | A specifier |
| Paquet moteur deployable | Bloque | C1, C2, C3, R10, R12 | Lancement local propre, sans mod puis avec mod |
