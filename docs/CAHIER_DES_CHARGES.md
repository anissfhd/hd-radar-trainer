# Cahier des charges V103 — soldats alliés créés, clone de véhicule, ordres par la carte

**Rédigé le 3 septembre 2026 à 22:46:03 (heure locale).**
**Mis à jour le 3 septembre 2026 à 23:59:08 : état d'avancement en fin de document.**
**Mis à jour le 4 septembre 2026 à 01:54:08 : chapitre 12, correction du plantage à la création.**
**Mis à jour le 4 septembre 2026 à 02:08:04 : chapitre 13, saisie du nombre et groupe au choix.**
**Mis à jour le 4 septembre 2026 à 02:28:34 : chapitre 14, placement et initialisation des soldats créés.**
**Mis à jour le 4 septembre 2026 à 02:38:06 : chapitre 15, nombre exact, position juste, fenêtre lisible.**
**Mis à jour le 4 septembre 2026 à 02:50:12 : chapitre 16, plantage sous le feu ennemi, touches 1 2 3 4, suppression des soldats créés.**
**Mis à jour le 4 septembre 2026 à 03:04:31 : chapitre 17, placement groupé et mortalité rétablie.**
**Mis à jour le 4 septembre 2026 à 11:40:07 : chapitre 18, armes en miroir et `menu_id` mesuré par le code.**
**Mis à jour le 4 septembre 2026 à 11:51:02 : chapitre 19, la base d'inventaire déduite au lieu d'être transposée.**
**Mis à jour le 4 septembre 2026 à 11:56:26 : chapitre 20, cheats natifs refusés sur un soldat créé.**
**Mis à jour le 4 septembre 2026 à 12:05:25 : chapitre 21, le miroir d'arme coupé et la pièce qui bloque.**
**Mis à jour le 4 septembre 2026 à 12:14:18 : chapitre 22, santé des soldats créés et vérification du nom.**
**Mis à jour le 4 septembre 2026 à 12:27:50 : chapitre 23, case de bandeau libre et noms durables.**
**Mis à jour le 4 septembre 2026 à 12:36:29 : chapitre 24, la case d'un vrai soldat devient interdite.**
**Mis à jour le 4 septembre 2026 à 12:44:56 : chapitre 25, missions à quatre soldats et fenêtre des armes.**
**Mis à jour le 4 septembre 2026 à 12:52:54 : chapitre 26, lecture complète du journal.**
**Mis à jour le 4 septembre 2026 à 14:16:22 : chapitre 27, garde sur le bandeau et abandon de l'équipement forcé.**
**Mis à jour le 4 septembre 2026 à 14:32:07 : chapitre 28, le « unknown » vient du numéro de visage.**
**Mis à jour le 4 septembre 2026 à 14:43:37 : chapitre 29, transposer au lieu de mesurer.**
**Mis à jour le 4 septembre 2026 à 14:58:06 : chapitre 30, `AddItem` cassé par une fausse déduction.**
**Mis à jour le 4 septembre 2026 à 15:09:36 : chapitre 31, l'arme arrive et la liste est purgée.**
**Mis à jour le 4 septembre 2026 à 15:20:01 : chapitre 32, munitions et main vide.**
**Mis à jour le 4 septembre 2026 à 15:25:23 : chapitre 33, drapeaux exacts et reprise de l'équipement.**
**Mis à jour le 4 septembre 2026 à 17:54:41 : chapitre 34, l'équipement fonctionne, la main manque.**
**Mis à jour le 4 septembre 2026 à 19:24:05 : chapitre 35, le témoin tranche, la sortie d'arme devient volontaire.**
**Mis à jour le 4 septembre 2026 à 19:30:08 : chapitre 36, pourquoi le port de l'arme est impossible.**
**Mis à jour le 4 septembre 2026 à 19:46:19 : chapitre 37, mesure de la table par le groupe d'ennemi.**
**Mis à jour le 4 septembre 2026 à 19:52:25 : chapitre 38, l'offset lu dans `GetTable`.**
**Mis à jour le 4 septembre 2026 à 20:15:59 : chapitre 39, la fiche mesurée à +0x1A4 et la garde qui était déjà posée.**
**Mis à jour le 4 septembre 2026 à 20:36:03 : chapitre 40, l'arme en main par la recopie de fiche et `TableUpdate`.**
**Mis à jour le 5 septembre 2026 à 12:38:22 : chapitre 41, rallier les ennemis — comptage, choix du nombre, armes et ordres.**
**Mis à jour le 5 septembre 2026 à 12:45:10 : chapitre 42, le plus proche d'abord et « venir à moi ».**
**Mis à jour le 5 septembre 2026 à 12:58:16 : chapitre 43, la limite de trente-cinq hommes et le compte faussé.**
**Mis à jour le 5 septembre 2026 à 13:11:09 : chapitre 44, l'ordre par la carte détaché de la téléportation, et le miroir d'arme étendu aux ralliés.**
**Mis à jour le 5 septembre 2026 à 13:28:09 : chapitre 45, l'arrêt du jeu à la distribution d'arme — file unique, petits paquets, et saut des hommes déjà armés.**
**Mis à jour le 5 septembre 2026 à 13:34:42 : chapitre 46, la vraie cause — des pointeurs vers des acteurs morts au combat.**
**Mis à jour le 5 septembre 2026 à 13:44:07 : chapitre 47, deux diagnostics faux — on instrumente au lieu de supposer.**
**Mis à jour le 5 septembre 2026 à 13:57:25 : chapitre 48, la cause réelle — écriture sur du code en cours d'exécution.**
**Mis à jour le 5 septembre 2026 à 14:06:49 : chapitre 49, la sortie d'arme est coupée — bilan de ce qui est éliminé.**
**Mis à jour le 5 septembre 2026 à 14:24:39 : chapitre 50, la cause trouvée — un argument manquant à `SetSelectedInvItem`.**
**Mis à jour le 5 septembre 2026 à 14:41:34 : chapitre 51, l'ordre de déplacement vide la liste au lieu de s'y insérer.**
**Mis à jour le 5 septembre 2026 à 14:54:12 : chapitre 52, main libre — piloter un allié comme on se pilote soi-même.**
**Mis à jour le 5 septembre 2026 à 15:15:40 : chapitre 53, la caméra sur l'homme piloté — et deux pièges lus dans le binaire.**
**Mis à jour le 5 septembre 2026 à 15:32:14 : chapitre 54, le numéro de message prouvé par son appelant, et le switch étendu aux alliés.**
**Mis à jour le 5 septembre 2026 à 15:42:38 : chapitre 55, un camp propre au joueur, et le switch trié comme le jeu.**
**Mis à jour le 5 septembre 2026 à 15:52:05 : chapitre 56, pourquoi ils ne tiraient pas — le déclenchement, pas l'hostilité.**
**Mis à jour le 5 septembre 2026 à 15:58:52 : chapitre 57, les fenêtres se rouvrent sur le dernier choix.**
**Mis à jour le 5 septembre 2026 à 16:10:00 : chapitre 58, l'ordre d'attaque était jeté — il fallait les faire regarder.**
**Mis à jour le 5 septembre 2026 à 16:17:37 : chapitre 59, à cent mètres personne ne voit personne — ils vont au contact.**
**Mis à jour le 5 septembre 2026 à 16:42:12 : chapitre 60, c'étaient mes ordres qui empêchaient leur IA — retour à la forme du moteur.**
**Mis à jour le 5 septembre 2026 à 16:49:33 : chapitre 61, seuls les chasseurs partaient — les réglages d'IA sont dans la fiche.**
**Mis à jour le 5 septembre 2026 à 17:04:53 : chapitre 62, FEU ALL — ils enchaînent les cibles sans empiler les ordres.**
**Mis à jour le 6 septembre 2026 à 01:45:20 : chapitre 63, le journal ne coûte plus d'images.**
Ce document rassemble **tout** ce qui a été demandé et discuté, sans rien omettre,
y compris les demandes antérieures encore ouvertes, les refus motivés et les
découvertes moteur qui rendent le travail possible. Il fait foi pour la V103.

---

## 1. Contexte et matériel

- Trainer externe `HDFinalAdvanced*.exe` + deux compagnons LAN
  `HD_AI_AUTHORITY_HOST_*` / `HD_AI_AUTHORITY_CLIENT_*`.
- Le joueur est **hôte**. Ses amis sont clients. Les compagnons donnent **100 %
  des ennemis à l'hôte**, ce qui rend l'IA calculable depuis une seule machine.
- **Oracle décisif** : le code source du moteur est présent dans
  `source/hde/_src` (Actors.cpp, GameMission.cpp, Vehicle.cpp, GunShoot.cpp…),
  et un binaire de référence `source/hde/bin/HDE.exe` du 13 mai 2002 est livré
  **avec sa table de symboles `HDE.map`**. Les adresses de fonctions n'y sont pas
  celles du jeu installé, mais les **offsets de champs** et la **logique** le
  sont, à un décalage constant près (mesuré à `+0x14` sur trois champs tardifs).

---

## 2. État déjà livré et validé (V95 → V102)

| Version | Contenu | Validé par le joueur |
|---|---|---|
| V95 | « Invisible » = perception seule ; « Protection / Escouade entière » couvre les amis (Hit, Explode) ; application instantanée des cases | oui |
| V96 | Les ennemis redécouvrent les amis dès le début de mission ; W rend invisible | oui |
| V97 | Nettoyage des cheats (F3 et F6 retirés), F4 et F5 natifs, F3 = sortie de véhicule, aimbot 1000 m + case Illimité, la portée du masque pilote l'invisibilité | partiellement |
| V98 | Protocole LAN v5, santé chez les amis, position masquée des amis, F6 liste des véhicules, G réparation | F6 validé |
| V99 | F3 replacement à côté du véhicule, G revu, diagnostics des deux côtés | non testé |
| V100 | **Correction du canal LAN** : diffusion dirigée par interface, plus seulement 255.255.255.255 ; G répare la carrosserie en place | canal validé |
| V101 | La vie étendue couvre **toutes les copies** de l'acteur, pas seulement les locales | oui |
| V102 | Protocole v6 : l'hôte publie la vie de chaque soldat, les clients s'alignent | **santé validée** |

---

## 3. Points encore ouverts, hérités des versions précédentes

### 3.1 Bug de déplacement signalé par le joueur
« Je clique pour partir à droite, je m'arrête, ils restent marcher. »
**Cause inconnue à ce jour.** Trois suspects, par ordre de probabilité :

1. **Vitesse du jeu (touches 9 et 8)** — lues en permanence. Un appui
   involontaire sur 9 multiplie le pas de temps distribué à la simulation ; le
   soldat parcourt beaucoup plus de distance par image et paraît continuer.
   *Vérification : le badge de la carte « Vitesse du jeu » doit afficher 1.0x.*
2. **F7 « Passer la mission »** — seul cheat encore tapé au clavier. Il bloque
   brièvement les entrées ; une touche de déplacement tenue à cet instant n'est
   jamais vue comme relâchée.
3. **Noclip (touche V)** — s'il est armé, un filtre clavier intercepte les
   touches de déplacement.

**Test qui tranche : fermer complètement le trainer et rejouer.** Si le défaut
persiste, il n'est pas de notre fait.

### 3.2 Traits de balles ennemies invisibles chez les amis
Établi par le test du joueur : *les dégâts traversent le réseau, le tir visuel
non*. La réplication est pourtant native et inconditionnelle
(`GunShoot.cpp`, `C_gun_shoot::Evaluate` → `NM_GAME_CREATE_SHOOT` dès que
`net && !network_actor`). C'est la conséquence directe des **100 % d'ennemis
donnés à l'hôte** : nativement chaque machine dessinait localement les tirs de
sa moitié. **Non corrigé** ; correction = instrumenter le message de tir en
émission et en réception. Travail de recherche, non promis.

### 3.3 Barre de vie qui ne se rafraîchit qu'au prochain dégât
`C_human::SetResistance` est la seule routine qui appelle
`game_menu.SetHealth` ; une écriture mémoire ne la traverse pas. La **valeur**
est identique sur toutes les machines depuis la V102, seul l'**affichage** de
chacun attend le premier coup encaissé. Cosmétique.

### 3.4 G : volumes de collision non ré-inscrits
La réparation de carrosserie rejoue la partie visible de
`C_version::SetVersion` (bits `FRMFLAGS_ON` des frames de version) mais pas
`EnumFrames(cbVol, scene, ENUMF_VOLUME)`. La forme de collision peut rester
celle de l'épave.

### 3.5 « G, F6 et F3 ne marchent plus »
Signalé sans détail, jamais décrit. **Information manquante** : pour chacun,
*rien du tout* / *un message dans le panneau* / *un effet mais pas le bon*.
Rappel des changements qui peuvent surprendre :
- **G** n'agit **plus qu'à l'intérieur d'un véhicule** depuis la V100 ;
- **F3** repose le joueur à 3 m devant le véhicule depuis la V99.

---

## 4. Cahier des charges de la V103

### 4.1 Soldats alliés créés — fonction principale

**Déclenchement.** Touche **G lorsque le joueur est à pied**. La touche **G en
véhicule conserve sa fonction actuelle** (réparation de carrosserie). Une seule
touche, deux comportements selon le contexte.

**Fenêtre de création.** Une fenêtre s'affiche **dans le jeu**, comme celle de
F6, et demande **un nombre**. Le joueur le saisit lui-même. **Maximum 50.**

**Création.** Les N soldats apparaissent **près du soldat contrôlé**.

**Apparence.** Le modèle est une **duplication du soldat du joueur**, donc
**son uniforme exact, propre à la mission en cours**, sans qu'aucune tenue ait
à être connue ou choisie.

**Type d'acteur.** `ACTOR_PLAYER` — indispensable pour pouvoir les incarner
(voir 5.2). Leur `menu_id` sera forcé dans la plage valide **0–3** pour que la
jauge de vie n'écrive jamais hors du tableau des portraits ; ils partageront
donc un portrait avec un soldat existant, sans conséquence de jeu.

**Camp.** Alliés. Ils ne sont pas hostiles au joueur ; les Allemands leur sont
hostiles.

### 4.2 Fenêtre de gestion des soldats

Elle liste les soldats créés : **Soldat 1, Soldat 2, … jusqu'au nombre choisi**.

Trois actions doivent y figurer :

1. **Sélectionner un soldat** → le joueur **prend son contrôle total**,
   exactement comme son soldat d'origine : clavier, caméra, visée, carte. Rien
   ne doit différer de la conduite du soldat initial.
2. **Commande par groupe** → le joueur choisit **un nombre de soldats**
   (exemple : 4), puis **un endroit sur la carte** ; **tous ceux du groupe y
   vont**.
3. **Commande « Tous »** → même logique, appliquée à **l'ensemble** des soldats
   créés.

### 4.3 Ordres par la carte

- **La carte ne doit pas être affichée en permanence.** Elle **apparaît sur
  appui d'une touche** et disparaît ensuite, afin de ne rien masquer et de ne
  pas gêner le jeu.
- **Pendant que le joueur pilote l'un des soldats créés** : il ouvre la carte,
  clique un endroit, et **ce soldat seul** s'y rend — comportement natif du
  soldat contrôlé.
- **Pour les soldats non pilotés** : l'ordre passe par
  `AddProgram(0, PRG_MOVE, destination)`.

### 4.4 Tir sans réplique — pour les soldats créés uniquement

Les ennemis prononcent une réplique avant d'ouvrir le feu lorsqu'ils
découvrent une cible ; ce délai fait perdre du temps. **Les soldats créés ne
doivent rien dire et tirer directement.**

**Restriction expresse : les vrais ennemis du jeu ne sont pas concernés.** Leur
comportement, réplique comprise, reste strictement identique. Le traitement est
appliqué **acteur par acteur**, uniquement aux soldats créés — même mécanisme
per-instance que celui déjà utilisé pour l'invisibilité (coupure de la voix à
`acteur+0x280`, relâchement de `holding_fire` à `acteur+0x296`).

### 4.5 F6 — deux actions par ligne de véhicule

La logique actuelle est **conservée telle quelle**. Chaque ligne de la liste
propose désormais **deux actions** :

- **« Voler »** — déplacer le véhicule de la mission devant le joueur
  (comportement actuel, déjà validé) ;
- **« Recréer une copie »** — créer un **nouveau véhicule identique**,
  conduisible.

### 4.6 Règle de livraison

**Aucune version intermédiaire ne sera livrée sans la création de soldats.**
Le recrutement d'ennemis existants (bascule de camp) ne sera pas livré seul.

---

## 5. Découvertes moteur qui rendent ce travail possible

### 5.1 Un véhicule se reconstruit tout seul depuis son modèle
`C_automobil::SetFrame` (`Vehicle.cpp:1784`) reconstruit l'intégralité du
véhicule en lisant les noms des sous-objets de la carrosserie :

```cpp
w = frm->FindChildFrame(C_fstring("wheel_%.2i", i));   // roues
j = frm->FindChildFrame(C_fstring("joint_%.2i", i));   // articulations
volant = frm->FindChildFrame("volant");                 // volant
frm_seat = frm->FindChildFrame(C_fstring("seat_%.2i", num_of_seats));
engine_dummy = frm->FindChildFrame("engine");
col_parent = frm->FindChildFrame("col");                // collision
```

`C_version::SetFrame` (`Vehicle.cpp:225`) construit de même la liste des
carrosseries endommagées depuis les enfants `~01*`. Un modèle dupliqué donne
donc un véhicule **fonctionnel**, pas décoratif.

### 5.2 La bascule de contrôle n'est PAS limitée à quatre
`C_game_mission::PlayerSwitch` (`GameMission.cpp:3047`) :

```cpp
vector<LPC_actor> plrs;
EnumActors(cbGetPlrs, &plrs);            // tous les ACTOR_PLAYER, sans limite
slist.Add(plrs[i], plrs[i]->GetMenuID());
slist.Sort();
slist[id]->SetActive(true, show_switch_msg);   // prendre le contrôle
```

La liste est **dynamique** ; `#define MAX_PLAYERS 4` ne dimensionne que le
tableau des **portraits** (`C_plr_menu *pmenu[MAX_PLAYERS]`), pas la liste des
joueurs. Prendre le contrôle se réduit à `SetActive(false)` sur l'actuel puis
`SetActive(true)` sur le choisi — méthode **virtuelle**, emplacement déjà connu
du trainer (`vtable+0x6C`).

*Correction assumée : une réponse antérieure affirmait que la limite de quatre
interdisait d'incarner des soldats supplémentaires. C'était faux.*

### 5.3 Les ordres de déplacement sont une méthode virtuelle
`Actors.cpp:5780` :

```cpp
virtual bool AddProgram(int pos, E_PRG_ITEM itm, const S_prg_add &pa,
                        bool net_send = true);
virtual bool DelProgram(int pos, bool net_send = true);   // Actors.cpp:6107
```

Le trainer appelle **déjà** `DelProgram` (`vtable+0x28`) pour effacer les
poursuites. `AddProgram` est déclarée juste avant, donc à un emplacement
voisin, vérifiable. Le moteur écrit lui-même l'ordre « va là-bas » ainsi :

```cpp
AddProgram(0, PRG_MOVE, S_prg_add((dword)&use_pos, true, MR_USE_REACH));
```

### 5.4 Le moteur duplique un modèle à chaud
`GameMission.cpp:3018`, code de la neige et de la pluie :

