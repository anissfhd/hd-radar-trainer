# HDFinalESPBallistic

> ## Etat reel au 2 septembre 2026 -- ne pas distribuer comme solution finale
>
> Ce depot contient deux pistes. La piste historique est le trainer externe et
> ses helpers LAN Vxx : elle conserve le moteur reseau pair-a-pair d'origine.
> Les essais ont confirme qu'elle ne peut pas garantir que les deux PC voient
> le meme etat pour **Protection totale** et **Retour a la vie (F12)**. Elle ne
> doit donc plus etre presentee comme une autorite hote fiable, meme si plusieurs
> fonctions locales du trainer fonctionnent correctement.
>
> La piste cible est `source/hde` : un port du moteur dans lequel l'hote doit
> devenir l'unique source de verite pour toute la partie. Un premier code source
> est implemente et compile, mais son paquet de test ne demarre pas encore dans
> une installation de jeu. Ne remplacez aucun `hde.exe` avec `HDE_Authority.exe`
> ou le contenu de `source/hde/dist/HDE_Authority_Test` tant que le paquet n'a
> pas ete repare et valide localement.
>
> Etat et plan actuels : [`plan.md`](plan.md),
> [`source/hde/BUILD_PORT_STATUS.md`](source/hde/BUILD_PORT_STATUS.md) et
> [`source/hde/HOST_AUTHORITY_SPEC.md`](source/hde/HOST_AUTHORITY_SPEC.md).

## Audit du port moteur -- 2 septembre 2026 (synthese R1 a R12)

> Audit produit en relisant le code de `source/hde/_src` et en analysant la
> table d'import reelle du binaire compile. Le detail complet, avec les
> references de fichier et de ligne, est dans [`plan.md`](plan.md), section
> « Audit technique du port moteur -- 2 septembre 2026 ».
>
> Rien ci-dessous ne doit etre presente comme fonctionnel parce que cela
> compile, parce qu'un journal affiche « actif », ou parce que cela se comporte
> correctement sur le seul ecran de l'hote.

Cinq categories strictes, a ne jamais melanger :

**A. Ancien trainer externe historique.** `src/`, `build/`, `release/`,
`archive/`, helpers `HD_AI_AUTHORITY_*` et canal UDP prive 48217. Cette piste ne
modifie pas le moteur reseau : le pair-a-pair d'origine reste en place. Les
traces V88/V92 prouvent seulement que les commandes UDP arrivent, jamais que le
client affiche l'etat canonique de l'hote. **Protection totale et Retour a la
vie F12 restent non valides en LAN par cette piste.** Les fonctions purement
locales (ESP, radar, armes locales, noclip, overlay) fonctionnent toujours :
local n'est pas autoritaire. Aucun trainer, helper, hook memoire ou canal UDP
d'autorite ne doit tourner pendant un test du moteur.

**B. Code moteur deja compile.** `HDE_Authority.exe`, PE32 x86, SHA-256
`7388BC...A2D49` verifie. Existent reellement : `HD_HOST_AUTHORITY` (`Net.h:19`),
la trame d'intention `S_authority_input` sans aucun champ position/vie/degat/
inventaire (`Net.h:24-40`), `NM_AUTH_INPUT = 0x5700` (`Net.h:215`), l'envoi et la
validation d'entrees (`Net.cpp:227-330`), les ennemis attribues a l'hote
(`GameMission.cpp:1302-1310`), la validation PID/acteur/sequence
(`GameMission.cpp:2702-2725`) et le controleur distant
(`Actors.cpp:13668-13795`). **Ce code n'a jamais ete execute**, ni en solo ni en
LAN. Defauts releves, invisibles a la compilation : R2 demotion du client
seulement pendant `Tick` ; R3 trou sur `PLRMODE_PROGRAM` / `DYING` / `DEAD` ;
R4 autorite non transitive au-dela de deux PC ; R5 le client emet toujours ses
resultats, on ne compte que sur le rejet cote hote ; R6 `AuthorityAcceptInput`
non sure isolement ; R7 64 slots pour `GKEY_LAST == 65` ; R8 comportement au
timeout de 500 ms ni defini ni observe.

**C. Erreurs de packaging.** C1 : le binaire importe `MSVCP140.dll`,
`VCRUNTIME140.dll` **et 10 `api-ms-win-crt-*.dll`** ; il faut le redistribuable
VC++ 2015-2022 x86 complet ou un CRT statique (cause :
`_cmake/CMakeLists.txt:7`, `/MD`). C2 : le binaire charge dynamiquement les
**noms historiques** `i3d2.dll`, `igraph2.dll`, `inet2.dll`, `isound2.dll` via
ses thunks, alors que le paquet ne contient que `hd_i3d_2.dll`,
`hd_igraph2.dll`, `hd_inet2.dll`, `hd_isnd2.dll` : dans le dossier de test le
chargement echoue, et pose dans une vraie installation il chargerait les DLL de
2002 -- melange ABI, faux positif dangereux. C3 : `hd_debugmem.dll` construit
mais reference par aucun import. R10 : les fichiers texte du paquet
conseillaient encore de l'installer, corriges le 2 septembre 2026. R12 : le
`hde.exe` de `source/hde/bin` (`1C6712...`) n'est pas la revision des
installations testees (`5D5EED...`) -- le port repare peut demarrer et malgre
tout ne pas lire les donnees, tables et scripts du mod **Ultimate Mod 5.0**.

**D. Blocages avant le premier test LAN.** D0 : le paquet
`source/hde/dist/HDE_Authority_Test/` est **interdit de test** tant que C1, C2 et
C3 ne sont pas repares et qu'un lancement local n'a pas reussi ; aucun `hde.exe`
original ne doit etre remplace, renomme ou ecrase. R1 : le filtre qui rejette
**tout** `NM_GAME` venant du client (`GameMission.cpp:2730-2737`) est trop
large -- `NM_GAME` transporte aussi `NM_SYNC`, `NM_MAP`, `NM_CHAT` et
`NM_HUMAN_SWITCH_SYNC`, et `C_net_sync` exige la reponse de **tous** les PID,
donc synchronisation, briefing, carte, chat, menus, vehicule et **debut de
mission** risquent de ne jamais se resoudre. R9 : `source/`, `release/` et
`interface/` ne sont **pas versionnes** (`git ls-files source` vide, et ils ne
sont pas dans `.gitignore`) -- une procedure de sauvegarde est decrite dans
`plan.md`, a executer uniquement sur autorisation explicite ; aucun `git add` ni
commit n'a ete lance. R12 : a lever par un lancement local avec le mod.

**Regle de blocage.** Protection totale et F12 n'existent pas dans le moteur :
aucune ligne. Ils **ne doivent etre ni testes, ni annonces, ni presentes comme
fonctionnels** avant que les instantanes/ACK/resynchronisation et le canal de
commande trainer host-only existent et soient verifies.

**E. Travail restant.** 1 message de protocole sur 8 (`NM_AUTH_INPUT` seul) ;
aucun instantane, ACK ni resynchronisation ; aucun handshake version/hash ;
aucune commande trainer host-only ; menus, inventaire, vehicules, objectifs,
reprise et reconnexion non couverts. R11 : la specification annonce un
protocole dans le canal `NM_GAME` alors que le code a place `NM_AUTH_INPUT` au
premier niveau -- specification a corriger.

**Ordre d'execution arrete le 2 septembre 2026 :** sauvegarde/versionnement
approuves -> packaging executable -> lancement local sans mod -> lancement local
avec mod -> correction du filtre `NM_GAME` -> test LAN minimal -> demotion
client permanente -> instantanes/ACK/resynchronisation -> handshake ->
commandes trainer host-only -> Protection totale -> Retour a la vie F12. Detail
et criteres d'arret dans [`plan.md`](plan.md).

## Archive historique -- trainer externe, avant la migration moteur (repere ajoute le 2 septembre 2026)

> Les informations qui suivent decrivent les fonctions et versions historiques
> du trainer externe. Elles ne doivent pas etre lues comme la specification du
> nouveau moteur hote-autoritaire, ni comme une garantie LAN pour Protection
> totale ou F12. Leur date d'etat est anterieure au **2 septembre 2026** ; les
> derniers constats priment et sont resumes ci-dessus.

Trainer externe x86 pour `hde.exe` (Hidden & Dangerous Deluxe, Ultimate Mod 5.0),
avec fenêtre DirectX 9 / Dear ImGui et lecture de la mémoire du jeu
(`ReadProcessMemory`).

## Binaire à tester

```text
release\V165_JOURNAL_RAPIDE\HDFinalAdvancedV165_JOURNAL_RAPIDE.exe
release\V165_JOURNAL_RAPIDE\HD_AI_AUTHORITY_HOST_V105.exe
release\V165_JOURNAL_RAPIDE\HD_AI_AUTHORITY_CLIENT_V105.exe
```

Construit le 6 septembre 2026, 01:45:20 (heure locale). Sauvegarde complète
(binaires + sources) dans `_GOLD_V165\`, états précédents dans `_GOLD_V164\`,
`_GOLD_V163\`,
`_GOLD_V162\`,
`_GOLD_V161\`,
`_GOLD_V160\`,
`_GOLD_V159\`,
`_GOLD_V158\`,
`_GOLD_V157\`,
`_GOLD_V156\`,
`_GOLD_V155\`,
`_GOLD_V154\`,
`_GOLD_V152\`,
`_GOLD_V151\`,
`_GOLD_V150\`,
`_GOLD_V149\`,
`_GOLD_V148\`,
`_GOLD_V147\`,
`_GOLD_V146\`,
`_GOLD_V145\`,
`_GOLD_V144\`,
`_GOLD_V143\`,
`_GOLD_V142\`,
`_GOLD_V141\`,
`_GOLD_V140\`,
`_GOLD_V138\`,
`_GOLD_V137\`,
`_GOLD_V136\`,
`_GOLD_V135\`,
`_GOLD_V134\`,
`_GOLD_V133\`,
`_GOLD_V132\`,
`_GOLD_V131\`,
`_GOLD_V130\`,
`_GOLD_V129\`,
`_GOLD_V128\`,
`_GOLD_V127\`,
`_GOLD_V126\`,
`_GOLD_V125\`, `_GOLD_V124\`,
`_GOLD_V123\`,
`_GOLD_V122\`,
`_GOLD_V121\`,
`_GOLD_V120\`,
`_GOLD_V119\`,
`_GOLD_V118\`, `_GOLD_V117\`, `_GOLD_V116\`,
`_GOLD_V115\`,
`_GOLD_V114\`,
`_GOLD_V113\`,
`_GOLD_V112\`,
`_GOLD_V111\`, `_GOLD_V110\`,
`_GOLD_V109\`, `_GOLD_V108\`, `_GOLD_V107\`, `_GOLD_V106\`,
`_GOLD_V105\`, `_GOLD_V104\`,
`_GOLD_V103\`,
`_GOLD_V102\`,
`_GOLD_V101\`,
`_GOLD_V100\`,
`_GOLD_V99\`,
`_GOLD_V98\`,
`_GOLD_V97\`, `_GOLD_V96\`, `_GOLD_V95\` et `_GOLD_V94\`.

⚠️ **Le message LAN passe en version 5 : chaque PC ami doit remplacer son
compagnon par `HD_AI_AUTHORITY_CLIENT_V98.exe`.** Un ancien CLIENT rejette
proprement le nouveau paquet. Le HOTE n'a aucun code nouveau, seul son nom
change.
Trainer et helper HOTE sur le PC qui héberge, helper CLIENT seul sur chaque
PC ami. Voir `release\V95_SCOPE_LOGIC\LISEZ_MOI.txt`.

Les binaires des générations précédentes sont archivés dans `archive\bin`
(Release) et `archive\bin-debug` (Debug). Le suivi technique complet est dans
[`plan.md`](plan.md).

## V165 — le journal ne coûte plus d'images (6 septembre 2026, 01:45:20)

Le jeu saccadait pendant les missions. La mesure, avant toute correction : sur
les 1855 lignes du dernier journal, **1473 sont le même message, toutes dans la
même seconde** (18:06:53). Une rafale, pas une croissance lente.

Chaque ligne coûtait **trois accès disque** — lire la taille, **ouvrir**,
**fermer**. La rafale venait de `InstallPlayerDamageHookState` : une fois le
hook posé, le prologue de `C_player::Hit` commence par `E9`, la signature ne
correspond donc plus jamais, et l'échec était journalisé à chaque image.

Trois corrections, **toutes sur la journalisation, aucune sur le jeu** : le
fichier reste ouvert (un seul `WriteFile` par ligne), le chemin et la taille ne
sont plus recalculés à chaque ligne, et les lignes identiques consécutives sont
comptées puis résumées.

## V164 — FEU ALL, sans empiler les ordres (5 septembre 2026, 17:04:53)

Nouvelle ligne `FEU ALL`, juste au-dessus de `FEU` : les alliés enchaînent les
cibles jusqu'au dernier ennemi.

Le piège était la répétition : `AddProgram(0, ...)` **insère** en tête, si bien que
redonner l'ordre à tout le monde toutes les deux secondes ferait grossir sans fin la
liste de programmes de chaque homme. Le trainer ne renvoie donc **que les alliés dont
la cible est tombée** — il retient qui vise qui et compare aux ennemis encore
hostiles. Tant que chacun a une cible vivante, rien n'est envoyé.

## V163 — seuls les chasseurs partaient (5 septembre 2026, 16:49:33)

« Pourquoi seuls quelques-uns partent ? » Le journal excluait déjà le trainer :
`10 homme(s) recoivent le COUPLE`. Le refus vient de `MayHunt` — trois conditions
tirées des **réglages de mission de chaque homme** : poursuite autorisée, distance à
son poste d'origine, rayon de poursuite. Un garde statique reste à son poste.

Ces réglages ne sont pas dans une table séparée : `AI_e`/`AI_f` lisent **la fiche de
l'acteur**, celle où le camp est déjà écrit, et `Tables.h` y réserve les propriétés
96 à 127. Le trainer y écrit donc « poursuis = oui », un rayon de 500 m et une portée
de vision de 120 m — en n'écrivant que les propriétés réellement présentes, et en
comptant les absentes au journal.

La ligne `FEU` porte désormais un **nombre saisi au clavier** : le joueur choisit
combien d'alliés partent.

## V162 — c'étaient mes ordres qui empêchaient leur IA (5 septembre 2026, 16:42:12)

« Dans la V153 les ennemis de mon équipe frappaient les autres. » Cette observation
du joueur valait mieux que trois chapitres de lecture de code : **la V153 n'ajoutait
aucun ordre aux ralliés**. Leur IA tournait normalement et l'engagement se faisait
par le chemin habituel du jeu. Les V158, V160 et V161 ont ajouté des ordres pour
résoudre un problème que ces ordres créaient en partie.

Le défaut précis : un `PRG_MOVE` avec `MR_ATTACK_REACH` **isolé**. Le moteur ne
s'en sert jamais seul — il l'insère toujours devant un `PRG_ATTACK` déjà présent, et
son traitement interroge le programme suivant en le supposant être cette attaque
(`next_key.GetSubject()`, puis `del = 2; //remove MOVE and ATTACK`).

Le stub émet désormais le **couple**, dans l'ordre du moteur : attaque puis
déplacement inséré devant elle. Et le **groupe 1** revient dès que la mission le
permet — c'est celui que le joueur a vu fonctionner.

## V161 — à cent mètres, personne ne voit personne (5 septembre 2026, 16:17:37)

La mesure décisive était dans le journal depuis la V143 : « le premier est à **104
mètres** de vous ». `WatchHumans` borne strictement l'ajout à la liste de
surveillance — au-delà de la « Watch distance » de la mission, **l'acteur n'est même
pas ajouté**. Aucune visibilité, aucun `IsSeen()`, donc aucun tir, quel que soit le
camp.

