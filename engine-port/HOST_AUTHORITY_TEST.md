# Test du moteur a autorite hote -- document cree le 2 septembre 2026

## Statut au 2 septembre 2026 : bloque avant installation

Ce guide remplace l'ancienne instruction qui demandait de copier
`HDE_Authority.exe` dans une copie du jeu. Cette instruction etait prematuree.
Le binaire source compile actuellement ne dispose pas encore d'un paquet de
runtimes et DLL moteur compatible, donc il ne doit pas etre teste dans le jeu
et ne doit surtout pas remplacer un `hde.exe` original.

> **Complement du 2 septembre 2026 (audit).** Statut confirme et renforce.
> Le paquet `dist/HDE_Authority_Test` est **interdit de test** ; ses fichiers
> `README.txt` et `TEST.txt` ont ete corriges le meme jour. Un blocage moteur
> supplementaire (filtre `NM_GAME` trop large) empeche de toute facon un
> premier test LAN utile. Protection totale et F12 sont des tests interdits
> jusqu'a nouvel ordre. Voir « Mise a jour apres audit » en fin de document.

Le diagnostic actuel est consigne dans `BUILD_PORT_STATUS.md` : dependances
`MSVCP140.dll` / `VCRUNTIME140.dll` absentes, noms de DLL moteur differents de
ceux attendus par le chargeur historique, et compatibilite ABI non validee.

## Objectif du futur test -- defini le 2 septembre 2026

Quand un paquet complet sera valide, les deux PC utiliseront exactement le meme
build moteur. L'hote simulera toute la mission ; le client enverra uniquement
ses entrees et affichera les resultats officiels produits par l'hote.

Le test ne sera considere valide que si les deux ecrans et les deux journaux
confirment le meme resultat pour chaque evenement : position, tir, impact,
degat, mort, reanimation, inventaire, vehicule et objectif.

## Preconditions obligatoires avant de remettre un paquet -- liste definie le 2 septembre 2026

1. Une copie complete et separee du jeu est preparee dans un dossier neuf.
2. `hde.exe` et toutes ses DLL viennent de la meme compilation source, avec
   leurs noms historiques et dependances x86 verifies.
3. Le paquet demarre localement sans trainer, puis avec le meme mod de jeu.
4. Son journal de lancement ne contient aucune DLL manquante, erreur de chargement
   ou melange de modules anciens/nouveaux.
5. Le hash/version du paquet sont ecrits dans un fichier de livraison ; le
   meme paquet est installe sur l'hote et sur le client.
6. Aucun ancien trainer externe, helper V88/V92, hook memoire ou canal UDP
   d'autorite ne tourne pendant le test moteur.

## Scenarios a executer apres livraison validee -- liste definie le 2 septembre 2026

| Scenario | Action de l'hote | Resultat obligatoire sur les deux PC |
| --- | --- | --- |
| Mouvement/tir client | Le client avance et tire | L'hote simule, les deux PC voient la meme action et le meme impact |
| Perte d'entree client | Couper le flux client | Apres le timeout, le soldat s'arrete ; aucune touche ancienne ne reste active |
| Protection totale | Cocher puis decocher | L'hote publie explicitement ON puis OFF ; les degats/morts concordent et OFF restaure les degats sur les deux PC |
| Escouade complete | Activer la regle host-only | Tous les soldats cibles suivent la decision de l'hote, pas un filtre client |
| Retour a la vie F12 | Demander la reanimation | L'hote valide/reanime ; l'ami voit la meme reapparation, jamais "vivant localement, mort a distance" |
| Inventaire/vehicule/objectif | Executer chaque commande trainer | L'hote accepte ou refuse et replique le resultat officiel |
| Resynchronisation | Simuler perte/reconnexion | Le client demande un snapshot complet et retrouve l'etat hote |

## Regle de securite -- definie le 2 septembre 2026

En cas d'ecart entre les deux ecrans, le test est en echec. Il faut conserver
les deux journaux et corriger le moteur : il ne faut jamais compenser l'ecart
avec un hook local ou un second helper client.

## Mise a jour apres audit -- 2 septembre 2026

Le statut « bloque avant installation » ci-dessus est **confirme et renforce**.
L'audit a montre que le diagnostic de paquetage etait incomplet et qu'un
blocage supplementaire, interne au moteur, empeche de toute facon un premier
test LAN utile.

### Paquet `HDE_Authority_Test` : interdit de test

Le dossier `source/hde/dist/HDE_Authority_Test/` ne doit etre **ni installe, ni
copie dans un jeu, ni fourni au second PC**. Aucun `hde.exe` original ne doit
etre remplace, renomme ou ecrase, dans aucune installation.

Les fichiers `README.txt` et `TEST.txt` de ce dossier conseillaient encore de le
copier sur les deux PC. Ils ont ete corriges le 2 septembre 2026 et portent
desormais un avis de blocage ; leur ancienne procedure y est marquee annulee.

Raisons du blocage, detaillees dans `BUILD_PORT_STATUS.md` :

- **C1** : le binaire importe `MSVCP140.dll`, `VCRUNTIME140.dll` **et 10
  `api-ms-win-crt-*.dll`**. Il faut le redistribuable VC++ 2015-2022 x86 complet
  ou un CRT statique, pas deux fichiers copies.
- **C2** : le binaire charge dynamiquement les **noms historiques** `i3d2.dll`,
  `igraph2.dll`, `inet2.dll`, `isound2.dll`, alors que le paquet ne contient que
  `hd_i3d_2.dll`, `hd_igraph2.dll`, `hd_inet2.dll`, `hd_isnd2.dll`. Dans le
  dossier de test le chargement echoue ; pose dans une vraie installation, il
  chargerait les DLL de 2002 -- melange ABI et **faux positif dangereux**.
