# Carte d'implementation : hote autoritaire

> ## Etat reel -- 2 septembre 2026
>
> Cette carte decrit le travail moteur restant. Une premiere version des trois
> premiers axes existe dans la copie source et compile, mais elle n'est pas
> encore un paquet jouable. Les anciens helpers V88/V92 ne sont pas une
> alternative : leurs traces prouvent seulement que les commandes UDP arrivent,
> pas que le client voit l'etat canonique de l'hote.
>
> **Complement du 2 septembre 2026 (audit).** Les emplacements source ont
> ete verifies et completes en fin de document. Un blocage a corriger avant
> tout test LAN y est ajoute : le filtre qui rejette **tout** `NM_GAME`
> venant du client est trop large et risque de bloquer synchronisation,
> briefing, chat, carte, menus, vehicule et debut de mission.

Ce document est la carte de travail de la migration du moteur. Il ne decrit
pas un trainer externe et ne modifie pas le jeu installe.

## Regle non negociable

L'hote est la seule simulation de jeu. Le client transmet uniquement des
intentions d'entree et affiche les etats, evenements et validations emis par
l'hote. Un paquet client ne peut jamais directement fixer une position, une
vie, un inventaire, des munitions ou l'etat d'un objectif.

## Points source a remplacer

| Domaine | Ancien comportement | Point source | Comportement cible |
| --- | --- | --- | --- |
| Repartition IA | Les ennemis sont distribues entre PC | `GameMission.cpp:1297` | Tous les ennemis ont `network_actor=0` sur l'hote et l'hote est proprietaire sur le client. |
| Joueurs | Le PC qui obtient le controle simule le soldat | `Actors.cpp:13041`, `Actors.cpp:13694`, `Actors.cpp:13813` | Le client actif envoie ses touches; l'hote simule son soldat avec un controleur d'entree distant. |
| Reception | Les paquets sont appliques directement a l'acteur | `GameMission.cpp:2681`, `Actors.cpp:7989` | Les paquets `AUTH_INPUT` sont acceptes seulement par l'hote et passes au proprietaire autorise. |
| Mouvement, tirs, degats | Chaque proprietaire emet ses propres resultats | `Actors.cpp:5418`, `Actors.cpp:5592`, `Actors.cpp:11019` | Seul l'hote emet positions, tirs, degats, morts et inventaire. |
| Revivre / changement de soldat | Actions locales et synchronisation historique | `Actors.cpp:13694` et gestion `net_switch_sync` | Commande client -> validation hote -> evenement de resultat hote. |

## Suivi de mise en oeuvre -- 2 septembre 2026

| Etape | Etat | Note |
| --- | --- | --- |
| Compilation du port | Compile | `HDE_Authority.exe` est produit, mais le paquet ne demarre pas encore. |
| Ennemis host-owned | Premier code ajoute | A verifier en LAN executable. |
| `AUTH_INPUT` client -> hote | Premier code ajoute | PID, acteur, sequence et timeout sont verifies. |
| Resultats client historiques bloques | Premier code ajoute | L'hote rejette `NM_GAME` emis par un client. |
| Snapshots/ACK/resync | A faire | Necessaire avant toute promesse de synchronisation exacte. |
| Trainer host-only complet | A faire | Protection, F12 et toutes les options gameplay doivent passer ici. |
| Paquet de test complet | A faire | Runtime MSVC et DLL moteur exactes a construire/valider. |

## Flux de paquets cible

```text
Client: entree locale -> AUTH_INPUT(actor, sequence, touches, souris)
                                  |
                                  v
Hote: validation proprietaire -> simulation unique -> paquets acteur existants
                                  |
                                  v
Tous les clients: NetIn -> rendu de l'etat hote
```

Les paquets de resultat conservent le protocole acteur existant chaque fois
que possible (`NM_HUMAN_POS`, `NM_HUMAN_HIT`, `NM_HUMAN_DIE`,
`NM_HUMAN_INVENTORY`). Cela limite le changement aux entrees et a
l'autorite, au lieu de recreer une seconde synchronisation incompatible.