Les V158 (ordonner l'attaque) et V160 (les faire regarder) corrigeaient donc l'aval
d'un problème dont la cause était la distance.

Le moteur résout ce cas d'une seule façon : quand il perd sa cible, il **va la
chercher** (`PRG_MOVE` en courant, `MR_ATTACK_REACH`). C'est l'ordre désormais posé,
automatiquement dès le ralliement.

## V160 — l'ordre d'attaque était jeté (5 septembre 2026, 16:10:00)

`execute=1` et pourtant personne ne tirait. La cause est dans l'exécution de
`PRG_ATTACK` : `if(dist >= fire_dist){ ... del = true; break; }` — un ordre
d'attaque sur une cible qui n'est pas encore `subject_seen` est **supprimé à la
première image**. Le moteur refuse de faire tirer un homme sur ce qu'il ne voit pas.
La V158 posait donc le mauvais ordre.

Le bon est `PRG_WATCH` : il **tourne l'homme vers un point** et l'y fait regarder en
balayant, tout en appelant l'IA à chaque image avec une vigilance élevée. Dès que la
cible est vue, l'IA ajoute son `PRG_ATTACK` elle-même, par le chemin normal du jeu.

C'est désormais **automatique dès le ralliement** : chaque nouveau rallié se tourne
vers l'ennemi le plus proche de lui à la seconde où il change de camp.

## V159 — les fenêtres se rouvrent sur le dernier choix (5 septembre 2026, 15:58:52)

Les fenêtres G et J remettaient leur sélection en haut à chaque ouverture. Elles
retiennent désormais le dernier choix — mais en mémorisant **l'action** de la ligne,
pas son numéro : le contenu de la fenêtre G varie selon qu'il y a des ralliés, un
allié en main ou des soldats créés, si bien qu'un index conservé tel quel
désignerait une autre action. Pour la fenêtre J, c'est le **rang de l'arme** qui est
retenu, la liste dépendant de l'inventaire du joueur.

## V158 — le déclenchement, pas l'hostilité (5 septembre 2026, 15:52:05)

Le camp était bon — le journal le montre : ralliés au groupe 3, ennemis restants au
groupe 0, et `GetRelation` rend `RELATION_ENEMY`. Mais **le moteur ne fait tirer un
homme que sur ce qu'il a vu** : `AI_solution` n'ajoute `PRG_ATTACK` que si un ennemi
est passé par le cône de vision, la portée et la ligne de vue de `WatchHumans`.

Or un rallié et ses anciens camarades **se tournent le dos** : ils étaient du même
camp une seconde plus tôt. Deux groupes immobiles qui ne se regardent pas ne se
verront jamais.

Nouvelle ligne `FEU` : chaque rallié reçoit l'ordre d'attaquer l'ennemi le plus
proche de lui — exactement l'ordre que son IA se serait donné
(`AddProgram(0, PRG_ATTACK, S_prg_add(cible, partie), true)`), inséré en tête comme
elle le fait. Ensuite l'IA prend le relais toute seule.

## V157 — un camp propre au joueur, et le switch trié comme le jeu (5 septembre 2026, 15:42:38)

« Dans une autre mission les ennemis de mon côté ne tirent pas sur les autres. »
Constat juste, et faute réelle de la V142 : `C_enemy::IsEnemy` décide l'hostilité par
**différence** de camp (`return my_group != his_group`). Le groupe 1 (russe) marche
tant que les ennemis sont allemands — mais dans une mission où ils sont **déjà
russes**, les ralliés se retrouvaient dans le même camp qu'eux.

Ils passent désormais au **groupe 3**, qui n'appartient à personne : différent de 0
(donc amis du joueur), de 2 (donc ils combattent vraiment) et de tous les camps
existants (donc hostiles à chacun, quelle que soit la mission). `LIBERER` note et
restitue le **vrai** camp d'origine de chacun, au lieu de les rendre aux Allemands.

**Le switch ne bouclait jamais** : les soldats étaient listés dans l'ordre de la
liste d'acteurs, alors que `PlayerSwitch` les trie par `menu_id`. Le test « du
dernier au premier » portait sur un ordre étranger à celui du jeu.

## V156 — le numéro prouvé par son appelant, et le switch étendu (5 septembre 2026, 15:32:14)

La caméra ne bougeait pas alors que l'appel réussissait : le message 137 tombait
hors des bornes de l'aiguillage (`cmp eax,0x3F`, messages 1 à 64) et le jeu rendait
sa valeur par défaut. L'erreur venait de la lecture du prologue — c'est `dec eax`
(index = msg − 1), pas un `add eax,imm` pris plus loin.

Le bon numéro est désormais établi **par preuve, non par calcul** :
`C_game_camera::SetFocus` est localisée à `0045DEB0` par son prologue exact repris
du binaire 2002 (une seule occurrence), et l'un de ses cinq appelants tombe *à
l'intérieur* du cas que la table numérote **21** — la valeur de 2002. L'énumération
n'avait pas changé. Identifier un message par la forme d'un cas est une déduction ;
par un appelant connu, c'est une preuve.

**Le switch étendu** : la touche de switch du jeu continue sur les alliés ralliés
après le dernier soldat, puis revient. Aucune touche n'est interceptée — le trainer
observe le bouclage du jeu (dernier soldat vers premier) et prend la main à ce
moment-là. Les touches 1 2 3 4 restent donc intactes par construction.

## V155 — la caméra sur l'homme piloté (5 septembre 2026, 15:15:40)

`cbProc(CB_SETFOCUS, 1, 0, 0)` pose la caméra du jeu sur l'allié pris en main, et la
ramène sur le soldat du joueur quand il rend le pilotage.

Deux pièges, tous deux évités par lecture du binaire. **Le nombre d'arguments** :
l'en-tête de 2002 en déclare trois (`?cbProc@C_actor@@UAEKKKK@Z`, `ret 12`), mais
les trois `cbProc` de Deluxe finissent par `ret 16` — quatre. La signature a changé
entre les versions ; c'est le piège de la V151, tendu une seconde fois. **Le numéro
du message** : `CB_SETFOCUS` vaut 21 en 2002, mais la table d'aiguillage de Deluxe
couvre les messages 117 à 180. Le bon numéro (137) a été établi en décodant cette
table, et confirmé par son voisin `CB_IS_FOCUSED` (138), reconnaissable à sa
comparaison du focus caméra avec la frame de l'acteur.

**La touche switch du jeu ne pourra pas les prendre**, et c'est prouvé :
`PlayerSwitch` filtre `ACTOR_PLAYER` *et* appelle `SetActive`, qui est vide pour
`C_enemy`. Deux murs, pas un.

## V154 — main libre : piloter un allié comme on se pilote soi-même (5 septembre 2026, 14:54:12)

Le mode « placer un par un » de la V153 est retiré à la demande du joueur ; les
sources repartent de `_GOLD_V152`. Sa demande était autre : « comme le noclip qui
existe déjà, même si je ne contrôle pas ce soldat, je peux le déplacer dans la map
comme je veux, n'importe où ».

C'est possible sans rien inventer. Le crochet du noclip n'est pas posé sur le
joueur : il est posé sur `C_human::Tick`, que **tous** les humains exécutent, et son
trampoline compare l'acteur en cours à celui qu'on lui publie
(`cmp ecx,[acteur publié]`). `UpdateNoclip` lui publiait `player_object_address` en
dur — la modification tient en un choix d'acteur, et toute la mécanique de vol est
celle déjà validée.

Deux lignes dans la fenêtre G : `MAIN LIBRE piloter l'allie N`, et le retour au
soldat du joueur. L'allié piloté est revalidé à chaque image, et le crochet de
publication réseau compare lui aussi l'acteur — il ne publiera jamais sa position à
la place de celle du joueur.

## V152 — ils s'arrêtent là où on les envoie (5 septembre 2026, 14:41:34)

« Quand je clique sur VENIR il ne s'arrête pas à cette place. » Le premier argument
d'`AddProgram(0, PRG_MOVE, ...)` est une **position d'insertion**, pas un
remplacement : l'ordre était posé en tête d'une liste contenant déjà la patrouille
de mission de l'ennemi, qui reprenait donc sa ronde après être venu.

Le moteur, lui, vide la liste avant tout ordre neuf. Le stub retire désormais
l'ordre de tête douze fois via `DelProgram` — vérifiée dans le binaire du joueur au
rang `+0x28` avec son `ret 8` — puis ajoute le sien. `PRG_READY` étant automatique
en fin de liste, l'homme arrivé reste sur place. Valable pour les ralliés comme
pour les soldats du joueur.

## V151 — un argument manquant, depuis huit versions (5 septembre 2026, 14:24:39)

`SetSelectedInvItem(int indx, bool net_send = true)` prend **deux** paramètres. La
valeur par défaut est posée par l'appelant en C++ ; dans le binaire la fonction en
attend deux, et son désassemblage sur le binaire du joueur le confirme : `ret 8` —
elle dépile huit octets.

**Le stub du trainer n'en empilait qu'un.** Chaque appel laissait `esp` quatre octets
trop haut, en plein dans les registres sauvés par le `pushad` d'entrée ; au `popad`
le jeu restaurait des valeurs décalées et repartait vers une adresse arbitraire.

Cela explique le « parfois ça marche, parfois ça tue », le témoin qui rapportait un
processus déjà détruit, l'inefficacité de la suspension des threads — **et les huit
échecs sur les soldats créés, attribués à tort à leur fiche manquante.**

Les deux sites d'appel empilent désormais les deux arguments, `net_send = false`. Un
audit des autres appels natifs (`AddItem`, `Reload`, `AddProgram`, `SetActive`,
`TableUpdate`) les donne tous corrects. La règle du dépôt — ne poser une valeur
qu'après l'avoir lue dans le binaire — s'étend désormais aux conventions d'appel.

## V150 — la sortie d'arme est coupée (5 septembre 2026, 14:06:49)

La V149 n'a pas empêché l'arrêt, mais elle a appris quelque chose : `PatchCodeSafely`
a refusé d'écrire pendant cinq secondes avant la remise en état — ce qui n'arrive
que si des threads du jeu disparaissent. **Le jeu mourait déjà.**

Bilan : munitions, distributions concurrentes, acteurs morts, les trois appels du
moteur, course sur le site de triche — **cinq hypothèses, cinq démenties**. La cause
reste inconnue, et la sixième version ne sera pas une sixième supposition.

Décision : la **désignation** de l'arme (`SetSelectedInvItem`) est coupée ; la
**remise** (`AddItem` + `Reload`) est conservée. C'est le seul chemin qui n'ait
jamais arrêté le jeu, et la perte est faible pour un rallié — il porte déjà son
arme. La dernière ligne de la fenêtre J reste un essai volontaire, avec un témoin
qui distingue enfin « rien à lire » de « le processus ne répond plus ».

## V149 — écriture sur du code en cours d'exécution (5 septembre 2026, 13:57:25)

Le témoin posé en V148 a donné la réponse au premier essai : **`etape 0 = le jeu
n'est jamais entré dans le code`**. Ni `AddItem`, ni `Reload`, ni la sortie d'arme —
le code injecté n'a jamais exécuté une instruction. Le jeu mourait *avant*, pendant
l'installation du saut.

Le site de triche est traversé **à chaque image** (établi dès la V107, qui a dû y
poser un garde de ré-entrée). Y écrire cinq octets pendant que le processeur les
exécute est une course : gagnée presque toujours, perdue de temps en temps — et le
jeu exécute alors une instruction à moitié réécrite. Cela explique le « 1er passage
réussi, 2e fatal », la V145 avec ses deux installations coup sur coup, et pourquoi
les V146 et V147 n'ont rien changé : elles corrigeaient le contenu, pas
l'installation.

`PatchCodeSafely` suspend désormais tous les threads du jeu, vérifie qu'aucun n'est
dans la zone visée, écrit, puis les relance — sur les 12 installations et les 19
remises en état du trainer.

Et, sur demande du joueur, les **soldats créés quittent les fenêtres d'armes** :
sans fiche, ils ne peuvent pas soutenir l'état « je tiens une arme », et les mêler
aux ralliés entretenait la confusion.

## V148 — deux diagnostics faux, une mesure pour trancher (5 septembre 2026, 13:44:07)

Les V146 et V147 ont chacune avancé une cause à l'arrêt du jeu à la distribution
d'arme, et **les deux étaient fausses** : après correction, le jeu a planté à
l'identique, et le journal de la V147 ne porte aucune ligne « morts ou disparus »
— la revalidation n'a rejeté personne.

Ce que les journaux établissent, en revanche : `resultat=0` n'est pas un refus mais
« le jeu est entré dans le code injecté et n'en est jamais ressorti » ; et ce n'est
**jamais le premier passage, toujours le second**, quel que soit l'effectif.

Le code injecté écrit désormais, avant chaque appel, le rang de l'homme traité et
l'étape en cours (`AddItem`, `Reload`, `SetSelectedInvItem`). Le trainer les relit
toutes les 2 ms et les journalise si le code ne rend pas la main. Un homme par
passage, 400 ms de repos, et arrêt de la file au premier échec.

## V147 — des pointeurs vers des acteurs morts au combat (5 septembre 2026, 13:34:42)

Le joueur suppose que la quantité de munitions est en cause. **Écartée** :
`AddItem` range `num` dans un champ entier, il n'alloue pas `num` objets — et le
premier paquet a réussi avec exactement les mêmes 978 munitions.

La vraie cause : `resultat=0` ne veut pas dire « refusé » mais « le jeu est entré
dans le code injecté et n'en est jamais ressorti ». La liste des ralliés n'était
nettoyée que **lorsque la fenêtre G était ouverte** — vingt-sept secondes de combat
s'étaient écoulées depuis le dernier nettoyage, des hommes étaient morts, et le
trainer a écrit dans leur mémoire libérée.

Le nettoyage tourne désormais à chaque image, chaque homme est revalidé au moment
où on le touche (vtable dans le module, type, vivant), et les ordres par la carte
sont protégés de la même façon — eux aussi sont armés puis consommés plusieurs
secondes plus tard.

## V146 — l'arrêt du jeu à la distribution d'arme (5 septembre 2026, 13:28:09)

L'ordre par la carte est validé par le joueur. Mais choisir une arme dans la
fenêtre J a arrêté le jeu, et son journal en donne la cause à la milliseconde :
**le miroir et la fenêtre J ont distribué tous les deux**, sur les mêmes 25
ralliés, dans la même image — et **les 25 tenaient déjà cette arme**.

Or sortir une arme n'est pas une écriture : `SetGun` libère le modèle courant, en
crée un neuf, le **charge** et l'accroche à la main. Cinquante chargements de
modèle depuis un détour, dans une seule image. Le jeu a tenu deux secondes.

Trois corrections : une **file unique** alimentée par les deux mécanismes, un
traitement par **paquets de 4 espacés de 250 ms**, et le **saut** de tout homme
qui tient déjà l'arme demandée — ce qui, dans ce cas précis, aurait supprimé la
totalité du travail.

## V145 — la carte pour commander, et le miroir d'arme pour les ralliés (5 septembre 2026, 13:11:09)

Le joueur valide le ralliement : « OUI ça marche parfaitement ». Restaient deux
manques, tous deux causés par un couplage abusif.

**L'ordre par la carte** était suspendu à la case « téléportation par la carte »
(`LBUTTON_EDGE enabled=0` dans son journal). Or `ApplyPendingTeleport` traite déjà
l'ordre en priorité sur la téléportation — les deux fonctions sont indépendantes.
Un drapeau `map_usable = teleport_map_enabled || map_serves_orders` remplace la
case aux cinq points de contrôle : dès qu'une troupe est armée, la carte répond à K
et au clic, case cochée ou non.

**Le miroir d'arme** ne visait que les soldats créés ; les ralliés n'y figuraient
pas. Il couvre désormais les deux troupes, avec la distinction établie : les créés
reçoivent l'arme dans le sac, les ralliés la **sortent**.

## V144 — la limite de trente-cinq hommes, et le compte faussé (5 septembre 2026, 12:58:16)

Le journal du joueur valide le ralliement — 1, 25 puis 40 ennemis passés de son
côté, groupe écrit **et relu**, 67 ennemis comptés, aucun plantage — et révèle deux
bugs.

**L'ordre de déplacement plafonnait à 35 hommes.** Le code était écrit déroulé, 21
octets par soldat, et un garde refusait tout code atteignant 768 octets. `26 →
resultat=1`, `66 → resultat=0`. Le code devient une boucle sur un tableau
d'acteurs : 74 octets constants au lieu de 21 × N, plafond porté à 768.

**« 66 sur 63 ennemis ».** La purge ne retirait un rallié que si son type d'acteur
avait changé, or un ennemi détruit garde son type. Les disparus restaient comptés
et faussaient le calcul des hostiles restants. Le critère devient la présence dans
la liste d'acteurs du jeu.

## V143 — le plus proche d'abord, et « venir à moi » (5 septembre 2026, 12:45:10)

Le joueur a demandé si un ennemi rallié venait se placer à côté de lui. Non : le
ralliement n'écrit qu'un entier, l'acteur ne bouge pas. Mais la question a révélé
un défaut réel de la V142 — les ennemis étaient pris dans l'ordre de la liste
d'acteurs du moteur, sans rapport avec la distance, si bien que rallier un seul
homme pouvait en désigner un à l'autre bout de la carte, et le joueur aurait
conclu à un échec sans rien avoir vu.

Ils sont désormais classés **du plus proche au plus loin**, la fenêtre affiche la
distance du premier (`RALLIER 1 ennemis (le plus proche a 23 m)`), et une ligne
`VENIR` les fait se déplacer jusqu'au joueur sans passer par la carte — même ordre
`AddProgram(PRG_MOVE)`, avec sa position comme destination.

## V142 — rallier les ennemis : le chemin certain (5 septembre 2026, 12:38:22)

La mise en main d'une arme sur un soldat **créé** est abandonnée, sur décision du
joueur après le test de la V141 : un acteur créé n'a pas de fiche, et le jeu ne
remplit une fiche qu'au chargement d'une mission. Huit tentatives, huit échecs.

Un ennemi de la mission, lui, a été fabriqué par le jeu — fiche complète, arme en
main, posture, animations, IA. Le faire changer de camp n'écrit **qu'un entier** :
`TAB_I_ENM_GROUP` (propriété 74), lu par les deux fonctions d'hostilité et par la
boucle de vision de l'IA. Écrire `1` le rend non hostile au joueur et hostile aux
Allemands, des deux côtés à la fois.

La fenêtre G compte les ennemis de la mission, laisse taper combien en rallier, et
permet de les libérer. La fenêtre J vise les deux troupes — et les ralliés
**sortent** l'arme, par les fonctions natives du moteur, parce qu'eux ont la fiche
qui soutient cet état. Les ordres passent par `AddProgram(PRG_MOVE)`, mécanisme
natif des ennemis. Sur le radar, un rallié passe en allié et sort des aides à la
visée.

**Limite annoncée :** piloter un rallié au clavier est impossible — `SetActive`
n'est surchargée que par `C_player`, `C_enemy` hérite d'une version vide. Le
commandement se fait par la carte.

## V141 — l'arme en main, par la fiche et `TableUpdate` (4 septembre 2026, 20:36:03)

Sept façons de mettre l'arme dans la main des soldats créés ont été essayées,
toutes fatales, et toutes procédaient de la même manière : forcer l'état depuis
l'extérieur. Le moteur ne procède jamais ainsi — il remplit la **fiche** du
soldat puis l'applique, et c'est `TableUpdate` qui vide l'inventaire, y remet ce
que la fiche indique et **désigne le premier poste**, donc le met dans la main
(`if(!i) SetSelectedInvItem(1, false)`).

La seule chose qui manquait à un soldat créé était le contenu de sa fiche :
`MissionLoad` d'un humain se réduit à `tab->Open(...)`, et le constructeur lui
avait déjà donné animations, main et inventaire de base.

La recopie de fiche que le joueur avait choisie s'avère **obligatoire** et non
un confort : `C_player::TableUpdate` recalcule la santé depuis l'endurance de la
fiche, et sur une fiche vide `Max(1, 200-600)` vaut 1 point de vie.

`TableUpdate` est à `+0xBC`, établi par lecture des vtables et non par déduction :
`+0xB4` porte `GetTable`, `+0xB8` rend une constante, `+0xBC` porte trois
implantations distinctes — une par classe d'humain. Le code machine injecté a été
vérifié hors du trainer, et l'appel est refusé si un seul acteur visé ne présente
pas de méthode valide à ce rang.

## V140 — la fiche mesurée à +0x1A4, et la garde qui était déjà posée (4 septembre 2026, 20:15:59)

Le dossier `source` a tranché. `itabler2.dll` contient `C_table::Item`, et sa
lecture montre que **le moteur ne range aucune taille de données** — or le
trainer exigeait un champ de taille en `+0x28`, repris d'ailleurs, et rejetait
chaque candidat sur ce critère. C'était la cause unique des quatre mesures
ratées. La fiche d'un acteur est à **`+0x1A4`**, et le numéro de visage a été
écrit sur les six soldats créés : c'est lui qui décide du nom, et zéro valait
« unknown ».

Le même journal montrait les cinq fonctions du bandeau introuvables, et donc
des soldats créés rendus **invulnérables** — une capacité validée, défaite. La
lecture de la mémoire vive du jeu a donné la raison : **la garde était déjà
posée** par une exécution précédente du trainer, sur un jeu non relancé. Les
cinq premiers octets étaient remplacés par un saut. La recherche porte désormais
sur la queue du prologue, qu'un détour ne recouvre jamais, et reconnaît une
garde déjà en place au lieu de la déclarer absente.

`SetPlayerFace`, « non identifiée » depuis la V129, est trouvée : Deluxe empile
un registre de plus que la version 2002, ce qui décale l'argument de `0x1C` à
`0x20`. Le décalage constant `0x99C0` qui relie `SetHealth` et `SetDeathFace` à
leurs adresses de 2002 désigne, pour elle, du code sans rapport — sixième échec
de la transposition. L'adresse retenue vient de la lecture.

**À faire avant d'essayer : relancer le jeu**, pour repartir d'une mémoire sans
les détours des versions précédentes.

## V138 — l'offset de la table est écrit dans le moteur : je le lis (4 septembre 2026, 19:52:25)

Quatre mesures, quatre échecs — trois par un critère faux, la quatrième par une
forme de code introuvable. Je cherchais compliqué : le moteur donne la réponse
en clair.

`C_human::GetTable(int index)` tient en six instructions :

```
8B 44 24 04          mov eax,[esp+4]        ; l'index demandé
85 C0                test eax,eax
75 09                jnz  (index ≠ 0)
8B 81 90 01 00 00    mov eax,[ecx+0x190]    ; return tab
```

**Le déplacement de la dernière instruction *est* l'emplacement de la table.**
Le trainer lit la vtable du soldat du joueur, cherche dans les rangs `+0xB0` à
`+0xC0` la fonction présentant cette forme, et en extrait le nombre. Rien n'est
déduit ni balayé : c'est l'instruction que le moteur exécute lui-même.

C'est la **cinquième** fois dans ce dossier qu'une lecture du code remplace
avantageusement une déduction, après `SetFrame` (prologue), `menu_id`
(instruction de rangement), la base d'inventaire (`mov ecx,[ebp+8]` dans
`AddItem`) et le groupe d'ennemi (les deux fonctions d'hostilité).