- **C3** : `hd_debugmem.dll` est construit mais reference par aucun import.

### Blocage supplementaire cote moteur : R1

Meme avec un paquet reparable, un test LAN lance aujourd'hui echouerait pour une
raison sans rapport avec l'autorite. Le filtre de `GameMission.cpp:2730-2737`
rejette **tout** `NM_GAME` venant d'un client, alors que ce canal transporte
aussi `NM_SYNC`, `NM_MAP`, `NM_CHAT` et `NM_HUMAN_SWITCH_SYNC`. Comme
`C_net_sync` exige la reponse de **tous** les PID avant de resoudre un
evenement, il faut s'attendre a un blocage de la synchronisation, du briefing,
du chat, de la carte, des menus, du vehicule et surtout du **debut de mission**
(`NS_GAME_BEGIN`).

**Ce filtre doit etre corrige avant de mobiliser un second PC**, sinon la
session de test est perdue pour une cause parasite. Detail dans
`AUTHORITY_MIGRATION_MAP.md`.

### Preconditions obligatoires -- liste completee le 2 septembre 2026

Les six preconditions de la section precedente restent valables. S'y ajoutent :

7. **Sauvegarde et versionnement approuves** (R9). `source/`, `release/` et
   `interface/` ne sont **pas versionnes** : `git ls-files source` renvoie vide
   et ces dossiers ne sont pas dans `.gitignore`. Une archive hors depot doit
   exister avant toute modification de packaging. Aucun `git add` et aucun
   commit ne seront lances sans autorisation explicite de l'utilisateur. La
   procedure est decrite dans `BUILD_PORT_STATUS.md` et `plan.md`.
8. **Compatibilite des donnees prouvee** (R12). Le `hde.exe` de `source/hde/bin`
   porte le SHA-256 `1C6712...4AD` alors que les deux installations utilisent
   `5D5EED...62D0` : l'arbre source n'est pas la revision du jeu installe. Le
   paquet doit demarrer localement **sans mod**, puis **avec le mod Ultimate
   5.0**, et lire correctement donnees, tables et scripts, avant tout test LAN.
9. **Filtre `NM_GAME` corrige et journalise** (R1), avec trace du PID emetteur
   pour chaque sous-code rejete, afin de distinguer un rejet voulu d'un blocage
   accidentel.
10. **Comportement au timeout specifie** (R8). Le paquet promet que « le joueur
    cesse de recevoir les anciennes touches » apres 500 ms. Le code fait
    retomber l'acteur en acteur distant sans mise a jour ; arret net, glissade
    ou animation figee n'est pas defini. Le resultat attendu doit etre ecrit
    **avant** le test, sinon l'observation ne sera pas concluante.

### Tests interdits tant que le moteur n'est pas pret

Ajoute le 2 septembre 2026. Protection totale et Retour a la vie F12 n'existent
pas dans le moteur : recherche sur les fichiers reseau et mission, aucune
occurrence de protection, garde de mort, garde de degats, reanimation ou
commande hote.

Ces deux scenarios de la table ci-dessus **ne doivent etre ni executes, ni
annonces, ni presentes comme fonctionnels** tant que les deux conditions
suivantes ne sont pas remplies :

1. `NM_AUTH_SNAPSHOT`, `NM_AUTH_EVENT`, `NM_AUTH_ACK` et `NM_AUTH_RESYNC`
   existent, et le client rend les acteurs non locaux **uniquement** depuis
   l'etat officiel recu ;
2. `NM_AUTH_COMMAND` et `NM_AUTH_COMMAND_RESULT` existent, et chaque commande
   est journalisee des deux cotes avec `acceptee` / `refusee` et sa raison.

Tester Protection totale ou F12 avant cela ne produirait aucune information
exploitable et reproduirait l'echec de V92 : une commande qui arrive, un journal
qui dit « actif », et deux ecrans qui ne montrent pas la meme chose.

### Ordre d'execution des scenarios -- 2 septembre 2026

Les scenarios de la table ci-dessus ne sont pas simultanement disponibles. Ils
s'ouvrent dans cet ordre, chacun apres l'etape correspondante du plan
d'execution de `plan.md` :

| Scenario | Ouvert apres l'etape |
| --- | --- |
| Demarrage local, sans mod puis avec mod | 3 et 4 -- lancements locaux |
| Mouvement / tir client | 6 -- test LAN minimal |
| Perte d'entree client (timeout 500 ms) | 6 -- test LAN minimal, apres specification de R8 |
| Etat identique des acteurs non locaux | 8 -- instantanes / ACK / resynchronisation |
| Resynchronisation apres perte et reconnexion | 8 -- instantanes / ACK / resynchronisation |
| Refus d'un binaire different | 9 -- handshake version/hash |
| Inventaire, vehicule, objectif | 10 -- commandes trainer host-only |
| **Protection totale** (cocher puis decocher) | 11 -- apres 8 et 10, jamais avant |
| **Escouade complete** | 11 -- apres 8 et 10 |
| **Retour a la vie F12** | 12 -- apres 8 et 10, jamais avant |

La regle de securite de la section suivante s'applique a chacun : en cas
d'ecart entre les deux ecrans, le test est en echec, on conserve les deux
journaux et on corrige le moteur. Jamais un hook local, jamais un second helper
client.