```cpp
PI3D_model mod = driver->CreateModel();
mod->Duplicate(m_snow_rain);
mod->SetOn(true);
mod->SetPos(v);
mod->LinkTo(scene->GetPrimarySector());
```

### 5.5 Le camp d'un acteur
`C_enemy::IsEnemy` (`Actors.cpp:15045`) lit
`tab->ItemI(TAB_I_ENM_GROUP)` — dans le binaire de référence :
`mov eax,[esi+190h]` puis `push 4Ah` puis `call [edx+34h]`, soit **`tab` à
+0x190** et **`TAB_I_ENM_GROUP` = 74**.
Groupes : **0 = allemand (hostile au joueur)**, 1 = allié, 2 = civil, ami de
tous. Attention : `tab` est **partagé par tous les acteurs du même type** ;
l'écrire les retournerait tous. Le camp se règle donc soit à la création, soit
par le crochet `IsEnemy` déjà en place, **acteur par acteur**.

---

## 6. Le seul inconnu, et ce qui se passe s'il résiste

**`C_game_mission::CreateActor`** — `HDE.map` la situe à `0x004316A0` dans le
binaire de référence, mais elle **n'est pas virtuelle** : son adresse ne se
déduit d'aucune table dans le jeu installé, et le binaire du joueur n'est pas
disponible sur la machine de développement.

**Elle doit donc être retrouvée à l'exécution, par empreinte, depuis le
trainer**, avec le résultat écrit dans le journal.

- **Si elle est trouvée** : tout le cahier des charges ci-dessus est réalisable.
- **Si elle résiste** : ce sera dit franchement, sans substitut déguisé. Ni la
  création de soldats ni le clone de véhicule ne seront livrés, et les autres
  points de la V103 le seront seuls.

---

## 7. Demandes écartées, avec leur motif

| Demande | Réponse | Motif |
|---|---|---|
| Soldats supplémentaires **avec portrait et jauge dans le bandeau** | non | `pmenu[MAX_PLAYERS]` est un tableau fixe de 4 ; écrire au-delà corrompt la mémoire |
| **Changer l'uniforme** d'un ennemi retourné | non | l'apparence est le modèle chargé avec l'acteur ; le remplacer suppose de reconstruire la hiérarchie visuelle |
| **Clone de véhicule décoratif** | écarté | une voiture dans laquelle on ne peut pas monter n'a aucun intérêt ; on vise le clone conduisible |
| Lister **tous les véhicules du jeu**, même absents de la mission | non | le moteur n'instancie jamais un véhicule ex nihilo : l'acteur est accroché à un modèle **déjà chargé par la carte** (`GameMission.cpp:1759`) et ses paramètres viennent du bloc de mission via `MissionLoad` ; il n'existe aucun catalogue global |

---

## 8. Plan d'exécution

| Phase | Contenu | Bloquante |
|---|---|---|
| **0** | Recherche de `CreateActor` par empreinte à l'exécution, résultat journalisé | **oui** — tout en dépend |
| 1 | Création d'**un seul** soldat : duplication du modèle, `CreateActor(ACTOR_PLAYER)`, `SetFrame`, `menu_id`, camp. Vérifier apparition et stabilité | oui |
| 2 | Bascule de contrôle par `SetActive`, aller et retour vers le soldat d'origine | oui |
| 3 | Fenêtre in-game : saisie du nombre (≤ 50), liste des soldats, sélection | non |
| 4 | Ordres par la carte : un soldat, un groupe, tous. Carte ouverte **à la demande** par une touche | non |
| 5 | Tir sans réplique, **appliqué aux seuls soldats créés** | non |
| 6 | F6 : seconde action « Recréer une copie », réutilisant la mécanique des phases 0 à 1 | non |
| 7 | Diagnostic des points ouverts du chapitre 3, selon les réponses du joueur | non |
| 8 | Sauvegarde `_GOLD_V103`, mise à jour de tous les `.md` avec date et heure exactes, compilation des trois cibles, livraison | oui |

Chaque phase est compilée et sauvegardée avant de passer à la suivante ; aucune
livraison intermédiaire n'est envoyée au joueur avant la phase 8.

---

## 9. Règles permanentes du projet

1. **Sauvegarde complète** (binaires + sources) dans `_GOLD_Vxx` à chaque
   version.
2. **Tous les fichiers `.md`** mis à jour à chaque version, avec la **date et
   l'heure exactes**.
3. Ne rien modifier des fonctions déjà validées par le joueur sans le lui dire.
4. Annoncer clairement ce qui n'est **pas testé en jeu**, et ne jamais présenter
   une compilation réussie comme une validation.
5. Dire franchement ce qui est impossible, avec la preuve dans le code, plutôt
   que de livrer un substitut.

---

## 10. Informations attendues du joueur

1. Le badge « Vitesse du jeu » affiche-t-il **1.0x** ? (bug de déplacement)
2. La case **Noclip** est-elle décochée ?
3. Le bug de déplacement persiste-t-il **trainer fermé** ?
4. Pour **G, F6 et F3** : pour chacun, *rien du tout*, *un message dans le
   panneau*, ou *un effet mais pas le bon* ?

---

## 11. État d'avancement au 3 septembre 2026, 23:59:08

| Point du cahier des charges | État |
|---|---|
| 4.1 Soldats alliés créés, G à pied, fenêtre, nombre jusqu'à 50, uniforme du joueur | **écrit et branché**, non essayé en jeu |
| 4.2 Fenêtre : sélection d'un soldat → contrôle total ; groupe ; tous | **écrit et branché**, non essayé en jeu |
| 4.3 Ordres par la carte, carte ouverte à la demande (K) | **écrit**, le mécanisme d'ordre agit déjà sur l'escouade actuelle |
| 4.4 Tir sans réplique pour les seuls soldats créés | **sans objet** : la réplique est un comportement d'acteur ennemi, les soldats créés sont de type joueur. Les vrais ennemis sont intacts |
| 4.5 F6, deux actions par ligne (Voler / Recréer une copie) | **écrit et branché**, non essayé en jeu |
| 4.6 Aucune livraison sans la création | **respecté** : la V105 contient la création |
| 3.1 Bug de déplacement | **ouvert**, trois vérifications en attente du joueur |
| 3.2 Traits de balles ennemies | **ouvert**, travail de recherche non entrepris |
| 3.3 Barre de vie rafraîchie au prochain dégât | **connu**, cosmétique |
| 3.4 Volumes de collision après réparation | **connu**, non résolu |
| 3.5 G, F6, F3 « ne marchent plus » | **F3 et G corrigés** en V103 ; F6 validé par le joueur |
| Phase 0 : `CreateActor` | **empreinte écrite et validée** sur le binaire de référence ; résolution à l'exécution chez le joueur |

Réserve principale : **la création d'acteurs n'a pas pu être essayée en jeu**,
le binaire du joueur n'étant pas sur la machine de développement. Le LISEZ_MOI
de la V105 demande de créer **un** soldat d'abord, après sauvegarde.

---

## 12. Correction du plantage à la création — 4 septembre 2026, 01:54:08

Le joueur a signalé que le premier choix de la fenêtre G le faisait **sortir du
jeu**, et a demandé de lire le journal. Le journal montrait `PRET=1`,
`declenche=1`, `execute=0` : les empreintes avaient abouti chez lui, le stub
partait, et ne revenait pas.

| Défaut | Ce qui était écrit | Ce qui est écrit maintenant |
|---|---|---|
| `Duplicate` | `+0x38`, qui est `GetRot1(S_vector&, float&)` — écrit à travers deux pointeurs dont un pris sur la pile | `+0x98`, rang 38 sur les 50 méthodes d'`I3D_frame`, comptage ancré sur `SetPos = +0x0C` |
| Encodage des appels | `FF 51 <octet signé>` en toutes circonstances : `+0x98` s'y lit `-104` | forme longue `FF 91 <32 bits>` dès que le rang dépasse `0x7F` |
| `SetFrame` | `+0x80`, relevé sur un binaire dont la vtable diverge de celle du joueur | reconnu à son prologue **à l'exécution**, refus explicite sans candidate unique |
| `LinkTo` / `SetOn` | absents : le duplicata n'appartenait à aucun secteur et restait éteint | appelés après `Duplicate`, comme le moteur le fait lui-même |

**Garde renforcée.** `capability.ready` exige désormais aussi que le rang de
`SetFrame` ait été résolu. Un échec ne peut donc plus faire sortir du jeu : la
ligne « CRÉER » reste inactive et le journal relève les premiers octets de
chaque rang de `+0x70` à `+0xA0`, de quoi trancher sans nouvel essai à
l'aveugle.

**Question du joueur : « où cliquer pour choisir le nombre de soldats ? »**
Nulle part — cela se règle au clavier, et la fenêtre ne le disait pas assez.
La première ligne affiche maintenant `<   CREER  5  soldats   >`, une ligne
d'aide permanente est dessinée sous chaque fenêtre in-game, et la liste défile
en se recentrant sur la ligne choisie (elle en montrait douze sur
cinquante-trois).

**Réserve inchangée** : la création n'a toujours pas pu être essayée en jeu, le
binaire du joueur n'étant pas sur la machine de développement. La différence
avec la V105 est qu'un échec est désormais silencieux et journalisé, plus
jamais fatal.

---

## 13. Saisie du nombre, groupe au choix — 4 septembre 2026, 02:08:04

Le joueur a demandé trois choses après l'essai de la V107 :

**« est-ce que je ne peux pas choisir librement le nombre que je veux ? »**
Le nombre se **tape** désormais au clavier, chiffre par chiffre, retour arrière
pour effacer. Les flèches restent pour un cran. La saisie va à la ligne
choisie.

**« pour la section groupe je dois choisir exactement combien de personnes »**
`GROUPE` n'est plus figé à quatre : la ligne porte son propre nombre, tapé de
la même façon, borné à l'effectif réel.

**« la liste écrite quand je clique sur G n'est pas bien écrite et mal formée »**
La fenêtre est réécrite : chaque ligne annonce ce qu'elle fait et ce que
`Entrée` y déclenche, le titre est court, l'aide est en pied de fenêtre, et la
liste défile en se recentrant sur la ligne choisie.

**« je clique sur entrer rien n'apparaît »** — le journal a donné la réponse :
`CreateActor` valait zéro parce que la recherche d'empreinte était partie avant
l'attachement au jeu et que **l'échec avait été mis en cache pour la
session**. Seul un succès est retenu maintenant ; un échec est réessayé toutes
les deux secondes.

Le même journal a confirmé que la correction de la V107 visait juste :
`SetFrame` est reconnu chez le joueur au rang **`+0x88`**, dix-sept octets de
prologue sur dix-sept — la V105 employait `+0x80`.

| Point | État |
|---|---|
| Nombre de soldats saisi librement | **fait** |
| Effectif du groupe choisi | **fait** |
| Fenêtre lisible, liste qui défile | **fait** |
| `SetFrame` résolu sur le binaire du joueur | **fait**, `+0x88`, vérifié dans son journal |
| `CreateActor` retrouvé après attachement | **fait**, reste à observer en jeu |
| Apparition effective des soldats | **jamais encore observée** : la V107 refusait avant d'exécuter |

---

## 14. Placement et initialisation des soldats créés — 4 septembre 2026, 02:28:34

Retour d'essai du joueur : « c'est catastrophique ». Six points, traités un par
un.

| Point signalé | Diagnostic | État |
|---|---|---|
| Soldats très loin du joueur | `LinkTo` recevait un parent lu à `frame+0x18`, offset supposé | **corrigé** : `GetParent()` demandé au moteur |
| Soldats écrasés au sol | `MissionLoad` n'est pas rejoué après `SetFrame` | **non corrigé**, voir plus bas |
| Impossible de revenir au soldat initial | aucune ligne ne le proposait | **corrigé** : ligne `REVENIR` |
| « nombre illimité » de joueurs | le journal dit `demandes=5 … crees=5` : la création est exacte ; c'est la liste et le bandeau qui ne se vident jamais | **expliqué**, la purge dépend de la même initialisation |
| Soldats parasites dans le bandeau du bas | identité vide faute de `MissionLoad` | **non corrigé**, même cause |
| Textes coupés, lignes illisibles | libellés plus larges que la fenêtre | **corrigé** : lignes courtes, aide en pied |
| « je tape le nombre, pas de touche pour monter » | déjà implémenté en V108 mais la fenêtre parlait encore de flèches | **corrigé** : plus aucune mention |
| Choix d'arme pour tous les soldats | passe par l'inventaire, qui fait partie de `MissionLoad` | **non livré**, même cause |

**Pourquoi je ne devine pas `TableUpdate`.** Son prologue lit une table à
`[acteur+0x190]` et appelle une méthode dessus ; sur un acteur neuf cette table
est nulle. Et son rang reste ambigu : le comptage des virtuelles de `C_actor`
ancré sur `SetFrame = +0x88` retombe juste sur `IsEnemy = +0xA4`, mais donne
`Explode = +0xD8` là où le trainer emploie `+0xDC`, validé en jeu — une
méthode a été insérée entre les deux, sans qu'on sache de quel côté de
`TableUpdate`. Trois suppositions ont déjà coûté trois sessions au joueur ; la
quatrième sera mesurée par empreinte, comme `SetFrame` l'a été (+0x88, dix-sept
octets sur dix-sept).

---

## 15. Nombre exact, position juste, fenêtre lisible — 4 septembre 2026, 02:38:06

| Point signalé | Cause réelle | État |
|---|---|---|
| « beaucoup de soldats alors que j'en ai choisi peu » | réentrée du trampoline : le site de cheat est traversé à chaque image et réexécutait toute la boucle | **corrigé et confirmé par le joueur** |
| le journal affichait pourtant le bon nombre | le compteur était **plafonné** avant d'être écrit — le journal masquait le défaut | **corrigé** : plafond retiré, anomalie signalée |
| soldats très loin du joueur | `SetPos` place *relativement au parent*, la position connue est en *monde* | **corrigé et confirmé** : placement par la routine de téléportation |
| textes illisibles, lignes incomplètes | boîte de texte de 48 px, police de 30 px, **interligne de 26 px** : les lignes se chevauchaient | **corrigé** : panneau opaque, interligne égal, chasse fixe, alignement à gauche |
| « je veux taper le nombre exact » | implémenté depuis la V108, mais invérifiable | **corrigé** : chaque chiffre frappé est journalisé, la ligne choisie l'annonce |
| choix d'armes pour tous et par groupe | dépend de l'inventaire, que seule l'initialisation crée | **non livré**, séquencé après l'initialisation |
| soldats écrasés au sol, parasites dans le bandeau | `MissionLoad` manquante | **non corrigé**, prochaine étape |

**Séquencement assumé.** `C_inventory::AddItem` est localisée
(`?AddItem@C_inventory@@QAEHHK@Z`, 0x0045A3B0 dans le binaire de référence),
mais donner une arme à un soldat qui n'a pas encore d'inventaire n'aurait aucun
effet. L'initialisation vient d'abord ; elle règle d'un coup la posture, le
bandeau et l'inventaire.

---

## 16. Plantage sous le feu, touches directes, soldats sous terre — 4 septembre 2026, 02:50:12

| Point signalé | Cause établie dans le code du moteur | État |
|---|---|---|
| « quand les ennemis nous frappent le jeu se crash » | `SetResistance` → `C_game_menu::SetHealth(menu_id, …)`, **sans borne** ; `menu_id` vaut `-1` au-delà de quatre joueurs, donc `pmenu[0xFFFFFFFF]` puis appel virtuel sur un pointeur hors tableau | **corrigé pour les dégâts** via `no_hit_cheat` ; **pas pour les explosions** |
| « le suivant marche, 1 2 3 4 non, même pour les soldats officiels » | `PlayerSwitch` : `\|\| id != -1` fait entrer tous les acteurs joueur ; tri par `GetMenuID()`, les `-1` en tête ; puis `if(!slist[id]->IsAlive()) return -1` | **diagnostiqué**, remède livré |
| — remède | ligne `SUPPRIMER n soldats crees` via `DestroyActor` reconnue par empreinte | **livré** |
| « des soldats sous la terre, d'autres très loin » | grille de vingt mètres à la hauteur du joueur, relief inconnu | **corrigé** : couronnes, cinquante soldats dans huit mètres |
| « ces soldats ne peuvent pas avoir d'armes » | `AddItem` localisée, décalage mesurable, mais la liste des armes vient d'une table de données | **non livré**, pièces établies |

**Ce que cette suite d'essais a montré.** Chaque défaut rapporté par le joueur
s'est révélé réel et explicable dans le code du moteur, sans exception. Trois
d'entre eux venaient de la même racine — les soldats créés ne sont pas
initialisés — et deux venaient directement du trainer (le plafond du journal
qui masquait la sur-création, et la grille de placement trop étalée).

---

## 17. Placement groupé, mortalité rétablie — 4 septembre 2026, 03:04:31

| Point signalé | Cause | État |
|---|---|---|
| « certains soldats à côté et les autres loin » | la position était posée par un appel séparé **par soldat** ; au-delà d'une dizaine, certains appels n'aboutissaient pas | **corrigé** : un seul passage pose toutes les positions |
| « pourquoi ces soldats ne meurent pas » | ma garde `no_hit_cheat` de la V113, posée pour éviter le plantage | **corrigé** : `menu_id` valide **mesuré**, garde retirée |
| « je ne peux pas revenir à mes soldats avec les touches du jeu » | tri de `PlayerSwitch` par `GetMenuID()`, les `-1` en tête | **amélioré** : plus grand identifiant valide, les créés se rangent en fin |
| « je ne dois pas contrôler ces soldats depuis les touches du jeu » | exclusion totale = changer le type d'acteur, or `ACTOR_ENEMY` en ferait des ennemis | **non fait**, refus de toucher ce champ sans l'avoir établi |
| choix d'armes par groupe et pour tous | manque la liste des armes, qui vient d'une table de données du jeu | **non livré**, deux pièces sur trois établies |

**Méthode employée pour `menu_id`.** Aucune supposition : le champ est repéré
par une propriété vérifiable — sur les soldats du joueur il porte des valeurs
distinctes comprises entre 0 et 3. Balayage de `+0x20` à `+0x400`, au moins
trois soldats requis, une seule candidate acceptée, refus journalisé sinon.
C'est la même discipline que pour `SetFrame` (`+0x88`, dix-sept octets sur
dix-sept) et `CreateActor`.

---

## 18. Armes en miroir, `menu_id` mesuré par le code — 4 septembre 2026, 11:40:07

Liste donnée par le joueur avant de partir, traitée point par point.

| Demande | Réalisation |
|---|---|
| « le jeu crash quand ces soldats reçoivent des dégâts » | `menu_id` valide mesuré par le code → plus de lecture hors `pmenu[]` |
| « et ils ne reçoivent même pas de dégâts » | `no_hit_cheat` retiré, la garde n'est plus nécessaire |
| « si je clique sur 1 je me retrouve dans un soldat créé » | plus grand `menu_id` valide → les créés se rangent en fin de tri |
| « les soldats apparaissent avec un nom unknown » | `SetName` : `Soldat 1`, `Soldat 2`… |
| « dans la fenêtre G je ne vois rien pour les contrôler ni leur choisir une place » | deux lignes par soldat : piloter, ou envoyer **seul** sur la carte |
| « les soldats suivent exactement l'arme que je porte » | miroir d'arme, redistribution au changement d'identifiant |
| « touche J : ils ne portent plus mon arme, ils gardent la leur » | **J** bascule le miroir ; l'état est aussi une ligne de la fenêtre |