Si la lecture aboutit, elle débloque dans l'ordre : **rallier un ennemi** (une
écriture d'entier), **le nom** des soldats créés, et **la recopie de table**.

## V137 — la mesure de la table, avec enfin une vraie vérification (4 septembre 2026, 19:46:19)

**Le critère, pas la mesure, était en cause.** Les trois tentatives précédentes
validaient l'emplacement par le *numéro de visage* — or en partie réseau le nom
du soldat actif vient du réseau (`Actors.cpp:12410`), et le visage peut
légitimement valoir zéro. Le critère rejetait donc forcément tout.

**Le nouveau critère vient du code de l'hostilité.** Les deux fonctions qui
décident qui est ennemi de qui lisent le même entier :

```cpp
// C_player::IsEnemy(asker)
case ACTOR_ENEMY:
   return (asker->GetTable(0)->ItemI(TAB_I_ENM_GROUP) == 0);

// C_enemy::IsEnemy(asker)
int my_group = tab->ItemI(TAB_I_ENM_GROUP);
case ACTOR_PLAYER:  return (my_group == 0);
case ACTOR_ENEMY:   ... return (my_group != his_group);
```

Ce nombre ne peut valoir que **0** (allemand), **1** (russe) ou **2** (civil,
ami de tous), et tout ennemi en porte un. Le trainer relève les ennemis vus par
le radar et n'accepte un emplacement que si le groupe y tient dans ces bornes
chez **tous** — une contrainte qui ne se produit pas par hasard sur huit
acteurs.

**Ce que la mesure débloque**, dans l'ordre : le **nom** (le « unknown » vient
du visage à zéro) ; la **recopie de table** depuis un vrai soldat — sûre, car
les descripteurs portent une taille de chaîne, donc les textes sont *dans* le
bloc de données et rien n'est partagé ; et le **ralliement d'un ennemi** en
écrivant `1` dans son groupe, ce qui le fait changer de camp des deux côtés à
la fois en gardant arme, posture, animations, nom et IA.

## V136 — pourquoi les soldats créés ne peuvent pas porter l'arme (4 septembre 2026, 19:30:08)

**La réponse, établie par les journaux du joueur et non supposée.**

Ce qui marche, prouvé : l'arme entre dans l'inventaire avec sa réserve et son
chargeur (`30(1+0) 14(79+32) 71(368+20) 101(978+0)`) ; leur main existe et est
vide (témoin : eux `1`, le joueur `2`) ; et l'équipement lui-même **aboutit**
(`selectionne=1` atteint en V133).

**Le point décisif** : le jeu meurt toujours **après** que l'opération soit
terminée.

| Version | Méthode | Résultat |
|---|---|---|
| V116/V117 | écriture directe de l'index, **sans** appel au moteur | mort ~400 ms plus tard |
| V133 | appel natif complet, qui **réussit** | mort juste après |

Deux chemins opposés, la même mort, toujours après coup : **ce n'est pas
l'opération qui plante, c'est l'état qui en résulte** — un soldat créé qui
*tient* une arme.

Porter une arme met le moteur en posture de visée et d'animation. Un acteur
créé n'a jamais reçu `MissionLoad`, qui prépare posture, animation et
identité — c'est déjà pour cela qu'ils sont **écrasés au sol**. On leur demande
ensuite une posture de tir qu'ils n'ont pas, et le moteur tombe au tour
suivant. **C'est la même pièce manquante.**

La ligne de la fenêtre J l'annonce désormais telle quelle plutôt qu'avec un
avertissement vague. Tout le reste — créer, placer, commander, **donner** des
armes avec leurs munitions — reste sans risque.

## V135 — le témoin tranche, et « sortir l'arme » devient un essai volontaire (4 septembre 2026, 19:24:05)

**Le témoin dit l'inverse de ce que je croyais :**

```
Mains: soldats crees = 1 1 1 1 1 1 | VOTRE soldat = 2.
(0 = main introuvable, 1 = main vide, 2+ = main occupee.)
```

Le soldat du joueur rend **2** — main trouvée, arme dedans : la recherche
fonctionne et le rang de la méthode est bon. Les soldats créés rendent **1** —
leur main **est** trouvée, et elle est **déjà vide**, exactement ce que
`SetGun` attend. Le diagnostic de la V134 était donc faux : `Duplicate`
reproduit bien la main. En V133, `SetGun` allait jusqu'au bout — création du
modèle, **chargement depuis le disque**, accrochage — et c'est ce chemin qui
arrête le jeu.

**Plutôt que de deviner une septième fois**, l'essai cesse d'être imposé :
« SORTIR l'arme » devient une **ligne à part** de la fenêtre J, déclenchée par
le joueur. Créer des soldats et leur *donner* des armes reste sans risque —
acquis et confirmé par son journal ; seule cette ligne peut arrêter le jeu, au
moment qu'il choisit.

**Acquis confirmé** : `4 objet(s), 30(1+0) 14(79+32) 71(368+20) 101(978+0)` —
quatre armes avec réserve et chargeur, trois distributions successives, aucun
arrêt.

## V134 — l'équipement a marché, et voici ce qui tue le jeu juste après (4 septembre 2026, 17:54:41)

**`selectionne=1`.** Pour la première fois les soldats créés *tenaient*
réellement l'arme 71 avec ses 368 munitions — ce que cinq tentatives
précédentes n'avaient jamais atteint, parce qu'elles portaient sur un
inventaire vide. Puis le jeu s'est arrêté, et la cause est dans la même page :

```
Mains des soldats: 0 main(s) videe(s) sur 5 soldat(s) execute=1.
```

La main n'est pas trouvée. `SetGun` fait `if(!hand) return;` — donc **aucun
modèle d'arme n'est attaché** — alors que `SetSelectedInvItem` a *déjà* posé
l'index de sélection avant de l'appeler. Le soldat déclare tenir une arme
inexistante, et le moteur la déréférence au tour suivant. L'équipement est donc
coupé, mais on sait maintenant exactement ce qui manque.

**La mesure qui manquait : un témoin.** « 0 main vidée » ne prouve rien tant
qu'on n'a pas vérifié que la recherche fonctionne — j'ai failli conclure que la
main des copies est vide, ce qui aurait été une erreur du même genre que les
précédentes. Le soldat du joueur *tient* une arme : sa main existe forcément.
Il est ajouté à la mesure en dernière position, relevé et **jamais touché**.

```
Mains: soldats crees = 0 0 0 0 0 | VOTRE soldat = 2.
```

Témoin ≥ 2 et copies à 0 → `Duplicate` ne reproduit pas la main. Témoin à 0
aussi → c'est le rang de la méthode de recherche qui est faux. **Un seul essai
tranche.**

## V133 — les bons drapeaux, et le premier essai valable de l'équipement (4 septembre 2026, 15:25:23)

**Les munitions sont bonnes**, le journal le prouve :
`30(1+0) 14(79+32) 71(368+20)` — réserve *et* chargeur, identiques à ceux du
joueur.

**« 0 main vidée » — c'était ma mesure qui était fausse.** Le stub a tourné
proprement (`execute=1`) et n'a rien trouvé ; j'aurais pu conclure que la main
est vide. L'en-tête du moteur dit autre chose :

```c
#define ENUMF_ALL       0x0ffff
#define ENUMF_WILDMASK  0x10000
```

`SetGun` cherche la main avec exactement `ENUMF_WILDMASK | ENUMF_ALL` =
`0x1FFFF`. Le trainer passait `0xFFFFFFFF`, activant une quinzaine de filtres
inconnus au-delà — la recherche rejetait tout. Corrigé aux drapeaux exacts.

**L'équipement est repris — et ce n'est pas une supposition de plus.** Les cinq
tentatives fatales (V116→V126) se sont toutes déroulées dans des conditions
qu'on sait désormais fausses : base d'inventaire à `+0x58`, mémoire déjà
corrompue, `menu_id` à `-1`. Le journal de la V130 l'a prouvé —
`1 objet(s), identifiants = 30` : **on demandait au moteur d'équiper une arme
qui n'était jamais entrée dans l'inventaire.** Aucune de ces tentatives n'était
un essai valable.

Les conditions sont aujourd'hui vérifiées une par une par le journal du
joueur : l'arme entre, avec ses munitions, la garde est posée, `menu_id` vaut
1000, les acteurs détruits sont purgés. C'est le **premier essai valable**.

## V132 — les munitions, et la main qui n'était pas vide (4 septembre 2026, 15:20:01)

**Une seule munition — erreur d'analyse.** La V119 avait ramené la quantité à
`1`, en raisonnant que le moteur passe « une arme ». Mais dans `S_item`, ce
nombre n'est pas un nombre d'exemplaires :

```cpp
struct S_item{
   int itm;                 // l'objet
   dword amount;            // la RÉSERVE de munitions
   dword bullets_in_stack;
};
```

C'est cette même `amount` que `weapon_mods` lit depuis des versions comme la
réserve de l'arme. En passant `1`, le trainer donnait **une balle**. Les
soldats reçoivent désormais la réserve de l'arme du joueur, minimum trente, et
le journal détaille les munitions objet par objet.

**Ils ne portent pas l'arme — la main n'était pas vide.** `C_human::SetGun`
contient `assert(!hand->NumChildren())` : le moteur **attend une main vide**.
Or un soldat créé est la copie du soldat du joueur, dupliquée alors que
celui-ci tenait une arme — sa main contient déjà une arme copiée que le moteur
ne connaît pas. La main est désormais vidée à la création, et le compte est
journalisé.

Les rangs employés (`FindChildFrame` +0x94, `NumChildren` +0x88, `GetChild`
+0x8C, `LinkTo` +0x84) viennent de la même énumération d'`I3D_frame`, dont
**trois points sont confirmés par le jeu** : `SetPos` rang 3 (téléportations),
`SetName`/`GetName` rangs 28-29 (le moteur rend « Soldat 1 »), `Duplicate`
rang 38 (l'uniforme). Ce n'est pas une transposition : c'est une déduction
ancrée sur trois points validés, et chaque étape se garde.

**Confirmé par le journal** : les armes arrivent et le jeu ne plante plus, y
compris en créant des soldats en plusieurs fois et en n'en visant qu'un — la
purge de la V131 tient.

## V131 — l'arme arrive enfin, et la liste jamais purgée (4 septembre 2026, 15:09:36)

**L'arme arrive.** Le journal du joueur, quatre distributions de suite :

```
Inventaire du soldat 1 : 3 objet(s), identifiants = 30 30 14   (demandée 14)
Inventaire du soldat 1 : 4 objet(s), identifiants = 30 30 14 71
Inventaire du soldat 1 : 5 objet(s), identifiants = 30 30 14 71 101
```

La base d'inventaire `+0x54` — celle cassée en V116 — était la bonne. Les
soldats reçoivent réellement l'arme choisie. Ils ne la sortent toujours pas
(`selectionne=0`), mais on est passé de « rien n'arrive » à « tout arrive ».

**Le dernier arrêt : `g_spawned_soldiers` n'était jamais purgée.** Le joueur
visait *un seul* soldat ; la liste conservait les pointeurs de soldats morts et
détruits par le moteur. Écrire dans un acteur libéré arrête le jeu. Chaque
acteur est désormais vérifié avant usage — table de méthodes dans le module,
champ de type annonçant toujours un joueur — et la purge a lieu à chaque tour
et avant chaque distribution. Une cible disparue donne un refus propre.

**Les noms : mon critère était faux pour le jeu en réseau.** Le moteur
(`Actors.cpp:12410`) rend le nom **du réseau** pour le soldat actif quand
`net != NULL` ; les propres soldats du joueur peuvent donc porter un numéro de
visage **nul** en toute légitimité. Exiger un numéro non nul chez tous rejetait
forcément tous les emplacements. Le critère ne vérifie plus que la structure,
et chaque candidat est journalisé avec le numéro qu'il donne.

## V130 — l'arme n'arrivait jamais : `AddItem` était cassé par moi (4 septembre 2026, 14:58:06)

**La mesure d'inventaire ajoutée en V129 a tout révélé** :

```
Inventaire du soldat 1 : 1 objet(s), selectionne=0,
identifiants = 30 (l'arme demandee etait 14).
```

Un seul objet — le `30`, celui que le constructeur du moteur donne à tout
humain. **`AddItem` n'ajoutait rien.** Les soldats n'ont jamais reçu l'arme :
ce n'était pas un problème de dégainage, il n'y avait rien à dégainer.

**Pourquoi.** En V116 j'avais « corrigé » la base de `C_inventory` de `+0x54`
à `+0x58`, en raisonnant que le vecteur commence par son pointeur de début. Le
désassemblage d'`AddItem` dit l'inverse :

```
8B E9         mov ebp,ecx          ; ebp = this (C_inventory*)
8B 4D 08      mov ecx,[ebp+8]      ; items.begin = this + 8
8B 45 0C      mov eax,[ebp+0x0C]   ; items.end   = this + 0x0C
```

Le vecteur commence à `this+8` — l'ancien MSVC place un membre allocateur de
quatre octets devant les pointeurs. Le début validé étant `acteur+0x5C`, la
base vaut **`0x5C − 8 = 0x54`** : exactement ce que les sites d'appel
indiquaient, et que j'avais cassé. **Quatorze versions passées à chercher
ailleurs.**

**Le plantage : 116 soldats vivants.** `50 + 21 + 10 + 10 + 10 + 10 + 5`. La
limite de cinquante ne portait que sur *une* création. Elle porte désormais sur
le **total vivant**, avec un message renvoyant vers `SUPPRIMER`.

**La garde tient.** `4 fonction(s) sur 5 protegee(s), dont les deux
indispensables` — la correction du « tout ou rien » fonctionne, l'identifiant
1000 est posé, les touches 1 2 3 4 sont rendues.

**La table résiste encore** (`NON MESURABLE`). Plutôt que d'élargir les
critères au hasard, l'emplacement est maintenant lu **dans le code** de
`TableUpdate` (`51 56 8B F1 57 6A ?? 8B 86 <déplacement>`), et un relevé du
descripteur est journalisé en cas d'échec pour dire laquelle des trois
hypothèses est fausse.

## V129 — transposer au lieu de mesurer : la même faute, trois fois (4 septembre 2026, 14:43:37)

**Le journal du joueur donne raison à sa critique.**

```
Garde bandeau: SetHealth a 00461960.            <- trouvée
Garde bandeau: SetDeathFace a 00461820.         <- trouvée
Garde bandeau: SetPlayerFace INTROUVABLE (0 candidates).
Garde bandeau: SetPrgKeyColor a 00461CD0.       <- trouvée
Garde bandeau: GetPrgKeyColor a 00461D00.       <- trouvée
Garde bandeau: REFUSEE.
Creation d'acteurs sur 5 : … identifiant 1000 pose chez 0 … Garde ABSENTE
Nom des soldats: aucun numero de visage lisible chez vos soldats
```

Quatre fonctions sur cinq trouvées, **une seule manquante**, et la garde était
écrite en « tout ou rien » : aucune garde posée, `menu_id` laissé à `-1`, donc
touches 1 2 3 4 volées et soldats redevenus invulnérables. **La régression du
switch vient de là**, et elle est de mon fait — j'avais lié des choses qui
n'avaient pas à l'être.

Les deux échecs ont la même cause : `SetPlayerFace` et l'emplacement de la
table (`+0x190`) étaient **transposés du binaire de référence** au lieu d'être
mesurés. C'est la troisième occurrence de cette faute dans ce dossier, après
`+0x54` pour l'inventaire.

**Ce qui change.**

1. **La garde est indépendante fonction par fonction.** Une fonction non
   identifiée est sautée ; le verdict qui gouverne `menu_id = 1000` ne dépend
   plus que de `SetHealth` et `SetDeathFace`, toutes deux trouvées.
2. **`SetPlayerFace` est cherchée par sa forme** — elle partage celle de
   `SetDeathFace` à deux octets près, laissés libres — et la candidate qui
   n'est pas `SetDeathFace` est retenue, ou aucune si plusieurs se présentent.
3. **L'emplacement de la table est mesuré et se vérifie lui-même** : il n'est
   retenu que si, pour *tous* les soldats du joueur, l'objet pointé présente la
   disposition attendue **et** que la propriété 65 y lit un numéro de visage
   plausible et non nul — ce qui valide d'un coup l'emplacement, la disposition
   et l'indice.
4. **L'inventaire des soldats est relu et journalisé** après chaque
   distribution : `resultat=1` ne prouvait que l'exécution, pas l'effet.

**Règle appliquée désormais** : aucune valeur du binaire de référence n'est
employée telle quelle. Elle se déduit d'une valeur que le jeu valide, ou se
mesure avec vérification, ou est refusée et journalisée.

## V128 — le « unknown » trouvé à la ligne près (4 septembre 2026, 14:32:07)

**Le nom affiché ne vient ni de la frame, ni du bandeau.** Il vient du **numéro
de visage** dans la table de l'acteur (`Actors.cpp:12417`) :

```cpp
int i = tab->ItemI(TAB_I_HUM_FACE);
if(!i)
   return "unknown";                    // littéralement
i = GT_GAME_MENU_SOLDIER_NAME + i - 1;
return all_txt[i];
```

La table d'un soldat créé est ouverte par le constructeur avec le modèle par
défaut, où ce numéro vaut **zéro** — et zéro *est* « unknown ». Le `SetName`
posé sur la frame était correct depuis le début, comme le journal le confirmait
à chaque création ; il ne servait simplement pas à ça.

Chaque soldat créé reçoit désormais un numéro relevé dans la table des
**propres soldats du joueur** — donc forcément valide — répartis en boucle pour
varier les noms. Rien n'est écrit si aucun numéro n'est lisible : inventer un
numéro enverrait le moteur chercher un nom hors de sa table de textes. La
mécanique de lecture/écriture des tables est celle que `weapon_mods` emploie
depuis des versions pour le recul des armes ; la table de l'acteur est à
`acteur+0x190`, relevé dans le prologue de `TableUpdate`.

**La garde du bandeau couvre maintenant cinq fonctions.** En relisant toutes
les méthodes publiques de `C_game_menu` prenant un identifiant de joueur :
`DestroyPlayerMenu` et `SetPrgList` se gardent elles-mêmes (`cmp eax,4 / jae`),
mais `SetPlayerFace`, `SetPrgKeyColor` et `GetPrgKeyColor` indexent `pmenu[]`
sans borne, comme `SetHealth` et `SetDeathFace`. `SetPlayerFace` est appelée
par le changement de visage — donc par le correctif ci-dessus : les deux vont
ensemble. Les cinq gardes sont posées d'un bloc, ou aucune.

**L'arme non dégainée — une piste nouvelle, non activée.** Le moteur suppose
une main vide avant d'y placer une arme (`assert(!hand->NumChildren())` dans
`SetGun`). Or les soldats créés sont des copies d'un joueur qui tenait une
arme : leur main contient déjà une arme dupliquée. C'est une piste sérieuse,
mais une piste — elle ne sera activée qu'après mesure.