## Validation appliquee par l'hote

1. PID expediteur present dans la session.
2. Le soldat vise est attribue a ce PID par l'hote.
3. La sequence est strictement plus recente.
4. Les valeurs d'entree sont bornees et les commandes ponctuelles ne sont
   executees qu'une fois.
5. Les changements de soldat, objets, vehicules, inventaire et objectifs sont
   revalides dans la simulation hote avant emission du resultat.

## Ordre de mise en oeuvre

1. Compiler le moteur source complet dans le dossier de construction separe.
2. Ajouter `AUTH_INPUT` et le controleur distant sans changer les regles.
3. Rendre les ennemis et evenements physiques proprietes de l'hote.
4. Rendre les joueurs distants simules par l'hote avec les entrees recues.
5. Bloquer les anciens transferts de simulation client et les remplacer par
   des requetes validees par l'hote.
6. Ajouter trace hote/client avec PID, acteur, sequence, acceptation et etat
   final; tester deux executables issus du meme build.

## Critere de fin

Un client debranche ne doit plus pouvoir changer l'IA, les ennemis, les
objectifs, la vie, les degats, les objets ou les armes. Avec une latence
normale, le client voit les memes morts, retours a la vie, positions et
resultats que l'hote, car ces resultats ne sont produits qu'une fois par
l'hote.

## Releve de code verifie -- audit du 2 septembre 2026

Les emplacements cites plus haut ont ete confirmes et completes par lecture du
code. Ce tableau donne les references exactes de ce qui **existe reellement**,
pour que la suite de la migration parte d'un etat verifie et non d'un resume.

| Element | Emplacement verifie | Effet reel |
| --- | --- | --- |
| Interrupteur de mode | `Net.h:19` `#define HD_HOST_AUTHORITY 1` | L'ancien round-robin reste present dans la branche `#else` |
| Trame d'intention | `Net.h:24-40` `S_authority_input` | 64 slots de touches, 3 axes souris, `key` / `modify_keys` / `mouse_buttons`, 3 valeurs de config. **Aucun champ position, vie, degat ou inventaire** |
| Message reseau | `Net.h:215` `NM_AUTH_INPUT = 0x5700` | Code de premier niveau, hors du chunk `NM_GAME` |
| Envoi client | `Net.cpp:227-256` `AuthoritySendInput` | Refuse d'emettre si `IsHost()` ; envoi direct au PID hote ; `INSEND_CANCELOLDER` |
| Decodage hote | `Net.cpp:265-285` `AuthorityReadInput` | Controle `AUTHORITY_INPUT_PROTOCOL == 1` |
| Acceptation | `Net.cpp:290-310` `AuthorityAcceptInput` | Anti-rejeu par sequence ; conserve l'etat « touche deja consommee » |
| Consommation | `Net.cpp:313-325` `AuthorityGetInput` | Verifie le proprietaire et la peremption `AUTHORITY_INPUT_STALE_MS = 500` |
| Deconnexion | `Net.cpp:328`, appelee depuis `Net.cpp:349` | `AuthorityForgetInput(pid)` sur `INET_SYSMSG_PLAYER_DESTROYED` |
| Repartition IA | `GameMission.cpp:1302-1310` | `network_actor = 0` sur l'hote, `= GetHostPID()` sur le client, pour tous les `ACTOR_ENEMY` |
| Validation de propriete | `GameMission.cpp:2702-2725` | `actor->GetType()==ACTOR_PLAYER && actor->network_actor == input.sender_pid` |
| Rejet des resultats client | `GameMission.cpp:2730-2737` | `SkipChunk()` sur **tout** `NM_GAME` recu par l'hote -- voir R1 |
| Controleur distant | `Actors.cpp:13668-13727` | `C_authority_controller` et 4 slots runtime persistants |
| Simulation hote / demotion client | `Actors.cpp:13743-13795` | Client : envoie l'intention puis se traite en acteur distant. Hote : rejoue le `Tick` natif du soldat client |