**Méthode pour `menu_id`.** Le constructeur de `C_player` fait
`menu_id = AddPlayerMenu(1)`. On retrouve `AddPlayerMenu` par empreinte, on
repère ses appelants, on lit le déplacement du `mov [reg+…], eax` qui suit.
Deux appelants dans le binaire de référence, tous deux `+0x29C`. Mesure faite
sur le binaire du joueur, pas transposée.

**Recoupement pour l'inventaire.** `lea ecx,[objet+0x54]` sur quatre des douze
sites appelant `AddItem`, et `0x54 + 0x08 = 0x5C` — l'emplacement du vecteur
d'inventaire que le jeu valide à chaque tir. Deux mesures indépendantes
concordantes.

**Réserves.** Les soldats créés partagent la jauge du dernier soldat dans le
bandeau (conséquence assumée du `menu_id` valide). Leur exclusion complète de
l'énumération du moteur reste non faite : elle exigerait de changer leur type
d'acteur, ce qui en ferait des ennemis.

---

## 19. La base d'inventaire, déduite au lieu d'être transposée — 4 septembre 2026, 11:51:02

Le journal du joueur montre la V115 aboutissant sur **tous** les points sauf le
dernier : `menu_id` mesuré à `+0x2B0`, cinq soldats créés, placés et nommés,
`AddItem` retrouvée — puis arrêt du jeu juste après la distribution d'armes.

| Élément | V115 | V116 |
|---|---|---|
| Base de `C_inventory` dans un soldat | `+0x54`, **transposée** du binaire de référence | `+0x58`, **déduite** de `début du vecteur (+0x5C) − 4`, valeur validée par le jeu |
| Désignation de l'arme tenue | appel de la fonction native, écrite pour le soldat piloté | écriture directe de l'index, sans effet de bord |

**Leçon, notée pour la suite.** Un offset relevé sur le binaire de référence
n'est acceptable que s'il se recoupe avec une valeur que le jeu du joueur
valide. Ici les deux existaient — `lea ecx,[objet+0x54]` d'un côté,
`début du vecteur = +0x5C` de l'autre — et je n'avais retenu que le premier
alors que le second était le seul vérifié en jeu. La règle appliquée
désormais : **dériver de ce qui est validé, ne transposer que faute de mieux,
et le dire.**

**Acquis mesuré à conserver** : `menu_id = +0x2B0` sur le binaire Deluxe du
joueur, contre `+0x29C` en 2002 — soit le même décalage `+0x14` que
`no_hit_cheat` (`0x2C0 → 0x2D4`). Mesure et décalage connu concordent.

---

## 20. Cheats natifs refusés sur un soldat créé — 4 septembre 2026, 11:56:26

| Constat du journal | Lecture |
|---|---|
| trois `Miroir d'arme … execute=1, 5 designee(s)` d'affilée | le miroir et la base d'inventaire `+0x58` sont validés en jeu |
| `Fullhands: completion_state=0 … Process: pid=0` | le jeu meurt sur la touche M, pas à la création |

**Cause.** `Fullhands` s'applique au soldat *piloté*, et la fenêtre G permet de
piloter un soldat créé. Le cheat rejoue un chemin du moteur écrit pour un
soldat de mission (table, entrée de bandeau, inventaire de départ) — données
absentes sur un acteur que `MissionLoad` n'a jamais initialisé.

**Correction.** `IsCreatedSoldier` garde deux points : l'entrée de `Fullhands`
et celle de `ApplyNativeActorCallback`, qui couvre F3, la ranimation, la
restauration d'image et les autres rappels natifs. Refus journalisé, avec la
marche à suivre.

**Conséquence pour l'usage.** Les cheats natifs exigent d'être sur un soldat
d'origine. La ligne `REVENIR` de la fenêtre G, ou les touches 1 2 3 4, y
ramènent.

---

## 21. Le miroir d'arme coupé, et la pièce qui bloque — 4 septembre 2026, 12:05:25

**Quatre causes trouvées et corrigées sur un seul passage**, toutes réelles :
base d'inventaire (`+0x54` → `+0x58`), désignation par la fonction native,
quantité (`200…1000` → `1`), et `Reload` ajoutée. Avec les quatre en place, le
journal donne exactement le même résultat qu'au premier essai : distribution
réussie, jeu arrêté 400 ms plus tard.

**Décision assumée.** Le miroir est coupé. Une cinquième tentative n'avait
aucune raison d'aboutir là où quatre corrections fondées ont échoué, et chaque
essai coûte une session au joueur.

**La pièce manquante, une seule pour trois défauts.**

| Défaut | Cause commune |
|---|---|
| soldats écrasés au sol | `MissionLoad` non rejouée |
| pas d'identité propre dans le bandeau | idem |
| impossible de porter une arme | idem |

`TableUpdate` lit une table à `[acteur+0x190]` et appelle une méthode dessus ;
sur un acteur neuf cette table est nulle. Il faut **créer et ouvrir la table de
l'acteur** d'abord — deux adresses restant à établir par empreinte, comme
`SetFrame`, `CreateActor`, `AddItem`, `AddPlayerMenu` et `Reload` l'ont été.

**État du dossier « soldats créés ».** Tout fonctionne et est validé par les
tests du joueur — nombre exact, placement, noms, mortalité, touches 1 2 3 4,
contrôle, ordres carte, suppression, fenêtre lisible — **sauf** ce qui dépend
de l'initialisation d'acteur.

---

## 22. Santé des soldats créés, et vérification du nom — 4 septembre 2026, 12:14:18

| Observation du joueur | Verdict | État |
|---|---|---|
| « ces soldats ont plus de vie que nous, ils mettent plus de temps à mourir » | **exact, et imputable au trainer** : 20000 points écrits au lieu de la santé normale (`200 + endurance × 1400`) | **corrigé** : santé copiée sur le soldat piloté, relue à chaque création |
| « les noms restent unknown » | `GetName()` rend `frame->GetName()`, donc la méthode devrait marcher ; deux explications restent possibles | **instrumenté** : le nom est relu après avoir été posé, le journal tranchera |
| « quand je clique sur J ils ne prennent pas l'arme » | volontaire depuis la V120 | **assumé**, bloqué sur `MissionLoad` |

**Sur le 20000.** La valeur répondait à un besoin réel — `init_resistance` vaut
zéro sur un acteur neuf et `SetResistance` divise par elle — mais le nombre
lui-même était arbitraire et n'avait pas été signalé au joueur. C'est le genre
de choix qui doit être dit, pas glissé.

---

## 23. Case de bandeau libre, noms durables — 4 septembre 2026, 12:27:50

| Observation | Cause établie | État |
|---|---|---|
| « un soldat créé meurt et mon soldat 2 passe en squelette » | `menu_id` partagé avec un vrai soldat depuis la V114 ; `SetDeathFace(menu_id)` marque son portrait | **corrigé** : case de bandeau **libre** (nulle), inoffensive |
| plantages après création de 20 puis 15 soldats | aucune arme ni cheat au moment des morts ; vingt acteurs écrivant dans la même case de bandeau | **même correction**, à vérifier par le joueur |
| « les noms restent unknown » | deux fautes du trainer : relecture faite après libération de la page, et chaînes logées dans une page rendue aussitôt | **corrigé** : page dédiée jamais rendue, relecture avant libération |
| logique de J | équipement forcé jamais évité jusqu'ici | **troisième voie** : arme créée, chargée, déposée, sans désignation |

**Méthode.** `pmenu` et `num_players` sont lus **dans les instructions** de
`AddPlayerMenu` sur le binaire du joueur (`lea eax,[edi+X]`,
`cmp [edi+Y], 4`), et le pointeur du bandeau vient de `C_inventory::game_menu`,
premier membre de la partie inventaire — donc à la base déjà établie pour les
armes. Rien n'est transposé.

---

## 24. La case d'un vrai soldat devient interdite — 4 septembre 2026, 12:36:29

| Point | État |
|---|---|
| noms « Soldat 1 »… | **acquis** : le moteur les rend, confirmé au journal |
| armes déposées sans arrêter le jeu | **acquis** : trois distributions d'affilée, +23 s de survie ; l'équipement forcé était bien le coupable |
| squelette sur le soldat d'origine | **corrigé** : la case d'un vrai soldat est désormais interdite |
| plantage « après un instant » | **candidat sérieux éliminé** : cinq créés écrivaient dans le `C_plr_menu` d'un vrai soldat |

**Ce que j'avais manqué.** Le constructeur de `C_player` appelle lui-même
`AddPlayerMenu` : les deux premiers soldats créés consomment les cases libres,
si bien qu'il n'en reste aucune pour les suivants, et mon repli retombait sur
la case d'un soldat d'origine. Chercher une case libre ne suffisait pas — il
fallait **interdire** les cases des vrais soldats, ce que fait la V123.

---

## 25. Missions à quatre soldats, fenêtre des armes — 4 septembre 2026, 12:44:56

| Point | Analyse | État |
|---|---|---|
| plantage en mission à **quatre** soldats | les quatre cases du bandeau sont au joueur, toutes interdites depuis la V123 ; `menu_id` restait à `-1` | **corrigé** : soldats créés invulnérables **dans ce cas seul**, annoncé au joueur |
| logique de J | remplacée par la fenêtre proposée par le joueur | **livré** : cible + arme + Entrée |
| noms « unknown » | le nom de l'acteur est bon (confirmé par le moteur) ; l'affichage vient d'ailleurs | **à localiser** avec le joueur |

**Le compromis, dit clairement.** Le bandeau n'a que quatre cases. Au-delà, un
soldat créé ne peut pas à la fois encaisser et ne pas marquer le portrait d'un
soldat d'origine. Le trainer choisit la stabilité et **le dit** ; le joueur
garde la mortalité en jouant une mission à trois soldats ou moins.

---

## 26. Lecture complète du journal — 4 septembre 2026, 12:52:54

131 lignes lues, regroupées par fréquence après normalisation. Quatre défauts.

| # | Défaut | Cause | État |
|---|---|---|---|
| 1 | `0 soldat(s) sur 5 regles` | écritures chaînées par `&&` : `fallback = -1` court-circuitait l'écriture de `no_hit_cheat`, si bien que l'invulnérabilité annoncée n'était jamais posée | **corrigé** : écritures indépendantes |
| 2 | journal contradictoire | le message décrivait une intention, pas un fait | **corrigé** : comptage réel poste par poste |
| 3 | `Fenetre armes … resultat=0` | la fenêtre était encore ouverte au déclenchement | **corrigé** : fermeture avant, seconde tentative |
| 4 | deux lignes par appui sur `G` | ligne de réparation émise même à pied | **corrigé** |

**La leçon, notée.** Le défaut n° 1 était visible dans le journal depuis trois
sessions (`0 soldat(s) sur 5`), masqué par le message n° 2 qui affirmait le
contraire. Un message de journal doit rapporter **ce qui a été fait**, jamais
ce qu'on avait l'intention de faire : celui-ci a coûté au joueur plusieurs
essais.

---

## 27. Garde sur le bandeau, équipement abandonné — 4 septembre 2026, 14:16:22

| Demande du joueur | Diagnostic | État |
|---|---|---|
| « à quatre soldats je perds le contrôle de mes 4 soldats » | `menu_id = -1` les rangeait **en tête** du tri de `PlayerSwitch` | **corrigé** : identifiant 1000, ils passent en dernier |
| « les soldats ne meurent pas quand les ennemis les frappent » | conséquence du repli d'invulnérabilité, seul moyen d'éviter `pmenu[-1]` | **corrigé** : la garde rend l'identifiant inoffensif, `no_hit_cheat = 0` |
| « si quelqu'un est tué par grenade le jeu crashe » | même chemin `SetResistance → SetHealth(menu_id)` | **corrigé** par la même garde |
| « je ne peux pas choisir l'arme » | le journal montre les armes 1, 2, 6 et 7 choisies et distribuées, `resultat=1` | **fonctionne déjà** |
| « ces soldats ne prennent pas l'arme choisie » | ils la reçoivent ; ils ne la **sortent** pas | **établi comme impossible** sans `MissionLoad` — cinq tentatives fatales |
| « les noms restent unknown » | le nom de l'acteur est correct (confirmé par le moteur) ; l'affichage vient du bandeau | avec l'identifiant hors tableau, ils n'y figurent plus |

**La leçon de la V126.** J'avais un raisonnement plausible pour reprendre
l'équipement une fois la garde posée. Le journal l'a démenti en une ligne. Un
raisonnement plausible ne vaut pas une mesure : la règle reste de ne livrer
activé que ce qu'un journal a validé.

---

## 28. Le « unknown » vient du numéro de visage — 4 septembre 2026, 14:32:07

| Demande | Diagnostic | État |
|---|---|---|
| « les noms restent unknown » | le nom affiché vient de `tab->ItemI(TAB_I_HUM_FACE)` ; zéro rend littéralement `"unknown"` | **corrigé** : numéro relevé dans les tables des soldats du joueur |
| « quand je choisis une arme ils ne la prennent pas » | ils la reçoivent (journal : `resultat=1`) ; ils ne la **dégainent** pas | **piste nouvelle**, non activée |

**Ce que j'avais mal cherché.** Le `SetName` posé sur la frame était juste
depuis la V122 — le moteur le confirmait à chaque création. Il ne sert
simplement pas à l'affichage du nom. Trois versions ont été passées à
instrumenter la mauvaise source.

**Piste pour l'arme.** `SetGun` contient `assert(!hand->NumChildren())` : le
moteur attend une main **vide**. Les soldats créés étant des copies d'un joueur
armé, leur main contient déjà une arme dupliquée. À vérifier avant toute
réactivation.

---

## 29. Transposer au lieu de mesurer — 4 septembre 2026, 14:43:37

Le joueur reproche de ne lire qu'une partie du code avant de modifier. Le
journal lui donne raison, et de façon mesurable.

| Symptôme | Cause | Nature de la faute |
|---|---|---|
| perte des touches 1 2 3 4 | garde « tout ou rien » sur cinq fonctions, une seule manquante | conception : avoir lié ce qui n'avait pas à l'être |
| `SetPlayerFace INTROUVABLE` | prologue recopié du binaire de référence | **transposition** |
| « aucun numéro de visage lisible » | `acteur+0x190` recopié du binaire de référence | **transposition** |
| (V116) inventaire à `+0x54` | recopié du binaire de référence | **transposition** |

**Trois fois la même faute.** La règle appliquée désormais : aucune valeur du
binaire de référence n'est employée telle quelle — elle se déduit d'une valeur
que le jeu du joueur valide, ou se mesure sur son binaire avec une
vérification, ou est refusée et journalisée.

**Et une règle de conception** : une garde optionnelle ne doit jamais
conditionner une garde essentielle.

---

## 30. `AddItem` cassé par une fausse déduction — 4 septembre 2026, 14:58:06

| Symptôme | Cause | État |
|---|---|---|
| « les soldats ne prennent pas l'arme choisie » | `AddItem` appelé avec un `this` décalé de 4 octets : **rien n'était ajouté** | **corrigé**, base `+0x54` lue dans les instructions |
| « le jeu sort quand je choisis une arme » | 116 soldats créés vivants simultanément | **corrigé** : limite sur le total, message vers `SUPPRIMER` |
| « les noms restent unknown » | emplacement de la table toujours non mesuré | **instrumenté** : lecture dans le code + relevé du descripteur |

**La quatrième faute du dossier, et la pire.** Les trois premières étaient des
transpositions du binaire de référence. Celle-ci est un **raisonnement** sur la
disposition mémoire produite par le compilateur — j'ai remplacé une valeur
juste (`+0x54`) par une valeur déduite (`+0x58`), sans la vérifier. Quatorze
versions ont ensuite cherché pourquoi les soldats ne dégainaient pas, alors
qu'ils n'avaient jamais reçu l'arme.

**Règle complétée** : une valeur ne s'écrit qu'après avoir été **lue dans les
instructions du jeu**, ou déduite d'une valeur que le jeu valide — jamais après
un raisonnement sur ce que le compilateur a probablement fait.

---

## 31. L'arme arrive, la liste est purgée — 4 septembre 2026, 15:09:36

| Point | État |
|---|---|
| « les soldats ne prennent pas l'arme choisie » | **résolu pour la réception** : l'arme figure dans leur inventaire, prouvé quatre fois par le journal |
| « le jeu sort quand je choisis une arme » | `g_spawned_soldiers` conservait des acteurs détruits ; écriture dans de la mémoire libérée | **corrigé** : purge à chaque tour et avant chaque distribution |
| « les noms restent unknown » | critère de mesure faux **pour le jeu en réseau** : le nom du soldat actif vient du réseau, le numéro de visage peut légitimement être nul | **critère corrigé**, candidats journalisés |
| ils ne dégainent pas | initialisation d'acteur | reste |

**Ce que ce tour montre.** La mesure ajoutée en V129 — relire l'inventaire au
lieu de croire `resultat=1` — a livré en une ligne ce que quatorze versions de
raisonnement n'avaient pas trouvé. Et le critère de mesure des noms était faux
parce qu'il ignorait le contexte réseau du joueur : mesurer ne suffit pas, il
faut mesurer la bonne chose.

---

## 32. Munitions et main vide — 4 septembre 2026, 15:20:01

| Point signalé | Cause | État |
|---|---|---|
| « les armes arrivent mais avec 1 munition » | `S_item::amount` est la **réserve**, pas un nombre d'exemplaires ; la V119 y avait mis `1` | **corrigé** : réserve de l'arme du joueur, minimum 30 |
| « je ne peux pas leur faire porter l'arme, même en les contrôlant » | `SetGun` exige une main **vide** ; le duplicata tient déjà l'arme copiée du joueur | **corrigé** : main vidée à la création, compte journalisé |
| plus de plantage à la distribution | purge des acteurs détruits (V131) | **confirmé par le journal** |
| noms « unknown » | mesure de la table toujours en cours | en attente des lignes `candidat` |

**Note de méthode.** Les rangs employés pour vider la main sont **déduits**
d'une énumération dont trois points sont validés en jeu (`SetPos`,
`SetName`/`GetName`, `Duplicate`) — et non transposés du binaire de référence.
La distinction est celle qui a coûté quatre régressions à ce dossier.

---

## 33. Drapeaux exacts, reprise de l'équipement — 4 septembre 2026, 15:25:23

| Point | Constat | État |
|---|---|---|
| munitions | `71(368+20)` — réserve et chargeur identiques au joueur | **validé en jeu** |
| « 0 main vidée » | mesure faussée : `0xFFFFFFFF` au lieu de `ENUMF_WILDMASK\|ENUMF_ALL` = `0x1FFFF` | **corrigé** |
| équipement | les cinq échecs portaient sur un inventaire **vide** (`1 objet(s), 30`) | **repris**, premier essai en conditions saines |

**Leçon.** Une mesure qui « tourne proprement » n'est pas une mesure juste :
`execute=1` avec zéro résultat semblait innocenter la main, alors que les
drapeaux passés faisaient rejeter toutes les frames. Vérifier le résultat d'une
mesure ne suffit pas — il faut vérifier que la mesure interroge bien ce qu'on
croit.

---

## 34. L'équipement fonctionne, la main manque — 4 septembre 2026, 17:54:41