## V126 / V127 — la garde sur le bandeau, et l'équipement définitivement abandonné (4 septembre 2026, 14:16:22)

**Le journal était illisible : un seul message l'occupait à 97 %.**
`Game speed: module info unavailable for hook install.` apparaissait **6569
fois pour 6222 lignes** (740 Ko), émis à chaque image dès que le jeu n'est plus
là. Bridé à une ligne toutes les dix secondes — le journal suivant fait 57
lignes.

**La cause commune de quatre défauts.** Perte des touches 1 2 3 4 en mission à
quatre soldats, soldats créés invulnérables, squelette sur un vrai portrait,
plantage à la grenade : les deux fonctions du bandeau lisent
`pmenu[identifiant]` **sans vérifier les bornes**.

```
SetHealth     8B 44 24 04     mov eax,[esp+4]        ; l'identifiant
              8B 74 81 08     mov esi,[ecx+eax*4+8]  ; pmenu[id]
SetDeathFace  8B 7C 24 1C     mov edi,[esp+0x1C]
              8B 44 BE 08     mov eax,[esi+edi*4+8]  ; pmenu[id]
```

Elles savent déjà ne rien faire quand la case est **nulle** ; il leur manquait
de ne rien faire quand l'indice est **hors du tableau**. Une garde de cinq
octets à l'entrée de chacune lève la contrainte à la racine. Les soldats créés
reçoivent alors l'identifiant **1000** — hors tableau — et les quatre défauts
tombent ensemble : ils se rangent **en dernier** dans le tri de `PlayerSwitch`
(touches 1 2 3 4 rendues, quelle que soit la taille de l'escouade), ils peuvent
**encaisser et mourir** partout, aucun portrait du joueur n'est marqué, et la
grenade emprunte le même chemin protégé. Fin du compromis « quatre soldats =
soldats invulnérables ».

**Le pari sur l'équipement, et son démenti.** La V126 avait supposé que, la
garde posée, les soldats pouvaient enfin *sortir* l'arme. Le journal a tranché :
`Garde bandeau: POSEE` … `Fenetre armes: … resultat=1` … `Process: pid=0`. La
garde était en place et le jeu s'est arrêté quand même. Ce n'est donc pas le
bandeau : c'est l'équipement lui-même, qui demande d'animer un squelette que
`MissionLoad` n'a jamais préparé. **Cinq tentatives, toutes fatales** ; la seule
qui ne l'est pas — déposer l'arme chargée sans la faire tenir — est validée
quatre fois par le journal. L'équipement ne sera pas retenté avant que
l'initialisation d'acteur existe.

## V125 — lecture complète du journal : quatre défauts trouvés, quatre corrigés (4 septembre 2026, 12:52:54)

**Défaut 1, grave — aucun soldat n'était réglé.** Trois fois dans le journal :

```
Bandeau: 4 case(s) reservee(s) … case de repli = -1.
Bandeau: … les soldats crees sont rendus INVULNERABLES …
Creation d'acteurs: 0 soldat(s) sur 5 regles …
```

Les trois écritures étaient chaînées par des `&&` :

```cpp
ok = ok && fallback >= 0 && WriteMemory(menu_id, fallback);
ok = ok && WriteMemory(no_hit_cheat, no_hit);
```

Avec `fallback = -1`, la condition rend faux et le `&&` suivant
**court-circuite** l'écriture de la garde. L'invulnérabilité annoncée n'était
donc **jamais posée** : les soldats restaient sans case de bandeau *et*
vulnérables, et le premier dégât relisait `pmenu[-1]`. C'est précisément
pourquoi la mission à quatre soldats plantait encore. Les trois écritures sont
désormais indépendantes, et le journal compte chacune séparément.

**Défaut 2 — le journal mentait.** Il annonçait « menu_id valide posé, ils
peuvent mourir » juste après avoir dit l'inverse, et alors que rien n'était
posé. Un message faux vaut moins que pas de message. Le nouveau compte ce qui a
réellement été écrit, poste par poste.

**Défaut 3 — la fenêtre des armes échouait** (`declenche=1 execute=0`,
`resultat=0`). Le miroir aboutissait toujours ; la différence est que la
fenêtre était **encore ouverte** au moment du déclenchement. Elle se referme
maintenant avant, avec une seconde tentative si besoin.

**Défaut 4 — deux lignes pour un seul appui sur G.** `G REPARATION VEHICULE`
suivi de `G a pied: ouverture de la fenetre des soldats`, cinq fois. La
première laissait croire à une réparation tentée à pied. Supprimée.

**Vérifié et non défectueux** : les états `*-unavailable` du radar (transitoires,
retour à `ready` neuf fois), `CreateActor: module principal illisible` au
lancement (réessayé, aboutit), les tests teleport/ESP désactivés.

## V124 — le cas « quatre soldats », et la fenêtre des armes (4 septembre 2026, 12:44:56)

**Le plantage à quatre soldats d'origine — de l'arithmétique.** Le bandeau
compte quatre cases. Avec quatre soldats d'origine elles sont toutes au joueur,
donc toutes interdites par la règle de la V123, et `AddPlayerMenu` rend `-1`
pour chaque soldat créé. Aucune case attribuable → `menu_id` restait à `-1` →
le premier dégât relisait `pmenu[-1]`.

Il n'existe pas de troisième voie : soit le soldat créé partage la case d'un
vrai soldat — et son squelette s'affiche sur ce portrait, ce que le joueur a
refusé — soit il ne peut pas encaisser. **Dans ce seul cas**, les soldats créés
sont rendus **invulnérables**, et le joueur en est averti au journal comme dans
la fenêtre. Avec trois soldats d'origine ou moins, ils restent mortels.

**La fenêtre des armes — l'idée du joueur, meilleure que le miroir.** `J`
ouvre une fenêtre : gauche/droite choisissent la **cible** (TOUS, GROUPE, ou
`Soldat N`), haut/bas choisissent l'**arme**, Entrée la donne. Les armes
proposées sont celles de l'inventaire du joueur — leur identifiant est donc
forcément valide, et il reconnaît ce qu'il porte ; le numéro d'objet est
affiché à côté, comme il l'avait proposé. La distribution emploie le chemin
validé par son journal : créer, **charger**, déposer, sans forcer l'équipement.

**Les noms.** Le moteur confirme `« Soldat 1 »` pour le nom de l'acteur. Le
« unknown » vient donc d'ailleurs — vraisemblablement du bandeau, qui affiche
l'identité de mission et non le nom de la frame. Reste à savoir *où* le joueur
le lit exactement : chaque emplacement lit une source différente.

## V123 — jamais la case d'un vrai soldat (4 septembre 2026, 12:36:29)

**Deux acquis confirmés par le journal du joueur.**

*Les noms fonctionnent* : `le moteur rend « Soldat 1 » pour le premier soldat`.
C'était bien la page rendue trop tôt qui détruisait les chaînes. Un « unknown »
résiduel ne viendrait donc plus du nom de l'acteur mais du bandeau, qui affiche
une identité de mission.

*Les armes ne tuent plus le jeu* : trois distributions d'affilée sans forcer
l'équipement, et le jeu tient 23 secondes de plus avant de tomber pour une
autre raison — là où les versions précédentes mouraient 400 ms après **chaque**
distribution. **C'était bien l'équipement forcé**, pas le fait de donner l'arme.

**Le squelette sur le soldat d'origine — la cause exacte.** Le journal la donne
mot pour mot : `menu_id 1 attribue (aucune case libre, portrait partage)` — la
case 1 étant celle du soldat 2 du joueur. Ce qui m'avait échappé : le
constructeur de chaque soldat créé appelle lui-même `AddPlayerMenu`, si bien
que les deux **premiers** créés prennent les cases 2 et 3 ; il n'y a alors plus
de case libre, et le repli choisissait « le plus grand identifiant valide ».

La règle est réécrite : **une case appartenant à un vrai soldat est
interdite**. Par ordre de préférence — la case que le constructeur a donnée au
soldat créé (elle est à lui), sinon une case nulle, sinon la case d'un *autre*
soldat créé. Jamais celle d'un soldat d'origine.

Cinq soldats créés écrivant tous dans le `C_plr_menu` d'un vrai soldat est
aussi un candidat sérieux pour le plantage « après un instant » ; la correction
l'élimine.

## V122 — une case de bandeau libre : plus de squelette, et la vraie cause du plantage (4 septembre 2026, 12:27:50)

**Ce que le journal montre.** Aucune arme ni cheat au moment des morts du jeu.
En revanche :

```
12:23:31  Creation d'acteurs: demandes=20 … crees=20    → mort 11 s après
12:24:31  Creation d'acteurs: demandes=15 … crees=15    → mort 65 s après
```

Ce sont les soldats **nombreux**, laissés dans le monde, qui tuent le jeu — et
ils partageaient tous **la même case de bandeau**, celle d'un vrai soldat.

**Le squelette sur le soldat d'origine — corrigé.** Conséquence directe du
choix de la V114 (`menu_id` d'un vrai soldat, pour rester dans les bornes) :
`SetDeathFace(menu_id)` marquait le portrait d'un soldat vivant. Le
désassemblage de `AddPlayerMenu` donne la solution — `pmenu` à `+0x08`,
`num_players` à `+0x18`, et **une case nulle est inoffensive** puisque
`SetHealth` et `SetDeathFace` commencent par `if(!pmenu[i]) return;`. Les
soldats créés prennent désormais une **case libre**, mesurée sur le binaire du
joueur. C'est très probablement aussi ce qui tuait le jeu à vingt soldats.

**Les noms — une erreur dans ma propre vérification.** La V121 lisait la page
*après* l'avoir mise en file de libération : le pointeur nul mesurait ma faute,
pas `SetName`. Et cette faute en cachait une plus grave — les chaînes vivaient
dans la page du stub, rendue aussitôt ; si `SetName` retient le pointeur au
lieu de copier, le nom devenait pendant. Les noms occupent maintenant une page
**dédiée et jamais rendue**, et la relecture précède toute libération.

**La touche J — la troisième voie.** Écriture directe de l'index (fatale) et
appel natif (fatal) ont été essayés ; **ne pas forcer l'équipement** ne l'avait
jamais été. Les mises à jour du bandeau étant conditionnées par `if(active)` et
un soldat créé n'étant pas actif, ce n'est pas le bandeau qui plantait : reste
l'équipement, qui demande d'animer un squelette jamais initialisé. L'arme est
donc créée, **chargée** (`Reload`) et déposée dans l'inventaire, sans forcer la
main du moteur. L'appel reste écrit, désactivé.

## V121 — la santé des soldats créés est celle du joueur (4 septembre 2026, 12:14:18)

**Le joueur a vu juste : ses soldats créés mettaient plus de temps à mourir que
lui, et c'était de mon fait.** Je leur écrivais 20000 points de vie, alors que
la santé normale d'un soldat vaut `200 + endurance × 1400` — quelques milliers
au plus. Ce 20000 répondait à un besoin réel — `init_resistance` vaut zéro sur
un acteur neuf et le moteur divise par cette valeur — mais la valeur elle-même
était arbitraire, et je ne l'avais pas signalée.

Les soldats créés **reprennent désormais la santé du soldat piloté**, relue à
chaque création : même `init_resistance`, même santé courante. Ils sont aussi
résistants que lui, ni plus ni moins, y compris quand la case Santé Max est
active.

**Les noms « unknown » : vérification plutôt que supposition.** `GetName()`
d'un acteur rend `frame->GetName()` (`H&D.h:885`), donc poser le nom sur la
frame devrait suffire — or le joueur voit toujours « unknown ». Plutôt que de
choisir au hasard entre « le rang de `SetName` est faux » et « le nom affiché
vient d'ailleurs », le trainer **relit le nom juste après l'avoir posé**, en
interrogeant le moteur, et l'écrit au journal. La prochaine ligne tranchera :

```
Nom des soldats: le moteur rend « Soldat 1 » pour le premier soldat
                 (attendu « Soldat 1 »).
```

## V118 → V120 — le miroir d'arme : quatre causes corrigées, puis coupé (4 septembre 2026, 12:05:25)

**Quatre causes distinctes trouvées et corrigées sur ce seul passage**, chacune
réelle et prouvée par les journaux du joueur :

| Version | Cause |
|---|---|
| V116 | base de `C_inventory` à `+0x54` au lieu de `+0x58` — `AddItem` écrivait à côté |
| V118 | index écrit à la main au lieu de la fonction native, seule à charger le modèle 3D (`S_item::model` est nul tant que `LoadModels` n'a pas tourné) |
| V119 | quantité de 200 à 1000 au lieu de **1** — confusion avec le nombre de balles |
| V119 | `C_inventory::Reload` jamais appelée, donc **arme vide** |

Le moteur fait bien trois choses (`Actors.cpp:7360`) : `AddItem`, sélection,
puis `Reload`. Avec les quatre corrections en place, le résultat n'a pas
bougé :

```
Reload: TROUVEE a 00463F20 (une seule candidate).
Miroir d'arme: objet 101 x1 … execute=1, 5 soldat(s) la tiennent.
Process: pid=0                      ← 400 ms plus tard
```

**Conclusion : ce n'est pas la façon de donner l'arme, c'est l'acteur qui la
reçoit.** Les soldats créés n'ont jamais été initialisés par `MissionLoad` — la
même pièce manquante qui les laisse écrasés au sol et sans identité. Un acteur
dans cet état ne peut pas se servir d'un objet.

**Le miroir est donc coupé**, plutôt que de laisser une touche qui ferme le
jeu. Tout le code reste en place ; le refus est un `return` unique, à retirer
le jour où l'initialisation d'acteur existera.

## V117 — ce n'est pas la création qui plante, c'est la touche M (4 septembre 2026, 11:56:26)

**Le journal innocente le miroir d'arme** : trois armes distribuées de suite,
`execute=1`, `5 arme(s) designee(s)` à chaque fois. La correction des quatre
octets de la V116 tient. Puis :

```
[TEST FULLHANDS #1] INPUT source=M …
Fullhands: trigger sent, waiting for completion.
Fullhands: completion_state=0 selected=0 after 1000 polls.
Process: pid=0 (was pid=10700)
```

**`Fullhands` agit sur le soldat piloté — et le joueur pilotait un soldat
créé.** Ce cheat rejoue un chemin du moteur écrit pour un soldat de *mission* :
il lit sa table, son entrée de bandeau, son inventaire de départ. Un soldat
créé n'ayant pas été initialisé par `MissionLoad`, ces données n'existent pas
et le jeu s'arrête. Le risque est commun à tous les cheats natifs.

**Les cheats natifs sont désormais refusés** quand le joueur pilote un soldat
créé — `Fullhands` et tous les rappels passant par `ApplyNativeActorCallback`
(F3, ranimation, restauration d'image…). Le journal explique et renvoie vers la
ligne `REVENIR a mon soldat d'origine`.

Offsets acquis, **mesurés sur le binaire du joueur** : `SetFrame` vtable
`+0x88`, `menu_id` `+0x2B0`, base d'inventaire `+0x58`, `CreateActor`
`00437EB0`, `AddItem` `00463DE0`, `AddPlayerMenu` `00460C20`.

## V116 — le plantage à la création : une erreur de quatre octets (4 septembre 2026, 11:51:02)

**Le journal du joueur montre que tout a abouti, sauf la dernière ligne.**
`menu_id` **mesuré chez lui à `+0x2B0`** (deux appelants d'accord), cinq
soldats créés, placés en un passage et **nommés**, `AddItem` retrouvée — puis
le jeu s'arrête juste après `Miroir d'arme: … execute=1`.

**La cause : quatre octets.** La base de `C_inventory` dans un soldat était
relevée à `+0x54` sur le binaire de référence, par les `lea ecx,[objet+0x54]`
précédant les appels à `AddItem`. C'était une **transposition**, et elle était
fausse. La déclaration de la classe tranche :

```cpp
class C_inventory{
   class C_game_menu &game_menu;              // 4 octets
   vector<C_smart_ptr<S_item> > items;        // début, fin, capacité
```

`items` est le **second** membre : son début est à `base + 4`. Or le début du
vecteur est en `+0x5C` — valeur que le trainer emploie depuis des versions et
que le jeu valide à chaque tir. La base vaut donc `0x5C − 4 = +0x58`. `AddItem`
était appelée avec un pointeur décalé de quatre octets et manipulait le vecteur
d'objets à côté, en pleine mémoire du soldat.

La valeur n'est plus écrite en dur : elle est **déduite** de l'emplacement
validé.

**Une seconde inconnue retirée.** Le même passage appelait aussi la fonction
native qui désigne l'arme tenue — écrite pour le soldat piloté, et touchant
vraisemblablement l'affichage. Plutôt que corriger deux inconnues à l'aveugle
en même temps, l'appel est remplacé par une simple écriture de l'index choisi
après coup. Le journal indique désormais combien d'armes ont été désignées.

## V115 — armes en miroir, touches 1 2 3 4 rendues, soldats nommés et mortels (4 septembre 2026, 11:40:07)

**`menu_id` mesuré par le code — la pièce qui débloque trois défauts d'un
coup.** Le journal de la V114 disait `menu_id non mesurable, protection
maintenue (immortels…)` : la mesure statistique exigeait au moins trois soldats
vivants et échouait, donc le repli s'appliquait — d'où **soldats immortels,
touches 1 2 3 4 volées et plantage à la grenade, tous les trois en même
temps**.

La nouvelle méthode ne dépend plus de l'état de la partie. Le constructeur de
`C_player` fait `menu_id = mission.game_menu.AddPlayerMenu(1)`, ce qui se
compile en un appel suivi du rangement du résultat ; dans le binaire de
référence, les deux sites appelants sont suivis du même
`mov [esi+0x29C], eax`. Le trainer retrouve `AddPlayerMenu` chez le joueur par
empreinte, repère ses appelants et **lit le déplacement** de l'instruction de
rangement. Une fois le `menu_id` valide posé :

- plus de lecture hors du tableau `pmenu[]` → **plus de plantage, grenade
  comprise** ;
- `no_hit_cheat` retiré → **les soldats encaissent et meurent** ;
- le plus grand identifiant valide les range en fin de la liste triée →
  **les touches 1 2 3 4 retrouvent les soldats d'origine**.

**Les armes en miroir.** Les soldats créés portent l'arme du joueur et en
changent avec lui, instantanément. **J** fige le miroir — ils gardent ce qu'ils
ont — et un second appui les remet en suivi ; l'état est affiché sur sa propre
ligne dans la fenêtre G. Toutes les pièces étaient déjà validées par le jeu
(inventaire `+0x5C`/`+0x60`, index `+0x258`, objet `+0x08`/`+0x18`/`+0x1C`) ;
il ne manquait que `C_inventory::AddItem`, reconnue par empreinte. Le rang de
la base `C_inventory` se recoupe deux fois : `lea ecx,[objet+0x54]` sur quatre
des douze sites appelants, et `0x54 + 0x08 = 0x5C`, l'emplacement du vecteur
que le jeu valide à chaque tir.

**Fini les « unknown ».** Chaque soldat créé reçoit son nom — `Soldat 1`,
`Soldat 2`… — posé sur sa frame par `SetName`.

**La fenêtre G complétée.** Chaque soldat a désormais **deux** lignes : une
pour le piloter, une pour l'envoyer **seul** sur un point de la carte. `GROUPE`
et `TOUS` gardent leur ordre collectif. Le pied de fenêtre rappelle la
séquence : Entrée, puis `K`, puis un clic sur la carte.

## V114 — placement en un seul passage, et les soldats meurent de nouveau (4 septembre 2026, 03:04:31)

**« Certains à côté, les autres loin » — la cause n'était pas le calcul des
positions mais la façon de les poser.** La V110 appelait la routine de
téléportation *une fois par soldat* ; chaque appel pose un trampoline, le
déclenche, attend jusqu'à une seconde, puis restaure. Avec douze soldats la
séquence s'étire et certains appels n'aboutissent pas — le soldat reste où le
moteur l'avait mis. **Un seul passage** pose désormais toutes les positions :
soit tous sont placés, soit aucun, et le journal le dit.

**« Pourquoi ces soldats ne meurent pas » — c'était ma garde de la V113.**
Le `no_hit_cheat` posé pour empêcher le plantage les rendait immortels : un
pansement, pas un correctif. La vraie réponse est de leur donner un `menu_id`
**valide**, et cet emplacement n'est pas deviné mais **mesuré** sur les soldats
du joueur — ils portent forcément des identifiants distincts entre 0 et 3, et
un seul emplacement de la structure présente cette propriété sur trois soldats
ou plus. Chaque soldat créé reçoit alors le plus grand identifiant valide :

- plus de lecture hors du tableau `pmenu[]` → le jeu ne s'arrête plus ;
- `no_hit_cheat` retiré → **les soldats créés encaissent et meurent** ;
- le plus grand identifiant les range **en fin** de la liste triée par
  `PlayerSwitch` → les touches 1 2 3 retrouvent les soldats du joueur.

Contrepartie assumée : ils partagent la jauge du dernier soldat. Défaut
d'affichage, de loin le moindre des trois maux. Si la mesure n'aboutit pas, on
retombe sur la protection de la V113 — mieux vaut des soldats immortels qu'un
jeu qui s'arrête — et le journal dit lequel des deux cas s'applique.

## V112 / V113 — le jeu ne s'arrête plus sous le feu ennemi (4 septembre 2026, 02:50:12)

**Le plantage quand les ennemis frappent — trouvé.** Dès qu'un joueur encaisse,
le moteur appelle `SetResistance`, qui appelle `C_game_menu::SetHealth` :

```cpp
void C_game_menu::SetHealth(dword plr_id, float f){
   assert(pmenu[plr_id]);
   if(!pmenu[plr_id]) return;      // lit pmenu[-1] AVANT de se garder
   pmenu[plr_id]->SetHealth(f);    // puis appelle sur ce pointeur
}
```

Aucune borne. Le constructeur de `C_player` pose
`menu_id = AddPlayerMenu(1)`, qui rend `-1` dès que les quatre places du
bandeau sont prises : sur un soldat créé en surnombre, le moteur lit
`pmenu[0xFFFFFFFF]`, hors du tableau, et appelle une méthode virtuelle sur ce
qu'il y trouve. Le moteur offre lui-même la coupure —
`case CB_HIT: if(no_hit_cheat) return 0;` — et chaque soldat créé la reçoit
désormais, avec des résistances valides (`init_resistance` valant zéro sur un
acteur non initialisé). **Réserve** : `C_player::Explode` atteint
`SetResistance` par un autre chemin, donc une grenade peut encore arrêter le
jeu tant que l'initialisation n'est pas faite.

**Touches 1 2 3 4.** Les deux façons de changer de soldat passent par
`PlayerSwitch(bool suivant, int id)`, mais `|| id != -1` fait entrer **tous**
les acteurs joueur sur le chemin des touches directes. La liste est triée par
`GetMenuID()` ; les soldats créés portant `-1` passent en tête, puis
`if(!slist[id]->IsAlive()) return -1` rejette. Les soldats officiels ne sont
pas cassés : leur **rang** a été déplacé. Remède livré : une ligne
`SUPPRIMER n soldats crees` via `C_game_mission::DestroyActor`.

**Soldats sous la terre.** La grille de placement s'étirait sur vingt mètres en
réutilisant la hauteur du joueur pour tous. Remplacée par des **couronnes** —
douze par couronne, première à 2 m, pas de 1,5 m — cinquante soldats tiennent
dans huit mètres.

## V110 / V111 — nombre exact, position juste, fenêtre enfin lisible (4 septembre 2026, 02:38:06)

**Confirmé par le joueur en V110 : le nombre créé est exactement celui demandé,
et les soldats apparaissent devant lui.** Deux causes, toutes deux réelles.

**Le nombre.** Le trampoline est posé sur un site que le jeu traverse à chaque
image. Le déclenchement ne provoquait qu'un *premier* passage ; tous les
suivants, pendant la seconde où le trampoline restait en place, **réexécutaient
la boucle entière**. Une garde de réentrée le rend à usage unique.

Pire : **le journal donnait tort au joueur, et c'était ma faute.** Le compteur
de créations était plafonné à la valeur demandée avant d'être écrit —
`crees=4` s'affichait fidèlement quel que soit le désastre. Le plafond est
retiré ; une anomalie est désormais signalée explicitement.

**La position.** `SetPos` place une frame **relativement à son parent**, alors
que la position connue du trainer est une position de **monde**. Le placement
passe maintenant par `SetFramePositionOnMainThread`, la routine que le jeu
valide à chaque téléportation.

**Les textes illisibles — un défaut de dessin, mesurable.** Chaque ligne était
dessinée dans une boîte de **48 px** avec une police de **30 px**, mais les
lignes étaient espacées de **26 px** : chacune empiétait de 22 px sur la
suivante. Elles se chevauchaient. Désormais : panneau opaque, interligne égal à
la hauteur de boîte, police à chasse fixe, alignement à gauche, marqueur `->`
sur la ligne choisie, quatorze lignes visibles.

**La saisie du nombre est vérifiable.** Chaque chiffre frappé est écrit dans le
journal (`chiffre 1 frappe, nombre = 1 (max 50)`), et la ligne choisie affiche
`< tapez le nombre`.

## V109 — placement, retour au soldat d'origine, fenêtre lisible (4 septembre 2026, 02:28:34)

**Le journal contredit une des impressions du joueur, et c'est important.**

```
Creation d'acteurs (type 1): demandes=5 declenche=1 execute=1 crees=5.
Creation d'acteurs (type 1): demandes=2 declenche=1 execute=1 crees=2.
```

Le nombre créé est exactement celui demandé. Le « nombre illimité » observé est
le **bandeau et la liste qui se remplissent sans jamais se vider** : chaque
soldat créé reste dans l'effectif jusqu'à la fin de la mission.

**Soldats très loin du joueur — corrigé.** Après duplication, le modèle doit
être raccroché à la branche de scène du soldat source. Le parent était lu à
`frame+0x18`, un emplacement **supposé** ; s'il ne désigne pas le parent, le
soldat est raccroché n'importe où. Le parent est maintenant **demandé au
moteur** par `GetParent()`, dans le stub. Que les soldats soient apparus *avec
l'uniforme du joueur* prouve que `Duplicate` (+0x98) est juste, donc que
l'énumération d'où sort `GetParent` (+0x80) l'est aussi.

**Retour au soldat d'origine — ajouté.** Le trainer retient qui était piloté
avant le premier changement et la fenêtre porte une ligne
`REVENIR a mon soldat d'origine`. Le joueur n'est plus prisonnier des soldats
créés.

**Fenêtre réécrite.** Les libellés dépassaient la largeur et étaient coupés.
Ils sont courts, le détail des touches figure une seule fois en pied de
fenêtre, et **aucune mention de flèche ne subsiste** : le nombre se tape.
Les lignes passent par une table d'actions au lieu d'indices en dur, si bien
qu'ajouter une ligne ne décale plus rien.

**Non livré, et dit franchement.** Les soldats écrasés au sol et leur présence
parasite dans le bandeau sont **le même défaut** : le moteur enchaîne
`CreateActor` → `SetFrame` → **`MissionLoad`**, et cette dernière étape manque.
Sans elle l'acteur n'a ni posture ni identité. Sa variante courte
(`TableUpdate`) commence par lire une table à `[acteur+0x190]` et appeler une
méthode dessus — table nulle sur un acteur neuf, donc plantage assuré. Le choix
d'arme repose sur la même pièce manquante : l'inventaire fait partie de cette
initialisation. Ces deux points attendent que les adresses soient établies par
empreinte, pas devinées.

## V108 — nombre tapé au clavier, groupe au choix, et la raison du « rien » (4 septembre 2026, 02:08:04)

**Le journal de 02:04 a tranché la V107.** Il dit d'abord que la correction
visait juste :

```
SetFrame acteur: rang +0x88 (0042B120), 17 octets de prologue reconnus sur 17.
```

Reconnaissance parfaite, et le rang est **`+0x88`**, pas `+0x80` : la V105
appelait donc bien une autre méthode.

Il dit ensuite pourquoi rien n'apparaissait :

```
Creation de soldats - aptitude: CreateActor=00000000 ... PRET=0.
```

`CreateActor` valait zéro, la garde a refusé d'exécuter — d'où l'absence de
soldat, et d'où l'absence de plantage. La cause tient en une ligne enregistrée
deux minutes plus tôt : `[02:02:06] CreateActor: module principal illisible.`
**La recherche partait à l'ouverture du trainer, avant que le jeu soit
attaché**, et le résultat vide était gardé pour toute la session. Seul un
succès est désormais retenu ; un échec est réessayé, espacé de deux secondes
pour ne pas relire une image de plusieurs dizaines de mégaoctets à chaque
image affichée. La résolution de `SetFrame` est elle aussi mémorisée — le
journal en recevait quinze lignes par seconde.

**Le nombre se tape.** Chiffres `0`–`9` (rangée du haut ou pavé numérique) : le
premier frappé remplace la valeur, les suivants s'ajoutent à droite, retour
arrière efface. Les flèches restent pour un cran. Tant que la fenêtre est
ouverte, `8` et `9` ne changent plus la vitesse du jeu — ce sont des chiffres,
ils vont à la saisie.

**`GROUPE` n'est plus figé à quatre.** La ligne porte son propre nombre, tapé
de la même façon, borné à l'effectif réel. Le joueur règle donc séparément
combien de soldats il crée et combien il envoie sur la carte.

**La fenêtre est réécrite.** Chaque ligne dit ce qu'elle fait *et* ce que
`Entrée` y déclenche :

```
VOS SOLDATS

CREER  5  soldats            -  Entree : les faire apparaitre
GROUPE  4  soldats           -  Entree, puis K et un clic sur la carte
TOUS  6  soldats             -  Entree, puis K et un clic sur la carte
Soldat 1                     -  celui que vous pilotez
Soldat 2                     -  Entree : prendre son controle
```

## V107 — la création de soldats ne fait plus sortir du jeu (4 septembre 2026, 01:54:08)

Le journal du joueur était formel : les deux empreintes étaient trouvées,
`PRET=1`, le déclenchement avait lieu — et rien ne se terminait
(`declenche=1 execute=0`). Le code partait donc et se perdait. **Trois défauts,
tous dans la même séquence.**

**1. `Duplicate` au mauvais rang — la cause directe.** Les rangs des méthodes
d'`I3D_frame` étaient comptés avec une expression qui laissait passer la forme
`I3DMETHOD_(type,Nom)` : trente-sept méthodes sur cinquante étaient sautées.
`Duplicate` était annoncé à `+0x38`, où se trouve en réalité
`GetRot1(S_vector &axe, float &angle)` — une méthode qui **écrit** à travers
deux pointeurs, dont le second était pris au hasard sur la pile.

Le comptage refait sur les cinquante méthodes tombe sur `SetPos` au rang 3,
soit `+0x0C` : l'emplacement que le trainer utilise depuis des versions pour
téléporter, et que le jeu valide donc à chaque usage. L'ancre étant juste, la
table l'est aussi.

| Méthode | Rang | Emplacement |
|---|---|---|
| `SetPos` | 3 | `+0x0C` *(ancre, déjà validée en jeu)* |
| `SetOn` | 26 | `+0x68` |
| `LinkTo` | 33 | `+0x84` |
| `Duplicate` | 38 | `+0x98` *(était `+0x38`)* |

**2. Les appels au-delà de `+0x7F` partaient n'importe où.** Le stub écrivait
toujours `call [reg+déplacement]` avec **un octet signé** : `+0x98` s'y lit
`-104`. Le point 1 corrigé seul n'aurait donc rien donné. La forme longue est
maintenant émise dès que le rang dépasse `0x7F`.

**3. `SetFrame` deviné sur le mauvais binaire.** Les vtables du binaire de
référence et de celui du joueur ne coïncident pas — écart `0` à `+0x6C`
(`SetActive`), `+8` à `IsEnemy`, `+0xC` à `Explode`, `+0x18` à `Die` et `Hit` :
Deluxe a inséré des méthodes au fil de la table. `SetFrame`, `+0x80` dans la
référence, tombe en pleine zone incertaine. Il n'est plus supposé : il est
**reconnu à son prologue à l'exécution**, comme `CreateActor`. Sans candidate
unique, la création est **refusée** et le journal relève les premiers octets de
chaque rang de la fenêtre `+0x70…+0xA0`.

Le modèle dupliqué est en outre raccroché à la branche du soldat d'origine
(`LinkTo`) et allumé (`SetOn`), ce que le moteur fait lui-même après chaque
duplication.

**Le réglage du nombre de soldats est enfin lisible.** Il ne se clique pas, il
se règle au clavier — la fenêtre ne le disait pas assez. La première ligne
affiche désormais `<   CREER  5  soldats   >`, une ligne d'aide permanente
figure sous chaque fenêtre in-game, et la liste **défile** en se recentrant sur
la ligne choisie : avec cinquante soldats elle compte cinquante-trois lignes et
n'en montrait que douze.

## V106 — toutes les touches dans le panneau (3 septembre 2026, 23:59:08)

Une section **TOUCHES** a été ajoutée sous la grille des cheats. Elle liste
toutes les commandes et ce que chacune fait ; elles étaient jusqu'ici dispersées
entre les libellés des cartes et les LISEZ_MOI.

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

Seul le trainer change ; les compagnons V105 sont repris tels quels.

### État du projet au 3 septembre 2026, 23:59:08

**Fait et validé par les tests du joueur** — invisibilité (perception seule,
deux portées), protection réseau totale (deux portées, couvre les amis), Santé
Max sur ses soldats **et** ceux de ses amis avec la même valeur des deux côtés,
F6 liste des véhicules, aimbot jusqu'à 1000 m ou illimité, canal LAN qui atteint
les PC amis.

**Fait, pas encore essayé en jeu** — création de soldats alliés avec l'uniforme
du joueur (jusqu'à 50), prise de contrôle d'un soldat créé, ordres par la carte
(un, groupe de 4, tous), seconde action de F6 (copie conduisible), corrections
de F3 et G.

**Ouvert** — traits de balles ennemies invisibles chez les amis ; bug de
déplacement du joueur (trois vérifications en attente) ; pas de portrait de
bandeau pour les soldats créés (`pmenu[MAX_PLAYERS]` fixe à quatre) ; volumes de
collision non ré-inscrits après réparation de carrosserie.

## V105 — version finale : soldats créés, contrôle, ordres, clone (3 septembre 2026, 23:30:28)

**La création d'acteurs est branchée.** La séquence est celle du moteur :

```cpp
PI3D_model mod = driver->CreateModel();      // GameMission.cpp:3018
mod->Duplicate(source);                      //   (code de la neige)
act = mission.CreateActor(ACTOR_PLAYER, 0);  // GameMission.cpp:1759
act->SetFrame(mod);
mod->SetPos(&destination);
```

Le modèle dupliqué est celui du soldat piloté : les soldats créés portent donc
l'uniforme exact de la mission. Ce sont des acteurs de type joueur, donc
`PlayerSwitch` les énumère (liste dynamique, pas limitée à quatre) et
`SetActive` permet de les incarner.

**Fenêtre des soldats — G à pied** (G au volant reste la réparation) :
`CRÉER n soldats` (1 à 50, flèches gauche/droite), `TOUS`, `GROUPE (4)`, puis
chaque soldat — Entrée sur un soldat **prend son contrôle**. Après `TOUS` ou
`GROUPE`, **K** ouvre la carte native et un clic les y envoie.

**F6 — deux actions par ligne** : `Entrée` vole le véhicule de la mission,
`C` en **recrée une copie conduisible** — l'acteur créé se reconfigure seul
depuis la hiérarchie dupliquée (`C_automobil::SetFrame` retrouve roues, sièges,
volant, moteur et collision par leurs noms).

**Garde** : rien ne s'exécute tant que les six adresses ne sont pas résolues et
validées ; la ligne « CRÉER » affiche alors « indisponible sur cette version »
et le journal détaille ce qui manque. Deux d'entre elles — `CreateActor` et le
global `driver` — sont reconnues par empreinte, chacune trouvant une seule
candidate à l'adresse exacte dans le binaire de référence.

**Non essayé en jeu** : le binaire du joueur n'est pas sur la machine de
développement. Le LISEZ_MOI recommande de créer **un** soldat d'abord.

## V104 — commander ses soldats par la carte (3 septembre 2026, 23:19:11)

**Le système d'ordres est livré et fonctionne déjà** sur l'escouade actuelle —
il agit sur des acteurs qui existent, donc il ne dépend d'aucune adresse à
retrouver.

- **G à pied** ouvre la fenêtre des soldats dans le jeu ; **G au volant** répare
  le véhicule. Une seule touche, deux comportements selon le contexte.
- La fenêtre liste « TOUS (N soldats) » puis « Soldat 1 », « Soldat 2 »… ;
  flèches, Entrée, Échap.
- Après le choix, **K** ouvre la carte **native** — elle n'apparaît donc que sur
  demande — et un clic envoie le ou les soldats choisis à cet endroit.
- Sans sélection, K et le clic gardent leur rôle de téléportation : rien de ce
  qui marchait ne change.

Mécanisme : `AddProgram(0, PRG_MOVE, S_prg_add(&position, true))` joué sur le
thread du jeu. `AddProgram` est à `vtable+0x24`, relevé dans la table de
`C_human` du binaire de référence où `DelProgram` occupe `+0x28` — exactement
l'emplacement que le trainer utilise déjà. `S_prg_add` (`H&D.h:556`) est un
simple tableau de cinq dwords et `PRG_MOVE` vaut 0.

**Création de soldats — état exact.** Sept des huit éléments nécessaires sont
résolus et recoupés (`AddProgram` +0x24, `SetActive` +0x6C, `SetFrame` +0x80,
`CreateModel` +0x68 du driver, `Duplicate` +0x38 de `I3D_frame` ancré sur
`SetPos` +0x0C, `S_prg_add`, `PRG_MOVE`). Restent deux adresses **hors de toute
table** : `CreateActor` et le global `driver`, cherchées par empreinte dans le
binaire du joueur. Les deux empreintes trouvent **une seule candidate à
l'adresse exacte** dans le binaire de référence. Le trainer les résout à chaque
lancement et publie un rapport d'aptitude dans `hdradar_diag.log`. L'exécution
n'est volontairement pas branchée tant que ce rapport n'a pas confirmé, sur la
machine du joueur, que les six adresses se résolvent : une seule fausse ne donne
pas un message d'erreur mais un plantage de la partie.

## V103 — G et F3 corrigés, sonde `CreateActor` (3 septembre 2026, 22:58:55)

**F3.** Le chemin natif de descente teste trois positions de dégagement et
**renonce** si aucune ne convient — véhicule en vol, coincé, ou en mouvement ;
le trainer annonçait pourtant une réussite. Le véhicule est désormais **arrêté
et raccroché à son secteur** juste avant (`CB_USE_AUTO(13)` puis SetPos /
Update / SetFrameSector), puis le trainer **vérifie que `using_item` est bien
vide** — seule preuve fiable de la sortie. Le journal distingue
`rappel=1 sorti=0`.

**G.** La résolution stricte du véhicule piloté exige d'être assis **en place
zéro** avec la référence arrière du frame concordante ; en passager ou pendant
l'animation d'entrée elle échoue, et G répondait « montez dans un véhicule »
alors qu'on y était. Repli sur `using_item`, tracé dans le journal.

**Sonde `CreateActor`.** Les soldats alliés créés et le clone de véhicule
dépendent d'une seule fonction non virtuelle. La recherche par empreinte tourne
une fois par lancement et écrit son verdict dans `hdradar_diag.log`. Empreinte
validée sur le binaire de référence de 2002 : **une seule candidate, exactement
à l'adresse attendue**. Le validateur est l'écriture du vecteur d'acteurs
(`mission+0x64` → begin `+0x68`, end `+0x6C`), offsets déjà utilisés par le
trainer, ce qui prouve que la disposition de la mission est identique entre les
deux builds.

Cahier des charges complet de ce qui reste :
[`CAHIER_DES_CHARGES_V103.md`](CAHIER_DES_CHARGES_V103.md).

## V102 — la même vie exacte sur les deux écrans (3 septembre 2026, 21:50:30)

⚠️ **Protocole LAN en version 6, 70 octets : le compagnon CLIENT doit être
redistribué.**

La V101 avait réglé la mort ; les deux copies restaient pourtant **deux
compteurs indépendants**. Tant que le dégât vient d'une source commune — une
balle ennemie, qui voyage en paquet réseau — elles descendent ensemble. Dès
qu'une source est locale à une seule machine (chute, feu, explosion déclenchée
sur place), une seule copie baisse, et rien dans le jeu ne recale les deux
valeurs.

L'hôte publie donc maintenant, dans le même message et à chaque battement, la
vie qu'il mesure pour chaque soldat, identifié par son numéro réseau
(`PeerHealthEntry`, 8 emplacements). Chaque compagnon recopie cette valeur dans
sa propre copie de l'acteur : une seule vérité, celle de l'hôte.

**Limite dite** : la barre affichée ne se redessine qu'au prochain dégât, le
moteur ne la rafraîchissant que dans `C_human::SetResistance` →
`game_menu.SetHealth`, routine que l'écriture mémoire ne traverse pas.

## V101 — la vie étendue couvre les deux copies (3 septembre 2026, 21:40:09)

Le journal du PC ami a confirmé que la V100 avait réparé le canal : les paquets
arrivent et la vie du soldat distant passe bien à 20000 **chez lui**. Il mourait
pourtant comme avant.

**Cause : H&D tient deux compteurs de vie pour le même soldat, un par machine.**
Le commentaire de notre propre garde de dégâts le disait déjà — `C_human::Hit`
est le point d'entrée « both locally simulated bullets **and** NM_HUMAN_HIT
packets » : chaque PC exécute `Hit` sur **sa** copie de l'acteur et y soustrait
la résistance. La copie détenue par l'hôte gardait sa vie normale, atteignait
zéro la première, déclenchait la mort et l'annonçait sur le réseau. L'ami
mourait sur ordre de l'hôte.

La vie étendue s'applique donc désormais à **toutes les copies d'acteur joueur**
sur chaque machine, quel que soit leur propriétaire — côté trainer comme côté
compagnon. Aucun compteur ne peut plus atteindre zéro avant les autres.

## V100 — le canal LAN atteint le PC ami (3 septembre 2026, 21:19:59)

**La santé d'équipe n'arrivait pas parce qu'aucun paquet n'arrivait.** Le
journal du PC ami montrait `receiver started protocol=5` puis **aucune** ligne
`[trace] RX` — or cette trace est écrite pour tout paquet, accepté comme
refusé. Ni la version du compagnon ni les offsets n'étaient en cause : le
message ne traversait pas.

Le trainer n'écrivait que vers `255.255.255.255`, la diffusion limitée, que
beaucoup de configurations ne délivrent pas : adaptateurs de LAN virtuel
(Hamachi, Radmin, ZeroTier), Wi-Fi avec isolation des clients, piles pare-feu.
`CollectBroadcastTargets` énumère désormais les interfaces via
`WSAIoctl(SIO_GET_INTERFACE_LIST)` et envoie le message à la diffusion **dirigée**
de chacune (`adresse | ~masque`) en plus de la diffusion limitée — c'est
`25.255.255.255` qui traverse un LAN Hamachi. Le nombre d'adresses est
journalisé (`adresses=N`).

**G répare la carrosserie au volant.** `SetVersion` reste inappelable, mais sa
partie visible se refait à la main : `version_list` est un vecteur de frames à
`+0x5C/+0x60`, et montrer ou cacher une hiérarchie revient à poser ou effacer le
bit `FRMFLAGS_ON` (`0x00020000` à `frame+0x0C`) — l'écriture directe que le
compagnon CLIENT utilise depuis la V63 pour masquer un soldat. Le joueur reste
assis, rien ne disparaît ni ne réapparaît. **Limite dite** : les volumes de
collision ne sont pas ré-inscrits dans la scène. Sur demande explicite, G n'agit
plus qu'à l'intérieur d'un véhicule.

**Traits de balles : le test a tranché.** Les dégâts traversent le réseau, le
tir visuel non — conséquence directe des 100 % d'ennemis donnés à l'hôte.
Non corrigé, et annoncé comme un travail de recherche.

**F6 « créer un nouveau véhicule » : refusé, avec la raison.**
`CreateActor(ACTOR_AUTOMOBIL)` ne prend aucune donnée ; l'acteur est accroché à
un frame déjà chargé par la carte puis ses tables sont lues depuis le bloc de la
mission par `MissionLoad`. Il n'existe aucun catalogue global de véhicules.

## V99 — corrections du test véhicules (3 septembre 2026, 19:53:55)

**F3 reposait le joueur au point de ramassage.** La position de descente vient
du véhicule : `case 7` renvoie `&(siège->entrée ? entrée : siège)->GetWorldPos()`,
soit la position monde **mise en cache** du frame d'entrée. Tant que la
hiérarchie du véhicule n'a pas été réactualisée, ce cache date du moment où le
joueur est monté. Le trainer lit donc la position réelle du véhicule avant la
sortie, laisse le jeu faire sa descente native, puis repose le soldat à trois
mètres devant le véhicule — par le même appel que la téléportation à pied.

**G ne redressait pas une carrosserie enfoncée, et ne le pouvait pas.** Écrire
`curr_version` ne change rien à l'écran : `C_version::SetVersion`
(`Vehicle.cpp:117`) cache la hiérarchie de frames de l'ancienne version, montre
celle de la nouvelle et **ré-inscrit les volumes de collision dans la scène**.
Cette méthode n'est pas virtuelle — son adresse ne se dérive donc pas de la
vtable comme `cbProc` — et le seul chemin de rappel qui l'atteint
(`CB_SIGNAL`/`SIGNAL_DETECTOR` sous-type 0) n'avance que vers **plus** abîmé.
G amène désormais devant le joueur le véhicule **intact** le plus proche
(jusqu'à 120 m), arrêté et remis en état ; au volant il remet l'état mécanique
et annonce clairement que la carrosserie ne se redresse pas en place. La liste
F6 marque « (abîmé) » les véhicules déformés.

**Santé Max chez les amis : diagnostic des deux côtés.** Aucun défaut trouvé
dans la chaîne. L'hôte journalise toutes les 5 s
`LAN->Client heartbeat: client_health=…` et le compagnon annonce à l'écran ce
qu'il reçoit, ce qu'il applique, et surtout **refuse bruyamment un message
d'une autre version** — le cas d'un PC ami resté sur un ancien compagnon.

## V98 — santé d'équipe et véhicules (3 septembre 2026, 19:26:55)

Quatre ajouts, dont deux qui **ne pouvaient pas** se faire depuis le PC hôte.

**Santé Max couvre les soldats des amis.** La vie appartient à la machine qui
pilote le soldat : l'hôte envoie l'ordre, le CLIENT de chaque ami étend
lui-même la vie de ses propres soldats et la rend à l'identique au relâchement
— ou après 1,5 s de silence de l'hôte, par le délai de sécurité existant.

**Masquer ma position, portée « Escouade entière », complète.** Le crochet du
site d'envoi `NM_HUMAN_POS` a été porté dans le CLIENT : il réécrit les trois
flottants sérialisés (`[ebp-10h/-0Ch/-08h]`) pour les soldats que cette machine
possède, sans toucher le modèle ni la physique locale. Position, modèle caché
et invisibilité aux ennemis couvrent enfin les trois PC.

**F6 — liste des véhicules de la mission, affichée dans le jeu** par la fenêtre
superposée : flèches, Entrée, Échap. Le véhicule choisi est remis en état et
posé devant le joueur. Limite structurelle assumée : le moteur n'instancie
jamais un véhicule ex nihilo, il accroche un acteur à un modèle déjà chargé par
la carte, donc seuls les véhicules présents dans la mission sont listés.

**G — réparation.** Version de carrosserie remise à 0, état d'épave annulé,
résistance rendue si elle était nulle ; à pied, le véhicule le plus proche est
en plus posé devant le joueur.

Protocole LAN en version 5, 20 octets, disposition identique des deux côtés
(`static_assert`).

## V97 — cheats natifs et aimbot longue portée (3 septembre 2026, 19:10:09)

La section Cheats ne dépend presque plus de la frappe clavier. Deux des cinq
cheats tapés ne faisaient **rien** en partie réseau : le moteur les encadre
d'un `if(!net)`.

| Touche | Avant | Maintenant |
|---|---|---|
| F3 | Life Unlimited (tapé) | **supprimé** → **Sortir du véhicule** (natif) |
| F4 | `ironman` tapé, mort en réseau | **Santé Max natif**, interrupteur |
| F5 | `fullhands` tapé, mort en réseau | **natif**, même moteur que la touche M |
| F6 | Big Heads | **supprimé** |
| F7 | `skipmission` tapé | inchangé — le seul que le moteur honore en réseau |

**Santé Max.** Le trainer écrit lui-même `resistance` et `init_resistance`.
Les deux offsets viennent du binaire de référence `source/hde/bin/HDE.exe`
(build du 13 mai 2002, livré avec sa table de symboles) où le site du cheat
compile en `mov eax,20000 / mov [esi+2C4h],eax / mov [esi+2Ch],eax`, corrigés
du décalage constant de `0x14` mesuré sur trois champs déjà cartographiés
(`stay_mode`, `mode`, `no_hit_cheat`). Chaque écriture est précédée d'une
relecture qui refuse d'agir si les valeurs ne ressemblent pas à une vie de
soldat.

**Sortie de véhicule (F3).** Appelle `C_human::cbProc(CB_USE_AUTO, 0, 0)`, le
chemin natif de descente, sur le thread du jeu. Un garde vérifie qu'un véhicule
est réellement occupé : dans ce cas le moteur déréférence sa référence sans
test, et l'appel à vide ferait planter le jeu.

**Aimbot.** Curseur porté à 1000 m, plus une case « Illimité » qui grise le
curseur et supprime toute limite — le sélecteur de cible traite déjà 0 comme
« aucune limite », c'est ce que Bullet Track utilise.

**Masquer ma position.** La pastille de la carte pilote désormais aussi
l'invisibilité aux ennemis : « Escouade entière » couvre vos soldats et ceux
des PC amis.

Restent pour la V98, tous liés à une nouvelle version du compagnon CLIENT ou à
une mécanique in-game : la santé étendue chez les amis, le masquage de leur
position, la fenêtre de choix des véhicules (F6) et la réparation de véhicule
(G). Détail dans `release/V97_NATIVE_CHEATS/LISEZ_MOI.txt`.

## V96 — re-balayage ennemi et W invisible (3 septembre 2026, 18:04:26)

Deux corrections issues du test LAN du 3 septembre 2026.

**1. Les amis restaient ignorés des ennemis en début de mission.** Tant que
l'invisibilité était active, `MaintainEnemyAwarenessSuppression` remettait à
zéro, à chaque image et pour chaque ennemi, le compteur `actor_enum_count`
(`enemy+0x298`). La source du moteur montre pourquoi c'est fatal :

```cpp
// Actors.cpp, C_human::WatchHumans
if((actor_enum_count += time) >= WATCH_ENUM_COUNT){
   actor_enum_count = 0;
   ... mission.EnumActors(...)          // seul push_back de watch_actors
}
// et le rappel commence par :  if(!a->IsEnemy(t.actor)) return true;
```

Le tenir à zéro empêche l'énumération **pour tout le monde** : l'ennemi ne
découvre plus aucun nouvel acteur, donc pas non plus le soldat d'un ami. Un
décochage/recochage laissait passer une énumération, d'où « ça marche
parfaitement après ». Or le gel n'était pas nécessaire : le rappel
d'énumération commence par `IsEnemy`, et notre crochet y répond « pas hostile »
pour l'acteur protégé — celui-ci ne peut donc pas être réinscrit même quand
l'énumération tourne. Le gel est désormais réservé à la portée « Escouade
entière », où tous les joueurs sont filtrés de toute façon.

**2. « Masquer ma position réseau » rend invisible pour les ennemis.** Le
premier W cochait la case « Invisible pour les ennemis » et ne la décochait
jamais — avec la portée qui s'y trouvait, parfois « Escouade entière ». Le
filtre est maintenant demandé directement par l'état caché du masque, **pour le
seul soldat piloté**, et il disparaît au W qui vous remontre. La case dédiée
n'est plus jamais modifiée et garde son comportement V95.

## V95 — règle exacte des deux portées (3 septembre 2026, 17:40:27)

Corrige deux comportements confirmés par un test LAN le 3 septembre 2026 :
les soldats des PC amis étaient immortels dès que « Invisible pour les
ennemis » était cochée, et « Protection réseau totale — Escouade entière »
n'avait aucun effet sur eux.

**Cause.** Les deux cases portaient la même condition, écrite à l'envers l'une
de l'autre. L'invisibilité installait une garde sur `C_player::Hit` testant
seulement `[ecx+1Ch] == 1` (« est-ce un joueur ? »), sans test de propriétaire,
donc valable pour tout le monde dans les deux portées. La protection, elle,
ajoutait `[ecx+34h] == 0` (« propriétaire local ») partout et écartait les
acteurs distants dès la collecte.

**Règle depuis la V95.**

| | Invisible pour les ennemis | Protection réseau totale |
|---|---|---|
| Rôle | perception seule : vue, ouïe, poursuites, voix | invulnérabilité seule |
| Dégâts | n'en bloque plus aucun | les bloque, selon la portée |
| « Joueur actuel / contrôlé » | le soldat piloté | le soldat piloté |
| « Escouade entière » | tous les joueurs, connectés compris | vos soldats ; **plus** les joueurs connectés pour `Hit` et `Explode` |

Le partage de la portée « Escouade entière » de la protection suit la machine
qui décide réellement : `C_player::Hit` et `C_player::Explode` couvrent aussi
les joueurs connectés, parce que l'IA ennemie appartient à l'hôte et que ces
deux dégâts sont calculés ici avant leur envoi réseau ; `C_player::Die`,
`C_human::Die`, la garde de chute et l'octet natif `no_hit` restent limités aux
soldats locaux, parce qu'une mort distante est décidée par sa propre machine et
que la bloquer ici ne ferait que désynchroniser les deux écrans.

**Application immédiate.** Cocher, décocher et changer de portée prennent effet
sur-le-champ. La portée et l'acteur protégé vivent maintenant dans deux mots de
la page du trampoline (`0xC0` et `0xC4`), donc une bascule est une écriture de
4 octets au lieu d'un retrait/repose différé jusqu'au retour dans le jeu ; les
pages des crochets retirés sont rendues par une file non bloquante au lieu de
figer l'interface jusqu'à une seconde ; la purge des perceptions déjà
mémorisées repasse toutes les 150 ms au lieu de 1000 ms.

Détail complet, preuves et limites : [`plan.md`](plan.md), section
« V95 - Chaque case retrouve sa logique ».

## Fonctions

### Cheats (boutons ou touches configurables)

Le trainer ne met **jamais** H&D au premier plan et ne simule aucune touche
tant que le jeu n'est pas déjà actif. Cocher un cheat depuis le panneau
enregistre la demande (`[ATTENTE] Demande enregistrée...`) et elle s'applique
d'elle-même dès votre retour dans le jeu. Un cheat déclenché par son raccourci
en cours de partie s'applique immédiatement, comme avant.

L'ordre de lancement est libre : le trainer peut être démarré avant hde.exe. Il
réélit la fenêtre du jeu tant qu'elle n'est pas une fenêtre principale visible,
au lieu d'en garder une fenêtre de démarrage pour toute la session.

- Life Unlimited (F3), Santé Max (F4), Big Heads (F6) et Passer la Mission
  sont disponibles dans Cheats. Life Unlimited appelle directement le callback
  natif du soldat contrôlé : il ne bloque ni ne simule le clavier.
  (F7). Envoyés au jeu sous forme de séquences clavier, sans écriture mémoire.
  L'ancien cheat « Inventaire complet » est masqué : il contournerait la
  rotation sûre en sept séries décrite plus bas.

### Visuals / ESP

- ESP ennemis rouge/vert : le vert exige un timestamp de rendu récent puis un
  test balistique partant des yeux du joueur ; les matériaux traversables par
  les balles (table de collision de la mission) sont ignorés. Une seule zone
  atteignable (visage, tête, épaule, torse) suffit pour passer au vert.
- ESP alliés bleu, lignes de visée.
- L'overlay est dessiné en **GDI** sur une fenêtre calque à clé de
  transparence, sans Direct3D. Les versions précédentes créaient un second
  périphérique Direct3D 9 que le pilote refuse tant que H&D détient
  l'adaptateur : l'ESP pouvait alors ne jamais apparaître de toute une session.
  Il n'y a plus aucune dépendance à l'adaptateur, donc cocher la case prend
  effet à l'image suivante.

### Armes (joueur local uniquement)

- Stabilité et précision 100 % (sans recul ni dispersion).
- Tir ultra-rapide et munitions illimitées (sans recharge).
- Les valeurs originales sont capturées puis restaurées au changement d'arme,
  à la désactivation et à la fermeture du trainer.

### Gameplay / Fonctions avancées

- **Super Run** : F8 augmente / F9 réduit le multiplicateur.
- **Noclip spatial (touche V)** : une seule case. La cocher active le
  déplacement libre **et** arme le raccourci **V** ; V bascule ensuite le vol
  sans se désarmer, donc elle le rallume aussi. La décocher rend intégralement
  la touche V au jeu, qui ne peut alors plus rien activer.
  **Z/S** avancent et reculent selon la caméra, **A/E** déplacent latéralement,
  **H/B** montent et descendent, **F8/F9** règlent la vitesse de 1 à 80 m/s.
  Pendant ce mode, seules ces touches de locomotion sont retirées au jeu : le
  tir, les armes et l'inventaire restent disponibles. **Au volant, c'est le
  véhicule qui vole** : un second trampoline est posé sur
  `C_automobile::Tick` (`0x0044E540`), parce que le cas véhicule de
  `C_player::Tick` n'appelle jamais `C_human::Tick`. Il partage la page de
  données du premier, exécute la séquence de déplacement du moteur
  (position, `I3D_frame::Update`, `scene->SetFrameSector`) pour que la
  voiture bouge à l'image près, et déplace la frame publiée par le trainer — celle de la voiture au lieu de celle du
  joueur, qui la suit comme son enfant. Les champs de physique neutralisés
  sont des offsets `C_human` et restent écrits sur le joueur uniquement,
  jamais sur le véhicule. La gravité et l'état de chute du joueur contrôlé
  sont
  maintenus à zéro pour rester parfaitement stable dans les airs, puis la
  physique et la locomotion natives reprennent dès la désactivation. Les
  translations sont intégrées directement par `C_human::Tick`, sur le thread
  du jeu et avec son propre delta temporel, afin que la caméra reçoive chaque
  mouvement sans déphasage avec le trainer.
- **Téléportation via la carte native** : touche physique AZERTY **K**
  (la commande native Espace est envoyée au jeu). Seule une carte ouverte par
  **K** arme la téléportation ; la carte normale ouverte avec Espace reste
  passive. À l'ouverture par K, l'origine exacte du modèle du joueur est
  projetée par la caméra de carte. Le Y est converti avec la correction
  widescreen utilisée par `Map_man.cpp`, puis un clic
  est converti par les véritables `I3D_scene::UnmapScreenPoint` et
  `map_scene->TestCollision(EXACT|RAY)` du jeu. Le point X/Z vient donc de la
  surface visible sur la carte, puis la carte se ferme et sa fermeture est
  confirmée pendant trois mises à jour avant la pose. Chaque étape échouée
  affiche un statut précis (carte, curseur, unmapping, collision, sol).
- **Invisible pour les ennemis** : portée « Joueur contrôlé uniquement » ou
  « Escouade entière ». La perception visuelle et la perception sonore sont
  filtrées dans les deux sens ; les sons, poursuites et voix déjà mémorisés
  sont purgés lors de l'activation. En réseau, « Escouade entière » couvre
  aussi le joueur connecté et ne dépend d'aucune adresse d'acteur.
  **Depuis la V95 (3 septembre 2026, 17:40:27 (heure locale)) cette case ne
  protège plus d'aucun dégât** : elle posait jusque-là une garde sur
  `C_player::Hit` qui testait seulement le type d'acteur, sans test de
  propriétaire, ce qui rendait aussi les soldats des PC amis insensibles aux
  balles dans les deux portées. L'invulnérabilité appartient désormais à
  « Protection réseau totale » seule. Changer de portée est appliqué
  immédiatement, sans retirer le moindre crochet.
- **Aimbot tête** (sans tir automatique), distance réglable.
- **Balles à travers les murs** (section Armes) : avec Bullet Track, les
  ennemis rouges/occlus sont sélectionnables comme les verts. La cible reste
  toujours la tête la plus proche du centre du viseur, y compris derrière
  plusieurs murs ou une maison.
- **Bullet Track tête** : acquisition sans limite métrique et sans limiteur de
  rafale. Le trampoline valide l'acteur dans la liste vivante de la mission,
  remet sa tête au résolveur natif et laisse le moteur prendre lui-même la
  référence du destinataire. `projectile.hit_frm` n'est jamais remplacé,
  référencé ou libéré par le trainer. Au moment de `Finish`, le frame tête
  courant est injecté uniquement dans le `S_CB_hit` local sur la pile : le
  callback reçoit donc une vraie zone tête, tandis que la table de matériaux
  relit ensuite le frame persistant natif, inchangé. Cette séparation corrige
  le crash capturé à `hde.exe+0x1FB13` (`sub_frame == null`) sans toucher à la
  propriété des objets du moteur. La distance native règle seulement la durée
  de vie visuelle du projectile; le destinataire et le dégât forcé ne possèdent
  aucun plafond en mètres.
- **Super vitesse véhicule** : touche physique **N** = accélère le
  multiplicateur, **B** = le réduit (minimum 1.0x = vitesse par défaut du
  jeu). Le multiplicateur reste appliqué tant que la case est cochée.
  Touches physiques **I** et **U** : la sensibilité de direction multiplie
  le facteur de braquage `0.6f` (`0x004552E9`) pour la seule voiture
  conduite, de 1.0x (braquage d'origine) à 8.0x, par pas de 0.5x. Plus la
  butée est atteinte vite, plus la voiture répond tôt — ce qu'il faut à
  grande vitesse, où une seconde de braquage laisse le virage derrière.
  Le trafic géré par l'IA garde le braquage du moteur. **B** réduit aussi la vitesse courante dans le même
  rapport : le frein du jeu est une constante fixe, donc sans cela un
  véhicule lancé à 40x mettrait 40 fois plus longtemps à s'arrêter.
- **Véhicule indestructible** : une case. Le véhicule conduit n'encaisse
  plus rien — tirs ennemis, collisions et chutes de grande hauteur. La
  résistance `C_version::v_resistance` (`+0x6C`) est mise à zéro, ce qui
  fait sortir `C_version::HitExplode` (`0x0044CF50`) avant toute
  soustraction de dégâts et avant la création de l'acteur explosion.
  Un second garde-fou couvre l'épave sans explosion : une chute assez
  violente pour détruire mais trop lente pour exploser appelle
  `C_automobil::Destroy` (`0x0044FC80`) directement, sans passer par
  `HitExplode`. Un trampoline de quatorze octets fait sortir cette
  fonction avant son corps pour le véhicule protégé, donc le moteur n'est
  jamais coupé. `Destroy` étant déclarée dans la classe, le compilateur
  l'a aussi inlinée à trois autres endroits : le champ `mode` (`+0x74`)
  est donc en plus remis à sa dernière valeur saine dès qu'il passe à
  `MODE_DESTROYED`, ce qui couvre tous les chemins.
  La protection reste active sur le véhicule que vous venez de quitter
  tant qu'il est valide, pour qu'en sauter en pleine chute ne le fasse
  pas exploser.
- **Armes et équipement, série suivante (touche M ou bouton)** : le catalogue
  complet est réparti dans **14 séries fixes**. Sur l'installation observée,
  128 lignes valides donnent 9 ou 10 objets par série; un catalogue de 91
  objets en donnerait 6 ou 7. Chaque appui remplace entièrement la série
  précédente puis avance de 1/14 à 14/14, sans accumulation. Ce découpage est
  ce qui gouverne le figement de la première ouverture : le moteur reconstruit
  chaque modèle porté à environ 8 ms pièce, soit près de 75 ms pour 9 objets
  contre 156 ms mesurés pour 19. `M` est capturée par le trainer
  et n'ouvre plus l'inventaire du jeu. Avant toute modification, le trainer
  attend deux lectures `inv_scene == 0`; le trampoline refait le contrôle sur
  le thread du jeu et annule **tout l'appui** si l'écran s'est rouvert. La série
  n'avance qu'après vérification que tous les objets attendus sont présents et
  qu'aucun objet de l'ancien lot ne subsiste. La suppression est déléguée à la
  fonction native signée `C_inventory::DeleteAllItems` (`0x00462E80`), après
  holster natif de l'arme courante; c'est donc le moteur qui efface le vecteur
  et gère ses références. Le trainer ne libère aucun objet ou modèle lui-même.

### Implémentation

Les mods de gameplay utilisent des écritures mémoire ciblées et des
trampolines **temporaires et réversibles** exécutés sur le thread du jeu
(signatures vérifiées avant installation, prologue restauré et page libérée
après acquittement). À la désactivation ou à la fermeture normale, toutes les
valeurs d'origine sont restaurées. Aucun fichier du jeu n'est modifié.

## Parties réseau

La cause des morts en réseau a été trouvée et corrigée en V28, et elle n'avait
rien à voir avec le rôle hôte/client.

Le thread du jeu se bloque régulièrement en réseau — le journal montre des
arrêts de plus de 2 secondes pendant que la machine attend l'autre. Le trainer
ne pouvait alors plus lire la mission, et **retirait les deux hooks
d'invisibilité**. Le jeu, lui, continuait normalement : les ennemis vous
voyaient de nouveau. En solo ces blocages sont rares, d'où « en solo aucun
problème ».

Depuis la V28, les hooks sont **maintenus** pendant ces trous de lecture. Le
statut passe à « Filtre maintenu » au lieu de retirer la protection.

| Situation | Résultat |
|---|---|
| Solo | Filtre complet. |
| **Vous hébergez** | L'IA est calculée sur votre PC : filtre complet, maintenu pendant les blocages. |
| **Vous rejoignez** | L'IA est calculée chez l'hébergeur et aucun patch local ne peut l'aveugler. **L'hébergeur doit aussi lancer le trainer.** |

### Portée « Joueur contrôlé » en réseau

Cette portée fige l'adresse du soldat protégé dans le code injecté. Si le jeu
reloge l'acteur — une réapparition le fait — le hook protège une adresse morte
et vous redevenez visible sans qu'aucun indicateur ne change. **En réseau,
préférez « Escouade entière »** : elle compare le type d'acteur et ne dépend
d'aucune adresse.

### Lire le journal

```text
Enemy invisibility HEARTBEAT: visual_applied=1 live=1 fn=0042A260 first=E9 | hearing_applied=1 live=1 ...
Enemy invisibility: HOLDING through mission gap fn=0042A260 first=E9 live=1
Enemy invisibility: status -> held-through-mission-gap
```

`live=1` signifie que le patch est réellement présent dans la mémoire du jeu,
relu à chaque battement. `live=0` avec `applied=1` voudrait dire que le jeu a
réécrit la fonction par-dessus.

Depuis la V29, chaque échec de lecture du radar est nommé, avec un
recensement des acteurs :

```text
Radar read state -> local-player-unavailable
Radar: no local player. actors=302 players=3 flagged=0 remembered=02612690 have_position=1
```

`flagged=0` signifie qu'aucun acteur ne porte le drapeau `+0x2B8` du joueur
contrôlé — le jeu l'efface lui-même. La sélection retombe alors sur l'acteur
joueur le plus proche de la dernière position connue, au lieu d'abandonner la
lecture pour le reste de la mission.

### Détection du rôle : limite connue

`net_host` et `net_join` (variables console du moteur) sont lues et
journalisées, mais **restent à 0 pour une partie lancée depuis le menu
multijoueur** : seules la console et la ligne de commande les assignent. Le
rôle n'est donc pas détecté de façon fiable. Le port UDP 2302 a été évalué
comme signal de remplacement puis écarté — `hde.exe` l'ouvre au démarrage, en
solo aussi. Ce n'est pas bloquant : le correctif de la V28 ne dépend d'aucun
rôle.

## Relais réseau — état au 31 août 2026

Cette section est la référence courte pour reprendre le travail réseau sans
relire l'historique complet de `plan.md`.

### Ce qui fonctionne et a été validé par le joueur

- En **solo**, les fonctions de protection existantes fonctionnent.
- En LAN, la case **Masquer ma position réseau** est séparée du noclip. Elle
  conserve localement le vrai déplacement, mais remplace uniquement la
  position envoyée au pair par une position-ancre choisie à l'activation.
- La case arme le masque. La touche **W** alterne ensuite :
  `masqué -> position réelle -> masqué`. À la seconde activation, une nouvelle
  ancre est capturée. Le pair ne reçoit donc la position courante que pendant
  l'état « position réelle » demandé par W.
- Cette logique est implantée dans `src/gameplay_mods.cpp`, au point exact où
  `C_human::Tick` construit le message `NM_HUMAN_POS` : RVA `0x1BD97`
  (`0x0041BD97` pour le binaire connu). Le trampoline remplace les trois
  coordonnées temporaires avant l'envoi ; il ne déplace ni la frame locale,
  ni la caméra, ni la collision locale.
- Les portées **Joueur actuel** et **Escouade entière** existent pour ce masque.
  En escouade, seuls les acteurs joueurs possédés localement sont publiés : le
  joueur de l'autre PC n'est jamais modifié.
- V49 ajoute une publication de position humaine pendant noclip sans réactiver
  les collisions : le test LAN doit confirmer que l'autre PC voit le soldat
  voler. Si
  **Masquer ma position réseau** est activé, cette case garde priorité et
  l'autre PC reçoit l'ancre masquée au lieu de la position réelle.
- F5 est de nouveau disponible dans **Cheats**. Il utilise le mécanisme
  existant `ApplySingleCheat(Fullhands)`, qui simule `fullhands` et bloque les
  entrées pendant cette saisie. Le raccourci M de séries d'équipement reste
  indépendant.

### Limite réseau importante

Hidden & Dangerous répartit les ennemis entre les deux processus. Dans
`GameMission.cpp`, chaque ennemi reçoit alternativement une autorité locale ou
le PID du pair (`CB_SET_NETWORK_ACTOR`). Un ennemi possédé par l'autre PC y
fait son IA et peut décider une mort. Le masque W réduit ce qu'il peut déduire
de la position publiée ; il n'empêche pas rétroactivement une mort déjà
validée par ce processus. Aucun changement local ne doit prétendre contrôler
le joueur humain du pair.

### Deux travaux demandés — plan initial (mis à jour par V39 ci-dessous)

1. **Protection totale, joueur contrôlé seulement.**
   La future case devra protéger uniquement l'adresse du joueur actuellement
   contrôlé, jamais les autres soldats ni le joueur du pair. Le garde déjà
   présent sur `C_player::Hit` bloque les blessures normales (balles,
   explosions, grenades). Il faut le découpler de « Invisible pour les ennemis »
   puis ajouter un garde signé sur le chemin direct `C_human::Die` : chute,
   eau, hors carte, mine et ordre de mort réseau. Le hook ne doit être posé
   qu'après identification dynamique de son slot de vtable et vérification de
   son prologue sur le SHA-256 de hde.exe documenté dans `plan.md`.

2. **F10 : réanimation ciblée du joueur contrôlé.**
   Ne pas envoyer le cheat natif `newlife` comme solution réseau : le code du
   jeu le refuse explicitement lorsque `net` est actif. Un F10 doit être
   construit après le garde de mort : conserver l'état vivant cohérent du seul
   joueur local protégé, empêcher son paquet de mort sortant et ignorer son
   ordre de mort entrant. Si le pair a déjà validé sa mort, une réanimation
   seulement locale crée une désynchronisation ; elle n'est donc pas une
   solution garantie sans composant synchronisé sur le second PC. Voir le plan
   de reprise détaillé dans `plan.md`.

### V52 — protection réseau du joueur actuel

### Outil d'autorité IA — hôte unique (test LAN requis)

Deux exécutables autonomes, sans dépendance Visual C++ externe, accompagnent
le trainer : `HD_AI_AUTHORITY_HOST.exe` pour le PC qui héberge et
`HD_AI_AUTHORITY_CLIENT.exe` pour le PC qui rejoint. Ils doivent tous les deux
être lancés avant la mission puis rester ouverts. Dès qu'une mission à deux
joueurs est détectée, le premier impose `network_actor=0` sur chaque ennemi
dans sa propre instance et le second impose le PID de l'hôte : seul l'hôte
exécute alors l'IA ennemie, tandis que le client reçoit sa synchronisation
native.

### V63 — disparition visuelle sur le PC client

La règle d'autorité IA ci-dessus reste inchangée. V63 ajoute seulement un
canal visuel indépendant entre le trainer de l'hôte et
`HD_AI_AUTHORITY_CLIENT.exe` : il ne modifie ni `network_actor`, ni les
dégâts, ni les paquets du jeu.

- Avec **Masquer ma position réseau** armé sur l'hôte, la première touche
  **W** conserve l'ancre réseau mais demande au helper Client de masquer les
  modèles des joueurs appartenant à l'hôte. L'hôte continue de les voir
  normalement ; les ennemis, le joueur client et l'IA ne sont pas touchés.
- Le **W** suivant restaure les modèles sur le PC client et publie la vraie
  position courante, comme auparavant. Un W ultérieur peut de nouveau créer
  une nouvelle ancre.
- Le signal utilise un petit broadcast UDP local (port 48217), seulement
  pendant la session LAN. Si le trainer hôte s'arrête ou si le signal ne
  revient pas pendant 1,5 seconde, le helper Client restaure lui-même les
  modèles. Autorisez le réseau privé si Windows le demande.
- Le masquage emploie le drapeau de visibilité `I3D_frame::SetOn(false)` du
  moteur. Le modèle n'est pas supprimé et le soldat n'est pas désactivé ;
  c'est volontairement distinct de `C_player::SetActive(false)`, qui change
  aussi la caméra et l'état du joueur.

### V64 — premier W : invisibilité ennemis automatique

Le premier appui sur **W** après avoir coché **Masquer ma position réseau**
active aussi **Invisible pour les ennemis** s'il était désactivé. Il ne peut
jamais le décocher : les W suivants gardent uniquement leur rôle réseau et
visuel. La case Invisible pour les ennemis reste indépendante et peut toujours
être cochée ou décochée manuellement sans utiliser le masque réseau.

### V65 — alternance W stricte

W alterne désormais sans troisième état visible : **cacher → montrer à la
vraie position → cacher → montrer**. Quand W est pressé alors que la vraie
position est affichée, il capture immédiatement une nouvelle ancre et masque
le modèle sur le Client dans le même appui.

### V66 — portée par défaut : joueur contrôlé

**Invisible pour les ennemis** et **Masquer ma position réseau** démarrent
tous les deux sur **Joueur actuel**. Les boutons Escouade entière restent des
choix manuels, mais ne sont plus sélectionnés par défaut.

### V67/V68 — réapparition sans course accélérée sur le Client

Au W qui rend visible, le helper Client garde brièvement le modèle hôte caché
pendant que la synchronisation native le fait passer de l'ancre à la nouvelle
position. V68 attend seulement 350 ms, puis réactive le modèle dès que sa frame
ne bouge plus pendant deux relevés ; le délai maximal est de 1,2 s. Cela évite
la course accélérée sans l'attente de plusieurs secondes de V67.

### V69 — la case arme, seul W commence le masque

Cocher **Masquer ma position réseau** ne fige plus la copie reçue par le PC
client : tant que vous n'avez pas appuyé sur **W**, il vous voit et suit vos
déplacements normalement. Le premier W capture l'ancre à cet instant, active
aussi Invisible pour les ennemis et cache le modèle sur le Client. Les appuis
suivants conservent l'alternance validée : montrer la vraie position, puis
cacher sur une nouvelle ancre.

### V70 — confirmation visuelle de l'état W

Quand le masque est armé, chaque W qui change réellement son état affiche
localement, pendant deux secondes, **« Maintenant invisible »** ou
**« Maintenant visible »**. Le texte utilise l'overlay GDI déjà présent et
disparaît seul ; il n'envoie ni touche, ni donnée réseau, ni écriture au jeu.

### V71 — mort locale réversible, Client maintenu vivant

La nouvelle case **Retour à la vie local (F12) — le Client ignore ma mort**
est distincte de toute protection : les dégâts et une mort continuent sur le
PC hôte. Tant qu'elle était cochée avant cette mort, `HD_AI_AUTHORITY_CLIENT`
refuse seulement les transitions `C_player::Die`, `C_human::Die` et
`C_player::Explode` pour les acteurs joueurs possédés par l'hôte. Le joueur du
Client et tous les ennemis restent natifs. L'hôte utilise ensuite **F12** pour
sa réanimation native complète ; le Client n'ayant jamais appliqué la mort,
il ne montre pas de squelette. La commande est un heartbeat UDP privé : à la
décoche, à la fermeture ou après 1,5 s sans hôte, le Client retire les gardes.

### V73 — chute normale, impact non létal avec Protection réseau totale

La case **Protection réseau totale — joueur actuel** ne modifie plus la vitesse
de chute : le joueur tombe à la vitesse native normale. Un trampoline vérifié,
placé juste avant la collision de `C_human::Tick`, efface uniquement le drapeau
interne qui autorise les deux appels de mort par chute. Avec le miroir V71
activé, le Client ne reçoit donc pas non plus un état mort après l'atterrissage.

Ce mécanisme reprend directement la règle de `GameMission.cpp`, qui répartit
normalement les ennemis entre les joueurs avec `CB_SET_NETWORK_ACTOR`. Il ne
modifie aucun soldat joueur. La fenêtre affiche `[actif]` avec le nombre
d'ennemis et le propriétaire choisi ; ne lancez pas la mission tant que les
deux fenêtres n'ont pas atteint cet état.

### V53 — garde du message de mort entrant

Le test LAN de V52 a montré que les deux gardes déjà présentes étaient actives
au moment de la mort. Le chemin restant est donc l'événement `NM_HUMAN_DIE`
provenant du PC qui simule l'ennemi : cette version peut le faire entrer
directement dans `C_human::Die`. V53 garde simultanément `C_player::Die`
(empêche l'émission locale) et `C_human::Die` (refuse l'ordre entrant), avec
la même adresse de joueur strictement contrôlée. Le journal doit maintenant
indiquer `network_die_live=1 base_die_live=1`.

`HDFinalAdvancedV52_NETWORK_ALIVE_GUARD.exe` corrige l'ordre du garde de
mort : l'ancienne version interceptait `C_human::Die`, alors que
`C_player::Die` envoie d'abord `NM_HUMAN_DIE` sur le réseau puis appelle cette
routine. Le nouveau garde cible `C_player::Die` au slot vtable `+0x100`, avant
la création du paquet. Avec la case **Protection réseau totale — joueur
actuel**, les tirs restent filtrés dans `C_player::Hit`, la santé ne descend
plus (activez-la lorsque le soldat est déjà à pleine santé), et aucune mort
locale ne doit être envoyée à l'autre PC. Seul le
soldat actuellement contrôlé est concerné.

Le journal doit contenir `network_die_live=1` et
`C_player::Die=00421150` (adresse habituelle de la version prise en charge).

### V39 — protection du joueur actuel et F10

`HDFinalAdvancedV39_ABSOLUTE_PLAYER_GUARD.exe` ajoute la case **Protection
totale — joueur actuel**. Elle installe deux gardes réversibles et vérifiés par
signature : `C_player::Hit` pour les dégâts normaux, puis `C_human::Die`
(`0x004208B0`, `ret 8`) pour les morts directes, notamment chute, eau, mine,
hors carte et ordre de mort reçu. Les gardes comparent l'adresse exacte du
soldat actuellement contrôlé ; ni les autres soldats ni le joueur du pair ne
sont concernés. Le journal contient `Absolute protection HEARTBEAT` avec
`hit_live=1` et `die_live=1`.

F10 restaure l'état vivant du joueur actuellement contrôlé si celui-ci est
déjà mort, même si la protection n'était pas cochée avant. La protection reste
à cocher avant le danger : c'est elle qui empêche le paquet de mort local et
le chemin de mort direct. Si le pair a
déjà validé une mort avant cela, il peut encore conserver son propre état mort
— aucune version locale seule ne peut garantir de le réécrire chez lui.

V40 enlève l'ancienne condition erronée qui refusait F10 tant que la case de
protection n'était pas active.

### V43/V45 — réanimation locale complète

V42 ne faisait que remettre `stay_mode=1` et `mode=2`. Le soldat pouvait alors
bouger, mais restait visuellement un squelette et sans réserve de vie : c'était
un état partiel, pas une réanimation réelle. V43 place un crochet temporaire
sur le thread de mise à jour du jeu, vérifie sa signature, puis reproduit la
suite native : état vivant, mode programme et
`cbProc(CB_SET_RESISTANCE, 1, 5000, 0)`. Le crochet est retiré et sa page est
libérée dès l'acquittement. Binaire :
`build\\vs2026-x86\\Release\\HDFinalAdvancedV43_FULL_NATIVE_REVIVE.exe`.

Le test V43 a cependant montré que le callback de résistance refuse son travail
tant que le bit `player+0x2B8` est inactif après une mort. V44 a tenté la grande
routine `SetActive(true)` et a provoqué un crash après une mort réseau : elle
modifie aussi la caméra et les liens de scène. V45 écrit uniquement ce drapeau,
puis appelle le callback de résistance sur le thread du jeu. Binaire :
`build\\vs2026-x86\\Release\\HDFinalAdvancedV45_SAFE_NATIVE_HEALTH.exe`.

Cette réanimation est volontairement **locale**. Si le PC distant a déjà reçu
la mort, il garde le cadavre : aucune écriture sur un seul PC ne peut lui
imposer une résurrection cohérente. Le masque W reste la protection réseau à
activer avant le danger afin d'éviter cette désynchronisation.

### V46 - transition native complete de F10

V46 remplace le raccourci de sante de V45 par la transition native du jeu :
F10 desactive le soldat actuellement controle, remet le soldat mort a l'etat
vivant, puis appelle `C_player::SetActive(true, true)` sur lui depuis le thread
de mise a jour du jeu. Cette routine reconstruit les liens de modele,
d'animation, de scene et de camera, puis initialise sa resistance. Elle attend
deux arguments (`ret 8`) ; V44 n'en fournissait qu'un, ce qui explique le crash.
F10 rend donc le controle au soldat ressuscite.

### V54 — garde d'explosion et réparation visuelle F12

`HDFinalAdvancedV54_EXPLOSION_GUARD.exe` complète la protection réseau du
joueur actuel par un quatrième garde, placé à l'entrée de
`C_player::Explode` (slot vtable `+0xDC`, prologue vérifié
`83 EC 44 53 55 56 8B F1 33`). C'est le chemin distinct employé par les obus
de tank, bazookas, grenades, mines et autres explosions : il était possible
de rester techniquement vivant grâce aux gardes de mort, tout en ayant déjà
subi la transition locale vers le squelette. V54 retourne avant toute perte de
résistance et avant cette transition.

Le journal doit afficher `explosion_live=1` dans le heartbeat de la protection.
La touche **F12 — RESTAURER L'IMAGE** exécute, dans le thread du jeu, la
transition native `SetActive(false,false)` puis `SetActive(true,true)` du
joueur vivant actuel. Elle sert uniquement à corriger l'image d'une ancienne
session déjà restée en squelette; elle n'envoie aucun paquet de mort. Si le
joueur est réellement mort, F12 redirige vers la réanimation native F10.

### V55 — rafale synchronisée avec Bullet Track

`HDFinalAdvancedV55_RAPID_BULLET_SYNC.exe` conserve intégralement la
sélection de tête et les dégâts Bullet Track. Le seul ajout à son trampoline
de création de projectile compare le tireur avec l'arme locale dont le tir
rapide a déjà été validé, puis remet `shoot_countdown` à zéro sur le **thread
du jeu**, immédiatement après chaque projectile réel. Le maintien du bouton
de tir ne dépend donc plus de la cadence de rafraîchissement de l'application.

La publication de la cible ne met également plus `player=0` durant ses trois
écritures de mise à jour. Les vérifications déjà présentes (acteur vivant,
mission, frame tête et racine) restent le garde-fou : une combinaison
temporairement incohérente tombe simplement dans le tir natif, sans envoyer
une balle vers une mauvaise cible. Cela supprime les refus `no_player` qui
pouvaient arriver seulement en rafale très rapide.

### V56 — chaîne de cibles pour rafale Bullet Track

`HDFinalAdvancedV56_BURST_TARGET_CHAIN.exe` corrige le refus observé pendant
une rafale rapide : après la mort de la cible, le radar ne publiait la cible
suivante qu'au rafraîchissement suivant. Les projectiles créés entre-temps
étaient donc refusés avec `dead`, car ils visaient encore l'acteur déjà mort.

Bullet Track publie maintenant la cible exacte sous le viseur et trois cibles
de réserve, classées par proximité du réticule. Le trampoline du thread du jeu
valide chaque acteur dans la mission; si le premier vient de mourir, le tir
suivant passe immédiatement au suivant. Tant que la première cible est
vivante, la visée et les dégâts restent rigoureusement inchangés.

### V57 — tir maintenu natif sans attente d'animation

`HDFinalAdvancedV57_NATIVE_HELD_FIRE.exe` corrige le ralentissement qui restait
alors que Bullet Track ne refusait aucune balle. Dans `C_human::UseItem`, le
jeu bloquait une nouvelle création de projectile tant que l'animation de tir
était marquée complète. V57 contourne uniquement cette attente, seulement pour
l'acteur local dont l'arme rapide est validée, puis remet son
`shoot_countdown` à zéro sur le thread du jeu. Les ennemis, les autres joueurs
et une arme sans l'option « Tir ultra-rapide » passent strictement par le code
natif inchangé.

Le journal doit écrire `Rapid fire: native held-trigger animation gate bypass
installed`. Maintenir le clic doit alors garder un flux régulier de
`forced_damage_count`, sans creux causé par l'animation.

## Compilation## Compilation## Compilation

Prérequis : Visual Studio 2022 ou 2026, CMake ≥ 3.21, accès réseau à la
première configuration (ImGui est téléchargé via FetchContent puis mis en
cache dans `build\...\_deps`).

```powershell
cmake --preset vs2026-x86
cmake --build --preset release-x86-vs2026 --target HDPhase1
# Debug :
cmake --build --preset debug-x86-vs2026 --target HDPhase1
```

Preset alternatif : `vs2022-x86` (Visual Studio 2022). Le projet est réservé
à Win32/x86 ; la configuration échoue volontairement en 64 bits.

## Diagnostic en lecture seule

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\bsp_probe.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\map_probe.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\visibility_probe.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\weapon_probe.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\vehicle_probe.ps1
```

Toutes les sondes utilisent uniquement `ReadProcessMemory`.

## Journal de diagnostic

Le trainer crée un nouveau `hdradar_diag.log` à côté de l'exécutable à chaque
lancement. La section correspondante a été retirée du panneau : le fichier
continue d'être écrit, il suffit de l'ouvrir depuis le dossier de l'exécutable. Il enregistre chaque appui M/K/clic, les décisions Fullhands,
les coordonnées carte/joueur, le rayon, le trampoline SetPos et les positions
réelles relues après la tentative. Pour les séries, le journal doit indiquer
`rotation=1`, `completion_state=1`, `balanced=1` puis
`replacement exact=1`. En cas d'échec, envoyer ce fichier complet.

Pour le Bullet Track, la ligne `[TEST BULLET TRACK]` donne le nombre de tirs
dont les dégâts ont réellement été forcés (`forced_damage_count`) puis, pour
chaque tir refusé, le motif exact du refus :

| Compteur | Signification |
|---|---|
| `no_player` | aucune publication Bullet Track active au moment du tir |
| `wrong_shooter` | le tir ne vient pas du joueur contrôlé |
| `no_actor` | acteur ennemi non publié |
| `no_mission` | pointeur de mission non publié |
| `bad_vector` | liste d'acteurs de la mission incohérente |
| `stale_actor` | l'acteur publié n'est plus dans la liste vivante de la mission |
| `wrong_type` | l'acteur publié n'est pas un ennemi (`type != 2`) |
| `dead` | l'ennemi est mort (`stay_mode == 4`) |
| `no_root` | l'acteur n'a pas de frame racine |
| `wrong_root` | le frame racine ne renvoie pas vers cet acteur |
| `no_head` | l'acteur n'a pas de frame tête (`actor+0x1C8`) |
| `busy_actor` | le projectile possède déjà un autre destinataire |

`retarget` compte les bascules réussies vers une cible de réserve pendant une
rafale. Ce n'est pas une balle perdue.

L'ESP écrit une ligne `[TEST ESP]` à chaque **changement d'état** seulement
(jamais à chaque image) : `visible`, `hidden` avec sa raison, `waiting` quand
le jeu détient l'adaptateur Direct3D, `recovered` après reprise du périphérique
et `failed` en cas d'erreur d'initialisation.

## Test demandé

Fermer les anciens overlays, garder une mission ouverte, puis lancer
uniquement `HDFinalAdvancedV26_7_STEERING_KEYS.exe` avec les mêmes droits que le jeu (il
ferme automatiquement l'instance précédente). Vérifier :

- ESP : mur principal solide rouge ; surface traversable par les tirs verte ;
  seul le visage, le haut de la tête ou une épaule exposé : vert ; corps
  entièrement protégé : rouge ; cible debout, accroupie et couchée ; alliés
  bleus.
- Armes : pas de recul ni dispersion ; cadence maximale et munitions
  inchangées ; restauration à la désactivation et au changement d'arme.
- Gameplay : Super Run, téléportation par clic sur la carte avec K, à pied puis
  au volant d'un véhicule avec ses occupants, invisibilité ennemis (approcher
  un ennemi en marchant/courant : aucune voix, rotation ou poursuite), aimbot,
  Bullet Track en rafale sur une cible visible puis rouge derrière plusieurs
  murs, y compris au-delà de 300 m. Pour l'inventaire, fermer l'écran puis
  appuyer plusieurs fois sur M : chaque lot doit remplacer le précédent, rester
  à 9-10 objets sur le catalogue observé et parcourir exactement 1/14 à
  14/14.
  La vitesse véhicule
  (**N** accélère / **B** réduit, minimum 1.0x) est déjà validée.
- Cheats F3/F4/F6/F7 et profils AZERTY.
- Fermeture du trainer : toutes les valeurs d'origine restaurées, aucun crash
  du jeu.

## V76 - Portee LAN ciblee et securite bazooka

Les trois programmes V76 utilisent un paquet LAN v3 contenant l'identifiant
reseau du soldat, jamais une adresse locale. Avec **Joueur actuel (defaut)**,
le masquage W et le miroir de vie ne concernent que ce soldat ; **Escouade
entiere** reste un choix explicite. Le crochet Client `C_player::Explode` a
ete retire : le crash bazooka du PC ami etait dans ce trampoline. Les gardes
de mort ordinaires restent actifs et F12 publie une sequence de
resynchronisation au Client.