Ce code n'a **jamais ete execute**, ni en solo ni en LAN. Il compile : cela ne
valide ni son comportement ni son effet reseau.

## R1 -- le filtre `NM_GAME` actuel est trop large. A corriger avant tout test LAN

`GameMission.cpp:2730-2737` jette **tout** `NM_GAME` venant d'un client. Or
`NM_GAME` n'est pas seulement le canal des resultats de simulation. Il
transporte aussi, comme sous-codes (`GameMission.cpp:2840-2866`) :

- `NM_SYNC` -- le protocole d'accord `C_net_sync` ;
- `NM_MAP` -- le gestionnaire de carte ;
- `NM_CHAT` -- le chat en mission ;
- `NM_HUMAN_SWITCH_SYNC` -- l'arbitrage de changement de soldat.

Et `C_net_sync` ne passe a `SS_RESOLVED` que lorsque **tous** les PID ont
repondu (`NetSync.cpp`, vidage de `pid_to_answer`). L'hote jetant la reponse du
client, les evenements suivants ne se resolvent jamais :

| Evenement | Emis depuis | Consequence attendue |
| --- | --- | --- |
| `NS_GAME_BEGIN` | `GameMission.cpp:1193` | **La mission ne demarre pas** |
| `NS_TO_BRIEFING`, `NS_TO_MAP` | `GameMission.cpp:510`, `596` | Blocage au briefing et a la carte |
| `NS_ENTER`, `NS_LEAVE` | `Briefing.cpp:234`, `351`, `CampAnim.cpp:55`, `89` | Blocage aux transitions |
| `NS_DONE_SUCCESS`, `NS_DONE_FAIL`, `NS_GAME_EXIT` | `GameMission.cpp:2681-2690` | Fin de mission impossible |
| Evenements de menu | `Menu.cpp:742` | Menus figes |
| Ramassage de ressource, mine | `Resource.cpp:157`, `Mine.cpp:197` | Actions bloquees |
| Sieges de vehicule | `Vehicle.cpp:2470`, `3235` | Entree et sortie de vehicule bloquees |
| Changement de soldat | `Actors.cpp:12553` | Arbitrage bloque |

**Comportement cible** : ne rejeter que les sous-codes de **resultat** emis par
un client -- les codes acteur (`>= 0x100`, dont `NM_HUMAN_POS`, `NM_HUMAN_HIT`,
`NM_HUMAN_DIE`, `NM_HUMAN_INVENTORY`) et les creations physiques
(`NM_GAME_CREATE_SHOOT`, `NM_GAME_CREATE_ROCKET`, `NM_GAME_CREATE_EXPLOSION`,
`NM_GAME_CREATE_TIME_BOMB`). Laisser passer `NM_SYNC`, `NM_MAP`, `NM_CHAT` et
`NM_HUMAN_SWITCH_SYNC`, qui sont des flux de coordination indispensables et non
des resultats de simulation.

Chaque sous-code rejete doit etre journalise avec son PID emetteur, pour que le
premier test LAN distingue un rejet voulu d'un blocage accidentel.

## Constats a traiter pendant la migration -- 2 septembre 2026