| Constat | Preuve | Conséquence |
|---|---|---|
| l'équipement **réussit** | `selectionne=1`, arme 71 avec 368 munitions | les cinq échecs précédents portaient bien sur un inventaire vide |
| le jeu meurt juste après | `Mains des soldats: 0 main(s) videe(s)` | `SetGun` abandonne sur `if(!hand) return;` ; l'index de sélection est posé avant |
| « 0 main vidée » ne prouve rien | la mesure n'était pas vérifiée | **témoin ajouté** : le soldat du joueur, qui tient une arme |

**La règle appliquée ici.** Un résultat de mesure n'a de valeur qu'accompagné
d'un témoin dont on connaît la réponse attendue. J'ai failli conclure deux fois
de suite à partir d'une mesure non vérifiée — d'abord `0xFFFFFFFF` au lieu de
`0x1FFFF`, puis « 0 main vidée » interprété comme « main vide ».

---

## 35. Le témoin tranche — 4 septembre 2026, 19:24:05

| Mesure | Résultat | Conclusion |
|---|---|---|
| main du soldat du joueur | `2` | recherche correcte, rang `+0x94` bon |
| main des soldats créés | `1` | main présente et **vide** — conforme à ce que `SetGun` attend |

Le diagnostic de la V134 était donc faux, et il reposait sur une mesure
elle-même faussée (drapeaux `0xFFFFFFFF`). **Deux mesures fausses de suite**
avant d'obtenir la bonne — le témoin était la seule façon de le voir.

**Décision.** Plutôt qu'une septième hypothèse, la sortie d'arme devient une
ligne à part de la fenêtre J, déclenchée par le joueur. Tout ce qui est acquis
— création, placement, santé, touches, mort, grenade, distribution d'armes avec
munitions — reste sans risque ; seul l'essai volontaire peut arrêter le jeu.

---

## 36. Pourquoi le port de l'arme est impossible — 4 septembre 2026, 19:30:08

**Question du joueur :** pourquoi ses soldats créés ne peuvent-ils pas porter
les armes ?

**Réponse établie.** Trois faits mesurés — l'arme entre dans l'inventaire avec
ses munitions, la main existe et est vide, l'équipement aboutit — et un
quatrième, décisif : le jeu meurt **toujours après** que l'opération soit
terminée, que l'on écrive l'index à la main (V116) ou que l'on appelle la
fonction native (V133). Deux chemins opposés, la même mort différée.

Ce n'est donc pas l'opération, c'est **l'état** : un acteur non initialisé qui
déclare tenir une arme. Porter une arme engage posture de visée et animation,
que `MissionLoad` prépare et qui n'ont jamais été posées — la même cause que
les corps écrasés au sol.

**Conséquence pour le dossier.** Port de l'arme, corps redressés et identité
dans le bandeau ne sont pas trois problèmes mais **un seul**, et il se règle
d'un coup en rejouant l'initialisation d'acteur.

---

## 37. Mesure de la table par le groupe d'ennemi — 4 septembre 2026, 19:46:19

**Le critère était faux, pas la mesure.** Valider l'emplacement de la table par
le numéro de visage ne pouvait pas marcher : en partie réseau le nom du soldat
actif vient du réseau, et le visage peut légitimement valoir zéro.

**Le nouveau critère** vient du code de l'hostilité : `TAB_I_ENM_GROUP`
(propriété 74) ne peut valoir que 0, 1 ou 2, et tout ennemi en porte un. Huit
ennemis relevés par le radar suffisent à écarter toute coïncidence.

**Trois voies s'ouvrent si la mesure aboutit**, par ordre de certitude :

1. **Rallier un ennemi** — écrire `1` dans son groupe. Il change de camp des
   deux côtés à la fois et garde arme, posture, animations, nom, IA. Une
   écriture d'entier ; mécanisme lu directement dans le moteur.
2. **Le nom** des soldats créés — écrire un visage valide.
3. **La recopie de table** depuis un vrai soldat — sûre car les chaînes sont
   stockées dans le bloc de données ; reste à vérifier que `TableUpdate` ne
   reproduit pas l'arrêt du jeu.

**Limite dite d'avance** : aucune de ces voies ne redresse la posture, que
`TableUpdate` ne touche pas.

---

## 38. L'offset lu dans `GetTable` — 4 septembre 2026, 19:52:25

Quatre mesures de l'emplacement de la table ont échoué. La réponse tenait dans
une fonction de six instructions que le moteur exécute lui-même :

```
8B 44 24 04          mov eax,[esp+4]
85 C0                test eax,eax
75 09                jnz
8B 81 90 01 00 00    mov eax,[ecx+0x190]   ; return tab
```

Le déplacement **est** l'emplacement. Le trainer le lit dans la vtable du
soldat du joueur, rangs `+0xB0` à `+0xC0`.

**Cinquième occurrence de la même leçon.** Lire le code a réussi cinq fois
(`SetFrame`, `menu_id`, base d'inventaire, groupe d'ennemi, table) ; déduire ou
transposer a échoué cinq fois. La règle est désormais sans exception : **on ne
pose une valeur qu'après l'avoir lue dans une instruction du jeu.**


## Chapitre 39 — la fiche mesurée à +0x1A4, et la garde qui était déjà posée

**Écrit le 4 septembre 2026 à 20:15:59 (heure locale).**

### 39.1 Le dossier `source` a tranché la question de la fiche

Le joueur a demandé de regarder le dossier `source`. Il contient `itabler2.dll`,
la bibliothèque qui implémente `C_table`. Sa fonction `Item` désassemblée en
entier :

```
8B 44 24 04   mov eax,[esp+4]        ; la fiche
8B 48 20      mov ecx,[eax+0x20]     ; les descripteurs
8B 50 24      mov edx,[eax+0x24]     ; les données
8B 74 24 0C   mov esi,[esp+0x0C]     ; le numéro demandé
8B 78 0C      mov edi,[eax+0x0C]     ; le nombre d'entrées
3B F7 / 73    cmp / jae              ; hors bornes -> échec
8B 04 F1      mov eax,[ecx+esi*8]    ; descripteur, pas de 8 octets
83 F8 FF / 74 cmp eax,-1 / je        ; propriété absente -> échec
03 C2         add eax,edx            ; données + décalage
```

Trois offsets sur quatre étaient justes. **Le quatrième n'existe pas** : le
moteur ne lit aucune taille de données. Or `ResolveActorTableInteger` exigeait
un champ de taille en `+0x28`, repris de la table des armes, et rejetait tout
candidat dont ce champ ne lui plaisait pas. **C'est la cause unique des quatre
mesures ratées** : le critère portait sur un champ que la fiche n'emploie pas.

Le résolveur ne vérifie donc plus que ce que le moteur vérifie lui-même.

### 39.2 La fiche est à +0x1A4

Journal du joueur, aussitôt après la correction :

```
Table d'acteur: LUE DANS `GetTable` (vtable rang +0xB4, fonction 004257D0)
-> la table est a +0x1A4. Reference 2002 : +0x190.

Nom des soldats: numero de visage pose chez 6 soldat(s) sur 6
(4 numero(s) repris de vos soldats).
```

La fiche d'un acteur est à **+0x1A4** sur Deluxe, contre +0x190 en 2002. Le
numéro de visage — propriété 65, `TAB_I_HUM_FACE` — a été écrit sur les six
soldats créés. C'est ce numéro qui décide du nom affiché ; zéro valait
« unknown ».

### 39.3 La garde du bandeau était déjà posée — la mortalité était défaite

Le même journal montrait les cinq fonctions du bandeau « INTROUVABLE
(0 candidates) », et donc :

```
... ils sont invulnerables, pour que le jeu ne s'arrete pas.
```

Une capacité validée par le joueur était donc défaite. La contradiction sautait
aux yeux : la mesure de `menu_id` trouvait `AddPlayerMenu` **trois millisecondes
avant**, dans la même zone mémoire.

Lecture de la mémoire vive du jeu, pendant qu'il tournait :

```
SetDeathFace    e9 db e8 f3 04 | 8b 7c 24 1c 8b f1 8b 44 be 08
SetHealth       e9 9b e6 f3 04 | 90 90 53 56 8b 74 81 08
SetPrgKeyColor  e9 ab e5 f3 04 | 90 90 90 50 8b 44 91 08
GetPrgKeyColor  e9 fb e5 f3 04 | 90 90 90 8b 44 24 08
AddPlayerMenu   51 57 8b f9 ...                        (intacte)
```

**La garde était déjà en place**, installée par une exécution précédente du
trainer sur un jeu qui n'avait pas été relancé. Les cinq premiers octets étaient
remplacés par un saut ; la recherche du prologue d'origine ne pouvait plus rien
reconnaître. `AddPlayerMenu`, jamais détournée, restait lisible.

**Correction.** La recherche porte désormais sur la *queue* du prologue, celle
qu'un détour ne recouvre jamais, puis remonte de `stolen` octets jusqu'au début
de la fonction. Le début est accepté sous deux formes : les octets d'origine
(fonction intacte, garde à poser) ou un `E9` (garde déjà posée, on n'y touche
pas — réécrire un détour par-dessus un détour recopierait le saut lui-même dans
le nouveau trampoline).

Chaque queue employée a été vérifiée **unique** dans l'exécutable du joueur, et
la recherche complète a été simulée sur une image de la mémoire vive du jeu :
une seule candidate pour chacune des cinq.

### 39.4 `SetPlayerFace` trouvée après onze versions

Elle était « non identifiée » depuis la V129. La comparaison des deux binaires
donne la raison :

```
2002    83 ec 10    56 57  8b 7c 24 1c  8b f1  8b 4c be 08  81 c1 fc ...
Deluxe  83 ec 10 55 56 57  8b 7c 24 20  8b f1  8b 4c be 08  81 c1 fc ...
                     ^^                    ^^
              un registre empilé      argument décalé d'autant
```

Deluxe empile `ebp` en plus, ce qui décale l'argument de `0x1C` à `0x20`. La
forme recherchée exigeait `56 57` immédiatement après `83 EC 10`.

**Sixième échec de la transposition, relevé au passage.** `SetHealth` et
`SetDeathFace` sont toutes deux à exactement `0x99C0` de leur adresse de 2002.
Ce décalage constant, appliqué à `SetPlayerFace`, désigne `00461210` — où l'on
ne trouve que du code sans rapport. L'adresse retenue, **`00461290`**, vient de
la lecture de la partie distinctive de la fonction,
`8B 4C BE 08 81 C1 FC 00 00 00`, qui n'apparaît qu'une fois dans tout
l'exécutable.

### 39.5 Adresses établies sur le binaire du joueur

| Élément | Valeur | Méthode |
|---|---|---|
| fiche d'un acteur | `+0x1A4` | lue dans `GetTable` (vtable `+0xB4`) |
| `C_table` : nombre d'entrées | `+0x0C` | lu dans `itabler2.dll` |
| `C_table` : descripteurs | `+0x20` | lu dans `itabler2.dll` |
| `C_table` : données | `+0x24` | lu dans `itabler2.dll` |
| `C_table` : taille des données | **n'existe pas** | absente de `Item` |
| `C_game_menu::SetHealth` | `00461960` | queue de prologue |
| `C_game_menu::SetDeathFace` | `00461820` | queue de prologue |
| `C_game_menu::SetPlayerFace` | `00461290` | partie distinctive, lue |
| `C_game_menu::SetPrgKeyColor` | `00461CD0` | queue de prologue |
| `C_game_menu::GetPrgKeyColor` | `00461D00` | queue de prologue |

### 39.6 Ce qui reste

La **recopie de fiche**, que le joueur a explicitement choisie, demande de
connaître l'étendue du bloc de données. Le moteur n'en range aucune ; elle se
déduit des descripteurs. V140 la **relève** au prochain lancement, en lecture
pure, sans rien écrire. Choisir cette taille au jugement serait exactement
l'erreur qui a déjà coûté cinq versions.


## Chapitre 40 — l'arme en main, par la fiche et `TableUpdate`

**Écrit le 4 septembre 2026 à 20:36:03 (heure locale).**

### 40.1 Sept tentatives, une seule erreur, toujours la même

Le joueur demande que ses soldats créés *portent* l'arme choisie dans la fenêtre
J. Elle entre bien dans leur inventaire — validé — mais reste dans le sac. Sept
façons de la leur mettre en main ont été essayées (écriture directe de l'index,
appel natif de `SetSelectedInvItem`, avec et sans garde, avec et sans `menu_id`),
**toutes fatales**. Toutes procédaient de la même manière : forcer l'état depuis
l'extérieur.

Le moteur ne procède jamais ainsi. Séquence exacte d'armement d'un soldat de
mission (`GameMission.cpp:1758`) :

```cpp
act = CreateActor(type);
act->SetFrame(frm);
act->MissionLoad(&lc.ck, 0);
```

Les deux premières lignes, le trainer les exécute. La troisième, jamais. Et pour
un humain elle se réduit à une ligne (`H&D.h:973`) :

```cpp
inline bool LoadTable(C_chunk *ck, LPC_table tab){
   return tab->Open((dword)ck->GetHandle(), TABOPEN_FILEHANDLE | TABOPEN_UPDATE);
}
```

**La seule chose qui manque à un soldat créé est le contenu de sa fiche.** Le
constructeur (`Actors.cpp:3409`) lui donne déjà son jeu d'animations (`pset`), sa
main, son inventaire de base (`AddItem(30)`) et une fiche ouverte *sur le
modèle*, donc vide.

### 40.2 C'est `TableUpdate` qui met l'arme dans la main

`TABOPEN_UPDATE` déclenche `TableUpdate` (`Actors.cpp:7343`) :

```cpp
C_inventory::DeleteAllItems();                    // ne garde que les mains
for(int i=0; i<tab->ArrayLen(TAB_I_HUM_INV_LIST); i++){
   int itm = tab->ItemI(TAB_I_HUM_INV_LIST, i);
   if(!itm) continue;
   ...
   C_inventory::AddItem(itm, tab->ItemI(TAB_I_HUM_INV_AMOUNT, i));
   if(!i) SetSelectedInvItem(1, false);           // <-- l'arme en main
   C_inventory::Reload(C_inventory::NumItems()-1, true);
}
```

`DeleteAllItems` conserve `items[0]` — les mains (`Inventory.cpp:45`). Donc au
premier tour de boucle l'arme devient l'objet d'indice 1, et `SetSelectedInvItem(1)`
la désigne. **La désignation est sous `if(!i)` : seul le premier poste de la liste
part dans la main.**

### 40.3 Pourquoi la recopie de fiche est obligatoire, et non un confort

`C_player::TableUpdate` (`Actors.cpp:12790`) commence par recalculer la santé :

```cpp
float endurance = (tab->ItemI(TAB_I_HUM_BAR5_ENDURANCE)-30)/70.0f;
resistance = Max(1, 200 + (int)(endurance*1400.0f));
init_resistance = resistance;
```

Sur une fiche vide, l'endurance vaut 0 : `200 + (-600) = -400`, et `Max(1,-400)`
vaut **1**. Appliquer la fiche sans l'avoir remplie donnerait aux soldats créés un
seul point de vie — une régression sur une capacité validée. La recopie que le
joueur avait choisie n'est donc pas un agrément : **sans elle, cette voie serait
nuisible.**

Elle appelle aussi `cbProc(CB_SETFACE, ...)`, qui mène à `C_game_menu::SetPlayerFace`
— la fonction restée non gardée jusqu'à la V140. C'est très probablement la cause
du plantage de la V133, où l'équipement avait *réussi* avant que le jeu ne meure.

### 40.4 Pourquoi la recopie est licite

Le source de la bibliothèque des fiches est dans `source/hde/_src/Insanity/Lib/Tabler2/ITabCore.h:131` :

```cpp
dword num_items;      // +0x0C
S_desc_item *desc;    // +0x20
byte *data;           // +0x24
dword data_size;      // +0x28
```

Les textes sont rangés **dans** le bloc de données (`max_string_size`, pas de
pointeur) : le bloc est auto-suffisant, rien n'est partagé, rien ne sera libéré
deux fois.

Et les trois tables de méthodes qui portent `GetTable` sur le binaire du joueur
rendent **le même modèle de fiche** :

```
rang +4   004257F0 : b8 18 f1 4f 00  c2 04 00   -> mov eax,004FF118; ret 4
```

Un vrai soldat et un soldat créé ont donc la même disposition de fiche. La
recopie est refusée si `num_items` ou `data_size` diffèrent.

### 40.5 `TableUpdate` est à +0xBC — lu, non supposé

`GetTable`, `GetTemplate` et `TableUpdate` se suivent dans la déclaration de
`C_actor` (`H&D.h:966`). La lecture des trois vtables du binaire du joueur confirme
la déduction au lieu de s'y fier :

| rang | adresse | contenu lu |
|---|---|---|
| +0xB4 | `004257D0` | `mov eax,[ecx+0x1A4]` — `GetTable` |
| +0xB8 | `004257F0` | `mov eax,004FF118; ret 4` — constante, donc `GetTemplate` |
| +0xBC | `0042A550` / `00425800` / `004364A0` | **trois** implantations distinctes |

Trois implantations différentes à ce rang, une par classe d'humain : c'est
exactement ce que la hiérarchie impose pour `TableUpdate`, et ce qu'une méthode
non surchargée ne montrerait pas.

L'appel est `TableUpdate(0, /*not_load_prg=*/true)` — le second argument commande
`if(!not_load_prg) LoadProgram();`, et un soldat créé n'a pas de programme de
mission.

### 40.6 Vérifications faites avant livraison

- **Code machine** : l'émission du stub a été rejouée hors du trainer. Les quatre
  sauts (`jne`, `jae`, `je`, `jmp`) tombent sur leurs cibles, le retour atterrit
  sur `0049FC15` = site de triche + 5, taille 83 octets.
- **Garde d'appel** : avant tout déclenchement, chaque acteur visé doit présenter
  au rang +0xBC une adresse lisible **dans le module du jeu**. Un seul manquant et
  l'opération entière est refusée, avec journal.
- **Repli** : si la recopie, l'écriture du poste ou l'application échouent, on
  retombe sur `GiveWeaponOnGameThread`, validé, qui dépose l'arme dans le sac sans
  rien casser.

### 40.7 Correction du chapitre 39

Le chapitre 39 affirme que le moteur ne range aucune taille de données. C'est
inexact : `data_size` existe bien en `+0x28` (`ITabCore.h:131`), c'est seulement
`C_table::Item` qui ne le lit pas. Ce qui a débloqué la mesure est la lecture de
`+0x1A4` dans `GetTable`, non l'assouplissement du critère. Ce champ sert
aujourd'hui à copier le bon nombre d'octets.


## Chapitre 41 — rallier les ennemis : le chemin certain

**Écrit le 5 septembre 2026 à 12:38:22 (heure locale).**

### 41.1 Décision : la mise en main des soldats créés est abandonnée

Le joueur l'a tranché lui-même après le test de la V141 : « je pense que cette
méthode ne va pas marcher, de créer soldats, je pense c'est parce que le jeu
impose ça ». Son intuition est exacte. Un acteur créé par le trainer n'a pas de
fiche, et `MissionLoad` — la seule étape qui en remplit une — ne s'exécute qu'au
chargement d'une mission, depuis un `C_chunk` du fichier de mission. Huit
tentatives, dont la V141 par `TableUpdate`, ont échoué.

Ce qui reste acquis sur les soldats créés est conservé : création du nombre
exact, placement, santé, noms, mortalité, ordres par la carte, arme dans le sac.

### 41.2 Pourquoi le ralliement est d'une autre nature

Un ennemi de la mission a été fabriqué **par le jeu** : fiche complète, arme en
main, posture, animations, IA. Le faire changer de camp ne construit rien — cela
écrit un entier.

`TAB_I_ENM_GROUP` (propriété 74) est lu par les deux fonctions d'hostilité :

```cpp
// C_player::IsEnemy(asker)                    Actors.cpp:12708
case ACTOR_ENEMY:
   return (asker->GetTable(0)->ItemI(TAB_I_ENM_GROUP) == 0);

// C_enemy::IsEnemy(asker)                     Actors.cpp:15045
int my_group = tab->ItemI(TAB_I_ENM_GROUP);
case ACTOR_PLAYER:  return (my_group == 0);
case ACTOR_ENEMY:   return (my_group != his_group);
```

Écrire `1` (russe) le rend non hostile au joueur **et** hostile aux Allemands, des
deux côtés simultanément.

Et l'IA suit : `C_human::WatchHumans` (`Actors.cpp:3937`) construit sa liste de
surveillance avec `if(!a->IsEnemy(t.actor)) return true;`. Le rallié cesse donc de
chercher le joueur et se met à chercher les Allemands.

Le groupe 2 (civil) est écarté : le code le déclare « friend of everyone », il ne
combattrait pour personne.

### 41.3 Ce qui a été livré

| Demande du joueur | État |
|---|---|
| compter les ennemis quand la mission est chargée | fait, à chaque image, par le **type** d'acteur |
| afficher le total dans la fenêtre G | ligne `ENNEMIS  N dans la mission, M de votre côté` |
| choisir exactement combien rallier | ligne `RALLIER`, nombre tapé au clavier, borné au nombre d'ennemis encore hostiles |
| leur donner des armes par la fenêtre J | fait — et **ils la sortent** |
| les contrôler | ordres par la carte, en groupe ou un par un |

Ajouts nécessaires non demandés explicitement mais sans lesquels la
fonctionnalité serait dangereuse :

- **`LIBERER`** : rendre les ralliés au camp allemand (annulation).
- **Radar** : un rallié passe en allié, sinon il resterait rouge et les aides à la
  visée le prendraient pour cible — le joueur tirerait sur ses propres hommes.

### 41.4 Les armes : deux troupes, deux traitements

La sortie d'arme (`SetSelectedInvItem`) avait été coupée en V127 puis rétablie et
recoupée en V134, parce qu'elle tuait le jeu sur les soldats créés. Elle revient
**uniquement pour les ralliés**, et la distinction porte exactement sur la cause
établie :

```
soldat créé   -> AddItem + Reload, pas de SetSelectedInvItem
                 (pas de fiche : l'état « je tiens une arme » est intenable)
ennemi rallié -> AddItem + Reload + SetSelectedInvItem
                 (acteur complet : opération ordinaire du moteur)
```

`GiveWeaponOnGameThread` prend désormais un paramètre `force_equip`, et l'appelant
sépare les cibles en deux listes avant de distribuer.

### 41.5 Le commandement, et sa limite

Les ordres passent par `AddProgram(0, PRG_MOVE, ...)` — vtable `+0x24`, déclarée
sur `C_actor` (`H&D.h:897`). C'est le mécanisme **natif** des ennemis : c'est ainsi
que la mission leur dicte leurs rondes. Il s'applique donc à eux sans détournement.

**Limite établie et annoncée au joueur :** piloter un rallié au clavier est
impossible. `SetActive` n'est surchargée que par `C_player` (`Actors.cpp:12730`) ;
`C_enemy` hérite de `C_actor::SetActive`, qui est vide (`H&D.h:944`). L'appeler sur
un ennemi ne ferait rien. Changer le type d'acteur ou la vtable a été écarté : la
disposition mémoire des deux classes diffère, et le moteur lirait des champs de
`C_player` dans un objet `C_enemy`.

### 41.6 Sûretés

- Le groupe écrit est **relu** avant d'être compté comme acquis ; le journal donne
  le nombre réel, pas le nombre demandé.
- Un rallié dont le type d'acteur n'est plus `ACTOR_ENEMY` est retiré de la liste à
  l'image suivante — même règle que `PruneCreatedSoldiers`, qui avait déjà évité un
  plantage sur pointeur mort.
- `ResolveActorTableInteger` refuse une propriété absente (`offset == -1`) : un
  ennemi dont la fiche ne porterait pas la propriété 74 est simplement sauté.
- Le comptage des ennemis se fait sur le **type** de l'acteur et non sur la couleur
  du radar, sinon le total baisserait à chaque ralliement.

### 41.7 À vérifier en réseau

Le trainer écrit dans la mémoire de l'hôte. Le jeu des amis lit sa propre copie de
la fiche : leur affichage pourrait continuer à montrer le rallié comme ennemi.
L'IA des ennemis étant pilotée par l'hôte, son **comportement** devrait suivre. À
tester à deux.


## Chapitre 42 — le plus proche d'abord, et « venir à moi »

**Écrit le 5 septembre 2026 à 12:45:10 (heure locale).**

### 42.1 La question du joueur a révélé un défaut

« Quand je sélectionne 1, est-ce qu'il se place à côté de moi ? »

**Non.** Le ralliement n'écrit qu'un entier : le camp. L'acteur ne se déplace pas,
il continue son programme, mais son `IsEnemy` a changé de réponse.

La question a mis au jour un défaut réel de la V142 : `EnemiesStillHostile()`
rendait les ennemis dans l'ordre de la liste d'acteurs du moteur, qui n'a aucun
rapport avec la distance. Rallier un seul ennemi pouvait donc désigner quelqu'un à
l'autre bout de la carte — le joueur n'aurait rien vu et aurait conclu à un échec,
exactement le genre de fausse piste qui a déjà coûté plusieurs versions à ce
dossier.

### 42.2 Les trois corrections

1. **Classement par distance.** `CollectMissionEnemies` retient la position de
   chaque ennemi et trie la liste du plus proche du joueur au plus loin
   (`std::stable_sort` sur le carré de la distance, pas de racine dans le
   comparateur). « RALLIER 1 » désigne donc celui que le joueur a sous les yeux.

2. **La distance est affichée.** La ligne devient
   `RALLIER  1 ennemis  (le plus proche a 23 m)`. Le joueur sait avant de valider
   s'il va voir quelque chose. Le journal le répète, et rappelle qu'un rallié ne se
   déplace pas.

3. **Ligne `VENIR`.** Les ralliés se déplacent jusqu'à la position du joueur, sans
   passer par la carte. C'est le même `IssueMoveOrderOnGameThread`, avec
   `snapshot->player.position` comme destination au lieu d'un clic — donc toujours
   `AddProgram(0, PRG_MOVE, ...)`, le mécanisme natif des ennemis.

### 42.3 Récapitulatif du déplacement des ralliés

| Ligne | Effet |
|---|---|
| `VENIR` | tous les ralliés viennent à la position du joueur |
| `ALLIES` | tous les ralliés vont au prochain clic sur la carte (K) |
| `-> allie N sur la carte` | un seul rallié, au point choisi |

### 42.4 Ce qui reste inchangé

Comptage des ennemis, saisie du nombre, `LIBERER`, passage en allié sur le radar
et retrait des aides à la visée, armes par la fenêtre J avec sortie d'arme pour les
ralliés seuls, et la limite annoncée au chapitre 41 : `SetActive` étant vide pour
`C_enemy`, un rallié se commande mais ne se pilote pas.


## Chapitre 43 — la limite de trente-cinq hommes, et le compte faussé

**Écrit le 5 septembre 2026 à 12:58:16 (heure locale).**

### 43.1 Ce que le journal du joueur établit

Le ralliement fonctionne. Trois écritures, trois relectures concordantes :

```
1 ennemi(s) demande(s),   1 passe(s) de votre cote  (groupe 1 ecrit et RELU)
25 ennemi(s) demande(s), 25 passe(s) de votre cote
40 ennemi(s) demande(s), 40 passe(s) de votre cote
Ordre de deplacement: 1 soldat(s) ... execute=1
Ordre de deplacement: 26 soldat(s) ... execute=1
```

67 ennemis comptés dans la mission, aucun plantage. Le mécanisme du chapitre 41
est validé sur le binaire du joueur.

### 43.2 Bug 1 — l'ordre de déplacement plafonnait à trente-cinq hommes

```
Ralliement: 26 allie(s) rappele(s) a votre position resultat=1
Ralliement: 66 allie(s) rappele(s) a votre position resultat=0
```

`IssueMoveOrderOnGameThread` écrivait son code **déroulé**, un bloc complet par
soldat :

```
6A 01        push net_send        2
68 <dw>      push &prg_add        5
6A <item>    push PRG_MOVE        2
6A 00        push pos             2
B9 <dw>      mov ecx,soldat       5
8B 01        mov eax,[ecx]        2
FF 50 24     call [eax+24]        3
                                 ---
                                  21 octets par homme
```

Le garde `if (code.size() >= kOrderDestinationOffset)` refusait tout code
atteignant `0x300` = 768 octets. Avec 19 octets d'entête et de queue, le plafond
tombait à **35 hommes**. En dessous l'ordre partait, au-dessus rien, et le seul
symptôme était `resultat=0`.

**Correction.** Le code devient une boucle sur un tableau d'acteurs, comme les
autres appels du trainer : `mov ebx,[esi*4+acteurs]`, appel, `inc esi`, boucle. La
taille du stub ne dépend plus du nombre d'hommes.

| | avant | après |
|---|---|---|
| taille du code | 21 octets × N | **74 octets, constants** |
| plafond | 35 hommes | **768** (place du tableau) |

Les registres `esi` et `ebx` traversent l'appel : la convention MSVC les confie à
l'appelée. L'émission a été rejouée hors du trainer — les quatre sauts tombent sur
leurs cibles, le retour atterrit sur `0049FC15`.

### 43.3 Bug 2 — « 66 sur 63 ennemis »

```
Total de votre cote : 66 sur 63 ennemis de la mission.
```

La purge de la V142 ne retirait un rallié que si son **type d'acteur** n'était plus
`ACTOR_ENEMY`. Or un ennemi détruit garde son type jusqu'à ce que sa mémoire serve
à autre chose : les disparus restaient comptés.

Conséquence en cascade, visible dans le journal : trois disparus comptés comme
ralliés retiraient trois éléments de trop de `EnemiesStillHostile()`, qui en
rendait donc trois de plus. Le joueur a rallié **40** hommes là où **37** restaient
réellement hostiles — et le total a dépassé l'effectif.

**Correction.** Le critère devient la **présence dans la liste d'acteurs du jeu**,
relevée à la même image. Un rallié qui n'y figure plus a quitté la partie, quelle
qu'en soit la raison. C'est le même principe que `PruneCreatedSoldiers`, appliqué
avec le bon signal.

### 43.4 Ce que le journal ne dit pas encore

Aucune ligne `Fenetre armes` : la distribution d'armes aux ralliés n'a pas été
essayée. Et la session se termine par
`F10 TARGET: remembered controlled player died` — le joueur est mort, sans que le
journal dise de quelle main. Ce sont les deux points du prochain essai.


## Chapitre 44 — la carte pour commander, et le miroir d'arme pour les ralliés

**Écrit le 5 septembre 2026 à 13:11:09 (heure locale).**

### 44.1 Le ralliement est validé par le joueur

« OUI ça marche parfaitement » — les ralliés se battent de son côté, VENIR les
rappelle, la fenêtre J leur donne une arme et ils la sortent. Le chemin choisi au
chapitre 41 est confirmé en jeu.

Restaient deux manques.

### 44.2 « Je ne peux pas l'envoyer sur carte dans une position »

La cause tenait en une ligne de son journal :

```
[TEST TELEPORT #1] LBUTTON_EDGE enabled=0 map_open_cached=0 accepted=0
```

**Toute** la machinerie de la carte — ouverture par K, armement, lecture du clic,
et jusqu'aux journaux d'état — était conditionnée par `settings.teleport_map_enabled`,
la case « téléportation par la carte ». Un joueur qui ne veut pas se téléporter n'a
aucune raison de la cocher, et il perdait du même coup le seul moyen d'envoyer ses
hommes à un endroit précis.

Or les deux fonctions sont indépendantes : `ApplyPendingTeleport` traite déjà
l'ordre **en priorité** sur la téléportation.

```cpp
if (!g_soldier_order_targets.empty()) { /* ordre */ ClearSoldierOrder(); ... return; }
```