| Reference | Constat | Point source | A traiter a l'etape |
| --- | --- | --- | --- |
| R2 | La demotion du joueur client est **temporaire** : `network_actor` est restaure a `0` juste apres le `Tick`. Le reste du code client voit toujours le joueur comme local | `Actors.cpp:13766-13770` | Demotion client permanente |
| R3 | La branche client n'est active que si `mode == PLRMODE_ACTIVE`, alors que le constructeur initialise `PLRMODE_PROGRAM`. En `PROGRAM`, `DYING` et `DEAD`, le client resimule localement et cesse d'envoyer ses intentions | `Actors.cpp:13745`, `13024` | Demotion client permanente |
| R4 | L'autorite n'est pas transitive : le filtre ne s'applique que si `IsHost()`. Avec 3 ou 4 joueurs (`NET_MAX_PLAYERS 4`), un client applique encore les `NM_GAME` d'un autre client | `GameMission.cpp:2730` | Handshake |
| R5 | L'emission client n'est pas coupee : les sites de resultat sont gardes par le parametre `net_send` (« faut-il repliquer »), pas par la propriete. L'autorite est obtenue par filtrage en reception | `Actors.cpp:5418`, `5592`, `11019` | Demotion client permanente |
| R6 | `AuthorityAcceptInput` n'applique le controle de sequence que si `old.sender_pid == input.sender_pid` ; un PID different ecrase l'entree sans controle. La securite depend entierement de l'appelant | `Net.cpp:293-296` | Avec le test LAN minimal |
| R7 | `HD_AUTHORITY_INPUT_SLOTS = 64` pour `GKEY_LAST == 65`. Seul `GKEY_DEBUG` est tronque aujourd'hui ; marge nulle pour toute nouvelle touche | `Net.h:29`, `H&D.h:1863` | Avec le test LAN minimal |
| R8 | Comportement au timeout de 500 ms ni defini ni observe : l'acteur retombe en acteur distant sans mise a jour. Arret net, glissade ou animation figee ? | `Net.cpp:319`, `Actors.cpp:13772` | A specifier avant le test LAN minimal |

## Ordre de mise en oeuvre -- revision du 2 septembre 2026

Cette liste remplace la section « Ordre de mise en oeuvre » ci-dessus, qui reste
consultable comme etat anterieur. Elle est alignee sur le plan d'execution de
`plan.md`. Aucune etape ne commence avant que la precedente soit constatee,
journalisee et acceptee.

1. **Sauvegarde et versionnement approuves** (R9). `source/`, `release/` et
   `interface/` ne sont pas versionnes. Aucun `git add` et aucun commit sans
   autorisation explicite de l'utilisateur.
2. **Packaging executable** : runtime, noms de modules moteur historiques,
   dossier neuf sans aucune DLL de 2002. Voir `BUILD_PORT_STATUS.md`.
3. **Lancement local sans mod.**
4. **Lancement local avec le mod Ultimate 5.0** (R12).
5. **Correction du filtre `NM_GAME`** (R1), avec journal par sous-code rejete.
6. **Test LAN minimal** a deux PC : la mission demarre, le client se deplace et
   tire, l'hote voit le resultat, coupure du flux client au-dela de 500 ms.
   Journaux compares des deux cotes. Traite R6, R7, R8.
7. **Demotion client permanente** : acteur distant en permanence, tous modes
   inclus `PLRMODE_PROGRAM`, `PLRMODE_DYING`, `PLRMODE_DEAD` et vehicule.
   Traite R2, R3, R5.
8. **Instantanes, accuses de reception, resynchronisation.** Le client ne rend
   plus les acteurs non locaux que depuis l'etat officiel.
9. **Handshake version/hash.** Traite R4.
10. **Commandes trainer host-only** : `NM_AUTH_COMMAND`,
    `NM_AUTH_COMMAND_RESULT`, journal accepte/refuse avec raison des deux cotes.
11. **Protection totale**, regle evaluee uniquement par l'hote.
12. **Retour a la vie F12**, commande validee et executee par l'hote.

**Regle de blocage.** Protection totale et F12 n'existent pas dans le moteur :
aucune ligne. Ils ne doivent etre ni testes, ni annonces, ni presentes comme
fonctionnels avant que les etapes 8 et 10 soient faites et verifiees. Les migrer
avant reproduirait l'echec de V92 : une commande qui arrive, un journal qui dit
« actif », et deux ecrans qui ne montrent pas la meme chose.