**Correction.** Un drapeau `map_usable = teleport_map_enabled || map_serves_orders`
remplace la case dans les cinq points de contrôle (réarmement, K, `teleport_map_open`,
journal d'état, prise du clic). Dès qu'une troupe est armée pour un ordre, la carte
répond, case cochée ou non. `g_soldier_order_targets` n'étant vidé qu'à la
consommation de l'ordre ou sur sol invalide, l'armement survit jusqu'au clic.

Un repère est écrit une fois par armement :

```
Ordre par la carte: N homme(s) armes. La carte repond maintenant a K et au clic
MEME si la case de teleportation est decochee (elle vaut 0).
```

### 44.3 « Ils ne portent pas la même arme que moi »

Le miroir d'arme ne visait que `g_spawned_soldiers` :

```cpp
if (!g_weapon_mirror_enabled || g_spawned_soldiers.empty() || ...
GiveWeaponOnGameThread(process, g_spawned_soldiers, ..., false);
```

Les ralliés n'y figuraient pas — rien ne pouvait leur arriver.

**Correction.** Le miroir couvre les deux troupes, avec la même distinction que la
fenêtre J, fondée sur la cause établie au chapitre 41 :

| troupe | remise | sortie de l'arme |
|---|---|---|
| soldats créés | oui | non (pas de fiche) |
| ennemis ralliés | oui | **oui** |

Les deux distributions sont indépendantes : l'une peut aboutir sans l'autre, et le
miroir retient l'objet dès qu'au moins une a réussi.

### 44.4 Comment commander un rallié, en résumé

| Ligne de la fenêtre G | Effet |
|---|---|
| `VENIR` | tous les ralliés viennent au joueur, immédiatement |
| `ALLIES` | puis K, puis clic sur la carte — tous y vont |
| `-> envoyer l'allie N` | puis K, puis clic — un seul y va |


## Chapitre 45 — l'arrêt du jeu à la distribution d'arme

**Écrit le 5 septembre 2026 à 13:28:09 (heure locale).**

### 45.1 L'ordre par la carte est validé

« parfait pour K qui les envoie dans une position ». Le découplage du chapitre 44
est confirmé en jeu : la carte répond aux ordres sans la case de téléportation.

### 45.2 Le journal date l'arrêt à la milliseconde

```
13:21:12.981  Miroir d'arme: objet 14 ... chez 25 soldat(s) (25 l'ont deja en main)
13:21:12.981  Fenetre armes: 25 allie(s) rallie(s) recoivent l'arme 14 ET la sortent ... resultat=1
13:21:12.981  Fenetre armes: arme 2 (objet 14) donnee a 25 allie(s) rallie(s), resultat=1
13:21:15.029  Process: pid=0
```

Trois faits, tous dans la même milliseconde :

1. **Le miroir et la fenêtre J ont distribué tous les deux**, sur les mêmes 25
   acteurs, dans la même image — chacun posant son propre détour sur le site de
   triche, coup sur coup. C'est la V145 qui a rendu ce cumul possible en étendant
   le miroir aux ralliés (chapitre 44.3) sans le sérialiser avec la fenêtre J.
2. **Les 25 tenaient déjà cette arme.** Le journal l'écrit lui-même
   (« 25 l'ont deja en main ») et l'inventaire relevé le confirme :
   `selectionne=4` désigne bien l'objet 14.
3. Le travail était donc **entièrement inutile**.

Et ce travail n'est pas une écriture. `C_human::SetGun` (`Actors.cpp:6318`) libère
le modèle courant, en crée un neuf par `driver->CreateModel()`, le charge par
`model_cache.Open(...)` et l'accroche à la main. **Cinquante chargements de modèle
depuis un détour, dans une seule image.** Le jeu a tenu deux secondes.

### 45.3 Les trois corrections

**Une seule file.** `QueueEquipRequest` / `ProcessPendingEquip` : le miroir et la
fenêtre J alimentent la même file, et une demande nouvelle remplace la précédente.
Deux distributions ne peuvent plus se chevaucher.

**Par petits paquets.** `kEquipBatchSize = 4`, `kEquipBatchRestMs = 250`. Au plus
un paquet par image, avec un repos entre deux. La file est traitée dans
`UpdateGameplayMods`, juste après `ProcessPendingRemoteReleases`.

**Saut des hommes déjà armés.** Avant chaque paquet, `ReadHeldWeapon` relit l'objet
tenu ; s'il vaut celui qu'on demande, l'acteur est retiré sans rien faire. Dans le
cas du journal ci-dessus, cela aurait supprimé **la totalité** du travail.

### 45.4 Ce que le journal dira

```
Sortie d'arme: 25 homme(s) mis en file pour l'objet 14, par paquets de 4
avec 250 ms de repos. Ceux qui la tiennent deja seront sautes.
Sortie d'arme: paquet de 4 homme(s) pour l'objet 14 resultat=1 (21 restant(s)).
Sortie d'arme: termine pour l'objet 14 - 8 l'ont sortie, 17 la tenaient deja
(sautes), 0 n'ont pas repondu.
```

La distribution devient progressive au lieu d'être instantanée. C'est le
comportement voulu : c'est précisément ce qui évite l'arrêt.

### 45.5 Leçon à retenir pour ce dossier

Deux mécanismes qui écrivent au même endroit du jeu doivent partager une file, pas
s'ignorer. La V145 a étendu le miroir aux ralliés sans se demander ce qui se
passerait si la fenêtre J agissait dans la même image — et la réponse était : deux
détours au même endroit, et cinquante chargements de modèle pour rien.


## Chapitre 46 — des pointeurs vers des acteurs morts au combat

**Écrit le 5 septembre 2026 à 13:34:42 (heure locale).**

### 46.1 L'hypothèse du joueur, tranchée

Le joueur propose : « je pense que c'est car le nombre de munitions est très grand
par rapport à la limite ». **Écartée, de deux façons.**

D'abord le moteur (`Inventory.cpp:360`) :

```cpp
int C_inventory::AddItem(int itm, dword num){
   bool b = tab_inventory->ItemB(TAB_B_INV_MULTIPLE, itm);
   if(b){
      int i=NumItems();
      while(i--) if(items[i]->itm == itm){ items[i]->amount += num; return i; }
   }
   C_inventory::S_item *it = new C_inventory::S_item(itm, num);
   items.push_back(it);
   ...
```

`num` est rangé dans un champ entier — l'appel n'alloue **jamais** `num` objets.
Donner 30 ou 978 coûte exactement le même travail.

Ensuite son propre journal :

```
13:29:35.980  paquet de 4 homme(s) pour l'objet 101 resultat=1   <- réussi
13:29:37.534  paquet de 4 homme(s) pour l'objet 101 resultat=0   <- échoué
```

Le premier paquet a réussi avec **les mêmes 978 munitions et le même objet 101**.
Une cause qui n'agit qu'une fois sur deux n'est pas la quantité.

### 46.2 Ce que `resultat=0` signifie réellement

Ce n'est pas « refusé ». Le drapeau de fin est écrit par la **dernière** instruction
du code injecté ; `execute=0` veut donc dire que le jeu est entré dans ce code et
n'en est jamais ressorti. Seize millisecondes plus tard, le processus a disparu.

### 46.3 La cause : la liste des ralliés n'était nettoyée qu'avec la fenêtre G ouverte

```
13:28:42  cinquante ennemis ralliés
13:29:01  envoyés au combat par la carte
13:29:08  fenêtre G ouverte      <- dernier nettoyage
13:29:35  distribution d'arme    <- vingt-sept secondes plus tard
```

`CollectMissionEnemies` — la seule fonction qui retirait de `g_rallied_enemies` les
disparus — est appelée ligne 19242, **après** la garde de la ligne 19126 qui rend la
main dès que `soldier_menu_open` est faux. Fenêtre fermée, aucun nettoyage.

Pendant ces vingt-sept secondes, cinquante hommes se battaient. Certains sont morts,
le moteur a libéré leur mémoire, et le trainer gardait leurs adresses. Le premier
paquet est tombé sur quatre vivants ; le second sur un mort, et `AddItem` a écrit
dans une mémoire libérée.

C'est le même accident que celui de la V131 sur les soldats créés — pointeurs
conservés au-delà de la vie de l'acteur — appliqué cette fois aux ralliés, et rendu
possible par la file de la V146 qui étale la distribution sur plusieurs secondes.

### 46.4 Les trois corrections

**Nettoyage permanent.** `PruneRalliedEnemies` tourne dans `UpdateGameplayMods`,
donc à chaque image, fenêtre ouverte ou non (throttlé à 250 ms).

**Vérification juste avant de toucher.** `IsRalliedActorUsable` contrôle trois
choses, la première étant la plus sûre :

| contrôle | pourquoi |
|---|---|
| la vtable pointe dans le module du jeu | une mémoire libérée ne la présente presque jamais |
| `type == ACTOR_ENEMY` | l'objet est encore un ennemi |
| `resistance > 0` | et il est vivant — un mort n'a que faire d'une arme |

Elle est appliquée **au moment où l'homme entre dans le paquet**, jamais sur la foi
de sa mise en file.

**Les ordres par la carte aussi.** Même piège : l'ordre est armé dans la fenêtre G
et consommé au clic, parfois plusieurs secondes plus tard.
`IssueMoveOrderOnGameThread` filtre désormais avec `IsOrderableActor` (joueur *ou*
ennemi, vivant), et journalise les écartés.

### 46.5 Ce que le journal dira

```
Rallies: 3 homme(s) retire(s) de la liste (morts ou disparus); il en reste 47.
Sortie d'arme: termine pour l'objet 101 - 12 l'ont sortie, 5 la tenaient deja
(sautes), 3 sont morts ou ont disparu pendant l'attente, 0 n'ont pas repondu.
Ordre de deplacement: 2 homme(s) ecarte(s) - morts ou disparus depuis que
l'ordre a ete arme.
```

### 46.6 Leçon

Tout pointeur d'acteur conservé plus d'une image doit être revalidé avant usage —
et la revalidation doit tourner indépendamment de l'interface qui l'a produite.
Lier le nettoyage à l'ouverture d'une fenêtre revenait à ne nettoyer que lorsque le
joueur regardait.


## Chapitre 47 — deux diagnostics faux, et une mesure pour trancher

**Écrit le 5 septembre 2026 à 13:44:07 (heure locale).**

### 47.1 Le chapitre 46 était faux, et le chapitre 45 aussi

Il faut l'écrire clairement. Deux causes ont été avancées pour l'arrêt du jeu à la
distribution d'arme :

- **chapitre 45** : le miroir et la fenêtre J distribuent dans la même image.
  Corrigé par la file unique de la V146 → le jeu a planté à l'identique.
- **chapitre 46** : le trainer écrit dans des acteurs morts au combat. Corrigé par
  la revalidation de la V147 → le jeu a planté à l'identique, **et le journal ne
  porte aucune ligne « morts ou disparus »** : la vérification n'a rejeté personne.

Les deux raisonnements étaient plausibles et tous deux ont été démentis par
l'essai. C'est la cinquième fois dans ce dossier qu'une déduction est plus coûteuse
qu'une mesure.

### 47.2 Ce que les deux journaux établissent réellement

```
13:29:35.980  paquet 1 ... resultat=1        13:38:39.774  paquet 1 ... resultat=1
13:29:37.534  paquet 2 ... resultat=0        13:38:41.374  paquet 2 ... resultat=0
13:29:37.550  Process: pid=0                 13:38:41.413  Process: pid=0
```

Trois faits, et rien de plus :

1. `resultat=0` n'est pas un refus. Le drapeau de fin est écrit par la **dernière**
   instruction du code injecté ; sa valeur nulle signifie que le jeu est entré dans
   ce code et n'en est jamais ressorti.
2. **Jamais le premier passage. Toujours le second.**
3. Ni les mêmes hommes, ni le même effectif (25 puis 20) — mais toujours le second
   passage.

### 47.3 Deux témoins dans le code injecté

Le code écrit désormais, avant chaque appel, deux entiers dans sa page :

| offset | contenu |
|---|---|
| `+0x21C` | rang de l'homme en cours (`mov [temoin], esi`) |
| `+0x220` | étape : 1 = `AddItem`, 2 = `Reload`, 3 = `SetSelectedInvItem`, 4 = terminé |

Le trainer relit ces deux valeurs toutes les 2 ms pendant toute l'attente et
conserve les dernières lues. Si le jeu meurt, le journal donne l'homme et l'appel
exacts :

```
TEMOIN: le code pose n'a pas rendu la main. Dernier point vu : homme n1 sur 1
(adresse 025BAB10), etape 3 = SetSelectedInvItem (la SORTIR).
```

**Vérification du codage.** L'ajout de 36 octets de témoins allonge le corps de
boucle ; les sauts courts `je suivant` et `js suivant` ont été recalculés hors du
trainer : 96 octets au maximum, sous la limite de 127. Un dépassement aurait été
silencieusement tronqué par le cast en `uint8_t` — et aurait produit exactement le
genre de plantage qu'on cherche.

### 47.4 Trois sécurités

- **Un homme par passage** (`kEquipBatchSize = 1`) : le témoin désigne un acteur
  unique.
- **400 ms de repos** entre deux passages.
- **Arrêt de la file au premier échec.** Jusqu'ici le trainer enchaînait, c'est-à-
  dire qu'il continuait à écrire dans un jeu peut-être déjà mourant.

### 47.5 Ce qui reste stable

Ralliement, combats, `VENIR`, ordres par la carte, `LIBERER`, affichage radar : rien
de tout cela n'est touché. Seule la sortie d'arme sur les ralliés est en cause.


## Chapitre 48 — écriture sur du code en cours d'exécution

**Écrit le 5 septembre 2026 à 13:57:25 (heure locale).**

### 48.1 Le témoin a tranché

```
TEMOIN: le code pose n'a pas rendu la main. Dernier point vu : homme n1 sur 1
(adresse 0247BA80), etape 0 = aucune - le jeu n'est jamais entre dans le code.
Temoin lu : non - jamais rien lu.
```

Ni `AddItem`, ni `Reload`, ni `SetSelectedInvItem`. **Le code injecté n'a jamais
exécuté une seule instruction.** Le jeu meurt *avant*, pendant l'installation du
saut.

Trois versions de suite avaient cherché la cause dans le contenu du code injecté.
Elle était dans la **façon de l'installer**.

### 48.2 La cause

Pour faire exécuter quelque chose par le jeu, le trainer remplace cinq octets au
site de triche (`kProcessCheatHookRva`) par un `jmp`. Or ce site est traversé **à
chaque image** — c'est établi depuis la V107, qui a dû y poser un garde de
ré-entrée précisément parce que la boucle repassait sans arrêt.

Écrire cinq octets pendant que le processeur les exécute est une **course**. Elle
est gagnée la plupart du temps ; quand elle est perdue, le jeu exécute une
instruction à moitié réécrite et s'arrête.

Cela explique tout ce qui résistait :

| observation | explication |
|---|---|
| 1er passage réussi, 2e fatal, toujours | hasard ; chaque passage est une chance de plus de perdre |
| V145 : miroir + fenêtre J dans la même image | deux installations coup sur coup, deux chances de perdre |
| V146 et V147 sans effet | elles corrigeaient le contenu, pas l'installation |
| anciens plantages sur soldats créés | même cause, jamais expliquée |

### 48.3 La correction

Nouvelle primitive `TrainerProcess::PatchCodeSafely` :

1. suspend **tous** les threads du processus du jeu et les garde suspendus ;
2. lit le `Eip` de chacun et refuse si l'un se trouve dans la zone visée — ou dans
   les **15 octets qui la précèdent**, marge suffisante pour couvrir une
   instruction dont l'exécution amènerait dans la zone ;
3. écrit ;
4. relance tout le monde, quel que soit le résultat.

Deux enveloppes dans `gameplay_mods.cpp` :

- `InstallHookSafely` — jusqu'à 24 tentatives espacées de 4 ms, puis abandon **sans
  rien écrire**, avec journal. Une opération manquée vaut mieux qu'un arrêt.
- `RestoreHookSafely` — jusqu'à 250 tentatives (≈1 s), puis écriture inconditionnelle
  en dernier recours. Laisser le saut en place serait un arrêt **certain** à
  l'image suivante, là où la course n'est qu'un risque.

Appliqué aux **12** installations et aux **19** remises en état du fichier : cela
couvre la création de soldats, les ordres, la téléportation, le véhicule, les
armes — tout ce qui passe par le site de triche.

### 48.4 Les soldats créés quittent les fenêtres d'armes

Demande explicite du joueur : « pourquoi dans la fenêtre J tu combines les soldats
créés avec les ralliés, supprime totalement les soldats créés ».

Fondé : un soldat créé n'a pas de fiche, et huit versions ont établi qu'il ne peut
pas soutenir l'état « je tiens une arme ». Les mélanger entretenait la confusion
dans la fenêtre et dans le journal.

- `UpdateWeaponMenu` et son action ne visent plus que `g_rallied_enemies`.
- Le miroir d'arme non plus.
- Le chemin des soldats créés (recopie de fiche, `GiveWeaponOnGameThread` sans
  équipement) est conservé sous `#if 0` plutôt que supprimé : il documente huit
  versions de travail et pourrait resservir si l'initialisation d'acteur devenait
  possible.

Les soldats créés gardent création, noms, santé, mortalité, contrôle, ordres et
suppression.

### 48.5 Ce que ce dossier doit en retenir

Trois versions ont été dépensées à supposer une cause. La quatrième a posé un
témoin, et la réponse est venue au premier essai — en désignant un endroit qu'aucune
des suppositions n'aurait atteint. **Le témoin aurait dû être posé d'abord.**


## Chapitre 49 — la sortie d'arme est coupée, et le bilan de l'enquête

**Écrit le 5 septembre 2026 à 14:06:49 (heure locale).**

### 49.1 Ce que la V149 a appris malgré son échec

```
14:02:47.579  homme 025C7240 ... resultat=1
14:02:49.262  TEMOIN: etape 0 = jamais entre dans le code
14:02:54.093  Site de triche: la remise en etat n'a pas pu se faire threads
              suspendus apres une seconde. On ecrit quand meme.
14:02:54.144  Process: pid=0
```

La troisième ligne est nouvelle et informative : `PatchCodeSafely` a refusé
d'écrire pendant **cinq secondes**. Dans son implantation, un `OpenThread` ou un
`SuspendThread` en échec interrompt la boucle et fait rendre faux — ce qui arrive
quand des threads du processus disparaissent. **Le jeu était donc déjà en train de
mourir avant la remise en état.** La suspension n'a pas empêché la mort ; elle l'a
rendue visible.

### 49.2 Bilan : ce qui est éliminé, preuve à l'appui

| hypothèse | version | verdict |
|---|---|---|
| quantité de munitions trop grande | — | **fausse** : `AddItem` range `num` dans un entier (`Inventory.cpp:360`), et le 1er passage réussit avec la même valeur |
| deux distributions dans la même image | V146 | **fausse** : corrigée, plantage identique |
| pointeurs vers des acteurs morts | V147 | **fausse** : corrigée, plantage identique, et **aucun** homme rejeté |
| `AddItem` / `Reload` / `SetSelectedInvItem` | V148 | **éliminés** : le témoin rapporte `etape 0`, le code injecté n'a jamais tourné |
| course sur le site de triche | V149 | **fausse** : threads suspendus, plantage identique |

**La cause reste inconnue.** Cinq versions ont été dépensées ; la sixième ne sera
pas une sixième supposition.

### 49.3 Décision : couper la désignation, garder la remise

- **Coupé** : `SetSelectedInvItem` — le geste qui met l'arme dans la main.
- **Conservé** : `AddItem` + `Reload` — l'arme entre dans le sac, chargée.

Ce chemin est le seul qui n'ait jamais arrêté le jeu, sur les soldats créés comme
sur les ralliés, et il avait été validé par le joueur plusieurs versions plus tôt.
La perte est faible pour un rallié : il porte déjà sa propre arme et s'en sert.

`UpdateWeaponMenu` et le miroir appellent désormais `GiveWeaponOnGameThread(...,
force_equip = false)` directement, sans passer par la file — la file n'avait de
raison d'être que pour étaler des chargements de modèle qui n'ont plus lieu.

### 49.4 La porte laissée ouverte, avec une mesure qui tranchera

La dernière ligne de la fenêtre J reste un **essai volontaire** de sortie d'arme,
passant par la file, un homme à la fois. Le témoin comble le trou de la V148 :

```
TEMOIN: ... Lectures reussies=412, echouees=88, premiere echouee au essai 412
(soit 824 ms apres le declenchement). Le processus a cesse de repondre PENDANT
l'attente : le jeu est mort a ce moment-la, pas plus tard.
```

La V148 confondait « le témoin vaut zéro » et « la lecture a échoué » — deux
situations opposées : dans l'une le jeu tourne et n'entre pas dans le code, dans
l'autre il est déjà mort. Le compteur de lectures et l'instant du premier échec
lèvent l'ambiguïté et daterait la mort à la milliseconde.

### 49.5 Méthode

Quatre diagnostics faux d'affilée sur le même symptôme. Le seul progrès réel est
venu d'un témoin, pas d'un raisonnement — et chaque correction fondée sur une
déduction a coûté une partie au joueur. Quand une cause résiste à deux essais, il
faut instrumenter, et livrer entre-temps un état qui ne casse rien.


## Chapitre 50 — un argument manquant, depuis huit versions

**Écrit le 5 septembre 2026 à 14:24:39 (heure locale).**

### 50.1 La cause

```cpp
void SetSelectedInvItem(int indx, bool net_send = true)
```

Deux paramètres. La valeur par défaut est posée **par l'appelant** en C++ ; dans le
binaire, la fonction en attend bel et bien deux. Son désassemblage sur le binaire du
joueur :

```
004076E0  8b 44 24 04   mov eax,[esp+4]      ; indx
          56            push esi
          8b f1         mov esi,ecx          ; this
          39 86 58 02   cmp [esi+0x258],eax  ; selected_inv_item == indx ?
          ...
+0x1AD    c2 08 00      ret 8                ; DÉPILE HUIT OCTETS
```

Le stub du trainer n'en empilait **qu'un** :

```
push index          ; 4 octets
mov ecx, acteur
call [SetSelectedInvItem]   ; la fonction dépile 8
```

Chaque appel laissait donc `esp` **quatre octets trop haut**, en plein dans les
registres sauvés par le `pushad` d'entrée. Au `popad`, le jeu restaurait des valeurs
décalées d'un cran et repartait vers une adresse arbitraire.

### 50.2 Ce que cela explique

| observation | explication |
|---|---|
| « parfois ça marche, parfois ça tue » | hasard pur : ce que les registres ramassent |
| 1er passage survit, 2e fatal | même hasard, deux tirages |
| témoin V148 : « jamais entré dans le code » | le processus était déjà détruit à la relecture |
| V149 sans effet | la course sur le site de triche n'était pas en cause |
| **les huit échecs sur les soldats créés** | **la même cause** |

Ce dernier point mérite d'être écrit sans détour : pendant huit versions, l'échec de
la mise en main sur les soldats créés a été attribué à leur fiche manquante
(chapitres 36, 40, 41). **Il venait de l'appel.** La fiche n'a peut-être jamais été
le problème, et cela vaudra la peine de reprendre l'essai sur eux une fois cette
version validée.

### 50.3 La correction

Les deux sites d'appel — `GiveWeaponOnGameThread` et `EquipLastItemOnGameThread` —
empilent désormais les deux arguments :

```
push 0              ; net_send = false
push index
mov ecx, acteur
call [SetSelectedInvItem]
```

`net_send = false` est délibéré : à vrai, le moteur émettrait un message réseau
construit sur la valeur ramassée au hasard — un problème de plus chez les amis du
joueur.

Distances des sauts courts revérifiées après l'ajout : 100 octets de corps de
boucle, sous la limite de 127.

### 50.4 Audit des autres appels natifs

| fonction | attend | le trainer empile | |
|---|---|---|---|
| `AddItem(int, dword)` | 2 | 2 | OK |
| `Reload(int, bool)` | 2 | 2 | OK |
| `AddProgram(int, E_PRG_ITEM, const S_prg_add&, bool)` | 4 | 4 | OK |
| `SetActive(bool, bool)` | 2 | 2 | OK |
| `TableUpdate(int, bool)` | 2 | 2 | OK |
| `SetSelectedInvItem(int, bool)` | 2 | **1** | **faux, corrigé** |

### 50.5 La leçon, et elle est sévère

Cinq versions à supposer une cause, cinq démentis. Ce qui a fini par donner la
réponse n'est ni un raisonnement ni un témoin : c'est d'avoir **désassemblé la
fonction appelée pour compter ses arguments**.

Ce dossier applique depuis le début la règle « on ne pose une valeur qu'après
l'avoir lue dans une instruction du jeu » — pour les offsets, les vtables, les
prologues. Elle n'avait jamais été appliquée aux **conventions d'appel**. Elle l'est
désormais : tout appel natif ajouté au trainer doit voir son `ret N` vérifié dans le
binaire avant d'être écrit.


## Chapitre 51 — l'ordre s'insérait au lieu de remplacer

**Écrit le 5 septembre 2026 à 14:41:34 (heure locale).**

### 51.1 Le constat du joueur

« quand je clique sur VENIR il ne s'arrête pas à cette place, il reste bouger ».

### 51.2 La cause

`IssueMoveOrderOnGameThread` appelait `AddProgram(0, PRG_MOVE, ...)`. Le premier
argument est une **position d'insertion**, pas un remplacement : l'ordre était placé
en tête d'une liste qui contenait déjà la patrouille de mission de l'ennemi. Il
venait donc jusqu'au joueur, puis reprenait sa ronde là où il l'avait laissée.

### 51.3 Ce que fait le moteur

Le jeu vide la liste avant de poser un ordre neuf (`Actors.cpp:14293`) :

```cpp
if(tc.p_ctrl->Get(GKEY_CMD_PRG_CLR)){
   tc.p_ctrl->IgnoreValue(GKEY_CMD_PRG_CLR);
   if(program.size()){
      ClrProgram(true);
      cbProc(CB_PRG_SHOW, false, 1);
   }
}
```

`ClrProgram` n'est pas virtuelle, mais `DelProgram(int pos, bool net_send)` l'est —
déclarée juste après `AddProgram` (`H&D.h:897-898`). Le stub retire donc l'ordre de
tête douze fois avant d'ajouter le sien. `PRG_READY` étant automatique en fin de
liste, l'homme arrivé n'a plus rien à faire : il reste sur place.

### 51.4 Vérification dans le binaire, pas par déduction

La leçon du chapitre 50 est appliquée immédiatement :

```
vtable +0x24 -> 00429480   ret 16  (4 arguments)   AddProgram
vtable +0x28 -> 004294C0   ret 8   (2 arguments)   DelProgram
```

Les trois tables de méthodes des humains donnent le même couple. Le stub pousse
bien deux arguments à `DelProgram`.

Encodage rejoué hors du trainer : boucle de vidage `jnz -14` correcte, corps de
boucle à 39 octets (limite 127).

### 51.5 Portée

La correction vaut pour **tous** les ordres — ralliés comme soldats du joueur,
`VENIR` comme clic sur la carte. Vider la liste d'ordres ne touche pas à l'instinct
de combat : `WatchHumans` et l'attaque ne passent pas par le programme.


## Chapitre 52 — main libre : piloter un allié comme on se pilote soi-même

**Écrit le 5 septembre 2026 à 14:54:12 (heure locale).**

### 52.1 Le mode « placer un par un » est retiré

Le joueur l'a écarté explicitement : « non je ne veux pas là comme ça, restaure la
dernière version ». Les sources repartent de `_GOLD_V152`. Sa demande était autre.

### 52.2 La demande

« Ce que je veux c'est comme le noclip qui existe déjà : même si je ne contrôle pas
ce soldat, je peux le déplacer dans la map comme je veux, n'importe où. »

Donner une destination et regarder l'homme y marcher ne suffit pas. Il veut le
**prendre et le poser**, y compris là où aucun chemin ne mène.

### 52.3 Pourquoi c'est possible sans rien inventer

Le crochet du noclip n'est pas posé sur le joueur : il est posé sur `C_human::Tick`,
que **tous** les humains exécutent — soldats et ennemis. Et le trampoline compare
l'acteur en cours à celui publié dans sa page :

```
3B 0D <adresse>    cmp ecx,[acteur publié]
```

Il pilote donc l'acteur qu'on lui **désigne**. `UpdateNoclip` lui publiait simplement
`snapshot->player_object_address` en dur (`gameplay_mods.cpp:3309`).

La modification tient donc en un choix d'acteur. Toute la mécanique — vitesse,
direction prise sur la caméra du joueur, relien au secteur, arrêt propre — est celle
qui vole sous ses ordres depuis des versions.

### 52.4 Les deux lignes de la fenêtre G

```
MAIN LIBRE piloter l'allie   3    <  tapez le numero
MAIN LIBRE -> rendre le pilotage a mon soldat
```

La seconde n'apparaît que lorsqu'un allié est pris en main.

### 52.5 Sûretés

- **Revalidation à chaque image** avant de piloter : vtable dans le module, type
  `ACTOR_ENEMY` ou `ACTOR_PLAYER`, `resistance > 0`. Même règle que depuis le
  chapitre 46. S'il meurt en vol, le pilotage revient seul au joueur et le journal
  le dit.
- **Sortie de la liste des ralliés** : libération automatique, dans
  `PruneRalliedEnemies`.
- **Réseau** : le crochet de publication compare lui aussi l'acteur
  (`cmp ebx,[acteur]`, `gameplay_mods.cpp:2290`). Il ne publiera donc jamais la
  position de l'allié à la place de celle du joueur. Et un rallié étant un acteur de
  type ennemi, l'hôte synchronise déjà sa position vers les clients par le chemin
  normal du jeu — à confirmer à deux.

### 52.6 Rappel du joueur, honoré

« N'oublie pas pour le choix VENIR : quand ils viennent il faut qu'ils restent
stables, qu'ils stoppent dans cet endroit. » C'est la correction du chapitre 51,
présente dans la base restaurée : l'ordre vide la liste au lieu de s'y insérer.


## Chapitre 53 — la caméra sur l'homme piloté

**Écrit le 5 septembre 2026 à 15:15:40 (heure locale).**

### 53.1 Les trois questions du joueur

1. « Pourquoi ne pas mettre la caméra exactement sur lui ? » → **fait**.
2. « Et pour changer sa direction ? » → **pas encore**, voir 53.5.
3. « Pourquoi pas la touche switch du jeu pour passer sur eux ? » → **impossible**,
   et prouvé en 53.4.

### 53.2 Le mécanisme

`C_human::cbProc` traite `CB_SETFOCUS` (`Actors.cpp:11761`) :

```cpp
case CB_SETFOCUS:
   switch(prm1){
   case 0: mission.game_cam.SetAimModel(NULL, NULL); on = true; break;
   case 1: mission.game_cam.SetFocus(frm_head ? frm_head : frame, frame, this);
           mission.game_cam.SetDeltaDir(aim_dir);
           on = mission.game_cam.GetDistanceMode()!=0;
           break;
   default: return true;
   }
```

Le trainer appelle donc `cbProc(CB_SETFOCUS, 1, 0, 0)` sur l'allié pris en main, et
sur le soldat du joueur quand il rend le pilotage.

### 53.3 Deux pièges, tous deux évités par lecture

**Le nombre d'arguments.** L'en-tête de 2002 déclare trois paramètres, et le nom
décoré le confirme : `?cbProc@C_actor@@UAEKKKK@Z` — les trois `cbProc` du binaire
2002 finissent bien par `ret 12`. **Sur Deluxe, les trois finissent par `ret 16` :
quatre arguments.** La signature a changé entre les versions.

C'est exactement le piège du chapitre 50, tendu une seconde fois. La règle posée
alors — vérifier le `ret N` avant d'écrire un appel — l'a désamorcé.

**Le numéro du message.** `CB_SETFOCUS` vaut 21 dans l'énumération de 2002. Sur
Deluxe, la table d'aiguillage de `C_human::cbProc` couvre les messages **117 à 180**
— la numérotation est entièrement différente. Deviner aurait déclenché l'un des
soixante-quatre cas au hasard.

Le numéro a été établi en décodant la table à deux niveaux
(`mov cl,[octets+eax]` puis `jmp [sauts+ecx*4]`) :

| msg | code du cas | identification |
|---|---|---|
| 138 | `mov ecx,[esi+0x18]` ; `mov edx,[ecx+0x278]` ; `mov ecx,[esi+0x28]` ; `cmp` ; `sete al` | compare le focus caméra à la frame de l'acteur → `CB_IS_FOCUSED` |
| 137 | `mov eax,[ebp+0x0C]` ; `sub eax,0` ; `je` ; `dec eax` ; `je` ; `mov eax,1` | aiguillage sur `prm1` avec 0, 1 et défaut → `CB_SETFOCUS` |

`CB_IS_FOCUSED` suivant `CB_SETFOCUS` dans l'énumération, les deux se confirment
mutuellement. `+0x28` est bien `kActorFrameOffset`, déjà établi par le trainer.

Avant tout appel, le trainer vérifie que l'acteur présente une méthode au rang
`+0x04` et qu'elle pointe dans le module du jeu.

### 53.4 Pourquoi la touche switch ne peut pas les prendre

```cpp
int C_game_mission::PlayerSwitch(bool forward, int id){
   ...
   if(act->GetType()==ACTOR_PLAYER){       // premier mur
      plrs->push_back(act);
   }
   ...
   slist[id]->SetActive(true, show_switch_msg);   // second mur
}
```

Deux murs, pas un. Le filtre écarte les ennemis ; et même en forçant un rallié dans
la liste, `SetActive` est **vide** pour `C_enemy` (`H&D.h:944` — seul `C_player` la
surcharge, `Actors.cpp:12730`). Rien ne se passerait.

C'est la limite annoncée au chapitre 41 : on les commande, on ne les pilote pas. La
caméra est une autre affaire, et elle, elle marche.

### 53.5 Ce qui reste sur la direction

`SetDeltaDir(aim_dir)` règle la caméra **depuis** la direction de visée de l'acteur.
La changer suppose de situer `aim_dir` dans l'objet — une mesure à faire sur le
binaire du joueur, à la manière des autres. Repoussé après son essai de la caméra.


## Chapitre 54 — le numéro prouvé par son appelant, et le switch étendu

**Écrit le 5 septembre 2026 à 15:32:14 (heure locale).**

### 54.1 Pourquoi la caméra ne bougeait pas

Le journal du joueur : `Camera: ... (message 137) declenche=1 execute=1` — l'appel
partait, ne plantait pas, et ne faisait rien.

L'erreur était dans la lecture du prologue de `C_human::cbProc` :

```
8b 45 08   mov eax,[ebp+8]      = msg
8b 5d 10   mov ebx,[ebp+0x10]   = prm2
48         dec eax              <- l'index vaut msg - 1
8b 7d 0c   mov edi,[ebp+0x0C]   = prm1
83 f8 3f   cmp eax,0x3F         = 63, donc messages 1 à 64
```

Le script de décodage avait pris un `add eax,imm` situé plus loin dans la fenêtre
au lieu de ce `dec eax`, d'où un décalage de 116. Le message 137 tombait hors des
bornes, `ja default` rendait la valeur par défaut : ni effet, ni plantage.

### 54.2 Le numéro établi par preuve, non par calcul

`C_game_camera::SetFocus` est localisée sur le binaire du joueur par son prologue
exact repris du binaire 2002 — **une seule occurrence**, à `0045DEB0`. Ses cinq
appelants sont énumérés, et l'un d'eux, `0041F2B8`, tombe **à l'intérieur** du cas
que la table d'aiguillage numérote 21.

`CB_SETFOCUS = 21` — la valeur de 2002. **L'énumération n'avait pas changé ; seule
la lecture était fausse.** Le chapitre 53 affirmait le contraire ; cette correction
l'annule sur ce point. Ce qui reste vrai du chapitre 53 : `cbProc` prend bien
**quatre** arguments sur Deluxe contre trois en 2002 (`ret 16` contre `ret 12`).

Leçon de méthode : identifier un numéro de message par la *forme* d'un cas est une
déduction ; l'identifier par un **appelant connu** est une preuve. La seconde
méthode aurait dû être employée d'emblée.

### 54.3 Le switch étendu aux alliés

Demande du joueur : ne pas toucher aux touches 1 2 3 4, mais faire que la touche de
switch continue sur les ralliés après le dernier soldat, puis revienne.

**Aucune touche n'est interceptée** — ce qui garantit mécaniquement que 1 2 3 4
restent intactes. Le trainer observe seulement quel soldat est actif :

| événement observé | action |
|---|---|
| l'actif passe du **dernier** soldat au **premier** | entrée dans le tour des alliés, allié 1 |
| l'actif change encore, en mode allié | allié suivant |
| plus d'allié après le dernier | sortie, caméra rendue au soldat actif |

Le bouclage du jeu sert donc de signal. Faux positif possible : presser « 1 » depuis
le dernier soldat produit le même bouclage apparent — neutralisé en ignorant le
signal si un chiffre a été frappé dans les 400 ms.

Une ligne de la fenêtre G coupe le comportement, portée par
`settings.switch_cycle_enabled`.


## Chapitre 55 — un camp propre au joueur, et le switch trié comme le jeu

**Écrit le 5 septembre 2026 à 15:42:38 (heure locale).**

### 55.1 « Dans une autre mission ils ne tirent pas sur les autres ennemis »

Constat du joueur, et faute réelle du chapitre 41.

`C_enemy::IsEnemy` décide l'hostilité par **différence** de camp
(`Actors.cpp:15070`) :

```cpp
if(my_group==2 || his_group==2) return false;   // civil : ami de tous
return (my_group != his_group);
```

Le groupe 1 (russe) choisi au chapitre 41 fonctionne tant que les ennemis de la
mission sont allemands (groupe 0). **Dans une mission où ils sont déjà russes, les
ralliés se retrouvaient dans le même camp qu'eux** — même groupe, donc pas ennemis,
donc personne ne se bat. Le chapitre 41 avait raisonné sur une seule mission.

### 55.2 La correction : un groupe qui n'appartient à personne

`kEnemyGroupOurOwn = 3`.

| condition | conséquence |
|---|---|
| ≠ 0 | `C_player::IsEnemy` rend faux → pas nos ennemis ; `C_enemy::IsEnemy(joueur)` rend faux → ils ne nous visent pas |
| ≠ 2 | ils ne sont pas « amis de tout le monde » : ils combattent |
| ≠ 0, 1 **et** 2 | hostiles à tous les camps existants, quelle que soit la mission |
| identique entre eux | `3 != 3` faux → ils ne se tirent pas dessus |

**Sûreté vérifiée** : la carte fait `type = eg==0 ? 1 : 2` (`Map_man.cpp:2050`) — une
comparaison, pas un indice de tableau. Un groupe 3 n'y déborde rien. Aucun autre
usage de `GetEnemyGroup` n'existe dans les sources.

### 55.3 `LIBERER` rend le vrai camp

Rendre systématiquement le groupe 0 aurait fait, dans une mission russe, des
ennemis du joueur au lieu de rendre les hommes aux leurs. Le camp d'origine de
chaque rallié est désormais noté (`g_rallied_origin`) au moment du ralliement et
restitué à la libération.

### 55.4 Le journal dit ce qu'il y a en face

```
Ralliement: camps encore en face -> groupe 0 : 14, groupe 1 : 3. Les votres
passent au groupe 3, qui n'appartient a personne : ils sont donc hostiles a tous
ces camps a la fois, et amis avec vous.
```

Cette ligne rend le comportement vérifiable au lieu de supposé — et aurait révélé
la faute du chapitre 41 dès la première mission russe.

### 55.5 Le switch ne bouclait jamais

Le journal ne portait aucune ligne « vos N soldats sont passés ».

`UpdateSwitchCycle` listait les soldats dans l'ordre rendu par
`CollectLocalPlayers`, c'est-à-dire l'ordre de la liste d'acteurs du moteur. Or
`PlayerSwitch` les **trie par `menu_id`** avant de les faire défiler
(`GameMission.cpp:3062`) :

```cpp
slist.Add(plrs[i], plrs[i]->GetMenuID());
slist.Sort();
```

Le test « du dernier au premier » portait donc sur un ordre étranger à celui du jeu.
La liste est maintenant triée par `menu_id` croissant, avec l'offset déjà mesuré par
`MeasurePlayerMenuIdOffset` (+0x2B0).


## Chapitre 56 — le déclenchement, pas l'hostilité

**Écrit le 5 septembre 2026 à 15:52:05 (heure locale).**

### 56.1 Le camp était bon

```
Ralliement: camps encore en face -> groupe 0 : 11. Les votres passent au groupe 3.
```

Groupe 3 contre groupe 0, et `C_enemy::GetRelation` rend `RELATION_ENEMY` pour ce
couple (`Actors.cpp:15037`). L'hostilité est donc correctement posée depuis le
chapitre 55. Ce qui manquait est ailleurs.

### 56.2 Le moteur ne tire que sur ce qu'il a vu

```cpp
// WatchHumans (Actors.cpp:3917)
if(!a->IsEnemy(t.actor)) return true;        // filtre d'hostilité
if(distance < watch_test_range) { ... ajout à la liste, relation mise en cache }
// puis, périodiquement : cône de vision, facteur de distance, ligne de vue,
// accumulation de `seen_amount`

// AI_solution (Actors.cpp:4906)
bool is_enemy = (rel==RELATION_ENEMY);
if(wa.IsSeen() && is_enemy) first_seen = a;
...
case A_ATTACK:
   if(!first_seen) break;
   AddProgram(0, PRG_ATTACK, S_prg_add((dword)first_seen, ...));
```

**Rien ne se déclenche tant que `first_seen` est nul**, c'est-à-dire tant qu'aucun
ennemi n'a été *vu* — cône de vision, portée, ligne de vue, et accumulation dans le
temps.

Or un rallié et ses anciens camarades **se tournent le dos** : ils étaient du même
camp une seconde plus tôt, et rien ne leur donne de raison de se retourner. Deux
groupes immobiles qui ne se regardent pas ne se verront jamais.

Ce n'était donc ni le camp, ni l'IA, ni le programme : c'était le premier regard.

Deux gardes envisagées puis écartées comme fausses pistes, pour mémoire :
`TAB_E_AI_ENABLED` est testé sous `#ifdef EDITOR` (`Actors.cpp:6612` et `15326`),
donc inactif dans le jeu vendu ; et un programme vide n'empêche pas l'IA de tourner
(`if(!program.size()){ if(AI_solution(...)) break; }`, `Actors.cpp:14944`).

### 56.3 La ligne `FEU`

Elle pose l'ordre que l'IA se serait donné, avec la même fonction et la même forme :

```cpp
AddProgram(0, PRG_ATTACK, S_prg_add((dword)cible, partie_du_corps), true)
```

`S_prg_add` est un simple `dword d[5]` (`H&D.h:556`) : `d[0]` la cible, `d[1]` la
partie du corps (1 = poitrine, celle que l'IA prend). L'ordre est inséré **en tête**,
comme le fait l'IA ; le programme n'est pas vidé, pour que l'homme reprenne ensuite
son occupation, et surtout parce que l'IA prend le relais dès qu'ils se sont vus.

Chaque rallié reçoit l'ennemi encore hostile **le plus proche de lui**, calculé sur
les positions relevées à chaque image.

### 56.4 Sûretés

- `AddProgram` : rang `+0x24`, `ret 16` — quatre arguments, déjà vérifiés au
  chapitre 51 et respectés par le stub.
- Chaque couple attaquant/cible passe par `IsOrderableActor` juste avant l'ordre :
  vtable dans le module, type humain, vivant.
- Corps de boucle du stub : 30 octets, très en deçà de la limite de 127 des sauts
  courts.


## Chapitre 57 — les fenêtres se rouvrent sur le dernier choix

**Écrit le 5 septembre 2026 à 15:58:52 (heure locale).**

### 57.1 La demande

« Quand j'ouvre la fenêtre je me retrouve en haut à chaque fois, alors que je veux
me retrouver par défaut sur le dernier choix. Même logique pour la touche J. »

### 57.2 Pourquoi retenir l'action et non le numéro de ligne

Les deux fenêtres remettaient la sélection à zéro à l'ouverture
(`g_soldier_menu_selection = 0`, `g_weapon_menu_selection = 0`).

Conserver simplement le numéro aurait été faux : **le contenu de la fenêtre G varie
d'une ouverture à l'autre**. Les lignes des alliés ralliés n'existent que s'il y en
a ; « rendre le pilotage » n'apparaît que si un allié est en main ; « SUPPRIMER » que
si des soldats ont été créés ; le bloc `RALLIER`/`LIBERER`/`FEU`/`VENIR`/`ALLIES`
apparaît et disparaît avec eux. Un index gardé tel quel désignerait une action
différente à chaque fois.

Le trainer retient donc **l'action** portée par la ligne validée, plus son `index`
pour les lignes propres à un homme (`Control`, `SendOne`, `SendOneRallied`). À la
réouverture, il cherche la ligne qui porte le même couple ; si elle a disparu, il
retombe sur la première.

### 57.3 La fenêtre J

Sa structure est fixe — trois lignes d'en-tête puis les armes — mais la **liste des
armes** dépend de l'inventaire du joueur. On retient donc le **rang de l'arme**
(`weapon_index`), pas le numéro de ligne, et la sélection est replacée à
`3 + rang`, bornée au nombre de lignes réel. La cible (tous / groupe / un homme)
était déjà conservée par `settings.weapon_menu_target`.

La ligne d'essai volontaire de sortie d'arme n'est pas mémorisée : elle ne doit
jamais devenir le choix par défaut d'une réouverture.

### 57.4 Mise en œuvre

Un drapeau `..._just_opened` est levé par la bascule d'ouverture, et consommé après
la reconstruction des lignes — c'est le seul moment où l'index a un sens, puisque la
table des actions vient d'être rebâtie pour l'état courant.


## Chapitre 58 — l'ordre d'attaque était jeté ; il fallait les faire regarder

**Écrit le 5 septembre 2026 à 16:10:00 (heure locale).**

### 58.1 `execute=1` et pourtant personne ne tire

Le journal du joueur : `Ouverture du feu: 10 couple(s) attaquant/cible,
declenche=1 execute=1`, deux fois. L'ordre arrivait donc bien, et rien ne se
passait.

La cause est dans l'exécution de `PRG_ATTACK` (`Actors.cpp:9581`) :

```cpp
fire_dist = GetType()==ACTOR_ENEMY ? AI_f(TAB_F_AI_MIN_SHOOT_DIST)
                                   : AI_f(TAB_F_AI_WATCH_MAX_DIST);
if(dist >= fire_dist){
   if(MayHunt(sub_pos, key) && subject_seen){ /* insère PRG_MOVE vers lui */ }
   del = true;                        // sinon l'ordre est SUPPRIMÉ
   break;
}
```

Et, plus haut dans le même cas :

```cpp
if(!gun && !gun_mode){ if(!cbProc(CB_WEAPONCHANGE)) del = true; break; }
```

Deux conditions, donc : **tenir une arme**, et surtout **avoir vu la cible**. Un
ordre d'attaque sur quelqu'un qui n'est pas encore `subject_seen` est jeté à la
première image. Le moteur refuse de faire tirer un homme sur ce qu'il ne voit pas
— ce qui est sain, et ce que le chapitre 56 avait déjà identifié sans en tirer la
conséquence : **le chapitre 56 posait le mauvais ordre.**

### 58.2 Le bon ordre : `PRG_WATCH`

`Actors.cpp:10290` :

```cpp
case PRG_WATCH:
   if(AI_solution(tc.time, dest_pose, tab_game_cfg->ItemF(TAB_F_GAME_WATCH_CARE_WATCH)))
      break;
   ...
   S_vector &base_dest = *(LPS_vector)key.sub_param.watch.base_dest;
   S_vector dir(base_dest-curr_pos);            // il se tourne vers le point
   ... balayage aléatoire autour de cette direction ...
```

Il **tourne l'homme vers un point** et l'y fait regarder en balayant, et il appelle
`AI_solution` à chaque image avec une vigilance élevée — ce qui accélère
l'accumulation de `seen_amount` dans `WatchHumans`.

Dès que la cible est vue, **c'est l'IA elle-même** qui ajoute son `PRG_ATTACK`
(`Actors.cpp:5172`), par le chemin normal du jeu. Rien n'est forcé : on leur donne
seulement la raison de se retourner, qui est exactement ce qui leur manquait.

`S_prg_add` pour `PRG_WATCH` : `d[0]` = pointeur sur la destination,
`d[1]` = nombre de phases (4 par défaut). Le stub calcule l'adresse de la
destination de chaque homme — `base + rang × 12` — et l'écrit dans `d[0]` avant
chaque appel.

### 58.3 Automatique dès le ralliement

Demande du joueur : « je veux qu'ils soient instantanément dès le ralliement ».

`SetEnemyGroup` réussi ⇒ `BuildOpenFirePairs` puis `OrderWatchTowardOnGameThread`
dans la foulée. Chaque nouveau rallié se tourne vers l'ennemi encore hostile le plus
proche de lui, calculé sur les positions relevées à chaque image.

La ligne `FEU` reste dans la fenêtre G : elle refait la même chose à la demande,
utile après les avoir déplacés ailleurs.

### 58.4 Effet de bord utile

Le chapitre 57 signalait qu'un rallié déjà en train de viser un joueur pouvait finir
son geste, la relation étant mise en cache. L'engagement automatique réduit fortement
ce cas : il lui donne aussitôt une autre direction à regarder, et `PRG_WATCH` passe
en tête de son programme.


## Chapitre 59 — à cent mètres, personne ne voit personne

**Écrit le 5 septembre 2026 à 16:17:37 (heure locale).**

### 59.1 La donnée qui manquait, et elle était sous les yeux

```
Ralliement: les ennemis sont pris du PLUS PROCHE au plus loin;
le premier est a 104 metres de vous.
```

Cette ligne existe depuis la V143 et n'avait jamais été rapprochée du problème.
Les ralliés et les Allemands restants sont à **cent mètres** les uns des autres.

Or `WatchHumans` borne strictement l'ajout à la liste de surveillance
(`Actors.cpp:3920`) :

```cpp
float watch_test_range = AI_f(TAB_F_AI_WATCH_MAX_DIST);   // « Watch distance »
...
if(((*a->GetPos()) - t.pos).Magnitude() < t.watch_test_range){
   if(i==-1){ ... ajout avec la relation ... }
}
```

**Au-delà, l'acteur n'est même pas ajouté à la liste.** Il n'existe pas pour ce
soldat. Aucune accumulation de visibilité, aucun `IsSeen()`, donc aucun
`PRG_ATTACK` — quel que soit le camp.

### 59.2 Deux chapitres de corrections inutiles, et pourquoi

- **Chapitre 56** : ordonner `PRG_ATTACK`. L'ordre était supprimé faute de
  `subject_seen` **et** parce que `dist >= fire_dist`.
- **Chapitre 58** : ordonner `PRG_WATCH` vers la position de l'ennemi. Ils
  regardaient dans le vide : à cette distance, il n'y a personne à voir.

Les deux corrigeaient l'aval d'un problème dont la cause était en amont — la
distance. Le journal la donnait depuis le début.

### 59.3 La correction : la solution du moteur lui-même

Quand le moteur perd sa cible de vue, il ne la « regarde » pas : il **va la
chercher** (`Actors.cpp:9711`) :

```cpp
AddProgram(++i, PRG_MOVE,
           S_prg_add((dword)&sub_pos, true /*run*/, MR_ATTACK_REACH), true);
```

C'est exactement l'ordre posé désormais : `PRG_MOVE` vers la position de l'ennemi
le plus proche, en courant, avec `MR_ATTACK_REACH` (= 3, `Actors.h:54`). Dès qu'ils
entrent dans la portée de vision, ils voient, et leur IA ajoute l'attaque.

`S_prg_add` : `d[0]` = destination (écrite par le stub, une par homme),
`d[1]` = course, `d[2]` = raison du déplacement.

Automatique dès le ralliement, et rejouable par la ligne `FEU`.

### 59.4 Ce qui ne peut pas être contourné

Aucun soldat du jeu — allié, ennemi ou joueur — ne voit ni ne tire au-delà de la
portée de vision fixée par la mission. Deux groupes à cent mètres ne s'engageront
jamais sans que l'un se déplace. Le seul levier est donc la distance, et c'est
celui qui est actionné.

### 59.5 Méthode

Trois chapitres (56, 58, 59) sur le même symptôme, dont deux corrections sans effet.
Le journal portait la mesure décisive — « le premier est à 104 mètres » — depuis le
début. **Une donnée déjà journalisée doit être confrontée au symptôme avant toute
lecture de code.**


## Chapitre 60 — c'étaient mes ordres qui empêchaient leur IA

**Écrit le 5 septembre 2026 à 16:42:12 (heure locale).**

### 60.1 L'observation qui a tout débloqué

« Dans la version V153 les ennemis de mon équipe frappaient les autres ennemis. »

Cette phrase valait mieux que trois chapitres de lecture de code. La V153 se
distingue par deux choses : elle employait le **groupe 1**, et surtout **elle
n'ajoutait aucun ordre aux ralliés**. Ils gardaient le programme de la mission, leur
IA tournait normalement, et l'engagement se faisait par le chemin habituel du jeu.

Les chapitres 56, 58 et 59 ont ajouté des ordres pour résoudre un problème que ces
ordres créaient en partie.

### 60.2 Le déplacement seul était malformé

Le chapitre 59 posait un `PRG_MOVE` avec `MR_ATTACK_REACH` **isolé**. Or le moteur
ne s'en sert jamais seul (`Actors.cpp:9711`) :

```cpp
if(stay_mode!=SM_STAY) AddProgram(++i, PRG_STAY_UP, S_prg_add());
AddProgram(++i, PRG_MOVE, S_prg_add((dword)&sub_pos, true, MR_ATTACK_REACH), true);
```

Il l'insère **toujours devant un `PRG_ATTACK` déjà présent**. Et le traitement de ce
déplacement interroge le programme suivant, qu'il suppose être cette attaque
(`Actors.cpp:9360` et `9329`) :

```cpp
LPC_actor subject = next_key.GetSubject();
...
del = 2;   //remove MOVE and ATTACK
```

Un `MR_ATTACK_REACH` sans attaque derrière est donc un ordre incomplet : il lit un
programme qui n'est pas celui qu'il attend.

### 60.3 La correction : le couple, dans l'ordre du moteur

Le stub émet désormais **deux** `AddProgram` par homme :

1. `AddProgram(0, PRG_ATTACK, S_prg_add(cible, poitrine), true)`
2. `AddProgram(0, PRG_MOVE, S_prg_add(&destination, course, MR_ATTACK_REACH), true)`

La seconde insertion passe devant la première : la liste devient
`[MOVE, ATTACK, ...]`, exactement la forme du moteur. Corps de boucle vérifié hors
du trainer : 68 octets, saut court dans les bornes.

### 60.4 Le groupe 1 revient quand la mission le permet

Le raisonnement du chapitre 55 reste valide — le groupe 3 n'appartient à personne,
donc il est hostile à tous, et aucun usage du moteur ne le rejette (`!group ? 50 : 0`
pour la voix, un `switch` de statistiques sur 0 et 2, une comparaison à 2 pour les
ordres). Mais **le groupe 1 est celui que le joueur a vu fonctionner**, et rien ne
justifie de lui préférer une valeur non éprouvée quand elle convient.

`ChooseAllyGroup` retient, parmi 1 puis 3, la première valeur qu'aucun ennemi encore
en face n'utilise :

| ennemis de la mission | groupe des ralliés |
|---|---|
| groupe 0 (allemands) | **1** — exactement la V153 |
| groupe 1 (russes) | 3 |

### 60.5 Méthode

Cinq versions sur ce symptôme, dont trois corrections qui l'aggravaient. Le signal le
plus utile est venu du joueur : « telle version faisait mieux ». **Quand une version
antérieure se comportait mieux, comparer ce qui a changé passe avant toute nouvelle
lecture de code — et avant tout nouvel ajout.**


## Chapitre 61 — seuls les chasseurs partaient

**Écrit le 5 septembre 2026 à 16:49:33 (heure locale).**

### 61.1 Ce n'était pas le trainer

`Engagement: 10 homme(s) recoivent le COUPLE [deplacement, attaque]` — les dix
recevaient l'ordre. Le refus venait du jeu, dans `MayHunt` (`Actors.cpp:3787`) :

```cpp
case ACTOR_ENEMY:
   if(AI_e(TAB_E_AI_HUNT)==2){                            // (1) poursuite permise
      const S_vector &origin_pos = program.back()->dest;  // (2) son poste
      float dist = |our_pos - origin_pos|;
      if(dist < (AI_f(TAB_F_AI_HUNT_MAX_DIST)-.5f)) return true;   // (3) rayon
      ...
   }
   break;
return false;
```

Trois conditions, toutes issues des **réglages de mission de cet homme**. Un garde
statique — « Hunt enemy… = No » — abandonne l'ordre et reste à son poste. Ce n'est
ni un bug ni un plafond posé par le trainer : c'est le métier que la mission lui a
donné.

### 61.2 Ces réglages sont dans la fiche que nous écrivons déjà

`AI_e` / `AI_f` ne lisent pas une table séparée (`Actors.cpp:4855`) :

```cpp
byte AI_e(dword indx) const{
   byte ret = tab->ItemE(indx);                    // la fiche de l'acteur
   if(!ret) ret = mission.tab_property->ItemE(indx);
   if(!ret) ret = tab_game_cfg->ItemE(indx);
   return ret;
}
```

C'est `tab`, la même fiche que celle où le camp est écrit depuis le chapitre 41, à
`+0x1A4`. Et `Tables.h` réserve explicitement les propriétés **96 à 127** à l'IA
dans ce modèle.

| propriété | rôle | valeur écrite |
|---|---|---|
| 96 `TAB_E_AI_HUNT` | poursuivre l'ennemi | 2 (« oui ») |
| 97 `TAB_F_AI_HUNT_MAX_DIST` | rayon de poursuite autour du poste | 500 m |
| 98 `TAB_F_AI_WATCH_MAX_DIST` | portée de vision | 120 m |

La troisième agit aussi sur le chapitre 59 : ils accrochent de plus loin.

`FreeAllyAiLimits` n'écrit que ce qui existe — `ResolveActorTableInteger` rend zéro
pour une propriété absente du modèle, et le journal compte les cas. Aucune écriture
au hasard.

Noter la subtilité de la chaîne de repli : `AI_e` ne consulte la mission que si la
valeur de l'acteur vaut **zéro**. Écrire 2 sur l'acteur l'emporte donc dans tous les
cas, sans toucher aux réglages de la mission ni aux autres ennemis.

### 61.3 Le nombre au choix

La ligne `FEU` porte un nombre saisi au clavier, borné au nombre de ralliés vivants
et remis dans les bornes à chaque image. Les hommes sont pris dans l'ordre de la
liste ; ceux qui ne partent pas restent en place.

### 61.4 Ce que ce chapitre confirme

Le journal disait déjà « 10 homme(s) reçoivent » : la mesure existait, et elle
excluait le trainer. Le chapitre 59 avait tiré la même leçon. Elle tient : **avant
de chercher un défaut dans le trainer, lire ce que le journal affirme déjà.**


## Chapitre 62 — FEU ALL, sans empiler les ordres

**Écrit le 5 septembre 2026 à 17:04:53 (heure locale).**

### 62.1 La demande

« Un autre choix juste au-dessus de FEU : la même logique, mais en boucle — dès
qu'il attaque un, il part vers un autre plus proche, jusqu'à ce qu'ils les
terminent tous. »

### 62.2 Le piège : `AddProgram` insère, il ne remplace pas

Redonner l'ordre à tout le monde toutes les deux secondes ferait grossir sans fin la
liste de programmes de chaque homme : `AddProgram(0, ...)` **insère en tête**, et
chaque nouvel ordre masquerait le précédent sans jamais le retirer. C'est la même
propriété qui avait rendu le chapitre 51 nécessaire.

### 62.3 La règle retenue : ne renvoyer que ceux dont la cible est tombée

`g_fire_all_target` retient le couple homme → cible. À chaque passage (2 s) :

1. si plus d'allié rallié, ou plus d'ennemi hostile → le mode s'arrête et le dit ;
2. on liste les alliés dont la cible **ne figure plus** parmi les hostiles — morte,
   ralliée à son tour, ou disparue ;
3. `BuildOpenFirePairs` est appelée **restreinte à ceux-là** (paramètre `only`), on
   lève leurs limites de poursuite, on envoie le couple [déplacement, attaque], et on
   note les nouvelles cibles.

Tant que chacun a une cible vivante, **rien n'est envoyé** : aucune écriture dans le
jeu, aucun passage par le site de triche.

### 62.4 Arrêts automatiques

Le mode se coupe seul quand il n'y a plus d'ennemi en face, plus d'allié rallié, ou
quand le joueur le rebascule. Chaque arrêt est journalisé avec sa raison.

### 62.5 Ce qu'il ne change pas

La ligne `FEU` du chapitre 61 reste, avec son nombre saisi : elle sert à envoyer
exactement qui l'on veut, quand on le veut. `FEU ALL` est un mode, placé juste
au-dessus, et les deux ne se gênent pas — le mode ne réagit qu'aux cibles tombées.


## Chapitre 63 — le journal ne coûte plus d'images

**Écrit le 6 septembre 2026 à 01:45:20 (heure locale).**

### 63.1 Le constat du joueur

« Je pense que le journal fait lager mon jeu pendant une mission. »

### 63.2 La mesure, avant toute correction

Le journal du dernier essai contient 1855 lignes. **1473 d'entre elles sont le
même message, et elles portent toutes le même horodatage à la seconde près :**

```
[2026-09-05 18:06:53.xxx] Enemy invisibility: player damage signature mismatch at 004219B0.
```

Ce n'est donc pas un fichier qui grossit doucement : c'est une **rafale de 1473
écritures dans la même seconde**, en pleine mission.

### 63.3 Pourquoi une écriture coûtait si cher

`LogDiagnostic` faisait **trois accès disque par ligne** :

| appel | rôle |
|---|---|
| `GetFileAttributesExW` | relire la taille du fichier pour le plafond de 2 Mo |
| `CreateFileW` | **ouvrir** le fichier |
| `CloseHandle` | **fermer** le fichier |

Ouvrir et fermer un fichier est l'opération la plus coûteuse du lot. Multipliée
par 1473 dans la même seconde, sur le fil qui pilote aussi l'overlay et les
hooks, elle bloque le jeu.

### 63.4 La source de la rafale

`InstallPlayerDamageHookState` compare le prologue de `C_player::Hit` à sa
signature attendue (`83 EC 6C 53 55`). Or, une fois le hook posé, ce prologue
commence par `E9` — un saut. **La comparaison ne peut donc plus jamais
réussir**, et la fonction, réessayée à chaque image, journalisait son échec à
chaque image. C'est la propriété déjà connue du projet : les détours restent en
place dans le jeu.

Le message est conservé — il est utile — mais dit **une seule fois par adresse**.

### 63.5 Les trois mesures dans `diagnostics.cpp`

1. **Le fichier reste ouvert.** Plus aucune ouverture ni fermeture par ligne :
   un seul `WriteFile`. Le handle garde `FILE_SHARE_READ | FILE_SHARE_WRITE`,
   donc le journal se lit toujours pendant que le jeu tourne.
2. **Le chemin n'est calculé qu'une fois**, et la taille n'est vérifiée
   qu'une fois toutes les deux secondes au lieu de chaque ligne.
3. **Les lignes identiques consécutives sont regroupées** : la première est
   écrite, les suivantes sont comptées, puis résumées par
   `(ligne precedente repetee N fois de suite)`. Aucune rafale, quelle qu'en
   soit la source future, ne peut donc plus coûter des milliers d'écritures.

### 63.6 Ce que cette version ne change pas

**Aucune fonction du jeu n'est touchée.** Seule l'écriture du journal change.
Le contenu journalisé reste le même, à la rafale près, qui est désormais résumée
au lieu d'être répétée.

Le travail réseau commencé puis interrompu (`NormalizeRalliedAiOwnership`) a été
**retiré** de cette version et conservé à part dans
`V165_reseau_non_teste.patch` : il n'a jamais été compilé ni testé, et n'avait
donc pas sa place dans une version finale.
