#include "gameplay_mods.h"

#include "diagnostics.h"
#include "weapon_mods.h"

#include <WinSock2.h>
#include <Windows.h>
#include <WS2tcpip.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <string>
#include <utility>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <limits>
#include <thread>
#include <type_traits>
#include <vector>

namespace hd
{
namespace
{
constexpr float kMinimumSpeedMultiplier = 1.0f;
constexpr float kMaximumSpeedMultiplier = 80.0f;
constexpr float kSpeedMultiplierPerSecond = 40.0f;
constexpr float kMinimumAimbotDistance = 40.0f;
// V97 : plafond porte de 220 m a 1000 m. La distance n'est qu'un filtre au
// carre applique aux entites du radar, et la valeur 0 y signifie deja « aucune
// limite » - c'est ce que Bullet Track utilise. Rien dans le moteur ne borne
// la portee d'un tir : la balle resout sa collision par un rayon.
constexpr float kMaximumAimbotDistance = 1000.0f;
constexpr float kMinimumBulletTrackRadius = 40.0f;
constexpr float kMaximumBulletTrackRadius = 500.0f;
constexpr std::uintptr_t kActorFrameOffset = 0x28;
constexpr std::uintptr_t kActorModelOffset = 0x24;
constexpr std::uintptr_t kActorMoveDirectionOffset = 0x1AC;
constexpr std::uintptr_t kActorFallSpeedOffset = 0x1B8;
constexpr std::uintptr_t kActorCurrentPoseOffset = 0x1BC;
constexpr std::uintptr_t kActorFallingOffset = 0x264;
constexpr std::uintptr_t kActorCollisionTestCountdownOffset = 0x274;
constexpr std::uintptr_t kFrameActorBackReferenceOffset = 0x80;
constexpr std::uintptr_t kFrameSetPositionVtableOffset = 0x0C;
constexpr float kTeleportGroundClearance = 0.08f;
constexpr float kMinimumNoclipSpeed = 1.0f;
constexpr float kMaximumNoclipSpeed = 80.0f;
// Reaching a useful flight speed took several seconds of holding F8. The ramp
// is now proportional: a large fixed step plus a share of the current speed, so
// it accelerates as it climbs and slows down just as quickly.
constexpr float kNoclipSpeedPerSecond = 30.0f;
constexpr float kNoclipSpeedGrowthPerSecond = 1.2f;
constexpr float kNoclipReleasedFallSpeed = 0.01f;
// Noclip owns translation and must keep the native collision pass asleep;
// forcing that pass merely to produce a network update makes walls solid
// again. Network publication therefore remains a separate concern.
constexpr std::int32_t kNoclipCollisionHoldMs = 1000;
constexpr std::uintptr_t kHumanTickRva = 0x0001'A560;
constexpr std::size_t kNoclipHookPatchSize = 9;
// C_human::Tick after its `model == nullptr` early return. At this point EBX
// is still the ticking C_human*, which lets a one-shot command run on the
// engine's own update thread without synthesising a keyboard event.
constexpr std::uintptr_t kLifeUnlimitedTickHookRva = 0x0001'A583;
constexpr std::size_t kLifeUnlimitedTickHookPatchSize = 6;
constexpr std::size_t kLifeUnlimitedRemoteSize = 0x200;
constexpr std::size_t kLifeUnlimitedTargetOffset = 0x180;
constexpr std::size_t kLifeUnlimitedCompletionOffset = 0x184;
// The fall guard uses the same verified C_human::Tick point as the one-shot
// Life Unlimited command.  It runs immediately before the native collision
// code, so it can clear only the "already falling" flag for one actor while
// leaving the engine's velocity and position integration untouched.
constexpr std::size_t kProtectedFallRemoteSize = 0x200;
constexpr std::size_t kProtectedFallTargetOffset = 0x180;
constexpr std::size_t kProtectedFallLifeTargetOffset = 0x184;
constexpr std::size_t kProtectedFallLifeCompletionOffset = 0x188;
// V97 : message et premier parametre du rappel a jouer sur le thread du jeu.
constexpr std::size_t kProtectedFallCallbackMessageOffset = 0x18C;
constexpr std::size_t kProtectedFallCallbackPrm1Offset = 0x190;
constexpr std::uintptr_t kActorTypeOffset = 0x1C;
constexpr std::uint32_t kPlayerCheatCallback = 14;
constexpr std::uint32_t kLifeUnlimitedCheat = 14;
constexpr std::size_t kNoclipRemoteSize = 0x400;
// In C_human::Tick this is the first instruction after the collision branch.
// When collision is intentionally asleep for noclip, normal code reaches this
// site with its local `upd` flag clear and skips NM_HUMAN_POS.  The small
// second trampoline changes only that local flag for our current player, so
// the existing native packet builder runs without re-enabling collision.
constexpr std::uintptr_t kNoclipNetworkPublishRva = 0x0001'BC4E;
constexpr std::size_t kNoclipNetworkPublishHookPatchSize = 11;
constexpr std::size_t kNoclipRemotePublishCountOffset = 0x33C;
constexpr std::size_t kNoclipRemoteActiveOffset = 0x300;
constexpr std::size_t kNoclipRemoteActorOffset = 0x304;
constexpr std::size_t kNoclipRemoteVelocityOffset = 0x308;
constexpr std::size_t kNoclipRemotePositionOffset = 0x314;
constexpr std::size_t kNoclipRemoteElapsedOffset = 0x320;
constexpr std::size_t kNoclipRemoteSecondsPerMsOffset = 0x324;
// The frame the hook actually moves. On foot this is the player's own frame;
// while driving it is the vehicle's, so the whole car travels and the seated
// player follows it as its child. Publishing the frame instead of deriving it
// from the actor is what makes the same hook serve both cases.
constexpr std::size_t kNoclipRemoteBodyFrameOffset = 0x328;
// Incremented on every accepted pass so the journal can prove which of the
// two tick hooks is actually driving the movement.
constexpr std::size_t kNoclipRemoteTickCountOffset = 0x32C;
// The automobile being flown, or 0 on foot. Its own trampoline compares this
// against `ecx`, so publishing 0 is what keeps the two hooks exclusive.
constexpr std::size_t kNoclipRemoteVehicleOffset = 0x330;
constexpr std::size_t kNoclipRemoteVehiclePassOffset = 0x334;
// The mission scene, republished every frame. The vehicle trampoline re-links
// the moved car into the right BSP sector through it, exactly as the engine's
// own move sequence does. Zero simply skips the re-link.
constexpr std::size_t kNoclipRemoteSceneOffset = 0x338;
// I3D_frame::Update in i3d2.dll. Its prologue tests the very dirty bit the
// trampoline sets:  mov eax,[ecx+0Ch] / test eax,00800000h. Writing the local
// position alone left that bit pending, so the car's world matrix was only
// recomputed when native physics resumed -- which is why it appeared to jump
// into place on toggle-off instead of moving.
constexpr std::uintptr_t kI3dFrameUpdateRva = 0x0000'34D0;
// C_automobile::Tick(const S_tick_context&). Reached from the call graph of
// the speed patches at 0x0044F1F3/0x0044F22C, which sit inside it. thiscall,
// so ecx is the car at entry, and its argument lands at [esp+28h] once the
// trampoline has pushed flags and registers -- the same offset C_human::Tick
// uses, so the time extraction below is identical.
constexpr std::uintptr_t kAutomobileTickRva = 0x0004'E540;
constexpr std::size_t kVehicleNoclipHookPatchSize = 6;
constexpr std::size_t kVehicleNoclipRemoteSize = 0x200;
constexpr float kVehicleTeleportGroundClearance = 0.65f;
constexpr std::uint32_t kUseAutomobileCallback = 43;
constexpr unsigned kTeleportClosedMapSamples = 3;
constexpr ULONGLONG kTeleportMapCloseTimeoutMs = 3000;
constexpr std::uintptr_t kFrameWorldPositionOffset = 0xBC;
constexpr std::uintptr_t kFrameLocalPositionOffset = 0x14C;
// This is the source-confirmed `net_sync_pos` member of C_human. The native
// compiler emits the network position message at kHumanPositionSendRva. At
// that exact point `ebx` is C_human* and the three `to` coordinates are local
// variables at [ebp-10h], [ebp-0Ch], [ebp-08h]. Replacing only those locals
// changes the outgoing message, never the real frame position.
constexpr std::uintptr_t kHumanNetworkSyncPositionOffset = 0x204;
constexpr std::uintptr_t kHumanPositionSendRva = 0x0001'BD97;
constexpr std::size_t kNetworkPositionMaskHookPatchSize = 9;
constexpr std::size_t kNetworkPositionMaskRemoteSize = 0x240;
constexpr std::size_t kNetworkPositionMaskActiveOffset = 0x180;
constexpr std::size_t kNetworkPositionMaskCountOffset = 0x184;
constexpr std::size_t kNetworkPositionMaskEntriesOffset = 0x188;
constexpr std::size_t kNetworkPositionMaskMaximumActors = 8;
// The authority Client helper is already deliberately present on the second
// PC. This separate, LAN-only message controls its *local renderer*; it is
// not an H&D network packet and never changes an actor, health or ownership.
constexpr std::uint32_t kPeerVisualCommandMagic = 0x50445648U; // "HVDP"
constexpr std::uint16_t kPeerVisualCommandVersion = 6;
constexpr unsigned short kPeerVisualCommandPort = 48217;
constexpr DWORD kPeerVisualCommandHeartbeatMs = 350U;
constexpr std::uint16_t kPeerAllHostPlayersNetworkId = 0xFFFFU;
// Post-SetPos verification: sample the frame world position for a few
// trainer ticks and detect whether the game re-snaps it to the actor's old
// pose. Beyond this distance the pose is considered overwritten.
constexpr float kTeleportVerifySnapDistance = 1.5f;
constexpr unsigned kTeleportVerifySamples = 8;
constexpr ULONGLONG kTeleportVerifyTimeoutMs = 1200;
constexpr std::uintptr_t kPlayerIsEnemyVtableOffset = 0xA4;
// C_human::Hit is the virtual slot used by both locally simulated bullets and
// NM_HUMAN_HIT packets.  The installed Deluxe binary resolves it to
// C_player::Hit at 0x00429100.  Unlike IsEnemy, this is the point at which
// resistance is changed, so it also covers an attack already authorised by a
// remote peer before our perception filter ran.
constexpr std::uintptr_t kPlayerHitVtableOffset = 0x104;
// C_player overrides C_human::Die at this virtual slot. The old protection
// guarded only the C_human base routine, which was too late in LAN: the
// C_player override builds its NM_HUMAN_DIE packet before it calls the base
// routine. Guarding this entry rejects death before the local state or the
// outbound death packet exists.
constexpr std::uintptr_t kPlayerDieVtableOffset = 0x100;
// C_player::Explode is a separate path from Hit.  Explosive shells, bazookas,
// grenades and mines enter here and C_human::Explode subtracts resistance
// before any of the Die guards have a chance to run.  Guard its virtual entry
// instead, so the model/death transition is never started for our player.
constexpr std::uintptr_t kPlayerExplodeVtableOffset = 0xDC;
// Source and installed-binary confirmed C_player::no_hit_cheat. The native
// `immortality` cheat (CB_CHEAT, 14) toggles this exact byte and
// C_player::cbProc(CB_HIT) returns before dispatching C_player::Hit while it
// is set. Unlike a shared function hook, this state belongs to each player
// instance, which is the correct primitive for current-player versus squad
// scope (including Ultimate Mod player actors).
constexpr std::uintptr_t kPlayerNoHitCheatOffset = 0x2D4;

constexpr std::uintptr_t kActorInventoryBeginOffset = 0x5C;
// V116 - la base de `C_inventory` DANS un soldat, DEDUITE et non transposee.
//
// La V115 employait +0x54, releve sur le binaire de reference par les
// `lea ecx,[objet+0x54]` qui precedent les appels a `AddItem`. Le journal du
// joueur a montre l'erreur : tout se deroulait normalement jusqu'a la ligne
// « Miroir d'arme: ... execute=1 », et le jeu s'arretait juste apres.
//
// La declaration de la classe donne la reponse :
//
//   class C_inventory{
//      class C_game_menu &game_menu;              // 4 octets
//      vector<C_smart_ptr<S_item> > items;        // debut, fin, capacite
//      ...
//
// `items` est le SECOND membre; son `debut` se trouve donc a base + 4. Or le
// trainer sait, et le jeu valide a chaque tir, que ce debut est en +0x5C. La
// base vaut donc 0x5C - 4 = +0x58, et non +0x54 : le +0x54 du binaire de
// reference correspond a un objet decale de quatre octets dans cette region,
// exactement le genre de transposition qui a deja coute plusieurs sessions.
//
// On la DERIVE donc de la valeur validee, au lieu de l'ecrire en dur : si le
// debut du vecteur bouge un jour, la base suivra.
// V130 - le vecteur commence a `this + 8`, et c'est MESURE.
//
// La V116 avait pose cette base a `debut du vecteur - 4`, en supposant que le
// vecteur commence par son pointeur de debut. Le journal du joueur montre le
// resultat :
//
//   Inventaire du soldat 1 : 1 objet(s), identifiants = 30
//                            (l'arme demandee etait 14).
//
// Un seul objet, le 30 - celui que le constructeur du moteur donne a tout
// humain. `AddItem` ne rajoutait RIEN : appele avec un `this` decale de quatre
// octets, il travaillait a cote du vecteur. Les soldats n'ont jamais recu
// l'arme; ce n'etait donc pas un probleme de degainage.
//
// Le desassemblage d'`AddItem` tranche, sans supposition :
//
//   8B E9         mov ebp,ecx          ; ebp = this (C_inventory*)
//   ...
//   8B 4D 08      mov ecx,[ebp+8]      ; items.begin = this + 8
//   8B 45 0C      mov eax,[ebp+0x0C]   ; items.end   = this + 0x0C
//
// Le vecteur commence donc a `this+8` et non `this+4` : l'ancien MSVC place un
// membre allocateur de quatre octets devant les pointeurs. Le trainer sait, et
// le jeu valide a chaque tir, que ce debut est en `acteur+0x5C`. La base vaut
// donc 0x5C - 8 = 0x54 - exactement ce que les sites d'appel du binaire de
// reference indiquaient, et que j'avais « corrige » a tort.
constexpr std::uintptr_t kInventoryVectorOffsetInInventory = 8;
constexpr std::uintptr_t kInventoryBaseOffset =
    kActorInventoryBeginOffset - kInventoryVectorOffsetInInventory;


// =====================================================================
// V117 - les cheats natifs ne s'appliquent QU'AUX soldats de la mission
// =====================================================================
//
// Le journal du joueur montre le miroir d'arme distribuant trois armes de
// suite sans incident, puis le jeu qui meurt sur la touche M :
//
//   Miroir d'arme: objet 101 x978 donne a 5 soldat(s) execute=1.
//   Miroir d'arme: objet  14 x200 donne a 5 soldat(s) execute=1.
//   Miroir d'arme: objet  30 x200 donne a 5 soldat(s) execute=1.
//   [TEST FULLHANDS #1] INPUT source=M ...
//   Fullhands: trigger sent, waiting for completion.
//   Fullhands: completion_state=0 selected=0 after 1000 polls.
//   Process: pid=0 (was pid=10700)
//
// La correction des quatre octets de la V116 tient donc : le miroir n'est pas
// en cause. Ce qui a tue le jeu, c'est `Fullhands`, qui agit sur le soldat
// PILOTE - et le joueur pilotait un soldat CREE.
//
// Les cheats natifs rejouent des chemins du moteur ecrits pour un soldat de
// mission : ils lisent sa table, son entree de bandeau, son inventaire de
// depart. Un soldat cree n'a pas ete initialise par `MissionLoad`; ces chemins
// n'ont donc rien de valide a lire. Ils sont desormais refuses sur un soldat
// cree, avec une ligne de journal explicite plutot qu'un arret du jeu.
std::vector<std::uintptr_t> g_spawned_soldiers{};

// Vrai quand le bandeau n'offre aucune case attribuable - c'est-a-dire
// quand la mission compte quatre soldats d'origine. Les soldats crees sont
// alors rendus invulnerables plutot que de faire s'arreter le jeu.
bool g_created_soldiers_invulnerable = false;

bool IsCreatedSoldier(std::uintptr_t actor)
{
    return actor != 0 &&
        std::find(
            g_spawned_soldiers.begin(), g_spawned_soldiers.end(), actor) !=
        g_spawned_soldiers.end();
}
// V97, 3 septembre 2026 - Sante Max natif.
// Le cheat `ironman` du moteur fait exactement ceci, mais seulement hors
// reseau (`case 8: if(!net){ SetResistance(init_resistance = 20000); }` dans
// C_player::cbProc). Les deux champs ont ete lus dans le binaire de reference
// `source/hde/bin/HDE.exe` (build du 13 mai 2002, fourni avec sa table de
// symboles) : le site du cheat y compile en
//     mov eax,20000 / mov [esi+2C4h],eax / mov [esi+2Ch],eax
// soit init_resistance = +0x2C4 et resistance = +0x2C.
// Ce build n'est pas celui du jeu installe : trois champs tardifs y sont
// decales de 0x14 par rapport a la revision Deluxe deja cartographiee par le
// trainer (stay_mode 0x240 -> 0x254, mode 0x2A0 -> 0x2B4, no_hit_cheat
// 0x2C0 -> 0x2D4). Le decalage est constant sur les trois, donc
// init_resistance passe de 0x2C4 a 0x2D8. `resistance`, lui, est situe dans
// l'en-tete de C_actor - juste apres frame (+0x28) et avant network_owner
// (+0x34), deux offsets identiques dans les deux builds - donc non decale.
// La deduction reste une deduction : chaque ecriture est precedee d'une
// relecture de controle qui refuse d'agir si les valeurs lues ne ressemblent
// pas a une vie de soldat. Voir ValidateExtendedHealthLayout.
constexpr std::uintptr_t kPlayerResistanceOffset = 0x2C;
constexpr std::uintptr_t kPlayerInitResistanceOffset = 0x2D8;
constexpr std::int32_t kExtendedHealthValue = 20000;
// La vie native vaut `200 + endurance * 1400` (Actors.cpp), donc au plus
// 1600. La marge autorise un mod genereux sans jamais accepter une lecture
// aberrante qui signalerait un mauvais offset.
constexpr std::int32_t kMaximumNativeInitResistance = 6000;
// The base entry is also needed for incoming NM_HUMAN_DIE. The Ultimate Mod
// can dispatch that packet straight to C_human::Die instead of returning
// through C_player::Die.
constexpr std::uintptr_t kHumanDieRva = 0x0002'08B0;
constexpr std::size_t kHumanDieHookPatchSize = 9;
constexpr std::size_t kPlayerExplodeHookPatchSize = 10;
constexpr std::size_t kAbsoluteProtectionRemoteSize = 0x100;
constexpr std::size_t kProtectionRemoteTargetOffset = 0xC0;
constexpr std::uintptr_t kHumanStayModeOffset = 0x254;
constexpr std::uintptr_t kPlayerModeOffset = 0x2B4;
constexpr std::uint32_t kHumanStayModeAlive = 1;
constexpr std::uint32_t kPlayerModeProgram = 2;
constexpr std::uint32_t kHumanStayModeDead = 4;
// The native `newlife` branch does not merely flip stay_mode.  It immediately
// sends CB_SET_RESISTANCE (26, 1, 5000, 0) through C_player::cbProc, which
// recreates the live resistance/version state.  Calling that callback from the
// trainer thread races the game, so a short C_human::Tick hook performs it on
// the game's own update thread and then removes itself.
constexpr std::size_t kPlayerReviveRemoteSize = 0x240;
constexpr std::size_t kPlayerReviveStateOffset = 0x200;
constexpr std::size_t kPlayerReviveTargetOffset = 0x204;
constexpr std::size_t kPlayerReviveSourceOffset = 0x208;
// The CB_SET_RESISTANCE handler explicitly returns without touching health
// while player+0x2B8 is zero, which is precisely the state left by
// C_human::Die. The active flag is the narrow condition needed by that
// handler; calling the much broader C_player::SetActive(true) after a LAN
// death also changes camera/scene ownership and proved unsafe in V44.
constexpr std::uintptr_t kPlayerSetActiveVtableOffset = 0x6C;
constexpr std::uint32_t kPlayerReviveQueued = 1;
constexpr std::uint32_t kPlayerReviveCompleted = 2;
constexpr std::uint32_t kPlayerReviveRejected = 3;
constexpr ULONGLONG kPlayerReviveTimeoutMs = 2500;
constexpr std::uintptr_t kFrameWorldMatrixOffset = 0x8C;
constexpr std::uintptr_t kActorAimDirectionOffset = 0x1D0;
constexpr std::uintptr_t kActorAimCountOffset = 0x1E8;
constexpr std::uintptr_t kActorBrestFrameOffset = 0x1C4;
constexpr std::uintptr_t kFrameParentOffset = 0x18;
constexpr std::int32_t kAimbotAimCount = 2500;
constexpr std::size_t kAimbotRemoteSize = 0x100;
constexpr std::size_t kAimbotEnabledOffset = 0xC0;
constexpr std::size_t kAimbotDeltaOffset = 0xC4;
constexpr std::size_t kAimbotObservedDeltaOffset = 0xD0;
constexpr std::size_t kAimbotObservedSequenceOffset = 0xDC;
constexpr std::uintptr_t kActorUsingItemOffset = 0x250;
constexpr std::uintptr_t kHumanProgramOffset = 0x284;
constexpr std::uintptr_t kHumanProgramDirtyOffset = 0x290;
constexpr std::uintptr_t kHumanCommandNewOffset = 0x291;
constexpr std::uintptr_t kHumanHoldingFireOffset = 0x292;
constexpr std::uintptr_t kHumanActorEnumCountOffset = 0x294;
constexpr std::uintptr_t kHumanWatchActorsOffset = 0x298;
constexpr std::uintptr_t kCommandSubjectOffset = 0x04;
constexpr std::uintptr_t kMissionPointerRva = 0x0010AD4C;
constexpr std::uintptr_t kMissionActorBeginOffset = 0x68;
constexpr std::uintptr_t kMissionActorEndOffset = 0x6C;
constexpr std::uintptr_t kMissionMapManagerOffset = 0xA0;
constexpr std::uintptr_t kMissionMapActiveOffset = 0xA4;
constexpr std::uint32_t kActorTypePlayer = 1;
// V142 - meme valeur que dans le radar : ACTOR_ENEMY.
constexpr std::uint32_t kActorTypeEnemy = 2;
constexpr std::uint32_t kActorTypeAutoCannon = 8;
constexpr std::uint32_t kActorTypeAutomobile = 16;
constexpr std::uintptr_t kAutomobileSeatsOffset = 0x140;
constexpr std::size_t kAutomobileSeatSize = 0x20;
constexpr std::size_t kAutomobileSeatCount = 8;
constexpr std::uintptr_t kAutomobileSeatUserOffset = 0x0C;
constexpr std::uintptr_t kAutomobileSpeedOffset = 0x244;
// C_version::v_resistance. C_version::HitExplode is at 0x0044CF50 and opens
// with exactly the source's guard:
//   mov eax,[esi+6Ch] / test eax,eax / je <return false>
//   ... cmp curr_version,[version_list.size()-1] / jae <return false>
// so a resistance of zero makes every hit -- enemy fire, a fall, a crash --
// leave the function before any damage is subtracted and before the
// explosion actor is ever created. No hook, no ownership: one integer.
constexpr std::uintptr_t kVersionResistanceOffset = 0x6C;
constexpr std::uintptr_t kVersionCurrentIndexOffset = 0x70;
constexpr std::uintptr_t kVersionListBeginOffset = 0x5C;
// V100 - reparation visuelle en place, sans sortir du vehicule.
// I3D_frame garde ses drapeaux a +0x0C et le bit prive FRMFLAGS_ON y vaut
// 0x00020000 : c'est ce bit que I3D_frame::SetOn(false) efface, apres quoi le
// moteur de rendu saute toute la hierarchie du modele. Le compagnon CLIENT
// l'ecrit deja directement pour masquer un soldat, et cela fonctionne depuis la
// V63; ici il sert a cacher la carrosserie enfoncee et a montrer l'intacte.
constexpr std::uintptr_t kFrameFlagsOffset = 0x0C;
constexpr std::uint32_t kFrameOnFlag = 0x00020000;
constexpr std::size_t kMaximumVehicleVersions = 16;
constexpr std::uintptr_t kVersionListEndOffset = 0x60;
constexpr std::int32_t kMaximumVersionResistance = 1'000'000;
// C_automobil::mode, and the value that marks the car as wrecked. The switch
// at 0x0044FCA2 (`cmp eax,4 / ja / jmp [eax*4+...]`) proves the field holds
// five states, matching MODE_STAY..MODE_DESTROYED, so MODE_DESTROYED is 4.
//
// Hooking Destroy was not enough: `mov dword ptr [reg+74h],4` appears at FOUR
// sites -- 0x0044FEE6 inside Destroy itself, plus 0x00450976, 0x00452BEE and
// 0x00454805 where the compiler inlined it. That is why the car could still be
// counted as destroyed, and the escort mission failed, with no explosion.
// Restoring the field covers every one of them, present and unfound.
constexpr std::uintptr_t kAutomobileModeOffset = 0x74;
constexpr std::uint32_t kAutomobileModeDestroyed = 4;
constexpr std::uint32_t kAutomobileModeStopped = 3;
// C_automobil::Destroy(bool exploded, bool net_send, dword code, byte bits).
// Zeroing v_resistance only closes the explosion path. A crash fast enough to
// wreck but too slow to explode takes the other branch entirely:
//   if(curr_speed > destroy_speed){
//      if(curr_speed > explode_speed) HitExplode(this, 4000, ...);
//      else                           Destroy(false, true, 1, bits);
//   }
// Water and the mine field call it directly too. The address comes from the
// call site at 0x0045176E, right after the confirmed HitExplode at 0x0044CF50.
// The trampoline returns before the function body for the protected car, so
// `mode` never becomes MODE_DESTROYED and the motor is never cut.
constexpr std::uintptr_t kAutomobileDestroyRva = 0x0004'FC80;
constexpr std::size_t kAutomobileDestroyPatchSize = 6;
constexpr std::size_t kVehicleDestroyRemoteSize = 0x100;
constexpr std::size_t kVehicleDestroyVehicleOffset = 0x80;
constexpr std::uintptr_t kAutomobileTransmissionOffset = 0x258;
constexpr std::int32_t kAutomobileReverseTransmission = -1;
constexpr std::int32_t kAutomobileMaximumForwardTransmission = 6;
constexpr std::uintptr_t kAutomobileTargetSpeedHookRva = 0x0004'F1F3;
constexpr std::uintptr_t kAutomobileAccelerationHookRva = 0x0004'F22C;
constexpr std::size_t kVehicleSpeedRemoteSize = 0x100;
constexpr std::size_t kVehicleAddressRemoteOffset = 0xE0;
constexpr std::size_t kVehicleMultiplierRemoteOffset = 0xE4;

// Boucle principale de hde.exe. Le moteur moyenne le temps ecoule sur huit
// images, puis "sar eax,3" suivi de "mov [esp+14h],eax" fixe le pas de temps
// que tick_class->Tick(tc) distribue a toute la simulation. Multiplier eax a
// cet endroit accelere le jeu entier sans toucher a la mesure reelle du temps
// faite par igraph2.dll : aucune derive d'horloge ne s'accumule.
constexpr std::uintptr_t kMainLoopTimeScaleHookRva = 0x000C'D787;
constexpr std::size_t kGameSpeedRemoteSize = 0x80;
constexpr std::size_t kGameSpeedFactorRemoteOffset = 0x60;
constexpr float kMinimumGameSpeedMultiplier = 1.0f;
// Le moteur borne une image a 100 ms (MIN_FPS) et la moyenne glissante reste
// donc dans [5, 100] ms. Meme au plafond, 100 x 100 x 256 tient tres au large
// dans un entier 32 bits, donc l'imul du trampoline ne peut pas deborder.
// Au-dela d'une dizaine de fois, la physique travaille sur des pas de temps
// que le moteur ne voit jamais normalement : c'est volontairement laisse au
// joueur, une pression sur 8 ramene instantanement a une vitesse tenable.
constexpr float kMaximumGameSpeedMultiplier = 100.0f;
// Le pas grandit avec la vitesse : reglage fin en bas de l'echelle, et le
// plafond reste atteignable en une vingtaine de pressions.
constexpr float kGameSpeedFineStep = 0.5f;
constexpr float kGameSpeedMediumStep = 2.5f;
constexpr float kGameSpeedCoarseStep = 10.0f;
constexpr float kGameSpeedFineLimit = 5.0f;
constexpr float kGameSpeedMediumLimit = 20.0f;

float GameSpeedStep(float current)
{
    if (current < kGameSpeedFineLimit)
        return kGameSpeedFineStep;
    if (current < kGameSpeedMediumLimit)
        return kGameSpeedMediumStep;
    return kGameSpeedCoarseStep;
}
// Steering rate. The cbProc CB_USE_AUTO case 4 computes
//   SetWheelTurn(IntAsFloat(prm2) * 0.6f)
// and that multiply is the single instruction at 0x004552E9:
//   D8 0D C0 66 4F 00   fmul dword ptr ds:[004F66C0h]   ; 0.6f
// wheel_turn then accumulates towards its +-1 clamp at [esi+0x268]. The rate
// is constant, so reaching full lock takes about a second whatever the speed:
// at a high multiplier the car has already crossed the map by then, which is
// why it feels impossible to steer. Scaling this one factor with the
// multiplier restores control without touching the physics.
constexpr std::uintptr_t kAutomobileWheelTurnRateRva = 0x0005'52E9;
constexpr std::size_t kAutomobileWheelTurnPatchSize = 6;
constexpr std::uintptr_t kStockWheelTurnRateAddress = 0x004F'66C0;
constexpr std::size_t kWheelTurnRemoteSize = 0x100;
constexpr std::size_t kWheelTurnVehicleOffset = 0x80;
constexpr std::size_t kWheelTurnRateOffset = 0x84;
constexpr float kStockWheelTurnRate = 0.6f;
// Tying the rate to the speed multiplier made it unusable: at 40x it reached
// full lock inside one frame and the car could not be steered at all, only
// flicked. The boost is its own setting now, so it stays predictable whatever
// the speed.
constexpr float kMinimumSteeringBoost = 1.0f;
constexpr float kMaximumSteeringBoost = 8.0f;
constexpr std::size_t kEnemyInvisibilityRemoteSize = 0x100;
// V95, 3 septembre 2026 17:22 - bascule de portee instantanee.
// La portee et l'acteur protege ne sont plus compiles en dur dans les
// trampolines IsEnemy : ils vivent dans deux mots de leur propre page. Changer
// de portee ou de soldat controle coute alors une seule ecriture de 4 octets,
// sans retrait, sans attente et sans la moindre image sans filtre. C'est le
// mecanisme deja eprouve par le compagnon CLIENT pour sa cible reseau.
constexpr std::size_t kEnemyFilterScopeOffset = 0xC0;
constexpr std::size_t kEnemyFilterTargetOffset = 0xC4;
constexpr std::uint32_t kEnemyFilterScopeControlled = 0;
constexpr std::uint32_t kEnemyFilterScopeWholeSquad = 1;
constexpr std::array<std::uint8_t, 5> kPlayerIsEnemyOriginalPrefix{
    0x8B, 0x4C, 0x24, 0x04, 0x8B};
constexpr std::array<std::uint8_t, 8> kPlayerIsEnemyOriginalSuffix{
    0x41, 0x1C, 0x48, 0x74, 0x23, 0x48, 0x75, 0x20};

struct PlayerSpeedPatchState
{
    DWORD process_id = 0;
    std::uintptr_t actor = 0;
    std::int32_t pose = -1;
    Vector3 original{};
    Vector3 last_written{};
    bool applied = false;
};

PlayerSpeedPatchState g_player_speed{};
ULONGLONG g_last_speed_input_tick = 0;

struct EnemyInvisibilityPatchState
{
    DWORD process_id = 0;
    std::uintptr_t function = 0;
    std::uintptr_t protected_player = 0;
    std::uintptr_t remote = 0;
    std::size_t remote_size = 0;
    std::array<std::uint8_t, 5> original{};
    std::array<std::uint8_t, 5> patch{};
    EnemyInvisibilityScope scope = EnemyInvisibilityScope::WholeSquad;
    ULONGLONG next_reset_attempt = 0;
    bool awareness_reset = false;
    bool applied = false;
};

EnemyInvisibilityPatchState g_enemy_invisibility{};

struct EnemyHearingPatchState
{
    DWORD process_id = 0;
    std::uintptr_t function = 0;
    std::uintptr_t protected_player = 0;
    std::uintptr_t remote = 0;
    std::size_t remote_size = 0;
    std::array<std::uint8_t, 7> original{};
    std::array<std::uint8_t, 7> patch{};
    EnemyInvisibilityScope scope = EnemyInvisibilityScope::WholeSquad;
    bool applied = false;
};

EnemyHearingPatchState g_enemy_hearing{};

// Ecrit la selection courante dans les deux emplacements du trampoline.
// `changed` signale une bascule reelle : l'appelant relance alors la purge des
// perceptions deja memorisees, pour la nouvelle cible.
template <typename State>
bool ApplyEnemyFilterSelection(
    TrainerProcess& process,
    State& state,
    EnemyInvisibilityScope scope,
    std::uintptr_t protected_player,
    bool& changed)
{
    changed = false;
    if (!state.applied || state.remote == 0)
        return true;
    if (state.scope != scope)
    {
        const std::uint32_t desired =
            scope == EnemyInvisibilityScope::WholeSquad
            ? kEnemyFilterScopeWholeSquad
            : kEnemyFilterScopeControlled;
        if (!process.WriteMemory(
                state.remote + kEnemyFilterScopeOffset, desired))
        {
            return false;
        }
        state.scope = scope;
        changed = true;
    }
    if (state.protected_player != protected_player)
    {
        if (!process.WriteMemory(
                state.remote + kEnemyFilterTargetOffset,
                static_cast<std::uint32_t>(protected_player)))
        {
            return false;
        }
        state.protected_player = protected_player;
        changed = true;
    }
    return true;
}

struct PlayerDamagePatchState
{
    DWORD process_id = 0;
    std::uintptr_t function = 0;
    std::uintptr_t remote = 0;
    std::size_t remote_size = 0;
    std::array<std::uint8_t, 5> original{};
    std::array<std::uint8_t, 5> patch{};
    // Zero retains the legacy all-C_player guard used by enemy invisibility.
    // A nonzero value makes the same hook strict to one controlled actor.
    std::uintptr_t protected_player = 0;
    bool local_squad_scope = false;
    bool applied = false;
};

PlayerDamagePatchState g_player_damage{};
// Ultimate Mod can give individual squad members a derived C_player class
// with a different virtual table.  One hook from the currently controlled
// player therefore cannot cover the whole squad.  Squad scope owns one guard
// per distinct virtual entry; the single global guard above is intentionally
// kept for the existing controlled-player and enemy-invisibility paths.
std::vector<PlayerDamagePatchState> g_squad_player_damage{};

template <std::size_t PatchSize>
struct PlayerDeathPatchStateT
{
    DWORD process_id = 0;
    std::uintptr_t function = 0;
    std::uintptr_t protected_player = 0;
    bool local_squad_scope = false;
    std::uintptr_t remote = 0;
    std::size_t remote_size = 0;
    std::array<std::uint8_t, PatchSize> original{};
    std::array<std::uint8_t, PatchSize> patch{};
    bool applied = false;
};

using PlayerDeathPatchState = PlayerDeathPatchStateT<kHumanDieHookPatchSize>;
using PlayerExplosionPatchState =
    PlayerDeathPatchStateT<kPlayerExplodeHookPatchSize>;
PlayerDeathPatchState g_player_death{};
PlayerDeathPatchState g_player_base_death{};
PlayerExplosionPatchState g_player_explosion{};
std::vector<PlayerDeathPatchState> g_squad_player_death{};
std::vector<PlayerExplosionPatchState> g_squad_player_explosion{};

struct NativeNoHitState
{
    DWORD process_id = 0;
    std::uintptr_t actor = 0;
    std::uint8_t original = 0;
};

// We retain the original per-instance value so disabling total protection
// never turns off an immortality cheat that the player had already enabled.
std::vector<NativeNoHitState> g_native_no_hit_players{};

// V104 : soldats vises par le prochain clic sur la carte native. Vide = la
// carte teleporte normalement, comportement d'origine integralement conserve.
std::vector<std::uintptr_t> g_soldier_order_targets{};

void ClearSoldierOrder()
{
    g_soldier_order_targets.clear();
}

bool IssueMoveOrderOnGameThread(
    TrainerProcess& process,
    const std::vector<std::uintptr_t>& soldiers,
    const Vector3& destination);

// Sante Max : une entree par soldat touche, pour pouvoir rendre exactement la
// vie d'origine au decochage.
struct ExtendedHealthState
{
    DWORD process_id = 0;
    std::uintptr_t actor = 0;
    std::int32_t resistance = 0;
    std::int32_t init_resistance = 0;
};

std::vector<ExtendedHealthState> g_extended_health{};

// V95, 3 septembre 2026 17:22 - reprise instantanee des cases.
// Chaque retrait de crochet attendait la liberation de sa page distante :
// jusqu'a 250 sondages espaces de 2 ms, et chaque sondage suspend TOUS les
// threads de hde.exe. Decocher une case pouvait donc bloquer l'interface plus
// d'une seconde. Or ce que le joueur attend, c'est la reecriture des octets
// d'origine, qui est deja immediate; seule la restitution de la page exige que
// le jeu ait quitte le trampoline. La page part donc dans cette file et est
// rendue plus tard, avec une seule sonde non bloquante par image.
struct PendingRemoteRelease
{
    DWORD process_id = 0;
    std::uintptr_t address = 0;
    std::size_t size = 0;
};

std::vector<PendingRemoteRelease> g_pending_remote_releases{};

void QueueRemotePageRelease(
    TrainerProcess& process,
    std::uintptr_t address,
    std::size_t size)
{
    if (address == 0 || size == 0)
        return;
    bool executing = true;
    if (process.IsConnected() &&
        process.IsAnyThreadExecutingRange(address, size, executing) &&
        !executing && process.FreeRemoteMemory(address))
    {
        return;
    }
    g_pending_remote_releases.push_back(
        PendingRemoteRelease{process.ProcessId(), address, size});
}

void ProcessPendingRemoteReleases(TrainerProcess& process)
{
    if (g_pending_remote_releases.empty())
        return;
    auto entry = g_pending_remote_releases.begin();
    while (entry != g_pending_remote_releases.end())
    {
        if (!process.IsConnected() || process.ProcessId() != entry->process_id)
        {
            // Le processus du jeu a change ou disparu : la page est partie
            // avec lui, il n'y a plus rien a rendre.
            entry = g_pending_remote_releases.erase(entry);
            continue;
        }
        bool executing = true;
        if (process.IsAnyThreadExecutingRange(
                entry->address, entry->size, executing) &&
            !executing && process.FreeRemoteMemory(entry->address))
        {
            entry = g_pending_remote_releases.erase(entry);
            continue;
        }
        ++entry;
    }
}

struct ProtectedFallHookState
{
    DWORD process_id = 0;
    std::uintptr_t function = 0;
    std::uintptr_t protected_player = 0;
    bool local_squad_scope = false;
    std::uintptr_t remote = 0;
    std::size_t remote_size = 0;
    std::array<std::uint8_t, kLifeUnlimitedTickHookPatchSize> original{};
    std::array<std::uint8_t, kLifeUnlimitedTickHookPatchSize> patch{};
    bool applied = false;
};

// Unlike V72 this state never writes fall_speed. It merely prevents the
// collision branch from treating the selected actor as a fatal faller.
ProtectedFallHookState g_protected_fall{};

// H&D can replace player_object_address on the very frame a controlled
// soldier dies. Keep the old address long enough to observe its SM_DEAD state;
// F10 must revive that soldier, not the living ally selected after the switch.
struct ReviveTargetHistory
{
    DWORD process_id = 0;
    std::uintptr_t last_controlled_player = 0;
    std::uintptr_t last_dead_controlled_player = 0;
};

ReviveTargetHistory g_revive_target_history{};

struct PlayerRevivePatchState
{
    DWORD process_id = 0;
    std::uintptr_t function = 0;
    std::uintptr_t target = 0;
    std::uintptr_t source = 0;
    std::uintptr_t remote = 0;
    std::size_t remote_size = 0;
    ULONGLONG queued_at = 0;
    std::array<std::uint8_t, kNoclipHookPatchSize> original{};
    std::array<std::uint8_t, kNoclipHookPatchSize> patch{};
    bool applied = false;
};

PlayerRevivePatchState g_player_revive{};

struct AimbotPatchState
{
    DWORD process_id = 0;
    std::uintptr_t actor = 0;
    std::uintptr_t remote = 0;
    std::uintptr_t hook = 0;
    std::array<std::uint8_t, 9> original{};
    std::array<std::uint8_t, 9> patch{};
    Vector3 last_commanded_delta{};
    std::uint32_t last_observed_sequence = 0;
    ULONGLONG last_switch_tick = 0;
    bool command_valid = false;
    bool applied = false;
};

AimbotPatchState g_aimbot{};

// Remote page shared by the three Bullet Track trampolines: projectile
// redirection, native recipient resolution and the transient damage callback
// frame. The shared publication block lives at the end of the page.
constexpr std::size_t kBulletTrackRemoteSize = 0x800;
constexpr std::size_t kBulletTrackPlayerPublicationOffset = 0x700;
constexpr ULONGLONG kBulletTrackDrainTimeMs = 2000;
// A rapid burst can create several projectiles between two radar refreshes.
// Keep a small, crosshair-ranked reserve so a projectile never keeps aiming at
// an actor that an earlier projectile in the same burst has just killed.
constexpr std::size_t kBulletTrackCandidateCount = 4;
// Bullet Track has always redirected the native projectile to the enemy head,
// but it deliberately kept each weapon's ordinary damage.  Armoured enemy
// types can therefore survive many confirmed head impacts.  This value stays
// far below signed overflow even after the engine's head multiplier and makes
// each successfully redirected projectile decisive.
constexpr std::int32_t kBulletTrackLethalPower = 20'000;

// counters[0] is the forced damage count, counters[1..12] are the per-filter
// rejection counters emitted by the fallback stub, in stub order.
constexpr std::size_t kBulletTrackCounterCount = 13;

struct BulletTrackHookState
{
    DWORD process_id = 0;
    std::uintptr_t remote = 0;
    std::uintptr_t hook = 0;
    std::uintptr_t fallback_hook = 0;
    std::array<std::uint8_t, 8> original{};
    std::array<std::uint8_t, 8> patch{};
    std::array<std::uint8_t, 5> fallback_original{};
    std::array<std::uint8_t, 5> fallback_patch{};
    std::uintptr_t finish_hook = 0;
    std::array<std::uint8_t, 6> finish_original{};
    std::array<std::uint8_t, 6> finish_patch{};
    std::array<std::uint32_t, kBulletTrackCounterCount> last_counters{};
    std::uint32_t last_retarget_count = 0;
    ULONGLONG last_damage_log_tick = 0;
    ULONGLONG restore_after_tick = 0;
    bool applied = false;
};

BulletTrackHookState g_bullet_track{};

struct VehicleSpeedPatchState
{
    DWORD process_id = 0;
    std::uintptr_t remote = 0;
    std::uintptr_t target_hook = 0;
    std::uintptr_t acceleration_hook = 0;
    std::uintptr_t vehicle = 0;
    std::array<std::uint8_t, 7> target_original{};
    std::array<std::uint8_t, 7> target_patch{};
    std::array<std::uint8_t, 6> acceleration_original{};
    std::array<std::uint8_t, 6> acceleration_patch{};
    bool applied = false;
};

VehicleSpeedPatchState g_vehicle_speed{};

struct GameSpeedPatchState
{
    DWORD process_id = 0;
    std::uintptr_t remote = 0;
    std::uintptr_t hook = 0;
    std::array<std::uint8_t, 7> original{};
    std::array<std::uint8_t, 7> patch{};
    // Multiplicateur en virgule fixe 8.8, applique par un imul entier pour ne
    // jamais toucher a la pile x87 du moteur.
    std::int32_t factor = 256;
    bool applied = false;
};

GameSpeedPatchState g_game_speed{};

bool IsSanePointer(std::uintptr_t address);

struct WheelTurnHookState
{
    DWORD process_id = 0;
    std::uintptr_t hook = 0;
    std::uintptr_t remote = 0;
    std::array<std::uint8_t, kAutomobileWheelTurnPatchSize> original{};
    std::array<std::uint8_t, kAutomobileWheelTurnPatchSize> patch{};
    bool applied = false;
};

WheelTurnHookState g_wheel_turn{};

// Last multiplier actually applied to a given car, so that lowering it with B
// can take the current speed down with it.
struct VehicleThrottleState
{
    DWORD process_id = 0;
    std::uintptr_t vehicle = 0;
    float multiplier = 0.0f;
};

VehicleThrottleState g_vehicle_throttle{};

void RemoveWheelTurnHook(TrainerProcess& process)
{
    if (!g_wheel_turn.applied || !process.IsConnected() ||
        process.ProcessId() != g_wheel_turn.process_id)
    {
        g_wheel_turn = {};
        return;
    }
    const std::uint32_t none = 0;
    (void)process.WriteMemory(
        g_wheel_turn.remote + kWheelTurnVehicleOffset, none);
    std::array<std::uint8_t, kAutomobileWheelTurnPatchSize> current{};
    const bool restored =
        process.ReadMemory(
            g_wheel_turn.hook, current.data(), current.size()) &&
        (current == g_wheel_turn.original ||
         (current == g_wheel_turn.patch && process.WriteProtectedMemory(
             g_wheel_turn.hook,
             g_wheel_turn.original.data(),
             g_wheel_turn.original.size())));
    bool executing = true;
    if (restored)
    {
        for (unsigned attempt = 0; attempt < 50; ++attempt)
        {
            if (process.IsAnyThreadExecutingRange(
                    g_wheel_turn.remote, kWheelTurnRemoteSize, executing) &&
                !executing)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    LogDiagnostic(
        "Vehicle: steering hook %s, page %s.",
        restored ? "restored" : "restoration unverified",
        (restored && !executing) ? "freed" : "kept");
    if (restored && !executing)
        (void)process.FreeRemoteMemory(g_wheel_turn.remote);
    g_wheel_turn = {};
}

// Only the published car takes the scaled rate; every other vehicle keeps the
// engine's own 0.6f, so AI traffic still steers exactly as before.
bool UpdateWheelTurnHook(
    TrainerProcess& process, std::uintptr_t vehicle, float boost)
{
    const float rate = kStockWheelTurnRate * (std::clamp)(
        boost, kMinimumSteeringBoost, kMaximumSteeringBoost);
    if (g_wheel_turn.applied &&
        g_wheel_turn.process_id != process.ProcessId())
    {
        RemoveWheelTurnHook(process);
    }
    if (g_wheel_turn.applied)
    {
        return process.WriteMemory(
                   g_wheel_turn.remote + kWheelTurnVehicleOffset,
                   static_cast<std::uint32_t>(vehicle)) &&
               process.WriteMemory(
                   g_wheel_turn.remote + kWheelTurnRateOffset, rate);
    }

    RemoteModuleInfo module{};
    constexpr std::array<std::uint8_t, kAutomobileWheelTurnPatchSize> expected{
        0xD8, 0x0D, 0xC0, 0x66, 0x4F, 0x00};
    if (!process.GetMainModuleInfo(module) ||
        module.image_size <= kAutomobileWheelTurnRateRva + expected.size())
    {
        return false;
    }
    WheelTurnHookState next{};
    next.process_id = process.ProcessId();
    next.hook = module.base_address + kAutomobileWheelTurnRateRva;
    if (!process.ReadMemory(
            next.hook, next.original.data(), next.original.size()) ||
        next.original != expected)
    {
        LogDiagnostic(
            "Vehicle: steering rate signature mismatch at %08X.",
            static_cast<unsigned>(next.hook));
        return false;
    }
    next.remote = process.AllocateRemoteMemory(kWheelTurnRemoteSize);
    if (!IsSanePointer(next.remote))
        return false;

    std::vector<std::uint8_t> code;
    const auto byte = [&](std::uint8_t value) { code.push_back(value); };
    const auto dword = [&](std::uint32_t value)
    {
        const auto* raw = reinterpret_cast<const std::uint8_t*>(&value);
        code.insert(code.end(), raw, raw + sizeof(value));
    };
    const auto return_jump = [&]()
    {
        byte(0xE9);
        const std::int32_t relative = static_cast<std::int32_t>(
            (next.hook + kAutomobileWheelTurnPatchSize) -
            (next.remote + code.size() + sizeof(std::int32_t)));
        dword(static_cast<std::uint32_t>(relative));
    };

    // ESI is the automobile at this point; the value being scaled is already
    // on the x87 stack, so only the multiplier operand changes.
    byte(0x9C);                                  // pushfd
    byte(0x3B); byte(0x35);                      // cmp esi,[published]
    dword(static_cast<std::uint32_t>(next.remote + kWheelTurnVehicleOffset));
    byte(0x75);                                  // jne stock
    const std::size_t stock_displacement = code.size();
    byte(0);
    byte(0x9D);                                  // popfd
    byte(0xD8); byte(0x0D);                      // fmul [scaled rate]
    dword(static_cast<std::uint32_t>(next.remote + kWheelTurnRateOffset));
    return_jump();
    code[stock_displacement] = static_cast<std::uint8_t>(
        code.size() - (stock_displacement + 1));
    byte(0x9D);                                  // popfd  (stock)
    byte(0xD8); byte(0x0D);                      // fmul [004F66C0] = 0.6f
    dword(static_cast<std::uint32_t>(kStockWheelTurnRateAddress));
    return_jump();

    if (code.size() > kWheelTurnVehicleOffset ||
        !process.WriteMemory(next.remote, code.data(), code.size()) ||
        !process.WriteMemory(
            next.remote + kWheelTurnVehicleOffset,
            static_cast<std::uint32_t>(vehicle)) ||
        !process.WriteMemory(next.remote + kWheelTurnRateOffset, rate))
    {
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }

    next.patch.fill(0x90);
    next.patch[0] = 0xE9;
    const std::int32_t hook_relative = static_cast<std::int32_t>(
        next.remote - (next.hook + 5));
    std::memcpy(next.patch.data() + 1, &hook_relative, sizeof(hook_relative));
    if (!process.WriteProtectedMemory(
            next.hook, next.patch.data(), next.patch.size()))
    {
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }
    std::array<std::uint8_t, kAutomobileWheelTurnPatchSize> verified{};
    if (!process.ReadMemory(
            next.hook, verified.data(), verified.size()) ||
        verified != next.patch)
    {
        (void)process.WriteProtectedMemory(
            next.hook, next.original.data(), next.original.size());
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }
    next.applied = true;
    g_wheel_turn = next;
    LogDiagnostic(
        "Vehicle: steering hook installed at %08X remote=%08X rate=%.2f "
        "(stock 0.60).",
        static_cast<unsigned>(next.hook),
        static_cast<unsigned>(next.remote), rate);
    return true;
}

struct NoclipState
{
    DWORD process_id = 0;
    std::uintptr_t actor = 0;
    std::uintptr_t frame = 0;
    // Non-zero while the player is driving: the vehicle actor whose frame is
    // being moved. The physics fields are still written on `actor`, never on
    // this one -- they are C_human offsets and a vehicle has another layout.
    std::uintptr_t vehicle = 0;
    Vector3 position{};
    float initial_fall_speed = 0.0f;
    std::int32_t initial_collision_countdown = 0;
    std::uint8_t initial_falling = 0;
    ULONGLONG last_log_tick = 0;
    bool active = false;
};

NoclipState g_noclip{};

struct VehicleNoclipHookState
{
    DWORD process_id = 0;
    std::uintptr_t hook = 0;
    std::uintptr_t remote = 0;
    std::array<std::uint8_t, kVehicleNoclipHookPatchSize> original{};
    std::array<std::uint8_t, kVehicleNoclipHookPatchSize> patch{};
    bool applied = false;
};

VehicleNoclipHookState g_vehicle_noclip_hook{};

struct NoclipHookState
{
    DWORD process_id = 0;
    std::uintptr_t hook = 0;
    std::uintptr_t remote = 0;
    std::array<std::uint8_t, kNoclipHookPatchSize> original{};
    std::array<std::uint8_t, kNoclipHookPatchSize> patch{};
    bool applied = false;
};

NoclipHookState g_noclip_hook{};

struct NoclipNetworkPublishHookState
{
    DWORD process_id = 0;
    std::uintptr_t hook = 0;
    std::uintptr_t remote = 0;
    std::array<std::uint8_t, kNoclipNetworkPublishHookPatchSize> original{};
    std::array<std::uint8_t, kNoclipNetworkPublishHookPatchSize> patch{};
    bool applied = false;
};

NoclipNetworkPublishHookState g_noclip_network_publish_hook{};

struct NetworkPositionMaskState
{
    struct Entry
    {
        std::uintptr_t actor = 0;
        Vector3 anchor{};
    };

    DWORD process_id = 0;
    std::array<Entry, kNetworkPositionMaskMaximumActors> entries{};
    std::size_t count = 0;
    NetworkPositionMaskScope scope = NetworkPositionMaskScope::ControlledPlayer;
    bool active = false;
    bool real_position_published = false;
    bool remote_visual_hidden = false;
    // Network actor ids are stable across the two processes; raw actor
    // addresses are not.  The Client uses this only to affect the selected
    // host soldier, never the rest of the host squad.
    std::uint16_t controlled_network_id = 0;
};

NetworkPositionMaskState g_network_position_mask{};

// V96 : vrai seulement pendant que W tient le modele cache chez l'autre PC.
// La case « Masquer ma position reseau » demande alors la meme invisibilite
// que la case dediee, mais pour le seul soldat pilote, et uniquement pendant
// ce temps. Elle ne lit ni n'ecrit jamais les reglages de la case « Invisible
// pour les ennemis » : c'est une demande interne parallele.
bool NetworkPositionMaskHidesLocalPlayer(TrainerProcess& process)
{
    return g_network_position_mask.active &&
        g_network_position_mask.remote_visual_hidden &&
        g_network_position_mask.process_id == process.ProcessId();
}

struct NetworkPositionMaskHookState
{
    DWORD process_id = 0;
    std::uintptr_t hook = 0;
    std::uintptr_t remote = 0;
    std::array<std::uint8_t, kNetworkPositionMaskHookPatchSize> original{};
    std::array<std::uint8_t, kNetworkPositionMaskHookPatchSize> patch{};
    bool applied = false;
};

NetworkPositionMaskHookState g_network_position_mask_hook{};

constexpr std::size_t kPeerHealthSlots = 8;

#pragma pack(push, 1)
struct PeerHealthEntry
{
    std::uint16_t network_id = 0;
    std::int32_t resistance = 0;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct PeerVisualCommandWire
{
    std::uint32_t magic = kPeerVisualCommandMagic;
    std::uint16_t version = kPeerVisualCommandVersion;
    std::uint8_t hide_host_models = 0;
    // The companion Client ignores a death transition only for player actors
    // owned by this host. It never protects its local player or any enemy.
    std::uint8_t suppress_host_player_death = 0;
    std::uint8_t suppress_host_player_damage = 0;
    // V98 : ordres portant sur les soldats du CLIENT lui-meme.
    std::uint8_t extend_client_health = 0;
    std::uint8_t mask_client_position = 0;
    std::uint8_t reserved = 0;
    std::uint16_t visual_target_network_id = 0;
    std::uint16_t life_target_network_id = 0;
    std::uint32_t revive_sequence = 0;
    // V102 : table de vie publiee par l'hote. Elle rend la valeur affichee
    // identique sur toutes les machines, ce que la seule extension ne
    // garantissait pas : chaque PC soustrait la resistance sur SA copie, donc
    // les deux compteurs derivent des qu'une source de degats est locale a une
    // machine (chute, feu, explosion declenchee sur place). L'hote publie sa
    // valeur, chaque compagnon la recopie dans sa propre copie de l'acteur.
    std::uint8_t health_count = 0;
    std::uint8_t health_reserved = 0;
    PeerHealthEntry health[kPeerHealthSlots]{};
};
#pragma pack(pop)

static_assert(sizeof(PeerVisualCommandWire) == 70,
    "The LAN peer-visual command must stay a fixed small packet.");

struct PeerVisualCommandSender
{
    SOCKET socket = INVALID_SOCKET;
    bool winsock_ready = false;
    bool last_hidden = false;
    bool last_death_suppressed = false;
    bool last_damage_suppressed = false;
    bool last_client_health = false;
    bool last_client_position = false;
    std::uint16_t last_visual_target_network_id = 0;
    std::uint16_t last_life_target_network_id = 0;
    std::uint32_t last_revive_sequence = 0;
    DWORD last_send_tick = 0;
};

PeerVisualCommandSender g_peer_visual_sender{};
bool g_peer_remote_life_mirror_enabled = false;
// V98 : ce que l'hote demande aux CLIENTS d'appliquer a LEURS propres
// soldats. Ni l'un ni l'autre ne peut etre fait depuis cette machine : la vie
// et la position publiee d'un soldat appartiennent a son proprietaire.
bool g_peer_extend_client_health = false;
// Instantane de la vie de chaque copie d'acteur joueur, relu par l'hote a
// chaque image et publie tel quel.
std::array<PeerHealthEntry, kPeerHealthSlots> g_peer_health_table{};
std::uint8_t g_peer_health_count = 0;
bool g_peer_mask_client_position = false;
bool g_peer_remote_damage_mirror_enabled = false;
bool g_peer_client_revive_enabled = false;
std::uint16_t g_peer_life_target_network_id = 0;
std::uint32_t g_peer_revive_sequence = 0;

struct NetworkPositionMaskWireEntry
{
    std::uint32_t actor = 0;
    Vector3 anchor{};
};

static_assert(
    sizeof(NetworkPositionMaskWireEntry) == 16,
    "The injected scan expects actor + three float coordinates.");

struct PendingTeleportState
{
    DWORD process_id = 0;
    ULONGLONG started_at = 0;
    unsigned closed_map_samples = 0;
    bool wait_logged = false;
};

PendingTeleportState g_pending_teleport{};

struct TeleportVerifyState
{
    DWORD process_id = 0;
    std::uintptr_t actor = 0;
    std::uintptr_t frame = 0;
    std::uintptr_t vehicle = 0;
    std::uintptr_t scene = 0;
    Vector3 destination{};
    ULONGLONG started_at = 0;
    unsigned samples = 0;
    bool snapped_back = false;
};

TeleportVerifyState g_teleport_verify{};

struct RemoteVector32
{
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
    std::uint32_t capacity = 0;
};

bool IsSanePointer(std::uintptr_t address)
{
    return address >= 0x10000U && address <= 0x7FFF'FFFFU;
}

bool IsSaneMoveDirection(const Vector3& direction)
{
    return std::isfinite(direction.x) && std::isfinite(direction.y) &&
        std::isfinite(direction.z) && std::fabs(direction.x) <= 100.0f &&
        std::fabs(direction.y) <= 100.0f && std::fabs(direction.z) <= 100.0f;
}

bool IsSaneWorldPosition(const Vector3& position)
{
    constexpr float kMaximumCoordinate = 10'000'000.0f;
    return std::isfinite(position.x) && std::isfinite(position.y) &&
        std::isfinite(position.z) &&
        std::fabs(position.x) <= kMaximumCoordinate &&
        std::fabs(position.y) <= kMaximumCoordinate &&
        std::fabs(position.z) <= kMaximumCoordinate;
}

bool SameHorizontalDirection(const Vector3& left, const Vector3& right)
{
    constexpr float kTolerance = 0.0001f;
    return std::fabs(left.x - right.x) <= kTolerance &&
        std::fabs(left.z - right.z) <= kTolerance;
}

bool SendNativeMapKey(TrainerProcess& process)
{
    // SendInput delivers the key to the foreground window, so bring the game
    // window forward first, mirroring SendInventoryMainThreadTrigger. This
    // also gives the game a short delay to regain its input context before
    // the Space toggle arrives.
    const HWND game_window = process.WindowHandle();
    if (!process.IsConnected() || !IsWindow(game_window))
    {
        LogDiagnostic("Teleport: game window unavailable for Space send.");
        return false;
    }
    AllowSetForegroundWindow(process.ProcessId());
    if (IsIconic(game_window))
        ShowWindow(game_window, SW_RESTORE);
    SetForegroundWindow(game_window);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    if (!process.IsGameWindowActive())
    {
        LogDiagnostic(
            "Teleport: game window not active after focus request.");
        return false;
    }
    const UINT scan = MapVirtualKeyW(VK_SPACE, MAPVK_VK_TO_VSC);
    if (scan == 0)
    {
        LogDiagnostic("Teleport: Space scan code unavailable.");
        return false;
    }
    // The game polls keyboard_map[] (filled by its WH_KEYBOARD hook) once
    // per tick. A down+up pair delivered back-to-back can fall entirely
    // between two samples, so hold the key down for ~150 ms to guarantee at
    // least one tick observes the pressed state.
    INPUT down{};
    down.type = INPUT_KEYBOARD;
    down.ki.wScan = static_cast<WORD>(scan);
    down.ki.dwFlags = KEYEVENTF_SCANCODE;
    INPUT up = down;
    up.ki.dwFlags |= KEYEVENTF_KEYUP;
    const bool down_sent = SendInput(1, &down, sizeof(INPUT)) == 1;
    if (down_sent)
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const bool up_sent = SendInput(1, &up, sizeof(INPUT)) == 1;
    if (!down_sent || !up_sent)
        LogDiagnostic("Teleport: SendInput(Space) failed.");
    return down_sent && up_sent;
}

bool ReadNativeMapOpen(
    const TrainerProcess& process,
    const RadarSnapshot* snapshot,
    bool& open)
{
    open = false;
    if (!process.IsConnected() || !snapshot ||
        !IsSanePointer(snapshot->entity_list_object_address))
    {
        return false;
    }

    std::uint8_t active = 0;
    if (!process.ReadMemory(
            snapshot->entity_list_object_address +
                kMissionMapActiveOffset,
            active) || active > 1)
    {
        return false;
    }
    if (active == 0)
        return true;

    std::uintptr_t manager = 0;
    if (!process.ReadMemory(
            snapshot->entity_list_object_address +
                kMissionMapManagerOffset,
            manager) || !IsSanePointer(manager))
    {
        return false;
    }
    open = true;
    return true;
}


// =====================================================================
// V149 - LE SITE DE TRICHE EST TRAVERSE A CHAQUE IMAGE
// =====================================================================
//
// Le temoin pose en V148 a tranche, et il dit l'inverse de mes deux dernieres
// suppositions :
//
//   TEMOIN: le code pose n'a pas rendu la main. Dernier point vu : homme n1
//   sur 1 (adresse 0247BA80), etape 0 = aucune - le jeu n'est jamais entre
//   dans le code. Temoin lu : non - jamais rien lu.
//
// Ce n'est donc ni `AddItem`, ni `Reload`, ni la sortie d'arme : le code pose
// n'a JAMAIS TOURNE. Le jeu meurt AVANT, pendant qu'on installe le saut.
//
// La raison est connue depuis la V107, elle etait sous nos yeux : le site de
// triche est traverse A CHAQUE IMAGE - c'est pour cela qu'il a fallu un garde
// de re-entree. Y ecrire cinq octets pendant que le processeur les execute est
// une course. On la gagne presque toujours; de temps en temps le jeu execute
// une instruction a moitie ecrite et s'arrete.
//
// Cela explique enfin tout ce qui ne collait pas :
//
//   - le premier passage reussit, le second tue : c'est le hasard, et chaque
//     passage supplementaire est une chance de plus de perdre;
//   - le miroir ET la fenetre J dans la meme image, en V145 : deux
//     installations coup sur coup, donc deux chances de perdre;
//   - et les plantages anciens sur les soldats crees, qui n'avaient jamais
//     recu d'explication satisfaisante.
//
// La correction : on suspend tous les threads du jeu, on verifie qu'aucun
// n'est dans les octets vises, on ecrit, on relance. Et on reessaie quelques
// fois si l'un d'eux s'y trouve.
bool InstallHookSafely(
    TrainerProcess& process,
    std::uintptr_t hook,
    const void* bytes,
    std::size_t size,
    const char* what)
{
    for (int attempt = 0; attempt < 24; ++attempt)
    {
        if (process.PatchCodeSafely(hook, bytes, size))
            return true;
        // Un thread du jeu se trouve dans les octets vises. On lui laisse le
        // temps d'en sortir - une image dure une quinzaine de millisecondes.
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    LogDiagnostic(
        "Site de triche: impossible d'ecrire %s sans risque apres 24 essais "
        "(un thread du jeu y est reste). Operation abandonnee, le jeu n'est "
        "pas touche.",
        what);
    return false;
}


// La remise en etat, elle, ne peut pas etre abandonnee.
//
// Si le saut restait en place alors que la page va etre rendue, le jeu
// sauterait dans le vide a l'image suivante - un arret certain, la ou la
// course n'en donne qu'un risque. On insiste donc bien plus longtemps, et en
// tout dernier recours on ecrit malgre tout, en le disant.
bool RestoreHookSafely(
    TrainerProcess& process,
    std::uintptr_t hook,
    const void* original,
    std::size_t size)
{
    for (int attempt = 0; attempt < 250; ++attempt)
    {
        if (process.PatchCodeSafely(hook, original, size))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    LogDiagnostic(
        "Site de triche: la remise en etat n'a pas pu se faire threads "
        "suspendus apres une seconde. On ecrit quand meme - laisser le saut "
        "en place serait pire.");
    return process.WriteProtectedMemory(hook, original, size);
}

bool SendInventoryMainThreadTrigger(TrainerProcess& process)
{
    const HWND game_window = process.WindowHandle();
    if (!process.IsConnected() || !IsWindow(game_window))
    {
        LogDiagnostic("Fullhands: trigger window unavailable.");
        return false;
    }

    AllowSetForegroundWindow(process.ProcessId());
    if (IsIconic(game_window))
        ShowWindow(game_window, SW_RESTORE);
    SetForegroundWindow(game_window);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    if (!process.IsGameWindowActive())
    {
        LogDiagnostic(
            "Fullhands: game window not active after focus request.");
        return false;
    }

    const DWORD thread_id = GetWindowThreadProcessId(game_window, nullptr);
    const HKL layout = GetKeyboardLayout(thread_id);
    const UINT mapped = MapVirtualKeyExW('X', MAPVK_VK_TO_VSC_EX, layout);
    if (mapped == 0)
    {
        LogDiagnostic("Fullhands: trigger scan code unavailable.");
        return false;
    }

    std::array<INPUT, 2> inputs{};
    for (INPUT& input : inputs)
    {
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = LOBYTE(mapped);
        input.ki.dwFlags = KEYEVENTF_SCANCODE;
        const BYTE prefix = HIBYTE(mapped);
        if (prefix == 0xE0 || prefix == 0xE1)
            input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
    inputs[1].ki.dwFlags |= KEYEVENTF_KEYUP;
    const bool sent = SendInput(
        static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT)) ==
        inputs.size();
    if (!sent)
        LogDiagnostic("Fullhands: trigger SendInput failed.");
    return sent;
}

bool ActorFrameStillMatches(
    TrainerProcess& process, std::uintptr_t actor)
{
    std::uintptr_t frame = 0;
    std::uintptr_t frame_actor = 0;
    return process.IsConnected() && IsSanePointer(actor) &&
        process.ReadMemory(actor + kActorFrameOffset, frame) &&
        IsSanePointer(frame) &&
        process.ReadMemory(
            frame + kFrameActorBackReferenceOffset, frame_actor) &&
        frame_actor == actor;
}

void ClearPlayerSpeedPatch()
{
    g_player_speed = {};
}

void RestorePlayerSpeed(TrainerProcess& process)
{
    if (!g_player_speed.applied)
    {
        ClearPlayerSpeedPatch();
        return;
    }

    if (process.ProcessId() == g_player_speed.process_id &&
        ActorFrameStillMatches(process, g_player_speed.actor))
    {
        std::int32_t current_pose = -1;
        Vector3 current{};
        if (process.ReadMemory(
                g_player_speed.actor + kActorCurrentPoseOffset,
                current_pose) &&
            current_pose == g_player_speed.pose &&
            process.ReadMemory(
                g_player_speed.actor + kActorMoveDirectionOffset,
                current) &&
            IsSaneMoveDirection(current) &&
            SameHorizontalDirection(current, g_player_speed.last_written))
        {
            (void)process.WriteMemory(
                g_player_speed.actor + kActorMoveDirectionOffset,
                g_player_speed.original.x);
            (void)process.WriteMemory(
                g_player_speed.actor + kActorMoveDirectionOffset +
                    sizeof(float) * 2U,
                g_player_speed.original.z);
        }
    }

    ClearPlayerSpeedPatch();
}

void UpdateSpeedMultipliers(
    GameplaySettings& settings,
    const GameplayInput& input,
    bool vehicle_context)
{
    const ULONGLONG now = GetTickCount64();
    if (g_last_speed_input_tick == 0 || now < g_last_speed_input_tick)
        g_last_speed_input_tick = now;

    const ULONGLONG elapsed_ms =
        (std::min)(now - g_last_speed_input_tick, 100ULL);
    g_last_speed_input_tick = now;

    if (!settings.player_speed_enabled)
        settings.player_speed_multiplier = kMinimumSpeedMultiplier;
    if (!settings.vehicle_speed_enabled)
        settings.vehicle_speed_multiplier = kMinimumSpeedMultiplier;

    const int direction = static_cast<int>(input.speed_up_down) -
        static_cast<int>(input.speed_down_down);
    if (settings.noclip_active)
    {
        settings.noclip_speed_mps += static_cast<float>(direction) *
            (kNoclipSpeedPerSecond +
                settings.noclip_speed_mps * kNoclipSpeedGrowthPerSecond) *
            static_cast<float>(elapsed_ms) * 0.001f;
        settings.noclip_speed_mps = (std::clamp)(
            settings.noclip_speed_mps,
            kMinimumNoclipSpeed,
            kMaximumNoclipSpeed);
    }
    float* multiplier = nullptr;
    if (!settings.noclip_active && !vehicle_context &&
        settings.player_speed_enabled)
        multiplier = &settings.player_speed_multiplier;
    if (multiplier)
        *multiplier += static_cast<float>(direction) *
            kSpeedMultiplierPerSecond *
            static_cast<float>(elapsed_ms) * 0.001f;

    // Vehicle: one press of physical N raises the persistent multiplier by
    // one step, one press of physical B lowers it. The floor is 1.0x, which
    // is exactly the game's default vehicle speed.
    if (vehicle_context && settings.vehicle_speed_enabled)
    {
        constexpr float kVehicleMultiplierStep = 10.0f;
        const float previous = settings.vehicle_speed_multiplier;
        if (input.vehicle_speed_up_pressed)
            settings.vehicle_speed_multiplier += kVehicleMultiplierStep;
        if (input.vehicle_speed_down_pressed)
            settings.vehicle_speed_multiplier -= kVehicleMultiplierStep;
        settings.vehicle_speed_multiplier = (std::clamp)(
            settings.vehicle_speed_multiplier,
            kMinimumSpeedMultiplier,
            kMaximumSpeedMultiplier);
        if (settings.vehicle_speed_multiplier != previous)
        {
            LogDiagnostic(
                "Vehicle: multiplier %.1fx -> %.1fx (N/B pressed).",
                previous, settings.vehicle_speed_multiplier);
        }

        // Steering sensitivity rides with the same checkbox: I raises it, U
        // lowers it, both while driving so the setting can be found by feel
        // rather than by switching to the trainer.
        constexpr float kSteeringBoostStep = 0.5f;
        const float previous_boost = settings.vehicle_steering_boost;
        if (input.steering_boost_up_pressed)
            settings.vehicle_steering_boost += kSteeringBoostStep;
        if (input.steering_boost_down_pressed)
            settings.vehicle_steering_boost -= kSteeringBoostStep;
        settings.vehicle_steering_boost = (std::clamp)(
            settings.vehicle_steering_boost,
            kMinimumSteeringBoost,
            kMaximumSteeringBoost);
        if (settings.vehicle_steering_boost != previous_boost)
        {
            LogDiagnostic(
                "Vehicle: steering sensitivity %.1fx -> %.1fx (U/I pressed).",
                previous_boost, settings.vehicle_steering_boost);
        }
    }

    // Vitesse du jeu : une pression sur 9 monte d'un demi-cran, une pression
    // sur 8 redescend. Le plancher 1.0x est la vitesse normale du jeu, donc
    // relacher la case ne laisse jamais le moteur dans un etat accelere.
    if (settings.game_speed_enabled)
    {
        const float previous_game_speed = settings.game_speed_multiplier;
        if (input.game_speed_up_pressed)
            settings.game_speed_multiplier +=
                GameSpeedStep(settings.game_speed_multiplier);
        // En descente, le pas est celui de la tranche juste en dessous, pour
        // que 9 puis 8 ramenent exactement a la valeur precedente.
        if (input.game_speed_down_pressed)
            settings.game_speed_multiplier -=
                GameSpeedStep(settings.game_speed_multiplier - 0.001f);
        settings.game_speed_multiplier = (std::clamp)(
            settings.game_speed_multiplier,
            kMinimumGameSpeedMultiplier,
            kMaximumGameSpeedMultiplier);
        if (settings.game_speed_multiplier != previous_game_speed)
        {
            LogDiagnostic(
                "Game speed: multiplier %.1fx -> %.1fx (9/8 pressed).",
                previous_game_speed, settings.game_speed_multiplier);
        }
    }
    else
    {
        settings.game_speed_multiplier = kMinimumGameSpeedMultiplier;
    }

    settings.player_speed_multiplier = (std::clamp)(
        settings.player_speed_multiplier,
        kMinimumSpeedMultiplier,
        kMaximumSpeedMultiplier);
    settings.vehicle_speed_multiplier = (std::clamp)(
        settings.vehicle_speed_multiplier,
        kMinimumSpeedMultiplier,
        kMaximumSpeedMultiplier);
    settings.vehicle_steering_boost = (std::clamp)(
        settings.vehicle_steering_boost,
        kMinimumSteeringBoost,
        kMaximumSteeringBoost);
}

void SetNoclipHookActive(TrainerProcess& process, bool active)
{
    if (!g_noclip_hook.applied || !process.IsConnected() ||
        process.ProcessId() != g_noclip_hook.process_id ||
        !IsSanePointer(g_noclip_hook.remote))
    {
        return;
    }
    const std::uint32_t value = active ? 1U : 0U;
    (void)process.WriteMemory(
        g_noclip_hook.remote + kNoclipRemoteActiveOffset, value);
}

void RemoveVehicleNoclipHook(TrainerProcess& process);
void RemoveNoclipNetworkPublishHook(TrainerProcess& process);

void RemoveNoclipHook(TrainerProcess& process)
{
    RemoveVehicleNoclipHook(process);
    // This trampoline refers to the data page owned by the primary noclip
    // hook, so it must be removed before that page can be released.
    RemoveNoclipNetworkPublishHook(process);
    if (!g_noclip_hook.applied)
    {
        g_noclip_hook = {};
        return;
    }

    if (!process.IsConnected() ||
        process.ProcessId() != g_noclip_hook.process_id)
    {
        g_noclip_hook = {};
        return;
    }

    SetNoclipHookActive(process, false);
    std::array<std::uint8_t, kNoclipHookPatchSize> current{};
    const bool current_read = process.ReadMemory(
        g_noclip_hook.hook, current.data(), current.size());
    const bool restored = current_read &&
        (current == g_noclip_hook.original ||
         (current == g_noclip_hook.patch && process.WriteProtectedMemory(
             g_noclip_hook.hook,
             g_noclip_hook.original.data(),
             g_noclip_hook.original.size())));

    bool executing = true;
    if (restored)
    {
        for (unsigned attempt = 0; attempt < 50; ++attempt)
        {
            if (!process.IsAnyThreadExecutingRange(
                    g_noclip_hook.remote,
                    kNoclipRemoteSize,
                    executing) || !executing)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    const bool freed = restored && !executing &&
        process.FreeRemoteMemory(g_noclip_hook.remote);
    LogDiagnostic(
        "Noclip: native tick hook removal restored=%d idle=%d freed=%d.",
        restored ? 1 : 0, !executing ? 1 : 0, freed ? 1 : 0);
    if (restored)
        g_noclip_hook = {};
}

void RemoveVehicleNoclipHook(TrainerProcess& process)
{
    if (!g_vehicle_noclip_hook.applied ||
        !process.IsConnected() ||
        process.ProcessId() != g_vehicle_noclip_hook.process_id)
    {
        g_vehicle_noclip_hook = {};
        return;
    }
    std::array<std::uint8_t, kVehicleNoclipHookPatchSize> current{};
    const bool restored =
        process.ReadMemory(
            g_vehicle_noclip_hook.hook, current.data(), current.size()) &&
        (current == g_vehicle_noclip_hook.original ||
         (current == g_vehicle_noclip_hook.patch &&
          process.WriteProtectedMemory(
              g_vehicle_noclip_hook.hook,
              g_vehicle_noclip_hook.original.data(),
              g_vehicle_noclip_hook.original.size())));
    bool executing = true;
    if (restored)
    {
        for (unsigned attempt = 0; attempt < 50; ++attempt)
        {
            if (process.IsAnyThreadExecutingRange(
                    g_vehicle_noclip_hook.remote,
                    kVehicleNoclipRemoteSize, executing) && !executing)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    LogDiagnostic(
        "Noclip: vehicle tick hook %s, page %s.",
        restored ? "restored" : "restoration unverified",
        (restored && !executing) ? "freed" : "kept");
    if (restored && !executing)
        (void)process.FreeRemoteMemory(g_vehicle_noclip_hook.remote);
    g_vehicle_noclip_hook = {};
}

// Second trampoline, on the car's own tick. It shares every data slot with the
// C_human::Tick page, so both hooks integrate the same position with the same
// velocity; only one of them ever matches, because the vehicle slot is 0 on
// foot and the car never ticks as a human. Unlike the human path this one
// writes no actor field except the validated speed float: an automobile has
// its own layout and the C_human physics offsets would land anywhere in it.
bool EnsureVehicleNoclipHook(TrainerProcess& process, std::uintptr_t shared)
{
    if (g_vehicle_noclip_hook.applied && process.IsConnected() &&
        g_vehicle_noclip_hook.process_id == process.ProcessId())
    {
        return true;
    }
    if (g_vehicle_noclip_hook.applied)
        RemoveVehicleNoclipHook(process);
    if (!process.IsConnected() || !IsSanePointer(shared))
        return false;

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        module.image_size <= kAutomobileTickRva + 11)
    {
        return false;
    }
    // Same routine and same signature the teleport already validates before
    // moving a vehicle; refuse the hook rather than call an unverified address.
    constexpr std::array<std::uint8_t, 15> kExpectedFrameUpdate{
        0x8B, 0x4C, 0x24, 0x04, 0x83, 0xEC, 0x0C, 0x8B,
        0x41, 0x0C, 0xA9, 0x00, 0x00, 0x80, 0x00};
    RemoteModuleInfo i3d_module{};
    std::array<std::uint8_t, kExpectedFrameUpdate.size()> frame_update_bytes{};
    std::uintptr_t frame_update = 0;
    if (!process.GetModuleInfo(L"i3d2.dll", i3d_module) ||
        kI3dFrameUpdateRva + kExpectedFrameUpdate.size() >
            i3d_module.image_size ||
        (frame_update = i3d_module.base_address + kI3dFrameUpdateRva) == 0 ||
        !process.ReadMemory(
            frame_update, frame_update_bytes.data(),
            frame_update_bytes.size()) ||
        frame_update_bytes != kExpectedFrameUpdate ||
        !process.IsReadableCodeTarget(frame_update))
    {
        LogDiagnostic(
            "Noclip: I3D_frame::Update unavailable at %08X; vehicle flight "
            "refused.",
            static_cast<unsigned>(frame_update));
        return false;
    }
    VehicleNoclipHookState next{};
    next.process_id = process.ProcessId();
    next.hook = module.base_address + kAutomobileTickRva;
    constexpr std::array<std::uint8_t, 11> expected{
        0x81, 0xEC, 0xC8, 0x00, 0x00, 0x00, 0x53, 0x55, 0x56, 0x8B, 0xF1};
    std::array<std::uint8_t, expected.size()> signature{};
    if (!process.ReadMemory(next.hook, signature.data(), signature.size()) ||
        signature != expected)
    {
        LogDiagnostic(
            "Noclip: C_automobile::Tick signature mismatch at %08X.",
            static_cast<unsigned>(next.hook));
        return false;
    }
    std::copy(
        expected.begin(), expected.begin() + kVehicleNoclipHookPatchSize,
        next.original.begin());

    next.remote = process.AllocateRemoteMemory(kVehicleNoclipRemoteSize);
    if (!IsSanePointer(next.remote))
        return false;

    std::vector<std::uint8_t> code;
    const auto byte = [&](std::uint8_t value) { code.push_back(value); };
    const auto dword = [&](std::uint32_t value)
    {
        const auto* raw = reinterpret_cast<const std::uint8_t*>(&value);
        code.insert(code.end(), raw, raw + sizeof(value));
    };
    // Every data slot lives in the C_human::Tick page, addressed absolutely.
    const auto shared_slot = [&](std::size_t offset)
    {
        dword(static_cast<std::uint32_t>(shared + offset));
    };
    const auto near_jump = [&](std::uint8_t condition)
    {
        byte(0x0F); byte(condition);
        const std::size_t displacement = code.size();
        dword(0);
        return displacement;
    };
    const auto patch_jump = [&](std::size_t displacement, std::size_t target)
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            target - (displacement + sizeof(std::int32_t)));
        std::memcpy(code.data() + displacement, &relative, sizeof(relative));
    };

    byte(0x9C);                              // pushfd
    byte(0x60);                              // pushad
    byte(0x83); byte(0x3D); shared_slot(kNoclipRemoteActiveOffset); byte(0x01);
    const std::size_t inactive = near_jump(0x85);
    byte(0x3B); byte(0x0D); shared_slot(kNoclipRemoteVehicleOffset);
    const std::size_t other_vehicle = near_jump(0x85);
    // C_automobil::Tick1(int time, byte net_route_bits). Unlike C_human::Tick
    // the argument is the elapsed milliseconds itself, not a tick context:
    // `sub ebx,edi` and `add eax,edi` at 0x0044E566/0x0044E58C use it as a
    // number. V26.1 read it as a pointer and dereferenced it, which killed the
    // game on the first frame of vehicle noclip.
    byte(0x8B); byte(0x44); byte(0x24); byte(0x28); // mov eax,[esp+28h]
    byte(0x85); byte(0xC0);
    const std::size_t no_time = near_jump(0x8E);   // jle done
    byte(0x83); byte(0xF8); byte(0x32);      // cmp eax,50
    const std::size_t time_ok = near_jump(0x8E);
    byte(0xB8); dword(50);
    const std::size_t store_time = code.size();
    byte(0xA3); shared_slot(kNoclipRemoteElapsedOffset);

    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        byte(0xDB); byte(0x05); shared_slot(kNoclipRemoteElapsedOffset);
        byte(0xD8); byte(0x0D); shared_slot(kNoclipRemoteSecondsPerMsOffset);
        byte(0xD8); byte(0x0D); shared_slot(
            kNoclipRemoteVelocityOffset + axis * sizeof(float));
        byte(0xD8); byte(0x05); shared_slot(
            kNoclipRemotePositionOffset + axis * sizeof(float));
        byte(0xD9); byte(0x1D); shared_slot(
            kNoclipRemotePositionOffset + axis * sizeof(float));
    }

    // Stop the car integrating its own motion; 0x244 is the speed float the
    // vehicle-speed feature already reads back and validates.
    byte(0xC7); byte(0x81); dword(
        static_cast<std::uint32_t>(kAutomobileSpeedOffset)); dword(0);
    byte(0x8B); byte(0x15); shared_slot(kNoclipRemoteBodyFrameOffset);
    byte(0x85); byte(0xD2);
    const std::size_t no_frame = near_jump(0x84);
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        byte(0xA1); shared_slot(
            kNoclipRemotePositionOffset + axis * sizeof(float));
        byte(0x89); byte(0x82); dword(static_cast<std::uint32_t>(
            kFrameLocalPositionOffset + axis * sizeof(float)));
    }
    byte(0x81); byte(0x4A); byte(0x0C); dword(0x0080'0000U);
    // Exact engine sequence for a moved vehicle: the position write above
    // stands in for SetPos, then Update consumes the dirty bit and rebuilds
    // the world matrix, then the scene re-links the frame into its sector.
    byte(0xFF); byte(0x35); shared_slot(kNoclipRemoteBodyFrameOffset);
    byte(0xB8); dword(static_cast<std::uint32_t>(frame_update));
    byte(0xFF); byte(0xD0);                  // I3D_frame::Update(frame)
    byte(0xA1); shared_slot(kNoclipRemoteSceneOffset);
    byte(0x85); byte(0xC0);                  // test eax,eax
    byte(0x74);
    const std::size_t relink_displacement = code.size();
    byte(0);                                 // jz skip_relink
    byte(0xFF); byte(0x35); shared_slot(kNoclipRemoteBodyFrameOffset);
    byte(0x50);                              // push scene (this)
    byte(0x8B); byte(0x00);                  // mov eax,[eax] (vtable)
    byte(0xFF); byte(0x50); byte(0x60);      // call SetFrameSector
    code[relink_displacement] = static_cast<std::uint8_t>(
        code.size() - (relink_displacement + 1));
    byte(0xFF); byte(0x05); shared_slot(kNoclipRemoteVehiclePassOffset);

    const std::size_t done = code.size();
    patch_jump(inactive, done);
    patch_jump(other_vehicle, done);
    patch_jump(no_time, done);
    patch_jump(time_ok, store_time);
    patch_jump(no_frame, done);
    byte(0x61);                              // popad
    byte(0x9D);                              // popfd
    code.insert(
        code.end(), next.original.begin(), next.original.end());
    byte(0xE9);
    const std::int32_t return_relative = static_cast<std::int32_t>(
        (next.hook + kVehicleNoclipHookPatchSize) -
        (next.remote + code.size() + sizeof(std::int32_t)));
    dword(static_cast<std::uint32_t>(return_relative));

    if (code.size() > kVehicleNoclipRemoteSize ||
        !process.WriteMemory(next.remote, code.data(), code.size()))
    {
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }

    next.patch.fill(0x90);
    next.patch[0] = 0xE9;
    const std::int32_t hook_relative = static_cast<std::int32_t>(
        next.remote - (next.hook + 5));
    std::memcpy(next.patch.data() + 1, &hook_relative, sizeof(hook_relative));
    if (!process.WriteProtectedMemory(
            next.hook, next.patch.data(), next.patch.size()))
    {
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }
    std::array<std::uint8_t, kVehicleNoclipHookPatchSize> verified{};
    if (!process.ReadMemory(next.hook, verified.data(), verified.size()) ||
        verified != next.patch)
    {
        (void)process.WriteProtectedMemory(
            next.hook, next.original.data(), next.original.size());
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }
    next.applied = true;
    g_vehicle_noclip_hook = next;
    LogDiagnostic(
        "Noclip: native C_automobile::Tick hook installed at %08X "
        "remote=%08X shared=%08X update=%08X.",
        static_cast<unsigned>(next.hook),
        static_cast<unsigned>(next.remote),
        static_cast<unsigned>(shared),
        static_cast<unsigned>(frame_update));
    return true;
}

bool EnsureNoclipHook(TrainerProcess& process)
{
    if (g_noclip_hook.applied && process.IsConnected() &&
        g_noclip_hook.process_id == process.ProcessId())
    {
        return true;
    }
    if (g_noclip_hook.applied)
        RemoveNoclipHook(process);
    if (!process.IsConnected())
        return false;

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        module.image_size <= kHumanTickRva + kNoclipHookPatchSize)
    {
        return false;
    }
    NoclipHookState next{};
    next.process_id = process.ProcessId();
    next.hook = module.base_address + kHumanTickRva;
    constexpr std::array<std::uint8_t, kNoclipHookPatchSize> expected{
        0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x6C, 0x03, 0x00, 0x00};
    if (!process.ReadMemory(next.hook, next.original.data(), next.original.size()) ||
        next.original != expected)
    {
        LogDiagnostic(
            "Noclip: C_human::Tick signature mismatch at %08X.",
            static_cast<unsigned>(next.hook));
        return false;
    }

    next.remote = process.AllocateRemoteMemory(kNoclipRemoteSize);
    if (!IsSanePointer(next.remote))
        return false;

    std::vector<std::uint8_t> code;
    code.reserve(kNoclipRemoteActiveOffset);
    const auto byte = [&](std::uint8_t value) { code.push_back(value); };
    const auto dword = [&](std::uint32_t value)
    {
        const auto* raw = reinterpret_cast<const std::uint8_t*>(&value);
        code.insert(code.end(), raw, raw + sizeof(value));
    };
    const auto absolute = [&](std::size_t offset)
    {
        dword(static_cast<std::uint32_t>(next.remote + offset));
    };
    const auto near_jump = [&](std::uint8_t condition)
    {
        byte(0x0F); byte(condition);
        const std::size_t displacement = code.size();
        dword(0);
        return displacement;
    };
    const auto patch_jump = [&](std::size_t displacement, std::size_t target)
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            target - (displacement + sizeof(std::int32_t)));
        std::memcpy(
            code.data() + displacement, &relative, sizeof(relative));
    };

    byte(0x9C);                              // pushfd
    byte(0x60);                              // pushad
    byte(0x83); byte(0x3D); absolute(kNoclipRemoteActiveOffset); byte(0x01);
    const std::size_t inactive = near_jump(0x85); // jne done
    byte(0x3B); byte(0x0D); absolute(kNoclipRemoteActorOffset); // cmp ecx,[actor]
    const std::size_t other_actor = near_jump(0x85);
    byte(0x8B); byte(0x44); byte(0x24); byte(0x28); // mov eax,[esp+28h]
    byte(0x85); byte(0xC0);                  // test eax,eax
    const std::size_t no_context = near_jump(0x84);
    byte(0x8B); byte(0x00);                  // mov eax,[eax] (tc.time)
    byte(0x85); byte(0xC0);                  // test eax,eax
    const std::size_t no_time = near_jump(0x8E); // jle done
    byte(0x83); byte(0xF8); byte(0x32);      // cmp eax,50
    const std::size_t time_ok = near_jump(0x8E); // jle store_time
    byte(0xB8); dword(50);                   // mov eax,50
    const std::size_t store_time = code.size();
    byte(0xA3); absolute(kNoclipRemoteElapsedOffset);

    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        byte(0xDB); byte(0x05); absolute(kNoclipRemoteElapsedOffset); // fild
        byte(0xD8); byte(0x0D); absolute(kNoclipRemoteSecondsPerMsOffset);
        byte(0xD8); byte(0x0D); absolute(
            kNoclipRemoteVelocityOffset + axis * sizeof(float));
        byte(0xD8); byte(0x05); absolute(
            kNoclipRemotePositionOffset + axis * sizeof(float));
        byte(0xD9); byte(0x1D); absolute(
            kNoclipRemotePositionOffset + axis * sizeof(float));
    }

    byte(0x8B); byte(0x15); absolute(kNoclipRemoteActorOffset); // mov edx,[actor]
    byte(0xC7); byte(0x82); dword(0x1AC); dword(0); // move_dir.x
    byte(0xC7); byte(0x82); dword(0x1B0); dword(0); // move_dir.y
    byte(0xC7); byte(0x82); dword(0x1B4); dword(0); // move_dir.z
    byte(0xC7); byte(0x82); dword(0x1B8); dword(0); // fall_speed
    byte(0xC6); byte(0x82); dword(0x264); byte(0);  // falling
    // Keep native collision suppressed: the movement above intentionally
    // crosses geometry. A forced native update would make noclip collide with
    // walls again.
    byte(0xC7); byte(0x82); dword(0x274); dword(kNoclipCollisionHoldMs);
    // Move the published frame rather than the ticking actor's own one: on
    // foot they are the same, while driving it is the vehicle's frame.
    byte(0x8B); byte(0x15); absolute(kNoclipRemoteBodyFrameOffset);
    byte(0x85); byte(0xD2);                  // test edx,edx
    const std::size_t no_frame = near_jump(0x84);
    byte(0xFF); byte(0x05); absolute(kNoclipRemoteTickCountOffset);
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        byte(0xA1); absolute(
            kNoclipRemotePositionOffset + axis * sizeof(float));
        byte(0x89); byte(0x82); dword(static_cast<std::uint32_t>(
            kFrameLocalPositionOffset + axis * sizeof(float)));
    }
    byte(0x81); byte(0x4A); byte(0x0C); dword(0x0080'0000U);

    const std::size_t done = code.size();
    patch_jump(inactive, done);
    patch_jump(other_actor, done);
    patch_jump(no_context, done);
    patch_jump(no_time, done);
    patch_jump(time_ok, store_time);
    patch_jump(no_frame, done);
    byte(0x61);                              // popad
    byte(0x9D);                              // popfd
    code.insert(code.end(), expected.begin(), expected.end());
    byte(0xE9);
    const std::int32_t return_relative = static_cast<std::int32_t>(
        (next.hook + kNoclipHookPatchSize) -
        (next.remote + code.size() + sizeof(std::int32_t)));
    dword(static_cast<std::uint32_t>(return_relative));

    if (code.size() >= kNoclipRemoteActiveOffset)
    {
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }
    std::array<std::uint8_t, kNoclipRemoteSize> remote_image{};
    std::copy(code.begin(), code.end(), remote_image.begin());
    const float seconds_per_ms = 0.001f;
    std::memcpy(
        remote_image.data() + kNoclipRemoteSecondsPerMsOffset,
        &seconds_per_ms,
        sizeof(seconds_per_ms));
    if (!process.WriteMemory(
            next.remote, remote_image.data(), remote_image.size()))
    {
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }

    next.patch.fill(0x90);
    next.patch[0] = 0xE9;
    const std::int32_t hook_relative = static_cast<std::int32_t>(
        next.remote - (next.hook + 5));
    std::memcpy(next.patch.data() + 1, &hook_relative, sizeof(hook_relative));
    if (!process.WriteProtectedMemory(
            next.hook, next.patch.data(), next.patch.size()))
    {
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }
    std::array<std::uint8_t, kNoclipHookPatchSize> verified{};
    if (!process.ReadMemory(next.hook, verified.data(), verified.size()) ||
        verified != next.patch)
    {
        (void)process.WriteProtectedMemory(
            next.hook, next.original.data(), next.original.size());
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }
    next.applied = true;
    g_noclip_hook = next;
    LogDiagnostic(
        "Noclip: native C_human::Tick hook installed at %08X remote=%08X.",
        static_cast<unsigned>(next.hook),
        static_cast<unsigned>(next.remote));
    return true;
}

// Publishes a noclip position through the game's own NM_HUMAN_POS code path.
// The hook is deliberately after the collision decision: setting `upd` there
// lets the normal frame-sector and net-message code run, while the earlier
// collision test remains skipped by kNoclipCollisionHoldMs.
bool EnsureNoclipNetworkPublishHook(TrainerProcess& process)
{
    if (g_noclip_network_publish_hook.applied && process.IsConnected() &&
        g_noclip_network_publish_hook.process_id == process.ProcessId())
    {
        return true;
    }
    if (g_noclip_network_publish_hook.applied)
        RemoveNoclipNetworkPublishHook(process);
    if (!process.IsConnected() || !g_noclip_hook.applied ||
        g_noclip_hook.process_id != process.ProcessId() ||
        !IsSanePointer(g_noclip_hook.remote))
    {
        return false;
    }

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        module.image_size <= kNoclipNetworkPublishRva +
            kNoclipNetworkPublishHookPatchSize)
    {
        return false;
    }

    NoclipNetworkPublishHookState next{};
    next.process_id = process.ProcessId();
    next.hook = module.base_address + kNoclipNetworkPublishRva;
    constexpr std::array<std::uint8_t, kNoclipNetworkPublishHookPatchSize>
        expected{
            0x8A, 0x45, 0xFF, 0x84, 0xC0, 0x0F,
            0x84, 0x9B, 0x01, 0x00, 0x00};
    if (!process.ReadMemory(
            next.hook, next.original.data(), next.original.size()) ||
        next.original != expected)
    {
        LogDiagnostic(
            "Noclip: network publish signature mismatch at %08X.",
            static_cast<unsigned>(next.hook));
        return false;
    }

    next.remote = process.AllocateRemoteMemory(0x100);
    if (!IsSanePointer(next.remote))
        return false;

    std::vector<std::uint8_t> code;
    code.reserve(0x80);
    const auto byte = [&](std::uint8_t value) { code.push_back(value); };
    const auto dword = [&](std::uint32_t value)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(value));
        std::memcpy(code.data() + offset, &value, sizeof(value));
    };
    const auto absolute_shared = [&](std::size_t offset)
    {
        dword(static_cast<std::uint32_t>(g_noclip_hook.remote + offset));
    };
    const auto jump_to = [&](std::uintptr_t target)
    {
        byte(0xE9);
        const std::int32_t relative = static_cast<std::int32_t>(
            target - (next.remote + code.size() + sizeof(std::int32_t)));
        dword(static_cast<std::uint32_t>(relative));
    };

    byte(0x9C);                              // pushfd
    byte(0x60);                              // pushad
    byte(0x83); byte(0x3D);                  // cmp [active], 1
    absolute_shared(kNoclipRemoteActiveOffset);
    byte(0x01);
    byte(0x75);                              // jne done
    const std::size_t inactive_disp = code.size();
    byte(0x00);
    byte(0xA1);                              // mov eax, [actor]
    absolute_shared(kNoclipRemoteActorOffset);
    byte(0x3B); byte(0xD8);                  // cmp ebx, eax
    byte(0x75);                              // jne done
    const std::size_t other_actor_disp = code.size();
    byte(0x00);
    byte(0xC6); byte(0x45); byte(0xFF); byte(0x01);
                                               // upd = true
    byte(0xFF); byte(0x05);                  // ++publish_count
    absolute_shared(kNoclipRemotePublishCountOffset);
    const std::size_t done = code.size();
    code[inactive_disp] = static_cast<std::uint8_t>(
        done - (inactive_disp + 1));
    code[other_actor_disp] = static_cast<std::uint8_t>(
        done - (other_actor_disp + 1));
    byte(0x61);                              // popad
    byte(0x9D);                              // popfd
    byte(0x80); byte(0x7D); byte(0xFF); byte(0x00);
                                               // cmp byte ptr [ebp-1], 0
    byte(0x0F); byte(0x85);                  // jne native update block
    const std::int32_t updated_relative = static_cast<std::int32_t>(
        (module.base_address + 0x0001'BC59) -
        (next.remote + code.size() + sizeof(std::int32_t)));
    dword(static_cast<std::uint32_t>(updated_relative));
    jump_to(module.base_address + 0x0001'BDF4); // original `upd == false`

    if (code.size() > 0x100 ||
        !process.WriteMemory(next.remote, code.data(), code.size()))
    {
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }
    next.patch.fill(0x90);
    next.patch[0] = 0xE9;
    const std::int32_t hook_relative = static_cast<std::int32_t>(
        next.remote - (next.hook + sizeof(std::int32_t) + 1));
    std::memcpy(next.patch.data() + 1, &hook_relative, sizeof(hook_relative));
    if (!process.WriteProtectedMemory(
            next.hook, next.patch.data(), next.patch.size()))
    {
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }
    std::array<std::uint8_t, kNoclipNetworkPublishHookPatchSize> verified{};
    if (!process.ReadMemory(next.hook, verified.data(), verified.size()) ||
        verified != next.patch)
    {
        (void)process.WriteProtectedMemory(
            next.hook, next.original.data(), next.original.size());
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }
    next.applied = true;
    g_noclip_network_publish_hook = next;
    LogDiagnostic(
        "Noclip: collision-free network publish hook installed at %08X "
        "remote=%08X.",
        static_cast<unsigned>(next.hook), static_cast<unsigned>(next.remote));
    return true;
}

void ClearNoclipState()
{
    g_noclip = {};
}

void StopNoclipMovement(TrainerProcess& process)
{
    // Clearing the published car first means its trampoline stops matching
    // before anything else is undone, whatever happens next.
    if (g_noclip_hook.applied && process.IsConnected() &&
        process.ProcessId() == g_noclip_hook.process_id)
    {
        const std::uint32_t no_vehicle = 0;
        (void)process.WriteMemory(
            g_noclip_hook.remote + kNoclipRemoteVehicleOffset, no_vehicle);
    }
    SetNoclipHookActive(process, false);
    if (g_noclip.active && process.IsConnected() &&
        process.ProcessId() == g_noclip.process_id &&
        IsSanePointer(g_noclip.actor))
    {
        const Vector3 stopped{};
        const std::uint8_t not_falling = 0;
        const std::int32_t collision_test_now = 0;
        (void)process.WriteMemory(
            g_noclip.actor + kActorMoveDirectionOffset, stopped);
        (void)process.WriteMemory(
            g_noclip.actor + kActorFallSpeedOffset,
            kNoclipReleasedFallSpeed);
        (void)process.WriteMemory(
            g_noclip.actor + kActorFallingOffset, not_falling);
        (void)process.WriteMemory(
            g_noclip.actor + kActorCollisionTestCountdownOffset,
            collision_test_now);
    }
    ClearNoclipState();
}

void RemoveNoclipNetworkPublishHook(TrainerProcess& process)
{
    if (!g_noclip_network_publish_hook.applied)
    {
        g_noclip_network_publish_hook = {};
        return;
    }
    if (!process.IsConnected() ||
        process.ProcessId() != g_noclip_network_publish_hook.process_id)
    {
        g_noclip_network_publish_hook = {};
        return;
    }

    std::array<std::uint8_t, kNoclipNetworkPublishHookPatchSize> current{};
    const bool restored = process.ReadMemory(
            g_noclip_network_publish_hook.hook,
            current.data(), current.size()) &&
        (current == g_noclip_network_publish_hook.original ||
         (current == g_noclip_network_publish_hook.patch &&
          process.WriteProtectedMemory(
              g_noclip_network_publish_hook.hook,
              g_noclip_network_publish_hook.original.data(),
              g_noclip_network_publish_hook.original.size())));
    bool executing = true;
    if (restored)
    {
        for (unsigned attempt = 0; attempt < 50; ++attempt)
        {
            if (!process.IsAnyThreadExecutingRange(
                    g_noclip_network_publish_hook.remote, 0x100,
                    executing) || !executing)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    const bool freed = restored && !executing &&
        process.FreeRemoteMemory(g_noclip_network_publish_hook.remote);
    LogDiagnostic(
        "Noclip: network-publish hook removal restored=%d idle=%d freed=%d.",
        restored ? 1 : 0, !executing ? 1 : 0, freed ? 1 : 0);
    if (restored)
        g_noclip_network_publish_hook = {};
}

void SetNetworkPositionMaskHookActive(TrainerProcess& process, bool active)
{
    if (!g_network_position_mask_hook.applied || !process.IsConnected() ||
        process.ProcessId() != g_network_position_mask_hook.process_id ||
        !IsSanePointer(g_network_position_mask_hook.remote))
    {
        return;
    }

    const std::uint32_t value = active ? 1U : 0U;
    (void)process.WriteMemory(
        g_network_position_mask_hook.remote +
            kNetworkPositionMaskActiveOffset,
        value);
}

// Sends only a companion instruction to the already-running Client helper.
// UDP broadcast avoids asking the player for an IP address. A missing
// heartbeat fails safe: the helper restores native rendering and death logic.
// The game itself neither receives nor emits a modified H&D message here.
std::uint16_t ReadActorNetworkId(
    TrainerProcess& process,
    std::uintptr_t actor)
{
    std::uint16_t network_id = 0;
    return IsSanePointer(actor) &&
        process.ReadMemory(actor + 0x20, network_id) ? network_id : 0;
}

// V100, 3 septembre 2026 - pourquoi le compagnon ne recevait rien.
//
// Le journal du PC ami montrait la reception demarree et AUCUNE ligne
// « [trace] RX ». Or cette trace est ecrite a chaque paquet accepte comme a
// chaque paquet refuse : rien n'arrivait donc du tout, ce qui elimine a la fois
// la version du compagnon et les offsets de vie.
//
// La cause est l'adresse d'envoi. Le trainer n'ecrivait que vers
// 255.255.255.255, la diffusion dite limitee. Beaucoup de configurations ne la
// delivrent pas : adaptateurs virtuels de LAN (Hamachi, Radmin, ZeroTier), Wi-Fi
// avec isolation des clients, certaines piles pare-feu. Le jeu, lui, continue de
// fonctionner car il utilise ses propres adresses.
//
// Le message part desormais vers TOUTES les adresses plausibles : la diffusion
// limitee, et la diffusion dirigee de chaque interface locale (adresse | ~masque),
// par exemple 192.168.1.255 sur un reseau domestique et 25.255.255.255 sur un
// adaptateur Hamachi. C'est cette derniere qui traverse un LAN virtuel.
std::vector<std::uint32_t> CollectBroadcastTargets()
{
    std::vector<std::uint32_t> targets;
    targets.push_back(INADDR_BROADCAST);

    SOCKET probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (probe == INVALID_SOCKET)
        return targets;
    std::array<INTERFACE_INFO, 32> interfaces{};
    DWORD returned = 0;
    if (WSAIoctl(
            probe, SIO_GET_INTERFACE_LIST, nullptr, 0,
            interfaces.data(),
            static_cast<DWORD>(interfaces.size() * sizeof(INTERFACE_INFO)),
            &returned, nullptr, nullptr) == 0)
    {
        const std::size_t count = returned / sizeof(INTERFACE_INFO);
        for (std::size_t index = 0; index < count; ++index)
        {
            const INTERFACE_INFO& entry = interfaces[index];
            if ((entry.iiFlags & IFF_UP) == 0 ||
                (entry.iiFlags & IFF_LOOPBACK) != 0 ||
                (entry.iiFlags & IFF_BROADCAST) == 0)
            {
                continue;
            }
            const std::uint32_t address =
                entry.iiAddress.AddressIn.sin_addr.s_addr;
            const std::uint32_t mask =
                entry.iiNetmask.AddressIn.sin_addr.s_addr;
            if (address == 0 || mask == 0)
                continue;
            const std::uint32_t directed = address | ~mask;
            if (std::find(targets.begin(), targets.end(), directed) ==
                targets.end())
            {
                targets.push_back(directed);
            }
        }
    }
    closesocket(probe);
    return targets;
}

void PublishPeerVisualCommand(bool hide_host_models, bool force = false)
{
    const DWORD now = GetTickCount();
    const bool suppress_host_player_death =
        g_peer_remote_life_mirror_enabled;
    const bool suppress_host_player_damage =
        g_peer_remote_damage_mirror_enabled;
    const bool extend_client_health = g_peer_extend_client_health;
    const bool mask_client_position = g_peer_mask_client_position;
    const std::uint16_t visual_target_network_id = hide_host_models &&
        g_network_position_mask.scope ==
            NetworkPositionMaskScope::ControlledPlayer
        ? g_network_position_mask.controlled_network_id : 0;
    const std::uint16_t life_target_network_id =
        (suppress_host_player_death || suppress_host_player_damage ||
         g_peer_client_revive_enabled) ? g_peer_life_target_network_id : 0;
    const bool heartbeat_due =
        (hide_host_models || suppress_host_player_death ||
         suppress_host_player_damage || extend_client_health ||
         mask_client_position) &&
        static_cast<DWORD>(now - g_peer_visual_sender.last_send_tick) >=
            kPeerVisualCommandHeartbeatMs;
    const bool state_changed = hide_host_models != g_peer_visual_sender.last_hidden ||
        suppress_host_player_death != g_peer_visual_sender.last_death_suppressed ||
        suppress_host_player_damage != g_peer_visual_sender.last_damage_suppressed ||
        extend_client_health != g_peer_visual_sender.last_client_health ||
        mask_client_position != g_peer_visual_sender.last_client_position ||
        visual_target_network_id != g_peer_visual_sender.last_visual_target_network_id ||
        life_target_network_id != g_peer_visual_sender.last_life_target_network_id ||
        g_peer_revive_sequence != g_peer_visual_sender.last_revive_sequence;
    if (!force && !state_changed && !heartbeat_due &&
        hide_host_models == g_peer_visual_sender.last_hidden &&
        suppress_host_player_death ==
            g_peer_visual_sender.last_death_suppressed &&
        suppress_host_player_damage ==
            g_peer_visual_sender.last_damage_suppressed &&
        visual_target_network_id ==
            g_peer_visual_sender.last_visual_target_network_id &&
        life_target_network_id ==
            g_peer_visual_sender.last_life_target_network_id &&
        g_peer_revive_sequence == g_peer_visual_sender.last_revive_sequence &&
        !heartbeat_due)
    {
        return;
    }

    if (!g_peer_visual_sender.winsock_ready)
    {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            LogDiagnostic("Network position mask: peer-visual LAN socket unavailable.");
            return;
        }
        g_peer_visual_sender.winsock_ready = true;
    }
    if (g_peer_visual_sender.socket == INVALID_SOCKET)
    {
        g_peer_visual_sender.socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (g_peer_visual_sender.socket == INVALID_SOCKET)
        {
            LogDiagnostic("Network position mask: peer-visual UDP socket creation failed.");
            return;
        }
        const BOOL broadcast = TRUE;
        if (setsockopt(
                g_peer_visual_sender.socket, SOL_SOCKET, SO_BROADCAST,
                reinterpret_cast<const char*>(&broadcast), sizeof(broadcast)) ==
            SOCKET_ERROR)
        {
            closesocket(g_peer_visual_sender.socket);
            g_peer_visual_sender.socket = INVALID_SOCKET;
            LogDiagnostic("Network position mask: peer-visual broadcast unavailable.");
            return;
        }
    }

    PeerVisualCommandWire command{};
    command.hide_host_models = hide_host_models ? 1U : 0U;
    command.suppress_host_player_death =
        suppress_host_player_death ? 1U : 0U;
    command.suppress_host_player_damage =
        suppress_host_player_damage ? 1U : 0U;
    command.extend_client_health = extend_client_health ? 1U : 0U;
    command.mask_client_position = mask_client_position ? 1U : 0U;
    command.health_count = extend_client_health ? g_peer_health_count : 0U;
    for (std::size_t index = 0; index < kPeerHealthSlots; ++index)
        command.health[index] = g_peer_health_table[index];
    command.visual_target_network_id = visual_target_network_id;
    command.life_target_network_id = life_target_network_id;
    command.revive_sequence = g_peer_revive_sequence;
    // Les interfaces changent rarement; les relire toutes les cinq secondes
    // suffit et evite un WSAIoctl a chaque image.
    static std::vector<std::uint32_t> targets;
    static DWORD next_refresh = 0;
    if (targets.empty() || now >= next_refresh)
    {
        next_refresh = now + 5000U;
        targets = CollectBroadcastTargets();
    }
    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(kPeerVisualCommandPort);
    unsigned delivered = 0;
    int last_error = 0;
    for (const std::uint32_t target : targets)
    {
        destination.sin_addr.s_addr = target;
        const int sent = sendto(
            g_peer_visual_sender.socket,
            reinterpret_cast<const char*>(&command), sizeof(command), 0,
            reinterpret_cast<const sockaddr*>(&destination),
            sizeof(destination));
        if (sent == SOCKET_ERROR)
            last_error = WSAGetLastError();
        else
            ++delivered;
    }
    if (delivered == 0)
    {
        LogDiagnostic(
            "Network position mask: peer-visual command send failed (%d) "
            "targets=%u.",
            last_error, static_cast<unsigned>(targets.size()));
        return;
    }
    if (state_changed || force)
    {
        LogDiagnostic(
            "LAN->Client command: hide=%u death_guard=%u damage_guard=%u "
            "client_health=%u client_position=%u visual_id=%u life_id=%u "
            "revive=%u force=%u.",
            hide_host_models ? 1U : 0U, suppress_host_player_death ? 1U : 0U,
            suppress_host_player_damage ? 1U : 0U,
            extend_client_health ? 1U : 0U, mask_client_position ? 1U : 0U,
            static_cast<unsigned>(visual_target_network_id),
            static_cast<unsigned>(life_target_network_id),
            static_cast<unsigned>(g_peer_revive_sequence), force ? 1U : 0U);
    }
    g_peer_visual_sender.last_hidden = hide_host_models;
    g_peer_visual_sender.last_death_suppressed =
        suppress_host_player_death;
    g_peer_visual_sender.last_damage_suppressed =
        suppress_host_player_damage;
    g_peer_visual_sender.last_client_health = extend_client_health;
    g_peer_visual_sender.last_client_position = mask_client_position;
    g_peer_visual_sender.last_visual_target_network_id =
        visual_target_network_id;
    g_peer_visual_sender.last_life_target_network_id =
        life_target_network_id;
    g_peer_visual_sender.last_revive_sequence = g_peer_revive_sequence;
    g_peer_visual_sender.last_send_tick = now;

    // Trace periodique : sans elle, un ordre qui part correctement mais que le
    // CLIENT n'applique pas est indiscernable d'un ordre jamais emis.
    static DWORD next_periodic = 0;
    if (now >= next_periodic)
    {
        next_periodic = now + 5000U;
        LogDiagnostic(
            "LAN->Client heartbeat: client_health=%u client_position=%u "
            "hide=%u death=%u damage=%u adresses=%u.",
            extend_client_health ? 1U : 0U, mask_client_position ? 1U : 0U,
            hide_host_models ? 1U : 0U, suppress_host_player_death ? 1U : 0U,
            suppress_host_player_damage ? 1U : 0U,
            static_cast<unsigned>(targets.size()));
    }
}

void ClosePeerVisualCommandSender()
{
    // A final SHOW prevents a permanently hidden model when the trainer exits
    // normally. The Client helper also has an independent timeout for a crash.
    if (g_peer_visual_sender.socket != INVALID_SOCKET)
    {
        g_peer_remote_life_mirror_enabled = false;
        g_peer_remote_damage_mirror_enabled = false;
        g_peer_client_revive_enabled = false;
        g_peer_extend_client_health = false;
        g_peer_mask_client_position = false;
        PublishPeerVisualCommand(false, true);
        closesocket(g_peer_visual_sender.socket);
    }
    if (g_peer_visual_sender.winsock_ready)
        WSACleanup();
    g_peer_visual_sender = {};
    g_peer_remote_life_mirror_enabled = false;
    g_peer_remote_damage_mirror_enabled = false;
    g_peer_client_revive_enabled = false;
}

void UpdatePeerRemoteLifeMirror(
    TrainerProcess& process,
    const RadarSnapshot* snapshot,
    const GameplaySettings& settings)
{
    // This remains entirely separate from the local absolute-protection
    // setting. The host may die normally; only the Client's representation of
    // host-owned players keeps the death transition out until F12 revives them
    // locally. The regular command heartbeat also makes this fail safe.
    // Total protection proves locally that the selected host actor must stay
    // alive. Arm the Client mirror automatically in that case, so an inbound
    // explosion packet cannot turn the peer's copy into a skeleton while the
    // host remains alive. The separate F12 option still works by itself when
    // deliberate local death/revive is desired.
    g_peer_remote_life_mirror_enabled =
        settings.absolute_player_protection_enabled;
    g_peer_remote_damage_mirror_enabled =
        settings.absolute_player_protection_enabled;
    g_peer_client_revive_enabled = settings.remote_life_mirror_enabled;
    // Sante Max couvre « tous les soldats, et meme les amis » : l'hote traite
    // les siens, chaque CLIENT traite les siens sur ordre.
    g_peer_extend_client_health = settings.extended_health_enabled;
    // Masquer ma position en portee escouade : chaque CLIENT fige aussi la
    // position qu'il publie pour son propre soldat.
    g_peer_mask_client_position =
        g_network_position_mask.active &&
        g_network_position_mask.remote_visual_hidden &&
        g_network_position_mask.scope ==
            NetworkPositionMaskScope::WholeSquad;
    // When H&D switches control immediately after a death, the snapshot now
    // points to a living ally.  Keep the pre-switch actor selected until its
    // F12 native revive completes; otherwise the Client would be told to
    // protect the wrong soldier during exactly that short transition.
    const std::uintptr_t life_target =
        IsSanePointer(g_revive_target_history.last_dead_controlled_player)
        ? g_revive_target_history.last_dead_controlled_player
        : (snapshot ? snapshot->player_object_address : 0);
    g_peer_life_target_network_id =
        settings.absolute_player_protection_enabled &&
        settings.absolute_player_protection_scope ==
            AbsolutePlayerProtectionScope::WholeSquad
        ? kPeerAllHostPlayersNetworkId
        : ReadActorNetworkId(process, life_target);
    PublishPeerVisualCommand(g_network_position_mask.remote_visual_hidden);
}

void StopNetworkPositionMask(TrainerProcess& process)
{
    SetNetworkPositionMaskHookActive(process, false);
    if (g_network_position_mask.remote_visual_hidden)
        PublishPeerVisualCommand(false, true);
    g_network_position_mask = {};
}

void RemoveNetworkPositionMaskHook(TrainerProcess& process)
{
    if (!g_network_position_mask_hook.applied)
        return;

    if (!process.IsConnected() ||
        process.ProcessId() != g_network_position_mask_hook.process_id)
    {
        g_network_position_mask_hook = {};
        return;
    }

    SetNetworkPositionMaskHookActive(process, false);
    std::array<std::uint8_t, kNetworkPositionMaskHookPatchSize> current{};
    const bool restored = process.ReadMemory(
            g_network_position_mask_hook.hook,
            current.data(), current.size()) &&
        (current == g_network_position_mask_hook.original ||
         (current == g_network_position_mask_hook.patch &&
          process.WriteProtectedMemory(
              g_network_position_mask_hook.hook,
              g_network_position_mask_hook.original.data(),
              g_network_position_mask_hook.original.size())));

    bool executing = true;
    if (restored)
    {
        for (unsigned attempt = 0; attempt < 50; ++attempt)
        {
            if (!process.IsAnyThreadExecutingRange(
                    g_network_position_mask_hook.remote,
                    kNetworkPositionMaskRemoteSize, executing) ||
                !executing)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    if (restored && !executing)
        (void)process.FreeRemoteMemory(g_network_position_mask_hook.remote);

    LogDiagnostic(
        "Network position mask: hook removed restored=%d idle=%d.",
        restored ? 1 : 0, !executing ? 1 : 0);
    g_network_position_mask_hook = {};
}

bool EnsureNetworkPositionMaskHook(TrainerProcess& process)
{
    if (g_network_position_mask_hook.applied && process.IsConnected() &&
        g_network_position_mask_hook.process_id == process.ProcessId())
    {
        return true;
    }
    if (g_network_position_mask_hook.applied)
        RemoveNetworkPositionMaskHook(process);
    if (!process.IsConnected())
        return false;

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        module.image_size <=
            kHumanPositionSendRva + kNetworkPositionMaskHookPatchSize)
    {
        return false;
    }

    NetworkPositionMaskHookState next{};
    next.process_id = process.ProcessId();
    next.hook = module.base_address + kHumanPositionSendRva;
    constexpr std::array<std::uint8_t, kNetworkPositionMaskHookPatchSize>
        expected{
            0x8B, 0x4D, 0xF4,                         // mov ecx,[ebp-0Ch]
            0x8D, 0x83, 0x10, 0x02, 0x00, 0x00};      // lea eax,[ebx+210h]
    if (!process.ReadMemory(
            next.hook, next.original.data(), next.original.size()) ||
        next.original != expected)
    {
        LogDiagnostic(
            "Network position mask: send signature mismatch at %08X.",
            static_cast<unsigned>(next.hook));
        return false;
    }

    next.remote = process.AllocateRemoteMemory(kNetworkPositionMaskRemoteSize);
    if (!IsSanePointer(next.remote))
        return false;

    std::vector<std::uint8_t> code;
    code.reserve(kNetworkPositionMaskActiveOffset);
    const auto byte = [&](std::uint8_t value) { code.push_back(value); };
    const auto dword = [&](std::uint32_t value)
    {
        const auto* raw = reinterpret_cast<const std::uint8_t*>(&value);
        code.insert(code.end(), raw, raw + sizeof(value));
    };
    const auto absolute = [&](std::size_t offset)
    {
        dword(static_cast<std::uint32_t>(next.remote + offset));
    };
    const auto near_jump = [&](std::uint8_t condition)
    {
        byte(0x0F); byte(condition);
        const std::size_t displacement = code.size();
        dword(0);
        return displacement;
    };
    const auto patch_jump = [&](std::size_t displacement, std::size_t target)
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            target - (displacement + sizeof(std::int32_t)));
        std::memcpy(
            code.data() + displacement, &relative, sizeof(relative));
    };

    // This hook runs only at the native `NM_HUMAN_POS` construction site.
    // EBX is the C_human being ticked. EBP is still the game's stack frame,
    // so writing its `to` locals lets the original code serialize the anchor
    // while the true model position and all local physics stay untouched.
    byte(0x9C);                              // pushfd
    byte(0x60);                              // pushad
    byte(0x83); byte(0x3D); absolute(kNetworkPositionMaskActiveOffset);
    byte(0x01);                              // cmp [active],1
    const std::size_t inactive = near_jump(0x85); // jne done
    byte(0x8B); byte(0x0D); absolute(kNetworkPositionMaskCountOffset);
                                                  // mov ecx,[count]
    byte(0xBA); absolute(kNetworkPositionMaskEntriesOffset);
                                                  // mov edx,entries
    const std::size_t scan = code.size();
    byte(0x85); byte(0xC9);                  // test ecx,ecx
    const std::size_t no_match = near_jump(0x84); // je done
    byte(0x3B); byte(0x1A);                  // cmp ebx,[edx]
    const std::size_t matched = near_jump(0x84);  // je write_anchor
    byte(0x83); byte(0xC2); byte(0x10);      // add edx,16
    byte(0x49);                              // dec ecx
    byte(0xE9);
    const std::size_t repeat_scan = code.size();
    dword(0);
    const std::size_t write_anchor = code.size();
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        byte(0x8B); byte(0x42);
        byte(static_cast<std::uint8_t>(
            sizeof(std::uint32_t) + axis * sizeof(float)));
                                                  // mov eax,[edx+4/8/Ch]
        byte(0x89); byte(0x45);
        byte(static_cast<std::uint8_t>(0xF0U + axis * sizeof(float)));
                                                  // mov [ebp-10/-0c/-08],eax
    }
    const std::size_t done = code.size();
    patch_jump(inactive, done);
    patch_jump(no_match, done);
    patch_jump(matched, write_anchor);
    const std::int32_t scan_relative = static_cast<std::int32_t>(
        scan - (repeat_scan + sizeof(std::int32_t)));
    std::memcpy(
        code.data() + repeat_scan, &scan_relative, sizeof(scan_relative));
    byte(0x61);                              // popad
    byte(0x9D);                              // popfd
    code.insert(code.end(), expected.begin(), expected.end());
    byte(0xE9);
    const std::int32_t return_relative = static_cast<std::int32_t>(
        (next.hook + kNetworkPositionMaskHookPatchSize) -
        (next.remote + code.size() + sizeof(std::int32_t)));
    dword(static_cast<std::uint32_t>(return_relative));

    if (code.size() >= kNetworkPositionMaskActiveOffset)
    {
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }
    std::array<std::uint8_t, kNetworkPositionMaskRemoteSize> image{};
    std::copy(code.begin(), code.end(), image.begin());
    if (!process.WriteMemory(next.remote, image.data(), image.size()))
    {
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }

    next.patch.fill(0x90);
    next.patch[0] = 0xE9;
    const std::int32_t hook_relative = static_cast<std::int32_t>(
        next.remote - (next.hook + 5));
    std::memcpy(next.patch.data() + 1, &hook_relative, sizeof(hook_relative));
    if (!process.WriteProtectedMemory(
            next.hook, next.patch.data(), next.patch.size()))
    {
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }

    std::array<std::uint8_t, kNetworkPositionMaskHookPatchSize> verified{};
    if (!process.ReadMemory(next.hook, verified.data(), verified.size()) ||
        verified != next.patch)
    {
        (void)process.WriteProtectedMemory(
            next.hook, next.original.data(), next.original.size());
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }

    next.applied = true;
    g_network_position_mask_hook = next;
    LogDiagnostic(
        "Network position mask: send hook installed at %08X remote=%08X.",
        static_cast<unsigned>(next.hook), static_cast<unsigned>(next.remote));
    return true;
}

void UpdateNetworkPositionMask(
    TrainerProcess& process,
    const RadarSnapshot* snapshot,
    GameplaySettings& settings,
    const GameplayInput& input,
    GameplayStatus& status)
{
    // Checking the checkbox deliberately does not capture or publish an
    // anchor. W is the only action that starts masking. If a W arrives just
    // before the local player snapshot, retain the request until it can be
    // safely fulfilled instead of silently losing that first press.
    const bool has_current_context =
        g_network_position_mask.active &&
        g_network_position_mask.process_id == process.ProcessId() &&
        g_network_position_mask.scope == settings.network_position_mask_scope;
    // This is deliberately performed before the position-state branches, so
    // it also works when W is pressed immediately after entering a mission.
    // V96 : ce premier W ne coche plus la case « Invisible pour les ennemis ».
    // Elle restait cochee ensuite, avec la portee que l'utilisateur y avait
    // laissee - escouade entiere le cas echeant - alors que le masque ne
    // concerne que le soldat pilote. Le filtre de perception est desormais
    // demande directement par l'etat cache du masque, pour ce seul soldat et
    // seulement tant qu'il est cache; la case dediee n'est plus touchee.
    if (settings.network_position_mask_enabled &&
        input.publish_real_position_pressed &&
        !settings.network_position_mask_first_w_consumed)
    {
        settings.network_position_mask_first_w_consumed = true;
        LogDiagnostic(
            "Network position mask: first W armed; enemy perception filter "
            "follows the hidden state for the controlled soldier only "
            "(checkbox untouched=%u).",
            settings.enemy_invisibility_enabled ? 1U : 0U);
    }
    if (settings.network_position_mask_enabled &&
        input.publish_real_position_pressed)
    {
        // W reaches H&D normally. The first W captures an anchor and hides
        // the host model on the companion Client. Further W presses strictly
        // alternate showing the true position and capturing a new hidden
        // anchor.
        if (!has_current_context)
        {
            settings.network_position_mask_activation_requested = true;
            LogDiagnostic(
                "Network position mask: W requested the first hidden anchor.");
        }
        else if (g_network_position_mask.active &&
            !g_network_position_mask.real_position_published &&
            !g_network_position_mask.remote_visual_hidden)
        {
            g_network_position_mask.remote_visual_hidden = true;
            PublishPeerVisualCommand(true, true);
            status.network_position_mask =
                NetworkPositionMaskStatus::RemoteVisualHidden;
            LogDiagnostic(
                "Network position mask: W hid host player models on Client; "
                "anchor remains published.");
            return;
        }
        else if (g_network_position_mask.active &&
            !g_network_position_mask.real_position_published &&
            g_network_position_mask.remote_visual_hidden)
        {
            SetNetworkPositionMaskHookActive(process, false);
            g_network_position_mask.remote_visual_hidden = false;
            PublishPeerVisualCommand(false, true);
            g_network_position_mask.real_position_published = true;
            status.network_position_mask =
                NetworkPositionMaskStatus::RealPosition;
            LogDiagnostic(
                "Network position mask: W restored Client rendering and "
                "published real position.");
            return;
        }
        else if (g_network_position_mask.active &&
            g_network_position_mask.real_position_published)
        {
            StopNetworkPositionMask(process);
            settings.network_position_mask_activation_requested = true;
            LogDiagnostic(
                "Network position mask: W requested a fresh hidden anchor.");
        }
    }
    if (!settings.network_position_mask_enabled)
    {
        // A future explicit re-check starts a fresh mask session. Do not touch
        // enemy_invisibility_enabled here: it remains the user's own setting.
        settings.network_position_mask_first_w_consumed = false;
        settings.network_position_mask_activation_requested = false;
        StopNetworkPositionMask(process);
        status.network_position_mask = NetworkPositionMaskStatus::Disabled;
        return;
    }
    if (!has_current_context &&
        !settings.network_position_mask_activation_requested)
    {
        // Armed only: leave the native network position and the Client model
        // completely untouched until the user presses W.
        StopNetworkPositionMask(process);
        status.network_position_mask = NetworkPositionMaskStatus::Armed;
        return;
    }
    if (g_network_position_mask.active &&
        g_network_position_mask.real_position_published)
    {
        SetNetworkPositionMaskHookActive(process, false);
        PublishPeerVisualCommand(false);
        status.network_position_mask = NetworkPositionMaskStatus::RealPosition;
        return;
    }
    if (!process.IsConnected() || !snapshot ||
        !IsSanePointer(snapshot->player_object_address))
    {
        SetNetworkPositionMaskHookActive(process, false);
        if (g_network_position_mask.remote_visual_hidden)
            PublishPeerVisualCommand(false, true);
        g_network_position_mask = {};
        status.network_position_mask = NetworkPositionMaskStatus::WaitingForMission;
        return;
    }
    if (!EnsureNetworkPositionMaskHook(process))
    {
        settings.network_position_mask_enabled = false;
        StopNetworkPositionMask(process);
        status.network_position_mask =
            NetworkPositionMaskStatus::UnsupportedRevision;
        return;
    }

    struct Candidate
    {
        std::uintptr_t actor = 0;
        Vector3 position{};
    };
    std::array<Candidate, kNetworkPositionMaskMaximumActors> candidates{};
    std::size_t candidate_count = 0;
    const auto add_candidate = [&](std::uintptr_t actor, const Vector3& position)
    {
        if (!IsSanePointer(actor) || !IsSaneWorldPosition(position) ||
            candidate_count >= candidates.size())
        {
            return;
        }
        for (std::size_t index = 0; index < candidate_count; ++index)
        {
            if (candidates[index].actor == actor)
                return;
        }
        candidates[candidate_count++] = {actor, position};
    };

    // The selected player belongs to this PC. In squad mode we additionally
    // select allies with network_actor == 0. A connected friend's player has
    // that friend's PID here, so it is never included in this list.
    add_candidate(snapshot->player_object_address, snapshot->player.position);
    if (settings.network_position_mask_scope ==
        NetworkPositionMaskScope::WholeSquad)
    {
        for (std::size_t index = 0;
             index < snapshot->entity_array.count;
             ++index)
        {
            const RadarEntity& entity = snapshot->entity_array.entities[index];
            if (entity.team != EntityTeam::Ally)
                continue;
            std::uint32_t network_owner = UINT32_MAX;
            if (!process.ReadMemory(
                    entity.actor_address + 0x34, network_owner) ||
                network_owner != 0)
            {
                continue;
            }
            add_candidate(entity.actor_address, entity.position);
        }
    }
    if (candidate_count == 0)
    {
        SetNetworkPositionMaskHookActive(process, false);
        if (g_network_position_mask.remote_visual_hidden)
            PublishPeerVisualCommand(false, true);
        g_network_position_mask = {};
        status.network_position_mask = NetworkPositionMaskStatus::WaitingForMission;
        return;
    }

    const bool new_context = !g_network_position_mask.active ||
        g_network_position_mask.process_id != process.ProcessId() ||
        g_network_position_mask.scope != settings.network_position_mask_scope;
    if (new_context)
    {
        SetNetworkPositionMaskHookActive(process, false);
        if (g_network_position_mask.remote_visual_hidden)
            PublishPeerVisualCommand(false, true);
        g_network_position_mask = {};
        g_network_position_mask.process_id = process.ProcessId();
        g_network_position_mask.scope = settings.network_position_mask_scope;
        g_network_position_mask.active = true;
    }

    if (settings.network_position_mask_activation_requested)
        g_network_position_mask.remote_visual_hidden = true;

    std::array<NetworkPositionMaskState::Entry,
        kNetworkPositionMaskMaximumActors> next_entries{};
    for (std::size_t index = 0; index < candidate_count; ++index)
    {
        const Candidate& candidate = candidates[index];
        Vector3 anchor = candidate.position;
        if (!new_context)
        {
            for (std::size_t old = 0;
                 old < g_network_position_mask.count;
                 ++old)
            {
                if (g_network_position_mask.entries[old].actor ==
                    candidate.actor)
                {
                    anchor = g_network_position_mask.entries[old].anchor;
                    break;
                }
            }
        }
        next_entries[index] = {candidate.actor, anchor};
    }
    g_network_position_mask.entries = next_entries;
    g_network_position_mask.count = candidate_count;
    g_network_position_mask.controlled_network_id = ReadActorNetworkId(
        process, snapshot->player_object_address);

    std::array<NetworkPositionMaskWireEntry,
        kNetworkPositionMaskMaximumActors> wire_entries{};
    for (std::size_t index = 0; index < g_network_position_mask.count; ++index)
    {
        wire_entries[index].actor = static_cast<std::uint32_t>(
            g_network_position_mask.entries[index].actor);
        wire_entries[index].anchor =
            g_network_position_mask.entries[index].anchor;
    }
    const std::uint32_t count32 = static_cast<std::uint32_t>(
        g_network_position_mask.count);
    const bool published =
        process.WriteMemory(
            g_network_position_mask_hook.remote +
                kNetworkPositionMaskCountOffset,
            count32) &&
        process.WriteMemory(
            g_network_position_mask_hook.remote +
                kNetworkPositionMaskEntriesOffset,
            wire_entries.data(),
            wire_entries.size() * sizeof(wire_entries.front()));
    if (!published)
    {
        settings.network_position_mask_enabled = false;
        StopNetworkPositionMask(process);
        status.network_position_mask = NetworkPositionMaskStatus::WriteFailed;
        LogDiagnostic("Network position mask: state publish failed.");
        return;
    }

    SetNetworkPositionMaskHookActive(process, true);
    // The W request has now produced a valid anchored send state. Later W
    // presses use the live state above; this latch is only for arming.
    settings.network_position_mask_activation_requested = false;
    if (g_network_position_mask.remote_visual_hidden)
    {
        PublishPeerVisualCommand(true);
        status.network_position_mask =
            NetworkPositionMaskStatus::RemoteVisualHidden;
    }
    else
    {
        PublishPeerVisualCommand(false);
        status.network_position_mask = NetworkPositionMaskStatus::Active;
    }
    if (new_context)
    {
        LogDiagnostic(
            "Network position mask: scope=%s masked_players=%u.",
            settings.network_position_mask_scope ==
                    NetworkPositionMaskScope::WholeSquad
                ? "squad" : "current",
            static_cast<unsigned>(g_network_position_mask.count));
    }
}


// =====================================================================
// V154 - MAIN LIBRE : PILOTER UN ALLIE COMME ON SE PILOTE SOI-MEME
// =====================================================================
//
// Le joueur : « ce que je veux c'est comme le noclip qui existe deja, meme si
// je ne controle pas ce soldat mais je peux le deplacer dans la map comme je
// veux n'importe ou ».
//
// C'est possible sans rien inventer, et voici pourquoi. Le crochet du noclip
// n'est pas pose sur le joueur : il est pose sur `C_human::Tick`, que TOUS les
// humains executent - vos soldats comme les ennemis. Et le trampoline compare
// l'acteur en cours a celui qu'on lui publie dans sa page :
//
//   3B 0D <adresse>    cmp ecx,[acteur publie]
//
// Il pilote donc l'acteur qu'on lui DESIGNE. `UpdateNoclip` lui publiait
// simplement `snapshot->player_object_address` en dur.
//
// Il suffit donc de lui publier un autre humain. Toute la machinerie - la
// vitesse, la direction prise sur votre camera, le relien au secteur, l'arret
// propre - est celle qui vole deja sous vos ordres depuis des versions.
//
// A noter pour le reseau : un allie rallie est un acteur de type ennemi, et
// les positions des ennemis sont publiees par l'hote. Le deplacement devrait
// donc parvenir a vos amis par le chemin normal du jeu, sans que le trainer
// ait rien a envoyer.
std::uintptr_t g_free_move_actor = 0;


// =====================================================================
// V155 - LA CAMERA SUR L'HOMME QU'ON PILOTE
// =====================================================================
//
// Le joueur : « pourquoi ne pas mettre la camera exactement sur lui ? »
//
// Le moteur sait le faire, et par un seul message. `C_human::cbProc` traite
// `CB_SETFOCUS` ainsi (Actors.cpp:11761) :
//
//   case CB_SETFOCUS:
//      switch(prm1){
//      case 0: mission.game_cam.SetAimModel(NULL, NULL); on = true; break;
//      case 1: mission.game_cam.SetFocus(frm_head ? frm_head : frame,
//                                        frame, this);
//              mission.game_cam.SetDeltaDir(aim_dir);
//              on = mission.game_cam.GetDistanceMode()!=0;
//              break;
//      default: return true;
//      }
//
// DEUX CHOSES ONT DU ETRE LUES DANS LE BINAIRE, ET NON SUPPOSEES.
//
// 1. LE NOMBRE D'ARGUMENTS. L'en-tete de 2002 declare trois parametres, et le
//    nom decore le confirme : `?cbProc@C_actor@@UAEKKKK@Z`, `ret 12`. Mais sur
//    Deluxe les trois `cbProc` finissent par `ret 16` : QUATRE arguments. La
//    signature a change entre les versions. C'est exactement le piege qui a
//    coute huit versions en V151, et il etait tendu une seconde fois.
//
// 2. LE NUMERO DU MESSAGE. `CB_SETFOCUS` vaut 21 dans l'enumeration de 2002.
//    Sur Deluxe, la table d'aiguillage de `C_human::cbProc` couvre les
//    messages 117 a 180 : la numerotation est ENTIEREMENT differente. Deviner
//    aurait declenche n'importe lequel des soixante-quatre cas.
//
// 2bis. LE NUMERO, ETABLI PAR L'APPELANT ET NON PAR UN CALCUL.
//
//    La V155 a envoye 137, et le journal du joueur a montre `execute=1` sans
//    que la camera bouge : le message tombait hors des bornes de
//    l'aiguillage, qui rendait simplement sa valeur par defaut.
//
//    L'erreur etait dans MA lecture du prologue. `C_human::cbProc` commence
//    par :
//
//      8b 45 08   mov eax,[ebp+8]      = msg
//      8b 5d 10   mov ebx,[ebp+0x10]   = prm2
//      48         dec eax              <- l'index vaut msg - 1
//      8b 7d 0c   mov edi,[ebp+0x0C]   = prm1
//      83 f8 3f   cmp eax,0x3F         = 63, donc messages 1 a 64
//
//    Mon script avait pris un `add eax,imm` situe plus loin dans la fenetre
//    au lieu de ce `dec eax`, d'ou un decalage de 116.
//
//    Le numero est desormais etabli par une preuve et non par un calcul :
//    `C_game_camera::SetFocus` est a 0045DEB0 sur le binaire du joueur - son
//    prologue exact, repris du binaire de 2002, n'y apparait qu'UNE fois - et
//    l'un de ses cinq appelants, 0041F2B8, tombe A L'INTERIEUR du cas dont la
//    table d'aiguillage dit qu'il porte le message 21.
//
//    C'est donc bien la valeur de 2002 : l'enumeration n'avait pas bouge, et
//    seule ma lecture etait fausse. `CB_IS_FOCUSED` suit a 22, comme attendu.
constexpr std::uintptr_t kActorCbProcVtableOffset = 0x04;
constexpr std::uint32_t kCbSetFocusDeluxe = 21;

// Pose la camera du jeu sur cet acteur, sur le thread du jeu.
bool FocusCameraOnActor(TrainerProcess& process, std::uintptr_t actor)
{
    if (!IsSanePointer(actor) || !process.IsConnected())
        return false;
    constexpr std::uintptr_t kProcessCheatHookRva = 0x0009'FC10;
    constexpr std::array<std::uint8_t, 5> kExpected{
        0xB8, 0x5C, 0x10, 0x00, 0x00};
    constexpr std::size_t kRemoteSize = 0x400;
    constexpr std::size_t kActorOffset = 0x200;
    constexpr std::size_t kCompletionOffset = 0x204;

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        kProcessCheatHookRva >= module.image_size)
    {
        return false;
    }
    // On ne saute jamais dans une methode sans l'avoir verifiee.
    std::uintptr_t vtable = 0;
    std::uintptr_t method = 0;
    if (!process.ReadMemory(actor, vtable) || !IsSanePointer(vtable) ||
        !process.ReadMemory(vtable + kActorCbProcVtableOffset, method) ||
        method < module.base_address ||
        method >= module.base_address + module.image_size)
    {
        LogDiagnostic(
            "Camera: l'acteur %08X ne presente pas de methode valide au rang "
            "+0x%02X; la camera n'est pas deplacee.",
            static_cast<unsigned>(actor),
            static_cast<unsigned>(kActorCbProcVtableOffset));
        return false;
    }
    const std::uintptr_t hook = module.base_address + kProcessCheatHookRva;
    std::array<std::uint8_t, 5> original{};
    if (!process.ReadMemory(hook, original.data(), original.size()) ||
        original != kExpected)
    {
        return false;
    }
    const std::uintptr_t remote = process.AllocateRemoteMemory(kRemoteSize);
    if (!IsSanePointer(remote))
        return false;

    std::vector<std::uint8_t> code;
    const auto byte = [&](std::uint8_t v) { code.push_back(v); };
    const auto dword = [&](std::uint32_t v)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(v));
        std::memcpy(code.data() + offset, &v, sizeof(v));
    };
    const auto slot = [&](std::size_t offset)
    {
        dword(static_cast<std::uint32_t>(remote + offset));
    };

    byte(0x9C);                                   // pushfd
    byte(0x60);                                   // pushad
    byte(0x83); byte(0x3D); slot(kCompletionOffset); byte(0x00);
    const std::size_t skip_all = code.size();
    byte(0x75); byte(0x00);                       // jne fin
    byte(0x8B); byte(0x1D); slot(kActorOffset);   // mov ebx,[acteur]
    byte(0x85); byte(0xDB);                       // test ebx,ebx
    const std::size_t skip_call = code.size();
    byte(0x74); byte(0x00);                       // je fin
    // cbProc(msg, prm1, prm2, prm3) : QUATRE arguments, empiles de droite a
    // gauche, liberes par l'appelee (`ret 16`).
    byte(0x6A); byte(0x00);                       // push prm3 = 0
    byte(0x6A); byte(0x00);                       // push prm2 = 0
    byte(0x6A); byte(0x01);                       // push prm1 = 1  (focus on)
    byte(0x68); dword(kCbSetFocusDeluxe);         // push CB_SETFOCUS
    byte(0x8B); byte(0xCB);                       // mov ecx,acteur
    byte(0x8B); byte(0x03);                       // mov eax,[acteur]
    byte(0xFF); byte(0x50);
    byte(static_cast<std::uint8_t>(kActorCbProcVtableOffset));
    const std::size_t fin = code.size();
    code[skip_all + 1] =
        static_cast<std::uint8_t>(fin - (skip_all + 2));
    code[skip_call + 1] =
        static_cast<std::uint8_t>(fin - (skip_call + 2));
    byte(0xC7); byte(0x05); slot(kCompletionOffset); dword(1);
    byte(0x61);                                   // popad
    byte(0x9D);                                   // popfd
    code.insert(code.end(), original.begin(), original.end());
    byte(0xE9);
    dword(static_cast<std::uint32_t>(
        (hook + original.size()) -
        (remote + code.size() + sizeof(std::int32_t))));

    const std::uint32_t zero = 0;
    if (code.size() >= kActorOffset ||
        !process.WriteMemory(remote, code.data(), code.size()) ||
        !process.WriteMemory(
            remote + kActorOffset, static_cast<std::uint32_t>(actor)) ||
        !process.WriteMemory(remote + kCompletionOffset, zero))
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }
    std::array<std::uint8_t, 5> patch{0xE9, 0, 0, 0, 0};
    const std::int32_t relative =
        static_cast<std::int32_t>(remote - (hook + patch.size()));
    std::memcpy(patch.data() + 1, &relative, sizeof(relative));
    if (!InstallHookSafely(
            process, hook, patch.data(), patch.size(),
            "le saut vers le code pose"))
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }
    const bool triggered = SendInventoryMainThreadTrigger(process);
    std::uint32_t completed = 0;
    if (triggered)
    {
        for (int attempt = 0; attempt < 500 && completed == 0; ++attempt)
        {
            (void)process.ReadMemory(remote + kCompletionOffset, completed);
            if (completed == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    RestoreHookSafely(process, hook, original.data(), original.size());
    QueueRemotePageRelease(process, remote, kRemoteSize);
    LogDiagnostic(
        "Camera: focus demande sur l'acteur %08X (message %u, quatre "
        "arguments) declenche=%u execute=%u.",
        static_cast<unsigned>(actor), kCbSetFocusDeluxe,
        triggered ? 1U : 0U, completed);
    return completed != 0;
}

void ClearFreeMove(const char* why)
{
    if (g_free_move_actor != 0)
    {
        LogDiagnostic(
            "Main libre: le pilotage de l'allie %08X est rendu (%s). Les "
            "touches de vol reviennent a votre soldat.",
            static_cast<unsigned>(g_free_move_actor), why);
    }
    g_free_move_actor = 0;
}


void UpdateNoclip(
    TrainerProcess& process,
    const RadarSnapshot* snapshot,
    GameplaySettings& settings,
    const GameplayInput& input,
    bool vehicle_context,
    std::uintptr_t controlled_vehicle,
    GameplayStatus& status)
{
    if (!settings.noclip_enabled)
    {
        settings.noclip_active = false;
        if (g_noclip.active)
            LogDiagnostic("Noclip: disabled; native locomotion restored.");
        StopNoclipMovement(process);
        status.noclip = NoclipStatus::Disabled;
        return;
    }
    if (!input.noclip_input_capture_available)
    {
        settings.noclip_enabled = false;
        settings.noclip_active = false;
        StopNoclipMovement(process);
        status.noclip = NoclipStatus::InputCaptureUnavailable;
        LogDiagnostic(
            "Noclip: activation refused because the selective keyboard "
            "capture hook is unavailable.");
        return;
    }
    if (!settings.noclip_active)
    {
        StopNoclipMovement(process);
        status.noclip = NoclipStatus::Armed;
        return;
    }
    if (!process.IsConnected() || !snapshot ||
        !IsSanePointer(snapshot->player_object_address) ||
        !IsSanePointer(snapshot->camera_object_address))
    {
        StopNoclipMovement(process);
        status.noclip = NoclipStatus::WaitingForMission;
        return;
    }

    // V154 - MAIN LIBRE : si un allie est designe, c'est LUI qu'on pilote.
    //
    // Le crochet est pose sur `C_human::Tick`, que tous les humains executent,
    // et le trampoline compare l'acteur en cours a celui qu'on publie. Publier
    // un allie suffit donc a le faire voler a la place du joueur.
    std::uintptr_t actor = snapshot->player_object_address;
    if (g_free_move_actor != 0)
    {
        // On ne pilote jamais un acteur sans l'avoir revalide - meme regle que
        // partout ailleurs depuis la V147.
        RemoteModuleInfo free_module{};
        std::uintptr_t vtable = 0;
        std::uint32_t type = 0;
        std::int32_t resistance = 0;
        const bool usable =
            process.GetMainModuleInfo(free_module) &&
            process.ReadMemory(g_free_move_actor, vtable) &&
            vtable >= free_module.base_address &&
            vtable < free_module.base_address + free_module.image_size &&
            process.ReadMemory(
                g_free_move_actor + kActorTypeOffset, type) &&
            (type == kActorTypeEnemy || type == kActorTypePlayer) &&
            process.ReadMemory(
                g_free_move_actor + kPlayerResistanceOffset, resistance) &&
            resistance > 0;
        if (usable)
            actor = g_free_move_actor;
        else
            ClearFreeMove("il est mort ou a disparu");
    }
    // The hook always triggers on the player's C_human::Tick, because that is
    // the function it patches and the player keeps ticking while seated. What
    // changes when driving is only the frame it moves.
    const bool drive_vehicle =
        vehicle_context && IsSanePointer(controlled_vehicle);
    std::uintptr_t body_frame = 0;
    if (drive_vehicle)
    {
        std::uintptr_t vehicle_frame_actor = 0;
        if (!process.ReadMemory(
                controlled_vehicle + kActorModelOffset, body_frame) ||
            !IsSanePointer(body_frame) ||
            !process.ReadMemory(
                body_frame + kFrameActorBackReferenceOffset,
                vehicle_frame_actor) ||
            vehicle_frame_actor != controlled_vehicle)
        {
            StopNoclipMovement(process);
            status.noclip = NoclipStatus::SuspendedInVehicle;
            return;
        }
    }
    std::uintptr_t frame = 0;
    std::uintptr_t frame_actor = 0;
    Vector3 world{};
    Vector3 local{};
    Vector3 camera_forward{};
    float fall_speed = 0.0f;
    std::uint8_t falling = 0;
    std::int32_t collision_countdown = 0;
    if (!process.ReadMemory(actor + kActorFrameOffset, frame) ||
        !IsSanePointer(frame) ||
        !process.ReadMemory(
            frame + kFrameActorBackReferenceOffset, frame_actor) ||
        frame_actor != actor ||
        !process.ReadMemory(frame + kFrameWorldPositionOffset, world) ||
        !process.ReadMemory(frame + kFrameLocalPositionOffset, local) ||
        !process.ReadMemory(
            snapshot->camera_object_address + 0xAC, camera_forward) ||
        !process.ReadMemory(actor + kActorFallSpeedOffset, fall_speed) ||
        !process.ReadMemory(actor + kActorFallingOffset, falling) ||
        !process.ReadMemory(
            actor + kActorCollisionTestCountdownOffset,
            collision_countdown) ||
        !IsSaneWorldPosition(world) || !IsSaneWorldPosition(local) ||
        !std::isfinite(fall_speed) || std::fabs(fall_speed) > 200.0f ||
        falling > 1 || collision_countdown < -60'000 ||
        collision_countdown > 60'000)
    {
        StopNoclipMovement(process);
        status.noclip = NoclipStatus::UnsupportedLayout;
        return;
    }

    if (!drive_vehicle)
        body_frame = frame;

    if (!EnsureNoclipHook(process))
    {
        settings.noclip_enabled = false;
        settings.noclip_active = false;
        StopNoclipMovement(process);
        status.noclip = NoclipStatus::UnsupportedLayout;
        LogDiagnostic("Noclip: native tick hook unavailable; activation refused.");
        return;
    }
    // Vehicles use a separate tick routine. This validated message path is
    // for a human flying on foot; it does not alter vehicle noclip behaviour.
    const bool network_publish_ready = drive_vehicle ||
        EnsureNoclipNetworkPublishHook(process);
    if (!network_publish_ready)
    {
        LogDiagnostic(
            "Noclip: on-foot network publishing unavailable; local noclip "
            "continues, but the other PC will not receive flight positions.");
    }
    // A car never ticks as a human, so while driving the movement has to come
    // from C_automobile::Tick instead. Both trampolines share the data page
    // allocated above, which is why it must exist first.
    if (drive_vehicle &&
        !EnsureVehicleNoclipHook(process, g_noclip_hook.remote))
    {
        StopNoclipMovement(process);
        status.noclip = NoclipStatus::SuspendedInVehicle;
        LogDiagnostic(
            "Noclip: C_automobile::Tick hook unavailable; vehicle flight "
            "refused.");
        return;
    }

    const ULONGLONG now = GetTickCount64();
    // Entering or leaving a vehicle changes the moved frame, so it has to
    // restart the run from the new body's own position.
    const bool context_changed = !g_noclip.active ||
        g_noclip.process_id != process.ProcessId() ||
        g_noclip.actor != actor || g_noclip.frame != body_frame ||
        g_noclip.vehicle != (drive_vehicle ? controlled_vehicle : 0U);
    if (context_changed)
    {
        StopNoclipMovement(process);
        g_noclip.process_id = process.ProcessId();
        g_noclip.actor = actor;
        g_noclip.frame = body_frame;
        g_noclip.vehicle = drive_vehicle ? controlled_vehicle : 0U;
        Vector3 body_world = world;
        if (drive_vehicle &&
            (!process.ReadMemory(
                 body_frame + kFrameWorldPositionOffset, body_world) ||
             !IsSaneWorldPosition(body_world)))
        {
            StopNoclipMovement(process);
            status.noclip = NoclipStatus::SuspendedInVehicle;
            return;
        }
        g_noclip.position = body_world;
        g_noclip.initial_fall_speed = fall_speed;
        g_noclip.initial_collision_countdown = collision_countdown;
        g_noclip.initial_falling = falling;
        g_noclip.last_log_tick = 0;
        g_noclip.active = true;
        LogDiagnostic(
            "Noclip: activated target=%s actor=%08X vehicle=%08X frame=%08X "
            "position=(%.3f,%.3f,%.3f) speed=%.2f m/s "
            "physics=(fall=%.3f falling=%u collision_countdown=%d).",
            drive_vehicle ? "vehicle" : "player",
            static_cast<unsigned>(actor),
            static_cast<unsigned>(drive_vehicle ? controlled_vehicle : 0U),
            static_cast<unsigned>(body_frame),
            g_noclip.position.x, g_noclip.position.y, g_noclip.position.z,
            settings.noclip_speed_mps,
            fall_speed, static_cast<unsigned>(falling), collision_countdown);
    }

    camera_forward.y = 0.0f;
    const float forward_length = std::sqrt(
        camera_forward.x * camera_forward.x +
        camera_forward.z * camera_forward.z);
    if (!std::isfinite(forward_length) || forward_length < 0.0001f)
    {
        status.noclip = NoclipStatus::UnsupportedLayout;
        return;
    }
    camera_forward.x /= forward_length;
    camera_forward.z /= forward_length;
    const Vector3 camera_right{
        camera_forward.z, 0.0f, -camera_forward.x};
    const float forward_axis =
        static_cast<float>(input.noclip_forward_down) -
        static_cast<float>(input.noclip_backward_down);
    const float right_axis =
        static_cast<float>(input.noclip_right_down) -
        static_cast<float>(input.noclip_left_down);
    const float vertical_axis =
        static_cast<float>(input.noclip_up_down) -
        static_cast<float>(input.noclip_down_down);
    Vector3 direction{
        camera_forward.x * forward_axis + camera_right.x * right_axis,
        vertical_axis,
        camera_forward.z * forward_axis + camera_right.z * right_axis};
    const float direction_length = std::sqrt(
        direction.x * direction.x + direction.y * direction.y +
        direction.z * direction.z);
    if (direction_length > 1.0f)
    {
        direction.x /= direction_length;
        direction.y /= direction_length;
        direction.z /= direction_length;
    }
    const Vector3 velocity{
        direction.x * settings.noclip_speed_mps,
        direction.y * settings.noclip_speed_mps,
        direction.z * settings.noclip_speed_mps};
    if (context_changed)
        SetNoclipHookActive(process, false);
    const bool actor_written = !context_changed || process.WriteMemory(
        g_noclip_hook.remote + kNoclipRemoteActorOffset, actor);
    const std::uint32_t cleared_ticks = 0;
    const std::uint32_t published_vehicle = static_cast<std::uint32_t>(
        drive_vehicle ? controlled_vehicle : 0U);
    // Mission scene, read the same way the teleport reads it. If it cannot be
    // resolved the trampoline simply skips the sector re-link.
    std::uintptr_t mission_scene = 0;
    if (!drive_vehicle ||
        !process.ReadMemory(
            snapshot->entity_list_object_address + 0x10, mission_scene) ||
        !IsSanePointer(mission_scene))
    {
        mission_scene = 0;
    }
    const bool body_written =
        process.WriteMemory(
            g_noclip_hook.remote + kNoclipRemoteBodyFrameOffset, body_frame) &&
        process.WriteMemory(
            g_noclip_hook.remote + kNoclipRemoteVehicleOffset,
            published_vehicle) &&
        process.WriteMemory(
            g_noclip_hook.remote + kNoclipRemoteSceneOffset,
            static_cast<std::uint32_t>(mission_scene)) &&
        (!context_changed || (process.WriteMemory(
            g_noclip_hook.remote + kNoclipRemoteTickCountOffset,
            cleared_ticks) && process.WriteMemory(
            g_noclip_hook.remote + kNoclipRemoteVehiclePassOffset,
            cleared_ticks) && process.WriteMemory(
            g_noclip_hook.remote + kNoclipRemotePublishCountOffset,
            cleared_ticks)));
    const bool position_initialized = !context_changed || process.WriteMemory(
        g_noclip_hook.remote + kNoclipRemotePositionOffset,
        g_noclip.position);
    const bool velocity_written = process.WriteMemory(
        g_noclip_hook.remote + kNoclipRemoteVelocityOffset, velocity);
    const std::uint32_t active = 1;
    const bool active_written = process.WriteMemory(
        g_noclip_hook.remote + kNoclipRemoteActiveOffset, active);
    if (!actor_written || !body_written || !position_initialized ||
        !velocity_written || !active_written)
    {
        settings.noclip_enabled = false;
        settings.noclip_active = false;
        StopNoclipMovement(process);
        status.noclip = NoclipStatus::WriteFailed;
        LogDiagnostic(
            "Noclip: native command write failed "
            "(actor=%d body=%d position=%d velocity=%d active=%d).",
            actor_written ? 1 : 0,
            body_written ? 1 : 0,
            position_initialized ? 1 : 0,
            velocity_written ? 1 : 0,
            active_written ? 1 : 0);
        return;
    }

    Vector3 native_position{};
    if (process.ReadMemory(
            g_noclip_hook.remote + kNoclipRemotePositionOffset,
            native_position) && IsSaneWorldPosition(native_position))
    {
        g_noclip.position = native_position;
    }

    if (direction_length > 0.0f &&
        (g_noclip.last_log_tick == 0 || now - g_noclip.last_log_tick >= 500))
    {
        g_noclip.last_log_tick = now;
        std::uint32_t hook_ticks = 0;
        std::uint32_t vehicle_ticks = 0;
        std::uint32_t publish_ticks = 0;
        (void)process.ReadMemory(
            g_noclip_hook.remote + kNoclipRemoteTickCountOffset, hook_ticks);
        (void)process.ReadMemory(
            g_noclip_hook.remote + kNoclipRemoteVehiclePassOffset,
            vehicle_ticks);
        (void)process.ReadMemory(
            g_noclip_hook.remote + kNoclipRemotePublishCountOffset,
            publish_ticks);
        LogDiagnostic(
            "Noclip: move target=%s position=(%.3f,%.3f,%.3f) "
            "velocity=(%.3f,%.3f,%.3f) speed=%.2f m/s "
            "human_passes=%u vehicle_passes=%u network_publish=%u.",
            drive_vehicle ? "vehicle" : "player",
            g_noclip.position.x, g_noclip.position.y, g_noclip.position.z,
            velocity.x, velocity.y, velocity.z,
            settings.noclip_speed_mps, hook_ticks, vehicle_ticks,
            publish_ticks);
    }
    status.noclip = network_publish_ready
        ? NoclipStatus::Active
        : NoclipStatus::NetworkPublishUnavailable;
}

struct VehicleInvulnerabilityState
{
    DWORD process_id = 0;
    std::uintptr_t vehicle = 0;
    std::int32_t original_resistance = 0;
    // Last mode seen while the car was still intact, put back if anything
    // marks it destroyed.
    std::uint32_t healthy_mode = kAutomobileModeStopped;
    bool applied = false;
};

VehicleInvulnerabilityState g_vehicle_invulnerability{};

struct VehicleDestroyHookState
{
    DWORD process_id = 0;
    std::uintptr_t hook = 0;
    std::uintptr_t remote = 0;
    std::array<std::uint8_t, kAutomobileDestroyPatchSize> original{};
    std::array<std::uint8_t, kAutomobileDestroyPatchSize> patch{};
    bool applied = false;
};

VehicleDestroyHookState g_vehicle_destroy_hook{};

void RemoveVehicleDestroyHook(TrainerProcess& process)
{
    if (!g_vehicle_destroy_hook.applied || !process.IsConnected() ||
        process.ProcessId() != g_vehicle_destroy_hook.process_id)
    {
        g_vehicle_destroy_hook = {};
        return;
    }
    // Stop matching first: from here the trampoline is inert whatever happens.
    const std::uint32_t none = 0;
    (void)process.WriteMemory(
        g_vehicle_destroy_hook.remote + kVehicleDestroyVehicleOffset, none);
    std::array<std::uint8_t, kAutomobileDestroyPatchSize> current{};
    const bool restored =
        process.ReadMemory(
            g_vehicle_destroy_hook.hook, current.data(), current.size()) &&
        (current == g_vehicle_destroy_hook.original ||
         (current == g_vehicle_destroy_hook.patch &&
          process.WriteProtectedMemory(
              g_vehicle_destroy_hook.hook,
              g_vehicle_destroy_hook.original.data(),
              g_vehicle_destroy_hook.original.size())));
    bool executing = true;
    if (restored)
    {
        for (unsigned attempt = 0; attempt < 50; ++attempt)
        {
            if (process.IsAnyThreadExecutingRange(
                    g_vehicle_destroy_hook.remote,
                    kVehicleDestroyRemoteSize, executing) && !executing)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    LogDiagnostic(
        "Vehicle invulnerability: Destroy hook %s, page %s.",
        restored ? "restored" : "restoration unverified",
        (restored && !executing) ? "freed" : "kept");
    if (restored && !executing)
        (void)process.FreeRemoteMemory(g_vehicle_destroy_hook.remote);
    g_vehicle_destroy_hook = {};
}

bool EnsureVehicleDestroyHook(
    TrainerProcess& process, std::uintptr_t vehicle)
{
    if (g_vehicle_destroy_hook.applied &&
        g_vehicle_destroy_hook.process_id != process.ProcessId())
    {
        RemoveVehicleDestroyHook(process);
    }
    if (g_vehicle_destroy_hook.applied)
    {
        return process.WriteMemory(
            g_vehicle_destroy_hook.remote + kVehicleDestroyVehicleOffset,
            static_cast<std::uint32_t>(vehicle));
    }

    RemoteModuleInfo module{};
    constexpr std::array<std::uint8_t, 12> expected{
        0x51, 0x53, 0x55, 0x56, 0x8B, 0xF1,
        0x57, 0x33, 0xFF, 0x8B, 0x46, 0x24};
    if (!process.GetMainModuleInfo(module) ||
        module.image_size <= kAutomobileDestroyRva + expected.size())
    {
        return false;
    }
    VehicleDestroyHookState next{};
    next.process_id = process.ProcessId();
    next.hook = module.base_address + kAutomobileDestroyRva;
    std::array<std::uint8_t, expected.size()> signature{};
    if (!process.ReadMemory(next.hook, signature.data(), signature.size()) ||
        signature != expected)
    {
        LogDiagnostic(
            "Vehicle invulnerability: C_automobil::Destroy signature mismatch "
            "at %08X.",
            static_cast<unsigned>(next.hook));
        return false;
    }
    std::copy(
        expected.begin(), expected.begin() + kAutomobileDestroyPatchSize,
        next.original.begin());

    next.remote = process.AllocateRemoteMemory(kVehicleDestroyRemoteSize);
    if (!IsSanePointer(next.remote))
        return false;

    std::vector<std::uint8_t> code;
    const auto byte = [&](std::uint8_t value) { code.push_back(value); };
    const auto dword = [&](std::uint32_t value)
    {
        const auto* raw = reinterpret_cast<const std::uint8_t*>(&value);
        code.insert(code.end(), raw, raw + sizeof(value));
    };

    // ecx is the car at entry. Nothing has been pushed by the function yet, so
    // the protected path can simply return: `ret 10h` drops the return address
    // and the four stack arguments, exactly as the real epilogue does.
    byte(0x9C);                                  // pushfd
    byte(0x3B); byte(0x0D);                      // cmp ecx,[protected]
    dword(static_cast<std::uint32_t>(
        next.remote + kVehicleDestroyVehicleOffset));
    byte(0x75);                                  // jne native
    const std::size_t native_displacement = code.size();
    byte(0);
    byte(0x9D);                                  // popfd
    byte(0xC2); byte(0x10); byte(0x00);          // ret 10h
    const std::size_t native_path = code.size();
    code[native_displacement] = static_cast<std::uint8_t>(
        native_path - (native_displacement + 1));
    byte(0x9D);                                  // popfd  (native)
    code.insert(code.end(), next.original.begin(), next.original.end());
    byte(0xE9);
    const std::int32_t return_relative = static_cast<std::int32_t>(
        (next.hook + kAutomobileDestroyPatchSize) -
        (next.remote + code.size() + sizeof(std::int32_t)));
    dword(static_cast<std::uint32_t>(return_relative));

    if (code.size() > kVehicleDestroyVehicleOffset ||
        !process.WriteMemory(next.remote, code.data(), code.size()) ||
        !process.WriteMemory(
            next.remote + kVehicleDestroyVehicleOffset,
            static_cast<std::uint32_t>(vehicle)))
    {
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }

    next.patch.fill(0x90);
    next.patch[0] = 0xE9;
    const std::int32_t hook_relative = static_cast<std::int32_t>(
        next.remote - (next.hook + 5));
    std::memcpy(next.patch.data() + 1, &hook_relative, sizeof(hook_relative));
    if (!process.WriteProtectedMemory(
            next.hook, next.patch.data(), next.patch.size()))
    {
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }
    std::array<std::uint8_t, kAutomobileDestroyPatchSize> verified{};
    if (!process.ReadMemory(next.hook, verified.data(), verified.size()) ||
        verified != next.patch)
    {
        (void)process.WriteProtectedMemory(
            next.hook, next.original.data(), next.original.size());
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }
    next.applied = true;
    g_vehicle_destroy_hook = next;
    LogDiagnostic(
        "Vehicle invulnerability: Destroy hook installed at %08X remote=%08X "
        "protecting %08X.",
        static_cast<unsigned>(next.hook),
        static_cast<unsigned>(next.remote),
        static_cast<unsigned>(vehicle));
    return true;
}

// The layout is re-proved on every pass, so a freed or recycled actor can
// never be written to: the type must still be a car and the version list must
// still bracket the current index.
bool ValidateVersionLayout(
    TrainerProcess& process,
    std::uintptr_t vehicle,
    std::int32_t& resistance)
{
    std::uint32_t type = 0;
    std::uintptr_t list_begin = 0;
    std::uintptr_t list_end = 0;
    std::uint32_t current_version = 0;
    if (!IsSanePointer(vehicle) ||
        !process.ReadMemory(vehicle + 0x1C, type) ||
        (type != kActorTypeAutomobile && type != kActorTypeAutoCannon) ||
        !process.ReadMemory(
            vehicle + kVersionResistanceOffset, resistance) ||
        !process.ReadMemory(vehicle + kVersionListBeginOffset, list_begin) ||
        !process.ReadMemory(vehicle + kVersionListEndOffset, list_end) ||
        !process.ReadMemory(
            vehicle + kVersionCurrentIndexOffset, current_version))
    {
        return false;
    }
    if (resistance < 0 || resistance > kMaximumVersionResistance)
        return false;
    if (list_begin == 0 && list_end == 0)
        return current_version == 0;
    if (!IsSanePointer(list_begin) || list_end < list_begin ||
        (list_end - list_begin) % sizeof(std::uint32_t) != 0)
    {
        return false;
    }
    const std::uintptr_t count =
        (list_end - list_begin) / sizeof(std::uint32_t);
    return count <= 64 && current_version <= count;
}

void RestoreVehicleInvulnerability(TrainerProcess& process)
{
    RemoveVehicleDestroyHook(process);
    if (!g_vehicle_invulnerability.applied)
    {
        g_vehicle_invulnerability = {};
        return;
    }
    if (process.IsConnected() &&
        process.ProcessId() == g_vehicle_invulnerability.process_id)
    {
        std::int32_t current = 0;
        // Only put the original back if the field is still the zero we wrote:
        // the engine may legitimately have re-armed it through AddResist.
        if (ValidateVersionLayout(
                process, g_vehicle_invulnerability.vehicle, current) &&
            current == 0)
        {
            (void)process.WriteMemory(
                g_vehicle_invulnerability.vehicle + kVersionResistanceOffset,
                g_vehicle_invulnerability.original_resistance);
        }
        LogDiagnostic(
            "Vehicle invulnerability: released vehicle=%08X resistance=%d.",
            static_cast<unsigned>(g_vehicle_invulnerability.vehicle),
            g_vehicle_invulnerability.original_resistance);
    }
    g_vehicle_invulnerability = {};
}

void UpdateVehicleInvulnerability(
    TrainerProcess& process,
    std::uintptr_t controlled_vehicle,
    const GameplaySettings& settings,
    GameplayStatus& status)
{
    if (!settings.vehicle_invulnerable_enabled || !process.IsConnected())
    {
        RestoreVehicleInvulnerability(process);
        status.vehicle_invulnerability =
            VehicleInvulnerabilityStatus::Disabled;
        return;
    }
    if (g_vehicle_invulnerability.applied &&
        g_vehicle_invulnerability.process_id != process.ProcessId())
    {
        g_vehicle_invulnerability = {};
    }

    // Keep protecting the car just left, as long as it still validates: a
    // vehicle abandoned mid-fall would otherwise explode the instant the seat
    // check stops naming the player.
    std::int32_t resistance = 0;
    std::uintptr_t target = controlled_vehicle;
    if (!IsSanePointer(target) && g_vehicle_invulnerability.applied)
        target = g_vehicle_invulnerability.vehicle;
    if (!ValidateVersionLayout(process, target, resistance))
    {
        if (g_vehicle_invulnerability.applied)
            RestoreVehicleInvulnerability(process);
        status.vehicle_invulnerability = IsSanePointer(target)
            ? VehicleInvulnerabilityStatus::UnsupportedLayout
            : VehicleInvulnerabilityStatus::WaitingForVehicle;
        return;
    }

    if (g_vehicle_invulnerability.applied &&
        g_vehicle_invulnerability.vehicle != target)
    {
        RestoreVehicleInvulnerability(process);
    }
    if (!g_vehicle_invulnerability.applied)
    {
        g_vehicle_invulnerability.process_id = process.ProcessId();
        g_vehicle_invulnerability.vehicle = target;
        g_vehicle_invulnerability.original_resistance = resistance;
        g_vehicle_invulnerability.healthy_mode = kAutomobileModeStopped;
        g_vehicle_invulnerability.applied = true;
        LogDiagnostic(
            "Vehicle invulnerability: holding vehicle=%08X original "
            "resistance=%d at +%02X.",
            static_cast<unsigned>(target), resistance,
            static_cast<unsigned>(kVersionResistanceOffset));
    }
    else if (resistance != 0)
    {
        // The engine re-armed it (a version change, or a reload). Remember the
        // new value so unticking restores something meaningful.
        g_vehicle_invulnerability.original_resistance = resistance;
    }

    if (resistance != 0)
    {
        const std::int32_t cleared = 0;
        if (!process.WriteMemory(
                target + kVersionResistanceOffset, cleared))
        {
            status.vehicle_invulnerability =
                VehicleInvulnerabilityStatus::WriteFailed;
            return;
        }
    }
    // The resistance alone leaves the "wrecked without exploding" branch open,
    // and it is that branch which failed the escort missions.
    if (!EnsureVehicleDestroyHook(process, target))
    {
        status.vehicle_invulnerability =
            VehicleInvulnerabilityStatus::UnsupportedLayout;
        return;
    }

    // Last line of defence, and the one that does not depend on having found
    // every copy of Destroy: if anything at all marked the car wrecked, put
    // the mode back to what it was while it was still intact.
    std::uint32_t mode = 0;
    if (process.ReadMemory(target + kAutomobileModeOffset, mode))
    {
        if (mode == kAutomobileModeDestroyed)
        {
            const std::uint32_t restored =
                g_vehicle_invulnerability.healthy_mode;
            if (process.WriteMemory(
                    target + kAutomobileModeOffset, restored))
            {
                LogDiagnostic(
                    "Vehicle invulnerability: mode %u restored to %u on "
                    "vehicle=%08X.",
                    kAutomobileModeDestroyed, restored,
                    static_cast<unsigned>(target));
            }
        }
        else if (mode < kAutomobileModeDestroyed)
        {
            g_vehicle_invulnerability.healthy_mode = mode;
        }
    }
    status.vehicle_invulnerability = VehicleInvulnerabilityStatus::Active;
}

bool ResolveControlledVehicle(
    TrainerProcess& process,
    const RadarSnapshot* snapshot,
    std::uintptr_t& vehicle)
{
    vehicle = 0;
    if (!process.IsConnected() || !snapshot ||
        !IsSanePointer(snapshot->player_object_address))
    {
        return false;
    }

    const auto is_local_driver = [&](std::uintptr_t candidate)
    {
        std::uint32_t type = 0;
        std::uintptr_t frame = 0;
        std::uintptr_t frame_actor = 0;
        std::uint8_t is_driver = 0;
        std::uintptr_t user = 0;
        return IsSanePointer(candidate) &&
            process.ReadMemory(candidate + 0x1C, type) &&
            (type == kActorTypeAutomobile ||
             type == kActorTypeAutoCannon) &&
            process.ReadMemory(candidate + kActorFrameOffset, frame) &&
            IsSanePointer(frame) &&
            process.ReadMemory(
                frame + kFrameActorBackReferenceOffset, frame_actor) &&
            frame_actor == candidate &&
            process.ReadMemory(
                candidate + kAutomobileSeatsOffset, is_driver) &&
            is_driver == 1 &&
            process.ReadMemory(
                candidate + kAutomobileSeatsOffset +
                    kAutomobileSeatUserOffset,
                user) &&
            user == snapshot->player_object_address;
    };

    // using_item is the cheapest path, but it can be cleared temporarily while
    // the enter/drive animation changes pose. Do not let that transient state
    // redirect the speed keys to Super Run.
    std::uintptr_t using_item = 0;
    if (process.ReadMemory(
            snapshot->player_object_address + kActorUsingItemOffset,
            using_item) &&
        is_local_driver(using_item))
    {
        vehicle = using_item;
        return true;
    }

    // Authoritative fallback: find the actor whose seat zero names the local
    // player. The mission actor vector is bounded before it is copied.
    RemoteModuleInfo module{};
    std::uintptr_t mission = 0;
    std::uint32_t actor_begin = 0;
    std::uint32_t actor_end = 0;
    if (!process.GetMainModuleInfo(module) ||
        kMissionPointerRva >= module.image_size ||
        !process.ReadMemory(
            module.base_address + kMissionPointerRva, mission) ||
        !IsSanePointer(mission) ||
        !process.ReadMemory(
            mission + kMissionActorBeginOffset, actor_begin) ||
        !process.ReadMemory(
            mission + kMissionActorEndOffset, actor_end) ||
        !IsSanePointer(actor_begin) || actor_end < actor_begin ||
        (actor_end - actor_begin) % sizeof(std::uint32_t) != 0)
    {
        return false;
    }
    const std::size_t actor_count =
        (actor_end - actor_begin) / sizeof(std::uint32_t);
    if (actor_count == 0 || actor_count > 4096)
        return false;
    std::vector<std::uint32_t> actors(actor_count);
    if (!process.ReadMemory(
            actor_begin, actors.data(), actors.size() * sizeof(actors[0])))
    {
        return false;
    }
    for (const std::uint32_t candidate : actors)
    {
        if (is_local_driver(static_cast<std::uintptr_t>(candidate)))
        {
            vehicle = candidate;
            return true;
        }
    }
    return false;
}

void UpdatePlayerSpeed(
    TrainerProcess& process,
    const RadarSnapshot* snapshot,
    const GameplaySettings& settings,
    bool vehicle_context,
    GameplayStatus& status)
{
    if (!settings.player_speed_enabled)
    {
        RestorePlayerSpeed(process);
        status.player_speed = PlayerSpeedStatus::Disabled;
        return;
    }

    if (vehicle_context)
    {
        RestorePlayerSpeed(process);
        status.player_speed = PlayerSpeedStatus::SuspendedInVehicle;
        return;
    }

    if (!process.IsConnected() || !snapshot ||
        !IsSanePointer(snapshot->player_object_address))
    {
        RestorePlayerSpeed(process);
        status.player_speed = PlayerSpeedStatus::WaitingForMission;
        return;
    }

    const std::uintptr_t actor = snapshot->player_object_address;
    if (!ActorFrameStillMatches(process, actor))
    {
        RestorePlayerSpeed(process);
        status.player_speed = PlayerSpeedStatus::UnsupportedLayout;
        return;
    }

    std::int32_t pose = -1;
    Vector3 current{};
    if (!process.ReadMemory(actor + kActorCurrentPoseOffset, pose) ||
        !process.ReadMemory(actor + kActorMoveDirectionOffset, current) ||
        pose < -1 || pose > 1024 || !IsSaneMoveDirection(current))
    {
        RestorePlayerSpeed(process);
        status.player_speed = PlayerSpeedStatus::UnsupportedLayout;
        return;
    }

    const bool context_changed = !g_player_speed.applied ||
        g_player_speed.process_id != process.ProcessId() ||
        g_player_speed.actor != actor || g_player_speed.pose != pose;
    const bool game_replaced_direction = g_player_speed.applied &&
        !SameHorizontalDirection(current, g_player_speed.last_written);

    // PlayAnim writes curr_pose before it reloads move_dir from the animation
    // table. If the external read lands in that very small interval, wait for
    // the native direction instead of capturing our previous multiplied value
    // as a new baseline.
    if (g_player_speed.applied && g_player_speed.process_id == process.ProcessId() &&
        g_player_speed.actor == actor && g_player_speed.pose != pose &&
        SameHorizontalDirection(current, g_player_speed.last_written))
    {
        status.player_speed = PlayerSpeedStatus::Active;
        return;
    }

    if (context_changed || game_replaced_direction)
    {
        if (g_player_speed.applied &&
            (g_player_speed.process_id != process.ProcessId() ||
                g_player_speed.actor != actor))
        {
            RestorePlayerSpeed(process);
        }

        g_player_speed.process_id = process.ProcessId();
        g_player_speed.actor = actor;
        g_player_speed.pose = pose;
        g_player_speed.original = current;
        g_player_speed.applied = false;
    }

    Vector3 target = g_player_speed.original;
    target.x *= settings.player_speed_multiplier;
    target.z *= settings.player_speed_multiplier;
    if (!std::isfinite(target.x) || !std::isfinite(target.z) ||
        std::fabs(target.x) > 10'000.0f || std::fabs(target.z) > 10'000.0f)
    {
        RestorePlayerSpeed(process);
        status.player_speed = PlayerSpeedStatus::UnsupportedLayout;
        return;
    }

    const bool x_written = process.WriteMemory(
        actor + kActorMoveDirectionOffset, target.x);
    const bool z_written = process.WriteMemory(
        actor + kActorMoveDirectionOffset + sizeof(float) * 2U, target.z);
    if (!x_written || !z_written)
    {
        if (x_written)
            (void)process.WriteMemory(
                actor + kActorMoveDirectionOffset, current.x);
        if (z_written)
            (void)process.WriteMemory(
                actor + kActorMoveDirectionOffset + sizeof(float) * 2U,
                current.z);
        RestorePlayerSpeed(process);
        status.player_speed = PlayerSpeedStatus::WriteFailed;
        return;
    }

    target.y = current.y;
    g_player_speed.last_written = target;
    g_player_speed.applied = true;
    status.player_speed = PlayerSpeedStatus::Active;
}

bool SetFramePositionOnMainThread(
    TrainerProcess& process,
    std::uintptr_t frame,
    const Vector3& destination,
    std::uintptr_t vehicle_to_stop = 0,
    std::uintptr_t scene = 0)
{
    constexpr std::uintptr_t kProcessCheatHookRva = 0x0009'FC10;
    constexpr std::size_t kRemoteSize = 0x100;
    constexpr std::size_t kDestinationOffset = 0xD0;
    constexpr std::size_t kCompletionOffset = 0xE0;
    constexpr std::array<std::uint8_t, 5> kExpected{
        0xB8, 0x5C, 0x10, 0x00, 0x00};
    constexpr std::array<std::uint8_t, 15> kExpectedFrameUpdate{
        0x8B, 0x4C, 0x24, 0x04, 0x83, 0xEC, 0x0C, 0x8B,
        0x41, 0x0C, 0xA9, 0x00, 0x00, 0x80, 0x00};

    RemoteModuleInfo module{};
    std::uintptr_t frame_vtable = 0;
    std::uintptr_t set_position = 0;
    RemoteModuleInfo i3d_module{};
    std::uintptr_t frame_update = 0;
    std::uintptr_t vehicle_vtable = 0;
    std::uintptr_t vehicle_callback = 0;
    std::uintptr_t scene_vtable = 0;
    std::uintptr_t set_frame_sector_pos = 0;
    const bool vtable_read = process.ReadMemory(frame, frame_vtable);
    const bool set_position_read = vtable_read &&
        IsSanePointer(frame_vtable) &&
        process.ReadMemory(
            frame_vtable + kFrameSetPositionVtableOffset, set_position);
    std::array<std::uint8_t, kExpectedFrameUpdate.size()> frame_update_bytes{};
    const bool frame_update_valid = !IsSanePointer(vehicle_to_stop) ||
        (process.GetModuleInfo(L"i3d2.dll", i3d_module) &&
         kI3dFrameUpdateRva + kExpectedFrameUpdate.size() <=
             i3d_module.image_size &&
         (frame_update = i3d_module.base_address + kI3dFrameUpdateRva) != 0 &&
         process.ReadMemory(
             frame_update, frame_update_bytes.data(),
             frame_update_bytes.size()) &&
         frame_update_bytes == kExpectedFrameUpdate &&
         process.IsReadableCodeTarget(frame_update));
    const bool vehicle_callback_valid = !IsSanePointer(vehicle_to_stop) ||
        (process.ReadMemory(vehicle_to_stop, vehicle_vtable) &&
         IsSanePointer(vehicle_vtable) &&
         process.ReadMemory(vehicle_vtable + 0x04, vehicle_callback) &&
         IsSanePointer(vehicle_callback) &&
         process.IsReadableCodeTarget(vehicle_callback));
    const bool scene_relink_valid = !IsSanePointer(vehicle_to_stop) ||
        (process.ReadMemory(scene, scene_vtable) &&
         IsSanePointer(scene_vtable) &&
         process.ReadMemory(
             scene_vtable + 0x60, set_frame_sector_pos) &&
         IsSanePointer(set_frame_sector_pos) &&
         process.IsReadableCodeTarget(set_frame_sector_pos));
    if (!process.GetMainModuleInfo(module) ||
        kProcessCheatHookRva + kExpected.size() > module.image_size ||
        !vtable_read || !set_position_read || !frame_update_valid ||
        !IsSanePointer(set_position) ||
        !process.IsReadableCodeTarget(set_position) ||
        !vehicle_callback_valid || !scene_relink_valid)
    {
        LogDiagnostic(
            "Teleport: trampoline context unavailable "
            "(frame=%08X vtable=%08X setpos=%08X update=%08X "
            "vehicle=%08X cb=%08X "
            "scene=%08X relink=%08X).",
            static_cast<unsigned>(frame),
            static_cast<unsigned>(frame_vtable),
            static_cast<unsigned>(set_position),
            static_cast<unsigned>(frame_update),
            static_cast<unsigned>(vehicle_to_stop),
            static_cast<unsigned>(vehicle_callback),
            static_cast<unsigned>(scene),
            static_cast<unsigned>(set_frame_sector_pos));
        return false;
    }

    const std::uintptr_t hook =
        module.base_address + kProcessCheatHookRva;
    std::array<std::uint8_t, 5> original{};
    if (!process.ReadMemory(hook, original.data(), original.size()) ||
        original != kExpected)
    {
        LogDiagnostic(
            "Teleport: ProcessCheat signature mismatch at %08X.",
            static_cast<unsigned>(hook));
        return false;
    }
    const std::uintptr_t remote = process.AllocateRemoteMemory(kRemoteSize);
    if (!IsSanePointer(remote))
    {
        LogDiagnostic("Teleport: trampoline allocation failed.");
        return false;
    }
    const std::uintptr_t remote_destination = remote + kDestinationOffset;
    const std::uintptr_t completion = remote + kCompletionOffset;
    const auto relative = [](std::uintptr_t target, std::uintptr_t next)
    {
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<std::intptr_t>(target) -
            static_cast<std::intptr_t>(next)));
    };
    std::vector<std::uint8_t> code;
    const auto byte = [&](std::uint8_t value) { code.push_back(value); };
    const auto dword = [&](std::uint32_t value)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(value));
        std::memcpy(code.data() + offset, &value, sizeof(value));
    };
    byte(0x9C);                            // pushfd
    byte(0x60);                            // pushad
    if (IsSanePointer(vehicle_to_stop))
    {
        // Stop the automobile through its native CB_USE_AUTO(13) path before
        // moving the root frame. C_actor::cbProc uses the installed four-word
        // callback ABI and returns with ret 0x10.
        byte(0x6A); byte(0x00);            // reserved = 0
        byte(0x6A); byte(0x00);            // prm2 = 0
        byte(0x6A); byte(0x0D);            // prm1 = stop immediately
        byte(0x6A); byte(static_cast<std::uint8_t>(
            kUseAutomobileCallback));      // msg = CB_USE_AUTO
        byte(0xB9); dword(static_cast<std::uint32_t>(vehicle_to_stop));
                                               // mov ecx,vehicle
        byte(0x8B); byte(0x01);             // mov eax,[ecx]
        byte(0xFF); byte(0x50); byte(0x04); // call cbProc, ret 0x10
    }
    byte(0x68); dword(static_cast<std::uint32_t>(remote_destination));
                                             // push &destination
    // The installed Deluxe I3D interface uses I3DAPI __stdcall. Its native
    // calls push the explicit frame pointer after the vector argument; ECX is
    // not the implicit this pointer for this ABI.
    byte(0xB8); dword(static_cast<std::uint32_t>(frame)); // mov eax,frame
    byte(0x50);                            // push frame
    byte(0x8B); byte(0x00);                // mov eax,[eax]
    byte(0xFF); byte(0x50); byte(0x0C);    // call I3D_frame::SetPos
    if (IsSanePointer(vehicle_to_stop))
    {
        // Exact Vehicle.cpp sequence: model->SetPos(), model->Update(), then
        // scene->SetFrameSector(model). Update is a non-virtual I3D routine,
        // verified above in the installed i3d2.dll before this direct call.
        byte(0x68); dword(static_cast<std::uint32_t>(frame));
        byte(0xB8); dword(static_cast<std::uint32_t>(frame_update));
        byte(0xFF); byte(0xD0);                // I3D_frame::Update(frame)
        byte(0x68); dword(static_cast<std::uint32_t>(frame));
        byte(0xB8); dword(static_cast<std::uint32_t>(scene));
        byte(0x50);                            // scene (__stdcall this)
        byte(0x8B); byte(0x00);
        byte(0xFF); byte(0x50); byte(0x60);    // SetFrameSector
    }
    byte(0xC7); byte(0x05);                // mov [completion],1
    dword(static_cast<std::uint32_t>(completion));
    dword(1);
    byte(0x61);                            // popad
    byte(0x9D);                            // popfd
    byte(0xC2); byte(0x04); byte(0x00);   // ret 4

    const std::uint32_t pending = 0;
    if (code.size() > kDestinationOffset)
    {
        LogDiagnostic(
            "Teleport: trampoline too large (%u bytes, maximum=%u).",
            static_cast<unsigned>(code.size()),
            static_cast<unsigned>(kDestinationOffset));
        (void)process.FreeRemoteMemory(remote);
        return false;
    }
    if (!process.WriteMemory(remote, code.data(), code.size()) ||
        !process.WriteMemory(remote_destination, destination) ||
        !process.WriteMemory(completion, pending))
    {
        LogDiagnostic("Teleport: trampoline code write failed.");
        (void)process.FreeRemoteMemory(remote);
        return false;
    }
    std::array<std::uint8_t, 5> jump{0xE9, 0, 0, 0, 0};
    const std::uint32_t hook_relative = relative(remote, hook + jump.size());
    std::memcpy(jump.data() + 1, &hook_relative, sizeof(hook_relative));
    if (!process.WriteProtectedMemory(hook, jump.data(), jump.size()))
    {
        LogDiagnostic("Teleport: trampoline hook write failed.");
        (void)RestoreHookSafely(
        process, hook, original.data(), original.size());
        bool executing = true;
        if (process.IsAnyThreadExecutingRange(
                remote, kRemoteSize, executing) && !executing)
        {
            (void)process.FreeRemoteMemory(remote);
        }
        return false;
    }

    const bool triggered = SendInventoryMainThreadTrigger(process);
    std::uint32_t completed = 0;
    if (triggered)
    {
        for (int attempt = 0; attempt < 500 && completed == 0; ++attempt)
        {
            (void)process.ReadMemory(completion, completed);
            if (completed == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    (void)RestoreHookSafely(
        process, hook, original.data(), original.size());
    std::array<std::uint8_t, 5> restored{};
    const bool original_restored = process.ReadMemory(
        hook, restored.data(), restored.size()) && restored == original;
    bool idle = false;
    if (original_restored)
    {
        for (int attempt = 0; attempt < 250 && !idle; ++attempt)
        {
            bool executing = true;
            idle = process.IsAnyThreadExecutingRange(
                remote, kRemoteSize, executing) && !executing;
            if (!idle)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    if (completed != 1 || !original_restored || !idle)
    {
        LogDiagnostic(
            "Teleport: trampoline did not complete "
            "(triggered=%d completed=%u restored=%d idle=%d).",
            triggered ? 1 : 0, completed,
            original_restored ? 1 : 0, idle ? 1 : 0);
        return false;
    }
    const bool freed = process.FreeRemoteMemory(remote);
    LogDiagnostic(
        "Teleport: stdcall SetPos executed on game thread "
        "(destination %.1f, %.1f, %.1f, vehicle=%08X, freed=%d).",
        destination.x, destination.y, destination.z,
        static_cast<unsigned>(vehicle_to_stop), freed ? 1 : 0);
    return freed;
}

constexpr std::uintptr_t kActorAddProgramVtableOffset = 0x24;
// V152 - `DelProgram(int pos, bool net_send)` suit `AddProgram` dans la
// declaration de `C_actor` (`H&D.h:897-898`), et la lecture des trois tables
// de methodes du binaire du joueur le confirme :
//
//   +0x24 -> 00429480   ret 16  (4 arguments)  AddProgram
//   +0x28 -> 004294C0   ret 8   (2 arguments)  DelProgram
constexpr std::uintptr_t kActorDelProgramVtableOffset = 0x28;
// Combien d'ordres on retire au maximum avant d'en poser un neuf. Une
// patrouille de mission en compte rarement plus de quelques-uns; douze couvre
// largement, et `DelProgram` sur une liste vide ne fait rien.
constexpr int kProgramClearRounds = 12;
constexpr std::uintptr_t kDriverCreateModelVtableOffset = 0x68;
constexpr std::uint32_t kProgramMoveItem = 0;

// V107 - emplacements dans la vtable d'une frame I3D.
//
// Ils sont comptes dans `I3D2.h`, en enumerant TOUTES les methodes de
// `I3D_frame` dans l'ordre de declaration, les `I3DMETHOD(Nom)` comme les
// `I3DMETHOD_(type,Nom)`. Le comptage est ancre sur `SetPos`, qui tombe sur
// le rang 3, soit +0x0C : exactement l'emplacement dont le trainer se sert
// depuis des versions pour teleporter, et que le jeu valide donc a chaque
// utilisation. L'ancre etant juste, le reste de la table l'est aussi.
//
// La V105 comptait ces rangs avec une expression qui laissait passer la
// forme `I3DMETHOD_(type,Nom)` : trente-sept methodes sur cinquante etaient
// ignorees. `Duplicate` etait annonce a +0x38, qui est en realite
// `GetRot1(S_vector &axe, float &angle)` - une methode qui ECRIT a travers
// deux pointeurs, dont le second etait pris au hasard sur la pile. C'est ce
// qui faisait sortir du jeu des la creation du premier soldat.
constexpr std::uintptr_t kFrameSetOnVtableOffset = 0x68;      // rang 26
constexpr std::uintptr_t kFrameGetParentVtableOffset = 0x80;  // rang 32
constexpr std::uintptr_t kFrameLinkToVtableOffset = 0x84;     // rang 33
constexpr std::uintptr_t kFrameDuplicateVtableOffset = 0x98;  // rang 38

// V109 - cette enumeration est desormais CONFIRMEE par le jeu lui-meme : les
// soldats crees par la V108 portaient l'uniforme du joueur, ce qui ne peut
// arriver que si `Duplicate` a bien ete appele. Le rang 38 etant juste,
// l'ancrage sur `SetPos` l'est aussi, et avec lui tous les autres rangs.

// =====================================================================
// V104 - ordre de deplacement donne a des soldats, par la carte
// =====================================================================
//
// C'est le mecanisme que le moteur emploie lui-meme pour diriger un humain :
//
//   AddProgram(0, PRG_MOVE, S_prg_add((dword)&position, true));
//
// `AddProgram` est virtuelle, a vtable+0x24 - releve dans la vtable de
// C_human du binaire de reference, ou `DelProgram` occupe +0x28, exactement
// l'emplacement dont le trainer se sert deja pour effacer les poursuites. Le
// decalage est donc verifie, pas suppose.
//
// `S_prg_add` (H&D.h:556) est un simple tableau de cinq dwords :
//   d[0] = adresse de la position visee
//   d[1] = 1  (deplacement effectif)
//   d[2..4] = 0
// et PRG_MOVE vaut 0, valeur que le trainer connait deja sous le nom
// kProgramMove dans la purge des perceptions.
//
// Rien ici ne cree d'acteur : l'ordre s'applique a des soldats qui existent
// deja, donc cette fonction ne depend d'aucune adresse a retrouver.
// V147 - un acteur humain encore vivant, quel que soit son camp.
//
// Un ordre est arme dans la fenetre G puis consomme au clic sur la carte,
// parfois plusieurs secondes plus tard. Entre les deux, un homme peut mourir
// et le moteur rendre sa memoire. Appeler `AddProgram` sur cette adresse
// arrete le jeu - c'est le meme accident que celui trouve sur la distribution
// d'armes, avec la meme signature au journal : le drapeau de fin jamais ecrit.
bool IsOrderableActor(
    TrainerProcess& process,
    const RemoteModuleInfo& module,
    std::uintptr_t actor)
{
    if (!IsSanePointer(actor) || module.image_size == 0)
        return false;
    std::uintptr_t vtable = 0;
    if (!process.ReadMemory(actor, vtable) ||
        vtable < module.base_address ||
        vtable >= module.base_address + module.image_size)
    {
        return false;
    }
    std::uint32_t type = 0;
    if (!process.ReadMemory(actor + kActorTypeOffset, type) ||
        (type != kActorTypePlayer && type != kActorTypeEnemy))
    {
        return false;
    }
    std::int32_t resistance = 0;
    return process.ReadMemory(
               actor + kPlayerResistanceOffset, resistance) &&
        resistance > 0;
}

bool IssueMoveOrderOnGameThread(
    TrainerProcess& process,
    const std::vector<std::uintptr_t>& soldiers,
    const Vector3& destination)
{
    if (soldiers.empty() || !process.IsConnected())
        return false;

    constexpr std::uintptr_t kProcessCheatHookRva = 0x0009'FC10;
    constexpr std::array<std::uint8_t, 5> kProcessCheatExpected{
        0xB8, 0x5C, 0x10, 0x00, 0x00};
    // V144 - LA LIMITE DE TRENTE-CINQ HOMMES, TROUVEE DANS LE JOURNAL
    //
    // Le journal du joueur montre l'ordre de deplacement qui aboutit a 1 et a
    // 26 hommes, puis qui est REFUSE a 66 :
    //
    //   Ralliement: 26 allie(s) rappele(s) a votre position resultat=1
    //   Ralliement: 66 allie(s) rappele(s) a votre position resultat=0
    //
    // La cause est arithmetique. Le code etait ecrit DEROULE, un bloc complet
    // par soldat, et chaque bloc coute vingt et un octets :
    //
    //   6A 01        push net_send        2
    //   68 <dw>      push &prg_add        5
    //   6A <item>    push PRG_MOVE        2
    //   6A 00        push pos             2
    //   B9 <dw>      mov ecx,soldat       5
    //   8B 01        mov eax,[ecx]        2
    //   FF 50 24     call [eax+24]        3
    //
    // Or le garde refusait tout code atteignant `kOrderDestinationOffset`,
    // soit 768 octets - ce qui plafonnait a environ TRENTE-CINQ hommes. En
    // dessous cela marchait, au-dessus rien ne partait, et le seul indice
    // etait un `resultat=0`.
    //
    // Le code est desormais une BOUCLE sur un tableau d'acteurs, comme les
    // autres appels du trainer. Sa taille ne depend plus du nombre d'hommes,
    // et la seule borne restante est la place du tableau : 0xC00 octets, soit
    // 768 acteurs.
    constexpr std::size_t kOrderDestinationOffset = 0x300;
    constexpr std::size_t kOrderPrgAddOffset = 0x310;
    constexpr std::size_t kOrderCompletionOffset = 0x330;
    constexpr std::size_t kOrderCountOffset = 0x340;
    constexpr std::size_t kOrderActorsOffset = 0x400;
    constexpr std::size_t kOrderRemoteSize = 0x1000;
    constexpr std::size_t kOrderMaximumActors =
        (kOrderRemoteSize - kOrderActorsOffset) / sizeof(std::uint32_t);

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        kProcessCheatHookRva >= module.image_size)
    {
        return false;
    }
    const std::uintptr_t hook = module.base_address + kProcessCheatHookRva;
    std::array<std::uint8_t, 5> original{};
    if (!process.ReadMemory(hook, original.data(), original.size()) ||
        original != kProcessCheatExpected)
    {
        LogDiagnostic("Ordre de deplacement: site de cheat non reconnu.");
        return false;
    }

    const std::uintptr_t remote =
        process.AllocateRemoteMemory(kOrderRemoteSize);
    if (!IsSanePointer(remote))
        return false;

    std::vector<std::uint8_t> code;
    const auto byte = [&](std::uint8_t value) { code.push_back(value); };
    const auto dword = [&](std::uint32_t value)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(value));
        std::memcpy(code.data() + offset, &value, sizeof(value));
    };
    const auto slot = [&](std::size_t offset)
    {
        dword(static_cast<std::uint32_t>(remote + offset));
    };

    // Les acteurs retenus, dans l'ordre. V147 - chacun est VERIFIE ici, juste
    // avant d'entrer dans le tableau : l'ordre a pu etre arme plusieurs
    // secondes plus tot, et des hommes ont pu mourir entre-temps.
    std::vector<std::uint32_t> actor_table;
    unsigned gone = 0;
    for (const std::uintptr_t soldier : soldiers)
    {
        if (!IsOrderableActor(process, module, soldier))
        {
            ++gone;
            continue;
        }
        if (actor_table.size() >= kOrderMaximumActors)
            break;
        actor_table.push_back(static_cast<std::uint32_t>(soldier));
    }
    if (gone != 0)
    {
        LogDiagnostic(
            "Ordre de deplacement: %u homme(s) ecarte(s) - morts ou disparus "
            "depuis que l'ordre a ete arme.",
            gone);
    }
    if (actor_table.empty())
    {
        LogDiagnostic(
            "Ordre de deplacement: plus aucun homme valide, rien n'est "
            "envoye.");
        (void)process.FreeRemoteMemory(remote);
        return false;
    }

    std::vector<std::size_t> to_end;
    byte(0x9C);                              // pushfd
    byte(0x60);                              // pushad
    byte(0x33); byte(0xF6);                  // xor esi,esi
    const std::size_t loop_start = code.size();
    byte(0xA1); slot(kOrderCountOffset);     // mov eax,[nombre]
    byte(0x3B); byte(0xF0);                  // cmp esi,eax
    byte(0x0F); byte(0x83);
    to_end.push_back(code.size()); dword(0); // jae fin
    byte(0x8B); byte(0x1C); byte(0xB5);
    slot(kOrderActorsOffset);                // mov ebx,[esi*4+acteurs]
    byte(0x85); byte(0xDB);                  // test ebx,ebx
    const std::size_t skip_actor = code.size();
    byte(0x74); byte(0x00);                  // je suivant

    // ==================================================================
    // V152 - ON VIDE LE PROGRAMME AVANT D'EN POSER UN NEUF
    // ==================================================================
    //
    // Le joueur : « quand je clique sur VENIR il ne s'arrete pas a cette
    // place, il reste bouger ».
    //
    // La cause est dans la facon dont l'ordre etait pose. `AddProgram(0, ...)`
    // INSERE le deplacement EN TETE de la liste des ordres - il ne la remplace
    // pas. Un ennemi de mission a deja sa patrouille dans cette liste : il
    // venait bien jusqu'a nous, puis reprenait sa ronde la ou il l'avait
    // laissee.
    //
    // Le moteur, lui, vide la liste avant de poser un ordre nouveau. C'est ce
    // que fait la touche de commandement du joueur (Actors.cpp:14293) :
    //
    //   if(tc.p_ctrl->Get(GKEY_CMD_PRG_CLR)){
    //      if(program.size()){
    //         ClrProgram(true);
    //         cbProc(CB_PRG_SHOW, false, 1);
    //      }
    //   }
    //
    // `ClrProgram` n'est pas virtuelle, mais `DelProgram(pos, net_send)` l'est,
    // au rang +0x28 - verifie dans les trois tables de methodes du binaire du
    // joueur, avec le `ret 8` de ses deux arguments. On retire donc l'ordre de
    // tete douze fois de suite : la liste est vide, et le deplacement qu'on
    // pose devient le seul. Une fois arrive, l'homme n'a plus rien a faire et
    // il RESTE LA.
    byte(0xBF); dword(kProgramClearRounds);  // mov edi,douze
    const std::size_t clear_loop = code.size();
    byte(0x6A); byte(0x00);                  // push net_send = false
    byte(0x6A); byte(0x00);                  // push pos = 0, l'ordre de tete
    byte(0x8B); byte(0xCB);                  // mov ecx,acteur
    byte(0x8B); byte(0x03);                  // mov eax,[acteur]
    byte(0xFF); byte(0x50);
    byte(static_cast<std::uint8_t>(kActorDelProgramVtableOffset));
    byte(0x4F);                              // dec edi
    byte(0x75);                              // jnz vider
    byte(static_cast<std::uint8_t>(
        static_cast<std::int8_t>(
            static_cast<std::ptrdiff_t>(clear_loop) -
            static_cast<std::ptrdiff_t>(code.size() + 1))));

    // AddProgram(0, PRG_MOVE, prg_add, true) - __thiscall, arguments empiles
    // de droite a gauche. `esi` et `ebx` traversent l'appel : la convention de
    // MSVC les confie a l'appelee.
    byte(0x6A); byte(0x01);                  // push net_send = true
    byte(0x68); slot(kOrderPrgAddOffset);    // push &S_prg_add
    byte(0x6A); byte(
        static_cast<std::uint8_t>(kProgramMoveItem));  // push PRG_MOVE
    byte(0x6A); byte(0x00);                  // push pos = 0, en tete
    byte(0x8B); byte(0xCB);                  // mov ecx,acteur
    byte(0x8B); byte(0x03);                  // mov eax,[acteur]
    byte(0xFF); byte(0x50);
    byte(static_cast<std::uint8_t>(kActorAddProgramVtableOffset));

    const std::size_t next_actor = code.size();
    code[skip_actor + 1] =
        static_cast<std::uint8_t>(next_actor - (skip_actor + 2));
    byte(0x46);                              // inc esi
    byte(0xE9);
    const std::size_t repeat = code.size();
    dword(0);
    const std::size_t finish = code.size();
    for (const std::size_t displacement : to_end)
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            finish - (displacement + sizeof(std::int32_t)));
        std::memcpy(code.data() + displacement, &relative, sizeof(relative));
    }
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            loop_start - (repeat + sizeof(std::int32_t)));
        std::memcpy(code.data() + repeat, &relative, sizeof(relative));
    }
    byte(0xC7); byte(0x05); slot(kOrderCompletionOffset); dword(1);
    byte(0x61);                              // popad
    byte(0x9D);                              // popfd
    code.insert(code.end(), original.begin(), original.end());
    byte(0xE9);
    const std::int32_t back = static_cast<std::int32_t>(
        (hook + original.size()) -
        (remote + code.size() + sizeof(std::int32_t)));
    dword(static_cast<std::uint32_t>(back));

    // Donnees : la position visee, puis le S_prg_add qui la designe.
    std::array<std::uint32_t, 5> prg_add{};
    prg_add[0] = static_cast<std::uint32_t>(remote + kOrderDestinationOffset);
    prg_add[1] = 1U;
    const std::uint32_t pending = 0;
    if (code.size() >= kOrderDestinationOffset ||
        !process.WriteMemory(remote, code.data(), code.size()) ||
        !process.WriteMemory(
            remote + kOrderCountOffset,
            static_cast<std::uint32_t>(actor_table.size())) ||
        !process.WriteMemory(
            remote + kOrderActorsOffset, actor_table.data(),
            actor_table.size() * sizeof(std::uint32_t)) ||
        !process.WriteMemory(remote + kOrderDestinationOffset, destination) ||
        !process.WriteMemory(
            remote + kOrderPrgAddOffset, prg_add.data(),
            prg_add.size() * sizeof(prg_add[0])) ||
        !process.WriteMemory(remote + kOrderCompletionOffset, pending))
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }

    std::array<std::uint8_t, 5> patch{0xE9, 0, 0, 0, 0};
    const std::int32_t relative = static_cast<std::int32_t>(
        remote - (hook + patch.size()));
    std::memcpy(patch.data() + 1, &relative, sizeof(relative));
    if (!InstallHookSafely(
            process, hook, patch.data(), patch.size(),
            "le saut vers le code pose"))
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }

    const bool triggered = SendInventoryMainThreadTrigger(process);
    std::uint32_t completed = 0;
    if (triggered)
    {
        for (int attempt = 0; attempt < 500 && completed == 0; ++attempt)
        {
            (void)process.ReadMemory(
                remote + kOrderCompletionOffset, completed);
            if (completed == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    (void)RestoreHookSafely(
        process, hook, original.data(), original.size());
    QueueRemotePageRelease(process, remote, kOrderRemoteSize);

    LogDiagnostic(
        "Ordre de deplacement: %u soldat(s) vers (%.2f, %.2f, %.2f) "
        "declenche=%u execute=%u.",
        static_cast<unsigned>(soldiers.size()),
        destination.x, destination.y, destination.z,
        triggered ? 1U : 0U, completed);
    return completed == 1U;
}

void ApplyPendingTeleport(
    TrainerProcess& process,
    const RadarSnapshot* snapshot,
    std::uintptr_t controlled_vehicle,
    GameplaySettings& settings,
    GameplayStatus& status)
{
    if (!settings.teleport_pending)
    {
        g_pending_teleport = {};
        return;
    }

    if (!process.IsConnected() || !snapshot ||
        !IsSanePointer(snapshot->player_object_address))
    {
        settings.teleport_pending = false;
        g_pending_teleport = {};
        status.teleport = TeleportStatus::WaitingForMission;
        return;
    }

    const DWORD process_id = process.ProcessId();
    const ULONGLONG now = GetTickCount64();
    if (g_pending_teleport.process_id != process_id ||
        g_pending_teleport.started_at == 0 ||
        now < g_pending_teleport.started_at)
    {
        g_pending_teleport = {};
        g_pending_teleport.process_id = process_id;
        g_pending_teleport.started_at = now;
    }

    bool map_open = false;
    const bool map_state_valid =
        ReadNativeMapOpen(process, snapshot, map_open);
    if (!map_state_valid || map_open)
    {
        g_pending_teleport.closed_map_samples = 0;
        status.teleport = TeleportStatus::WaitingForMapClose;
        if (!g_pending_teleport.wait_logged)
        {
            LogDiagnostic(
                "Teleport: waiting for native map to close before SetPos "
                "(state_valid=%d open=%d).",
                map_state_valid ? 1 : 0, map_open ? 1 : 0);
            g_pending_teleport.wait_logged = true;
        }
        if (now - g_pending_teleport.started_at <
            kTeleportMapCloseTimeoutMs)
        {
            return;
        }

        LogDiagnostic(
            "Teleport: native map close confirmation timed out.");
        settings.teleport_pending = false;
        g_pending_teleport = {};
        status.teleport = TeleportStatus::NativeMapUnavailable;
        return;
    }

    ++g_pending_teleport.closed_map_samples;
    status.teleport = TeleportStatus::WaitingForMapClose;
    LogDiagnostic(
        "[TEST TELEPORT] MAP_CLOSE_SAMPLE sample=%u/%u mission=%08X "
        "player=%08X target_ground=(%.3f,%.3f,%.3f).",
        g_pending_teleport.closed_map_samples,
        kTeleportClosedMapSamples,
        static_cast<unsigned>(snapshot->entity_list_object_address),
        static_cast<unsigned>(snapshot->player_object_address),
        settings.teleport_ground.x, settings.teleport_ground.y,
        settings.teleport_ground.z);
    if (g_pending_teleport.closed_map_samples < kTeleportClosedMapSamples)
        return;

    LogDiagnostic(
        "Teleport: native map closure confirmed for %u samples; "
        "executing SetPos.",
        g_pending_teleport.closed_map_samples);
    settings.teleport_pending = false;
    g_pending_teleport = {};

    const Vector3 ground = settings.teleport_ground;
    if (!IsSaneWorldPosition(ground))
    {
        status.teleport = TeleportStatus::NoWalkableGround;
        ClearSoldierOrder();
        return;
    }

    // V104 : si des soldats ont ete armes dans leur fenetre, ce clic est un
    // ORDRE et non une teleportation. Le comportement d'origine de la carte
    // est integralement conserve quand aucun soldat n'est arme.
    if (!g_soldier_order_targets.empty())
    {
        const std::vector<std::uintptr_t> targets = g_soldier_order_targets;
        ClearSoldierOrder();
        const bool ordered = IssueMoveOrderOnGameThread(
            process, targets, ground);
        status.teleport = ordered
            ? TeleportStatus::Success
            : TeleportStatus::WriteFailed;
        return;
    }

    const bool teleport_vehicle = IsSanePointer(controlled_vehicle);
    const std::uintptr_t actor = teleport_vehicle
        ? controlled_vehicle : snapshot->player_object_address;
    std::uintptr_t frame = 0;
    std::uintptr_t frame_actor = 0;
    std::uintptr_t scene = 0;
    const std::uintptr_t target_frame_offset = teleport_vehicle
        ? kActorModelOffset : kActorFrameOffset;
    if (!process.ReadMemory(actor + target_frame_offset, frame) ||
        !IsSanePointer(frame) ||
        !process.ReadMemory(
            frame + kFrameActorBackReferenceOffset, frame_actor) ||
        frame_actor != actor ||
        (teleport_vehicle &&
         (!process.ReadMemory(
              snapshot->entity_list_object_address + 0x10, scene) ||
          !IsSanePointer(scene))))
    {
        status.teleport = TeleportStatus::UnsupportedLayout;
        return;
    }

    Vector3 destination = ground;
    destination.y += teleport_vehicle
        ? kVehicleTeleportGroundClearance : kTeleportGroundClearance;
    Vector3 world_before{};
    Vector3 local_before{};
    const bool world_before_read = process.ReadMemory(
        frame + kFrameWorldPositionOffset, world_before);
    const bool local_before_read = process.ReadMemory(
        frame + kFrameLocalPositionOffset, local_before);
    LogDiagnostic(
        "[TEST TELEPORT] SETPOS_BEGIN target=%s actor=%08X frame=%08X "
        "snapshot=(%.3f,%.3f,%.3f) ground=(%.3f,%.3f,%.3f) "
        "destination=(%.3f,%.3f,%.3f) world_before_ok=%d "
        "world_before=(%.3f,%.3f,%.3f) local_before_ok=%d "
        "local_before=(%.3f,%.3f,%.3f).",
        teleport_vehicle ? "vehicle" : "player",
        static_cast<unsigned>(actor), static_cast<unsigned>(frame),
        snapshot->player.position.x, snapshot->player.position.y,
        snapshot->player.position.z,
        ground.x, ground.y, ground.z,
        destination.x, destination.y, destination.z,
        world_before_read ? 1 : 0,
        world_before.x, world_before.y, world_before.z,
        local_before_read ? 1 : 0,
        local_before.x, local_before.y, local_before.z);
    if (!SetFramePositionOnMainThread(
            process, frame, destination,
            teleport_vehicle ? actor : 0U,
            teleport_vehicle ? scene : 0U))
    {
        LogDiagnostic("[TEST TELEPORT] SETPOS_CALL_FAILED.");
        status.teleport = TeleportStatus::WriteFailed;
        return;
    }
    Vector3 world_immediate{};
    Vector3 local_immediate{};
    const bool world_immediate_read = process.ReadMemory(
        frame + kFrameWorldPositionOffset, world_immediate);
    const bool local_immediate_read = process.ReadMemory(
        frame + kFrameLocalPositionOffset, local_immediate);
    LogDiagnostic(
        "[TEST TELEPORT] SETPOS_RETURNED world_ok=%d "
        "world=(%.3f,%.3f,%.3f) local_ok=%d local=(%.3f,%.3f,%.3f).",
        world_immediate_read ? 1 : 0,
        world_immediate.x, world_immediate.y, world_immediate.z,
        local_immediate_read ? 1 : 0,
        local_immediate.x, local_immediate.y, local_immediate.z);
    // Arm the post-pose verification: the game may re-derive the frame from
    // the actor's physics on the next ticks, silently cancelling SetPos.
    g_teleport_verify = {};
    g_teleport_verify.process_id = process.ProcessId();
    g_teleport_verify.actor = actor;
    g_teleport_verify.frame = frame;
    g_teleport_verify.vehicle = teleport_vehicle ? actor : 0U;
    g_teleport_verify.scene = teleport_vehicle ? scene : 0U;
    g_teleport_verify.destination = destination;
    g_teleport_verify.started_at = GetTickCount64();
    status.teleport = TeleportStatus::Success;
}

void UpdateTeleportVerification(TrainerProcess& process)
{
    if (g_teleport_verify.frame == 0 ||
        g_teleport_verify.process_id != process.ProcessId())
    {
        return;
    }

    const ULONGLONG now = GetTickCount64();
    if (g_teleport_verify.samples >= kTeleportVerifySamples ||
        now - g_teleport_verify.started_at > kTeleportVerifyTimeoutMs)
    {
        LogDiagnostic(
            "Teleport: pose verification done (%u samples, %s).",
            g_teleport_verify.samples,
            g_teleport_verify.snapped_back
                ? "frame re-snapped by the game" : "frame held at destination");
        g_teleport_verify = {};
        return;
    }

    Vector3 world{};
    Vector3 local{};
    const bool world_read = process.ReadMemory(
        g_teleport_verify.frame + kFrameWorldPositionOffset, world);
    const bool local_read = process.ReadMemory(
        g_teleport_verify.frame + kFrameLocalPositionOffset, local);
    if (!world_read)
    {
        LogDiagnostic(
            "[TEST TELEPORT] VERIFY sample=%u world_read=0 local_read=%d "
            "actor=%08X frame=%08X.",
            g_teleport_verify.samples, local_read ? 1 : 0,
            static_cast<unsigned>(g_teleport_verify.actor),
            static_cast<unsigned>(g_teleport_verify.frame));
        ++g_teleport_verify.samples;
        return;
    }
    const Vector3& target = g_teleport_verify.destination;
    const float distance = std::sqrt(
        (world.x - target.x) * (world.x - target.x) +
        (world.y - target.y) * (world.y - target.y) +
        (world.z - target.z) * (world.z - target.z));
    LogDiagnostic(
        "[TEST TELEPORT] VERIFY sample=%u actor=%08X frame=%08X "
        "target=(%.3f,%.3f,%.3f) world=(%.3f,%.3f,%.3f) "
        "local_ok=%d local=(%.3f,%.3f,%.3f) distance=%.3f.",
        g_teleport_verify.samples,
        static_cast<unsigned>(g_teleport_verify.actor),
        static_cast<unsigned>(g_teleport_verify.frame),
        target.x, target.y, target.z,
        world.x, world.y, world.z,
        local_read ? 1 : 0, local.x, local.y, local.z, distance);
    if (distance > kTeleportVerifySnapDistance)
    {
        if (!g_teleport_verify.snapped_back)
        {
            LogDiagnostic(
                "Teleport: frame re-snapped after SetPos "
                "(world=(%.1f, %.1f, %.1f) dist=%.2f), rewriting frame "
                "without a recursive rewrite.",
                world.x, world.y, world.z, distance);
            g_teleport_verify.snapped_back = true;
        }
        if (IsSanePointer(g_teleport_verify.vehicle))
        {
            // V20's first transaction completed. Repeating CB_USE_AUTO and
            // sector relinking from the immediate cached-world check caused
            // the recorded STATUS_SINGLE_STEP crash, so vehicle verification
            // is strictly observation-only.
            LogDiagnostic(
                "Teleport: vehicle recursive retry suppressed for crash safety.");
            g_teleport_verify = {};
            return;
        }
        else
        {
            // On-foot fallback retained for actors whose physics re-applies
            // their previous pose on the following game tick.
            const std::uintptr_t frame = g_teleport_verify.frame;
            (void)process.WriteMemory(
                frame + kFrameWorldPositionOffset, target);
            (void)process.WriteMemory(
                frame + kFrameLocalPositionOffset, target);
        }
    }
    ++g_teleport_verify.samples;
}

void ClearEnemyInvisibilityPatch()
{
    g_enemy_invisibility = {};
}

bool ResetEnemyAwarenessOnMainThread(
    TrainerProcess& process,
    const RadarSnapshot& snapshot,
    EnemyInvisibilityScope scope)
{
    constexpr std::uintptr_t kProcessCheatHookRva = 0x0009FC10;
    constexpr std::uintptr_t kWatchVectorEraseRva = 0x00039CB0;
    constexpr std::uint32_t kEnemyType = 2;
    constexpr std::uint32_t kProgramMove = 0;
    constexpr std::uint32_t kProgramAttack = 4;
    constexpr std::uint32_t kMoveReasonAttackReach = 3;
    constexpr std::uintptr_t kProgramBeginOffset = 0x288;
    constexpr std::uintptr_t kProgramEndOffset = 0x28C;
    constexpr std::uintptr_t kHoldingFireInstalledOffset = 0x296;
    constexpr std::uintptr_t kActorEnumInstalledOffset = 0x298;
    constexpr std::uintptr_t kWatchVectorInstalledOffset = 0x29C;
    constexpr std::uintptr_t kCommandSubjectInstalledOffset = 0x04;
    constexpr std::uintptr_t kCommandMoveReasonInstalledOffset = 0x0C;
    constexpr std::uintptr_t kCommandProgramItemInstalledOffset = 0x2C;
    constexpr std::uint32_t kMinimumRemotePointer = 0x0001'0000U;
    constexpr std::uint32_t kMaximumRemotePointer = 0x7FFF'FFFFU;
    constexpr std::uint32_t kMaximumProgramBytes = 128U * 4U;
    constexpr std::uint32_t kWatchActorSize = 0x20U;
    constexpr std::uint32_t kMaximumWatchBytes =
        4096U * kWatchActorSize;
    constexpr std::uintptr_t kVoiceInstalledOffset = 0x280;
    constexpr std::uintptr_t kListenBeginInstalledOffset = 0x3C;
    constexpr std::uintptr_t kListenEndInstalledOffset = 0x40;
    constexpr std::array<std::uint8_t, 5> kProcessCheatExpected{
        0xB8, 0x5C, 0x10, 0x00, 0x00};
    constexpr std::array<std::uint8_t, 6> kVectorEraseExpected{
        0x8B, 0x54, 0x24, 0x08, 0x8B, 0xC1};

    std::vector<std::uintptr_t> enemies;
    std::vector<std::uintptr_t> protected_players;
    enemies.reserve(snapshot.entity_array.count);
    protected_players.reserve(snapshot.entity_array.count + 1U);
    protected_players.push_back(snapshot.player_object_address);
    for (std::uint32_t index = 0; index < snapshot.entity_array.count; ++index)
    {
        const RadarEntity& entity = snapshot.entity_array.entities[index];
        if (entity.team == EntityTeam::Enemy &&
            IsSanePointer(entity.actor_address))
        {
            enemies.push_back(entity.actor_address);
        }
        else if (scope == EnemyInvisibilityScope::WholeSquad &&
                 entity.team == EntityTeam::Ally &&
                 IsSanePointer(entity.actor_address))
        {
            protected_players.push_back(entity.actor_address);
        }
    }
    if (enemies.empty())
        return true;

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        kProcessCheatHookRva >= module.image_size ||
        kWatchVectorEraseRva >= module.image_size)
        return false;
    const std::uintptr_t hook =
        module.base_address + kProcessCheatHookRva;
    const std::uintptr_t vector_erase =
        module.base_address + kWatchVectorEraseRva;
    std::array<std::uint8_t, 5> original{};
    std::array<std::uint8_t, 6> erase_signature{};
    if (!process.ReadMemory(hook, original.data(), original.size()) ||
        original != kProcessCheatExpected ||
        !process.ReadMemory(
            vector_erase, erase_signature.data(), erase_signature.size()) ||
        erase_signature != kVectorEraseExpected)
        return false;

    std::vector<std::uint8_t> code;
    code.reserve(
        160U + enemies.size() *
            (400U + protected_players.size() * 12U));
    const auto byte = [&](std::uint8_t value) { code.push_back(value); };
    const auto dword = [&](std::uint32_t value)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(value));
        std::memcpy(code.data() + offset, &value, sizeof(value));
    };
    const auto near_jump = [&](std::uint8_t condition)
    {
        byte(0x0F); byte(condition);
        const std::size_t displacement = code.size();
        dword(0);
        return displacement;
    };
    const auto jump = [&]()
    {
        byte(0xE9);
        const std::size_t displacement = code.size();
        dword(0);
        return displacement;
    };
    const auto patch_jump = [&](std::size_t displacement, std::size_t target)
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            target - (displacement + sizeof(std::int32_t)));
        std::memcpy(code.data() + displacement, &relative, sizeof(relative));
    };
    const auto call_del_program = [&]()
    {
        byte(0x6A); byte(0x00);             // push net_send=false
        byte(0x57);                         // push edi (program index)
        byte(0x8B); byte(0xCE);             // mov ecx,esi
        byte(0x8B); byte(0x01);             // mov eax,[ecx]
        byte(0xFF); byte(0x50); byte(0x28); // call [eax+28h]
    };
    const auto stop_enemy_voice = [&]()
    {
        byte(0x8B); byte(0x86); dword(static_cast<std::uint32_t>(
            kVoiceInstalledOffset));        // mov eax,[esi+voice]
        byte(0x85); byte(0xC0);              // test eax,eax
        const std::size_t no_voice = near_jump(0x84);
        byte(0x3D); dword(kMinimumRemotePointer);
        const std::size_t voice_low = near_jump(0x82);
        byte(0x3D); dword(kMaximumRemotePointer);
        const std::size_t voice_high = near_jump(0x87);
        byte(0x8B); byte(0x10);              // mov edx,[eax]
        byte(0x81); byte(0xFA); dword(kMinimumRemotePointer);
        const std::size_t vtable_low = near_jump(0x82);
        byte(0x81); byte(0xFA); dword(kMaximumRemotePointer);
        const std::size_t vtable_high = near_jump(0x87);
        byte(0x8B); byte(0x52); byte(0x68);  // mov edx,[edx+68h]
        byte(0x81); byte(0xFA); dword(kMinimumRemotePointer);
        const std::size_t function_low = near_jump(0x82);
        byte(0x81); byte(0xFA); dword(kMaximumRemotePointer);
        const std::size_t function_high = near_jump(0x87);
        byte(0x6A); byte(0x00);              // push off
        byte(0x50);                          // push voice (stdcall this)
        byte(0xFF); byte(0xD2);              // call edx
        const std::size_t done = code.size();
        patch_jump(no_voice, done);
        patch_jump(voice_low, done);
        patch_jump(voice_high, done);
        patch_jump(vtable_low, done);
        patch_jump(vtable_high, done);
        patch_jump(function_low, done);
        patch_jump(function_high, done);
    };

    byte(0x9C);                             // pushfd
    byte(0x60);                             // pushad

    for (const std::uintptr_t enemy : enemies)
    {
        byte(0xBE); dword(static_cast<std::uint32_t>(enemy)); // mov esi,enemy
        byte(0x83); byte(0x7E); byte(0x1C); byte(kEnemyType);
        const std::size_t stale_enemy = near_jump(0x85);      // jne block_end

        // Listen sources bypass C_player::IsEnemy: EmitListen asks the
        // receiving enemy whether the emitter is hostile. Discard sources
        // already queued before the reciprocal IsEnemy hook became active.
        // S_listen_source is POD, so vector::clear is exactly end = begin and
        // intentionally keeps the allocation owned by the game.
        byte(0x8B); byte(0x86); dword(static_cast<std::uint32_t>(
            kListenBeginInstalledOffset));
        byte(0x89); byte(0x86); dword(static_cast<std::uint32_t>(
            kListenEndInstalledOffset));
        stop_enemy_voice();

        const std::size_t program_restart = code.size();
        byte(0x8B); byte(0x86); dword(static_cast<std::uint32_t>(
            kProgramBeginOffset));           // mov eax,[esi+288h]
        byte(0x85); byte(0xC0);              // test eax,eax
        const std::size_t no_program = near_jump(0x84);
        byte(0x3D); dword(kMinimumRemotePointer); // cmp eax,minimum pointer
        const std::size_t program_begin_low = near_jump(0x82); // jb clear
        byte(0x3D); dword(kMaximumRemotePointer); // cmp eax,maximum pointer
        const std::size_t program_begin_high = near_jump(0x87); // ja clear
        byte(0x8B); byte(0xBE); dword(static_cast<std::uint32_t>(
            kProgramEndOffset));             // mov edi,[esi+28Ch]
        byte(0x81); byte(0xFF); dword(kMinimumRemotePointer);
                                                // cmp edi,minimum pointer
        const std::size_t program_end_low = near_jump(0x82);   // jb clear
        byte(0x81); byte(0xFF); dword(kMaximumRemotePointer);
                                                // cmp edi,maximum pointer
        const std::size_t program_end_high = near_jump(0x87);  // ja clear
        byte(0x3B); byte(0xF8);              // cmp edi,eax
        const std::size_t program_reversed = near_jump(0x82);  // jb clear
        byte(0x2B); byte(0xF8);              // sub edi,eax
        byte(0xF7); byte(0xC7); dword(0x03); // test edi,3
        const std::size_t program_unaligned = near_jump(0x85); // jne clear
        byte(0x81); byte(0xFF); dword(kMaximumProgramBytes);
                                                // cmp edi,max bytes
        const std::size_t program_too_large = near_jump(0x87); // ja clear
        byte(0xC1); byte(0xEF); byte(0x02);  // shr edi,2
        byte(0x85); byte(0xFF);              // test edi,edi
        const std::size_t empty_program = near_jump(0x84);
        byte(0x4F);                          // dec edi

        const std::size_t scan_command = code.size();
        byte(0x8B); byte(0x86); dword(static_cast<std::uint32_t>(
            kProgramBeginOffset));
        byte(0x8B); byte(0x0C); byte(0xB8);  // mov ecx,[eax+edi*4]
        byte(0x85); byte(0xC9);              // test ecx,ecx
        const std::size_t null_command = near_jump(0x84);
        byte(0x81); byte(0xF9); dword(kMinimumRemotePointer);
                                                // cmp ecx,minimum pointer
        const std::size_t command_low = near_jump(0x82);
        byte(0x81); byte(0xF9); dword(kMaximumRemotePointer);
                                                // cmp ecx,maximum pointer
        const std::size_t command_high = near_jump(0x87);
        byte(0x83); byte(0x79);
        byte(static_cast<std::uint8_t>(kCommandProgramItemInstalledOffset));
        byte(static_cast<std::uint8_t>(kProgramAttack));
        const std::size_t not_attack = near_jump(0x85);

        std::vector<std::size_t> protected_subject_matches;
        protected_subject_matches.reserve(protected_players.size());
        for (const std::uintptr_t protected_player : protected_players)
        {
            byte(0x81); byte(0x79);
            byte(static_cast<std::uint8_t>(
                kCommandSubjectInstalledOffset));
            dword(static_cast<std::uint32_t>(protected_player));
            protected_subject_matches.push_back(near_jump(0x84)); // je match
        }
        const std::size_t subject_not_protected = jump();
        const std::size_t protected_subject = code.size();
        for (const std::size_t match : protected_subject_matches)
            patch_jump(match, protected_subject);

        // Cancel the physical and audible part of the reaction only after an
        // attack against a protected player has been positively identified.
        byte(0xC6); byte(0x86); dword(static_cast<std::uint32_t>(
            kHoldingFireInstalledOffset)); byte(0x00);
        stop_enemy_voice();

        byte(0x33); byte(0xED);              // xor ebp,ebp (paired move flag)
        byte(0x85); byte(0xFF);              // test edi,edi
        const std::size_t no_previous = near_jump(0x84);
        byte(0x8B); byte(0x86); dword(static_cast<std::uint32_t>(
            kProgramBeginOffset));
        byte(0x8B); byte(0x44); byte(0xB8); byte(0xFC);
                                                // mov eax,[eax+edi*4-4]
        byte(0x85); byte(0xC0);              // test eax,eax
        const std::size_t previous_null = near_jump(0x84);
        byte(0x3D); dword(kMinimumRemotePointer);
        const std::size_t previous_low = near_jump(0x82);
        byte(0x3D); dword(kMaximumRemotePointer);
        const std::size_t previous_high = near_jump(0x87);
        byte(0x83); byte(0x78);
        byte(static_cast<std::uint8_t>(kCommandProgramItemInstalledOffset));
        byte(static_cast<std::uint8_t>(kProgramMove));
        const std::size_t previous_not_move = near_jump(0x85);
        byte(0x83); byte(0x78);
        byte(static_cast<std::uint8_t>(kCommandMoveReasonInstalledOffset));
        byte(static_cast<std::uint8_t>(kMoveReasonAttackReach));
        const std::size_t previous_not_hunt = near_jump(0x85);
        byte(0xBD); dword(1);                // mov ebp,1

        const std::size_t delete_attack = code.size();
        patch_jump(no_previous, delete_attack);
        patch_jump(previous_null, delete_attack);
        patch_jump(previous_low, delete_attack);
        patch_jump(previous_high, delete_attack);
        patch_jump(previous_not_move, delete_attack);
        patch_jump(previous_not_hunt, delete_attack);
        // C_enemy's AI update reads programs[0] with no emptiness guard when
        // the flag at [esi+0x295] is clear: 0040C192 jumps straight to
        // 0040C262, which does mov eax,[esi+288h] / mov eax,[eax] /
        // mov edi,[eax+44h]. The guarded path at 0040C1A6 does test the
        // begin/end difference, the unguarded one does not. Emptying the
        // vector therefore makes the engine read past the allocation on its
        // next tick, one to two seconds later. Always leave one program.
        const auto keep_one_program = [&]()
        {
            byte(0x8B); byte(0x86); dword(static_cast<std::uint32_t>(
                kProgramEndOffset));
            byte(0x2B); byte(0x86); dword(static_cast<std::uint32_t>(
                kProgramBeginOffset));
            byte(0x83); byte(0xF8); byte(0x08); // fewer than two entries?
            return near_jump(0x82);             // jb: keep the last one
        };
        const std::size_t last_program = keep_one_program();
        call_del_program();
        byte(0x85); byte(0xED);              // test ebp,ebp
        const std::size_t no_paired_move = near_jump(0x84);
        const std::size_t last_paired_program = keep_one_program();
        byte(0x4F);                          // dec edi
        call_del_program();
        const std::size_t after_delete = code.size();
        patch_jump(no_paired_move, after_delete);
        patch_jump(last_paired_program, after_delete);
        const std::size_t restart_jump = jump();
        patch_jump(restart_jump, program_restart);

        const std::size_t next_command = code.size();
        patch_jump(null_command, next_command);
        patch_jump(command_low, next_command);
        patch_jump(command_high, next_command);
        patch_jump(not_attack, next_command);
        patch_jump(subject_not_protected, next_command);
        patch_jump(last_program, next_command);
        byte(0x85); byte(0xFF);              // test edi,edi
        const std::size_t first_checked = near_jump(0x84);
        byte(0x4F);                          // dec edi
        const std::size_t scan_jump = jump();
        patch_jump(scan_jump, scan_command);

        const std::size_t clear_watch = code.size();
        patch_jump(no_program, clear_watch);
        patch_jump(program_begin_low, clear_watch);
        patch_jump(program_begin_high, clear_watch);
        patch_jump(program_end_low, clear_watch);
        patch_jump(program_end_high, clear_watch);
        patch_jump(program_reversed, clear_watch);
        patch_jump(program_unaligned, clear_watch);
        patch_jump(program_too_large, clear_watch);
        patch_jump(empty_program, clear_watch);
        patch_jump(first_checked, clear_watch);
        byte(0xC7); byte(0x86); dword(static_cast<std::uint32_t>(
            kActorEnumInstalledOffset)); dword(1000);
        byte(0x8D); byte(0x8E); dword(static_cast<std::uint32_t>(
            kWatchVectorInstalledOffset));  // lea ecx,[esi+29Ch]
        byte(0x8B); byte(0x41); byte(0x08); // mov eax,[ecx+8]
        byte(0x8B); byte(0x51); byte(0x04); // mov edx,[ecx+4]
        byte(0x3B); byte(0xD0);             // cmp edx,eax
        const std::size_t watch_empty = near_jump(0x84);
        byte(0x81); byte(0xFA); dword(kMinimumRemotePointer);
                                                // cmp edx,minimum pointer
        const std::size_t watch_begin_low = near_jump(0x82);
        byte(0x81); byte(0xFA); dword(kMaximumRemotePointer);
                                                // cmp edx,maximum pointer
        const std::size_t watch_begin_high = near_jump(0x87);
        byte(0x3D); dword(kMinimumRemotePointer);
                                                // cmp eax,minimum pointer
        const std::size_t watch_end_low = near_jump(0x82);
        byte(0x3D); dword(kMaximumRemotePointer);
                                                // cmp eax,maximum pointer
        const std::size_t watch_end_high = near_jump(0x87);
        byte(0x3B); byte(0xC2);              // cmp eax,edx
        const std::size_t watch_reversed = near_jump(0x82);
        byte(0x8B); byte(0xF8);              // mov edi,eax
        byte(0x2B); byte(0xFA);              // sub edi,edx
        byte(0xF7); byte(0xC7); dword(kWatchActorSize - 1U);
                                                // test edi,element alignment
        const std::size_t watch_unaligned = near_jump(0x85);
        byte(0x81); byte(0xFF); dword(kMaximumWatchBytes);
                                                // cmp edi,max bytes
        const std::size_t watch_too_large = near_jump(0x87);
        if (scope == EnemyInvisibilityScope::WholeSquad)
        {
            // The entire watch list is protected in squad mode, so any voice
            // tied to its current recognition state is no longer relevant.
            stop_enemy_voice();
            byte(0x8D); byte(0x8E); dword(static_cast<std::uint32_t>(
                kWatchVectorInstalledOffset)); // reload ecx=watch vector
            byte(0x8B); byte(0x41); byte(0x08); // reload eax=end
            byte(0x8B); byte(0x51); byte(0x04); // reload edx=begin
            byte(0x50);                     // push eax=end
            byte(0x52);                     // push edx=begin
            byte(0xB8); dword(static_cast<std::uint32_t>(vector_erase));
            byte(0xFF); byte(0xD0);         // call eax
        }
        else
        {
            byte(0x8B); byte(0xFA);         // mov edi,edx (cursor)
            const std::size_t watch_scan = code.size();
            byte(0x3B); byte(0xF8);         // cmp edi,eax
            const std::size_t watch_not_found = near_jump(0x83); // jae
            byte(0x81); byte(0x3F);
            dword(static_cast<std::uint32_t>(
                snapshot.player_object_address));
            const std::size_t watch_found = near_jump(0x84);
            byte(0x83); byte(0xC7); byte(kWatchActorSize); // add edi,20h
            const std::size_t watch_loop = jump();
            patch_jump(watch_loop, watch_scan);
            const std::size_t erase_one_watch = code.size();
            patch_jump(watch_found, erase_one_watch);
            // In local mode only a watch record that names the controlled
            // player is allowed to stop the enemy's current reaction.
            stop_enemy_voice();
            byte(0x8D); byte(0x8E); dword(static_cast<std::uint32_t>(
                kWatchVectorInstalledOffset)); // reload ecx=watch vector
            byte(0x8D); byte(0x57); byte(kWatchActorSize); // lea edx,[edi+20h]
            byte(0x52);                     // push element end
            byte(0x57);                     // push element begin
            byte(0xB8); dword(static_cast<std::uint32_t>(vector_erase));
            byte(0xFF); byte(0xD0);         // call eax
            const std::size_t watch_scan_end = code.size();
            patch_jump(watch_not_found, watch_scan_end);
        }
        const std::size_t block_end = code.size();
        patch_jump(watch_empty, block_end);
        patch_jump(watch_begin_low, block_end);
        patch_jump(watch_begin_high, block_end);
        patch_jump(watch_end_low, block_end);
        patch_jump(watch_end_high, block_end);
        patch_jump(watch_reversed, block_end);
        patch_jump(watch_unaligned, block_end);
        patch_jump(watch_too_large, block_end);
        patch_jump(stale_enemy, block_end);
    }

    byte(0xC7); byte(0x05);
    const std::size_t completion_address_patch = code.size();
    dword(0); dword(1);                    // mov [completion],1
    byte(0x61);                            // popad
    byte(0x9D);                            // popfd
    byte(0xC2); byte(0x04); byte(0x00);   // ret 4

    const std::size_t completion_offset =
        (code.size() + 15U) & ~std::size_t(15U);
    const std::size_t remote_size =
        (completion_offset + sizeof(std::uint32_t) + 0xFFFU) &
        ~std::size_t(0xFFFU);
    const std::uintptr_t remote = process.AllocateRemoteMemory(remote_size);
    if (!IsSanePointer(remote))
        return false;
    const std::uintptr_t completion = remote + completion_offset;
    const std::uint32_t completion32 =
        static_cast<std::uint32_t>(completion);
    std::memcpy(
        code.data() + completion_address_patch,
        &completion32, sizeof(completion32));
    const std::uint32_t pending = 0;
    if (!process.WriteMemory(completion, pending) ||
        !process.WriteMemory(remote, code.data(), code.size()))
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }

    const auto relative = [](std::uintptr_t target, std::uintptr_t next)
    {
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<std::intptr_t>(target) -
            static_cast<std::intptr_t>(next)));
    };
    std::array<std::uint8_t, 5> hook_jump{0xE9, 0, 0, 0, 0};
    const std::uint32_t hook_relative = relative(remote, hook + 5U);
    std::memcpy(hook_jump.data() + 1, &hook_relative, sizeof(hook_relative));
    if (!process.WriteProtectedMemory(
            hook, hook_jump.data(), hook_jump.size()))
    {
        (void)RestoreHookSafely(
        process, hook, original.data(), original.size());
        bool executing = true;
        if (process.IsAnyThreadExecutingRange(
                remote, remote_size, executing) && !executing)
            (void)process.FreeRemoteMemory(remote);
        return false;
    }

    const bool triggered = SendInventoryMainThreadTrigger(process);
    std::uint32_t completed = 0;
    if (triggered)
    {
        for (int attempt = 0; attempt < 500 && completed == 0; ++attempt)
        {
            (void)process.ReadMemory(completion, completed);
            if (completed == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    (void)RestoreHookSafely(
        process, hook, original.data(), original.size());
    std::array<std::uint8_t, 5> restored{};
    const bool original_restored = process.ReadMemory(
        hook, restored.data(), restored.size()) && restored == original;
    if (completed != 1 || !original_restored)
        return false;
    // Le site de cheat a deja retrouve ses octets d'origine : la purge est
    // terminee du point de vue du jeu. La page rejoint la file de liberation.
    QueueRemotePageRelease(process, remote, remote_size);
    return true;
}

// Read-only census of what the enemy AI currently holds against the player.
// Nothing is written. This measures the thing that actually matters - whether
// enemies are still carrying an attack program or a watch entry aimed at a
// protected actor - independently of whether the IsEnemy hooks are installed.
// The IsEnemy filter answers "is this actor hostile"; it does not by itself
// prove that no enemy is already committed to shooting.
void LogEnemyAwarenessCensus(
    TrainerProcess& process,
    const RadarSnapshot& snapshot,
    EnemyInvisibilityScope scope)
{
    constexpr std::uintptr_t kProgramBeginOffset = 0x288;
    constexpr std::uintptr_t kProgramEndOffset = 0x28C;
    constexpr std::uintptr_t kWatchVectorOffset = 0x29C;
    constexpr std::uintptr_t kCommandProgramItemOffset = 0x2C;
    constexpr std::uintptr_t kCommandSubjectCensusOffset = 0x04;
    constexpr std::uint32_t kProgramAttackType = 4;
    constexpr std::uint32_t kWatchEntrySize = 0x20;
    constexpr std::uint32_t kMaximumScannedCommands = 128;
    constexpr std::uint32_t kMaximumScannedWatches = 512;

    std::vector<std::uintptr_t> protected_players;
    protected_players.push_back(snapshot.player_object_address);
    if (scope == EnemyInvisibilityScope::WholeSquad)
    {
        for (std::uint32_t i = 0; i < snapshot.entity_array.count; ++i)
        {
            const RadarEntity& entity = snapshot.entity_array.entities[i];
            if (entity.team == EntityTeam::Ally &&
                IsSanePointer(entity.actor_address))
            {
                protected_players.push_back(entity.actor_address);
            }
        }
    }
    const auto is_protected = [&](std::uintptr_t actor)
    {
        return std::find(
            protected_players.begin(), protected_players.end(), actor) !=
            protected_players.end();
    };

    std::uint32_t enemies = 0;
    std::uint32_t attacking = 0;
    std::uint32_t watching = 0;
    for (std::uint32_t i = 0; i < snapshot.entity_array.count; ++i)
    {
        const RadarEntity& entity = snapshot.entity_array.entities[i];
        if (entity.team != EntityTeam::Enemy ||
            !IsSanePointer(entity.actor_address))
        {
            continue;
        }
        ++enemies;

        std::uintptr_t begin = 0;
        std::uintptr_t end = 0;
        if (process.ReadMemory(
                entity.actor_address + kProgramBeginOffset, begin) &&
            process.ReadMemory(
                entity.actor_address + kProgramEndOffset, end) &&
            IsSanePointer(begin) && end > begin)
        {
            const std::uint32_t count = std::min(
                static_cast<std::uint32_t>((end - begin) / sizeof(
                    std::uint32_t)),
                kMaximumScannedCommands);
            for (std::uint32_t slot = 0; slot < count; ++slot)
            {
                std::uintptr_t command = 0;
                std::uint32_t item_type = 0;
                std::uintptr_t subject = 0;
                if (process.ReadMemory(
                        begin + slot * sizeof(std::uint32_t), command) &&
                    IsSanePointer(command) &&
                    process.ReadMemory(
                        command + kCommandProgramItemOffset, item_type) &&
                    item_type == kProgramAttackType &&
                    process.ReadMemory(
                        command + kCommandSubjectCensusOffset, subject) &&
                    is_protected(subject))
                {
                    ++attacking;
                    break;
                }
            }
        }

        std::uintptr_t watch_begin = 0;
        std::uintptr_t watch_end = 0;
        if (process.ReadMemory(
                entity.actor_address + kWatchVectorOffset + 0x04,
                watch_begin) &&
            process.ReadMemory(
                entity.actor_address + kWatchVectorOffset + 0x08, watch_end) &&
            IsSanePointer(watch_begin) && watch_end > watch_begin)
        {
            const std::uint32_t count = std::min(
                static_cast<std::uint32_t>(
                    (watch_end - watch_begin) / kWatchEntrySize),
                kMaximumScannedWatches);
            for (std::uint32_t slot = 0; slot < count; ++slot)
            {
                std::uintptr_t watched = 0;
                if (process.ReadMemory(
                        watch_begin + slot * kWatchEntrySize, watched) &&
                    is_protected(watched))
                {
                    ++watching;
                    break;
                }
            }
        }
    }

    LogDiagnostic(
        "Enemy AI census: enemies=%u attacking_protected=%u "
        "watching_protected=%u protected_actors=%u scope=%u.",
        enemies, attacking, watching,
        static_cast<unsigned>(protected_players.size()),
        static_cast<unsigned>(scope));
}

bool NeutralizeEnemyAwareness(
    TrainerProcess& process,
    const RadarSnapshot& snapshot,
    EnemyInvisibilityScope scope)
{
    if (g_enemy_invisibility.awareness_reset)
        return true;
    // The purge runs through the cheat trigger, which delivers keystrokes to
    // the foreground window and therefore used to pull the game in front the
    // moment this box was ticked. Wait for H&D to be active on its own instead:
    // the two IsEnemy hooks are plain memory writes and are already in place,
    // so the player is invisible from the start; only the purge of sounds,
    // chases and voices already memorised is deferred.
    if (!process.IsGameWindowActive())
        return false;
    const ULONGLONG now = GetTickCount64();
    if (now < g_enemy_invisibility.next_reset_attempt)
        return false;
    // V95 : 1000 ms d'attente entre deux tentatives faisaient perdre jusqu'a
    // une seconde au retour dans le jeu, alors que la purge elle-meme dure
    // quelques millisecondes. 150 ms suffisent a ne pas marteler le jeu.
    g_enemy_invisibility.next_reset_attempt = now + 150ULL;
    g_enemy_invisibility.awareness_reset =
        ResetEnemyAwarenessOnMainThread(process, snapshot, scope);
    return g_enemy_invisibility.awareness_reset;
}

// The one-shot main-thread purge removes the programs and watch records that
// already existed when the box was enabled.  It cannot, by itself, prevent a
// later WatchHumans pass from rebuilding those records; the trainer therefore
// also held the enemy's own scan accumulator at zero, every frame.
//
// V96, 3 septembre 2026 18:05 - ce gel devient propre a la portee escouade.
// La source du moteur tranche la question (`Actors.cpp`,
// `C_human::WatchHumans`) :
//
//     if((actor_enum_count += time) >= WATCH_ENUM_COUNT){
//        actor_enum_count = 0;
//        ... mission.EnumActors(...)   // seul push_back de watch_actors
//     }
//
// et le rappel d'enumeration commence par `if(!a->IsEnemy(t.actor)) return;`.
// Deux consequences :
//  1. tant que le crochet `C_player::IsEnemy` est pose, un acteur protege ne
//     PEUT PAS etre reinscrit dans une liste de surveillance, meme si
//     l'enumeration tourne librement : le gel n'est donc pas necessaire pour
//     proteger le joueur;
//  2. maintenir l'accumulateur a zero empeche l'ennemi de decouvrir QUI QUE CE
//     SOIT de nouveau, y compris le soldat d'un PC ami. C'est exactement le
//     defaut rapporte le 3 septembre 2026 : en portee « Joueur actuel », les
//     amis restaient ignores des ennemis en debut de mission, jusqu'a ce qu'un
//     decochage/recochage laisse une enumeration se produire.
//
// En portee escouade, tous les joueurs sont filtres de toute facon : le gel n'y
// change rien d'observable et il est conserve tel quel. En portee locale, seule
// la liberation de `holding_fire` est maintenue et l'enumeration native reprend
// son cours. Ces deux ecritures scalaires alignees restent sans course avec le
// jeu, contrairement a une modification de vecteur.
bool MaintainEnemyAwarenessSuppression(
    TrainerProcess& process,
    const RadarSnapshot& snapshot,
    EnemyInvisibilityScope scope)
{
    constexpr std::uintptr_t kHoldingFireOffset = 0x296;
    constexpr std::uintptr_t kActorEnumOffset = 0x298;
    constexpr std::uint32_t kScanInhibit = 0;

    const bool inhibit_rescan =
        scope == EnemyInvisibilityScope::WholeSquad;
    std::uint32_t held = 0;
    std::uint32_t failed = 0;
    const std::uint8_t release_fire = 0;
    for (std::uint32_t index = 0;
         index < snapshot.entity_array.count;
         ++index)
    {
        const RadarEntity& entity = snapshot.entity_array.entities[index];
        if (entity.active == 0 || entity.team != EntityTeam::Enemy ||
            !IsSanePointer(entity.actor_address))
        {
            continue;
        }

        // An actor may disappear between the radar read and this pass.  Count
        // that failed write for diagnostics but do not disable protection for
        // every other live enemy.
        bool written = process.WriteMemory(
            entity.actor_address + kHoldingFireOffset, release_fire);
        if (inhibit_rescan)
        {
            written = process.WriteMemory(
                entity.actor_address + kActorEnumOffset, kScanInhibit) &&
                written;
        }
        if (written)
            ++held;
        else
            ++failed;
    }

    static ULONGLONG next_log_tick = 0;
    const ULONGLONG now = GetTickCount64();
    if (now >= next_log_tick)
    {
        next_log_tick = now + 5000ULL;
        LogDiagnostic(
            "Enemy invisibility: continuous awareness hold enemies=%u "
            "write_failures=%u scope=%u rescan_frozen=%u.",
            static_cast<unsigned>(held), static_cast<unsigned>(failed),
            static_cast<unsigned>(scope), inhibit_rescan ? 1U : 0U);
    }
    return true;
}

void ClearEnemyHearingPatch()
{
    g_enemy_hearing = {};
}

bool RestoreEnemyHearingHook(TrainerProcess& process)
{
    if (!g_enemy_hearing.applied)
    {
        ClearEnemyHearingPatch();
        return true;
    }
    if (!process.IsConnected() ||
        process.ProcessId() != g_enemy_hearing.process_id)
    {
        ClearEnemyHearingPatch();
        return true;
    }

    std::array<std::uint8_t, 7> current{};
    bool restored = process.ReadMemory(
        g_enemy_hearing.function, current.data(), current.size());
    if (restored && current != g_enemy_hearing.original)
    {
        restored = current == g_enemy_hearing.patch &&
            process.WriteProtectedMemory(
                g_enemy_hearing.function,
                g_enemy_hearing.original.data(),
                g_enemy_hearing.original.size());
    }
    if (restored)
    {
        restored = process.ReadMemory(
            g_enemy_hearing.function, current.data(), current.size()) &&
            current == g_enemy_hearing.original;
    }

    // Les octets natifs sont deja reposes : le comportement du jeu est
    // revenu a la normale a cet instant precis. La page part dans la file de
    // liberation differee au lieu de bloquer l'interface.
    if (!restored)
        return false;
    QueueRemotePageRelease(
        process, g_enemy_hearing.remote, g_enemy_hearing.remote_size);
    ClearEnemyHearingPatch();
    return true;
}

bool RestorePlayerDamageHookState(
    TrainerProcess& process,
    PlayerDamagePatchState& state)
{
    if (!state.applied)
        return true;

    const PlayerDamagePatchState previous = state;
    if (!process.IsConnected() ||
        process.ProcessId() != previous.process_id)
    {
        state = {};
        return true;
    }

    std::array<std::uint8_t, 5> current{};
    bool restored = process.ReadMemory(
        previous.function, current.data(), current.size());
    if (restored && current != previous.original)
    {
        restored = current == previous.patch &&
            process.WriteProtectedMemory(
                previous.function,
                previous.original.data(),
                previous.original.size());
    }
    if (restored)
    {
        restored = process.ReadMemory(
            previous.function, current.data(), current.size()) &&
            current == previous.original;
    }

    if (!restored)
    {
        LogDiagnostic(
            "Absolute protection: player damage hook restore failed "
            "function=%08X.",
            static_cast<unsigned>(previous.function));
        return false;
    }
    QueueRemotePageRelease(process, previous.remote, previous.remote_size);
    LogDiagnostic(
        "Absolute protection: player damage hook removed function=%08X.",
        static_cast<unsigned>(previous.function));
    state = {};
    return true;
}

bool RestorePlayerDamageHook(TrainerProcess& process)
{
    return RestorePlayerDamageHookState(process, g_player_damage);
}

bool InstallPlayerDamageHookState(
    TrainerProcess& process,
    std::uintptr_t function,
    PlayerDamagePatchState& state,
    std::uintptr_t protected_player = 0,
    bool local_squad_scope = false)
{
    // C_player::Hit begins with three complete instructions: sub esp,6Ch;
    // push ebx; push ebp.  A five-byte JMP replaces exactly those bytes and
    // the trampoline replays them on the native path.
    constexpr std::array<std::uint8_t, 5> kExpected{
        0x83, 0xEC, 0x6C, 0x53, 0x55};

    PlayerDamagePatchState next{};
    next.process_id = process.ProcessId();
    next.function = function;
    next.protected_player = protected_player;
    next.local_squad_scope = local_squad_scope;
    next.remote_size = kEnemyInvisibilityRemoteSize;
    if (!process.ReadMemory(
            function, next.original.data(), next.original.size()) ||
        next.original != kExpected)
    {
        // V165 - ce chemin est repris a CHAQUE image tant que le hook est
        // deja pose : le prologue commence alors par E9, donc la signature ne
        // correspondra plus jamais. Journalise tel quel, il a ecrit 1473
        // lignes identiques dans la MEME seconde - donc 1473 ouvertures et
        // fermetures de fichier - et le jeu saccadait. On ne le dit plus
        // qu'une fois par adresse.
        static std::uintptr_t last_reported_mismatch = 0;
        if (last_reported_mismatch != function)
        {
            last_reported_mismatch = function;
            LogDiagnostic(
                "Enemy invisibility: player damage signature mismatch at "
                "%08X (dit une seule fois - le hook est deja en place).",
                static_cast<unsigned>(function));
        }
        return false;
    }
    next.remote = process.AllocateRemoteMemory(next.remote_size);
    if (!IsSanePointer(next.remote))
        return false;

    const auto relative32 = [](std::uintptr_t target, std::uintptr_t next_ip)
    {
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<std::intptr_t>(target) -
            static_cast<std::intptr_t>(next_ip)));
    };
    std::vector<std::uint8_t> code;
    const auto byte = [&](std::uint8_t value) { code.push_back(value); };
    const auto dword = [&](std::uint32_t value)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(value));
        std::memcpy(code.data() + offset, &value, sizeof(value));
    };

    // Hit is virtual and therefore can be invoked for enemies as well. Keep
    // their native path intact. Enemy invisibility retains its historical
    // all-C_player guard; absolute protection compares ECX to one exact
    // actor address, so squad mates and the network peer remain untouched.
    std::size_t native_jump = 0;
    if (local_squad_scope)
    {
        // V95 : le second test, `network_owner == 0`, a ete retire. C'est
        // cette machine qui possede l'IA ennemie, donc c'est ici que sont
        // calcules les degats subis par le soldat d'un PC ami avant leur
        // emission reseau. Le laisser passer, c'etait laisser la portee
        // « Escouade entiere » sans aucun effet sur les joueurs connectes.
        byte(0x83); byte(0x79); byte(0x1C); byte(kActorTypePlayer);
        byte(0x0F); byte(0x85);
        native_jump = code.size();
        dword(0);
        byte(0x32); byte(0xC0);
        byte(0xC2); byte(0x14); byte(0x00);
        const std::size_t native = code.size();
        for (const std::uint8_t value : kExpected)
            byte(value);
        byte(0xE9);
        dword(relative32(function + kExpected.size(),
            next.remote + code.size() + sizeof(std::uint32_t)));
        const std::int32_t native_relative = static_cast<std::int32_t>(
            native - (native_jump + sizeof(std::int32_t)));
        std::memcpy(
            code.data() + native_jump, &native_relative,
            sizeof(native_relative));
    }
    else if (IsSanePointer(protected_player))
    {
        const std::uint32_t target = static_cast<std::uint32_t>(protected_player);
        byte(0xB8); dword(target);            // mov eax, protected player
        byte(0x3B); byte(0xC8);                // cmp ecx,eax
        byte(0x0F); byte(0x85);                // jne native
        native_jump = code.size();
        dword(0);
    }
    else
    {
        byte(0x83); byte(0x79); byte(0x1C); byte(kActorTypePlayer);
                                                 // cmp dword ptr [ecx+1Ch],1
        byte(0x0F); byte(0x85);                // jne native
        native_jump = code.size();
        dword(0);
    }
    if (!local_squad_scope)
    {
        byte(0x32); byte(0xC0);              // xor al,al
        byte(0xC2); byte(0x14); byte(0x00); // ret 14h

        const std::size_t native = code.size();
        for (const std::uint8_t value : kExpected)
            byte(value);
        byte(0xE9);
        dword(relative32(
            function + kExpected.size(),
            next.remote + code.size() + sizeof(std::uint32_t)));
        const std::int32_t native_relative = static_cast<std::int32_t>(
            native - (native_jump + sizeof(std::int32_t)));
        std::memcpy(
            code.data() + native_jump,
            &native_relative,
            sizeof(native_relative));
    }

    if (!process.WriteMemory(next.remote, code.data(), code.size()))
    {
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }
    next.patch = {0xE9, 0, 0, 0, 0};
    const std::uint32_t hook_relative = relative32(
        next.remote, function + next.patch.size());
    std::memcpy(next.patch.data() + 1, &hook_relative, 4);
    next.applied = true;
    state = next;
    if (!process.WriteProtectedMemory(
            function, next.patch.data(), next.patch.size()))
    {
        (void)RestorePlayerDamageHookState(process, state);
        return false;
    }
    LogDiagnostic(
        "Enemy invisibility: direct player damage filter installed "
        "function=%08X remote=%08X.",
        static_cast<unsigned>(function), static_cast<unsigned>(next.remote));
    return true;
}

bool InstallPlayerDamageHook(
    TrainerProcess& process,
    std::uintptr_t function,
    std::uintptr_t protected_player = 0,
    bool local_squad_scope = false)
{
    return InstallPlayerDamageHookState(
        process, function, g_player_damage, protected_player,
        local_squad_scope);
}

template <std::size_t PatchSize>
bool RestoreDeathHook(
    TrainerProcess& process,
    PlayerDeathPatchStateT<PatchSize>& state,
    const char* guard_name)
{
    if (!state.applied)
        return true;

    const PlayerDeathPatchStateT<PatchSize> previous = state;
    if (!process.IsConnected() || process.ProcessId() != previous.process_id)
    {
        state = {};
        return true;
    }

    std::array<std::uint8_t, PatchSize> current{};
    bool restored = process.ReadMemory(
        previous.function, current.data(), current.size());
    if (restored && current != previous.original)
    {
        restored = current == previous.patch && process.WriteProtectedMemory(
            previous.function, previous.original.data(), previous.original.size());
    }
    if (restored)
    {
        restored = process.ReadMemory(
            previous.function, current.data(), current.size()) &&
            current == previous.original;
    }

    if (!restored)
    {
        LogDiagnostic(
            "Absolute protection: %s hook restore failed function=%08X.",
            guard_name,
            static_cast<unsigned>(previous.function));
        return false;
    }
    QueueRemotePageRelease(process, previous.remote, previous.remote_size);
    LogDiagnostic(
        "Absolute protection: %s hook removed function=%08X.",
        guard_name,
        static_cast<unsigned>(previous.function));
    state = {};
    return true;
}

bool RestorePlayerDeathHook(TrainerProcess& process)
{
    return RestoreDeathHook(process, g_player_death, "network death");
}

bool RestorePlayerBaseDeathHook(TrainerProcess& process)
{
    return RestoreDeathHook(process, g_player_base_death, "base death");
}

bool RestorePlayerExplosionHook(TrainerProcess& process)
{
    return RestoreDeathHook(process, g_player_explosion, "explosion");
}

template <std::size_t PatchSize>
bool InstallDeathHook(
    TrainerProcess& process,
    std::uintptr_t function,
    std::uintptr_t protected_player,
    PlayerDeathPatchStateT<PatchSize>& state,
    const std::array<std::uint8_t, PatchSize>& expected,
    const char* guard_name,
    bool local_squad_scope = false,
    bool include_remote_players = false)
{
    if (!IsSanePointer(protected_player))
        return false;

    PlayerDeathPatchStateT<PatchSize> next{};
    next.process_id = process.ProcessId();
    next.function = function;
    next.protected_player = protected_player;
    next.local_squad_scope = local_squad_scope;
    next.remote_size = kAbsoluteProtectionRemoteSize;
    if (!process.ReadMemory(
            function, next.original.data(), next.original.size()) ||
        next.original != expected)
    {
        LogDiagnostic(
            "Absolute protection: %s signature mismatch at %08X.",
            guard_name,
            static_cast<unsigned>(function));
        return false;
    }
    next.remote = process.AllocateRemoteMemory(next.remote_size);
    if (!IsSanePointer(next.remote))
        return false;

    const auto relative32 = [](std::uintptr_t target, std::uintptr_t next_ip)
    {
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<std::intptr_t>(target) -
            static_cast<std::intptr_t>(next_ip)));
    };
    std::vector<std::uint8_t> code;
    const auto byte = [&](std::uint8_t value) { code.push_back(value); };
    const auto dword = [&](std::uint32_t value)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(value));
        std::memcpy(code.data() + offset, &value, sizeof(value));
    };

    // Both entries are thiscall and end in ret 8. ECX is the dying actor.
    // Squad scope still excludes the joined player by network_owner.
    std::array<std::size_t, 2> native_jumps{};
    std::size_t native_jump_count = 0;
    if (local_squad_scope)
    {
        byte(0x83); byte(0x79); byte(0x1C); byte(kActorTypePlayer);
        byte(0x0F); byte(0x85);
        native_jumps[native_jump_count++] = code.size(); dword(0);
        if (!include_remote_players)
        {
            // Die et C_human::Die restent strictement locaux : la mort d'un
            // soldat distant est decidee par sa propre machine. Explode, lui,
            // est calcule ici quand la grenade appartient a l'IA de l'hote,
            // donc il est pose sans test de proprietaire.
            byte(0x83); byte(0x79); byte(0x34); byte(0x00);
            byte(0x0F); byte(0x85);
            native_jumps[native_jump_count++] = code.size(); dword(0);
        }
    }
    else
    {
        byte(0x3B); byte(0x0D);                // cmp ecx,[target]
        dword(static_cast<std::uint32_t>(
            next.remote + kProtectionRemoteTargetOffset));
        byte(0x0F); byte(0x85);                // jne native
        native_jumps[native_jump_count++] = code.size(); dword(0);
    }
    byte(0x33); byte(0xC0);                    // xor eax,eax
    byte(0xC2); byte(0x08); byte(0x00);        // ret 8

    const std::size_t native = code.size();
    code.insert(code.end(), expected.begin(), expected.end());
    byte(0xE9);
    dword(relative32(
        function + expected.size(),
        next.remote + code.size() + sizeof(std::uint32_t)));
    for (std::size_t index = 0; index < native_jump_count; ++index)
    {
        const std::int32_t native_relative = static_cast<std::int32_t>(
            native - (native_jumps[index] + sizeof(std::int32_t)));
        std::memcpy(code.data() + native_jumps[index], &native_relative,
            sizeof(native_relative));
    }

    if (code.size() > kProtectionRemoteTargetOffset ||
        !process.WriteMemory(next.remote, code.data(), code.size()) ||
        !process.WriteMemory(
            next.remote + kProtectionRemoteTargetOffset,
            static_cast<std::uint32_t>(protected_player)))
    {
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }
    next.patch.fill(0x90);
    next.patch[0] = 0xE9;
    const std::uint32_t hook_relative = relative32(
        next.remote, function + 5U);
    std::memcpy(next.patch.data() + 1, &hook_relative, sizeof(hook_relative));
    next.applied = true;
    state = next;
    if (!process.WriteProtectedMemory(
            function, next.patch.data(), next.patch.size()))
    {
        (void)RestoreDeathHook(process, state, guard_name);
        return false;
    }
    LogDiagnostic(
        "Absolute protection: %s guard installed fn=%08X player=%08X remote=%08X.",
        guard_name,
        static_cast<unsigned>(function),
        static_cast<unsigned>(protected_player),
        static_cast<unsigned>(next.remote));
    return true;
}

bool InstallPlayerDeathHook(
    TrainerProcess& process,
    std::uintptr_t function,
    std::uintptr_t protected_player,
    bool local_squad_scope = false)
{
    constexpr std::array<std::uint8_t, kHumanDieHookPatchSize> kExpected{
        // C_player::Die: push ebp / mov ebp,esp / sub esp,0BCh.
        0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xBC, 0x00, 0x00, 0x00};
    return InstallDeathHook(
        process, function, protected_player, g_player_death, kExpected,
        "C_player::Die network death", local_squad_scope);
}

bool InstallPlayerBaseDeathHook(
    TrainerProcess& process,
    std::uintptr_t function,
    std::uintptr_t protected_player,
    bool local_squad_scope = false)
{
    constexpr std::array<std::uint8_t, kHumanDieHookPatchSize> kExpected{
        // C_human::Die: sub esp,44h / push ebx / push ebp / mov ebp,ecx...
        0x83, 0xEC, 0x44, 0x53, 0x55, 0x8B, 0xE9, 0x56, 0x57};
    return InstallDeathHook(
        process, function, protected_player, g_player_base_death, kExpected,
        "C_human::Die incoming death", local_squad_scope);
}

bool InstallPlayerExplosionHook(
    TrainerProcess& process,
    std::uintptr_t function,
    std::uintptr_t protected_player,
    bool local_squad_scope = false)
{
    constexpr std::array<std::uint8_t, kPlayerExplodeHookPatchSize> kExpected{
        // C_player::Explode: sub esp,44h / push ebx / push ebp / push esi
        // / mov esi,ecx / xor ecx,ecx. The complete xor is mandatory: V76
        // copied only 0x33 and corrupted native explosion handling.
        0x83, 0xEC, 0x44, 0x53, 0x55, 0x56, 0x8B, 0xF1, 0x33, 0xC9};
    return InstallDeathHook(
        process, function, protected_player, g_player_explosion, kExpected,
        "C_player::Explode explosive damage", local_squad_scope);
}

bool RestoreSquadPlayerGuards(TrainerProcess& process)
{
    bool restored = true;
    for (PlayerDamagePatchState& state : g_squad_player_damage)
        restored = RestorePlayerDamageHookState(process, state) && restored;
    for (PlayerDeathPatchState& state : g_squad_player_death)
        restored = RestoreDeathHook(process, state, "C_player::Die squad") && restored;
    for (PlayerExplosionPatchState& state : g_squad_player_explosion)
        restored = RestoreDeathHook(process, state, "C_player::Explode squad") && restored;
    if (restored)
    {
        g_squad_player_damage.clear();
        g_squad_player_death.clear();
        g_squad_player_explosion.clear();
    }
    return restored;
}

bool CollectLocalSquadVirtualEntries(
    TrainerProcess& process,
    const RadarSnapshot& snapshot,
    std::vector<std::uintptr_t>& local_players,
    std::vector<std::uintptr_t>& hits,
    std::vector<std::uintptr_t>& dies,
    std::vector<std::uintptr_t>& explodes,
    std::size_t& local_player_count,
    std::size_t& remote_player_count)
{
    local_players.clear();
    hits.clear();
    dies.clear();
    explodes.clear();
    local_player_count = 0;
    remote_player_count = 0;
    std::vector<std::uintptr_t> actors;
    actors.reserve(snapshot.entity_array.count + 1U);
    actors.push_back(snapshot.player_object_address);
    for (std::uint32_t index = 0; index < snapshot.entity_array.count; ++index)
    {
        const RadarEntity& entity = snapshot.entity_array.entities[index];
        // Do not trust RadarEntity::team here. Ultimate Mod players can be
        // classified as Unknown (or temporarily as another team) while still
        // being a perfectly valid local C_player. The raw actor type and
        // network owner below are the authoritative filters.
        if (IsSanePointer(entity.actor_address))
            actors.push_back(entity.actor_address);
    }
    std::sort(actors.begin(), actors.end());
    actors.erase(std::unique(actors.begin(), actors.end()), actors.end());

    for (const std::uintptr_t actor : actors)
    {
        std::uint32_t type = 0;
        std::uint32_t owner = 0;
        std::uintptr_t vtable = 0;
        std::uintptr_t hit = 0;
        std::uintptr_t die = 0;
        std::uintptr_t explode = 0;
        if (!IsSanePointer(actor) ||
            !process.ReadMemory(actor + kActorTypeOffset, type) ||
            !process.ReadMemory(actor + 0x34U, owner) ||
            type != kActorTypePlayer)
        {
            continue;
        }
        const bool remote_player = owner != 0;
        if (
            !process.ReadMemory(actor, vtable) || !IsSanePointer(vtable) ||
            !process.ReadMemory(vtable + kPlayerHitVtableOffset, hit) ||
            !process.ReadMemory(vtable + kPlayerDieVtableOffset, die) ||
            !process.ReadMemory(vtable + kPlayerExplodeVtableOffset, explode) ||
            !process.IsReadableCodeTarget(hit) ||
            !process.IsReadableCodeTarget(die) ||
            !process.IsReadableCodeTarget(explode))
        {
            if (remote_player)
                ++remote_player_count;
            continue;
        }
        if (remote_player)
        {
            // V95, 3 septembre 2026 17:22. Le soldat d'un PC ami n'est jamais
            // tue par cette machine, mais c'est bien ici que l'IA locale - qui
            // appartient a l'hote - calcule les degats qu'il subit, avant de
            // les emettre sur le reseau. Ses entrees Hit et Explode rejoignent
            // donc le jeu de gardes de la portee escouade. Ses entrees Die
            // restent natives : sa mort est decidee par sa propre machine, et
            // la bloquer ici ne le sauverait pas, cela desynchroniserait les
            // deux ecrans. Son octet no_hit local n'est pas touche non plus.
            ++remote_player_count;
            hits.push_back(hit);
            explodes.push_back(explode);
            continue;
        }
        ++local_player_count;
        local_players.push_back(actor);
        hits.push_back(hit);
        dies.push_back(die);
        explodes.push_back(explode);
    }
    const auto deduplicate = [](std::vector<std::uintptr_t>& entries)
    {
        std::sort(entries.begin(), entries.end());
        entries.erase(std::unique(entries.begin(), entries.end()), entries.end());
    };
    deduplicate(hits);
    deduplicate(dies);
    deduplicate(explodes);
    deduplicate(local_players);
    return !hits.empty() && !dies.empty() && !explodes.empty();
}

bool IsMatchingLocalPlayer(
    TrainerProcess& process,
    std::uintptr_t actor,
    DWORD process_id)
{
    if (!process.IsConnected() || process.ProcessId() != process_id ||
        !IsSanePointer(actor))
    {
        return false;
    }
    std::uint32_t type = 0;
    std::uint32_t owner = 0;
    return process.ReadMemory(actor + kActorTypeOffset, type) &&
        process.ReadMemory(actor + 0x34U, owner) &&
        type == kActorTypePlayer && owner == 0;
}

bool RestoreNativeNoHitFlags(TrainerProcess& process)
{
    if (g_native_no_hit_players.empty())
        return true;

    const std::size_t entry_count = g_native_no_hit_players.size();
    bool restored = true;
    for (const NativeNoHitState& state : g_native_no_hit_players)
    {
        // Actor allocations can disappear at mission teardown. Never write
        // an old byte into a reused/non-player allocation.
        if (!IsMatchingLocalPlayer(
                process, state.actor, state.process_id))
        {
            continue;
        }
        std::uint8_t current = 0;
        if (!process.ReadMemory(
                state.actor + kPlayerNoHitCheatOffset, current))
        {
            restored = false;
            continue;
        }
        if (current != state.original &&
            !process.WriteMemory(
                state.actor + kPlayerNoHitCheatOffset, state.original))
        {
            restored = false;
        }
    }
    if (restored || !process.IsConnected())
        g_native_no_hit_players.clear();
    LogDiagnostic(
        "Absolute protection: native no-hit restore entries=%u success=%u.",
        static_cast<unsigned>(entry_count),
        restored ? 1U : 0U);
    return restored;
}

bool ApplyNativeNoHitFlags(
    TrainerProcess& process,
    const std::vector<std::uintptr_t>& desired_players,
    std::size_t& live_count)
{
    live_count = 0;
    if (!process.IsConnected())
        return false;

    const DWORD process_id = process.ProcessId();
    if (!g_native_no_hit_players.empty() &&
        g_native_no_hit_players.front().process_id != process_id)
    {
        g_native_no_hit_players.clear();
    }

    // Restore actors that left the chosen scope while they are still valid.
    auto tracked = g_native_no_hit_players.begin();
    while (tracked != g_native_no_hit_players.end())
    {
        if (std::find(
                desired_players.begin(), desired_players.end(),
                tracked->actor) != desired_players.end())
        {
            ++tracked;
            continue;
        }
        if (IsMatchingLocalPlayer(
                process, tracked->actor, tracked->process_id))
        {
            std::uint8_t current = 0;
            if (!process.ReadMemory(
                    tracked->actor + kPlayerNoHitCheatOffset, current) ||
                (current != tracked->original &&
                 !process.WriteMemory(
                     tracked->actor + kPlayerNoHitCheatOffset,
                     tracked->original)))
            {
                return false;
            }
        }
        tracked = g_native_no_hit_players.erase(tracked);
    }

    for (const std::uintptr_t actor : desired_players)
    {
        if (!IsMatchingLocalPlayer(process, actor, process_id))
            return false;
        auto existing = std::find_if(
            g_native_no_hit_players.begin(), g_native_no_hit_players.end(),
            [&](const NativeNoHitState& item) { return item.actor == actor; });
        if (existing == g_native_no_hit_players.end())
        {
            std::uint8_t original = 0;
            if (!process.ReadMemory(
                    actor + kPlayerNoHitCheatOffset, original) ||
                original > 1U)
            {
                LogDiagnostic(
                    "Absolute protection: invalid native no-hit byte "
                    "player=%08X value=%u.",
                    static_cast<unsigned>(actor),
                    static_cast<unsigned>(original));
                return false;
            }
            g_native_no_hit_players.push_back(
                NativeNoHitState{process_id, actor, original});
        }
        const std::uint8_t enabled = 1;
        std::uint8_t current = 0;
        if (!process.ReadMemory(
                actor + kPlayerNoHitCheatOffset, current) ||
            (current != enabled &&
             !process.WriteMemory(
                 actor + kPlayerNoHitCheatOffset, enabled)) ||
            !process.ReadMemory(
                actor + kPlayerNoHitCheatOffset, current) ||
            current != enabled)
        {
            LogDiagnostic(
                "Absolute protection: native no-hit write/verify failed "
                "player=%08X.", static_cast<unsigned>(actor));
            return false;
        }
        ++live_count;
    }
    return live_count == desired_players.size() && live_count != 0;
}

template <typename State>
bool ContainsFunction(const std::vector<State>& states, std::uintptr_t function)
{
    return std::any_of(states.begin(), states.end(),
        [&](const State& state) { return state.function == function; });
}

template <typename State, typename Restore>
bool RemoveStaleSquadGuards(
    TrainerProcess& process,
    std::vector<State>& states,
    const std::vector<std::uintptr_t>& desired,
    Restore&& restore)
{
    bool success = true;
    auto it = states.begin();
    while (it != states.end())
    {
        const bool stale = it->process_id != process.ProcessId() ||
            std::find(desired.begin(), desired.end(), it->function) == desired.end();
        if (!stale)
        {
            ++it;
            continue;
        }
        if (!restore(*it))
        {
            success = false;
            ++it;
            continue;
        }
        it = states.erase(it);
    }
    return success;
}

bool RestoreProtectedFallHook(TrainerProcess& process)
{
    if (!g_protected_fall.applied)
        return true;

    const ProtectedFallHookState previous = g_protected_fall;
    if (!process.IsConnected() || process.ProcessId() != previous.process_id)
    {
        g_protected_fall = {};
        return true;
    }

    // First make the trampoline stop matching. This is safe even if the
    // engine is executing the page while we wait to restore the six bytes.
    const std::uint32_t none = 0;
    (void)process.WriteMemory(
        previous.remote + kProtectedFallTargetOffset, none);
    (void)process.WriteMemory(
        previous.remote + kProtectedFallLifeTargetOffset, none);

    std::array<std::uint8_t, kLifeUnlimitedTickHookPatchSize> current{};
    bool restored = process.ReadMemory(
        previous.function, current.data(), current.size());
    if (restored && current != previous.original)
    {
        restored = current == previous.patch && process.WriteProtectedMemory(
            previous.function, previous.original.data(), previous.original.size());
    }
    if (restored)
    {
        restored = process.ReadMemory(
            previous.function, current.data(), current.size()) &&
            current == previous.original;
    }

    if (!restored)
    {
        LogDiagnostic(
            "Absolute protection: fall-impact hook restore failed fn=%08X.",
            static_cast<unsigned>(previous.function));
        return false;
    }
    QueueRemotePageRelease(process, previous.remote, previous.remote_size);
    g_protected_fall = {};
    LogDiagnostic("Absolute protection: native fall-impact guard removed.");
    return true;
}

bool EnsureProtectedFallHook(
    TrainerProcess& process,
    std::uintptr_t player,
    bool local_squad_scope = false)
{
    if (!IsSanePointer(player) || !process.IsConnected())
        return false;

    if (g_protected_fall.applied &&
        g_protected_fall.process_id == process.ProcessId())
    {
        if (g_protected_fall.local_squad_scope == local_squad_scope &&
            (local_squad_scope || g_protected_fall.protected_player == player))
            return true;
        if (g_protected_fall.local_squad_scope != local_squad_scope)
        {
            if (!RestoreProtectedFallHook(process))
                return false;
        }
        else
        {
        if (!process.WriteMemory(
                g_protected_fall.remote + kProtectedFallTargetOffset,
                static_cast<std::uint32_t>(player)))
        {
            return false;
        }
        g_protected_fall.protected_player = player;
        return true;
        }
    }
    if (g_protected_fall.applied && !RestoreProtectedFallHook(process))
        return false;

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        module.image_size <= kLifeUnlimitedTickHookRva +
            kLifeUnlimitedTickHookPatchSize)
    {
        return false;
    }

    ProtectedFallHookState next{};
    next.process_id = process.ProcessId();
    next.function = module.base_address + kLifeUnlimitedTickHookRva;
    next.protected_player = player;
    next.local_squad_scope = local_squad_scope;
    next.remote_size = kProtectedFallRemoteSize;
    constexpr std::array<std::uint8_t, kLifeUnlimitedTickHookPatchSize>
        kExpected{0x8B, 0x83, 0xCC, 0x00, 0x00, 0x00};
    if (!process.ReadMemory(
            next.function, next.original.data(), next.original.size()) ||
        next.original != kExpected)
    {
        LogDiagnostic(
            "Absolute protection: fall-impact signature mismatch at %08X.",
            static_cast<unsigned>(next.function));
        return false;
    }
    next.remote = process.AllocateRemoteMemory(next.remote_size);
    if (!IsSanePointer(next.remote))
        return false;

    const auto relative32 = [](std::uintptr_t target, std::uintptr_t next_ip)
    {
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<std::intptr_t>(target) -
            static_cast<std::intptr_t>(next_ip)));
    };
    std::vector<std::uint8_t> code;
    code.reserve(160);
    const auto byte = [&](std::uint8_t value) { code.push_back(value); };
    const auto dword = [&](std::uint32_t value)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(value));
        std::memcpy(code.data() + offset, &value, sizeof(value));
    };
    const auto slot = [&](std::size_t offset)
    {
        dword(static_cast<std::uint32_t>(next.remote + offset));
    };
    const auto near_jump = [&](std::uint8_t condition)
    {
        byte(0x0F); byte(condition);
        const std::size_t displacement = code.size();
        dword(0);
        return displacement;
    };
    const auto patch_jump = [&](std::size_t displacement, std::size_t target)
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            target - (displacement + sizeof(std::int32_t)));
        std::memcpy(code.data() + displacement, &relative, sizeof(relative));
    };

    byte(0x9C);                              // pushfd
    byte(0x60);                              // pushad
    std::array<std::size_t, 2> other_fallers{};
    std::size_t other_faller_count = 0;
    if (local_squad_scope)
    {
        byte(0x83); byte(0x7B); byte(0x1C); byte(kActorTypePlayer);
        other_fallers[other_faller_count++] = near_jump(0x85);
        byte(0x83); byte(0x7B); byte(0x34); byte(0x00);
        other_fallers[other_faller_count++] = near_jump(0x85);
    }
    else
    {
        byte(0x3B); byte(0x1D); slot(kProtectedFallTargetOffset);
        other_fallers[other_faller_count++] = near_jump(0x85);
    }
    // This is intentionally the only fall write. C_human::Tick still applies
    // normal gravity and moves normally; its two CB_DIE fall branches only
    // run when this prior-frame flag is set.
    byte(0xC6); byte(0x83); dword(static_cast<std::uint32_t>(
        kActorFallingOffset)); byte(0x00);     // falling = false
    const std::size_t after_fall = code.size();
    for (std::size_t index = 0; index < other_faller_count; ++index)
        patch_jump(other_fallers[index], after_fall);

    // Keep F3 / Life Unlimited compatible with this permanent tick hook.
    // The existing implementation used this exact site only for one frame;
    // here it becomes a one-shot command slot on the same safe game thread.
    byte(0x3B); byte(0x1D); slot(kProtectedFallLifeTargetOffset);
                                                  // cmp ebx,[life target]
    const std::size_t other_life = near_jump(0x85); // jne done
    byte(0xC7); byte(0x05); slot(kProtectedFallLifeTargetOffset); dword(0);
    byte(0x6A); byte(0x00);                  // reserved
    byte(0x6A); byte(0x00);                  // prm2
    // V97 : le message et son premier parametre viennent de deux mots de la
    // page, au lieu d'etre compiles en dur. Le meme creneau sert donc aussi
    // bien a l'ancien CB_CHEAT qu'a la sortie de vehicule CB_USE_AUTO.
    byte(0xFF); byte(0x35); slot(kProtectedFallCallbackPrm1Offset);
    byte(0xFF); byte(0x35); slot(kProtectedFallCallbackMessageOffset);
    byte(0x8B); byte(0xCB);                  // mov ecx,ebx
    byte(0x8B); byte(0x01);                  // mov eax,[ecx]
    byte(0xFF); byte(0x50); byte(0x04);      // call cbProc
    byte(0xC7); byte(0x05); slot(kProtectedFallLifeCompletionOffset);
    dword(1);
    const std::size_t done = code.size();
    patch_jump(other_life, done);
    byte(0x61);                              // popad
    byte(0x9D);                              // popfd
    code.insert(code.end(), kExpected.begin(), kExpected.end());
    byte(0xE9);
    dword(relative32(
        next.function + kLifeUnlimitedTickHookPatchSize,
        next.remote + code.size() + sizeof(std::uint32_t)));

    if (code.size() >= kProtectedFallTargetOffset ||
        !process.WriteMemory(next.remote, code.data(), code.size()) ||
        !process.WriteMemory(
            next.remote + kProtectedFallTargetOffset,
            static_cast<std::uint32_t>(player)))
    {
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }
    const std::uint32_t none = 0;
    if (!process.WriteMemory(
            next.remote + kProtectedFallLifeTargetOffset, none) ||
        !process.WriteMemory(
            next.remote + kProtectedFallLifeCompletionOffset, none) ||
        !process.WriteMemory(
            next.remote + kProtectedFallCallbackMessageOffset, none) ||
        !process.WriteMemory(
            next.remote + kProtectedFallCallbackPrm1Offset, none))
    {
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }

    next.patch.fill(0x90);
    next.patch[0] = 0xE9;
    const std::uint32_t hook_relative = relative32(
        next.remote, next.function + 5U);
    std::memcpy(next.patch.data() + 1, &hook_relative, sizeof(hook_relative));
    if (!process.WriteProtectedMemory(
            next.function, next.patch.data(), next.patch.size()))
    {
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }
    std::array<std::uint8_t, kLifeUnlimitedTickHookPatchSize> verified{};
    if (!process.ReadMemory(
            next.function, verified.data(), verified.size()) ||
        verified != next.patch)
    {
        (void)process.WriteProtectedMemory(
            next.function, next.original.data(), next.original.size());
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }
    next.applied = true;
    g_protected_fall = next;
    LogDiagnostic(
        "Absolute protection: native fall-impact guard installed fn=%08X "
        "player=%08X remote=%08X.",
        static_cast<unsigned>(next.function),
        static_cast<unsigned>(player), static_cast<unsigned>(next.remote));
    return true;
}

bool QueueActorCallbackThroughProtectedFallHook(
    TrainerProcess& process,
    std::uintptr_t player,
    std::uint32_t message,
    std::uint32_t prm1)
{
    if (!g_protected_fall.applied ||
        g_protected_fall.process_id != process.ProcessId() ||
        !IsSanePointer(player))
    {
        return false;
    }
    const std::uint32_t none = 0;
    // L'ordre compte : message et parametre d'abord, cible en dernier. Le
    // trampoline ne lit les deux premiers qu'apres avoir reconnu la cible.
    if (!process.WriteMemory(
            g_protected_fall.remote + kProtectedFallLifeCompletionOffset,
            none) ||
        !process.WriteMemory(
            g_protected_fall.remote + kProtectedFallCallbackMessageOffset,
            message) ||
        !process.WriteMemory(
            g_protected_fall.remote + kProtectedFallCallbackPrm1Offset,
            prm1) ||
        !process.WriteMemory(
            g_protected_fall.remote + kProtectedFallLifeTargetOffset,
            static_cast<std::uint32_t>(player)))
    {
        return false;
    }

    std::uint32_t completed = 0;
    for (unsigned attempt = 0; attempt < 500 && completed == 0; ++attempt)
    {
        (void)process.ReadMemory(
            g_protected_fall.remote + kProtectedFallLifeCompletionOffset,
            completed);
        if (completed == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    (void)process.WriteMemory(
        g_protected_fall.remote + kProtectedFallLifeTargetOffset, none);
    LogDiagnostic(
        "Rappel natif: joue via la garde de chute completed=%u player=%08X "
        "msg=%u prm1=%u.",
        completed, static_cast<unsigned>(player),
        static_cast<unsigned>(message), static_cast<unsigned>(prm1));
    return completed == 1U;
}

// V101, 3 septembre 2026 - pourquoi le soldat d'un ami mourait quand meme.
//
// Le journal du PC ami prouvait que l'ordre arrivait et que sa vie passait bien
// a 20000 chez lui. Il mourait pourtant comme avant. La raison tient a la facon
// dont H&D suit la vie : CHAQUE machine execute `C_human::Hit` sur SA copie de
// l'acteur et y soustrait la resistance - le commentaire du crochet de degats
// le dit deja : « C_human::Hit is the virtual slot used by both locally
// simulated bullets and NM_HUMAN_HIT packets ».
//
// Il existe donc deux compteurs de vie pour le meme soldat. Celui de l'ami
// valait 20000, mais la copie gardee par l'hote valait toujours sa vie normale;
// c'est elle qui atteignait zero la premiere, declenchait la mort et
// l'annoncait sur le reseau. L'ami mourait sur ordre de l'hote.
//
// La vie etendue couvre donc maintenant TOUTES les copies d'acteur joueur, quel
// que soit leur proprietaire, sur chaque machine. Aucun compteur ne peut plus
// arriver a zero avant les autres.
void CollectLocalPlayers(
    TrainerProcess& process,
    const RadarSnapshot& snapshot,
    std::vector<std::uintptr_t>& players)
{
    players.clear();
    const auto consider = [&](std::uintptr_t actor)
    {
        if (!IsSanePointer(actor))
            return;
        std::uint32_t type = 0;
        if (!process.ReadMemory(actor + kActorTypeOffset, type) ||
            type != kActorTypePlayer)
        {
            return;
        }
        if (std::find(players.begin(), players.end(), actor) == players.end())
            players.push_back(actor);
    };
    consider(snapshot.player_object_address);
    for (std::uint32_t index = 0; index < snapshot.entity_array.count; ++index)
        consider(snapshot.entity_array.entities[index].actor_address);
}

// Refuse d'ecrire si ce que l'on lit ne ressemble pas a la vie d'un soldat.
// C'est le garde-fou de la deduction d'offset decrite plus haut.
bool ValidateExtendedHealthLayout(
    TrainerProcess& process,
    std::uintptr_t actor,
    std::int32_t& resistance,
    std::int32_t& init_resistance)
{
    if (!process.ReadMemory(actor + kPlayerResistanceOffset, resistance) ||
        !process.ReadMemory(
            actor + kPlayerInitResistanceOffset, init_resistance))
    {
        return false;
    }
    if (init_resistance == kExtendedHealthValue)
        return true;
    return init_resistance > 0 &&
        init_resistance <= kMaximumNativeInitResistance &&
        resistance >= 0 && resistance <= init_resistance;
}

bool RestoreExtendedHealth(TrainerProcess& process)
{
    if (g_extended_health.empty())
        return true;
    bool restored = true;
    for (const ExtendedHealthState& entry : g_extended_health)
    {
        // Meme condition que la pose : tout acteur encore vivant de type
        // joueur, proprietaire local ou distant.
        std::uint32_t type = 0;
        if (!process.IsConnected() ||
            process.ProcessId() != entry.process_id ||
            !IsSanePointer(entry.actor) ||
            !process.ReadMemory(entry.actor + kActorTypeOffset, type) ||
            type != kActorTypePlayer)
        {
            continue;
        }
        // La vie rendue est la vie normale PLEINE : le joueur a demande que la
        // desactivation le remette a l'etat sain, pas a une fraction d'une
        // barre qui valait 20000 la seconde d'avant.
        if (!process.WriteMemory(
                entry.actor + kPlayerInitResistanceOffset,
                entry.init_resistance) ||
            !process.WriteMemory(
                entry.actor + kPlayerResistanceOffset, entry.init_resistance))
        {
            restored = false;
        }
    }
    if (restored || !process.IsConnected())
    {
        LogDiagnostic(
            "Sante Max: vie normale rendue a %u soldat(s).",
            static_cast<unsigned>(g_extended_health.size()));
        g_extended_health.clear();
    }
    return restored;
}

void UpdateExtendedHealth(
    TrainerProcess& process,
    const RadarSnapshot* snapshot,
    GameplaySettings& settings,
    GameplayStatus& status)
{
    const bool toggled = settings.extended_health_toggle_requested;
    settings.extended_health_toggle_requested = false;
    if (toggled)
    {
        settings.extended_health_enabled = !settings.extended_health_enabled;
        LogDiagnostic(
            "Sante Max: bascule -> %u.",
            settings.extended_health_enabled ? 1U : 0U);
    }

    if (!settings.extended_health_enabled)
    {
        g_peer_health_count = 0;
        status.extended_health = RestoreExtendedHealth(process)
            ? ExtendedHealthStatus::Disabled
            : ExtendedHealthStatus::WriteFailed;
        return;
    }
    if (!process.IsConnected() || !snapshot ||
        !IsSanePointer(snapshot->player_object_address))
    {
        status.extended_health = ExtendedHealthStatus::WaitingForPlayer;
        return;
    }

    const DWORD process_id = process.ProcessId();
    if (!g_extended_health.empty() &&
        g_extended_health.front().process_id != process_id)
    {
        g_extended_health.clear();
    }

    std::vector<std::uintptr_t> players;
    CollectLocalPlayers(process, *snapshot, players);
    if (players.empty())
    {
        status.extended_health = ExtendedHealthStatus::WaitingForPlayer;
        return;
    }

    std::size_t applied = 0;
    bool layout_ok = true;
    bool write_ok = true;
    for (const std::uintptr_t actor : players)
    {
        std::int32_t resistance = 0;
        std::int32_t init_resistance = 0;
        if (!ValidateExtendedHealthLayout(
                process, actor, resistance, init_resistance))
        {
            LogDiagnostic(
                "Sante Max: disposition refusee actor=%08X resistance=%d "
                "init=%d (aucune ecriture).",
                static_cast<unsigned>(actor), resistance, init_resistance);
            layout_ok = false;
            continue;
        }
        const bool known = std::any_of(
            g_extended_health.begin(), g_extended_health.end(),
            [&](const ExtendedHealthState& item)
            {
                return item.actor == actor;
            });
        if (!known)
        {
            if (init_resistance == kExtendedHealthValue)
            {
                // Deja etendu sans que nous l'ayons enregistre : ne jamais
                // memoriser 20000 comme « vie d'origine ».
                continue;
            }
            g_extended_health.push_back(
                ExtendedHealthState{
                    process_id, actor, resistance, init_resistance});
        }
        else if (toggled == false && init_resistance == kExtendedHealthValue)
        {
            // Deja etendu et deja enregistre : ne pas remplir la barre a
            // chaque image, elle doit descendre normalement.
            ++applied;
            continue;
        }
        const std::int32_t extended = kExtendedHealthValue;
        std::int32_t verify = 0;
        if (!process.WriteMemory(
                actor + kPlayerInitResistanceOffset, extended) ||
            !process.WriteMemory(
                actor + kPlayerResistanceOffset, extended) ||
            !process.ReadMemory(
                actor + kPlayerInitResistanceOffset, verify) ||
            verify != extended)
        {
            LogDiagnostic(
                "Sante Max: ecriture refusee actor=%08X.",
                static_cast<unsigned>(actor));
            write_ok = false;
            continue;
        }
        ++applied;
    }

    if (applied == 0)
    {
        status.extended_health = layout_ok
            ? ExtendedHealthStatus::WriteFailed
            : ExtendedHealthStatus::UnsupportedLayout;
        return;
    }
    // Publier la vie telle qu'elle est sur CETTE machine : c'est elle qui fait
    // foi, et chaque compagnon y alignera sa propre copie.
    g_peer_health_count = 0;
    for (const std::uintptr_t actor : players)
    {
        if (g_peer_health_count >= kPeerHealthSlots)
            break;
        std::uint16_t network_id = 0;
        std::int32_t resistance = 0;
        if (!process.ReadMemory(actor + 0x20U, network_id) ||
            network_id == 0 ||
            !process.ReadMemory(actor + kPlayerResistanceOffset, resistance) ||
            resistance <= 0)
        {
            continue;
        }
        g_peer_health_table[g_peer_health_count].network_id = network_id;
        g_peer_health_table[g_peer_health_count].resistance = resistance;
        ++g_peer_health_count;
    }

    status.extended_health = (layout_ok && write_ok)
        ? ExtendedHealthStatus::Active
        : ExtendedHealthStatus::WriteFailed;
    static ULONGLONG next_log = 0;
    const ULONGLONG now = GetTickCount64();
    if (now >= next_log)
    {
        next_log = now + 5000ULL;
        LogDiagnostic(
            "Sante Max: actif sur %u soldat(s) locaux, %u enregistre(s).",
            static_cast<unsigned>(applied),
            static_cast<unsigned>(g_extended_health.size()));
    }
}

void UpdateAbsolutePlayerProtection(
    TrainerProcess& process,
    const RadarSnapshot* snapshot,
    const GameplaySettings& settings,
    GameplayStatus& status)
{
    // Return-to-life deliberately leaves the native damage/death path intact
    // so both PCs see the same health loss and death. It only adds the F12
    // resynchronisation request after a real death; total protection is the
    // sole mode that owns the damage/death guards.
    const bool keep_host_alive = settings.absolute_player_protection_enabled;
    if (!keep_host_alive)
    {
        const bool death_restored = RestorePlayerDeathHook(process) &&
            RestorePlayerBaseDeathHook(process) &&
            RestorePlayerExplosionHook(process) &&
            RestoreProtectedFallHook(process) &&
            RestoreSquadPlayerGuards(process) &&
            RestoreNativeNoHitFlags(process);
        bool damage_restored = true;
        if (g_player_damage.applied && g_player_damage.protected_player != 0)
            damage_restored = RestorePlayerDamageHook(process);
        status.absolute_player_protection =
            (death_restored && damage_restored)
            ? AbsolutePlayerProtectionStatus::Disabled
            : AbsolutePlayerProtectionStatus::WriteFailed;
        return;
    }
    if (!process.IsConnected() || !snapshot ||
        !IsSanePointer(snapshot->player_object_address))
    {
        status.absolute_player_protection =
            AbsolutePlayerProtectionStatus::WaitingForPlayer;
        return;
    }

    RemoteModuleInfo module{};
    std::uintptr_t vtable = 0;
    std::uintptr_t hit = 0;
    std::uintptr_t die = 0;
    std::uintptr_t explode = 0;
    const std::uintptr_t player = snapshot->player_object_address;
    // The independent F12/return option always keeps only the currently
    // controlled host player alive. A stale squad pill must never silently
    // turn that separate option into whole-squad protection.
    const bool local_squad_scope =
        settings.absolute_player_protection_enabled &&
        settings.absolute_player_protection_scope ==
            AbsolutePlayerProtectionScope::WholeSquad;

    // A whole squad can contain both original H&D players and Ultimate Mod
    // players. They do not necessarily dispatch Hit/Die/Explode through the
    // same virtual table, so derive a complete *set* of entries before
    // installing guards. The earlier implementation inspected only `player`.
    if (local_squad_scope)
    {
        std::vector<std::uintptr_t> squad_players;
        std::vector<std::uintptr_t> squad_hits;
        std::vector<std::uintptr_t> squad_dies;
        std::vector<std::uintptr_t> squad_explodes;
        std::size_t local_player_count = 0;
        std::size_t remote_player_count = 0;
        if (!CollectLocalSquadVirtualEntries(
                process, *snapshot, squad_players,
                squad_hits, squad_dies, squad_explodes,
                local_player_count, remote_player_count))
        {
            status.absolute_player_protection =
                AbsolutePlayerProtectionStatus::UnsupportedRevision;
            return;
        }
        std::size_t native_no_hit_live = 0;
        if (!ApplyNativeNoHitFlags(
                process, squad_players, native_no_hit_live))
        {
            status.absolute_player_protection =
                AbsolutePlayerProtectionStatus::WriteFailed;
            return;
        }

        // Do not leave a strict current-player hook on top of the first squad
        // function. V95 : l'invisibilite ne pose plus de garde partagee, donc
        // le cas `protected_player == 0` ci-dessous ne peut plus se produire;
        // il est conserve uniquement comme filet de securite.
        if (g_player_damage.applied && g_player_damage.protected_player != 0 &&
            !RestorePlayerDamageHook(process))
        {
            status.absolute_player_protection =
                AbsolutePlayerProtectionStatus::WriteFailed;
            return;
        }
        if (g_player_death.applied && !RestorePlayerDeathHook(process))
        {
            status.absolute_player_protection =
                AbsolutePlayerProtectionStatus::WriteFailed;
            return;
        }
        if (g_player_explosion.applied && !RestorePlayerExplosionHook(process))
        {
            status.absolute_player_protection =
                AbsolutePlayerProtectionStatus::WriteFailed;
            return;
        }

        if (g_player_damage.applied && g_player_damage.protected_player == 0)
        {
            squad_hits.erase(std::remove(
                squad_hits.begin(), squad_hits.end(), g_player_damage.function),
                squad_hits.end());
        }
        const auto restore_damage = [&](PlayerDamagePatchState& guard)
        {
            return RestorePlayerDamageHookState(process, guard);
        };
        const auto restore_death = [&](PlayerDeathPatchState& guard)
        {
            return RestoreDeathHook(process, guard, "C_player::Die squad");
        };
        const auto restore_explosion = [&](PlayerExplosionPatchState& guard)
        {
            return RestoreDeathHook(process, guard, "C_player::Explode squad");
        };
        if (!RemoveStaleSquadGuards(
                process, g_squad_player_damage, squad_hits, restore_damage) ||
            !RemoveStaleSquadGuards(
                process, g_squad_player_death, squad_dies, restore_death) ||
            !RemoveStaleSquadGuards(
                process, g_squad_player_explosion, squad_explodes, restore_explosion))
        {
            status.absolute_player_protection =
                AbsolutePlayerProtectionStatus::WriteFailed;
            return;
        }

        for (const std::uintptr_t function : squad_hits)
        {
            if (ContainsFunction(g_squad_player_damage, function))
                continue;
            PlayerDamagePatchState guard{};
            if (!InstallPlayerDamageHookState(
                    process, function, guard, player, true))
            {
                status.absolute_player_protection =
                    AbsolutePlayerProtectionStatus::UnsupportedRevision;
                return;
            }
            g_squad_player_damage.push_back(guard);
        }
        for (const std::uintptr_t function : squad_dies)
        {
            if (ContainsFunction(g_squad_player_death, function))
                continue;
            PlayerDeathPatchState guard{};
            if (!InstallDeathHook(
                    process, function, player, guard,
                    std::array<std::uint8_t, kHumanDieHookPatchSize>{
                        0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xBC, 0x00, 0x00, 0x00},
                    "C_player::Die squad", true))
            {
                status.absolute_player_protection =
                    AbsolutePlayerProtectionStatus::UnsupportedRevision;
                return;
            }
            g_squad_player_death.push_back(guard);
        }
        for (const std::uintptr_t function : squad_explodes)
        {
            if (ContainsFunction(g_squad_player_explosion, function))
                continue;
            PlayerExplosionPatchState guard{};
            if (!InstallDeathHook(
                    process, function, player, guard,
                    std::array<std::uint8_t, kPlayerExplodeHookPatchSize>{
                        0x83, 0xEC, 0x44, 0x53, 0x55, 0x56, 0x8B, 0xF1, 0x33, 0xC9},
                    "C_player::Explode squad", true, true))
            {
                status.absolute_player_protection =
                    AbsolutePlayerProtectionStatus::UnsupportedRevision;
                return;
            }
            g_squad_player_explosion.push_back(guard);
        }

        RemoteModuleInfo squad_module{};
        if (!process.GetMainModuleInfo(squad_module))
        {
            status.absolute_player_protection =
                AbsolutePlayerProtectionStatus::UnsupportedRevision;
            return;
        }
        const std::uintptr_t base_die = squad_module.base_address + kHumanDieRva;
        if (g_player_base_death.applied &&
            (g_player_base_death.process_id != process.ProcessId() ||
             g_player_base_death.function != base_die ||
             !g_player_base_death.local_squad_scope) &&
            !RestorePlayerBaseDeathHook(process))
        {
            status.absolute_player_protection =
                AbsolutePlayerProtectionStatus::WriteFailed;
            return;
        }
        if (!g_player_base_death.applied &&
            !InstallPlayerBaseDeathHook(process, base_die, player, true))
        {
            status.absolute_player_protection =
                AbsolutePlayerProtectionStatus::UnsupportedRevision;
            return;
        }
        if (!EnsureProtectedFallHook(process, player, true))
        {
            status.absolute_player_protection =
                AbsolutePlayerProtectionStatus::WriteFailed;
            return;
        }
        static ULONGLONG next_squad_heartbeat = 0;
        const ULONGLONG now = GetTickCount64();
        if (now >= next_squad_heartbeat)
        {
            next_squad_heartbeat = now + 5000ULL;
            LogDiagnostic(
                "Absolute protection SQUAD HEARTBEAT: local_players=%u "
                "remote_players=%u hit_functions=%u die_functions=%u "
                "explode_functions=%u active_hit_guards=%u "
                "active_die_guards=%u active_explode_guards=%u "
                "native_no_hit=%u/%u.",
                static_cast<unsigned>(local_player_count),
                static_cast<unsigned>(remote_player_count),
                static_cast<unsigned>(squad_hits.size() +
                    ((g_player_damage.applied &&
                      g_player_damage.protected_player == 0) ? 1U : 0U)),
                static_cast<unsigned>(squad_dies.size()),
                static_cast<unsigned>(squad_explodes.size()),
                static_cast<unsigned>(g_squad_player_damage.size() +
                    ((g_player_damage.applied &&
                      g_player_damage.protected_player == 0) ? 1U : 0U)),
                static_cast<unsigned>(g_squad_player_death.size()),
                static_cast<unsigned>(g_squad_player_explosion.size()),
                static_cast<unsigned>(native_no_hit_live),
                static_cast<unsigned>(squad_players.size()));
        }
        status.absolute_player_protection = AbsolutePlayerProtectionStatus::Active;
        return;
    }

    // The scope was changed from squad back to the controlled player. Remove
    // every derived-class guard before returning to the original one-target
    // path below.
    if (!RestoreSquadPlayerGuards(process))
    {
        status.absolute_player_protection =
            AbsolutePlayerProtectionStatus::WriteFailed;
        return;
    }

    // Only the explicit total-protection option owns the native immortality
    // flag. The separate return-to-life option keeps its historical death
    // interception but must not silently toggle the game's immortality cheat.
    std::size_t native_no_hit_live = 0;
    if (settings.absolute_player_protection_enabled)
    {
        if (!ApplyNativeNoHitFlags(
                process, std::vector<std::uintptr_t>{player},
                native_no_hit_live))
        {
            status.absolute_player_protection =
                AbsolutePlayerProtectionStatus::WriteFailed;
            return;
        }
    }
    else if (!RestoreNativeNoHitFlags(process))
    {
        status.absolute_player_protection =
            AbsolutePlayerProtectionStatus::WriteFailed;
        return;
    }

    if (!process.GetMainModuleInfo(module) ||
        !process.ReadMemory(player, vtable) || !IsSanePointer(vtable) ||
        !process.ReadMemory(vtable + kPlayerHitVtableOffset, hit) ||
        !process.ReadMemory(vtable + kPlayerDieVtableOffset, die) ||
        !process.ReadMemory(vtable + kPlayerExplodeVtableOffset, explode))
    {
        status.absolute_player_protection =
            AbsolutePlayerProtectionStatus::UnsupportedRevision;
        return;
    }
    const std::uintptr_t base_die = module.base_address + kHumanDieRva;
    if (
        hit < module.base_address || hit + 5U > module.base_address + module.image_size ||
        die < module.base_address ||
        die + kHumanDieHookPatchSize > module.base_address + module.image_size ||
        explode < module.base_address ||
        explode + kHumanDieHookPatchSize > module.base_address + module.image_size ||
        base_die < module.base_address ||
        base_die + kHumanDieHookPatchSize > module.base_address + module.image_size)
    {
        status.absolute_player_protection =
            AbsolutePlayerProtectionStatus::UnsupportedRevision;
        return;
    }

    // A shared guard installed by enemy invisibility already protects this
    // player from Hit. Otherwise own a strict single-player damage guard.
    if (g_player_damage.applied &&
        (g_player_damage.process_id != process.ProcessId() ||
         g_player_damage.function != hit ||
         (!local_squad_scope && g_player_damage.protected_player != 0 &&
          g_player_damage.protected_player != player) ||
         g_player_damage.local_squad_scope != local_squad_scope))
    {
        if (!RestorePlayerDamageHook(process))
        {
            status.absolute_player_protection =
                AbsolutePlayerProtectionStatus::WriteFailed;
            return;
        }
    }
    if (!g_player_damage.applied && !InstallPlayerDamageHook(
            process, hit, player, local_squad_scope))
    {
        status.absolute_player_protection =
            AbsolutePlayerProtectionStatus::UnsupportedRevision;
        return;
    }

    if (g_player_death.applied &&
        (g_player_death.process_id != process.ProcessId() ||
         g_player_death.function != die ||
         g_player_death.local_squad_scope != local_squad_scope))
    {
        if (!RestorePlayerDeathHook(process))
        {
            status.absolute_player_protection =
                AbsolutePlayerProtectionStatus::WriteFailed;
            return;
        }
    }
    if (!g_player_death.applied && !InstallPlayerDeathHook(
            process, die, player, local_squad_scope))
    {
        status.absolute_player_protection =
            AbsolutePlayerProtectionStatus::UnsupportedRevision;
        return;
    }
    if (!local_squad_scope && g_player_death.protected_player != player &&
        !process.WriteMemory(
            g_player_death.remote + kProtectionRemoteTargetOffset,
            static_cast<std::uint32_t>(player)))
    {
        status.absolute_player_protection =
            AbsolutePlayerProtectionStatus::WriteFailed;
        return;
    }
    g_player_death.protected_player = player;

    // A peer-controlled enemy can deliver NM_HUMAN_DIE through the base
    // routine directly. Keep this second, strict guard armed as well: it
    // stops that inbound path without touching any other player.
    if (g_player_base_death.applied &&
        (g_player_base_death.process_id != process.ProcessId() ||
         g_player_base_death.function != base_die ||
         g_player_base_death.local_squad_scope != local_squad_scope))
    {
        if (!RestorePlayerBaseDeathHook(process))
        {
            status.absolute_player_protection =
                AbsolutePlayerProtectionStatus::WriteFailed;
            return;
        }
    }
    if (!g_player_base_death.applied &&
        !InstallPlayerBaseDeathHook(process, base_die, player, local_squad_scope))
    {
        status.absolute_player_protection =
            AbsolutePlayerProtectionStatus::UnsupportedRevision;
        return;
    }
    if (!local_squad_scope && g_player_base_death.protected_player != player &&
        !process.WriteMemory(
            g_player_base_death.remote + kProtectionRemoteTargetOffset,
            static_cast<std::uint32_t>(player)))
    {
        status.absolute_player_protection =
            AbsolutePlayerProtectionStatus::WriteFailed;
        return;
    }
    g_player_base_death.protected_player = player;

    // Explosions have their own C_player entry and must be stopped before
    // C_human::Explode decreases resistance or starts the skeleton/death pose.
    if (g_player_explosion.applied &&
        (g_player_explosion.process_id != process.ProcessId() ||
         g_player_explosion.function != explode ||
         g_player_explosion.local_squad_scope != local_squad_scope))
    {
        if (!RestorePlayerExplosionHook(process))
        {
            status.absolute_player_protection =
                AbsolutePlayerProtectionStatus::WriteFailed;
            return;
        }
    }
    if (!g_player_explosion.applied &&
        !InstallPlayerExplosionHook(process, explode, player, local_squad_scope))
    {
        status.absolute_player_protection =
            AbsolutePlayerProtectionStatus::UnsupportedRevision;
        return;
    }
    if (!local_squad_scope && g_player_explosion.protected_player != player &&
        !process.WriteMemory(
            g_player_explosion.remote + kProtectionRemoteTargetOffset,
            static_cast<std::uint32_t>(player)))
    {
        status.absolute_player_protection =
            AbsolutePlayerProtectionStatus::WriteFailed;
        return;
    }
    g_player_explosion.protected_player = player;

    if (!EnsureProtectedFallHook(process, player, local_squad_scope))
    {
        status.absolute_player_protection =
            AbsolutePlayerProtectionStatus::WriteFailed;
        LogDiagnostic(
            "Absolute protection: fall-impact guard unavailable player=%08X.",
            static_cast<unsigned>(player));
        return;
    }

    static ULONGLONG next_heartbeat = 0;
    const ULONGLONG now = GetTickCount64();
    if (now >= next_heartbeat)
    {
        next_heartbeat = now + 5000ULL;
        std::array<std::uint8_t, 5> hit_bytes{};
        std::array<std::uint8_t, kHumanDieHookPatchSize> die_bytes{};
        std::array<std::uint8_t, kHumanDieHookPatchSize> base_die_bytes{};
        std::array<std::uint8_t, kPlayerExplodeHookPatchSize> explosion_bytes{};
        const bool hit_ok = process.ReadMemory(
            g_player_damage.function, hit_bytes.data(), hit_bytes.size());
        const bool die_ok = process.ReadMemory(
            g_player_death.function, die_bytes.data(), die_bytes.size());
        const bool base_die_ok = process.ReadMemory(
            g_player_base_death.function,
            base_die_bytes.data(), base_die_bytes.size());
        const bool explosion_ok = process.ReadMemory(
            g_player_explosion.function,
            explosion_bytes.data(), explosion_bytes.size());
        LogDiagnostic(
            "Absolute protection HEARTBEAT: player=%08X hit_live=%u "
            "network_die_live=%u base_die_live=%u explosion_live=%u "
            "native_no_hit=%u C_player::Die=%08X C_human::Die=%08X "
            "C_player::Explode=%08X.",
            static_cast<unsigned>(player),
            (hit_ok && hit_bytes[0] == 0xE9) ? 1U : 0U,
            (die_ok && die_bytes[0] == 0xE9) ? 1U : 0U,
            (base_die_ok && base_die_bytes[0] == 0xE9) ? 1U : 0U,
            (explosion_ok && explosion_bytes[0] == 0xE9) ? 1U : 0U,
            static_cast<unsigned>(native_no_hit_live),
            static_cast<unsigned>(g_player_death.function),
            static_cast<unsigned>(g_player_base_death.function),
            static_cast<unsigned>(g_player_explosion.function));
    }
    status.absolute_player_protection = g_player_damage.protected_player == 0
        ? AbsolutePlayerProtectionStatus::SharedDamageGuard
        : AbsolutePlayerProtectionStatus::Active;
}

void ObserveReviveTargetHistory(
    TrainerProcess& process,
    const RadarSnapshot* snapshot)
{
    const DWORD process_id = process.ProcessId();
    if (g_revive_target_history.process_id != process_id)
    {
        g_revive_target_history = {};
        g_revive_target_history.process_id = process_id;
    }
    if (!process.IsConnected())
        return;

    // Check the remembered actor BEFORE accepting the new radar selection.
    // This is the essential ordering: the game frequently has already chosen
    // a living ally by the time the next snapshot reaches the trainer.
    if (IsSanePointer(g_revive_target_history.last_controlled_player))
    {
        std::uint32_t previous_stay = 0;
        if (process.ReadMemory(
                g_revive_target_history.last_controlled_player +
                    kHumanStayModeOffset,
                previous_stay) &&
            previous_stay == kHumanStayModeDead &&
            g_revive_target_history.last_dead_controlled_player !=
                g_revive_target_history.last_controlled_player)
        {
            g_revive_target_history.last_dead_controlled_player =
                g_revive_target_history.last_controlled_player;
            LogDiagnostic(
                "F10 TARGET: remembered controlled player died player=%08X.",
                static_cast<unsigned>(
                    g_revive_target_history.last_dead_controlled_player));
        }
    }

    if (snapshot && IsSanePointer(snapshot->player_object_address))
        g_revive_target_history.last_controlled_player =
            snapshot->player_object_address;
}

bool RestorePlayerReviveHook(TrainerProcess& process)
{
    if (!g_player_revive.applied)
        return true;

    const PlayerRevivePatchState previous = g_player_revive;
    if (!process.IsConnected() || process.ProcessId() != previous.process_id)
    {
        g_player_revive = {};
        return true;
    }

    std::array<std::uint8_t, kNoclipHookPatchSize> current{};
    bool restored = process.ReadMemory(
        previous.function, current.data(), current.size());
    if (restored && current != previous.original)
    {
        restored = current == previous.patch && process.WriteProtectedMemory(
            previous.function, previous.original.data(), previous.original.size());
    }
    if (restored)
    {
        restored = process.ReadMemory(
            previous.function, current.data(), current.size()) &&
            current == previous.original;
    }

    bool page_idle = previous.remote == 0;
    if (restored && !page_idle)
    {
        for (unsigned attempt = 0; attempt < 100 && !page_idle; ++attempt)
        {
            bool executing = true;
            page_idle = process.IsAnyThreadExecutingRange(
                previous.remote, previous.remote_size, executing) && !executing;
            if (!page_idle)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    if (!restored || !page_idle ||
        (previous.remote != 0 && !process.FreeRemoteMemory(previous.remote)))
    {
        LogDiagnostic(
            "F10 native revive: hook restore failed fn=%08X.",
            static_cast<unsigned>(previous.function));
        return false;
    }

    LogDiagnostic(
        "F10 native revive: game-thread hook removed fn=%08X.",
        static_cast<unsigned>(previous.function));
    g_player_revive = {};
    return true;
}

bool QueueNativePlayerRevive(
    TrainerProcess& process,
    std::uintptr_t player,
    std::uintptr_t current_player,
    bool rebuild_living_player = false)
{
    if (!process.IsConnected() || !IsSanePointer(player) ||
        !IsSanePointer(current_player) ||
        (player == current_player && !rebuild_living_player))
        return false;
    if (g_player_revive.applied)
    {
        // Do not replace a live patch on the same function. The request will
        // finish or time out on the next game update.
        return g_player_revive.process_id == process.ProcessId() &&
            g_player_revive.target == player &&
            g_player_revive.source == current_player;
    }
    // Noclip owns the same C_human::Tick prologue while flying. It is safer to
    // ask the player to leave noclip first than to stack two detours there.
    if (g_noclip_hook.applied &&
        g_noclip_hook.process_id == process.ProcessId())
    {
        LogDiagnostic("F10 native revive: unavailable while noclip tick hook is active.");
        return false;
    }

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        module.image_size <= kHumanTickRva + kNoclipHookPatchSize)
    {
        return false;
    }

    PlayerRevivePatchState next{};
    next.process_id = process.ProcessId();
    next.function = module.base_address + kHumanTickRva;
    next.target = player;
    next.source = current_player;
    next.remote_size = kPlayerReviveRemoteSize;
    constexpr std::array<std::uint8_t, kNoclipHookPatchSize> kExpected{
        0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x6C, 0x03, 0x00, 0x00};
    if (!process.ReadMemory(
            next.function, next.original.data(), next.original.size()) ||
        next.original != kExpected)
    {
        LogDiagnostic(
            "F10 native revive: C_human::Tick signature mismatch at %08X.",
            static_cast<unsigned>(next.function));
        return false;
    }
    // SetActive is the game's native scene/model transition. It takes two
    // stack arguments and returns with `ret 8`: (active, restore_resistance).
    // Validate the virtual slot for both actors before the game-thread job
    // uses either one.
    std::uintptr_t target_vtable = 0;
    std::uintptr_t source_vtable = 0;
    std::uintptr_t target_set_active = 0;
    std::uintptr_t source_set_active = 0;
    constexpr std::array<std::uint8_t, 9> kSetActiveExpected{
        0x53, 0x8B, 0x5C, 0x24, 0x08, 0x56, 0x8B, 0xF1, 0x57};
    std::array<std::uint8_t, kSetActiveExpected.size()> target_signature{};
    std::array<std::uint8_t, kSetActiveExpected.size()> source_signature{};
    if (!process.ReadMemory(player, target_vtable) ||
        !process.ReadMemory(current_player, source_vtable) ||
        !IsSanePointer(target_vtable) || !IsSanePointer(source_vtable) ||
        !process.ReadMemory(
            target_vtable + kPlayerSetActiveVtableOffset,
            target_set_active) ||
        !process.ReadMemory(
            source_vtable + kPlayerSetActiveVtableOffset,
            source_set_active) ||
        !process.IsReadableCodeTarget(target_set_active) ||
        !process.IsReadableCodeTarget(source_set_active) ||
        !process.ReadMemory(
            target_set_active,
            target_signature.data(), target_signature.size()) ||
        !process.ReadMemory(
            source_set_active,
            source_signature.data(), source_signature.size()) ||
        target_signature != kSetActiveExpected ||
        source_signature != kSetActiveExpected)
    {
        LogDiagnostic(
            "F10 native revive: C_player::SetActive unavailable "
            "target=%08X source=%08X.",
            static_cast<unsigned>(player),
            static_cast<unsigned>(current_player));
        return false;
    }

    next.remote = process.AllocateRemoteMemory(next.remote_size);
    if (!IsSanePointer(next.remote))
        return false;

    const auto relative32 = [](std::uintptr_t target, std::uintptr_t next_ip)
    {
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<std::intptr_t>(target) -
            static_cast<std::intptr_t>(next_ip)));
    };
    std::vector<std::uint8_t> code;
    const auto byte = [&](std::uint8_t value) { code.push_back(value); };
    const auto dword = [&](std::uint32_t value)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(value));
        std::memcpy(code.data() + offset, &value, sizeof(value));
    };
    const auto remote_slot = [&](std::size_t offset)
    {
        dword(static_cast<std::uint32_t>(next.remote + offset));
    };
    const auto near_jump = [&](std::uint8_t condition)
    {
        byte(0x0F); byte(condition);
        const std::size_t displacement = code.size();
        dword(0);
        return displacement;
    };
    const auto patch_jump = [&](std::size_t displacement, std::size_t target)
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            target - (displacement + sizeof(std::int32_t)));
        std::memcpy(code.data() + displacement, &relative, sizeof(relative));
    };

    // This one-shot trampoline runs at the start of a C_human Tick, therefore
    // on the game's own update thread. It mirrors the engine's normal squad
    // switch: deactivate the current player, make the target live, then call
    // SetActive(true, true) on it. For F12 source and target are deliberately
    // the same living player: the native off/on scene transition rebuilds the
    // local model after an older explosion had left only its skeleton.
    byte(0x9C);                              // pushfd
    byte(0x60);                              // pushad
    byte(0x83); byte(0x3D); remote_slot(kPlayerReviveStateOffset);
    byte(static_cast<std::uint8_t>(kPlayerReviveQueued));
    const std::size_t not_queued = near_jump(0x85); // jne done
    byte(0xA1); remote_slot(kPlayerReviveTargetOffset); // mov eax,[target]
    byte(0x85); byte(0xC0);                  // test eax,eax
    const std::size_t invalid_target = near_jump(0x84); // je rejected
    byte(0x8B); byte(0x10);                  // mov edx,[target vtable]
    byte(0x85); byte(0xD2);
    const std::size_t invalid_target_vtable = near_jump(0x84);
    byte(0x8B); byte(0x92); dword(static_cast<std::uint32_t>(
        kPlayerSetActiveVtableOffset));      // mov edx,[vtable+6C]
    byte(0x85); byte(0xD2);
    const std::size_t invalid_target_set_active = near_jump(0x84);
    byte(0xA3); remote_slot(kPlayerReviveTargetOffset + 0x08); // save target
    byte(0xA1); remote_slot(kPlayerReviveSourceOffset); // mov eax,[source]
    byte(0x85); byte(0xC0);
    const std::size_t invalid_source = near_jump(0x84);
    byte(0x8B); byte(0x10);                  // mov edx,[source vtable]
    byte(0x85); byte(0xD2);
    const std::size_t invalid_source_vtable = near_jump(0x84);
    byte(0x8B); byte(0x92); dword(static_cast<std::uint32_t>(
        kPlayerSetActiveVtableOffset));      // mov edx,[vtable+6C]
    byte(0x85); byte(0xD2);
    const std::size_t invalid_source_set_active = near_jump(0x84);
    byte(0xA3); remote_slot(kPlayerReviveSourceOffset + 0x08); // save source
    // Exact normal transition, source SetActive(false, false).
    byte(0x6A); byte(0x00);                  // restore_resistance = false
    byte(0x6A); byte(0x00);                  // active = false
    byte(0x8B); byte(0xC8);                  // mov ecx,eax (source)
    byte(0xFF); byte(0xD2);                  // call source SetActive, ret 8
    byte(0xA1); remote_slot(kPlayerReviveTargetOffset + 0x08); // target
    byte(0xC7); byte(0x80); dword(static_cast<std::uint32_t>(
        kHumanStayModeOffset)); dword(kHumanStayModeAlive);
    byte(0xC7); byte(0x80); dword(static_cast<std::uint32_t>(
        kPlayerModeOffset)); dword(kPlayerModeProgram);
    byte(0x8B); byte(0x10);                  // mov edx,[target vtable]
    byte(0x8B); byte(0x92); dword(static_cast<std::uint32_t>(
        kPlayerSetActiveVtableOffset));      // mov edx,[vtable+6C]
    // Exact native activation: SetActive(true, true). The final true asks the
    // engine to initialise its own resistance after the visual transition.
    byte(0x6A); byte(0x01);                  // restore_resistance = true
    byte(0x6A); byte(0x01);                  // active = true
    byte(0x8B); byte(0xC8);                  // mov ecx,eax (target)
    byte(0xFF); byte(0xD2);                  // call target SetActive, ret 8
    byte(0xC7); byte(0x05); remote_slot(kPlayerReviveStateOffset);
    dword(kPlayerReviveCompleted);
    byte(0xE9);
    const std::size_t success_done = code.size();
    dword(0);

    const std::size_t rejected = code.size();
    byte(0xC7); byte(0x05); remote_slot(kPlayerReviveStateOffset);
    dword(kPlayerReviveRejected);
    const std::size_t done = code.size();
    patch_jump(not_queued, done);
    patch_jump(invalid_target, rejected);
    patch_jump(invalid_target_vtable, rejected);
    patch_jump(invalid_target_set_active, rejected);
    patch_jump(invalid_source, rejected);
    patch_jump(invalid_source_vtable, rejected);
    patch_jump(invalid_source_set_active, rejected);
    patch_jump(success_done, done);
    byte(0x61);                              // popad
    byte(0x9D);                              // popfd
    code.insert(code.end(), kExpected.begin(), kExpected.end());
    byte(0xE9);
    dword(relative32(
        next.function + kNoclipHookPatchSize,
        next.remote + code.size() + sizeof(std::uint32_t)));

    if (code.size() >= kPlayerReviveStateOffset ||
        !process.WriteMemory(next.remote, code.data(), code.size()) ||
        !process.WriteMemory(
            next.remote + kPlayerReviveTargetOffset,
            static_cast<std::uint32_t>(player)) ||
        !process.WriteMemory(
            next.remote + kPlayerReviveSourceOffset,
            static_cast<std::uint32_t>(current_player)) ||
        !process.WriteMemory(
            next.remote + kPlayerReviveStateOffset,
            kPlayerReviveQueued))
    {
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }

    next.patch.fill(0x90);
    next.patch[0] = 0xE9;
    const std::uint32_t hook_relative = relative32(
        next.remote, next.function + sizeof(std::uint32_t) + 1U);
    std::memcpy(next.patch.data() + 1, &hook_relative, sizeof(hook_relative));
    next.queued_at = GetTickCount64();
    next.applied = true;
    g_player_revive = next;
    if (!process.WriteProtectedMemory(
            next.function, next.patch.data(), next.patch.size()))
    {
        (void)RestorePlayerReviveHook(process);
        return false;
    }
    LogDiagnostic(
        "F10 native revive: queued target=%08X source=%08X "
        "SetActive=%08X/%08X tick=%08X.",
        static_cast<unsigned>(player), static_cast<unsigned>(current_player),
        static_cast<unsigned>(target_set_active),
        static_cast<unsigned>(source_set_active),
        static_cast<unsigned>(next.function));
    return true;
}

void PollNativePlayerRevive(TrainerProcess& process, GameplayStatus& status)
{
    if (!g_player_revive.applied)
        return;
    if (!process.IsConnected() ||
        process.ProcessId() != g_player_revive.process_id)
    {
        g_player_revive = {};
        status.revive_current_player = ReviveCurrentPlayerStatus::WriteFailed;
        return;
    }

    std::uint32_t state = 0;
    if (!process.ReadMemory(
            g_player_revive.remote + kPlayerReviveStateOffset, state))
    {
        status.revive_current_player = ReviveCurrentPlayerStatus::WriteFailed;
        (void)RestorePlayerReviveHook(process);
        return;
    }
    if (state == kPlayerReviveQueued)
    {
        if (GetTickCount64() - g_player_revive.queued_at <=
            kPlayerReviveTimeoutMs)
        {
            status.revive_current_player = ReviveCurrentPlayerStatus::Queued;
            return;
        }
        LogDiagnostic("F10 native revive: timed out waiting for game tick.");
        status.revive_current_player = ReviveCurrentPlayerStatus::WriteFailed;
        (void)RestorePlayerReviveHook(process);
        return;
    }

    const std::uintptr_t player = g_player_revive.target;
    const bool completed = state == kPlayerReviveCompleted;
    LogDiagnostic(
        "F10 native revive: completed=%u player=%08X state=%u.",
        completed ? 1U : 0U, static_cast<unsigned>(player), state);
    status.revive_current_player = completed
        ? ReviveCurrentPlayerStatus::Revived
        : ReviveCurrentPlayerStatus::WriteFailed;
    if (completed)
    {
        // Tell only the already-running Client companion that the native host
        // revive completed.  The actor id, unlike an address, means the same
        // soldier on the other PC.  The companion uses the sequence as a
        // one-shot visual/life resynchronisation request.
        g_peer_life_target_network_id = ReadActorNetworkId(process, player);
        ++g_peer_revive_sequence;
        if (g_peer_revive_sequence == 0)
            ++g_peer_revive_sequence;
        PublishPeerVisualCommand(g_network_position_mask.remote_visual_hidden, true);
        LogDiagnostic(
            "F10 native revive: Client sync sequence=%u net_id=%u.",
            g_peer_revive_sequence,
            static_cast<unsigned>(g_peer_life_target_network_id));
    }
    if (completed && g_revive_target_history.last_dead_controlled_player == player)
        g_revive_target_history.last_dead_controlled_player = 0;
    (void)RestorePlayerReviveHook(process);
}

void TryReviveCurrentPlayer(
    TrainerProcess& process,
    const RadarSnapshot* snapshot,
    GameplaySettings& settings,
    GameplayStatus& status)
{
    if (!settings.revive_current_player_requested)
        return;
    settings.revive_current_player_requested = false;
    LogDiagnostic(
        "F10 EXECUTE: connected=%u snapshot=%u player=%08X.",
        process.IsConnected() ? 1U : 0U,
        snapshot ? 1U : 0U,
        static_cast<unsigned>(snapshot ? snapshot->player_object_address : 0));
    if (!process.IsConnected())
    {
        status.revive_current_player = ReviveCurrentPlayerStatus::WaitingForPlayer;
        LogDiagnostic("F10 RESULT: hde.exe is not connected.");
        return;
    }
    const std::uintptr_t player =
        IsSanePointer(g_revive_target_history.last_dead_controlled_player)
        ? g_revive_target_history.last_dead_controlled_player
        : (snapshot && IsSanePointer(snapshot->player_object_address)
            ? snapshot->player_object_address : 0);
    if (!IsSanePointer(player))
    {
        status.revive_current_player = ReviveCurrentPlayerStatus::WaitingForPlayer;
        LogDiagnostic("F10 RESULT: no remembered or current player is readable.");
        return;
    }
    LogDiagnostic(
        "F10 TARGET: selected=%08X remembered_dead=%08X current=%08X.",
        static_cast<unsigned>(player),
        static_cast<unsigned>(g_revive_target_history.last_dead_controlled_player),
        static_cast<unsigned>(snapshot ? snapshot->player_object_address : 0));
    std::uint32_t stay_mode = 0;
    if (!process.ReadMemory(player + kHumanStayModeOffset, stay_mode))
    {
        status.revive_current_player = ReviveCurrentPlayerStatus::WriteFailed;
        LogDiagnostic("F10 RESULT: stay-mode read failed player=%08X.",
            static_cast<unsigned>(player));
        return;
    }
    if (stay_mode != kHumanStayModeDead)
    {
        status.revive_current_player = ReviveCurrentPlayerStatus::PlayerAlive;
        LogDiagnostic(
            "F10 RESULT: player=%08X is not dead (stay_mode=%u).",
            static_cast<unsigned>(player), stay_mode);
        return;
    }

    // The game-thread job now performs the same two-actor transition used for
    // a normal squad switch. The currently controlled soldier is the source;
    // the remembered dead soldier is the target and becomes controlled again.
    const std::uintptr_t current_player = snapshot &&
        IsSanePointer(snapshot->player_object_address)
        ? snapshot->player_object_address : 0;
    const bool queued = QueueNativePlayerRevive(
        process, player, current_player);
    status.revive_current_player = queued
        ? ReviveCurrentPlayerStatus::Queued
        : ReviveCurrentPlayerStatus::WriteFailed;
    LogDiagnostic(
        "F10 RESULT: full native revive player=%08X queued=%u.",
        static_cast<unsigned>(player), queued ? 1U : 0U);
}

void TryRepairCurrentPlayer(
    TrainerProcess& process,
    const RadarSnapshot* snapshot,
    GameplaySettings& settings,
    GameplayStatus& status)
{
    if (!settings.repair_current_player_requested)
        return;
    settings.repair_current_player_requested = false;
    const std::uintptr_t player = snapshot ? snapshot->player_object_address : 0;
    LogDiagnostic(
        "F12 REPAIR: connected=%u player=%08X.",
        process.IsConnected() ? 1U : 0U, static_cast<unsigned>(player));
    if (!process.IsConnected() || !IsSanePointer(player))
    {
        status.revive_current_player = ReviveCurrentPlayerStatus::WaitingForPlayer;
        LogDiagnostic("F12 REPAIR RESULT: current player unavailable.");
        return;
    }

    // In Return-to-life mode a native death normally transfers control to a
    // surviving teammate before the user can press F12. The visible skeleton
    // belongs to the remembered prior soldier, not to the new current actor.
    // Route F12 to the existing native two-actor revive path on the next
    // frame so that exact soldier is restored locally and the Client receives
    // the same revive sequence.
    if (settings.remote_life_mirror_enabled &&
        IsSanePointer(g_revive_target_history.last_dead_controlled_player))
    {
        settings.revive_current_player_requested = true;
        LogDiagnostic(
            "F12 RETURN-LIFE: redirected to remembered dead player=%08X.",
            static_cast<unsigned>(
                g_revive_target_history.last_dead_controlled_player));
        return;
    }

    std::uint32_t stay_mode = 0;
    if (!process.ReadMemory(player + kHumanStayModeOffset, stay_mode))
    {
        status.revive_current_player = ReviveCurrentPlayerStatus::WriteFailed;
        LogDiagnostic("F12 REPAIR RESULT: stay-mode read failed player=%08X.",
            static_cast<unsigned>(player));
        return;
    }
    if (stay_mode == kHumanStayModeDead)
    {
        // A genuine death needs the existing remembered-target path.  Queue it
        // for the next gameplay frame rather than pretending an active-model
        // repair can revive a dead actor.
        settings.revive_current_player_requested = true;
        LogDiagnostic("F12 REPAIR: player is dead; redirected to native F10 revive.");
        return;
    }

    // A living player rendered as a skeleton has no death state to revive.
    // Re-run only the game's own active scene/model transition on its update
    // thread; no network death/message is emitted by SetActive.
    const bool queued = QueueNativePlayerRevive(
        process, player, player, true);
    status.revive_current_player = queued
        ? ReviveCurrentPlayerStatus::Queued
        : ReviveCurrentPlayerStatus::WriteFailed;
    LogDiagnostic(
        "F12 REPAIR RESULT: native scene rebuild player=%08X queued=%u.",
        static_cast<unsigned>(player), queued ? 1U : 0U);
}

bool RestoreEnemyInvisibility(TrainerProcess& process)
{
    // Every removal is logged: an invisibility that silently stopped being
    // installed is exactly what a LAN test could not explain, because the
    // panel kept saying nothing and the journal recorded only installs.
    if (g_enemy_invisibility.applied)
    {
        LogDiagnostic(
            "Enemy invisibility: REMOVING visual hook function=%08X "
            "protected=%08X scope=%u.",
            static_cast<unsigned>(g_enemy_invisibility.function),
            static_cast<unsigned>(g_enemy_invisibility.protected_player),
            static_cast<unsigned>(g_enemy_invisibility.scope));
    }
    // V95 : l'invisibilite ne possede plus aucune garde de degats. Le crochet
    // C_player::Hit appartient desormais exclusivement a Protection reseau
    // totale, donc rien n'est retire ici a sa place.
    const bool hearing_restored = RestoreEnemyHearingHook(process);
    if (!g_enemy_invisibility.applied)
    {
        ClearEnemyInvisibilityPatch();
        return hearing_restored;
    }
    if (!process.IsConnected() ||
        process.ProcessId() != g_enemy_invisibility.process_id)
    {
        ClearEnemyInvisibilityPatch();
        return true;
    }

    std::array<std::uint8_t, 5> current{};
    bool restored = process.ReadMemory(
        g_enemy_invisibility.function, current.data(), current.size());
    if (restored && current != g_enemy_invisibility.original)
    {
        restored = current == g_enemy_invisibility.patch &&
            process.WriteProtectedMemory(
                g_enemy_invisibility.function,
                g_enemy_invisibility.original.data(),
                g_enemy_invisibility.original.size());
    }
    if (restored)
    {
        restored = process.ReadMemory(
            g_enemy_invisibility.function,
            current.data(), current.size()) &&
            current == g_enemy_invisibility.original;
    }

    if (!restored)
        return false;
    QueueRemotePageRelease(
        process, g_enemy_invisibility.remote,
        g_enemy_invisibility.remote_size);
    ClearEnemyInvisibilityPatch();
    return hearing_restored;
}

bool InstallEnemyInvisibilityHook(
    TrainerProcess& process,
    std::uintptr_t function,
    const std::array<std::uint8_t, 13>& signature,
    std::uintptr_t protected_player,
    EnemyInvisibilityScope scope)
{
    EnemyInvisibilityPatchState next{};
    next.process_id = process.ProcessId();
    next.function = function;
    next.protected_player = protected_player;
    next.remote_size = kEnemyInvisibilityRemoteSize;
    next.scope = scope;
    std::copy_n(
        signature.begin(), next.original.size(), next.original.begin());
    next.remote = process.AllocateRemoteMemory(next.remote_size);
    if (!IsSanePointer(next.remote))
        return false;

    const auto relative32 = [](std::uintptr_t target, std::uintptr_t next_ip)
    {
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<std::intptr_t>(target) -
            static_cast<std::intptr_t>(next_ip)));
    };
    std::vector<std::uint8_t> code;
    code.reserve(48);
    const auto byte = [&](std::uint8_t value) { code.push_back(value); };
    const auto dword = [&](std::uint32_t value)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(value));
        std::memcpy(code.data() + offset, &value, sizeof(value));
    };

    // IsEnemy is shared by every squad player and ECX is the candidate player
    // on entry. Les deux portees testent une condition et retombent sinon dans
    // la fonction native. V95 : la portee est lue dans un emplacement de
    // donnees, donc les deux chemins coexistent en permanence dans la page et
    // un simple mot decide lequel est emprunte.
    const auto slot = [&](std::size_t offset)
    {
        dword(static_cast<std::uint32_t>(next.remote + offset));
    };
    byte(0x83); byte(0x3D); slot(kEnemyFilterScopeOffset); byte(0x00);
                                             // cmp dword [portee],0
    byte(0x0F); byte(0x85);                  // jne squad
    const std::size_t squad_jump = code.size();
    dword(0);
    byte(0x3B); byte(0x0D); slot(kEnemyFilterTargetOffset);
                                             // cmp ecx,[cible]
    byte(0x0F); byte(0x84);                  // je invisible
    const std::size_t controlled_hit_jump = code.size();
    dword(0);
    byte(0xE9);                              // jmp native
    const std::size_t controlled_miss_jump = code.size();
    dword(0);

    const std::size_t squad = code.size();
    byte(0x83); byte(0x79); byte(0x1C);
    byte(static_cast<std::uint8_t>(kActorTypePlayer));
                                             // cmp [ecx+type],joueur
    byte(0x0F); byte(0x84);                  // je invisible
    const std::size_t squad_hit_jump = code.size();
    dword(0);

    // Replay the complete first two instructions (7 bytes); the five-byte
    // hook ends in the middle of the second instruction.
    const std::size_t native = code.size();
    for (std::size_t index = 0; index < 7; ++index)
        byte(signature[index]);
    byte(0xE9);
    dword(relative32(
        function + 7U, next.remote + code.size() + sizeof(std::uint32_t)));

    const std::size_t invisible = code.size();
    byte(0x32); byte(0xC0);                  // xor al,al
    byte(0xC2); byte(0x04); byte(0x00);     // ret 4
    const auto patch_jump = [&](std::size_t displacement, std::size_t target)
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            target - (displacement + sizeof(std::int32_t)));
        std::memcpy(code.data() + displacement, &relative, sizeof(relative));
    };
    patch_jump(squad_jump, squad);
    patch_jump(controlled_hit_jump, invisible);
    patch_jump(controlled_miss_jump, native);
    patch_jump(squad_hit_jump, invisible);

    const std::uint32_t installed_scope =
        scope == EnemyInvisibilityScope::WholeSquad
        ? kEnemyFilterScopeWholeSquad
        : kEnemyFilterScopeControlled;
    if (code.size() > kEnemyFilterScopeOffset ||
        !process.WriteMemory(next.remote, code.data(), code.size()) ||
        !process.WriteMemory(
            next.remote + kEnemyFilterScopeOffset, installed_scope) ||
        !process.WriteMemory(
            next.remote + kEnemyFilterTargetOffset,
            static_cast<std::uint32_t>(protected_player)))
    {
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }
    next.patch = {0xE9, 0, 0, 0, 0};
    const std::uint32_t hook_relative = relative32(
        next.remote, function + next.patch.size());
    std::memcpy(next.patch.data() + 1, &hook_relative, 4);
    next.applied = true;
    g_enemy_invisibility = next;
    if (!process.WriteProtectedMemory(
            function, next.patch.data(), next.patch.size()))
    {
        LogDiagnostic(
            "Enemy invisibility: VISUAL hook write REFUSED function=%08X.",
            static_cast<unsigned>(function));
        (void)RestoreEnemyInvisibility(process);
        return false;
    }
    LogDiagnostic(
        "Enemy invisibility: visual IsEnemy hook installed "
        "function=%08X protected=%08X scope=%u remote=%08X.",
        static_cast<unsigned>(function),
        static_cast<unsigned>(protected_player),
        static_cast<unsigned>(scope),
        static_cast<unsigned>(next.remote));
    return true;
}

bool InstallEnemyHearingHook(
    TrainerProcess& process,
    std::uintptr_t function,
    std::uintptr_t protected_player,
    EnemyInvisibilityScope scope)
{
    constexpr std::array<std::uint8_t, 7> kExpected{
        0x56, 0x8B, 0xF1, 0x57, 0x8B, 0x46, 0x28};
        // Installed C_enemy::IsEnemy: push esi; mov esi,ecx; push edi;
        // mov eax,[esi+frame]. All seven bytes form complete instructions.

    EnemyHearingPatchState next{};
    next.process_id = process.ProcessId();
    next.function = function;
    next.protected_player = protected_player;
    next.remote_size = kEnemyInvisibilityRemoteSize;
    next.scope = scope;
    if (!process.ReadMemory(
            function, next.original.data(), next.original.size()) ||
        next.original != kExpected)
    {
        return false;
    }
    next.remote = process.AllocateRemoteMemory(next.remote_size);
    if (!IsSanePointer(next.remote))
        return false;

    const auto relative32 = [](std::uintptr_t target, std::uintptr_t next_ip)
    {
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<std::intptr_t>(target) -
            static_cast<std::intptr_t>(next_ip)));
    };
    std::vector<std::uint8_t> code;
    const auto byte = [&](std::uint8_t value) { code.push_back(value); };
    const auto dword = [&](std::uint32_t value)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(value));
        std::memcpy(code.data() + offset, &value, sizeof(value));
    };

    // L'emetteur teste est le parametre pile, pas ECX. Meme principe V95 :
    // les deux portees vivent dans la page, un mot decide laquelle s'applique.
    const auto slot = [&](std::size_t offset)
    {
        dword(static_cast<std::uint32_t>(next.remote + offset));
    };
    byte(0x8B); byte(0x44); byte(0x24); byte(0x04); // mov eax,[esp+4]
    byte(0x83); byte(0x3D); slot(kEnemyFilterScopeOffset); byte(0x00);
                                             // cmp dword [portee],0
    byte(0x0F); byte(0x85);                  // jne squad
    const std::size_t squad_jump = code.size();
    dword(0);
    byte(0x3B); byte(0x05); slot(kEnemyFilterTargetOffset);
                                             // cmp eax,[cible]
    byte(0x0F); byte(0x84);                  // je inaudible
    const std::size_t controlled_hit_jump = code.size();
    dword(0);
    byte(0xE9);                              // jmp native
    const std::size_t controlled_miss_jump = code.size();
    dword(0);

    // Whole-squad mode suppresses only player emitters. Enemy/civilian
    // relations continue through the native function.
    const std::size_t squad = code.size();
    byte(0x83); byte(0x78); byte(0x1C); byte(0x01);
                                             // cmp [eax+type],joueur
    byte(0x0F); byte(0x84);                  // je inaudible
    const std::size_t squad_hit_jump = code.size();
    dword(0);

    const std::size_t native = code.size();
    for (const std::uint8_t value : kExpected)
        byte(value);
    byte(0xE9);
    dword(relative32(
        function + kExpected.size(),
        next.remote + code.size() + sizeof(std::uint32_t)));

    const std::size_t inaudible = code.size();
    byte(0x32); byte(0xC0);                  // xor al,al
    byte(0xC2); byte(0x04); byte(0x00);     // ret 4
    const auto patch_jump = [&](std::size_t displacement, std::size_t target)
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            target - (displacement + sizeof(std::int32_t)));
        std::memcpy(code.data() + displacement, &relative, sizeof(relative));
    };
    patch_jump(squad_jump, squad);
    patch_jump(controlled_hit_jump, inaudible);
    patch_jump(controlled_miss_jump, native);
    patch_jump(squad_hit_jump, inaudible);

    const std::uint32_t installed_scope =
        scope == EnemyInvisibilityScope::WholeSquad
        ? kEnemyFilterScopeWholeSquad
        : kEnemyFilterScopeControlled;
    if (code.size() > kEnemyFilterScopeOffset ||
        !process.WriteMemory(next.remote, code.data(), code.size()) ||
        !process.WriteMemory(
            next.remote + kEnemyFilterScopeOffset, installed_scope) ||
        !process.WriteMemory(
            next.remote + kEnemyFilterTargetOffset,
            static_cast<std::uint32_t>(protected_player)))
    {
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }
    next.patch = {0xE9, 0, 0, 0, 0, 0x90, 0x90};
    const std::uint32_t hook_relative = relative32(
        next.remote, function + 5U);
    std::memcpy(next.patch.data() + 1, &hook_relative, 4);
    next.applied = true;
    g_enemy_hearing = next;
    if (!process.WriteProtectedMemory(
            function, next.patch.data(), next.patch.size()))
    {
        (void)RestoreEnemyHearingHook(process);
        return false;
    }
    LogDiagnostic(
        "Enemy invisibility: reciprocal IsEnemy hook installed "
        "function=%08X protected=%08X scope=%u.",
        static_cast<unsigned>(function),
        static_cast<unsigned>(protected_player),
        static_cast<unsigned>(scope));
    return true;
}

void UpdateEnemyInvisibilityImpl(
    TrainerProcess& process,
    const RadarSnapshot* snapshot,
    const GameplaySettings& settings,
    GameplayStatus& status)
{
    // Deux demandeurs possibles : la case dediee, avec sa portee, et le masque
    // de position pendant qu'il tient le modele cache, toujours pour le seul
    // soldat pilote. La case garde la priorite sur la portee : si elle est
    // cochee en « Escouade entiere », c'est un choix explicite du joueur.
    const bool mask_requests_filter =
        NetworkPositionMaskHidesLocalPlayer(process);
    const bool filter_enabled =
        settings.enemy_invisibility_enabled || mask_requests_filter;
    // V97 : quand la demande vient du masque de position, elle suit la
    // pastille de CE masque. « Joueur actuel » ne couvre que le soldat pilote;
    // « Escouade entiere » couvre tous les joueurs, tes soldats et ceux des PC
    // amis, exactement comme la portee escouade de la case dediee.
    const EnemyInvisibilityScope requested_scope =
        settings.enemy_invisibility_enabled
        ? settings.enemy_invisibility_scope
        : (g_network_position_mask.scope ==
               NetworkPositionMaskScope::WholeSquad
           ? EnemyInvisibilityScope::WholeSquad
           : EnemyInvisibilityScope::ControlledPlayer);
    if (!filter_enabled)
    {
        status.enemy_invisibility = RestoreEnemyInvisibility(process)
            ? EnemyInvisibilityStatus::Disabled
            : EnemyInvisibilityStatus::WriteFailed;
        return;
    }
    if (!process.IsConnected())
    {
        (void)RestoreEnemyInvisibility(process);
        status.enemy_invisibility =
            EnemyInvisibilityStatus::WaitingForMission;
        return;
    }
    if (!snapshot || !IsSanePointer(snapshot->player_object_address))
    {
        // H&D nulls its mission pointer for a frame or two during normal
        // play, and for far longer in a network session, where the game
        // thread stalls waiting on the other machine - stalls above two
        // seconds are in the journal. Once the caller's grace period expired
        // this branch tore the filter out, and it only went back when a
        // snapshot returned. That gap is what let the enemies see the player
        // again mid-fight.
        //
        // Holding the hooks is safe. Neither trampoline touches anything the
        // trainer owns while the mission is unreadable: the controlled-player
        // variant compares a pointer value without dereferencing it, and the
        // whole-squad variant reads the actor the game itself passed in. So
        // keep them until the feature is switched off or the process changes.
        if (g_enemy_invisibility.applied &&
            g_enemy_invisibility.process_id == process.ProcessId())
        {
            // Periodic proof that the filter really is still in the game
            // while the trainer is blind, so a long gap is measurable.
            static std::uint64_t next_hold_log = 0;
            const std::uint64_t now_ms =
                static_cast<std::uint64_t>(GetTickCount64());
            if (now_ms >= next_hold_log)
            {
                next_hold_log = now_ms + 5000ULL;
                std::array<std::uint8_t, 5> held{};
                const bool read_ok = process.ReadMemory(
                    g_enemy_invisibility.function,
                    held.data(), held.size());
                LogDiagnostic(
                    "Enemy invisibility: HOLDING through mission gap "
                    "fn=%08X first=%02X live=%u scope=%u.",
                    static_cast<unsigned>(g_enemy_invisibility.function),
                    read_ok ? held[0] : 0U,
                    (read_ok && held[0] == 0xE9) ? 1U : 0U,
                    static_cast<unsigned>(g_enemy_invisibility.scope));
            }
            status.enemy_invisibility =
                EnemyInvisibilityStatus::HeldThroughMissionGap;
            return;
        }
        (void)RestoreEnemyInvisibility(process);
        status.enemy_invisibility =
            EnemyInvisibilityStatus::WaitingForMission;
        return;
    }

    // The scope stays the player's choice. Note the trade-off when reading
    // the log: ControlledPlayer pins the trampoline to one actor address, so
    // the hook is rebuilt whenever the controlled soldier changes, and each
    // rebuild is a short window with no filter. WholeSquad compares the actor
    // type instead, covers every squad member and every connected player, and
    // never needs a rebuild.
    const EnemyInvisibilityScope effective_scope = requested_scope;

    // Heartbeat. Reads the real first bytes of both hooked functions back out
    // of the game, so the journal states plainly whether the filter was live
    // at any moment rather than only when it was installed.
    {
        static std::uint64_t next_heartbeat = 0;
        const std::uint64_t now_ms =
            static_cast<std::uint64_t>(GetTickCount64());
        if (now_ms >= next_heartbeat)
        {
            next_heartbeat = now_ms + 5000ULL;
            std::array<std::uint8_t, 5> visual_bytes{};
            std::array<std::uint8_t, 5> hearing_bytes{};
            std::array<std::uint8_t, 5> damage_bytes{};
            const bool visual_read = g_enemy_invisibility.applied &&
                process.ReadMemory(
                    g_enemy_invisibility.function,
                    visual_bytes.data(), visual_bytes.size());
            const bool hearing_read = g_enemy_hearing.applied &&
                process.ReadMemory(
                    g_enemy_hearing.function,
                    hearing_bytes.data(), hearing_bytes.size());
            const bool damage_read = g_player_damage.applied &&
                process.ReadMemory(
                    g_player_damage.function,
                    damage_bytes.data(), damage_bytes.size());
            LogDiagnostic(
                "Enemy invisibility HEARTBEAT: visual_applied=%u live=%u "
                "fn=%08X first=%02X | hearing_applied=%u live=%u fn=%08X "
                "first=%02X | damage_applied=%u live=%u fn=%08X first=%02X "
                "| scope=%u role=%u player=%08X enemies=%u.",
                g_enemy_invisibility.applied ? 1U : 0U,
                (visual_read && visual_bytes[0] == 0xE9) ? 1U : 0U,
                static_cast<unsigned>(g_enemy_invisibility.function),
                visual_read ? visual_bytes[0] : 0U,
                g_enemy_hearing.applied ? 1U : 0U,
                (hearing_read && hearing_bytes[0] == 0xE9) ? 1U : 0U,
                static_cast<unsigned>(g_enemy_hearing.function),
                hearing_read ? hearing_bytes[0] : 0U,
                g_player_damage.applied ? 1U : 0U,
                (damage_read && damage_bytes[0] == 0xE9) ? 1U : 0U,
                static_cast<unsigned>(g_player_damage.function),
                damage_read ? damage_bytes[0] : 0U,
                static_cast<unsigned>(effective_scope),
                static_cast<unsigned>(snapshot->network_role),
                static_cast<unsigned>(snapshot->player_object_address),
                static_cast<unsigned>(snapshot->entity_array.count));
            LogEnemyAwarenessCensus(process, *snapshot, effective_scope);
        }
    }

    // One line per role transition, so a network session can be diagnosed
    // from the log without reproducing it live.
    static NetworkRole logged_role = NetworkRole::Offline;
    static bool logged_role_valid = false;
    if (!logged_role_valid || logged_role != snapshot->network_role)
    {
        logged_role = snapshot->network_role;
        logged_role_valid = true;
        LogDiagnostic(
            "Enemy invisibility: network role=%s scope=%u "
            "net_host=%u net_join=%u "
            "player_actors=%u active_flags=%u player=%08X.",
            snapshot->network_role == NetworkRole::Host ? "host"
                : (snapshot->network_role == NetworkRole::Client ? "client"
                : (snapshot->network_role == NetworkRole::UnknownRole
                       ? "network(role unknown)"
                       : "offline")),
            static_cast<unsigned>(effective_scope),
            static_cast<unsigned>(snapshot->net_host_byte),
            static_cast<unsigned>(snapshot->net_join_byte),
            static_cast<unsigned>(snapshot->player_actor_count),
            static_cast<unsigned>(snapshot->active_player_count),
            static_cast<unsigned>(snapshot->player_object_address));
    }

    // What "the hooks are installed" is actually worth depends on who runs
    // the simulation. Hosting or offline, this process decides what the AI
    // perceives. As a client it does not: hde.exe skips the authoritative
    // update when it joined a session, so enemy perception and attacks are
    // resolved on the host and no local patch can suppress them.
    const auto installed_status = [&]()
    {
        switch (snapshot->network_role)
        {
        case NetworkRole::Host:
            return EnemyInvisibilityStatus::ActiveNetworkHost;
        case NetworkRole::Client:
            return EnemyInvisibilityStatus::HostAuthorityRequired;
        case NetworkRole::UnknownRole:
            return EnemyInvisibilityStatus::ActiveNetworkUnknownRole;
        case NetworkRole::Offline:
        default:
            return EnemyInvisibilityStatus::Active;
        }
    };

    // V95 : cette case ne lit plus C_player::Hit et n'y pose plus rien. Elle
    // ne traite que la perception. La garde de degats qu'elle installait
    // couvrait TOUT acteur de type joueur, sans test de proprietaire : elle
    // rendait donc aussi les soldats du PC ami insensibles aux balles, quelle
    // que soit la portee choisie, alors que l'interface n'annonce rien de tel.
    // Cette immunite appartient desormais a Protection reseau totale seule.
    std::uintptr_t vtable = 0;
    std::uintptr_t function = 0;
    RemoteModuleInfo module{};
    if (!process.ReadMemory(snapshot->player_object_address, vtable) ||
        !IsSanePointer(vtable) ||
        !process.ReadMemory(
            vtable + kPlayerIsEnemyVtableOffset, function) ||
        !process.GetMainModuleInfo(module) ||
        function < module.base_address ||
        function + 13U >
            module.base_address + module.image_size)
    {
        (void)RestoreEnemyInvisibility(process);
        status.enemy_invisibility =
            EnemyInvisibilityStatus::UnsupportedRevision;
        return;
    }

    std::uintptr_t enemy_function = 0;
    bool enemy_actor_present = false;
    for (std::uint32_t index = 0;
         index < snapshot->entity_array.count; ++index)
    {
        const RadarEntity& entity = snapshot->entity_array.entities[index];
        if (entity.active == 0 || entity.team != EntityTeam::Enemy ||
            !IsSanePointer(entity.actor_address))
        {
            continue;
        }
        enemy_actor_present = true;
        std::uintptr_t enemy_vtable = 0;
        if (process.ReadMemory(entity.actor_address, enemy_vtable) &&
            IsSanePointer(enemy_vtable) &&
            process.ReadMemory(
                enemy_vtable + kPlayerIsEnemyVtableOffset,
                enemy_function) &&
            enemy_function >= module.base_address &&
            enemy_function + 7U <=
                module.base_address + module.image_size)
        {
            break;
        }
        enemy_function = 0;
    }
    if (enemy_actor_present && !IsSanePointer(enemy_function))
    {
        (void)RestoreEnemyInvisibility(process);
        status.enemy_invisibility =
            EnemyInvisibilityStatus::UnsupportedRevision;
        return;
    }

    // V95 : ni la portee ni le changement de soldat controle ne justifient
    // plus de reposer un crochet. Les deux trampolines lisent leur mode et
    // leur cible dans leur propre page, donc le choix de l'interface est
    // applique par une ecriture de 4 octets, immediatement, meme pendant que
    // le trainer a le focus. Il n'existe donc plus ni report jusqu'au retour
    // dans le jeu, ni fenetre de reconstruction sans filtre. Seuls un
    // changement de processus ou de fonction virtuelle imposent un retrait.
    if (g_enemy_hearing.applied &&
        (g_enemy_hearing.process_id != process.ProcessId() ||
         (enemy_function != 0 &&
          g_enemy_hearing.function != enemy_function)))
    {
        if (!RestoreEnemyHearingHook(process))
        {
            status.enemy_invisibility =
                EnemyInvisibilityStatus::WriteFailed;
            return;
        }
    }
    if (enemy_function != 0 && !g_enemy_hearing.applied &&
        !InstallEnemyHearingHook(
            process,
            enemy_function,
            snapshot->player_object_address,
            effective_scope))
    {
        (void)RestoreEnemyInvisibility(process);
        status.enemy_invisibility =
            EnemyInvisibilityStatus::UnsupportedRevision;
        return;
    }

    if (g_enemy_invisibility.applied &&
        (g_enemy_invisibility.process_id != process.ProcessId() ||
         g_enemy_invisibility.function != function))
    {
        if (!RestoreEnemyInvisibility(process))
        {
            status.enemy_invisibility =
                EnemyInvisibilityStatus::WriteFailed;
            return;
        }
    }
    if (g_enemy_invisibility.applied)
    {
        const EnemyInvisibilityScope previous_scope =
            g_enemy_invisibility.scope;
        const std::uintptr_t previous_target =
            g_enemy_invisibility.protected_player;
        bool visual_changed = false;
        bool hearing_changed = false;
        if (!ApplyEnemyFilterSelection(
                process, g_enemy_invisibility, effective_scope,
                snapshot->player_object_address, visual_changed) ||
            !ApplyEnemyFilterSelection(
                process, g_enemy_hearing, effective_scope,
                snapshot->player_object_address, hearing_changed))
        {
            status.enemy_invisibility = EnemyInvisibilityStatus::WriteFailed;
            return;
        }
        if (visual_changed || hearing_changed)
        {
            // Une bascule de portee change qui est filtre, et un changement de
            // soldat en portee locale aussi : les perceptions deja memorisees
            // contre la nouvelle cible doivent etre purgees une fois de plus.
            // En portee escouade, l'emplacement de cible n'est pas lu par le
            // trampoline et tous les joueurs sont deja couverts : reecrire la
            // cible ne justifie alors aucune purge supplementaire.
            const bool selection_matters =
                previous_scope != effective_scope ||
                (effective_scope ==
                     EnemyInvisibilityScope::ControlledPlayer &&
                 previous_target != snapshot->player_object_address);
            if (selection_matters)
                g_enemy_invisibility.awareness_reset = false;
            LogDiagnostic(
                "Enemy invisibility: selection applied live scope=%u "
                "player=%08X repurge=%u (aucun crochet repose).",
                static_cast<unsigned>(effective_scope),
                static_cast<unsigned>(snapshot->player_object_address),
                selection_matters ? 1U : 0U);
        }
        std::array<std::uint8_t, 5> current{};
        if (!process.ReadMemory(function, current.data(), current.size()) ||
            current != g_enemy_invisibility.patch)
        {
            LogDiagnostic(
                "Enemy invisibility: PATCH LOST at function=%08X "
                "read=%02X%02X%02X%02X%02X expected=%02X%02X%02X%02X%02X.",
                static_cast<unsigned>(function),
                current[0], current[1], current[2], current[3], current[4],
                g_enemy_invisibility.patch[0], g_enemy_invisibility.patch[1],
                g_enemy_invisibility.patch[2], g_enemy_invisibility.patch[3],
                g_enemy_invisibility.patch[4]);
            status.enemy_invisibility =
                EnemyInvisibilityStatus::WriteFailed;
            return;
        }
        const bool awareness_purged = NeutralizeEnemyAwareness(
            process, *snapshot, effective_scope);
        const bool awareness_held = awareness_purged &&
            MaintainEnemyAwarenessSuppression(
                process, *snapshot, effective_scope);
        status.enemy_invisibility = awareness_held
            ? installed_status()
            : EnemyInvisibilityStatus::WriteFailed;
        return;
    }

    std::array<std::uint8_t, 13> signature{};
    if (!process.ReadMemory(function, signature.data(), signature.size()) ||
        !std::equal(
            kPlayerIsEnemyOriginalPrefix.begin(),
            kPlayerIsEnemyOriginalPrefix.end(), signature.begin()) ||
        !std::equal(
            kPlayerIsEnemyOriginalSuffix.begin(),
            kPlayerIsEnemyOriginalSuffix.end(), signature.begin() + 5))
    {
        status.enemy_invisibility =
            EnemyInvisibilityStatus::UnsupportedRevision;
        return;
    }

    if (!InstallEnemyInvisibilityHook(
            process,
            function,
            signature,
            snapshot->player_object_address,
            effective_scope))
    {
        status.enemy_invisibility = EnemyInvisibilityStatus::WriteFailed;
        return;
    }
    const bool awareness_purged = NeutralizeEnemyAwareness(
        process, *snapshot, effective_scope);
    const bool awareness_held = awareness_purged &&
        MaintainEnemyAwarenessSuppression(
            process, *snapshot, effective_scope);
    status.enemy_invisibility = awareness_held
        ? installed_status()
        : EnemyInvisibilityStatus::WriteFailed;
}

const char* EnemyInvisibilityStatusName(EnemyInvisibilityStatus value)
{
    switch (value)
    {
    case EnemyInvisibilityStatus::Disabled: return "disabled";
    case EnemyInvisibilityStatus::WaitingForMission: return "waiting";
    case EnemyInvisibilityStatus::Active: return "active";
    case EnemyInvisibilityStatus::ActiveNetworkHost: return "active-host";
    case EnemyInvisibilityStatus::ActiveNetworkUnknownRole:
        return "active-network";
    case EnemyInvisibilityStatus::HostAuthorityRequired:
        return "host-authority-required";
    case EnemyInvisibilityStatus::UnsupportedRevision:
        return "unsupported-revision";
    case EnemyInvisibilityStatus::HeldThroughMissionGap:
        return "held-through-mission-gap";
    case EnemyInvisibilityStatus::WriteFailed: return "write-failed";
    default: return "?";
    }
}

// Wrapper so that every exit path of the update is accounted for. The status
// is what the panel shows, and a silent change of it - enabled to waiting, or
// active to disabled - was previously invisible in the journal.
void UpdateEnemyInvisibility(
    TrainerProcess& process,
    const RadarSnapshot* snapshot,
    const GameplaySettings& settings,
    GameplayStatus& status)
{
    UpdateEnemyInvisibilityImpl(process, snapshot, settings, status);
    const bool mask_driven = !settings.enemy_invisibility_enabled &&
        NetworkPositionMaskHidesLocalPlayer(process);

    static bool seen = false;
    static EnemyInvisibilityStatus previous =
        EnemyInvisibilityStatus::Disabled;
    if (!seen || previous != status.enemy_invisibility)
    {
        seen = true;
        previous = status.enemy_invisibility;
        LogDiagnostic(
            "Enemy invisibility: status -> %s (checkbox=%u scope=%u "
            "mask_driven=%u snapshot=%u player=%08X).",
            EnemyInvisibilityStatusName(status.enemy_invisibility),
            settings.enemy_invisibility_enabled ? 1U : 0U,
            static_cast<unsigned>(settings.enemy_invisibility_scope),
            mask_driven ? 1U : 0U,
            snapshot != nullptr ? 1U : 0U,
            snapshot != nullptr
                ? static_cast<unsigned>(snapshot->player_object_address)
                : 0U);
    }
    if (mask_driven)
    {
        // Le filtre est bien pose, mais il appartient au masque de position.
        // La carte « Invisible pour les ennemis » doit continuer d'afficher
        // l'etat de sa propre case, decochee; c'est la carte du masque qui
        // annonce l'invisibilite en cours.
        status.enemy_invisibility = EnemyInvisibilityStatus::Disabled;
    }
}

float LengthSquared(const Vector3& value)
{
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

Vector3 SubtractVector(const Vector3& left, const Vector3& right)
{
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

float DotVector(const Vector3& left, const Vector3& right)
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

bool NormalizeVector(Vector3& value)
{
    const float length_squared = LengthSquared(value);
    if (!std::isfinite(length_squared) || length_squared <= 0.000001f)
        return false;
    const float inverse_length = 1.0f / std::sqrt(length_squared);
    value.x *= inverse_length;
    value.y *= inverse_length;
    value.z *= inverse_length;
    return IsSaneMoveDirection(value);
}

bool SameVector(const Vector3& left, const Vector3& right)
{
    constexpr float kTolerance = 0.0002f;
    return std::fabs(left.x - right.x) <= kTolerance &&
        std::fabs(left.y - right.y) <= kTolerance &&
        std::fabs(left.z - right.z) <= kTolerance;
}

const RadarEntity* SelectAimbotTarget(
    const RadarSnapshot& snapshot,
    float maximum_distance,
    bool restrict_to_bullet_circle,
    float bullet_circle_radius,
    std::uintptr_t preferred_actor = 0,
    bool require_head_frame = false,
    bool require_visible = true,
    std::array<std::uint32_t, 4>* tally = nullptr)
{
    const bool distance_limited = maximum_distance > 0.0f;
    const float maximum_distance_squared = distance_limited
        ? maximum_distance * maximum_distance : 0.0f;
    const RadarEntity* best = nullptr;
    const RadarEntity* preferred = nullptr;
    float best_screen_distance_squared =
        (std::numeric_limits<float>::infinity)();
    for (std::uint32_t index = 0;
         index < snapshot.entity_array.count; ++index)
    {
        const RadarEntity& entity = snapshot.entity_array.entities[index];
        if (entity.active == 0 || entity.team != EntityTeam::Enemy ||
            entity.stay_mode == kHumanStayModeDead ||
            !IsSanePointer(entity.actor_address) ||
            !IsSaneWorldPosition(entity.head_position))
        {
            continue;
        }
        if (tally)
            ++(*tally)[0];
        if (require_head_frame &&
            !IsSanePointer(entity.head_frame_address))
        {
            if (tally)
                ++(*tally)[1];
            continue;
        }
        // Normal mode requires a clear ballistic path. Through-walls mode
        // accepts red/occluded enemies too; live actor membership is validated
        // later by the game-thread resolver trampoline before dereferencing.
        const bool eligible =
            !require_visible || entity.directly_visible != 0;
        if (!eligible)
        {
            if (tally)
                ++(*tally)[2];
            continue;
        }
        const Vector3 delta = SubtractVector(
            entity.head_position, snapshot.player_aim_position);
        const float distance_squared = LengthSquared(delta);
        if (!std::isfinite(distance_squared) ||
            (distance_limited &&
             distance_squared > maximum_distance_squared))
        {
            continue;
        }
        // An enemy inside a house is often projected slightly outside the
        // client rectangle — below a window line, behind a door frame — and
        // the strict bound rejected 25 to 40 of the 44 enemies seen in the
        // journal. When shots are allowed through walls the margin is widened
        // by half a screen on each side; the target is still the one closest
        // to the crosshair, so aiming keeps deciding which enemy is hit.
        const float margin_x = require_visible
            ? 0.0f : snapshot.game_client_width * 0.5f;
        const float margin_y = require_visible
            ? 0.0f : snapshot.game_client_height * 0.5f;
        Vector2 projected{};
        if (!WorldToScreen(
                entity.head_position, projected,
                snapshot.view_projection,
                snapshot.game_client_width,
                snapshot.game_client_height) ||
            projected.x < -margin_x ||
            projected.x > snapshot.game_client_width + margin_x ||
            projected.y < -margin_y ||
            projected.y > snapshot.game_client_height + margin_y)
        {
            if (tally)
                ++(*tally)[3];
            continue;
        }
        const float center_x = snapshot.game_client_width * 0.5f;
        const float center_y = snapshot.game_client_height * 0.5f;
        const float dx = projected.x - center_x;
        const float dy = projected.y - center_y;
        float screen_distance_squared = dx * dx + dy * dy;
        if (restrict_to_bullet_circle &&
            IsSaneWorldPosition(entity.position))
        {
            Vector2 feet{};
            if (WorldToScreen(
                    entity.position, feet, snapshot.view_projection,
                    snapshot.game_client_width,
                    snapshot.game_client_height))
            {
                const float segment_x = feet.x - projected.x;
                const float segment_y = feet.y - projected.y;
                const float segment_length_squared =
                    segment_x * segment_x + segment_y * segment_y;
                const float projection = segment_length_squared > 0.001f
                    ? (std::clamp)(
                        ((center_x - projected.x) * segment_x +
                         (center_y - projected.y) * segment_y) /
                            segment_length_squared,
                        0.0f, 1.0f)
                    : 0.0f;
                const float nearest_x = projected.x + segment_x * projection;
                const float nearest_y = projected.y + segment_y * projection;
                const float circle_dx = nearest_x - center_x;
                const float circle_dy = nearest_y - center_y;
                screen_distance_squared =
                    circle_dx * circle_dx + circle_dy * circle_dy;
            }
        }
        if (restrict_to_bullet_circle &&
            screen_distance_squared >
                bullet_circle_radius * bullet_circle_radius)
        {
            continue;
        }
        if (preferred_actor != 0 &&
            entity.actor_address == preferred_actor)
        {
            preferred = &entity;
        }
        if (!best || screen_distance_squared < best_screen_distance_squared)
        {
            best = &entity;
            best_screen_distance_squared = screen_distance_squared;
        }
    }
    return preferred ? preferred : best;
}

std::array<std::uint32_t, kBulletTrackCandidateCount>
CollectBulletTrackCandidateActors(
    const RadarSnapshot& snapshot,
    const RadarEntity* primary,
    bool require_visible)
{
    std::array<std::uint32_t, kBulletTrackCandidateCount> candidates{};
    if (!primary || !IsSanePointer(primary->actor_address))
        return candidates;

    // Slot zero remains the exact target already selected for Bullet Track.
    // The following slots are only used after that actor is proved dead by the
    // game-thread trampoline, preserving the normal crosshair behaviour.
    candidates[0] = static_cast<std::uint32_t>(primary->actor_address);

    struct RankedCandidate
    {
        float screen_distance_squared = 0.0f;
        std::uintptr_t actor = 0;
    };
    std::vector<RankedCandidate> ranked;
    ranked.reserve(snapshot.entity_array.count);

    const float center_x = snapshot.game_client_width * 0.5f;
    const float center_y = snapshot.game_client_height * 0.5f;
    const float margin_x = require_visible
        ? 0.0f : snapshot.game_client_width * 0.5f;
    const float margin_y = require_visible
        ? 0.0f : snapshot.game_client_height * 0.5f;
    for (std::uint32_t index = 0;
         index < snapshot.entity_array.count; ++index)
    {
        const RadarEntity& entity = snapshot.entity_array.entities[index];
        if (entity.active == 0 || entity.team != EntityTeam::Enemy ||
            entity.stay_mode == kHumanStayModeDead ||
            !IsSanePointer(entity.actor_address) ||
            !IsSanePointer(entity.head_frame_address) ||
            !IsSaneWorldPosition(entity.head_position) ||
            (require_visible && entity.directly_visible == 0) ||
            entity.actor_address == primary->actor_address)
        {
            continue;
        }

        Vector2 projected{};
        if (!WorldToScreen(
                entity.head_position, projected, snapshot.view_projection,
                snapshot.game_client_width, snapshot.game_client_height) ||
            projected.x < -margin_x ||
            projected.x > snapshot.game_client_width + margin_x ||
            projected.y < -margin_y ||
            projected.y > snapshot.game_client_height + margin_y)
        {
            continue;
        }
        const float dx = projected.x - center_x;
        const float dy = projected.y - center_y;
        ranked.push_back({dx * dx + dy * dy, entity.actor_address});
    }

    std::sort(
        ranked.begin(), ranked.end(),
        [](const RankedCandidate& left, const RankedCandidate& right)
        {
            return left.screen_distance_squared < right.screen_distance_squared;
        });
    for (std::size_t index = 0;
         index < ranked.size() && index + 1 < candidates.size(); ++index)
    {
        candidates[index + 1] =
            static_cast<std::uint32_t>(ranked[index].actor);
    }
    return candidates;
}

const RadarEntity* SelectDirectionalAimbotTarget(
    const RadarSnapshot& snapshot,
    float maximum_distance,
    std::uintptr_t current_actor,
    int horizontal_direction)
{
    if (horizontal_direction == 0 || current_actor == 0)
        return nullptr;

    float current_x = 0.0f;
    bool current_found = false;
    for (std::uint32_t index = 0;
         index < snapshot.entity_array.count; ++index)
    {
        const RadarEntity& entity = snapshot.entity_array.entities[index];
        if (entity.actor_address != current_actor)
            continue;
        Vector2 projected{};
        current_found = WorldToScreen(
            entity.head_position, projected, snapshot.view_projection,
            snapshot.game_client_width, snapshot.game_client_height);
        if (current_found)
            current_x = projected.x;
        break;
    }
    if (!current_found)
        return nullptr;

    const float maximum_distance_squared =
        maximum_distance * maximum_distance;
    constexpr float kMinimumHorizontalSeparationPixels = 24.0f;
    const RadarEntity* best = nullptr;
    float best_horizontal_gap =
        (std::numeric_limits<float>::infinity)();
    for (std::uint32_t index = 0;
         index < snapshot.entity_array.count; ++index)
    {
        const RadarEntity& entity = snapshot.entity_array.entities[index];
        if (entity.actor_address == current_actor || entity.active == 0 ||
            entity.team != EntityTeam::Enemy ||
            entity.directly_visible == 0 ||
            !IsSanePointer(entity.actor_address) ||
            !IsSaneWorldPosition(entity.head_position))
        {
            continue;
        }
        const float distance_squared = LengthSquared(SubtractVector(
            entity.head_position, snapshot.player_aim_position));
        if (!std::isfinite(distance_squared) ||
            distance_squared > maximum_distance_squared)
        {
            continue;
        }
        Vector2 projected{};
        if (!WorldToScreen(
                entity.head_position, projected, snapshot.view_projection,
                snapshot.game_client_width, snapshot.game_client_height) ||
            projected.x < 0.0f ||
            projected.x > snapshot.game_client_width ||
            projected.y < 0.0f ||
            projected.y > snapshot.game_client_height)
        {
            continue;
        }
        const float signed_gap = projected.x - current_x;
        if ((horizontal_direction > 0 &&
             signed_gap < kMinimumHorizontalSeparationPixels) ||
            (horizontal_direction < 0 &&
             signed_gap > -kMinimumHorizontalSeparationPixels))
        {
            continue;
        }
        const float gap = std::fabs(signed_gap);
        if (!best || gap < best_horizontal_gap)
        {
            best = &entity;
            best_horizontal_gap = gap;
        }
    }
    return best;
}

void ClearAimbotPatch()
{
    g_aimbot = {};
}

void RestoreAimbot(TrainerProcess& process)
{
    if (!g_aimbot.applied)
    {
        ClearAimbotPatch();
        return;
    }
    if (!process.IsConnected() || process.ProcessId() != g_aimbot.process_id)
    {
        ClearAimbotPatch();
        return;
    }

    std::array<std::uint8_t, 9> current{};
    bool restored = process.ReadMemory(
        g_aimbot.hook, current.data(), current.size());
    if (restored && current != g_aimbot.original)
    {
        restored = current == g_aimbot.patch && process.WriteProtectedMemory(
            g_aimbot.hook,
            g_aimbot.original.data(),
            g_aimbot.original.size());
    }
    if (restored)
    {
        restored = process.ReadMemory(
            g_aimbot.hook, current.data(), current.size()) &&
            current == g_aimbot.original;
    }

    bool page_idle = false;
    if (restored)
    {
        for (int attempt = 0; attempt < 250 && !page_idle; ++attempt)
        {
            bool executing = false;
            page_idle = process.IsAnyThreadExecutingRange(
                g_aimbot.remote, 0x100, executing) && !executing;
            if (!page_idle)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    if (restored && page_idle && process.FreeRemoteMemory(g_aimbot.remote))
        ClearAimbotPatch();
}

bool ComputeAimbotCameraDelta(
    TrainerProcess& process,
    const RadarSnapshot& snapshot,
    const RadarEntity& target,
    Vector3& camera_delta)
{
    camera_delta = {};
    if (!IsSanePointer(snapshot.player_frame_address))
        return false;

    Matrix4x4 frame_matrix{};
    if (!process.ReadMemory(
            snapshot.player_frame_address + kFrameWorldMatrixOffset,
            frame_matrix))
        return false;

    Vector3 world_direction = SubtractVector(
        target.head_position, snapshot.player_aim_position);
    if (!NormalizeVector(world_direction))
        return false;

    const auto local_component = [&](int row, float& value)
    {
        const Vector3 axis{
            frame_matrix.m[row][0],
            frame_matrix.m[row][1],
            frame_matrix.m[row][2]};
        const float scale_squared = LengthSquared(axis);
        if (!std::isfinite(scale_squared) ||
            scale_squared < 0.01f || scale_squared > 100.0f)
        {
            return false;
        }
        value = DotVector(world_direction, axis) / scale_squared;
        return std::isfinite(value);
    };

    Vector3 local{};
    if (!local_component(0, local.x) ||
        !local_component(1, local.y) ||
        !local_component(2, local.z))
    {
        return false;
    }
    camera_delta = {local.x, -local.y, local.z};
    return NormalizeVector(camera_delta);
}

bool UpdateAimbotCameraHook(
    TrainerProcess& process,
    const RadarSnapshot& snapshot,
    const RadarEntity* target)
{
    constexpr std::uintptr_t kCameraTickRva = 0x0005E510;
    constexpr std::array<std::uint8_t, 9> kExpected{
        0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x64, 0x02, 0x00, 0x00};

    if (g_aimbot.applied && g_aimbot.process_id != process.ProcessId())
        RestoreAimbot(process);
    if (!g_aimbot.applied)
    {
        RemoteModuleInfo module{};
        if (!process.GetMainModuleInfo(module) ||
            kCameraTickRva >= module.image_size)
        {
            return false;
        }

        AimbotPatchState next{};
        next.process_id = process.ProcessId();
        next.hook = module.base_address + kCameraTickRva;
        if (!process.ReadMemory(
                next.hook, next.original.data(), next.original.size()) ||
            next.original != kExpected)
        {
            return false;
        }
        next.remote = process.AllocateRemoteMemory(kAimbotRemoteSize);
        if (!IsSanePointer(next.remote))
            return false;

        const std::uintptr_t shared_enabled =
            next.remote + kAimbotEnabledOffset;
        const std::uintptr_t shared_delta = next.remote + kAimbotDeltaOffset;
        const std::uintptr_t shared_observed =
            next.remote + kAimbotObservedDeltaOffset;
        const std::uintptr_t shared_sequence =
            next.remote + kAimbotObservedSequenceOffset;
        const auto relative32 = [](std::uintptr_t target_address,
                                   std::uintptr_t next_instruction)
        {
            return static_cast<std::uint32_t>(static_cast<std::int32_t>(
                static_cast<std::intptr_t>(target_address) -
                static_cast<std::intptr_t>(next_instruction)));
        };
        std::vector<std::uint8_t> code;
        const auto byte = [&](std::uint8_t value) { code.push_back(value); };
        const auto dword = [&](std::uint32_t value)
        {
            const std::size_t offset = code.size();
            code.resize(offset + 4);
            std::memcpy(code.data() + offset, &value, 4);
        };
        byte(0x9C); byte(0x60);             // pushfd; pushad
        for (std::uint8_t component = 0; component < 3; ++component)
        {
            byte(0x8B); byte(0x41);
            byte(static_cast<std::uint8_t>(0x14 + component * 4));
            byte(0xA3); dword(static_cast<std::uint32_t>(
                shared_observed + component * sizeof(float)));
        }
        byte(0xFF); byte(0x05);
        dword(static_cast<std::uint32_t>(shared_sequence));
        byte(0xA1); dword(static_cast<std::uint32_t>(shared_enabled));
        byte(0x85); byte(0xC0);             // test eax,eax
        byte(0x0F); byte(0x84);             // je skip override
        const std::size_t skip_displacement = code.size();
        dword(0);
        for (std::uint8_t component = 0; component < 3; ++component)
        {
            byte(0xA1); dword(static_cast<std::uint32_t>(
                shared_delta + component * sizeof(float)));
            byte(0x89); byte(0x41);
            byte(static_cast<std::uint8_t>(0x14 + component * 4));
        }
        const std::uintptr_t skip = next.remote + code.size();
        const std::uint32_t skip_relative = relative32(
            skip, next.remote + skip_displacement + 4);
        std::memcpy(code.data() + skip_displacement, &skip_relative, 4);
        byte(0x61); byte(0x9D);             // popad; popfd
        for (const std::uint8_t value : kExpected)
            byte(value);                    // displaced prologue
        byte(0xE9);
        dword(relative32(
            next.hook + kExpected.size(), next.remote + code.size() + 4));

        if (!process.WriteMemory(next.remote, code.data(), code.size()))
        {
            (void)process.FreeRemoteMemory(next.remote);
            return false;
        }
        next.patch = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90};
        const std::uint32_t hook_relative = relative32(
            next.remote, next.hook + 5);
        std::memcpy(next.patch.data() + 1, &hook_relative, 4);
        next.applied = true;
        g_aimbot = next;
        if (!process.WriteProtectedMemory(
                next.hook, next.patch.data(), next.patch.size()))
        {
            RestoreAimbot(process);
            return false;
        }
    }

    const std::uint32_t disabled = 0;
    if (!process.WriteMemory(
            g_aimbot.remote + kAimbotEnabledOffset, disabled))
    {
        return false;
    }
    if (!target)
        return true;

    Vector3 camera_delta{};
    if (!ComputeAimbotCameraDelta(
            process, snapshot, *target, camera_delta) ||
        !process.WriteMemory(
            g_aimbot.remote + kAimbotDeltaOffset, camera_delta))
    {
        return false;
    }
    const std::uint32_t enabled = 1;
    const bool first_command = !g_aimbot.command_valid;
    const bool enabled_written = process.WriteMemory(
        g_aimbot.remote + kAimbotEnabledOffset, enabled);
    if (enabled_written)
    {
        g_aimbot.last_commanded_delta = camera_delta;
        g_aimbot.command_valid = true;
        if (first_command)
        {
            (void)process.ReadMemory(
                g_aimbot.remote + kAimbotObservedSequenceOffset,
                g_aimbot.last_observed_sequence);
        }
    }
    return enabled_written;
}

int ConsumeAimbotHorizontalSwitchIntent(TrainerProcess& process)
{
    if (!g_aimbot.applied || !g_aimbot.command_valid ||
        g_aimbot.process_id != process.ProcessId())
    {
        return 0;
    }

    std::uint32_t sequence_before = 0;
    std::uint32_t sequence_after = 0;
    Vector3 observed{};
    if (!process.ReadMemory(
            g_aimbot.remote + kAimbotObservedSequenceOffset,
            sequence_before) ||
        sequence_before == g_aimbot.last_observed_sequence ||
        !process.ReadMemory(
            g_aimbot.remote + kAimbotObservedDeltaOffset, observed) ||
        !process.ReadMemory(
            g_aimbot.remote + kAimbotObservedSequenceOffset,
            sequence_after) ||
        sequence_before != sequence_after ||
        !IsSaneMoveDirection(observed))
    {
        return 0;
    }
    g_aimbot.last_observed_sequence = sequence_after;

    constexpr float kRapidHorizontalIntentThreshold = 0.10f;
    constexpr ULONGLONG kSwitchCooldownMs = 220;
    const float horizontal_change =
        observed.x - g_aimbot.last_commanded_delta.x;
    const ULONGLONG now = GetTickCount64();
    if (std::fabs(horizontal_change) < kRapidHorizontalIntentThreshold ||
        now - g_aimbot.last_switch_tick < kSwitchCooldownMs)
    {
        return 0;
    }
    g_aimbot.last_switch_tick = now;
    return horizontal_change > 0.0f ? 1 : -1;
}

void ClearBulletTrackHook()
{
    g_bullet_track = {};
}

void RestoreBulletTrackHook(TrainerProcess& process)
{
    if (!g_bullet_track.applied)
    {
        ClearBulletTrackHook();
        return;
    }
    if (!process.IsConnected() ||
        process.ProcessId() != g_bullet_track.process_id)
    {
        ClearBulletTrackHook();
        return;
    }
    const auto restore_site = [&process](
        std::uintptr_t address,
        const auto& original,
        const auto& patch)
    {
        using Bytes = std::decay_t<decltype(original)>;
        Bytes current{};
        bool restored = process.ReadMemory(
            address, current.data(), current.size());
        if (restored && current != original)
        {
            restored = current == patch &&
                process.WriteProtectedMemory(
                    address, original.data(), original.size());
        }
        return restored && process.ReadMemory(
            address, current.data(), current.size()) &&
            current == original;
    };
    const bool primary_restored = restore_site(
        g_bullet_track.hook,
        g_bullet_track.original,
        g_bullet_track.patch);
    const bool fallback_restored = restore_site(
        g_bullet_track.fallback_hook,
        g_bullet_track.fallback_original,
        g_bullet_track.fallback_patch);
    bool finish_restored = true;
    if (g_bullet_track.finish_hook != 0)
    {
        finish_restored = restore_site(
            g_bullet_track.finish_hook,
            g_bullet_track.finish_original,
            g_bullet_track.finish_patch);
    }
    const bool restored =
        primary_restored && fallback_restored && finish_restored;
    bool page_idle = false;
    if (restored)
    {
        for (int attempt = 0; attempt < 250 && !page_idle; ++attempt)
        {
            bool executing = false;
            page_idle = process.IsAnyThreadExecutingRange(
                g_bullet_track.remote,
                kBulletTrackRemoteSize,
                executing) && !executing;
            if (!page_idle)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    if (restored && page_idle &&
        process.FreeRemoteMemory(g_bullet_track.remote))
    {
        ClearBulletTrackHook();
    }
}

void DrainAndRestoreBulletTrackHook(
    TrainerProcess& process, bool wait_for_drain = false)
{
    if (!g_bullet_track.applied)
        return;
    if (!process.IsConnected() ||
        process.ProcessId() != g_bullet_track.process_id)
    {
        RestoreBulletTrackHook(process);
        return;
    }

    // Stop creating new forced recipients immediately, but keep the Finish
    // callback-frame guard alive long enough for projectiles already in flight
    // to reach Finish. Otherwise unchecking Bullet Track between Evaluate and
    // Finish could resurrect the exact null-sub_frame crash we fixed.
    const std::uint32_t disabled_player = 0;
    (void)process.WriteMemory(
        g_bullet_track.remote + kBulletTrackPlayerPublicationOffset,
        disabled_player);
    const ULONGLONG now = GetTickCount64();
    if (g_bullet_track.restore_after_tick == 0)
    {
        g_bullet_track.restore_after_tick = now + kBulletTrackDrainTimeMs;
        LogDiagnostic(
            "Bullet Track: publication disabled; draining in-flight "
            "projectiles for %llu ms before hook restoration.",
            static_cast<unsigned long long>(kBulletTrackDrainTimeMs));
    }
    if (wait_for_drain)
    {
        while (GetTickCount64() < g_bullet_track.restore_after_tick)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    else if (now < g_bullet_track.restore_after_tick)
    {
        return;
    }
    RestoreBulletTrackHook(process);
}

bool UpdateBulletTrackHook(
    TrainerProcess& process,
    const RadarSnapshot& snapshot,
    const RadarEntity* target,
    std::uintptr_t rapid_fire_actor,
    bool require_visible,
    bool instant_wall_impact)
{
    constexpr std::uintptr_t kCreateActorRva = 0x00037EB0;
    constexpr std::uintptr_t kResolvedCollisionRva = 0x00044E4B;
    // C_gun_shoot::Finish copies projectile.hit_frm into the stack-local
    // S_CB_hit::sub_frame at this instruction.  Forced recipients can have a
    // null native hit_frm; the receiving actor dereferences sub_frame without
    // checking it, which is the captured crash at hde.exe+0x1FB13.
    constexpr std::uintptr_t kFinishCallbackFrameRva = 0x00045677;
    constexpr std::array<std::uint8_t, 8> kExpected{
        0x56, 0x57, 0x8B, 0xF9, 0x8B, 0x4C, 0x24, 0x0C};
    constexpr std::array<std::uint8_t, 5> kFallbackExpected{
        0x8B, 0x46, 0x34, 0x85, 0xC0};
    constexpr std::array<std::uint8_t, 6> kFinishExpected{
        0x8B, 0x83, 0xA0, 0x00, 0x00, 0x00};
    constexpr std::size_t kRemoteSize = kBulletTrackRemoteSize;
    constexpr std::size_t kFallbackStubOffset = 0x180;
    constexpr std::size_t kFinishStubOffset = 0x600;
    constexpr std::size_t kPlayerOffset =
        kBulletTrackPlayerPublicationOffset;
    constexpr std::size_t kTargetOffset = 0x704;
    constexpr std::size_t kMissionOffset = 0x708;
    constexpr std::size_t kTargetActorOffset = 0x70C;
    constexpr std::size_t kDamageCountOffset = 0x710;
    // C_game_mission::actors is a std::vector<C_actor*> (begin/end/capacity).
    constexpr std::uint8_t kMissionActorsOffset = 0x68;
    constexpr std::uint32_t kMaximumLiveActorBytes = 0x4000;
    // Contiguous with the damage counter so a single read publishes the whole
    // diagnostic block to the journal.
    constexpr std::size_t kRejectBaseOffset = 0x714;
    // Scratch slot carrying the head frame across popad, written and read by
    // the same stub execution on the game thread.
    constexpr std::size_t kResolveFrameOffset = 0x744;
    // This slot remains stable so Finish can identify the controlled player's
    // projectiles even between radar refreshes.
    constexpr std::size_t kControlledPlayerOffset = 0x748;
    // Weapon modifiers publish this only after the equipped weapon and its
    // actor have been validated. The game-thread CreateActor hook resets that
    // actor's fire countdown after each actual projectile.
    constexpr std::size_t kRapidFireActorOffset = 0x74C;
    // The first actor is the normal crosshair target. The remaining entries
    // are the nearest valid enemies and are used only when that first target
    // was killed by an earlier projectile in the same rapid burst.
    constexpr std::size_t kCandidateActorsOffset = 0x750;
    constexpr std::size_t kRetargetCountOffset = 0x760;
    // Set only for a red/occluded target while the opt-in V2 checkbox is on.
    // The resolver still owns target validation; this merely asks the next
    // projectile tick to finish its already validated flight immediately.
    constexpr std::size_t kInstantWallImpactOffset = 0x764;

    if (g_bullet_track.applied &&
        g_bullet_track.process_id != process.ProcessId())
    {
        RestoreBulletTrackHook(process);
    }
    if (g_bullet_track.applied)
        g_bullet_track.restore_after_tick = 0;
    if (!g_bullet_track.applied)
    {
        RemoteModuleInfo module{};
        if (!process.GetMainModuleInfo(module) ||
            kResolvedCollisionRva + kFallbackExpected.size() >=
                module.image_size ||
            kFinishCallbackFrameRva + kFinishExpected.size() >=
                module.image_size)
        {
            return false;
        }
        BulletTrackHookState next{};
        next.process_id = process.ProcessId();
        next.hook = module.base_address + kCreateActorRva;
        next.fallback_hook =
            module.base_address + kResolvedCollisionRva;
        next.finish_hook =
            module.base_address + kFinishCallbackFrameRva;
        if (!process.ReadMemory(
                next.hook, next.original.data(), next.original.size()) ||
            next.original != kExpected ||
            !process.ReadMemory(
                next.fallback_hook,
                next.fallback_original.data(),
                next.fallback_original.size()) ||
            next.fallback_original != kFallbackExpected ||
            !process.ReadMemory(
                next.finish_hook,
                next.finish_original.data(),
                next.finish_original.size()) ||
            next.finish_original != kFinishExpected)
        {
            return false;
        }
        next.remote = process.AllocateRemoteMemory(kRemoteSize);
        if (!IsSanePointer(next.remote))
            return false;

        const auto relative32 = [](std::uintptr_t target_address,
                                   std::uintptr_t next_instruction)
        {
            return static_cast<std::uint32_t>(static_cast<std::int32_t>(
                static_cast<std::intptr_t>(target_address) -
                static_cast<std::intptr_t>(next_instruction)));
        };
        const std::uintptr_t stub = next.remote;
        const std::uintptr_t shared_player = next.remote + kPlayerOffset;
        const std::uintptr_t shared_target = next.remote + kTargetOffset;
        const std::uintptr_t shared_mission = next.remote + kMissionOffset;
        const std::uintptr_t shared_target_actor =
            next.remote + kTargetActorOffset;
        const std::uintptr_t shared_rapid_fire_actor =
            next.remote + kRapidFireActorOffset;
        const std::uintptr_t shared_candidate_actors =
            next.remote + kCandidateActorsOffset;
        const std::uintptr_t shared_instant_wall_impact =
            next.remote + kInstantWallImpactOffset;
        std::vector<std::uint8_t> code;
        const auto byte = [&](std::uint8_t value) { code.push_back(value); };
        const auto dword = [&](std::uint32_t value)
        {
            const std::size_t offset = code.size();
            code.resize(offset + 4);
            std::memcpy(code.data() + offset, &value, 4);
        };
        byte(0x9C); byte(0x60);             // pushfd; pushad
        byte(0x8B); byte(0x54); byte(0x24); byte(0x0C);
                                                // edx = ESP after pushfd
        byte(0x8B); byte(0x42); byte(0x08); // eax = actor type
        byte(0x83); byte(0xF8); byte(0x03); // cmp eax,ACTOR_GUN_SHOOT
        byte(0x0F); byte(0x85);             // jne skip redirect
        const std::size_t type_skip_displacement = code.size();
        dword(0);
        byte(0x8B); byte(0x52); byte(0x0C); // edx = S_gun_shoot_init*
        // Rapid fire used to set shoot_countdown from the trainer's render
        // loop. A held trigger can create another projectile between two
        // writes. Re-arm it here, after a real projectile is created and on
        // the game thread. This is independent from the head-target logic.
        byte(0xA1); dword(static_cast<std::uint32_t>(shared_rapid_fire_actor));
        byte(0x85); byte(0xC0);              // test rapid actor,rapid actor
        byte(0x0F); byte(0x84);              // jz after rapid re-arm
        const std::size_t rapid_empty_jump = code.size();
        dword(0);
        byte(0x39); byte(0x42); byte(0x1C); // cmp [shoot_init+shooter],eax
        byte(0x0F); byte(0x85);              // jne after rapid re-arm
        const std::size_t rapid_other_jump = code.size();
        dword(0);
        byte(0xC7); byte(0x80); dword(0x260); dword(0);
                                                // shooter.shoot_countdown=0
        const std::size_t after_rapid_rearm = code.size();
        const std::uint32_t rapid_empty_relative = relative32(
            stub + after_rapid_rearm, stub + rapid_empty_jump + 4);
        const std::uint32_t rapid_other_relative = relative32(
            stub + after_rapid_rearm, stub + rapid_other_jump + 4);
        std::memcpy(
            code.data() + rapid_empty_jump,
            &rapid_empty_relative, sizeof(rapid_empty_relative));
        std::memcpy(
            code.data() + rapid_other_jump,
            &rapid_other_relative, sizeof(rapid_other_relative));
        byte(0xA1); dword(static_cast<std::uint32_t>(shared_player));
        byte(0x39); byte(0x42); byte(0x1C); // cmp si.shooter,eax
        byte(0x0F); byte(0x85);             // jne skip redirect
        const std::size_t shooter_skip_displacement = code.size();
        dword(0);

        // The radar snapshot is published from another thread.  Validate the
        // actor against the mission's live actor vector before dereferencing
        // its head frame; type/root field checks alone also pass on a recently
        // freed CRT block.  EDI preserves S_gun_shoot_init while the scan uses
        // EDX for the candidate actor.
        std::vector<std::size_t> primary_skip_displacements{
            type_skip_displacement, shooter_skip_displacement};
        const auto primary_skip = [&](std::uint8_t condition)
        {
            byte(0x0F); byte(condition);
            const std::size_t displacement = code.size();
            dword(0);
            primary_skip_displacements.push_back(displacement);
        };
        byte(0x8B); byte(0xFA);             // mov edi,edx (shoot init)
        byte(0x8B); byte(0x15);
        dword(static_cast<std::uint32_t>(shared_target_actor));
        byte(0x85); byte(0xD2);             // test target actor,target actor
        primary_skip(0x84);                 // jz skip
        byte(0xA1); dword(static_cast<std::uint32_t>(shared_mission));
        byte(0x85); byte(0xC0);             // test mission,mission
        primary_skip(0x84);
        byte(0x8B); byte(0x48); byte(kMissionActorsOffset);
                                                // ecx=actors.begin
        byte(0x8B); byte(0x58); byte(kMissionActorsOffset + 4);
                                                // ebx=actors.end
        byte(0x85); byte(0xC9);             // test ecx,ecx
        primary_skip(0x84);
        byte(0x3B); byte(0xD9);             // cmp ebx,ecx
        primary_skip(0x82);                 // jb skip
        byte(0x8B); byte(0xC3);             // mov eax,ebx
        byte(0x2B); byte(0xC1);             // sub eax,ecx
        byte(0x3D); dword(kMaximumLiveActorBytes);
        primary_skip(0x87);                 // ja skip
        const std::size_t primary_scan_actor = code.size();
        byte(0x3B); byte(0xCB);             // cmp ecx,ebx
        primary_skip(0x83);                 // jae skip
        byte(0x39); byte(0x11);             // cmp [ecx],edx
        byte(0x0F); byte(0x84);             // je live actor
        const std::size_t primary_live_displacement = code.size();
        dword(0);
        byte(0x83); byte(0xC1); byte(0x04); // add ecx,4
        byte(0xE9);
        dword(relative32(
            stub + primary_scan_actor, stub + code.size() + 4));
        const std::size_t primary_live_actor = code.size();
        {
            const std::uint32_t relative = relative32(
                stub + primary_live_actor,
                stub + primary_live_displacement + 4);
            std::memcpy(
                code.data() + primary_live_displacement, &relative, 4);
        }
        byte(0x83); byte(0x7A); byte(0x1C); byte(0x02);
                                                // cmp actor.type,enemy
        primary_skip(0x85);
        byte(0x83); byte(0xBA); dword(0x254); byte(0x04);
                                                // cmp stay_mode,dead
        primary_skip(0x84);
        byte(0x8B); byte(0x42); byte(0x28); // eax=actor.frame
        byte(0x85); byte(0xC0);
        primary_skip(0x84);
        byte(0x39); byte(0x90); dword(0x80);// cmp [frame+80],actor
        primary_skip(0x85);
        byte(0x8B); byte(0x8A); dword(0x1C8); // ecx=actor.head
        byte(0x85); byte(0xC9);
        primary_skip(0x84);
        byte(0x3B); byte(0x0D);
        dword(static_cast<std::uint32_t>(shared_target));
                                                // current head == published
        primary_skip(0x85);
        byte(0x8B); byte(0xD7);             // mov edx,edi (shoot init)
        // This Ultimate Mod executable maps init+0x3C to C_gun_shoot::power.
        // init+0x34 instead becomes the cannon/network sender used by the LAN
        // post-shot packet path.  Writing damage there turns 20000 into a
        // pointer and crashes that path.  Keep the sender untouched.
        // C_gun_shoot still owns flight, collision, CB_HIT, death and network
        // replication.
        byte(0xC7); byte(0x42); byte(0x3C);
        dword(static_cast<std::uint32_t>(kBulletTrackLethalPower));
                                                    // si.power = lethal
        const auto emit_component = [&](std::uint32_t head_offset,
                                        std::uint8_t source_offset,
                                        std::uint8_t destination_offset)
        {
            byte(0xD9); byte(0x81); dword(head_offset);
            byte(0xD8); byte(0x62); byte(source_offset);
            byte(0xD9); byte(0x5A); byte(destination_offset);
        };
        emit_component(0xBC, 0, 0x0C);
        emit_component(0xC0, 4, 0x10);
        emit_component(0xC4, 8, 0x14);
        // C_gun_shoot's constructor normalizes si.dir itself. Calling the
        // unrelated RVA previously labelled Normalize left an x87 result on
        // the stack for every shot and is deliberately removed here.
        byte(0xC7); byte(0x42); byte(0x20); dword(0); // delay = 0
        byte(0xC7); byte(0x42); byte(0x38); dword(0); // aim_disp = null
        const std::uintptr_t skip = stub + code.size();
        for (const std::size_t displacement_offset :
             primary_skip_displacements)
        {
            const std::uint32_t relative = relative32(
                skip, stub + displacement_offset + 4);
            std::memcpy(
                code.data() + displacement_offset, &relative, 4);
        }
        byte(0x61); byte(0x9D);             // popad; popfd
        for (const std::uint8_t value : kExpected)
            byte(value);                    // displaced CreateActor prologue
        byte(0xE9);
        dword(relative32(
            next.hook + kExpected.size(), stub + code.size() + 4));
        if (code.size() > kFallbackStubOffset ||
            !process.WriteMemory(stub, code.data(), code.size()))
        {
            (void)process.FreeRemoteMemory(next.remote);
            return false;
        }

        const std::uintptr_t fallback_stub =
            next.remote + kFallbackStubOffset;
        std::vector<std::uint8_t> fallback_code;
        const auto fallback_byte = [&](std::uint8_t value)
        {
            fallback_code.push_back(value);
        };
        const auto fallback_dword = [&](std::uint32_t value)
        {
            const std::size_t offset = fallback_code.size();
            fallback_code.resize(offset + 4);
            std::memcpy(fallback_code.data() + offset, &value, 4);
        };
        const auto fallback_near_jump = [&](std::uint8_t condition)
        {
            fallback_byte(0x0F); fallback_byte(condition);
            const std::size_t displacement = fallback_code.size();
            fallback_dword(0);
            return displacement;
        };

        fallback_byte(0x9C); fallback_byte(0x60); // pushfd; pushad
        fallback_byte(0xA1);
        fallback_dword(static_cast<std::uint32_t>(shared_player));
        fallback_byte(0x85); fallback_byte(0xC0); // test eax,eax
        const std::size_t no_player = fallback_near_jump(0x84);
        fallback_byte(0x39); fallback_byte(0x86);
        fallback_dword(0x9C);                    // cmp [esi+shooter],eax
        const std::size_t wrong_shooter = fallback_near_jump(0x85);

        // A burst can outlive the radar selection that started it.  Walk the
        // small published reserve on the game thread: each candidate goes
        // through the same membership and ownership validation before it is
        // ever dereferenced.  Thus a dead first target immediately yields to
        // the next crosshair-ranked enemy instead of dropping the projectile.
        fallback_byte(0x31); fallback_byte(0xFF); // xor edi,edi (candidate)
        const std::size_t candidate_select = fallback_code.size();
        fallback_byte(0x8B); fallback_byte(0x14); fallback_byte(0xBD);
        fallback_dword(static_cast<std::uint32_t>(shared_candidate_actors));
                                                    // edx=candidates[edi]
        fallback_byte(0x85); fallback_byte(0xD2); // test edx,edx
        const auto candidate_retry_jump = [&]()
        {
            fallback_byte(0x0F); fallback_byte(0x84); // jz retry candidate
            const std::size_t displacement = fallback_code.size();
            fallback_dword(0);
            return displacement;
        };
        std::vector<std::size_t> candidate_retry_jumps;
        candidate_retry_jumps.push_back(candidate_retry_jump());

        // The published actor comes from an asynchronous radar snapshot and the
        // game may already have destroyed it. Freeing a CRT small block only
        // overwrites its first eight bytes, so type, stay_mode and the frame
        // pointer all still read as valid on a dead object: every field guard
        // below passes and the AddRef at actor+4 lands exactly on the heap
        // free-list link. Confirm membership in the live mission actor vector
        // first, on the game thread, where the list is coherent.
        fallback_byte(0xA1);
        fallback_dword(static_cast<std::uint32_t>(next.remote + kMissionOffset));
        fallback_byte(0x85); fallback_byte(0xC0); // test eax,eax
        const std::size_t no_mission = fallback_near_jump(0x84);
        fallback_byte(0x8B); fallback_byte(0x48);
        fallback_byte(kMissionActorsOffset);     // ecx = actors.begin
        fallback_byte(0x8B); fallback_byte(0x58);
        fallback_byte(kMissionActorsOffset + 4); // ebx = actors.end
        fallback_byte(0x8B); fallback_byte(0xC3); // mov eax,ebx
        fallback_byte(0x2B); fallback_byte(0xC1); // sub eax,ecx
        fallback_byte(0x3D);
        fallback_dword(kMaximumLiveActorBytes);  // bound a corrupted vector
        const std::size_t bad_actor_vector = fallback_near_jump(0x87);
        const std::size_t scan_actor = fallback_code.size();
        fallback_byte(0x3B); fallback_byte(0xCB); // cmp ecx,ebx
        candidate_retry_jumps.push_back(fallback_near_jump(0x83));
        fallback_byte(0x39); fallback_byte(0x11); // cmp [ecx],edx
        fallback_byte(0x74); fallback_byte(0x05); // je live_actor
        fallback_byte(0x83); fallback_byte(0xC1); fallback_byte(0x04);
        fallback_byte(0xEB);
        fallback_byte(static_cast<std::uint8_t>(
            static_cast<std::int8_t>(
                static_cast<std::intptr_t>(scan_actor) -
                static_cast<std::intptr_t>(fallback_code.size() + 1))));

        fallback_byte(0x83); fallback_byte(0x7A); fallback_byte(0x1C);
        fallback_byte(0x02);                    // cmp actor.type,enemy
        candidate_retry_jumps.push_back(fallback_near_jump(0x85));
        fallback_byte(0x83); fallback_byte(0xBA);
        fallback_dword(0x254); fallback_byte(0x04); // cmp stay_mode,dead
        candidate_retry_jumps.push_back(fallback_near_jump(0x84));
        fallback_byte(0x8B); fallback_byte(0x42); fallback_byte(0x28);
                                                    // eax=actor.frame
        fallback_byte(0x85); fallback_byte(0xC0);
        candidate_retry_jumps.push_back(fallback_near_jump(0x84));
        fallback_byte(0x39); fallback_byte(0x90);
        fallback_dword(0x80);
                                                    // cmp [frame+80],actor
        candidate_retry_jumps.push_back(fallback_near_jump(0x85));
        fallback_byte(0x8B); fallback_byte(0x8A);
        fallback_dword(0x1C8);                  // ecx=actor.frm_head
        fallback_byte(0x85); fallback_byte(0xC9); // test ecx,ecx
        candidate_retry_jumps.push_back(fallback_near_jump(0x84));

        // hit_actor is never written by hand any more. Evaluate's own resolver
        // (00444E52..00444E85) walks the frame to its actor, retries through
        // the parent chain and takes the reference the engine expects. The
        // manual store plus inc [actor+4] used until V23.8 skipped that path
        // and left a dangling C_actor reference behind, which killed the game
        // one to two seconds later inside a Release loop. Only act while the
        // projectile has no recipient yet, then hand the head frame over.
        fallback_byte(0x83); fallback_byte(0xBE);
        fallback_dword(0x98); fallback_byte(0x00); // cmp hit_actor,0
        const std::size_t busy_actor = fallback_near_jump(0x85);
        fallback_byte(0x8B); fallback_byte(0xDA);   // ebx = validated actor
        fallback_byte(0xFF); fallback_byte(0x05);
        fallback_dword(static_cast<std::uint32_t>(
            next.remote + kDamageCountOffset));     // diagnostic shot count

        // projectile.hit_frm is deliberately left exactly as the engine set
        // it: Finish reuses that persistent smart pointer for its material and
        // sound path. The third hook changes only the stack-local sub_frame
        // passed synchronously to the damage callback, then the native
        // persistent value is read again unchanged at 004456C8.

        // hit_dest is written again, and only it. C_gun_shoot::Finish passes it
        // to the damage callback as S_CB_hit::hit_pos, which the receiving
        // enemy uses to place the impact on its body. Left at the native value
        // it points at whatever the ray touched — a wall, tens of metres away
        // from the target — so a shot aimed through a wall registered as
        // forced damage without wounding anybody. The head position puts the
        // impact back on the actor that actually receives it. hit_frm, dist
        // and do_smoke stay untouched.
        fallback_byte(0x8B); fallback_byte(0xD3);   // edx = validated actor
        fallback_byte(0x8B); fallback_byte(0x8A);
        fallback_dword(0x1C8);                  // ecx = head frame
        const auto copy_head_component = [&](std::uint32_t source,
                                             std::uint8_t destination)
        {
            fallback_byte(0x8B); fallback_byte(0x81);
            fallback_dword(source);            // mov eax,[ecx+source]
            fallback_byte(0x89); fallback_byte(0x46);
            fallback_byte(destination);        // mov [esi+dest],eax
        };
        copy_head_component(0xBC, 0x70);
        copy_head_component(0xC0, 0x74);
        copy_head_component(0xC4, 0x78);

        // V2 is deliberately after every candidate/membership check above.
        // `dist` is only the remaining native flight time: zero makes the
        // ordinary Tick mark this projectile complete and invoke Finish on
        // its following frame. It does not alter hit_actor, hit_frm, the
        // CB_HIT callback, or any network-owned object.
        fallback_byte(0xA1);
        fallback_dword(static_cast<std::uint32_t>(shared_instant_wall_impact));
        fallback_byte(0x85); fallback_byte(0xC0);
        const std::size_t no_instant_wall_impact = fallback_near_jump(0x84);
        fallback_byte(0xC7); fallback_byte(0x86);
        fallback_dword(0x8C); fallback_dword(0); // projectile.dist = 0
        const std::size_t after_instant_wall_impact = fallback_code.size();
        {
            const std::uint32_t relative = relative32(
                fallback_stub + after_instant_wall_impact,
                fallback_stub + no_instant_wall_impact + 4);
            std::memcpy(
                fallback_code.data() + no_instant_wall_impact,
                &relative, sizeof(relative));
        }

        // do_smoke and hit_norm are still not written: everything this stub
        // stopped imposing bought stability, and only hit_dest has been given
        // back because the damage callback genuinely needs it.
        // dist is deliberately left to the engine. It controls only how long
        // the projectile/trail lives; when the native flight ends, Finish uses
        // the already resolved recipient and the published head position, so
        // target acquisition and damage have no metric-distance cutoff.
        // popad restores the engine's own ECX, so the head frame is handed to
        // the native resolver through a scratch slot written on this very
        // execution of the stub, on the game thread.
        fallback_byte(0x89); fallback_byte(0x0D);
        fallback_dword(static_cast<std::uint32_t>(
            next.remote + kResolveFrameOffset));
        fallback_byte(0x61); fallback_byte(0x9D); // popad; popfd
        // ECX is the collision frame the resolver at 00444E52 walks up to an
        // actor. Give it the selected head instead of the geometry the ray
        // happened to touch, and let the engine set hit_actor and take the
        // reference itself.
        fallback_byte(0x8B); fallback_byte(0x0D);
        fallback_dword(static_cast<std::uint32_t>(
            next.remote + kResolveFrameOffset));
        for (const std::uint8_t value : kFallbackExpected)
            fallback_byte(value);
        fallback_byte(0xE9);
        fallback_dword(relative32(
            next.fallback_hook + kFallbackExpected.size(),
            fallback_stub + fallback_code.size() + 4));

        // All candidate validation failures land here.  The counter is
        // deliberately separate from rejection counters: changing target is
        // a successful recovery, not a lost bullet.
        const std::size_t candidate_retry = fallback_code.size();
        fallback_byte(0xFF); fallback_byte(0x05);
        fallback_dword(static_cast<std::uint32_t>(
            next.remote + kRetargetCountOffset));
        fallback_byte(0x47);                    // inc edi
        fallback_byte(0x83); fallback_byte(0xFF);
        fallback_byte(static_cast<std::uint8_t>(kBulletTrackCandidateCount));
        fallback_byte(0x0F); fallback_byte(0x82); // jb candidate_select
        fallback_dword(relative32(
            fallback_stub + candidate_select,
            fallback_stub + fallback_code.size() + 4));
        fallback_byte(0xE9);                     // reserve exhausted
        const std::size_t no_target_actor = fallback_code.size();
        fallback_dword(0);
        for (const std::size_t displacement : candidate_retry_jumps)
        {
            const std::uint32_t relative = relative32(
                fallback_stub + candidate_retry,
                fallback_stub + displacement + 4);
            std::memcpy(
                fallback_code.data() + displacement, &relative,
                sizeof(relative));
        }

        // Each refused shot increments its own counter before rejoining the
        // native path, so the test journal can separate "this shot was never
        // selected" from "a filter refused the published target". These blocks
        // sit after the unconditional return above and are only reachable
        // through their conditional jump.
        struct RejectJump
        {
            std::size_t displacement = 0;
            std::size_t counter_index = 0;
        };
        // Candidate-local failures are recovered above.  Only a failure that
        // applies to the projectile or to the whole mission falls through to
        // the native path and is counted as a rejected shot.
        const std::array<RejectJump, 6> reject_jumps{{
            {no_player, 0},
            {wrong_shooter, 1},
            {no_target_actor, 2},
            {no_mission, 3},
            {bad_actor_vector, 4},
            {busy_actor, 11}}};
        std::array<std::size_t, reject_jumps.size()> reject_entries{};
        std::array<std::size_t, reject_jumps.size()> reject_tails{};
        for (std::size_t index = 0; index < reject_jumps.size(); ++index)
        {
            reject_entries[index] = fallback_code.size();
            fallback_byte(0xFF); fallback_byte(0x05);
            fallback_dword(static_cast<std::uint32_t>(
                next.remote + kRejectBaseOffset +
                reject_jumps[index].counter_index * sizeof(std::uint32_t)));
            fallback_byte(0xE9);
            reject_tails[index] = fallback_code.size();
            fallback_dword(0);
        }
        const std::size_t passthrough = fallback_code.size();
        for (std::size_t index = 0; index < reject_jumps.size(); ++index)
        {
            const std::uint32_t to_counter = relative32(
                fallback_stub + reject_entries[index],
                fallback_stub + reject_jumps[index].displacement + 4);
            std::memcpy(
                fallback_code.data() + reject_jumps[index].displacement,
                &to_counter, 4);
            const std::uint32_t to_passthrough = relative32(
                fallback_stub + passthrough,
                fallback_stub + reject_tails[index] + 4);
            std::memcpy(
                fallback_code.data() + reject_tails[index], &to_passthrough, 4);
        }
        fallback_byte(0x61); fallback_byte(0x9D); // popad; popfd
        for (const std::uint8_t value : kFallbackExpected)
            fallback_byte(value);
        fallback_byte(0xE9);
        fallback_dword(relative32(
            next.fallback_hook + kFallbackExpected.size(),
            fallback_stub + fallback_code.size() + 4));
        if (fallback_code.size() >
                kFinishStubOffset - kFallbackStubOffset ||
            !process.WriteMemory(
                fallback_stub,
                fallback_code.data(),
                fallback_code.size()))
        {
            (void)process.FreeRemoteMemory(next.remote);
            return false;
        }

        // Finish builds S_CB_hit on its own stack.  Override only the EAX
        // value copied into that temporary callback: projectile.hit_frm at
        // [ebx+A0] remains untouched, so the engine keeps sole ownership of
        // its smart pointer and its later material lookup sees the native
        // frame.  This supplies a valid head sub-frame to the damage receiver
        // without AddRef, Release, or any persistent object write.
        const std::uintptr_t finish_stub =
            next.remote + kFinishStubOffset;
        const std::uintptr_t controlled_player =
            next.remote + kControlledPlayerOffset;
        std::vector<std::uint8_t> finish_code;
        const auto finish_byte = [&](std::uint8_t value)
        {
            finish_code.push_back(value);
        };
        const auto finish_dword = [&](std::uint32_t value)
        {
            const std::size_t offset = finish_code.size();
            finish_code.resize(offset + 4);
            std::memcpy(finish_code.data() + offset, &value, 4);
        };
        const auto finish_native_jump = [&](std::uint8_t condition)
        {
            finish_byte(0x0F); finish_byte(condition);
            const std::size_t displacement = finish_code.size();
            finish_dword(0);
            return displacement;
        };
        std::vector<std::size_t> finish_native_jumps;
        finish_byte(0x9C); finish_byte(0x60); // pushfd; pushad
        finish_byte(0xA1);
        finish_dword(static_cast<std::uint32_t>(controlled_player));
        finish_byte(0x85); finish_byte(0xC0); // test player,player
        finish_native_jumps.push_back(finish_native_jump(0x84));
        finish_byte(0x39); finish_byte(0x83); finish_dword(0x9C);
                                                // cmp [projectile+9C],player
        finish_native_jumps.push_back(finish_native_jump(0x85));
        finish_byte(0x8B); finish_byte(0x93); finish_dword(0x98);
                                                // edx=projectile.hit_actor
        finish_byte(0x85); finish_byte(0xD2);
        finish_native_jumps.push_back(finish_native_jump(0x84));
        finish_byte(0x83); finish_byte(0x7A); finish_byte(0x1C);
        finish_byte(0x02);                    // enemy actor only
        finish_native_jumps.push_back(finish_native_jump(0x85));
        finish_byte(0x83); finish_byte(0xBA); finish_dword(0x254);
        finish_byte(0x04);                    // dead actor uses native branch
        finish_native_jumps.push_back(finish_native_jump(0x84));
        finish_byte(0x8B); finish_byte(0x4A); finish_byte(0x28);
                                                // ecx=actor root frame
        finish_byte(0x85); finish_byte(0xC9);
        finish_native_jumps.push_back(finish_native_jump(0x84));
        finish_byte(0x39); finish_byte(0x91); finish_dword(0x80);
                                                // root still owns actor
        finish_native_jumps.push_back(finish_native_jump(0x85));
        finish_byte(0x8B); finish_byte(0x82); finish_dword(0x1C8);
                                                // eax=current head frame
        finish_byte(0x85); finish_byte(0xC0);
        finish_byte(0x0F); finish_byte(0x85); // jnz use head
        const std::size_t finish_use_head_jump = finish_code.size();
        finish_dword(0);
        finish_byte(0x8B); finish_byte(0xC1); // no head: valid root fallback
        const std::size_t finish_use_frame = finish_code.size();
        {
            const std::uint32_t relative = relative32(
                finish_stub + finish_use_frame,
                finish_stub + finish_use_head_jump + 4);
            std::memcpy(
                finish_code.data() + finish_use_head_jump, &relative, 4);
        }
        finish_byte(0x89); finish_byte(0x44); finish_byte(0x24);
        finish_byte(0x1C);                    // saved EAX = callback frame
        finish_byte(0x61); finish_byte(0x9D); // popad; popfd
        finish_byte(0xE9);
        finish_dword(relative32(
            next.finish_hook + kFinishExpected.size(),
            finish_stub + finish_code.size() + 4));

        const std::size_t finish_native = finish_code.size();
        for (const std::size_t displacement : finish_native_jumps)
        {
            const std::uint32_t relative = relative32(
                finish_stub + finish_native,
                finish_stub + displacement + 4);
            std::memcpy(
                finish_code.data() + displacement, &relative, 4);
        }
        finish_byte(0x61); finish_byte(0x9D); // popad; popfd
        for (const std::uint8_t value : kFinishExpected)
            finish_byte(value);               // native mov eax,[ebx+A0]
        finish_byte(0xE9);
        finish_dword(relative32(
            next.finish_hook + kFinishExpected.size(),
            finish_stub + finish_code.size() + 4));
        if (finish_code.size() > kPlayerOffset - kFinishStubOffset ||
            !process.WriteMemory(
                finish_stub, finish_code.data(), finish_code.size()))
        {
            (void)process.FreeRemoteMemory(next.remote);
            return false;
        }

        next.patch = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90};
        const std::uint32_t displacement = relative32(
            next.remote, next.hook + 5);
        std::memcpy(next.patch.data() + 1, &displacement, 4);
        next.fallback_patch.fill(0x90);
        next.fallback_patch[0] = 0xE9;
        const std::uint32_t fallback_displacement = relative32(
            fallback_stub, next.fallback_hook + 5);
        std::memcpy(
            next.fallback_patch.data() + 1,
            &fallback_displacement,
            sizeof(fallback_displacement));
        next.finish_patch.fill(0x90);
        next.finish_patch[0] = 0xE9;
        const std::uint32_t finish_displacement = relative32(
            finish_stub, next.finish_hook + 5);
        std::memcpy(
            next.finish_patch.data() + 1,
            &finish_displacement,
            sizeof(finish_displacement));

        // Publish the complete restoration state before touching either hook.
        // WriteProtectedMemory can report a late protection/cache failure after
        // WriteProcessMemory already installed the jump; restoration must still
        // recognize that exact patch and keep the remote page alive if needed.
        next.applied = true;
        g_bullet_track = next;
        if (!process.WriteProtectedMemory(
                next.finish_hook,
                next.finish_patch.data(),
                next.finish_patch.size()))
        {
            RestoreBulletTrackHook(process);
            return false;
        }
        if (!process.WriteProtectedMemory(
                next.fallback_hook,
                next.fallback_patch.data(),
                next.fallback_patch.size()))
        {
            RestoreBulletTrackHook(process);
            return false;
        }
        if (!process.WriteProtectedMemory(
                next.hook, next.patch.data(), next.patch.size()))
        {
            RestoreBulletTrackHook(process);
            return false;
        }
        LogDiagnostic(
            "Bullet Track: unlimited head-damage hooks installed "
            "create=%08X resolve=%08X finish=%08X reserve=%u.",
            static_cast<unsigned>(next.hook),
            static_cast<unsigned>(next.fallback_hook),
            static_cast<unsigned>(next.finish_hook),
            static_cast<unsigned>(kBulletTrackCandidateCount));
    }

    const std::uint32_t player =
        static_cast<std::uint32_t>(snapshot.player_object_address);
    if (!process.WriteMemory(
            g_bullet_track.remote + kControlledPlayerOffset, player) ||
        !process.WriteMemory(
            g_bullet_track.remote + kRapidFireActorOffset,
            static_cast<std::uint32_t>(rapid_fire_actor)) ||
        !process.WriteMemory(
            g_bullet_track.remote + kInstantWallImpactOffset,
            instant_wall_impact ? 1U : 0U))
    {
        return false;
    }
    std::array<std::uint32_t, kBulletTrackCounterCount> counters{};
    std::uint32_t retarget_count = 0;
    if (process.ReadMemory(
            g_bullet_track.remote + kDamageCountOffset, counters) &&
        process.ReadMemory(
            g_bullet_track.remote + kRetargetCountOffset, retarget_count) &&
        (counters != g_bullet_track.last_counters ||
         retarget_count != g_bullet_track.last_retarget_count))
    {
        const ULONGLONG now = GetTickCount64();
        if (now - g_bullet_track.last_damage_log_tick >= 250ULL)
        {
            const auto& previous = g_bullet_track.last_counters;
            LogDiagnostic(
                "[TEST BULLET TRACK] forced_damage_count=%u delta=%u "
                "reject no_player=%u(+%u) wrong_shooter=%u(+%u) "
                "no_actor=%u(+%u) no_mission=%u(+%u) bad_vector=%u(+%u) "
                "stale_actor=%u(+%u) wrong_type=%u(+%u) dead=%u(+%u) "
                "no_root=%u(+%u) wrong_root=%u(+%u) no_head=%u(+%u) "
                "busy_actor=%u(+%u) retarget=%u(+%u).",
                counters[0], counters[0] - previous[0],
                counters[1], counters[1] - previous[1],
                counters[2], counters[2] - previous[2],
                counters[3], counters[3] - previous[3],
                counters[4], counters[4] - previous[4],
                counters[5], counters[5] - previous[5],
                counters[6], counters[6] - previous[6],
                counters[7], counters[7] - previous[7],
                counters[8], counters[8] - previous[8],
                counters[9], counters[9] - previous[9],
                counters[10], counters[10] - previous[10],
                counters[11], counters[11] - previous[11],
                counters[12], counters[12] - previous[12],
                retarget_count,
                retarget_count - g_bullet_track.last_retarget_count);
            g_bullet_track.last_damage_log_tick = now;
            g_bullet_track.last_counters = counters;
            g_bullet_track.last_retarget_count = retarget_count;
        }
    }
    // A cached snapshot is the last known state of a mission whose pointer was
    // transiently unreadable. Its actor addresses may already be freed, so the
    // hooks stay installed but no target is published from it.
    if (snapshot.from_cache)
        return true;
    if (!target)
    {
        // Do not let an occluded target that has just died remain in the
        // remote publication until a later radar pass happens to find another
        // enemy.  With through-walls enabled this was visible as a short burst
        // still following the old corpse.  A fresh snapshot with no eligible
        // target deliberately restores native projectile direction; the next
        // live candidate is published normally on the next update.
        const std::uint32_t none = 0;
        const std::array<std::uint32_t, kBulletTrackCandidateCount> no_candidates{};
        return process.WriteMemory(
                   g_bullet_track.remote + kTargetOffset, none) &&
            process.WriteMemory(
                g_bullet_track.remote + kTargetActorOffset, none) &&
            process.WriteMemory(
                g_bullet_track.remote + kCandidateActorsOffset,
                no_candidates.data(), sizeof(no_candidates));
    }
    const std::uint32_t head_frame =
        static_cast<std::uint32_t>(target->head_frame_address);
    const std::uint32_t target_actor =
        static_cast<std::uint32_t>(target->actor_address);
    const auto candidate_actors = CollectBulletTrackCandidateActors(
        snapshot, target, require_visible);
    const std::uint32_t mission =
        static_cast<std::uint32_t>(snapshot.entity_list_object_address);
    if (mission == 0)
        return true;
    // Keep the previous player publication live while the three target values
    // are refreshed. Each trampoline independently proves that head, actor and
    // mission still agree, so a mixed in-flight update safely falls through to
    // the native path instead of losing a rapid-fire bullet to a deliberate
    // `no_player` gap.
    return process.WriteMemory(
               g_bullet_track.remote + kTargetOffset,
               head_frame) &&
        process.WriteMemory(
            g_bullet_track.remote + kMissionOffset,
            mission) &&
        process.WriteMemory(
            g_bullet_track.remote + kTargetActorOffset,
            target_actor) &&
        process.WriteMemory(
            g_bullet_track.remote + kCandidateActorsOffset,
            candidate_actors.data(), sizeof(candidate_actors)) &&
        process.WriteMemory(g_bullet_track.remote + kPlayerOffset, player);
}

void UpdateAimbot(
    TrainerProcess& process,
    const RadarSnapshot* snapshot,
    const GameplaySettings& settings,
    GameplayStatus& status,
    std::uintptr_t rapid_fire_actor)
{
    status.bullet_track_visual_target_actor = 0;
    const auto set_statuses = [&](AimbotStatus aimbot,
                                  BulletTrackStatus bullet_track)
    {
        status.aimbot = settings.aimbot_enabled
            ? aimbot : AimbotStatus::Disabled;
        status.bullet_track = settings.bullet_track_enabled
            ? bullet_track : BulletTrackStatus::Disabled;
    };
    if (!settings.aimbot_enabled && !settings.bullet_track_enabled)
    {
        RestoreAimbot(process);
        DrainAndRestoreBulletTrackHook(process);
        set_statuses(
            AimbotStatus::Disabled, BulletTrackStatus::Disabled);
        return;
    }
    if (!process.IsConnected() || !snapshot ||
        !snapshot->view_projection_valid ||
        !IsSanePointer(snapshot->player_object_address))
    {
        RestoreAimbot(process);
        DrainAndRestoreBulletTrackHook(process);
        set_statuses(
            AimbotStatus::WaitingForMission,
            BulletTrackStatus::WaitingForMission);
        return;
    }
    const std::uintptr_t locked_actor =
        g_aimbot.applied && g_aimbot.process_id == process.ProcessId()
        ? g_aimbot.actor : 0;
    const int switch_direction = settings.aimbot_enabled
        ? ConsumeAimbotHorizontalSwitchIntent(process) : 0;
    const RadarEntity* aimbot_target = nullptr;
    // Both features aim at a live actor address; a cached snapshot keeps the
    // hooks alive across a transient mission loss but must never select one.
    if (settings.aimbot_enabled && !snapshot->from_cache)
    {
        // 0 = aucune limite de distance, exactement comme Bullet Track.
        const float aimbot_range = settings.aimbot_unlimited_distance
            ? 0.0f
            : settings.aimbot_max_distance_m;
        if (switch_direction != 0)
        {
            aimbot_target = SelectDirectionalAimbotTarget(
                *snapshot, aimbot_range,
                locked_actor, switch_direction);
        }
        if (!aimbot_target)
        {
            aimbot_target = SelectAimbotTarget(
                *snapshot, aimbot_range,
                false, settings.bullet_track_radius_pixels,
                locked_actor);
        }
    }
    std::array<std::uint32_t, 4> bullet_tally{};
    const RadarEntity* bullet_target = settings.bullet_track_enabled
        ? SelectAimbotTarget(
            *snapshot, 0.0f,
            false, settings.bullet_track_radius_pixels, 0, true,
            !settings.bullets_through_walls, &bullet_tally)
        : nullptr;
    const bool instant_wall_impact =
        settings.bullet_track_instant_wall_impact_v2 &&
        settings.bullets_through_walls && bullet_target != nullptr &&
        bullet_target->directly_visible == 0;
    if (instant_wall_impact)
    {
        status.bullet_track_visual_target_actor =
            bullet_target->actor_address;
    }
    if (settings.bullet_track_enabled)
    {
        static std::array<std::uint32_t, 4> logged_tally{};
        static bool logged_found = false;
        const bool found = bullet_target != nullptr;
        if (bullet_tally != logged_tally || found != logged_found)
        {
            logged_tally = bullet_tally;
            logged_found = found;
            LogDiagnostic(
                "[TEST TARGET] enemies=%u rejected: no_head=%u hidden=%u "
                "off_screen=%u selected=%d.",
                bullet_tally[0], bullet_tally[1], bullet_tally[2],
                bullet_tally[3], found ? 1 : 0);
        }
    }
    if (settings.bullet_track_enabled)
    {
        if (!UpdateBulletTrackHook(
                process, *snapshot, bullet_target, rapid_fire_actor,
                !settings.bullets_through_walls, instant_wall_impact))
        {
            RestoreAimbot(process);
            DrainAndRestoreBulletTrackHook(process);
            set_statuses(
                AimbotStatus::WriteFailed, BulletTrackStatus::WriteFailed);
            return;
        }
    }
    else
    {
        DrainAndRestoreBulletTrackHook(process);
    }
    const BulletTrackStatus bullet_status = !settings.bullet_track_enabled
        ? BulletTrackStatus::Disabled
        : (bullet_target
            ? BulletTrackStatus::Active
            : BulletTrackStatus::NoTargetInCircle);
    if (!settings.aimbot_enabled)
    {
        RestoreAimbot(process);
        status.aimbot = AimbotStatus::Disabled;
        status.bullet_track = bullet_status;
        return;
    }
    const RadarEntity* target = aimbot_target;
    if (!target)
    {
        if (!UpdateAimbotCameraHook(process, *snapshot, nullptr))
        {
            set_statuses(AimbotStatus::WriteFailed, bullet_status);
            return;
        }
        g_aimbot.actor = 0;
        set_statuses(
            AimbotStatus::NoTarget, bullet_status);
        return;
    }

    if (!UpdateAimbotCameraHook(process, *snapshot, target))
    {
        set_statuses(AimbotStatus::WriteFailed, bullet_status);
        return;
    }
    g_aimbot.actor = target->actor_address;
    status.aimbot = AimbotStatus::Active;
    status.bullet_track = bullet_status;
}

void ClearVehicleSpeedPatch()
{
    g_vehicle_speed = {};
}

void RestoreVehicleSpeed(TrainerProcess& process)
{
    RemoveWheelTurnHook(process);
    g_vehicle_throttle = {};
    if (!g_vehicle_speed.applied)
        return;
    if (!process.IsConnected() ||
        g_vehicle_speed.process_id != process.ProcessId())
    {
        ClearVehicleSpeedPatch();
        return;
    }

    (void)process.WriteProtectedMemory(
        g_vehicle_speed.target_hook,
        g_vehicle_speed.target_original.data(),
        g_vehicle_speed.target_original.size());
    (void)process.WriteProtectedMemory(
        g_vehicle_speed.acceleration_hook,
        g_vehicle_speed.acceleration_original.data(),
        g_vehicle_speed.acceleration_original.size());

    std::array<std::uint8_t, 7> target_current{};
    std::array<std::uint8_t, 6> acceleration_current{};
    const bool restored = process.ReadMemory(
            g_vehicle_speed.target_hook,
            target_current.data(), target_current.size()) &&
        target_current == g_vehicle_speed.target_original &&
        process.ReadMemory(
            g_vehicle_speed.acceleration_hook,
            acceleration_current.data(), acceleration_current.size()) &&
        acceleration_current == g_vehicle_speed.acceleration_original;
    if (!restored)
        return;

    bool idle = false;
    for (int attempt = 0; attempt < 250 && !idle; ++attempt)
    {
        bool executing = true;
        idle = process.IsAnyThreadExecutingRange(
            g_vehicle_speed.remote,
            kVehicleSpeedRemoteSize,
            executing) && !executing;
        if (!idle)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (!idle || !process.FreeRemoteMemory(g_vehicle_speed.remote))
    {
        LogDiagnostic(
            "Vehicle: hook restore incomplete (idle=%d).",
            idle ? 1 : 0);
        return;
    }
    LogDiagnostic("Vehicle: hooks restored and remote page freed.");
    ClearVehicleSpeedPatch();
}

bool UpdateVehicleSpeedHook(
    TrainerProcess& process,
    std::uintptr_t controlled_vehicle,
    float multiplier)
{
    constexpr std::array<std::uint8_t, 7> kExpectedTarget{
        0xD8, 0x0C, 0x95, 0x44, 0x7C, 0x4F, 0x00};
    constexpr std::array<std::uint8_t, 6> kExpectedAcceleration{
        0xD9, 0x44, 0x24, 0x20, 0xD8, 0x08};

    if (g_vehicle_speed.applied &&
        g_vehicle_speed.process_id != process.ProcessId())
    {
        ClearVehicleSpeedPatch();
    }
    if (g_vehicle_speed.applied)
    {
        const std::uint32_t vehicle32 = static_cast<std::uint32_t>(
            controlled_vehicle);
        if (!process.WriteMemory(
                g_vehicle_speed.remote + kVehicleAddressRemoteOffset,
                vehicle32) ||
            !process.WriteMemory(
                g_vehicle_speed.remote + kVehicleMultiplierRemoteOffset,
                multiplier))
        {
            return false;
        }
        g_vehicle_speed.vehicle = controlled_vehicle;
        return true;
    }

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        kAutomobileTargetSpeedHookRva + kExpectedTarget.size() >
            module.image_size ||
        kAutomobileAccelerationHookRva + kExpectedAcceleration.size() >
            module.image_size)
    {
        {
            // V126 - une seule ligne toutes les dix secondes.
            //
            // Le journal du joueur comptait SIX MILLE CINQ CENT SOIXANTE-NEUF
            // fois ce message, pour six mille deux cent vingt-deux lignes en
            // tout : il represente 97 % du fichier et 740 Ko. Il est emis a
            // chaque image des que le jeu n'est plus la, et il noyait tout ce
            // qui compte. Un journal illisible ne sert personne.
            static ULONGLONG next_report = 0;
            const ULONGLONG now = GetTickCount64();
            if (now >= next_report)
            {
                next_report = now + 10000ULL;
                LogDiagnostic(
                    "Vehicle: module info unavailable for hook install "
                    "(message limite a un par dix secondes).");
            }
        }
        return false;
    }
    const std::uintptr_t target_hook =
        module.base_address + kAutomobileTargetSpeedHookRva;
    const std::uintptr_t acceleration_hook =
        module.base_address + kAutomobileAccelerationHookRva;
    std::array<std::uint8_t, 7> target_original{};
    std::array<std::uint8_t, 6> acceleration_original{};
    const bool target_read = process.ReadMemory(
        target_hook, target_original.data(), target_original.size());
    const bool acceleration_read = process.ReadMemory(
        acceleration_hook,
        acceleration_original.data(), acceleration_original.size());
    if (!target_read || target_original != kExpectedTarget ||
        !acceleration_read ||
        acceleration_original != kExpectedAcceleration)
    {
        LogDiagnostic(
            "Vehicle: hook signature mismatch (target_read=%d "
            "target_match=%d acceleration_read=%d acceleration_match=%d).",
            target_read ? 1 : 0,
            target_original == kExpectedTarget ? 1 : 0,
            acceleration_read ? 1 : 0,
            acceleration_original == kExpectedAcceleration ? 1 : 0);
        return false;
    }

    const std::uintptr_t remote = process.AllocateRemoteMemory(
        kVehicleSpeedRemoteSize);
    if (!IsSanePointer(remote))
    {
        LogDiagnostic("Vehicle: remote allocation failed.");
        return false;
    }
    const std::uintptr_t remote_vehicle =
        remote + kVehicleAddressRemoteOffset;
    const std::uintptr_t remote_multiplier =
        remote + kVehicleMultiplierRemoteOffset;
    const auto relative = [](std::uintptr_t target, std::uintptr_t next)
    {
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<std::intptr_t>(target) -
            static_cast<std::intptr_t>(next)));
    };
    const auto append_dword = [](std::vector<std::uint8_t>& code,
                                 std::uint32_t value)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(value));
        std::memcpy(code.data() + offset, &value, sizeof(value));
    };

    std::vector<std::uint8_t> code;
    code.insert(code.end(), target_original.begin(), target_original.end());
    code.push_back(0x3B); code.push_back(0x35); // cmp esi,[vehicle]
    append_dword(code, static_cast<std::uint32_t>(remote_vehicle));
    code.push_back(0x75); code.push_back(0x06); // jne target_resume
    code.push_back(0xD8); code.push_back(0x0D); // fmul [multiplier]
    append_dword(code, static_cast<std::uint32_t>(remote_multiplier));
    code.push_back(0xE9);
    append_dword(code, relative(
        target_hook + target_original.size(),
        remote + code.size() + sizeof(std::uint32_t)));

    constexpr std::size_t kAccelerationCodeOffset = 0x40;
    code.resize(kAccelerationCodeOffset, 0x90);
    code.insert(
        code.end(),
        acceleration_original.begin(), acceleration_original.end());
    code.push_back(0x3B); code.push_back(0x35); // cmp esi,[vehicle]
    append_dword(code, static_cast<std::uint32_t>(remote_vehicle));
    code.push_back(0x75); code.push_back(0x06); // jne acceleration_resume
    code.push_back(0xD8); code.push_back(0x0D); // fmul [multiplier]
    append_dword(code, static_cast<std::uint32_t>(remote_multiplier));
    code.push_back(0xE9);
    append_dword(code, relative(
        acceleration_hook + acceleration_original.size(),
        remote + code.size() + sizeof(std::uint32_t)));

    const std::uint32_t vehicle32 = static_cast<std::uint32_t>(
        controlled_vehicle);
    if (!process.WriteMemory(remote, code.data(), code.size()) ||
        !process.WriteMemory(remote_vehicle, vehicle32) ||
        !process.WriteMemory(remote_multiplier, multiplier))
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }

    std::array<std::uint8_t, 7> target_patch{
        0xE9, 0, 0, 0, 0, 0x90, 0x90};
    std::array<std::uint8_t, 6> acceleration_patch{
        0xE9, 0, 0, 0, 0, 0x90};
    const std::uint32_t target_relative = relative(
        remote, target_hook + 5U);
    const std::uint32_t acceleration_relative = relative(
        remote + kAccelerationCodeOffset, acceleration_hook + 5U);
    std::memcpy(
        target_patch.data() + 1, &target_relative, sizeof(target_relative));
    std::memcpy(
        acceleration_patch.data() + 1,
        &acceleration_relative, sizeof(acceleration_relative));

    if (!process.WriteProtectedMemory(
            acceleration_hook,
            acceleration_patch.data(), acceleration_patch.size()) ||
        !process.WriteProtectedMemory(
            target_hook, target_patch.data(), target_patch.size()))
    {
        (void)process.WriteProtectedMemory(
            target_hook, target_original.data(), target_original.size());
        (void)process.WriteProtectedMemory(
            acceleration_hook,
            acceleration_original.data(), acceleration_original.size());
        bool executing = true;
        if (process.IsAnyThreadExecutingRange(
                remote, kVehicleSpeedRemoteSize, executing) && !executing)
        {
            (void)process.FreeRemoteMemory(remote);
        }
        return false;
    }

    g_vehicle_speed.process_id = process.ProcessId();
    g_vehicle_speed.remote = remote;
    g_vehicle_speed.target_hook = target_hook;
    g_vehicle_speed.acceleration_hook = acceleration_hook;
    g_vehicle_speed.vehicle = controlled_vehicle;
    g_vehicle_speed.target_original = target_original;
    g_vehicle_speed.target_patch = target_patch;
    g_vehicle_speed.acceleration_original = acceleration_original;
    g_vehicle_speed.acceleration_patch = acceleration_patch;
    g_vehicle_speed.applied = true;
    LogDiagnostic(
        "Vehicle: hooks installed (vehicle=%08X, multiplier %.1fx).",
        static_cast<unsigned>(controlled_vehicle), multiplier);
    return true;
}

void ClearGameSpeedPatch()
{
    g_game_speed = {};
}

void RestoreGameSpeed(TrainerProcess& process)
{
    if (!g_game_speed.applied)
        return;
    if (!process.IsConnected() ||
        g_game_speed.process_id != process.ProcessId())
    {
        ClearGameSpeedPatch();
        return;
    }

    (void)process.WriteProtectedMemory(
        g_game_speed.hook,
        g_game_speed.original.data(),
        g_game_speed.original.size());

    std::array<std::uint8_t, 7> current{};
    const bool restored = process.ReadMemory(
            g_game_speed.hook, current.data(), current.size()) &&
        current == g_game_speed.original;
    if (!restored)
        return;

    bool idle = false;
    for (int attempt = 0; attempt < 250 && !idle; ++attempt)
    {
        bool executing = true;
        idle = process.IsAnyThreadExecutingRange(
            g_game_speed.remote,
            kGameSpeedRemoteSize,
            executing) && !executing;
        if (!idle)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (!idle || !process.FreeRemoteMemory(g_game_speed.remote))
    {
        LogDiagnostic(
            "Game speed: hook restore incomplete (idle=%d).", idle ? 1 : 0);
        return;
    }
    LogDiagnostic("Game speed: hook restored and remote page freed.");
    ClearGameSpeedPatch();
}

bool UpdateGameSpeedHook(TrainerProcess& process, std::int32_t factor)
{
    // sar eax,3 ; mov dword ptr [esp+14h],eax
    static constexpr std::array<std::uint8_t, 7> kExpected{
        0xC1, 0xF8, 0x03, 0x89, 0x44, 0x24, 0x14};

    if (g_game_speed.applied &&
        g_game_speed.process_id != process.ProcessId())
    {
        ClearGameSpeedPatch();
    }
    if (g_game_speed.applied)
    {
        if (factor == g_game_speed.factor)
            return true;
        if (!process.WriteMemory(
                g_game_speed.remote + kGameSpeedFactorRemoteOffset, factor))
        {
            return false;
        }
        g_game_speed.factor = factor;
        return true;
    }

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        kMainLoopTimeScaleHookRva + kExpected.size() > module.image_size)
    {
        {
            // V126 - une seule ligne toutes les dix secondes.
            //
            // Le journal du joueur comptait SIX MILLE CINQ CENT SOIXANTE-NEUF
            // fois ce message, pour six mille deux cent vingt-deux lignes en
            // tout : il represente 97 % du fichier et 740 Ko. Il est emis a
            // chaque image des que le jeu n'est plus la, et il noyait tout ce
            // qui compte. Un journal illisible ne sert personne.
            static ULONGLONG next_report = 0;
            const ULONGLONG now = GetTickCount64();
            if (now >= next_report)
            {
                next_report = now + 10000ULL;
                LogDiagnostic(
                    "Game speed: module info unavailable for hook install "
                    "(message limite a un par dix secondes).");
            }
        }
        return false;
    }
    const std::uintptr_t hook =
        module.base_address + kMainLoopTimeScaleHookRva;
    std::array<std::uint8_t, 7> original{};
    const bool read = process.ReadMemory(
        hook, original.data(), original.size());
    if (!read || original != kExpected)
    {
        LogDiagnostic(
            "Game speed: hook signature mismatch (read=%d match=%d).",
            read ? 1 : 0, original == kExpected ? 1 : 0);
        return false;
    }

    const std::uintptr_t remote =
        process.AllocateRemoteMemory(kGameSpeedRemoteSize);
    if (!IsSanePointer(remote))
    {
        LogDiagnostic("Game speed: remote allocation failed.");
        return false;
    }
    const std::uintptr_t remote_factor = remote + kGameSpeedFactorRemoteOffset;
    const auto relative = [](std::uintptr_t target, std::uintptr_t next)
    {
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<std::intptr_t>(target) -
            static_cast<std::intptr_t>(next)));
    };
    const auto append_dword = [](std::vector<std::uint8_t>& code,
                                 std::uint32_t value)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(value));
        std::memcpy(code.data() + offset, &value, sizeof(value));
    };

    // Le trampoline est atteint par un jmp, donc esp est inchange et
    // [esp+14h] designe toujours tc.time comme dans le code d'origine.
    std::vector<std::uint8_t> code;
    code.push_back(0xC1); code.push_back(0xF8); code.push_back(0x03);
    code.push_back(0x0F); code.push_back(0xAF); code.push_back(0x05);
    append_dword(code, static_cast<std::uint32_t>(remote_factor));
    code.push_back(0xC1); code.push_back(0xF8); code.push_back(0x08);
    code.push_back(0x83); code.push_back(0xF8); code.push_back(0x01);
    code.push_back(0x7D); code.push_back(0x05);
    code.push_back(0xB8); append_dword(code, 1U);
    code.push_back(0x89); code.push_back(0x44);
    code.push_back(0x24); code.push_back(0x14);
    code.push_back(0xE9);
    append_dword(code, relative(
        hook + original.size(),
        remote + code.size() + sizeof(std::uint32_t)));

    if (!process.WriteMemory(remote, code.data(), code.size()) ||
        !process.WriteMemory(remote_factor, factor))
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }

    std::array<std::uint8_t, 7> patch{0xE9, 0, 0, 0, 0, 0x90, 0x90};
    const std::uint32_t jump = relative(remote, hook + 5U);
    std::memcpy(patch.data() + 1, &jump, sizeof(jump));
    if (!InstallHookSafely(
            process, hook, patch.data(), patch.size(),
            "le saut vers le code pose"))
    {
        (void)process.FreeRemoteMemory(remote);
        LogDiagnostic("Game speed: hook write refused.");
        return false;
    }

    g_game_speed.process_id = process.ProcessId();
    g_game_speed.remote = remote;
    g_game_speed.hook = hook;
    g_game_speed.original = original;
    g_game_speed.patch = patch;
    g_game_speed.factor = factor;
    g_game_speed.applied = true;
    LogDiagnostic(
        "Game speed: main-loop time scale hooked (factor %d/256).",
        static_cast<int>(factor));
    return true;
}

void UpdateGameSpeed(
    TrainerProcess& process,
    const GameplaySettings& settings,
    GameplayStatus& status)
{
    if (!settings.game_speed_enabled)
    {
        RestoreGameSpeed(process);
        status.game_speed = GameSpeedStatus::Disabled;
        return;
    }
    const float multiplier = (std::clamp)(
        settings.game_speed_multiplier,
        kMinimumGameSpeedMultiplier,
        kMaximumGameSpeedMultiplier);
    const std::int32_t factor = static_cast<std::int32_t>(
        std::lround(multiplier * 256.0f));
    if (!UpdateGameSpeedHook(process, factor))
    {
        status.game_speed = g_game_speed.applied
            ? GameSpeedStatus::WriteFailed
            : GameSpeedStatus::UnsupportedLayout;
        return;
    }
    status.game_speed = GameSpeedStatus::Active;
}

void UpdateVehicleSpeed(
    TrainerProcess& process,
    std::uintptr_t controlled_vehicle,
    const GameplaySettings& settings,
    GameplayStatus& status)
{
    if (!settings.vehicle_speed_enabled)
    {
        RestoreVehicleSpeed(process);
        status.vehicle_speed = VehicleSpeedStatus::Disabled;
        return;
    }
    if (!IsSanePointer(controlled_vehicle))
    {
        static bool waiting_logged = false;
        if (!waiting_logged)
        {
            LogDiagnostic(
                "Vehicle: waiting for a controlled vehicle "
                "(checkbox is on).");
            waiting_logged = true;
        }
        RestoreVehicleSpeed(process);
        status.vehicle_speed = VehicleSpeedStatus::WaitingForVehicle;
        return;
    }
    float current = 0.0f;
    std::int32_t transmission = 0;
    // Only speed (0x244) and transmission (0x258) are validated: both were
    // confirmed in the installed exe. The source-level proposed_forward
    // offset does not exist in this build and previously blocked the hook
    // from ever being installed.
    if (!process.ReadMemory(
            controlled_vehicle + kAutomobileSpeedOffset, current) ||
        !process.ReadMemory(
            controlled_vehicle + kAutomobileTransmissionOffset,
            transmission) ||
        !std::isfinite(current) || std::fabs(current) > 5000.0f ||
        transmission < kAutomobileReverseTransmission ||
        transmission > kAutomobileMaximumForwardTransmission)
    {
        LogDiagnostic(
            "Vehicle: layout validation failed (vehicle=%08X speed=%.2f "
            "transmission=%d).",
            static_cast<unsigned>(controlled_vehicle),
            current, static_cast<int>(transmission));
        RestoreVehicleSpeed(process);
        status.vehicle_speed = VehicleSpeedStatus::UnsupportedLayout;
        return;
    }

    // Tick1 multiplies both its gear target and acceleration only when ESI is
    // the controlled automobile. The multiplier is persistent: N raises it,
    // B lowers it, and 1.0x keeps the exact native calculation.
    const float active_multiplier = settings.vehicle_speed_multiplier;
    if (!UpdateVehicleSpeedHook(
            process, controlled_vehicle, active_multiplier))
    {
        RestoreVehicleSpeed(process);
        status.vehicle_speed = VehicleSpeedStatus::WriteFailed;
        return;
    }

    // B lowers the ceiling, but the engine's brake is a fixed constant
    // (TAB_F_AUTO_BRAKE), so a car launched at 40x coasts down 40 times more
    // slowly than it should. Bring the current speed down by the same ratio as
    // the multiplier: one press of B is then felt immediately, and 1.0x lands
    // back on the native speed instead of decaying towards it.
    if (g_vehicle_throttle.process_id == process.ProcessId() &&
        g_vehicle_throttle.vehicle == controlled_vehicle &&
        g_vehicle_throttle.multiplier > 0.0f &&
        active_multiplier < g_vehicle_throttle.multiplier)
    {
        float speed = 0.0f;
        if (process.ReadMemory(
                controlled_vehicle + kAutomobileSpeedOffset, speed) &&
            std::isfinite(speed) && std::fabs(speed) > 0.01f)
        {
            const float scaled =
                speed * (active_multiplier / g_vehicle_throttle.multiplier);
            if (std::isfinite(scaled) &&
                process.WriteMemory(
                    controlled_vehicle + kAutomobileSpeedOffset, scaled))
            {
                LogDiagnostic(
                    "Vehicle: multiplier lowered to %.1fx, speed %.2f -> "
                    "%.2f.",
                    active_multiplier, speed, scaled);
            }
        }
    }
    g_vehicle_throttle.process_id = process.ProcessId();
    g_vehicle_throttle.vehicle = controlled_vehicle;
    g_vehicle_throttle.multiplier = active_multiplier;

    // Optional: losing the steering boost must not cost the speed itself.
    if (!UpdateWheelTurnHook(
            process, controlled_vehicle, settings.vehicle_steering_boost))
    {
        LogDiagnostic("Vehicle: steering boost unavailable; native rate kept.");
    }

    status.vehicle_speed = VehicleSpeedStatus::Active;
}

#if 0
// V4 legacy path retained only as forensic documentation. It passed an
// externally fabricated 115-entry vector to CB_SETINVENTORY and pushed one
// argument too many before cbProc. Never compile or execute this code again.
bool GrantCompleteDeluxeInventoryV4Unsafe(
    TrainerProcess& process,
    const RadarSnapshot* snapshot,
    std::uint32_t& granted_count)
{
    // A new hde.exe starts the cycle over at the first series.
    if (g_fullhands_series_process != process.ProcessId())
    {
        g_fullhands_series_process = process.ProcessId();
        g_fullhands_series = 0;
    }
    constexpr std::uintptr_t kProcessCheatHookRva = 0x0009FC10;
    constexpr std::size_t kRemoteSize = 0x1000;
    constexpr std::size_t kVectorOffset = 0x100;
    constexpr std::size_t kItemListOffset = 0x120;
    constexpr std::size_t kCompletionOffset = 0x700;
    constexpr std::uint32_t kSetInventoryCallback = 17;
    constexpr std::uint32_t kCatalogItemCount = 115;
    constexpr std::array<std::uint8_t, 5> kExpected{
        0xB8, 0x5C, 0x10, 0x00, 0x00};

    granted_count = 0;
    if (!process.IsConnected() || !snapshot ||
        !IsSanePointer(snapshot->player_object_address))
    {
        return false;
    }
    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        kProcessCheatHookRva >= module.image_size)
    {
        return false;
    }
    if (!InstallPersistentInventoryModelCache(process, module))
    {
        LogDiagnostic(
            "Fullhands: 112-object grant cancelled because the persistent "
            "model cache could not be installed safely.");
        return false;
    }
    const std::uintptr_t hook = module.base_address + kProcessCheatHookRva;
    std::array<std::uint8_t, 5> original{};
    if (!process.ReadMemory(hook, original.data(), original.size()) ||
        original != kExpected)
    {
        return false;
    }

    struct InventorySeedItem
    {
        std::uint32_t item = 0;
        std::uint32_t pieces = 0;
        std::uint32_t ammunition = 0;
    };
    static_assert(sizeof(InventorySeedItem) == 12);
    std::array<InventorySeedItem, kCatalogItemCount> items{};
    std::size_t output_index = 0;
    const auto append_item = [&](std::uint32_t item)
    {
        InventorySeedItem& seed = items[output_index++];
        seed.item = item;
        seed.pieces = item == 30U ? 1U : 50U;
        seed.ammunition = item == 30U ? 0U : 500U;
    };
    // Every C_human is constructed with Free hands (catalogue row 30) in
    // inventory slot zero. Several native paths treat index zero as that
    // sentinel, so preserve the invariant while still adding all 115 rows.
    append_item(30U);
    for (std::uint32_t item = 1; item <= kCatalogItemCount; ++item)
    {
        if (item != 30U)
            append_item(item);
    }
    if (output_index != items.size())
        return false;

    const std::uintptr_t remote =
        process.AllocateRemoteMemory(kRemoteSize);
    if (!IsSanePointer(remote))
        return false;
    const std::uintptr_t remote_vector = remote + kVectorOffset;
    const std::uintptr_t remote_items = remote + kItemListOffset;
    const std::uintptr_t completion = remote + kCompletionOffset;
    std::vector<std::uint8_t> code;
    code.reserve(96);
    const auto byte = [&](std::uint8_t value) { code.push_back(value); };
    const auto dword = [&](std::uint32_t value)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(value));
        std::memcpy(code.data() + offset, &value, sizeof(value));
    };
    const auto relative = [&](std::uintptr_t target,
                              std::uintptr_t next_instruction)
    {
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<std::intptr_t>(target) -
            static_cast<std::intptr_t>(next_instruction)));
    };

    byte(0x9C);                            // pushfd
    byte(0x60);                            // pushad
    byte(0x6A); byte(0x00);               // push reserved = 0
    byte(0x6A); byte(0x00);               // push prm2 = 0
    byte(0x68); dword(static_cast<std::uint32_t>(remote_vector));
                                           // push prm1 = vector<S_item>*
    byte(0x6A); byte(static_cast<std::uint8_t>(kSetInventoryCallback));
                                           // push CB_SETINVENTORY
    byte(0xB9); dword(static_cast<std::uint32_t>(
        snapshot->player_object_address)); // mov ecx, player
    byte(0x8B); byte(0x11);                // mov edx,[ecx]
    // Installed GameBegin uses the first virtual callback after the
    // destructor slot, with four stack arguments: 0, 0, vector, message 17.
    byte(0xFF); byte(0x52); byte(0x04);    // call [edx+04h]
    byte(0xC7); byte(0x05);                // mov [completion],115
    dword(static_cast<std::uint32_t>(completion));
    dword(kCatalogItemCount);
    byte(0xB9); dword(20'000'000U);        // mov ecx,bounded wait count
    const std::uintptr_t wait_for_release = remote + code.size();
    byte(0x81); byte(0x3D);                // cmp dword ptr [completion],116
    dword(static_cast<std::uint32_t>(completion));
    dword(kCatalogItemCount + 1U);
    byte(0x74); byte(0x07);                // je released (skip dec + jnz rel32)
    byte(0x49);                            // dec ecx
    byte(0x0F); byte(0x85);                // jnz wait_for_release
    const std::uintptr_t wait_rel_next = remote + code.size() + 4U;
    dword(relative(wait_for_release, wait_rel_next));
    byte(0x61);                            // popad
    byte(0x9D);                            // popfd
    byte(0xC2); byte(0x04); byte(0x00);   // ret 4 (ProcessCheat(char))

    const RemoteVector32 remote_item_vector{
        static_cast<std::uint32_t>(remote_items),
        static_cast<std::uint32_t>(
            remote_items + sizeof(InventorySeedItem) * items.size()),
        static_cast<std::uint32_t>(
            remote_items + sizeof(InventorySeedItem) * items.size())};
    const std::uint32_t not_completed = 0;
    if (!process.WriteMemory(
            remote_items, items.data(), sizeof(items)) ||
        !process.WriteMemory(remote_vector, remote_item_vector) ||
        !process.WriteMemory(completion, not_completed) ||
        !process.WriteMemory(remote, code.data(), code.size()))
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }
    std::array<std::uint8_t, 5> jump{0xE9, 0, 0, 0, 0};
    const std::uint32_t hook_relative = relative(remote, hook + jump.size());
    std::memcpy(jump.data() + 1, &hook_relative, sizeof(hook_relative));
    const bool hook_write_reported = process.WriteProtectedMemory(
        hook, jump.data(), jump.size());
    if (!hook_write_reported)
    {
        // A failed return may still follow a successful WriteProcessMemory.
        // Restore only our exact jump, verify the original bytes, and free the
        // page only after proving that no game thread is still inside it.
        std::array<std::uint8_t, 5> current{};
        bool original_verified = process.ReadMemory(
            hook, current.data(), current.size()) && current == original;
        if (!original_verified && current == jump)
        {
            (void)RestoreHookSafely(
        process, hook, original.data(), original.size());
            original_verified = process.ReadMemory(
                hook, current.data(), current.size()) && current == original;
        }
        bool page_is_idle = false;
        if (original_verified)
        {
            bool executing = false;
            page_is_idle = process.IsAnyThreadExecutingRange(
                remote, kRemoteSize, executing) && !executing;
        }
        if (original_verified && page_is_idle)
            (void)process.FreeRemoteMemory(remote);
        return false;
    }

    const bool triggered = SendInventoryMainThreadTrigger(process);
    std::uint32_t completed = 0;
    if (triggered)
    {
        for (int attempt = 0; attempt < 1000 && completed == 0; ++attempt)
        {
            (void)process.ReadMemory(completion, completed);
            if (completed == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    (void)RestoreHookSafely(
        process, hook, original.data(), original.size());
    std::array<std::uint8_t, 5> restored_bytes{};
    const bool restored = process.ReadMemory(
        hook, restored_bytes.data(), restored_bytes.size()) &&
        restored_bytes == original;
    const std::uint32_t release_remote_code = kCatalogItemCount + 1U;
    const bool released = completed == kCatalogItemCount && restored &&
        process.WriteMemory(completion, release_remote_code);
    bool page_is_idle = false;
    if (released)
    {
        for (int attempt = 0; attempt < 250 && !page_is_idle; ++attempt)
        {
            bool executing = false;
            const bool inspected = process.IsAnyThreadExecutingRange(
                remote, kRemoteSize, executing);
            page_is_idle = inspected && !executing;
            if (!page_is_idle)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    if (released && page_is_idle)
    {
        granted_count = kCatalogItemCount;
        return process.FreeRemoteMemory(remote);
    }

    // If completion or thread inspection fails, keep the detached page alive:
    // freeing code that might still be executing is never safe. The remote
    // wait is bounded, so a failed trainer handshake cannot freeze the game.
    return false;
}
#endif

bool InstallPersistentInventoryModelCache(
    TrainerProcess& process,
    const RemoteModuleInfo& module)
{
    static DWORD installed_process_id = 0;
    static std::uintptr_t installed_stub = 0;
    // Installed Ultimate Mod hde.exe routines (verified against the native
    // SetInventory call sites and their complete instruction signatures).
    constexpr std::uintptr_t kLoadLoopHookRva = 0x0006'2F75;
    constexpr std::uintptr_t kLoadAfterModelRva = 0x0006'3281;
    constexpr std::uintptr_t kReleaseModelBranchRva = 0x0006'33B5;
    constexpr std::uintptr_t kInventorySceneGlobalRva = 0x0010'AC08;
    constexpr std::size_t kCacheStubSize = 0x80;
    constexpr std::array<std::uint8_t, 5> kExpectedLoad{
        0xA1, 0x08, 0xAC, 0x50, 0x00};
    constexpr std::array<std::uint8_t, 2> kExpectedRelease{0x74, 0x0C};
    constexpr std::array<std::uint8_t, 2> kCachedRelease{0xEB, 0x0C};

    if (module.base_address < 0x10000U ||
        kReleaseModelBranchRva + kExpectedRelease.size() > module.image_size)
    {
        LogDiagnostic("Fullhands cache: module bounds unavailable.");
        return false;
    }

    const std::uintptr_t load_hook =
        module.base_address + kLoadLoopHookRva;
    const std::uintptr_t release_branch =
        module.base_address + kReleaseModelBranchRva;
    std::array<std::uint8_t, 5> current_load{};
    std::array<std::uint8_t, 2> current_release{};
    if (!process.ReadMemory(load_hook, current_load.data(), current_load.size()) ||
        !process.ReadMemory(
            release_branch, current_release.data(), current_release.size()))
    {
        LogDiagnostic("Fullhands cache: signatures unreadable.");
        return false;
    }

    if (installed_process_id == process.ProcessId() &&
        IsSanePointer(installed_stub) &&
        current_load[0] == 0xE9 && current_release == kCachedRelease)
    {
        LogDiagnostic(
            "Fullhands cache: reusing installed stub=%08X for pid=%lu.",
            static_cast<unsigned>(installed_stub),
            static_cast<unsigned long>(installed_process_id));
        return true;
    }

    // The one-shot Fullhands action normally reaches this only once. Accept
    // the release-side patch if it was already installed during this process.
    if (current_load != kExpectedLoad ||
        (current_release != kExpectedRelease &&
         current_release != kCachedRelease))
    {
        LogDiagnostic(
            "Fullhands cache: unsupported signature load=%02X %02X %02X "
            "%02X %02X release=%02X %02X.",
            current_load[0], current_load[1], current_load[2],
            current_load[3], current_load[4],
            current_release[0], current_release[1]);
        return false;
    }

    const std::uintptr_t remote =
        process.AllocateRemoteMemory(kCacheStubSize);
    if (!IsSanePointer(remote))
    {
        LogDiagnostic("Fullhands cache: remote allocation failed.");
        return false;
    }
    const auto relative = [](std::uintptr_t target,
                             std::uintptr_t next_instruction)
    {
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<std::intptr_t>(target) -
            static_cast<std::intptr_t>(next_instruction)));
    };
    std::vector<std::uint8_t> code;
    const auto byte = [&](std::uint8_t value) { code.push_back(value); };
    const auto dword = [&](std::uint32_t value)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(value));
        std::memcpy(code.data() + offset, &value, sizeof(value));
    };
    byte(0x8B); byte(0x45); byte(0x08);   // mov eax,[ebp+8] (items.begin)
    byte(0x8B); byte(0x04); byte(0xB8);   // mov eax,[eax+edi*4] (S_item)
    byte(0x83); byte(0x78); byte(0x0C); byte(0x00);
                                           // cmp dword ptr [eax+0C],0 (model)
    byte(0x0F); byte(0x85);               // jne cached model path
    const std::size_t cached_displacement = code.size();
    dword(0);
    byte(0xA1); dword(static_cast<std::uint32_t>(
        module.base_address + kInventorySceneGlobalRva));
                                           // relocated original instruction
    byte(0xE9);
    const std::size_t normal_displacement = code.size();
    dword(0);
    const std::size_t cached_path = code.size();
    byte(0xE9);
    const std::size_t cached_jump_displacement = code.size();
    dword(0);

    const auto patch_relative = [&](std::size_t displacement,
                                    std::uintptr_t target)
    {
        const std::uint32_t value = relative(
            target, remote + displacement + sizeof(std::uint32_t));
        std::memcpy(code.data() + displacement, &value, sizeof(value));
    };
    patch_relative(cached_displacement, remote + cached_path);
    patch_relative(normal_displacement, load_hook + kExpectedLoad.size());
    patch_relative(
        cached_jump_displacement,
        module.base_address + kLoadAfterModelRva);

    std::array<std::uint8_t, 5> detour{0xE9, 0, 0, 0, 0};
    const std::uint32_t detour_relative = relative(remote, load_hook + 5U);
    std::memcpy(detour.data() + 1, &detour_relative, sizeof(detour_relative));
    if (!process.WriteMemory(remote, code.data(), code.size()) ||
        !process.WriteProtectedMemory(
            release_branch, kCachedRelease.data(), kCachedRelease.size()) ||
        !process.WriteProtectedMemory(load_hook, detour.data(), detour.size()))
    {
        (void)process.WriteProtectedMemory(
            release_branch, kExpectedRelease.data(), kExpectedRelease.size());
        (void)process.FreeRemoteMemory(remote);
        LogDiagnostic("Fullhands cache: installation failed.");
        return false;
    }

    // This page intentionally lives until hde.exe exits: every later native
    // inventory opening jumps through it and reuses the already loaded model.
    LogDiagnostic(
        "Fullhands cache: installed at load=%08X release=%08X stub=%08X; "
        "3D models will persist for this game process.",
        static_cast<unsigned>(load_hook),
        static_cast<unsigned>(release_branch),
        static_cast<unsigned>(remote));
    installed_process_id = process.ProcessId();
    installed_stub = remote;
    return true;
}

// Which catalogue lot the next M press grants. It advances on every
// accepted grant and wraps, so repeated presses walk the whole catalogue.
std::uint32_t g_fullhands_series = 0;
// Reset on every accepted grant so the stall report can tell the first
// inventory opening after M from the following ones.
std::uint32_t g_opens_since_grant = 0;
DWORD g_fullhands_series_process = 0;
std::uintptr_t g_fullhands_series_mission = 0;

bool GrantCompleteDeluxeInventory(
    TrainerProcess& process,
    const RadarSnapshot* snapshot,
    std::uint32_t& granted_count)
{
    // Fourteen fixed lots cover the complete installed catalogue. The
    // observed Ultimate catalogue has 128 eligible rows, so the actual split
    // is 9 or 10 objects per lot; a stock catalogue with about 91 rows
    // naturally gives 6 or 7 per lot. M replaces the previous lot instead of
    // accumulating.
    //
    // This count is what governs the opening stall, and it is the only safe
    // lever on it. C_inventory::LoadModels rebuilds every carried model on the
    // first opening after a content change, at roughly 8 ms each: 19 objects
    // measured 156 ms, so 9 lands near 75 ms. Warming those models ahead of
    // time from this trainer is not an option -- see the SetInventory note
    // below for why V25.1 had to be withdrawn.
    constexpr std::uint32_t series_count = 14U;
    // Rotation runs the game's own C_inventory::DeleteAllItems on its game
    // thread, then lets the native Fullhands callback construct the next lot.
    // No item pointer is released or reference-counted by trainer code.
    constexpr bool replace_previous = true;
    constexpr std::uintptr_t kProcessCheatHookRva = 0x0009FC10;
    // Exact installed Ultimate binary function. Official Deluxe source:
    // C_inventory::DeleteAllItems() calls ReleaseModels when visible, then
    // erases [items.begin()+1, items.end()) with native smart-pointer release.
    constexpr std::uintptr_t kDeleteAllItemsRva = 0x00062E80;
    constexpr std::uintptr_t kSetSelectedInventoryItemRva = 0x000076E0;
    // Do not warm the inventory models from this stub. V25.1 called
    // C_game_menu::SetInventory (0x0045FDC0, reached as [actor+18h]+1FCh) with
    // the inventory and then with NULL: it did build every model of the new
    // lot and release it again, moving the 156 ms opening stall into the M
    // press exactly as intended, and the journal confirmed it (preload=2, an
    // exact replacement). It also corrupted the heap -- the game died with
    // c0000005 / StackHash_dd55 / PCH_6F_FROM_ntdll. ProcessCheat runs from
    // keyboard handling, not from the engine's frame, so creating a scene and
    // loading models there is not a valid point to do driver work. Delegating
    // to the engine is necessary but not sufficient: the call also has to
    // happen where the engine itself makes it.
    constexpr std::uintptr_t kInventorySubobjectOffset = 0x54;
    constexpr std::uintptr_t kSelectedInventoryItemOffset = 0x258;
    constexpr std::uintptr_t kActiveInventoryItemOffset = 0x6C;
    constexpr std::size_t kRemoteSize = 0x2000;
    constexpr std::size_t kCatalogRowsOffset = 0x400;
    constexpr std::size_t kSelectionStateOffset = 0x1400;
    constexpr std::size_t kTypeCountersOffset = 0x1500;
    // Diagnostic window: type and first four name characters of the first rows,
    // recorded by the stub itself so the journal can show what the catalogue
    // query really returns.
    constexpr std::size_t kProbeRowsOffset = 0x1600;
    constexpr std::uint32_t kProbeRowCount = 24;
    // One M press grants one seventh of the eligible catalogue and wraps after
    // the seventh lot.
    constexpr std::size_t kCompletionOffset = 0x1800;
    constexpr std::uint32_t kCheatCallback = 14;
    constexpr std::uint32_t kFullhandsCheat = 9;
    constexpr std::uintptr_t kGameConfigurationRva = 0x0010'86E0;
    constexpr std::uintptr_t kInventoryTablesRva = 0x0010'AAD0;
    constexpr std::uint32_t kFirstCatalogRow = 1;
    constexpr std::uint32_t kCatalogCapacity = 0x400;
    // Permit every named weapon row observed in the Deluxe/Ultimate catalog.
    // The native selector exposes at most 111 usable rows in this revision;
    // 128 leaves room for all of them plus existing clothes and keys.
    constexpr std::uint32_t kMaximumFinalInventoryItems = 128;
    constexpr std::uint32_t kValidatedWeaponQuota = 112;
    constexpr std::uint32_t kOtherItemQuota = 6;
    constexpr std::uint32_t kEssentialItemQuota = 10;
    constexpr std::uint32_t kPerTypeQuota = 4;
    constexpr std::uint32_t kMaximumTrackedItemType = 64;
    constexpr std::uint32_t kUniformItemType = 10;
    constexpr std::uint32_t kKeyItemType = 14;
    constexpr std::uint32_t kCameraItemType = 9;
    // Column descriptor table published by itabler2.dll.
    constexpr std::uintptr_t kColumnDescriptorsOffset = 0x20;
    constexpr std::uint32_t kColumnDescriptorSize = 8;
    constexpr std::uint8_t kStringColumnType = 5;
    constexpr std::uint8_t kNameCatalogProperty = 0x00;
    constexpr std::uint8_t kTypeCatalogProperty = 0x03;
    constexpr std::uint32_t kColtItemType = 13;
    constexpr std::uintptr_t kInventoryBeginOffset = 0x5C;
    // C_inventory::inv_scene, null exactly when the screen is closed.
    constexpr std::uintptr_t kInventorySceneOffset = 0x68;
    constexpr std::uintptr_t kInventoryEndOffset = 0x60;
    constexpr std::uintptr_t kItemIdOffset = 0x08;
    constexpr std::uintptr_t kItemAmountOffset = 0x18;
    constexpr std::uint32_t kMaximumInventoryItems = 256;
    constexpr std::uint32_t kGrantedAmount = 100;
    constexpr std::array<std::uint8_t, 5> kExpected{
        0xB8, 0x5C, 0x10, 0x00, 0x00};
    constexpr std::array<std::uint8_t, 10> kDeleteAllItemsExpected{
        0x55, 0x8B, 0xE9, 0x8B, 0x45, 0x14, 0x85, 0xC0, 0x74, 0x05};
    constexpr std::array<std::uint8_t, 13> kSetSelectedItemExpected{
        0x8B, 0x44, 0x24, 0x04, 0x56, 0x8B, 0xF1,
        0x39, 0x86, 0x58, 0x02, 0x00, 0x00};
    struct InventoryItemView
    {
        std::uintptr_t object;
        std::uint32_t id;
        std::uint32_t amount;
    };

    granted_count = 0;
    // V117 - refus net plutot qu'un arret du jeu. Voir la note en tete de
    // fichier : ce cheat rejoue un chemin du moteur ecrit pour un soldat de
    // mission, et le joueur peut piloter un soldat cree.
    if (snapshot && IsCreatedSoldier(snapshot->player_object_address))
    {
        LogDiagnostic(
            "Fullhands: REFUSE, vous pilotez un soldat cree (%08X). Ce cheat "
            "rejoue un chemin du moteur ecrit pour un soldat de mission; sur "
            "un soldat cree il arreterait le jeu. Revenez a un soldat "
            "d'origine (fenetre G, ligne REVENIR) et reessayez.",
            static_cast<unsigned>(snapshot->player_object_address));
        return false;
    }
    if (!process.IsConnected() || !snapshot || snapshot->from_cache ||
        !IsSanePointer(snapshot->player_object_address) ||
        !IsSanePointer(snapshot->entity_list_object_address))
    {
        LogDiagnostic(
            "Fullhands: unavailable (connected=%d snapshot=%d cached=%d "
            "player=%08X).",
            process.IsConnected() ? 1 : 0,
            snapshot ? 1 : 0,
            snapshot && snapshot->from_cache ? 1 : 0,
            snapshot && snapshot->player_object_address
                ? static_cast<unsigned>(snapshot->player_object_address)
                : 0U);
        return false;
    }

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        kProcessCheatHookRva >= module.image_size ||
        kDeleteAllItemsRva + kDeleteAllItemsExpected.size() >=
            module.image_size ||
        kSetSelectedInventoryItemRva + kSetSelectedItemExpected.size() >=
            module.image_size)
    {
        LogDiagnostic("Fullhands: main module unavailable.");
        return false;
    }
    const std::uintptr_t hook = module.base_address + kProcessCheatHookRva;
    const std::uintptr_t delete_all_items =
        module.base_address + kDeleteAllItemsRva;
    const std::uintptr_t set_selected_inventory_item =
        module.base_address + kSetSelectedInventoryItemRva;
    std::array<std::uint8_t, kDeleteAllItemsExpected.size()>
        delete_all_items_signature{};
    std::array<std::uint8_t, kSetSelectedItemExpected.size()>
        set_selected_item_signature{};
    if (!process.ReadMemory(
            delete_all_items,
            delete_all_items_signature.data(),
            delete_all_items_signature.size()) ||
        delete_all_items_signature != kDeleteAllItemsExpected ||
        !process.ReadMemory(
            set_selected_inventory_item,
            set_selected_item_signature.data(),
            set_selected_item_signature.size()) ||
        set_selected_item_signature != kSetSelectedItemExpected)
    {
        LogDiagnostic(
            "Fullhands: native rotation signatures mismatch "
            "delete=%08X select=%08X; press refused.",
            static_cast<unsigned>(delete_all_items),
            static_cast<unsigned>(set_selected_inventory_item));
        return false;
    }
    LogDiagnostic(
        "Fullhands: native rotation verified delete=%08X select=%08X.",
        static_cast<unsigned>(delete_all_items),
        static_cast<unsigned>(set_selected_inventory_item));
    std::uint32_t game_configuration = 0;
    std::uint32_t inventory_table32 = 0;
    std::uintptr_t inventory_table_vtable = 0;
    std::uintptr_t inventory_table_query = 0;
    std::uintptr_t player_vtable = 0;
    std::uintptr_t player_callback = 0;
    if (kGameConfigurationRva >= module.image_size ||
        kInventoryTablesRva + sizeof(std::uint32_t) * 2U >
            module.image_size ||
        !process.ReadMemory(
            module.base_address + kGameConfigurationRva,
            game_configuration))
    {
        LogDiagnostic("Fullhands: configuration/table RVAs unavailable.");
        return false;
    }
    const std::uint32_t table_index =
        game_configuration >= 6U && game_configuration != 9U ? 1U : 0U;
    if (!process.ReadMemory(
            module.base_address + kInventoryTablesRva +
                table_index * sizeof(std::uint32_t),
            inventory_table32) ||
        !IsSanePointer(inventory_table32) ||
        !process.ReadMemory(inventory_table32, inventory_table_vtable) ||
        !IsSanePointer(inventory_table_vtable) ||
        !process.ReadMemory(
            inventory_table_vtable + 0x30, inventory_table_query) ||
        !IsSanePointer(inventory_table_query) ||
        !process.IsReadableCodeTarget(inventory_table_query))
    {
        LogDiagnostic(
            "Fullhands: inventory table unusable "
            "(configuration=%u index=%u table=%08X query=%08X).",
            game_configuration, table_index,
            static_cast<unsigned>(inventory_table32),
            static_cast<unsigned>(inventory_table_query));
        return false;
    }
    // itabler2.dll stores one 8-byte descriptor per column at [table+0x20]:
    // byte +4 is the column type (5 = string) and byte +5 is the number of
    // bytes per string entry. The name column must be read with the string
    // accessor and that exact stride; the 4-byte value accessor used until
    // V23.14 landed inside the padding of an earlier row, so every row read
    // as empty and the catalogue selected nothing at all.
    std::uint32_t column_descriptors = 0;
    std::uint8_t name_column_type = 0;
    std::uint8_t name_column_stride = 0;
    if (!process.ReadMemory(
            inventory_table32 + kColumnDescriptorsOffset, column_descriptors) ||
        !IsSanePointer(column_descriptors) ||
        !process.ReadMemory(
            column_descriptors + kNameCatalogProperty * kColumnDescriptorSize +
                4U,
            name_column_type) ||
        !process.ReadMemory(
            column_descriptors + kNameCatalogProperty * kColumnDescriptorSize +
                5U,
            name_column_stride) ||
        name_column_type != kStringColumnType ||
        name_column_stride < 4U || name_column_stride > 64U)
    {
        LogDiagnostic(
            "Fullhands: name column unusable (descriptors=%08X type=%u "
            "stride=%u).",
            static_cast<unsigned>(column_descriptors),
            static_cast<unsigned>(name_column_type),
            static_cast<unsigned>(name_column_stride));
        return false;
    }
    LogDiagnostic(
        "Fullhands: name column type=%u stride=%u bytes.",
        static_cast<unsigned>(name_column_type),
        static_cast<unsigned>(name_column_stride));
    if (!process.ReadMemory(
            snapshot->player_object_address, player_vtable) ||
        !IsSanePointer(player_vtable) ||
        !process.ReadMemory(
            player_vtable + 0x04, player_callback) ||
        !IsSanePointer(player_callback) ||
        !process.IsReadableCodeTarget(player_callback))
    {
        LogDiagnostic(
            "Fullhands: controlled-player cbProc slot +04 unusable "
            "(player=%08X vtable=%08X target=%08X).",
            static_cast<unsigned>(snapshot->player_object_address),
            static_cast<unsigned>(player_vtable),
            static_cast<unsigned>(player_callback));
        return false;
    }
    LogDiagnostic(
        "Fullhands: configuration=%u table=%08X controlled_player=%08X "
        "cbProc=%08X.",
        game_configuration, static_cast<unsigned>(inventory_table32),
        static_cast<unsigned>(snapshot->player_object_address),
        static_cast<unsigned>(player_callback));
    std::array<std::uint8_t, 5> original{};
    if (!process.ReadMemory(hook, original.data(), original.size()) ||
        original != kExpected)
    {
        LogDiagnostic(
            "Fullhands: ProcessCheat signature mismatch at %08X "
            "(expected B8 5C 10 00 00, read %02X %02X %02X %02X %02X).",
            static_cast<unsigned>(hook),
            original[0], original[1], original[2], original[3], original[4]);
        return false;
    }
    LogDiagnostic(
        "Fullhands: ProcessCheat signature verified at %08X.",
        static_cast<unsigned>(hook));
    // Do not retain inventory model pointers across ReleaseModels. The native
    // scene owns their lifetime; keeping those smart pointers alive produced
    // delayed 0xC0000096 crashes when the menu later touched stale models.
    // With only 37 items in the observed catalog result, native loading is
    // bounded and considerably safer than altering this ownership contract.
    LogDiagnostic(
        "Fullhands: persistent 3D-model cache disabled; native model lifetime preserved.");

    const auto read_inventory = [&](std::vector<InventoryItemView>& items)
    {
        items.clear();
        std::uintptr_t begin = 0;
        std::uintptr_t end = 0;
        if (!process.ReadMemory(
                snapshot->player_object_address + kInventoryBeginOffset,
                begin) ||
            !process.ReadMemory(
                snapshot->player_object_address + kInventoryEndOffset,
                end) ||
            !IsSanePointer(begin) || end < begin ||
            (end - begin) % sizeof(std::uint32_t) != 0)
        {
            return false;
        }
        const std::uintptr_t count =
            (end - begin) / sizeof(std::uint32_t);
        if (count == 0 || count > kMaximumInventoryItems)
            return false;

        items.reserve(static_cast<std::size_t>(count));
        for (std::uintptr_t index = 0; index < count; ++index)
        {
            InventoryItemView item{};
            if (!process.ReadMemory(
                    begin + index * sizeof(std::uint32_t), item.object) ||
                !IsSanePointer(item.object) ||
                !process.ReadMemory(
                    item.object + kItemIdOffset, item.id) ||
                !process.ReadMemory(
                    item.object + kItemAmountOffset, item.amount))
            {
                return false;
            }
            items.push_back(item);
        }
        return true;
    };

    // M can arrive in the few frames during which the inventory scene is still
    // being released. Wait for two consecutive closed samples before even
    // preparing the grant. If it never closes, refuse the whole press: adding
    // a new lot without first replacing the old one is never allowed.
    std::uint32_t inventory_scene = 0;
    int closed_samples = 0;
    int close_poll_count = 0;
    for (; close_poll_count < 80 && closed_samples < 2; ++close_poll_count)
    {
        if (!process.ReadMemory(
                snapshot->player_object_address + kInventorySceneOffset,
                inventory_scene))
        {
            LogDiagnostic("Fullhands: inv_scene unreadable; press refused.");
            return false;
        }
        closed_samples = inventory_scene == 0 ? closed_samples + 1 : 0;
        if (closed_samples < 2)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (closed_samples < 2)
    {
        LogDiagnostic(
            "Fullhands: inventory stayed open (scene=%08X polls=%d); "
            "press refused before rotation/grant.",
            inventory_scene, close_poll_count);
        return false;
    }
    if (g_fullhands_series_process != process.ProcessId() ||
        g_fullhands_series_mission != snapshot->entity_list_object_address)
    {
        g_fullhands_series_process = process.ProcessId();
        g_fullhands_series_mission = snapshot->entity_list_object_address;
        g_fullhands_series = 0;
        LogDiagnostic(
            "Fullhands: new mission/process, series reset to 1/%u.",
            series_count);
    }

    std::vector<InventoryItemView> before_items;
    if (!read_inventory(before_items))
    {
        LogDiagnostic("Fullhands: inventory unreadable before native call.");
        return false;
    }
    LogDiagnostic(
        "Fullhands: before native call items=%u inv_scene=%08X "
        "closed_samples=%d polls=%d.",
        static_cast<unsigned>(before_items.size()), inventory_scene,
        closed_samples, close_poll_count);
    // Rotation delegates deletion to the game's own C_inventory method. It
    // keeps the permanent first entry and releases every smart pointer through
    // the native vector erase path; the trainer never edits items.end itself.
    // Two guards are used: the host waits for a stable closed scene, then the
    // trampoline re-tests inv_scene on the game thread at the exact write. A
    // failed second guard aborts the entire press instead of skipping only the
    // rotation and accidentally accumulating the new lot.
    const bool rotate = replace_previous && before_items.size() > 1U;
    LogDiagnostic(
        "Fullhands: rotation=%d (requested=%d inv_scene=%08X items=%u).",
        rotate ? 1 : 0, replace_previous ? 1 : 0,
        inventory_scene, static_cast<unsigned>(before_items.size()));

    const std::size_t effective_before = rotate ? 1U : before_items.size();
    if (effective_before >= kMaximumFinalInventoryItems)
    {
        LogDiagnostic(
            "Fullhands: inventory already contains %u items (limit=%u).",
            static_cast<unsigned>(before_items.size()),
            kMaximumFinalInventoryItems);
        granted_count = 0;
        return true;
    }
    const std::uint32_t grant_limit =
        kMaximumFinalInventoryItems -
        static_cast<std::uint32_t>(effective_before);

    const std::uintptr_t remote = process.AllocateRemoteMemory(kRemoteSize);
    if (!IsSanePointer(remote))
    {
        LogDiagnostic("Fullhands: remote allocation failed.");
        return false;
    }
    const std::uintptr_t catalog_rows = remote + kCatalogRowsOffset;
    const std::uintptr_t completion = remote + kCompletionOffset;
    LogDiagnostic(
        "Fullhands: trampoline=%08X rows=%08X completion=%08X.",
        static_cast<unsigned>(remote),
        static_cast<unsigned>(catalog_rows),
        static_cast<unsigned>(completion));
    std::vector<std::uint8_t> code;
    code.reserve(512);
    const auto byte = [&](std::uint8_t value) { code.push_back(value); };
    const auto dword = [&](std::uint32_t value)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(value));
        std::memcpy(code.data() + offset, &value, sizeof(value));
    };
    const auto relative = [&](std::uintptr_t target,
                              std::uintptr_t next_instruction)
    {
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<std::intptr_t>(target) -
            static_cast<std::intptr_t>(next_instruction)));
    };

    const std::uintptr_t stack_checkpoint =
        remote + kSelectionStateOffset + 20U;
    const std::uintptr_t stack_observed =
        remote + kSelectionStateOffset + 24U;
    const std::uintptr_t selected_count =
        remote + kSelectionStateOffset + 28U;
    const std::uintptr_t eligible_index =
        remote + kSelectionStateOffset + 32U;
    const std::uintptr_t series_slot =
        remote + kSelectionStateOffset + 36U;
    byte(0x9C);                            // pushfd
    byte(0x60);                            // pushad
    byte(0x89); byte(0x25);                // mov [stack_checkpoint],esp
    dword(static_cast<std::uint32_t>(stack_checkpoint));

    // The game-thread check is authoritative. If the scene reopened after the
    // host-side wait, return completion=2 before either truncating or granting;
    // the previous V24.11 path merely skipped rotation and then accumulated
    // the next lot, which is exactly the reported bug.
    byte(0x83); byte(0x3D);
    dword(static_cast<std::uint32_t>(
        snapshot->player_object_address + kInventorySceneOffset));
    byte(0x00);                            // cmp [inv_scene],0
    byte(0x0F); byte(0x85);                // jne abort whole press
    const std::size_t inventory_open_abort_displacement = code.size();
    dword(0);
    std::vector<std::size_t> rotation_ready_displacements;
    if (rotate)
    {
        // ProcessCheat runs once per keystroke of the trigger sequence, so the
        // truncation is guarded by its own flag: without it the second run
        // would wipe the items the first run had just granted.
        const std::uintptr_t rotation_flag =
            remote + kSelectionStateOffset + 40U;
        byte(0x83); byte(0x3D);
        dword(static_cast<std::uint32_t>(rotation_flag));
        byte(0x00);                        // cmp [rotation_flag],0
        byte(0x0F); byte(0x85);            // jne already rotated
        rotation_ready_displacements.push_back(code.size());
        dword(0);
        byte(0xC7); byte(0x05);
        dword(static_cast<std::uint32_t>(rotation_flag));
        dword(1U);                         // mark it done
        // Holster the current weapon through the native human method before
        // its inventory item is erased. Arguments are (index=0, net=false).
        byte(0x6A); byte(0x00);            // push false
        byte(0x6A); byte(0x00);            // push selected index 0
        byte(0xB9); dword(static_cast<std::uint32_t>(
            snapshot->player_object_address));
        byte(0xB8); dword(static_cast<std::uint32_t>(
            set_selected_inventory_item));
        byte(0xFF); byte(0xD0);            // call SetSelectedInvItem
        byte(0xB9); dword(static_cast<std::uint32_t>(
            snapshot->player_object_address + kInventorySubobjectOffset));
                                                // ecx=&player.C_inventory
        byte(0xB8); dword(static_cast<std::uint32_t>(delete_all_items));
        byte(0xFF); byte(0xD0);            // call native DeleteAllItems
        byte(0xC7); byte(0x05);
        dword(static_cast<std::uint32_t>(
            snapshot->player_object_address + kActiveInventoryItemOffset));
        dword(0U);                         // inventory carousel starts at hands
    }
    byte(0xE9);                            // skip abort block
    rotation_ready_displacements.push_back(code.size());
    dword(0);
    const std::size_t inventory_open_abort = code.size();
    byte(0xC7); byte(0x05);                // mov [completion],2
    dword(static_cast<std::uint32_t>(completion));
    dword(2U);
    byte(0x61);                            // popad
    byte(0x9D);                            // popfd
    byte(0xC2); byte(0x04); byte(0x00);   // ret 4
    const std::size_t rotation_ready = code.size();
    {
        const std::uint32_t displacement = relative(
            remote + inventory_open_abort,
            remote + inventory_open_abort_displacement + 4U);
        std::memcpy(
            code.data() + inventory_open_abort_displacement,
            &displacement,
            sizeof(displacement));
    }
    for (const std::size_t displacement_offset :
         rotation_ready_displacements)
    {
        const std::uint32_t displacement = relative(
            remote + rotation_ready,
            remote + displacement_offset + 4U);
        std::memcpy(
            code.data() + displacement_offset,
            &displacement,
            sizeof(displacement));
    }
    // A row must expose a real native category and a non-empty, non-unknown
    // name. V20's inferred model-property indices rejected the whole installed
    // catalog (selected=0), so the game's Fullhands callback remains the
    // authority that constructs and validates each named item.
    byte(0xBF); dword(static_cast<std::uint32_t>(
        snapshot->player_object_address)); // mov edi,controlled player
    byte(0xBE); dword(inventory_table32);   // mov esi,inventory table
    byte(0xBB); dword(kFirstCatalogRow);    // mov ebx,first row
    // Resume the running total instead of restarting at zero. ProcessCheat is
    // called once per keystroke of the trigger sequence, so the stub runs
    // several times; with a per-run counter the second run rewrote the first
    // run's entries at index 0 and left the tail of the row list unwritten,
    // which the trainer then rejected as "invalid/duplicate catalog row 0".
    byte(0x8B); byte(0x2D);                 // mov ebp,[selected_count]
    dword(static_cast<std::uint32_t>(remote + kSelectionStateOffset + 28U));
    const std::uintptr_t current_type = remote + kSelectionStateOffset;
    const std::uintptr_t fast_count = current_type + 4U;
    const std::uintptr_t other_count = current_type + 8U;
    const std::uintptr_t essential_count = current_type + 12U;
    const std::uintptr_t type_counters = remote + kTypeCountersOffset;
    const auto emit_query = [&](std::uint8_t property)
    {
        // The installed table implementation does not consistently preserve
        // every non-volatile register for string properties. Keep all loop
        // state explicit around the native query; EAX remains the result.
        byte(0x53);                         // push ebx
        byte(0x56);                         // push esi
        byte(0x57);                         // push edi
        byte(0x55);                         // push ebp
        byte(0x53);                         // push row
        byte(0x6A); byte(property);         // push property
        byte(0x56);                         // push table
        byte(0x8B); byte(0x06);             // mov eax,[esi]
        byte(0xFF); byte(0x50); byte(0x30); // call table query
        byte(0x5D);                         // pop ebp
        byte(0x5F);                         // pop edi
        byte(0x5E);                         // pop esi
        byte(0x5B);                         // pop ebx
        byte(0x8B); byte(0x00);             // mov eax,[eax]
    };
    // String columns need vtable+0x48, which takes the per-entry stride as a
    // fourth argument and returns base + row * stride. vtable+0x30 returns
    // base + row * 4 and is only valid for the 4-byte value columns.
    const auto emit_string_query = [&](std::uint8_t property,
                                       std::uint8_t stride)
    {
        byte(0x53);                         // push ebx
        byte(0x56);                         // push esi
        byte(0x57);                         // push edi
        byte(0x55);                         // push ebp
        byte(0x6A); byte(stride);           // push bytes per entry
        byte(0x53);                         // push row
        byte(0x6A); byte(property);         // push property
        byte(0x56);                         // push table
        byte(0x8B); byte(0x06);             // mov eax,[esi]
        byte(0xFF); byte(0x50); byte(0x48); // call table string query
        byte(0x5D);                         // pop ebp
        byte(0x5F);                         // pop edi
        byte(0x5E);                         // pop esi
        byte(0x5B);                         // pop ebx
        byte(0x8B); byte(0x00);             // mov eax,[eax] (first 4 chars)
    };
    // Record the raw query results for the first rows so the journal can show
    // exactly what the catalogue answers, without changing any decision.
    const auto emit_probe = [&](std::uint32_t slot_offset)
    {
        byte(0x83); byte(0xFB); byte(static_cast<std::uint8_t>(kProbeRowCount));
        byte(0x73); byte(0x07);             // jnb skip
        byte(0x89); byte(0x04); byte(0xDD);
        dword(static_cast<std::uint32_t>(remote + kProbeRowsOffset +
                                         slot_offset));
    };
    const std::size_t catalog_loop = code.size();
    emit_query(kTypeCatalogProperty);       // inventory item type / ID
    emit_probe(0);
    byte(0xA3); dword(static_cast<std::uint32_t>(current_type));
                                             // mov [current_type],eax
    byte(0x83); byte(0xF8); byte(0x00);     // cmp eax,0 (hands/empty)
    byte(0x0F); byte(0x84);                // je next row
    const std::size_t empty_row_displacement = code.size();
    dword(0);
    byte(0x83); byte(0xF8); byte(0x04);     // cmp eax,4 (native exclusion)
    byte(0x0F); byte(0x84);                // je next row
    const std::size_t excluded_row_displacement = code.size();
    dword(0);

    emit_string_query(kNameCatalogProperty, name_column_stride);
                                            // display name (first 4 chars)
    emit_probe(4);
    byte(0x85); byte(0xC0);                 // test eax,eax
    byte(0x0F); byte(0x84);
    const std::size_t empty_name_displacement = code.size();
    dword(0);
    byte(0x8B); byte(0xD0);                 // mov edx,eax
    byte(0x81); byte(0xCA); dword(0x2020'2020U);
                                                // lowercase ASCII word
    byte(0x81); byte(0xFA); dword(0x6E6B'6E75U);
                                                // cmp edx,"unkn"
    byte(0x0F); byte(0x84);
    const std::size_t unknown_name_displacement = code.size();
    dword(0);
    byte(0xA1); dword(static_cast<std::uint32_t>(current_type));
                                                // mov eax,[current_type]
    byte(0x83); byte(0xF8); byte(static_cast<std::uint8_t>(
        kUniformItemType));                 // cmp eax,uniform
    byte(0x0F); byte(0x84);                 // je essential
    const std::size_t uniform_displacement = code.size();
    dword(0);
    byte(0x83); byte(0xF8); byte(static_cast<std::uint8_t>(kKeyItemType));
    byte(0x0F); byte(0x84);                 // je essential
    const std::size_t key_displacement = code.size();
    dword(0);

    // E_INV_ITEM weapon families from the official Deluxe H&D.h. Ultimate
    // Mod weapons keep one of these semantic IDs even when they add new rows.
    constexpr std::array<std::uint8_t, 9> kWeaponItemTypes{
        1, 2, 3, 5, 6, 7, 8, 11, 13};
    std::array<std::size_t, kWeaponItemTypes.size()>
        weapon_type_displacements{};
    for (std::size_t index = 0; index < kWeaponItemTypes.size(); ++index)
    {
        byte(0xA1); dword(static_cast<std::uint32_t>(current_type));
                                                // mov eax,[current_type]
        byte(0x83); byte(0xF8); byte(kWeaponItemTypes[index]);
        byte(0x0F); byte(0x84);                 // je validated weapon
        weapon_type_displacements[index] = code.size();
        dword(0);
    }
    byte(0xE9);                                 // other complete item
    const std::size_t validated_utility_displacement = code.size();
    dword(0);

    const std::size_t fast_weapon = code.size();
    byte(0x83); byte(0x3D); dword(static_cast<std::uint32_t>(fast_count));
    byte(static_cast<std::uint8_t>(kValidatedWeaponQuota));
    byte(0x0F); byte(0x83);                 // jae next row
    const std::size_t fast_full_displacement = code.size();
    dword(0);
    byte(0xFF); byte(0x05); dword(static_cast<std::uint32_t>(fast_count));
    byte(0xE9);                             // jmp eligible
    const std::size_t fast_eligible_displacement = code.size();
    dword(0);

    const std::size_t utility_item = code.size();
    byte(0xA1); dword(static_cast<std::uint32_t>(current_type));
    byte(0x83); byte(0xF8); byte(static_cast<std::uint8_t>(
        kCameraItemType));
    byte(0x0F); byte(0x84);
    const std::size_t utility_pistol1_displacement = code.size();
    dword(0);
    byte(0x83); byte(0xF8); byte(static_cast<std::uint8_t>(
        kColtItemType));
    byte(0x0F); byte(0x84);
    const std::size_t utility_pistol2_displacement = code.size();
    dword(0);
    byte(0x83); byte(0xF8); byte(static_cast<std::uint8_t>(
        kMaximumTrackedItemType));
    byte(0x0F); byte(0x83);
    const std::size_t untracked_type_displacement = code.size();
    dword(0);
    byte(0x83); byte(0x3D); dword(static_cast<std::uint32_t>(other_count));
    byte(static_cast<std::uint8_t>(kOtherItemQuota));
    byte(0x0F); byte(0x83);
    const std::size_t other_full_displacement = code.size();
    dword(0);
    byte(0x83); byte(0x3C); byte(0x85);
    dword(static_cast<std::uint32_t>(type_counters));
    byte(static_cast<std::uint8_t>(kPerTypeQuota));
                                             // cmp [counters+eax*4],4
    byte(0x0F); byte(0x83);
    const std::size_t type_full_displacement = code.size();
    dword(0);
    byte(0xFF); byte(0x04); byte(0x85);
    dword(static_cast<std::uint32_t>(type_counters));
                                             // inc [counters+eax*4]
    byte(0xFF); byte(0x05); dword(static_cast<std::uint32_t>(other_count));
    byte(0xE9);
    const std::size_t utility_eligible_displacement = code.size();
    dword(0);

    const std::size_t essential_item = code.size();
    byte(0x83); byte(0x3D); dword(static_cast<std::uint32_t>(essential_count));
    byte(static_cast<std::uint8_t>(kEssentialItemQuota));
    byte(0x0F); byte(0x83);
    const std::size_t essential_full_displacement = code.size();
    dword(0);
    byte(0xFF); byte(0x05); dword(static_cast<std::uint32_t>(essential_count));

    const std::size_t eligible_catalog_row = code.size();
    // Series filter: number the eligible rows and keep only those whose rank
    // modulo the series count matches the series requested for this press.
    byte(0xA1); dword(static_cast<std::uint32_t>(eligible_index));
    byte(0xFF); byte(0x05);
    dword(static_cast<std::uint32_t>(eligible_index));
    byte(0x33); byte(0xD2);                // xor edx,edx
    byte(0xB9); dword(series_count);
    byte(0xF7); byte(0xF1);                // div ecx -> edx = rank % series
    byte(0x3B); byte(0x15);
    dword(static_cast<std::uint32_t>(series_slot));
    byte(0x0F); byte(0x85);                // jne next row
    const std::size_t wrong_series_displacement = code.size();
    dword(0);
    byte(0x81); byte(0xFD); dword(grant_limit); // cmp ebp,grant_limit
    byte(0x0F); byte(0x83);
    const std::size_t global_full_displacement = code.size();
    dword(0);
    byte(0x89); byte(0x1C); byte(0xAD);    // mov [rows+ebp*4],ebx
    dword(static_cast<std::uint32_t>(catalog_rows));
    // C_actor::cbProc in the installed Ultimate Mod ABI receives four stack
    // arguments. C_game_mission::BroadcastMessage pushes this reserved zero
    // before prm2/prm1/msg, and the callback returns with ret 0x10. Omitting
    // it advances ESP by four bytes per item and makes popad/ret jump into
    // heap data after an otherwise successful grant.
    byte(0x53);                            // preserve catalog row
    byte(0x56);                            // preserve table
    byte(0x57);                            // preserve player
    byte(0x55);                            // preserve selected count
    byte(0x6A); byte(0x00);               // push reserved = 0
    byte(0x53);                            // push prm2 = catalog row
    byte(0x6A); byte(static_cast<std::uint8_t>(kFullhandsCheat));
                                           // push prm1 = CHEAT_FULLHANDS
    byte(0x6A); byte(static_cast<std::uint8_t>(kCheatCallback));
                                           // push msg = CB_CHEAT
    byte(0x8B); byte(0xCF);                // mov ecx,edi
    byte(0x8B); byte(0x01);                // mov eax,[ecx]
    byte(0xFF); byte(0x50); byte(0x04);    // call actor cbProc, ret 0x10
    byte(0x5D);                            // restore selected count
    byte(0x5F);                            // restore player
    byte(0x5E);                            // restore table
    byte(0x5B);                            // restore catalog row
    byte(0x45);                            // inc ebp
    const std::size_t next_catalog_row = code.size();
    byte(0x43);                            // inc ebx
    byte(0x81); byte(0xFB); dword(kCatalogCapacity);
                                           // cmp ebx,400h
    byte(0x0F); byte(0x8C);                // jl catalog_loop
    const std::size_t loop_displacement = code.size();
    dword(0);
    byte(0x89); byte(0x25);                // mov [stack_observed],esp
    dword(static_cast<std::uint32_t>(stack_observed));
    byte(0x8B); byte(0x25);                // mov esp,[stack_checkpoint]
    dword(static_cast<std::uint32_t>(stack_checkpoint));
    // EBP carried the running total in from the previous run of this same
    // trigger, so a plain store keeps the row list and the count consistent.
    byte(0x89); byte(0x2D);                // mov [selected_count],ebp
    dword(static_cast<std::uint32_t>(selected_count));
    byte(0xC7); byte(0x05);                // mov [completion],1
    dword(static_cast<std::uint32_t>(completion));
    dword(1U);
    byte(0x61);                            // popad
    byte(0x9D);                            // popfd
    byte(0xC2); byte(0x04); byte(0x00);   // ret 4 (ProcessCheat(char))

    const auto patch_relative = [&](std::size_t displacement_offset,
                                    std::size_t target_offset)
    {
        const std::uint32_t displacement = relative(
            remote + target_offset, remote + displacement_offset + 4U);
        std::memcpy(
            code.data() + displacement_offset,
            &displacement,
            sizeof(displacement));
    };
    patch_relative(empty_row_displacement, next_catalog_row);
    patch_relative(excluded_row_displacement, next_catalog_row);
    patch_relative(empty_name_displacement, next_catalog_row);
    patch_relative(unknown_name_displacement, next_catalog_row);
    patch_relative(uniform_displacement, essential_item);
    patch_relative(key_displacement, essential_item);
    for (const std::size_t displacement : weapon_type_displacements)
        patch_relative(displacement, fast_weapon);
    patch_relative(validated_utility_displacement, utility_item);
    patch_relative(fast_full_displacement, next_catalog_row);
    patch_relative(fast_eligible_displacement, eligible_catalog_row);
    patch_relative(utility_pistol1_displacement, next_catalog_row);
    patch_relative(utility_pistol2_displacement, next_catalog_row);
    patch_relative(untracked_type_displacement, next_catalog_row);
    patch_relative(other_full_displacement, next_catalog_row);
    patch_relative(type_full_displacement, next_catalog_row);
    patch_relative(utility_eligible_displacement, eligible_catalog_row);
    patch_relative(essential_full_displacement, next_catalog_row);
    patch_relative(global_full_displacement, next_catalog_row);
    patch_relative(wrong_series_displacement, next_catalog_row);
    patch_relative(loop_displacement, catalog_loop);

    if (code.size() > kCatalogRowsOffset)
    {
        LogDiagnostic(
            "Fullhands: trampoline too large (%u bytes, maximum=%u).",
            static_cast<unsigned>(code.size()),
            static_cast<unsigned>(kCatalogRowsOffset));
        (void)process.FreeRemoteMemory(remote);
        return false;
    }

    const std::uint32_t pending = 0;
    const std::array<std::uint32_t, kCatalogCapacity> cleared_rows{};
    const std::uint32_t requested_series = g_fullhands_series % series_count;
    LogDiagnostic(
        "Fullhands: granting series %u of %u (stub %u bytes of %u available).",
        requested_series + 1U, series_count,
        static_cast<unsigned>(code.size()),
        static_cast<unsigned>(kCatalogRowsOffset));
    // The stub and the catalogue rows share one page. Refuse rather than let
    // the code run past its own area and rewrite the rows it is about to read.
    if (code.size() > kCatalogRowsOffset)
    {
        LogDiagnostic("Fullhands: stub would overrun the row table; refused.");
        (void)process.FreeRemoteMemory(remote);
        return false;
    }
    if (!process.WriteMemory(
            catalog_rows, cleared_rows.data(), sizeof(cleared_rows)) ||
        !process.WriteMemory(series_slot, requested_series) ||
        !process.WriteMemory(completion, pending) ||
        !process.WriteMemory(remote, code.data(), code.size()))
    {
        LogDiagnostic("Fullhands: remote code write failed.");
        (void)process.FreeRemoteMemory(remote);
        return false;
    }

    std::array<std::uint8_t, 5> jump{0xE9, 0, 0, 0, 0};
    const std::uint32_t hook_relative = relative(remote, hook + jump.size());
    std::memcpy(jump.data() + 1, &hook_relative, sizeof(hook_relative));
    if (!process.WriteProtectedMemory(hook, jump.data(), jump.size()))
    {
        LogDiagnostic(
            "Fullhands: hook write failed at %08X, rolling back.",
            static_cast<unsigned>(hook));
        std::array<std::uint8_t, 5> current{};
        if (process.ReadMemory(hook, current.data(), current.size()) &&
            current == jump)
        {
            (void)RestoreHookSafely(
        process, hook, original.data(), original.size());
        }
        // Do not free a page unless the original prologue is verified and no
        // game thread can still be executing the temporary trampoline.
        bool idle = false;
        if (process.ReadMemory(hook, current.data(), current.size()) &&
            current == original)
        {
            bool executing = false;
            idle = process.IsAnyThreadExecutingRange(
                remote, kRemoteSize, executing) && !executing;
        }
        LogDiagnostic(
            "Fullhands: rollback applied, remote page %s.",
            idle ? "freed" : "kept");
        if (idle)
            (void)process.FreeRemoteMemory(remote);
        return false;
    }

    const bool triggered = SendInventoryMainThreadTrigger(process);
    LogDiagnostic(
        "Fullhands: trigger %s, waiting for completion.",
        triggered ? "sent" : "failed");
    std::uint32_t completion_state = 0;
    int poll_attempts = 0;
    if (triggered)
    {
        for (; poll_attempts < 1000 && completion_state == 0; ++poll_attempts)
        {
            (void)process.ReadMemory(completion, completion_state);
            if (completion_state == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    std::uint32_t completed = 0;
    if (completion_state == 1U)
        (void)process.ReadMemory(selected_count, completed);
    LogDiagnostic(
        "Fullhands: completion_state=%u selected=%u after %d polls.",
        completion_state, completed, poll_attempts);

    // Report what the catalogue query actually answered for the first rows.
    // Comparing this with a read-only dump of the table tells immediately
    // whether a rejection comes from the query or from the filters.
    std::array<std::uint32_t, kProbeRowCount * 2U> probe{};
    if (process.ReadMemory(
            remote + kProbeRowsOffset, probe.data(), sizeof(probe)))
    {
        for (std::uint32_t row = 1; row < kProbeRowCount; ++row)
        {
            const std::uint32_t type = probe[row * 2U];
            const std::uint32_t name = probe[row * 2U + 1U];
            char text[5]{};
            std::memcpy(text, &name, 4);
            for (char& character : text)
            {
                if (character != '\0' &&
                    (character < 0x20 || character > 0x7E))
                {
                    character = '?';
                }
            }
            LogDiagnostic(
                "[TEST FULLHANDS] row=%u type=%u name4=%08X '%s'.",
                row, type, name, text);
        }
    }

    (void)RestoreHookSafely(
        process, hook, original.data(), original.size());
    std::array<std::uint8_t, 5> restored_bytes{};
    const bool restored = process.ReadMemory(
        hook, restored_bytes.data(), restored_bytes.size()) &&
        restored_bytes == original;
    LogDiagnostic(
        "Fullhands: hook %s.",
        restored ? "restored" : "restoration unverified");
    const bool grant_completed = completion_state == 1U && completed > 0U &&
        completed < kCatalogCapacity && restored;
    const bool inventory_guard_aborted =
        completion_state == 2U && restored;
    bool page_is_idle = false;
    if (grant_completed || inventory_guard_aborted)
    {
        for (int attempt = 0; attempt < 250 && !page_is_idle; ++attempt)
        {
            bool executing = false;
            page_is_idle = process.IsAnyThreadExecutingRange(
                remote, kRemoteSize, executing) && !executing;
            if (!page_is_idle)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    if (inventory_guard_aborted && page_is_idle)
    {
        const bool page_released = process.FreeRemoteMemory(remote);
        LogDiagnostic(
            "Fullhands: game-thread inv_scene guard aborted the whole press; "
            "no item granted, remote page released=%d.",
            page_released ? 1 : 0);
        return false;
    }
    if (!grant_completed || !page_is_idle)
    {
        LogDiagnostic(
            "Fullhands: grant rejected (completion_state=%u selected=%u "
            "completed=%d idle=%d).",
            completion_state, completed,
            grant_completed ? 1 : 0, page_is_idle ? 1 : 0);
        return false;
    }

    std::vector<std::uint32_t> expected_rows(completed);
    if (!process.ReadMemory(
            catalog_rows,
            expected_rows.data(),
            expected_rows.size() * sizeof(expected_rows.front())))
    {
        LogDiagnostic("Fullhands: selected catalog row list unreadable.");
        (void)process.FreeRemoteMemory(remote);
        return false;
    }
    for (std::size_t index = 0; index < expected_rows.size(); ++index)
    {
        const std::uint32_t row = expected_rows[index];
        if (row < kFirstCatalogRow || row >= kCatalogCapacity ||
            std::find(
                expected_rows.begin(), expected_rows.begin() + index, row) !=
                expected_rows.begin() + index)
        {
            LogDiagnostic(
                "Fullhands: invalid/duplicate catalog row %u at index %u.",
                row, static_cast<unsigned>(index));
            (void)process.FreeRemoteMemory(remote);
            return false;
        }
    }

    std::array<std::uint32_t, 10> selection_state{};
    const bool selection_state_read = process.ReadMemory(
        remote + kSelectionStateOffset,
        selection_state.data(),
        selection_state.size() * sizeof(selection_state.front()));
    LogDiagnostic(
        "Fullhands: curated selection total=%u limit=%u "
        "weapons=%u utility=%u essential=%u reserved=%u state_read=%d "
        "stack_before=%08X stack_after=%08X balanced=%d "
        "eligible_total=%u series_slot=%u.",
        completed, grant_limit,
        selection_state[1], selection_state[2], selection_state[3],
        selection_state[4], selection_state_read ? 1 : 0,
        selection_state[5], selection_state[6],
        selection_state_read && selection_state[5] == selection_state[6]
            ? 1 : 0,
        selection_state[8], selection_state[9] + 1U);

    const bool callback_stack_balanced = selection_state_read &&
        selection_state[5] != 0U &&
        selection_state[5] == selection_state[6];
    if (!callback_stack_balanced)
    {
        LogDiagnostic(
            "Fullhands: callback ABI stack mismatch; grant rejected safely.");
        (void)process.FreeRemoteMemory(remote);
        return false;
    }

    if (!process.FreeRemoteMemory(remote))
    {
        LogDiagnostic("Fullhands: safe remote page release failed.");
        return false;
    }

    std::vector<InventoryItemView> after_native_items;
    if (!read_inventory(after_native_items))
    {
        LogDiagnostic("Fullhands: inventory unreadable after native call.");
        return false;
    }
    const auto find_item = [](std::vector<InventoryItemView>& items,
                              std::uint32_t id) -> InventoryItemView*
    {
        for (InventoryItemView& item : items)
        {
            if (item.id == id)
                return &item;
        }
        return nullptr;
    };

    bool amount_write_failed = false;
    std::uint32_t present_after_native = 0;
    for (const std::uint32_t row : expected_rows)
    {
        InventoryItemView* item = find_item(after_native_items, row);
        if (!item)
        {
            LogDiagnostic(
                "Fullhands: controlled-player callback did not add item id=%u.",
                row);
            continue;
        }
        ++present_after_native;
        if (item->amount < kGrantedAmount &&
            !process.WriteMemory(
                item->object + kItemAmountOffset, kGrantedAmount))
        {
            amount_write_failed = true;
            LogDiagnostic(
                "Fullhands: reserve write failed for item id=%u at %08X.",
                row, static_cast<unsigned>(item->object));
        }
    }

    std::vector<InventoryItemView> final_items;
    if (amount_write_failed || !read_inventory(final_items))
    {
        LogDiagnostic("Fullhands: final inventory read/write failed.");
        return false;
    }
    std::uint32_t selected_inventory_item = ~0U;
    std::uint32_t active_inventory_item = ~0U;
    const bool selection_reset = process.ReadMemory(
            snapshot->player_object_address + kSelectedInventoryItemOffset,
            selected_inventory_item) &&
        process.ReadMemory(
            snapshot->player_object_address + kActiveInventoryItemOffset,
            active_inventory_item) &&
        selected_inventory_item == 0U && active_inventory_item == 0U;
    LogDiagnostic(
        "Fullhands: native selection reset selected=%u active=%u valid=%d.",
        selected_inventory_item, active_inventory_item,
        selection_reset ? 1 : 0);
    std::uint32_t verified_count = 0;
    for (const std::uint32_t row : expected_rows)
    {
        InventoryItemView* item = find_item(final_items, row);
        if (item && item->amount >= kGrantedAmount)
        {
            ++verified_count;
        }
        else
        {
            LogDiagnostic(
                "Fullhands: missing/short item id=%u amount=%u required=%u.",
                row, item ? item->amount : 0U, kGrantedAmount);
        }
    }
    LogDiagnostic(
        "Fullhands: native catalog=%u, inventory before=%u native_after=%u "
        "final=%u present_after_native=%u "
        "verified=%u/%u.",
        completed,
        static_cast<unsigned>(before_items.size()),
        static_cast<unsigned>(after_native_items.size()),
        static_cast<unsigned>(final_items.size()),
        present_after_native, verified_count, completed);
    std::uint32_t unexpected_items = 0;
    for (std::size_t index = 1; index < final_items.size(); ++index)
    {
        const std::uint32_t id = final_items[index].id;
        if (std::find(expected_rows.begin(), expected_rows.end(), id) ==
            expected_rows.end())
        {
            ++unexpected_items;
            LogDiagnostic(
                "Fullhands: unexpected carried item after replacement "
                "index=%u id=%u.",
                static_cast<unsigned>(index), id);
        }
    }
    const bool exact_replacement = selection_reset &&
        verified_count == completed &&
        unexpected_items == 0U &&
        final_items.size() <= expected_rows.size() + 1U;
    LogDiagnostic(
        "Fullhands: replacement exact=%d unexpected=%u expected_max=%u.",
        exact_replacement ? 1 : 0, unexpected_items,
        static_cast<unsigned>(expected_rows.size() + 1U));
    if (!exact_replacement)
        return false;

    // Advance only after the final inventory proves that the old lot is gone
    // and the requested lot is complete. A refused/partial press is retried.
    g_fullhands_series = (requested_series + 1U) % series_count;
    g_opens_since_grant = 0;
    LogDiagnostic(
        "Fullhands: series %u/%u replaced exactly; next press=%u/%u.",
        requested_series + 1U, series_count,
        g_fullhands_series + 1U, series_count);
    granted_count = verified_count;
    return true;
}
}

// Measures the freeze instead of guessing at it. The engine stamps its scene
// with the time of the last render at scene+0x18C; while the game thread is
// stalled that stamp stops advancing. Sampling it together with inv_scene and
// the item count gives the exact duration of every stall, what the inventory
// was doing at that instant, and how it relates to the last M press.
struct InventoryStallState
{
    std::uintptr_t player = 0;
    std::uint32_t last_render = 0;
    ULONGLONG last_change_tick = 0;
    ULONGLONG stall_start_tick = 0;
    std::uint32_t stall_scene = 0;
    std::uint32_t stall_items = 0;
    std::uint32_t previous_scene = 0;
    int reported = 0;
};

InventoryStallState g_inventory_stall{};

void ObserveInventoryState(
    TrainerProcess& process,
    const RadarSnapshot* radar_snapshot)
{
    constexpr std::uint32_t kSceneLastRenderTimeOffset = 0x18C;
    constexpr ULONGLONG kStallThresholdMs = 120;

    InventoryStallState& state = g_inventory_stall;
    if (!radar_snapshot ||
        !IsSanePointer(radar_snapshot->player_object_address) ||
        !IsSanePointer(radar_snapshot->scene_object_address))
    {
        return;
    }
    if (state.player != radar_snapshot->player_object_address)
    {
        state = {};
        state.player = radar_snapshot->player_object_address;
    }
    if (state.reported >= 60)
        return;
    // A backgrounded H&D stops rendering entirely, which is not a stall. The
    // 38 s "stall" of the first measurement was simply the trainer window
    // holding the foreground.
    if (!process.IsGameWindowActive())
    {
        state.last_change_tick = 0;
        state.stall_start_tick = 0;
        return;
    }

    std::uint32_t render_time = 0;
    std::array<std::uint32_t, 4> header{};
    if (!process.ReadMemory(
            radar_snapshot->scene_object_address +
                kSceneLastRenderTimeOffset,
            render_time) ||
        !process.ReadMemory(
            radar_snapshot->player_object_address + 0x5C,
            header.data(),
            sizeof(header)))
    {
        return;
    }
    const std::uint32_t items =
        (IsSanePointer(header[0]) && header[1] >= header[0])
            ? static_cast<std::uint32_t>((header[1] - header[0]) / 4U)
            : 0U;
    const std::uint32_t scene = header[3];
    const ULONGLONG now = GetTickCount64();

    if (scene != 0 && state.previous_scene == 0)
    {
        ++g_opens_since_grant;
        ++state.reported;
        LogDiagnostic(
            "[TEST STALL] inventory opened: items=%u open_index_since_M=%u.",
            items, g_opens_since_grant);
    }
    else if (scene == 0 && state.previous_scene != 0)
    {
        ++state.reported;
        LogDiagnostic("[TEST STALL] inventory closed: items=%u.", items);
    }
    state.previous_scene = scene;

    if (render_time != state.last_render)
    {
        if (state.stall_start_tick != 0)
        {
            const ULONGLONG duration = now - state.stall_start_tick;
            state.stall_start_tick = 0;
            if (duration >= kStallThresholdMs)
            {
                ++state.reported;
                LogDiagnostic(
                    "[TEST STALL] game thread stalled %llu ms "
                    "(inventory_open=%d items=%u open_index_since_M=%u).",
                    static_cast<unsigned long long>(duration),
                    state.stall_scene != 0 ? 1 : 0,
                    state.stall_items,
                    g_opens_since_grant);
            }
        }
        state.last_render = render_time;
        state.last_change_tick = now;
        return;
    }
    if (state.last_change_tick != 0 && state.stall_start_tick == 0 &&
        now - state.last_change_tick >= kStallThresholdMs)
    {
        state.stall_start_tick = state.last_change_tick;
        state.stall_scene = scene;
        state.stall_items = items;
    }
}

bool ApplyNativeActorCallback(
    TrainerProcess& process,
    const RadarSnapshot* snapshot,
    std::uint32_t message,
    std::uint32_t prm1);

// F3 - sortie forcee du vehicule. Le moteur expose exactement ce chemin :
// `C_human::cbProc(CB_USE_AUTO, 0, 0)` delie le soldat du siege, demande au
// vehicule sa position de descente, teste les collisions et le repose. C'est
// la meme routine que la touche de sortie normale du jeu, donc elle marche
// aussi quand le vehicule vole ou se trouve dans une position ou le jeu
// refuse la sortie habituelle.
//
// Un garde est indispensable : dans `case 0`, lorsque le vehicule passe a
// NULL, le moteur dereference `using_item` SANS test. L'appeler alors qu'on
// n'est dans aucun vehicule ferait planter le jeu.
// =====================================================================
// V103, 3 septembre 2026 - recherche de C_game_mission::CreateActor
// =====================================================================
//
// Cette fonction est la porte d'entree de toute creation d'acteur : soldats
// allies, clone de vehicule. Elle n'est PAS virtuelle, donc son adresse ne se
// deduit d'aucune table dans le jeu installe. Elle doit etre retrouvee par
// empreinte, a l'execution.
//
// L'empreinte vient du binaire de reference `source/hde/bin/HDE.exe` (13 mai
// 2002), livre avec sa table de symboles, ou la fonction se trouve a
// 0x004316A0 et commence par :
//
//   56              push esi
//   57              push edi
//   8B F9           mov  edi,ecx          ; this = mission
//   8B 4C 24 0C     mov  ecx,[esp+0Ch]    ; type
//   8B C1           mov  eax,ecx
//   83 E8 00        sub  eax,0
//   0F 84 rel32     je   <cas type 0>
//   48              dec  eax
//   0F 84 rel32     je   <cas type 1>
//   48              dec  eax
//   74 rel8         je   <cas type 2>
//
// Un prologue seul ne suffit pas a affirmer une identite. Le validateur est
// bien plus solide : quelques octets plus loin, la fonction inscrit l'acteur
// cree dans le vecteur de la mission :
//
//   8B 47 6C        mov eax,[edi+6Ch]     ; fin du vecteur d'acteurs
//   8D 4F 64        lea ecx,[edi+64h]     ; le vecteur lui-meme
//
// Or le trainer connait deja ces offsets et s'en sert tous les jours :
// kMissionActorBeginOffset = 0x68 et kMissionActorEndOffset = 0x6C. La
// disposition de la mission est donc IDENTIQUE entre les deux builds, ce qui
// fait de cette paire d'instructions un validateur fiable.
//
// Toute candidate qui ne satisfait pas les deux conditions est rejetee, et
// deux candidates valides annulent la recherche : mieux vaut ne rien faire que
// d'appeler la mauvaise fonction.
constexpr std::size_t kCreateActorValidatorWindow = 0x60;

// V108 - un echec ne se retient PLUS.
//
// Le journal du joueur a montre le defaut : la recherche partait a l'ouverture
// du trainer, avant que le jeu soit attache, tombait sur
// « module principal illisible », et le resultat vide etait garde pour toute
// la session. Deux minutes plus tard le jeu etait la et pret, mais l'empreinte
// n'etait jamais recherchee a nouveau : la creation restait refusee sans
// raison. Seul un succes est desormais retenu; un echec est reessaye, espace
// pour ne pas relire une image de plusieurs dizaines de mega-octets a chaque
// image affichee.
constexpr ULONGLONG kSignatureRetryDelayMs = 2000ULL;

struct CreateActorSearch
{
    DWORD process_id = 0;
    bool searched = false;
    ULONGLONG next_retry = 0;
    std::uintptr_t address = 0;
    unsigned candidates = 0;
};

CreateActorSearch g_create_actor{};

// =====================================================================
// V104 - toutes les adresses necessaires a la creation d'un soldat
// =====================================================================
//
// Emplacements de table virtuelle, releves dans le binaire de reference puis
// recoupes avec ceux que le trainer utilise deja dans le jeu installe. Le
// recoupement est la garantie : si un emplacement connu tombe juste dans les
// deux builds, les emplacements voisins de la meme table le sont aussi.
//
//  - AddProgram   : vtable+0x24. Releve dans la vtable de C_human du binaire
//                   de reference, ou DelProgram occupe +0x28 - exactement
//                   l'emplacement que le trainer utilise deja pour effacer les
//                   poursuites. Le decalage est donc verifie.
//  - SetActive    : vtable+0x6C, deja connu du trainer (reanimation).
//  - SetFrame     : vtable+0x80, releve dans la vtable de C_player.
//  - CreateModel  : vtable+0x68 du driver I3D. Dans le binaire de reference,
//                   dix-sept sites appellent cet emplacement sur le global
//                   `driver`, et aucun autre global n'est appele ainsi.
//  - Duplicate    : vtable+0x38 de I3D_frame. Deduit de l'en-tete
//                   `Insanity/Include/I3D/I3D2.h` : Duplicate y est le
//                   douzieme I3DMETHOD declare, et l'ancrage est SetPos, dont
//                   le trainer sait deja qu'il vaut +0x0C - d'ou le decalage
//                   de trois emplacements applique a toute l'interface.


// Le global `driver` du moteur. Introuvable par table, il est reconnu par le
// seul motif qui lui soit propre dans tout le binaire de reference :
//
//   A1 <driver> 50 8B 08 FF 51 68
//   mov eax,[driver] / push eax / mov ecx,[eax] / call [ecx+68h]
//
// soit `driver->CreateModel()`. Dix-sept sites, un seul global concerne.
std::uintptr_t FindCreateActor(TrainerProcess& process);

struct DriverSearch
{
    DWORD process_id = 0;
    bool searched = false;
    ULONGLONG next_retry = 0;
    std::uintptr_t global = 0;
    unsigned candidates = 0;
};

DriverSearch g_driver{};

std::uintptr_t FindDriverGlobal(TrainerProcess& process)
{
    if (!process.IsConnected())
        return 0;
    const ULONGLONG now = GetTickCount64();
    if (g_driver.searched && g_driver.process_id == process.ProcessId() &&
        (g_driver.global != 0 || now < g_driver.next_retry))
    {
        return g_driver.global;
    }

    const DWORD searched_process = process.ProcessId();
    g_driver = {};
    g_driver.process_id = searched_process;
    g_driver.searched = true;
    g_driver.next_retry = now + kSignatureRetryDelayMs;

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) || module.image_size == 0 ||
        module.image_size > 64U * 1024U * 1024U)
    {
        return 0;
    }
    std::vector<std::uint8_t> image(module.image_size);
    if (!process.ReadMemory(module.base_address, image.data(), image.size()))
    {
        const std::size_t chunk = 0x10000;
        std::fill(image.begin(), image.end(), std::uint8_t{0});
        for (std::size_t offset = 0; offset < image.size(); offset += chunk)
        {
            const std::size_t length =
                (std::min)(chunk, image.size() - offset);
            (void)process.ReadMemory(
                module.base_address + offset, image.data() + offset, length);
        }
    }

    // Compter les globals appeles par ce motif exact; le plus frequent gagne,
    // et on exige une nette domination pour ne pas se tromper.
    std::vector<std::pair<std::uint32_t, unsigned>> tally;
    for (std::size_t index = 0; index + 12 < image.size(); ++index)
    {
        if (image[index] != 0xA1 || image[index + 5] != 0x50 ||
            image[index + 6] != 0x8B || image[index + 7] != 0x08 ||
            image[index + 8] != 0xFF || image[index + 9] != 0x51 ||
            image[index + 10] != kDriverCreateModelVtableOffset)
        {
            continue;
        }
        std::uint32_t target = 0;
        std::memcpy(&target, image.data() + index + 1, sizeof(target));
        if (target < module.base_address ||
            target >= module.base_address + module.image_size)
        {
            continue;
        }
        auto entry = std::find_if(
            tally.begin(), tally.end(),
            [&](const std::pair<std::uint32_t, unsigned>& item)
            {
                return item.first == target;
            });
        if (entry == tally.end())
            tally.emplace_back(target, 1U);
        else
            ++entry->second;
    }
    std::sort(
        tally.begin(), tally.end(),
        [](const std::pair<std::uint32_t, unsigned>& a,
           const std::pair<std::uint32_t, unsigned>& b)
        {
            return a.second > b.second;
        });

    g_driver.candidates = static_cast<unsigned>(tally.size());
    if (!tally.empty() &&
        (tally.size() == 1U || tally[0].second >= tally[1].second * 3U))
    {
        g_driver.global = tally[0].first;
        LogDiagnostic(
            "Driver I3D: global TROUVE a %08X (%u sites, %u candidats).",
            static_cast<unsigned>(g_driver.global), tally[0].second,
            g_driver.candidates);
    }
    else
    {
        LogDiagnostic(
            "Driver I3D: INTROUVABLE (%u candidats, aucun ne domine). La "
            "duplication de modele est impossible.",
            g_driver.candidates);
    }
    return g_driver.global;
}

// Rapport d'aptitude : tout ce dont la creation de soldats a besoin, resolu et
// journalise en une seule ligne. Rien n'est execute tant que tout n'est pas la.
struct SpawnCapability
{
    std::uintptr_t create_actor = 0;
    std::uintptr_t driver_global = 0;
    std::uintptr_t driver = 0;
    std::uintptr_t create_model = 0;
    std::uintptr_t duplicate = 0;
    std::uintptr_t mission = 0;
    std::uintptr_t set_frame = 0;
    std::uintptr_t set_frame_offset = 0;
    bool ready = false;
};

// V107 - `SetFrame` de l'acteur, resolu a l'execution et non suppose.
//
// Les vtables du binaire de reference et de celui du joueur ne coincident pas :
// le decalage vaut 0 a +0x6C (`SetActive`), +8 a `IsEnemy`, +0xC a `Explode`,
// +0x18 a `Die` et `Hit`. Deluxe a donc insere des methodes virtuelles au fil
// de la table. `SetFrame` occupe +0x80 dans le binaire de reference, ce qui le
// place en plein dans la zone incertaine : son rang chez le joueur vaut +0x80,
// +0x84 ou +0x88 selon l'endroit ou les deux methodes ont ete inserees.
//
// Plutot que de parier, on reconnait la fonction a son prologue, comme pour
// `CreateActor`. Si aucune candidate ne se distingue, la creation est refusee
// et le journal donne les premiers octets de chaque rang de la fenetre : de
// quoi trancher sans jamais faire sortir le joueur du jeu.
struct SetFrameSearch
{
    DWORD process_id = 0;
    std::uintptr_t vtable = 0;
    std::uintptr_t target = 0;
    std::uintptr_t offset = 0;
};
SetFrameSearch g_set_frame{};

std::uintptr_t ResolveActorSetFrame(
    TrainerProcess& process,
    std::uintptr_t actor_vtable,
    std::uintptr_t& resolved_offset)
{
    resolved_offset = 0;
    if (!IsSanePointer(actor_vtable))
        return 0;
    // La table ne bouge pas tant que le jeu tourne : une reussite se retient,
    // sans quoi le journal recevait la meme ligne quinze fois par seconde.
    if (g_set_frame.offset != 0 &&
        g_set_frame.process_id == process.ProcessId() &&
        g_set_frame.vtable == actor_vtable)
    {
        resolved_offset = g_set_frame.offset;
        return g_set_frame.target;
    }

    // Prologue de `C_player::SetFrame` dans le binaire de reference (HDE.exe
    // du 13 mai 2002, adresse 0x00426380 d'apres HDE.map).
    static constexpr std::uint8_t kPrologue[] = {
        0x83, 0xEC, 0x08, 0x53, 0x55, 0x56, 0x8B, 0x74, 0x24, 0x18,
        0x33, 0xDB, 0x57, 0x3B, 0xF3, 0x8B, 0xF9};
    static constexpr std::uintptr_t kFirstCandidate = 0x70;
    static constexpr std::uintptr_t kLastCandidate = 0xA0;

    struct Candidate
    {
        std::uintptr_t offset = 0;
        std::uintptr_t target = 0;
        std::size_t matched = 0;
        std::array<std::uint8_t, 24> head{};
    };
    std::vector<Candidate> candidates;
    for (std::uintptr_t offset = kFirstCandidate; offset <= kLastCandidate;
         offset += sizeof(std::uintptr_t))
    {
        Candidate candidate;
        candidate.offset = offset;
        if (!process.ReadMemory(actor_vtable + offset, candidate.target) ||
            !process.IsReadableCodeTarget(candidate.target) ||
            !process.ReadMemory(
                candidate.target, candidate.head.data(), candidate.head.size()))
        {
            continue;
        }
        while (candidate.matched < sizeof(kPrologue) &&
            candidate.head[candidate.matched] == kPrologue[candidate.matched])
        {
            ++candidate.matched;
        }
        candidates.push_back(candidate);
    }

    // Deux paliers : le prologue entier, puis sa premiere moitie. On n'accepte
    // un palier que s'il designe une seule candidate.
    for (const std::size_t required : {sizeof(kPrologue), std::size_t{6}})
    {
        const Candidate* winner = nullptr;
        std::size_t hits = 0;
        for (const Candidate& candidate : candidates)
        {
            if (candidate.matched >= required)
            {
                winner = &candidate;
                ++hits;
            }
        }
        if (hits == 1 && winner != nullptr)
        {
            resolved_offset = winner->offset;
            g_set_frame.process_id = process.ProcessId();
            g_set_frame.vtable = actor_vtable;
            g_set_frame.target = winner->target;
            g_set_frame.offset = winner->offset;
            LogDiagnostic(
                "SetFrame acteur: rang +0x%02X (%08X), %u octets de prologue "
                "reconnus sur %u.",
                static_cast<unsigned>(winner->offset),
                static_cast<unsigned>(winner->target),
                static_cast<unsigned>(winner->matched),
                static_cast<unsigned>(sizeof(kPrologue)));
            return winner->target;
        }
    }

    static ULONGLONG next_dump = 0;
    const ULONGLONG now = GetTickCount64();
    if (now >= next_dump)
    {
        next_dump = now + 15000ULL;
        LogDiagnostic(
            "SetFrame acteur: INTROUVABLE dans la vtable %08X. Creation de "
            "soldats refusee (aucun risque pris). Releve des rangs :",
            static_cast<unsigned>(actor_vtable));
        for (const Candidate& candidate : candidates)
        {
            char bytes[80] = {};
            int written = 0;
            for (std::size_t index = 0; index < 12; ++index)
            {
                written += std::snprintf(
                    bytes + written, sizeof(bytes) - written, "%02X ",
                    candidate.head[index]);
            }
            LogDiagnostic(
                "  +0x%02X -> %08X : %s(%u octets communs)",
                static_cast<unsigned>(candidate.offset),
                static_cast<unsigned>(candidate.target), bytes,
                static_cast<unsigned>(candidate.matched));
        }
    }
    return 0;
}

bool ResolveSpawnCapability(
    TrainerProcess& process,
    const RadarSnapshot& snapshot,
    SpawnCapability& capability)
{
    capability = {};
    capability.create_actor = FindCreateActor(process);
    capability.driver_global = FindDriverGlobal(process);

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        kMissionPointerRva >= module.image_size ||
        !process.ReadMemory(
            module.base_address + kMissionPointerRva, capability.mission) ||
        !IsSanePointer(capability.mission))
    {
        return false;
    }
    if (capability.driver_global != 0)
    {
        std::uintptr_t driver_vtable = 0;
        if (process.ReadMemory(capability.driver_global, capability.driver) &&
            IsSanePointer(capability.driver) &&
            process.ReadMemory(capability.driver, driver_vtable) &&
            IsSanePointer(driver_vtable))
        {
            (void)process.ReadMemory(
                driver_vtable + kDriverCreateModelVtableOffset,
                capability.create_model);
        }
    }
    std::uintptr_t frame_vtable = 0;
    if (IsSanePointer(snapshot.player_frame_address) &&
        process.ReadMemory(snapshot.player_frame_address, frame_vtable) &&
        IsSanePointer(frame_vtable))
    {
        (void)process.ReadMemory(
            frame_vtable + kFrameDuplicateVtableOffset, capability.duplicate);
    }
    std::uintptr_t actor_vtable = 0;
    if (IsSanePointer(snapshot.player_object_address) &&
        process.ReadMemory(snapshot.player_object_address, actor_vtable))
    {
        capability.set_frame = ResolveActorSetFrame(
            process, actor_vtable, capability.set_frame_offset);
    }

    capability.ready = capability.create_actor != 0 &&
        IsSanePointer(capability.driver) &&
        capability.set_frame_offset != 0 &&
        process.IsReadableCodeTarget(capability.create_actor) &&
        process.IsReadableCodeTarget(capability.create_model) &&
        process.IsReadableCodeTarget(capability.duplicate);

    static ULONGLONG next_report = 0;
    const ULONGLONG now = GetTickCount64();
    if (now >= next_report)
    {
        next_report = now + 10000ULL;
        LogDiagnostic(
            "Creation de soldats - aptitude: CreateActor=%08X driver=%08X "
            "(global %08X) CreateModel=%08X Duplicate=%08X mission=%08X "
            "SetFrame=+0x%02X PRET=%u.",
            static_cast<unsigned>(capability.create_actor),
            static_cast<unsigned>(capability.driver),
            static_cast<unsigned>(capability.driver_global),
            static_cast<unsigned>(capability.create_model),
            static_cast<unsigned>(capability.duplicate),
            static_cast<unsigned>(capability.mission),
            static_cast<unsigned>(capability.set_frame_offset),
            capability.ready ? 1U : 0U);
    }
    return capability.ready;
}

std::uintptr_t FindCreateActor(TrainerProcess& process)
{
    if (!process.IsConnected())
        return 0;
    const ULONGLONG now = GetTickCount64();
    if (g_create_actor.searched &&
        g_create_actor.process_id == process.ProcessId() &&
        (g_create_actor.address != 0 || now < g_create_actor.next_retry))
    {
        return g_create_actor.address;
    }

    const DWORD searched_process = process.ProcessId();
    g_create_actor = {};
    g_create_actor.process_id = searched_process;
    g_create_actor.searched = true;
    g_create_actor.next_retry = now + kSignatureRetryDelayMs;

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) || module.image_size == 0 ||
        module.image_size > 64U * 1024U * 1024U)
    {
        LogDiagnostic("CreateActor: module principal illisible.");
        return 0;
    }

    std::vector<std::uint8_t> image(module.image_size);
    if (!process.ReadMemory(
            module.base_address, image.data(), image.size()))
    {
        // Une image entiere peut contenir des pages non lisibles. Relire par
        // tranches et ne garder que ce qui repond.
        const std::size_t chunk = 0x10000;
        std::fill(image.begin(), image.end(), std::uint8_t{0});
        for (std::size_t offset = 0; offset < image.size(); offset += chunk)
        {
            const std::size_t length =
                (std::min)(chunk, image.size() - offset);
            (void)process.ReadMemory(
                module.base_address + offset, image.data() + offset, length);
        }
    }

    static const std::array<std::uint8_t, 13> kPrologue{
        0x56, 0x57, 0x8B, 0xF9, 0x8B, 0x4C, 0x24, 0x0C,
        0x8B, 0xC1, 0x83, 0xE8, 0x00};
    static const std::array<std::uint8_t, 6> kValidator{
        0x8B, 0x47, 0x6C, 0x8D, 0x4F, 0x64};

    std::uintptr_t found = 0;
    for (std::size_t index = 0;
         index + kPrologue.size() + kCreateActorValidatorWindow < image.size();
         ++index)
    {
        if (std::memcmp(
                image.data() + index, kPrologue.data(), kPrologue.size()) != 0)
        {
            continue;
        }
        // Le validateur doit apparaitre dans la fenetre qui suit.
        bool validated = false;
        for (std::size_t probe = index + kPrologue.size();
             probe + kValidator.size() <
                 index + kPrologue.size() + kCreateActorValidatorWindow;
             ++probe)
        {
            if (std::memcmp(
                    image.data() + probe, kValidator.data(),
                    kValidator.size()) == 0)
            {
                validated = true;
                break;
            }
        }
        if (!validated)
            continue;
        ++g_create_actor.candidates;
        if (found == 0)
            found = module.base_address + index;
    }

    if (g_create_actor.candidates == 1)
    {
        g_create_actor.address = found;
        LogDiagnostic(
            "CreateActor: TROUVEE a %08X (une seule candidate valide). La "
            "creation d'acteurs est possible.",
            static_cast<unsigned>(found));
    }
    else if (g_create_actor.candidates == 0)
    {
        LogDiagnostic(
            "CreateActor: INTROUVABLE. Aucune suite d'octets du binaire "
            "installe ne correspond au prologue de reference suivi de "
            "l'ecriture du vecteur d'acteurs. La creation de soldats et le "
            "clone de vehicule restent impossibles.");
    }
    else
    {
        LogDiagnostic(
            "CreateActor: %u candidates valides, recherche annulee par "
            "prudence (premiere a %08X). Appeler la mauvaise fonction "
            "planterait le jeu.",
            g_create_actor.candidates, static_cast<unsigned>(found));
    }
    return g_create_actor.address;
}


// V98 - vehicules de la mission.
// Le moteur ne cree jamais un vehicule a partir de rien : `CreateActor(
// ACTOR_AUTOMOBIL)` ne prend aucune donnee et l'acteur est accroche a un frame
// deja charge par la carte (GameMission.cpp, chargement CT_ACTOR). La liste ne
// peut donc contenir que les vehicules presents dans la mission en cours.
struct MissionVehicle
{
    std::uintptr_t actor = 0;
    std::uintptr_t frame = 0;
    std::uint32_t type = 0;
    Vector3 position{};
    float distance = 0.0f;
    // C_version::curr_version. 0 = carrosserie intacte; toute autre valeur est
    // une version endommagee, et seule la methode native SetVersion peut la
    // faire revenir en arriere - voir UpdateVehicleRepair.
    std::uint32_t version = 0;
};

std::vector<MissionVehicle> g_vehicle_menu{};
std::vector<std::wstring> g_vehicle_menu_labels{};
int g_vehicle_menu_selection = 0;

bool CollectMissionVehicles(
    TrainerProcess& process,
    const RadarSnapshot& snapshot,
    std::vector<MissionVehicle>& vehicles)
{
    vehicles.clear();
    RemoteModuleInfo module{};
    std::uintptr_t mission = 0;
    std::uint32_t actor_begin = 0;
    std::uint32_t actor_end = 0;
    if (!process.GetMainModuleInfo(module) ||
        kMissionPointerRva >= module.image_size ||
        !process.ReadMemory(
            module.base_address + kMissionPointerRva, mission) ||
        !IsSanePointer(mission) ||
        !process.ReadMemory(
            mission + kMissionActorBeginOffset, actor_begin) ||
        !process.ReadMemory(mission + kMissionActorEndOffset, actor_end) ||
        !IsSanePointer(actor_begin) || actor_end < actor_begin ||
        (actor_end - actor_begin) % sizeof(std::uint32_t) != 0)
    {
        return false;
    }
    const std::size_t count =
        (actor_end - actor_begin) / sizeof(std::uint32_t);
    if (count == 0 || count > 4096)
        return false;
    std::vector<std::uint32_t> actors(count);
    if (!process.ReadMemory(
            actor_begin, actors.data(), actors.size() * sizeof(actors[0])))
    {
        return false;
    }
    const Vector3 origin = snapshot.player.position;
    for (const std::uint32_t raw : actors)
    {
        const std::uintptr_t actor = raw;
        std::uint32_t type = 0;
        std::uintptr_t frame = 0;
        Vector3 position{};
        if (!IsSanePointer(actor) ||
            !process.ReadMemory(actor + kActorTypeOffset, type) ||
            (type != kActorTypeAutomobile && type != kActorTypeAutoCannon) ||
            !process.ReadMemory(actor + kActorFrameOffset, frame) ||
            !IsSanePointer(frame) ||
            !process.ReadMemory(
                frame + kFrameWorldPositionOffset, position) ||
            !IsSaneWorldPosition(position))
        {
            continue;
        }
        MissionVehicle entry{};
        entry.actor = actor;
        entry.frame = frame;
        entry.type = type;
        entry.position = position;
        (void)process.ReadMemory(
            actor + kVersionCurrentIndexOffset, entry.version);
        const float dx = position.x - origin.x;
        const float dy = position.y - origin.y;
        const float dz = position.z - origin.z;
        entry.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        vehicles.push_back(entry);
    }
    std::sort(
        vehicles.begin(), vehicles.end(),
        [](const MissionVehicle& a, const MissionVehicle& b)
        {
            return a.distance < b.distance;
        });
    return !vehicles.empty();
}

// Un point degage devant le joueur. heading = atan2(direction.x, direction.z)
// dans le radar, donc l'avant vaut (sin, 0, cos).
Vector3 PointInFrontOfPlayer(const RadarSnapshot& snapshot, float metres)
{
    const float heading = snapshot.player.heading_radians;
    Vector3 destination = snapshot.player.position;
    destination.x += std::sin(heading) * metres;
    destination.z += std::cos(heading) * metres;
    return destination;
}

// Remise en etat : version de carrosserie intacte, mode roulant, et une
// resistance vivante si l'epave etait a zero.
bool RepairVehicleFields(TrainerProcess& process, std::uintptr_t vehicle)
{
    constexpr std::int32_t kRepairedResistance = 4000;
    std::int32_t resistance = 0;
    std::uint32_t mode = 0;
    const std::uint32_t undamaged = 0;
    bool ok = process.WriteMemory(
        vehicle + kVersionCurrentIndexOffset, undamaged);
    if (process.ReadMemory(vehicle + kVersionResistanceOffset, resistance) &&
        (resistance <= 0 || resistance > kMaximumVersionResistance))
    {
        ok = process.WriteMemory(
            vehicle + kVersionResistanceOffset, kRepairedResistance) && ok;
    }
    if (process.ReadMemory(vehicle + kAutomobileModeOffset, mode) &&
        mode == kAutomobileModeDestroyed)
    {
        ok = process.WriteMemory(
            vehicle + kAutomobileModeOffset, kAutomobileModeStopped) && ok;
    }
    return ok;
}

// V100 - reparation visuelle de la carrosserie, en restant au volant.
//
// `C_version::SetVersion` (Vehicle.cpp:117) fait quatre choses : montrer et
// cacher les hierarchies de frames des deux versions, re-inscrire les volumes de
// collision dans la scene, migrer la donnee d'acteur, puis ecrire l'entier.
// Elle n'est pas virtuelle, donc on ne peut pas l'appeler. Mais sa partie
// visible se refait a la main : `version_list` est un vecteur de frames a
// +0x5C/+0x60, et montrer ou cacher une hierarchie revient a poser ou effacer
// le bit FRMFLAGS_ON de son frame.
//
// Le joueur reste donc au volant, ne voit rien disparaitre ni reapparaitre :
// la carrosserie enfoncee s'efface et l'intacte prend sa place au meme endroit.
//
// LIMITE ASSUMEE ET DITE : les volumes de collision, eux, ne sont pas
// re-inscrits dans la scene - cela demanderait l'EnumFrames natif. La forme de
// collision peut donc rester celle de l'epave. Le vehicule s'affiche et se
// conduit normalement; c'est une reparation visuelle, pas une reconstruction.
bool RepairVehicleBodyInPlace(
    TrainerProcess& process,
    std::uintptr_t vehicle,
    std::uint32_t& previous_version)
{
    previous_version = 0;
    std::uint32_t list_begin = 0;
    std::uint32_t list_end = 0;
    if (!process.ReadMemory(
            vehicle + kVersionCurrentIndexOffset, previous_version) ||
        !process.ReadMemory(vehicle + kVersionListBeginOffset, list_begin) ||
        !process.ReadMemory(vehicle + kVersionListEndOffset, list_end) ||
        !IsSanePointer(list_begin) || list_end <= list_begin ||
        (list_end - list_begin) % sizeof(std::uint32_t) != 0)
    {
        return false;
    }
    const std::size_t count =
        (list_end - list_begin) / sizeof(std::uint32_t);
    if (count < 2 || count > kMaximumVehicleVersions ||
        previous_version == 0 || previous_version >= count)
    {
        // Rien a redresser, ou disposition inattendue : ne rien ecrire.
        return previous_version == 0;
    }

    std::uint32_t damaged_frame = 0;
    std::uint32_t intact_frame = 0;
    if (!process.ReadMemory(
            list_begin + previous_version * sizeof(std::uint32_t),
            damaged_frame) ||
        !process.ReadMemory(list_begin, intact_frame) ||
        !IsSanePointer(damaged_frame) || !IsSanePointer(intact_frame))
    {
        return false;
    }

    std::uint32_t damaged_flags = 0;
    std::uint32_t intact_flags = 0;
    if (!process.ReadMemory(
            damaged_frame + kFrameFlagsOffset, damaged_flags) ||
        !process.ReadMemory(intact_frame + kFrameFlagsOffset, intact_flags))
    {
        return false;
    }
    const bool hidden = process.WriteMemory(
        damaged_frame + kFrameFlagsOffset, damaged_flags & ~kFrameOnFlag);
    const bool shown = process.WriteMemory(
        intact_frame + kFrameFlagsOffset, intact_flags | kFrameOnFlag);
    const std::uint32_t undamaged = 0;
    const bool indexed = process.WriteMemory(
        vehicle + kVersionCurrentIndexOffset, undamaged);
    LogDiagnostic(
        "Reparation carrosserie: vehicule=%08X version=%u->0 versions=%u "
        "cache=%08X montre=%08X hidden=%u shown=%u index=%u.",
        static_cast<unsigned>(vehicle),
        static_cast<unsigned>(previous_version),
        static_cast<unsigned>(count),
        static_cast<unsigned>(damaged_frame),
        static_cast<unsigned>(intact_frame),
        hidden ? 1U : 0U, shown ? 1U : 0U, indexed ? 1U : 0U);
    return hidden && shown && indexed;
}

// V99 - pourquoi G ne redressait pas une carrosserie enfoncee.
//
// Le trainer ecrivait `curr_version = 0`, et cela ne change strictement rien a
// l'ecran. La source du moteur le dit sans ambiguite : c'est
// `C_version::SetVersion` (Vehicle.cpp:117) qui fait le travail, et il fait
// beaucoup plus qu'ecrire l'entier :
//
//     version_list[curr]->EnumFrames(cbShow, false, LIGHT|SOUND|VISUAL)
//     version_list[curr]->EnumFrames(cbVol,  NULL,  VOLUME)
//     version_list[new ]->EnumFrames(cbShow, true,  LIGHT|SOUND|VISUAL)
//     version_list[new ]->EnumFrames(cbVol,  scene, VOLUME)
//     version_list[curr]->SetOn(false); version_list[new]->SetOn(true);
//     ... migration de l'acteur, remise a zero de l'animation, envoi reseau
//
// Autrement dit : montrer et cacher des hierarchies de frames entieres, et
// surtout re-inscrire les volumes de collision dans la scene. Ecrire l'index
// seul laisse l'epave affichee et ses volumes en place.
//
// SetVersion n'est pas virtuelle : elle n'apparait dans aucune table, donc son
// adresse ne se deduit pas depuis la vtable du vehicule comme cbProc. Le seul
// chemin de rappel qui l'atteint est CB_SIGNAL/SIGNAL_DETECTOR sous-type 0
// (Vehicle.cpp:319) et il n'avance QUE d'un cran vers plus abime, jamais en
// arriere. Reparer un modele en place demanderait donc de localiser SetVersion
// dans le binaire installe, ce qui n'est pas fait a ce jour.
//
// G rend donc ce qu'il peut rendre de facon sure :
//  - l'etat mecanique du vehicule vise (resistance, etat d'epave);
//  - et surtout, a pied, il amene devant vous le vehicule INTACT le plus
//    proche, ce qui donne bien « une voiture parfaite devant moi ».
void UpdateVehicleRepair(
    TrainerProcess& process,
    const RadarSnapshot* snapshot,
    std::uintptr_t controlled_vehicle,
    GameplaySettings& settings,
    GameplayStatus& status)
{
    if (!settings.vehicle_repair_requested)
        return;
    settings.vehicle_repair_requested = false;
    if (!process.IsConnected() || !snapshot ||
        !IsSanePointer(snapshot->player_object_address))
    {
        status.vehicle_repair = VehicleActionStatus::WaitingForPlayer;
        return;
    }

    // V100 : sur demande explicite du joueur, G n'agit QUE lorsqu'il est au
    // volant. A pied, la touche ne fait rien - c'est F6 qui sert a amener un
    // vehicule.
    //
    // V103 : `ResolveControlledVehicle` exige que le joueur soit assis en
    // place zero, celle du conducteur, et que la reference arriere du frame
    // corresponde. Un passager, ou un etat transitoire pendant l'animation
    // d'entree, la fait echouer alors que le joueur est bel et bien dans un
    // vehicule - G repondait alors « montez dans un vehicule » sans rien
    // faire. On retombe donc sur `using_item`, qui dit simplement ce que le
    // soldat occupe.
    std::uintptr_t target_vehicle = controlled_vehicle;
    if (!IsSanePointer(target_vehicle))
    {
        std::uintptr_t using_item = 0;
        std::uint32_t using_type = 0;
        if (process.ReadMemory(
                snapshot->player_object_address + kActorUsingItemOffset,
                using_item) &&
            IsSanePointer(using_item) &&
            process.ReadMemory(using_item + kActorTypeOffset, using_type) &&
            (using_type == kActorTypeAutomobile ||
             using_type == kActorTypeAutoCannon))
        {
            target_vehicle = using_item;
            LogDiagnostic(
                "Reparation vehicule: resolution stricte en echec, "
                "utilisation de using_item=%08X type=%u.",
                static_cast<unsigned>(using_item),
                static_cast<unsigned>(using_type));
        }
    }
    if (!IsSanePointer(target_vehicle))
    {
        // V104 : a pied, la meme touche ouvre la fenetre des soldats. C'est ce
        // que le joueur a demande - un seul G, deux comportements selon qu'on
        // soit au volant ou a pied.
        settings.soldier_menu_toggle_requested = true;
        LogDiagnostic(
            "G a pied: ouverture de la fenetre des soldats.");
        status.vehicle_repair = VehicleActionStatus::NoVehicle;
        return;
    }
    const std::uintptr_t controlled_vehicle_resolved = target_vehicle;

    std::uint32_t previous_version = 0;
    const bool body = RepairVehicleBodyInPlace(
        process, controlled_vehicle_resolved, previous_version);
    const bool fields = RepairVehicleFields(
        process, controlled_vehicle_resolved);
    LogDiagnostic(
        "Reparation vehicule: au volant cible=%08X version=%u carrosserie=%u "
        "mecanique=%u.",
        static_cast<unsigned>(controlled_vehicle_resolved),
        static_cast<unsigned>(previous_version),
        body ? 1U : 0U, fields ? 1U : 0U);
    status.vehicle_repair = (body && fields)
        ? VehicleActionStatus::Success
        : VehicleActionStatus::Failed;
}

// =====================================================================
// V104 - fenetre des soldats et ordres par la carte
// =====================================================================
//
// La liste contient tous les soldats que cette machine possede : l'escouade
// d'origine, et - des que la creation d'acteurs sera possible - les soldats
// crees, qui sont eux aussi des acteurs de type joueur appartenant a ce PC.
// Le systeme d'ordres est donc ecrit une fois pour les deux.
//
// La carte n'est jamais affichee d'office : c'est la carte NATIVE du jeu,
// ouverte par K, qui sert a designer la destination. Elle n'apparait donc que
// sur demande et ne masque rien le reste du temps.
std::vector<std::uintptr_t> g_soldier_menu{};
std::vector<std::wstring> g_soldier_menu_labels{};
int g_soldier_menu_selection = 0;

// =====================================================================
// V105 - creation de soldats allies, et bascule de controle
// =====================================================================
//
// La sequence est celle que le moteur suit lui-meme :
//
//   PI3D_model mod = driver->CreateModel();     // GameMission.cpp:3018
//   mod->Duplicate(source);                     //   (code de la neige)
//   act = mission.CreateActor(ACTOR_PLAYER, 0); // GameMission.cpp:1759
//   act->SetFrame(mod);                         //   comme au chargement
//   mod->SetPos(&destination);
//
// Le modele duplique est celui du soldat pilote : les soldats crees portent
// donc l'uniforme exact de la mission en cours, sans qu'aucune tenue n'ait a
// etre connue. Ce sont des acteurs de type joueur, donc `PlayerSwitch` les
// enumere (GameMission.cpp:3047, liste dynamique, pas limitee a quatre) et
// `SetActive` permet de les incarner comme le soldat d'origine.
//
// GARDE : rien n'est execute tant que `ResolveSpawnCapability` n'a pas resolu
// ET valide les six adresses. C'est le meme principe que tous les crochets du
// trainer, qui refusent d'agir quand une signature ne correspond pas.
constexpr std::size_t kMaximumSpawnedSoldiers = 50;

// Definie plus bas : pose toutes les positions en un seul passage sur le
// thread du jeu.
bool SetFramePositionsOnMainThread(
    TrainerProcess& process,
    const std::vector<std::uintptr_t>& frames,
    const std::vector<Vector3>& positions);

// Vrai tant que le joueur est en train de taper le nombre de soldats : le
// chiffre suivant s'ajoute alors a droite au lieu de repartir de zero.
bool g_soldier_count_typing = false;

// Ce que chaque ligne de la fenetre declenche. Passer par une table d'actions
// plutot que par des indices en dur supprime toute une categorie d'erreurs :
// ajouter ou retirer une ligne ne decale plus rien.
enum class SoldierMenuAction
{
    Create,
    Group,
    All,
    ReturnToOriginal,
    RemoveCreated,
    WeaponMirror,
    Control,   // Entree : prendre le controle
    SendOne,   // ordre de deplacement pour ce seul soldat
    // V142 - le ralliement d'ennemis.
    None,             // ligne d'information : Entree ne fait rien
    RallyEnemies,     // faire passer N ennemis de notre cote
    ReleaseEnemies,   // les rendre au camp allemand
    SendRallied,      // ordre de deplacement pour tous les rallies
    RalliedComeToMe,  // ils viennent a la position du joueur
    FreeMoveAlly,     // main libre : piloter cet allie comme soi-meme
    FreeMoveRelease,  // rendre le pilotage a son propre soldat
    SwitchCycleToggle,// la touche de switch continue-t-elle sur les allies ?
    OpenFire,         // chaque rallie attaque l'ennemi le plus proche
    OpenFireAll,      // et il enchaine sur le suivant, jusqu'au dernier
    SendOneRallied,   // ordre de deplacement pour un seul rallie
};

struct SoldierMenuEntryData
{
    SoldierMenuAction action = SoldierMenuAction::Create;
    std::size_t index = 0;  // rang dans g_soldier_menu, pour Control
};

std::vector<SoldierMenuEntryData> g_soldier_actions{};

// =====================================================================
// V159 - CHAQUE FENETRE SE ROUVRE SUR LE DERNIER CHOIX
// =====================================================================
//
// Le joueur : « quand j'ouvre la fenetre je me retrouve en haut a chaque fois,
// alors que je veux me retrouver par defaut sur le dernier choix. »
//
// On ne retient PAS le numero de la ligne, mais L'ACTION qu'elle porte - et,
// pour les lignes propres a un homme, son rang. La raison est que le contenu
// de la fenetre change : les lignes des allies rallies n'existent que s'il y
// en a, celle du retour au soldat d'origine n'apparait que si l'on pilote
// quelqu'un d'autre, et ainsi de suite. Un numero garde tel quel designerait
// une ligne differente d'une ouverture a l'autre.
//
// A la reouverture, on cherche la ligne qui porte la meme action; si elle a
// disparu, on retombe simplement sur la premiere.
bool g_soldier_last_valid = false;
SoldierMenuAction g_soldier_last_action = SoldierMenuAction::Create;
std::size_t g_soldier_last_index = 0;
bool g_soldier_menu_just_opened = false;

bool SpawnActorsOnGameThread(
    TrainerProcess& process,
    const SpawnCapability& capability,
    std::uintptr_t source_frame,
    const std::vector<Vector3>& destinations,
    std::uint8_t actor_type,
    std::vector<std::uintptr_t>& created)
{
    created.clear();
    if (!capability.ready || destinations.empty() ||
        !IsSanePointer(source_frame))
    {
        return false;
    }
    const std::uint32_t count = static_cast<std::uint32_t>(
        (std::min)(destinations.size(), kMaximumSpawnedSoldiers));

    constexpr std::uintptr_t kProcessCheatHookRva = 0x0009'FC10;
    constexpr std::array<std::uint8_t, 5> kProcessCheatExpected{
        0xB8, 0x5C, 0x10, 0x00, 0x00};
    constexpr std::size_t kSpawnRemoteSize = 0x2000;
    constexpr std::size_t kSpawnCountOffset = 0x400;
    constexpr std::size_t kSpawnSourceOffset = 0x404;
    constexpr std::size_t kSpawnMissionOffset = 0x408;
    constexpr std::size_t kSpawnDriverGlobalOffset = 0x40C;
    constexpr std::size_t kSpawnCreateActorOffset = 0x410;
    constexpr std::size_t kSpawnCompletionOffset = 0x414;
    constexpr std::size_t kSpawnCreatedCountOffset = 0x418;
    constexpr std::size_t kSpawnDestinationsOffset = 0x500;
    constexpr std::size_t kSpawnActorsOffset = 0x800;

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        kProcessCheatHookRva >= module.image_size)
    {
        return false;
    }

    // Le modele duplique doit etre raccroche a la meme branche que le soldat
    // d'origine. La V108 lisait le parent a `frame+0x18`, un emplacement
    // suppose : s'il ne designe pas le parent, `LinkTo` raccroche le soldat
    // n'importe ou dans la scene - ce qui explique exactement ce que le joueur
    // a vu, des soldats apparaissant tres loin de lui. Le parent est
    // maintenant DEMANDE au moteur, dans le stub, par `GetParent()`.

    const std::uintptr_t hook = module.base_address + kProcessCheatHookRva;
    std::array<std::uint8_t, 5> original{};
    if (!process.ReadMemory(hook, original.data(), original.size()) ||
        original != kProcessCheatExpected)
    {
        LogDiagnostic("Creation de soldats: site de cheat non reconnu.");
        return false;
    }
    const std::uintptr_t remote =
        process.AllocateRemoteMemory(kSpawnRemoteSize);
    if (!IsSanePointer(remote))
        return false;

    std::vector<std::uint8_t> code;
    const auto byte = [&](std::uint8_t value) { code.push_back(value); };
    const auto dword = [&](std::uint32_t value)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(value));
        std::memcpy(code.data() + offset, &value, sizeof(value));
    };
    const auto slot = [&](std::size_t offset)
    {
        dword(static_cast<std::uint32_t>(remote + offset));
    };
    const auto near_jump = [&](std::uint8_t condition)
    {
        byte(0x0F); byte(condition);
        const std::size_t displacement = code.size();
        dword(0);
        return displacement;
    };
    // `call [reg+deplacement]`. La forme courte ne porte qu'un octet SIGNE :
    // au-dela de 0x7F il faut la forme longue, sans quoi +0x98 se lirait -104
    // et l'appel partirait n'importe ou. La V105 n'emettait que la forme
    // courte, ce qui condamnait tout rang superieur a 0x7F.
    const auto call_indirect = [&](std::uint8_t short_form,
                                   std::uint8_t long_form,
                                   std::uintptr_t offset)
    {
        byte(0xFF);
        if (offset <= 0x7F)
        {
            byte(short_form);
            byte(static_cast<std::uint8_t>(offset));
        }
        else
        {
            byte(long_form);
            dword(static_cast<std::uint32_t>(offset));
        }
    };
    // vtable chargee dans ecx (methodes des frames I3D)
    const auto call_via_ecx = [&](std::uintptr_t offset)
    {
        call_indirect(0x51, 0x91, offset);
    };
    // vtable chargee dans eax (methodes des acteurs du jeu)
    const auto call_via_eax = [&](std::uintptr_t offset)
    {
        call_indirect(0x50, 0x90, offset);
    };
    std::vector<std::size_t> abandon;

    byte(0x9C);                                  // pushfd
    byte(0x60);                                  // pushad

    // V110 - GARDE DE REENTREE.
    //
    // Le trampoline est pose sur le site de traitement des cheats, un site que
    // le jeu traverse a chaque image. `SendInventoryMainThreadTrigger` ne fait
    // que provoquer un premier passage : tous les passages suivants, pendant
    // la seconde ou le trampoline reste en place, REEXECUTAIENT la boucle
    // entiere. Le joueur demandait quatre soldats et en recevait des dizaines.
    //
    // Le compteur de creations, lui, s'incrementait sans limite - mais le
    // trainer le plafonnait avant de l'ecrire dans le journal, si bien que
    // celui-ci affichait fidelement « crees=4 » quel que soit le desastre.
    // Le plafond est retire plus bas : le journal dit maintenant la verite.
    //
    // Un seul passage execute desormais quoi que ce soit : si le drapeau
    // d'achevement est deja pose, on saute directement aux octets d'origine.
    byte(0x83); byte(0x3D); slot(kSpawnCompletionOffset); byte(0x00);
    const std::size_t already_done = near_jump(0x85);   // jne -> fin

    byte(0x33); byte(0xF6);                      // xor esi,esi   (indice)
    const std::size_t loop_start = code.size();
    byte(0xA1); slot(kSpawnCountOffset);         // mov eax,[count]
    byte(0x3B); byte(0xF0);                      // cmp esi,eax
    abandon.push_back(near_jump(0x83));          // jae fin

    // driver = *[driver_global]
    byte(0xA1); slot(kSpawnDriverGlobalOffset);  // mov eax,[global]
    byte(0x85); byte(0xC0);
    abandon.push_back(near_jump(0x84));
    byte(0x8B); byte(0x00);                      // mov eax,[eax] = driver
    byte(0x85); byte(0xC0);
    abandon.push_back(near_jump(0x84));
    byte(0x50);                                  // push driver (this)
    byte(0x8B); byte(0x08);                      // mov ecx,[eax] = vtable
    call_via_ecx(kDriverCreateModelVtableOffset);
    byte(0x85); byte(0xC0);                      // test eax,eax
    abandon.push_back(near_jump(0x84));
    byte(0x8B); byte(0xD8);                      // mov ebx,eax = modele

    // modele->Duplicate(source)
    byte(0xFF); byte(0x35); slot(kSpawnSourceOffset);   // push source
    byte(0x53);                                         // push modele
    byte(0x8B); byte(0x0B);                             // mov ecx,[ebx]
    call_via_ecx(kFrameDuplicateVtableOffset);

    // modele->LinkTo(source->GetParent(), 0)
    //
    // Le parent est demande au moteur au moment ou l'on en a besoin, sur la
    // frame source elle-meme. Aucun offset de champ n'est suppose : seul le
    // rang de `GetParent` intervient, et il vient de la meme enumeration que
    // `Duplicate`, que le jeu vient de valider. Si le moteur rend un parent
    // nul, on ne raccroche rien plutot que de raccrocher au hasard.
    byte(0xFF); byte(0x35); slot(kSpawnSourceOffset);    // push source
    byte(0xA1); slot(kSpawnSourceOffset);                // mov eax,source
    byte(0x8B); byte(0x08);                              // mov ecx,[eax]
    call_via_ecx(kFrameGetParentVtableOffset);           // eax = parent
    byte(0x85); byte(0xC0);                              // test eax,eax
    const std::size_t skip_link = near_jump(0x84);       // je apres LinkTo
    byte(0x6A); byte(0x00);                              // push flags = 0
    byte(0x50);                                          // push parent
    byte(0x53);                                          // push modele
    byte(0x8B); byte(0x0B);                              // mov ecx,[ebx]
    call_via_ecx(kFrameLinkToVtableOffset);
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            code.size() - (skip_link + sizeof(std::int32_t)));
        std::memcpy(code.data() + skip_link, &relative, sizeof(relative));
    }

    // modele->SetOn(1)
    byte(0x6A); byte(0x01);
    byte(0x53);
    byte(0x8B); byte(0x0B);
    call_via_ecx(kFrameSetOnVtableOffset);

    // mission->CreateActor(ACTOR_PLAYER, 0)
    byte(0x6A); byte(0x00);                      // push data = 0
    byte(0x6A); byte(actor_type);
    byte(0x8B); byte(0x0D); slot(kSpawnMissionOffset);  // mov ecx,mission
    byte(0xA1); slot(kSpawnCreateActorOffset);          // mov eax,CreateActor
    byte(0xFF); byte(0xD0);                             // call eax
    byte(0x85); byte(0xC0);
    abandon.push_back(near_jump(0x84));
    byte(0x8B); byte(0xF8);                      // mov edi,eax = acteur

    // acteur->SetFrame(modele) - rang resolu a l'execution, jamais suppose
    byte(0x53);                                  // push modele
    byte(0x8B); byte(0xCF);                      // mov ecx,edi
    byte(0x8B); byte(0x01);                      // mov eax,[ecx]
    call_via_eax(capability.set_frame_offset);

    // V110 - la position n'est PLUS posee ici.
    //
    // `SetPos` place une frame RELATIVEMENT a son parent, alors que la
    // position que le trainer connait est une position de MONDE. Les deux ne
    // coincident que si le parent est a l'origine, ce qui n'a rien de garanti :
    // c'est ce qui faisait apparaitre les soldats tres loin du joueur, y
    // compris apres la correction du parent en V109.
    //
    // Le placement se fait maintenant apres coup, avec
    // `SetFramePositionOnMainThread` - la routine que le trainer emploie deja
    // pour les teleportations et pour deplacer les vehicules de F6, et que le
    // jeu valide donc a chaque utilisation. Aucune question d'espace de
    // coordonnees ne se pose plus : c'est exactement le chemin qui marche.

    // memoriser l'acteur cree
    byte(0x89); byte(0x3C); byte(0xB5);
    slot(kSpawnActorsOffset);                    // mov [esi*4+base],edi
    byte(0xFF); byte(0x05); slot(kSpawnCreatedCountOffset);  // inc [count]
    byte(0x46);                                  // inc esi
    byte(0xE9);
    const std::size_t repeat = code.size();
    dword(0);

    const std::size_t finish = code.size();
    abandon.push_back(already_done);
    for (const std::size_t displacement : abandon)
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            finish - (displacement + sizeof(std::int32_t)));
        std::memcpy(code.data() + displacement, &relative, sizeof(relative));
    }
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            loop_start - (repeat + sizeof(std::int32_t)));
        std::memcpy(code.data() + repeat, &relative, sizeof(relative));
    }
    byte(0xC7); byte(0x05); slot(kSpawnCompletionOffset); dword(1);
    byte(0x61);                                  // popad
    byte(0x9D);                                  // popfd
    code.insert(code.end(), original.begin(), original.end());
    byte(0xE9);
    const std::int32_t back = static_cast<std::int32_t>(
        (hook + original.size()) -
        (remote + code.size() + sizeof(std::int32_t)));
    dword(static_cast<std::uint32_t>(back));

    const std::uint32_t zero = 0;
    bool written = code.size() < kSpawnCountOffset &&
        process.WriteMemory(remote, code.data(), code.size()) &&
        process.WriteMemory(remote + kSpawnCountOffset, count) &&
        process.WriteMemory(
            remote + kSpawnSourceOffset,
            static_cast<std::uint32_t>(source_frame)) &&
        process.WriteMemory(
            remote + kSpawnMissionOffset,
            static_cast<std::uint32_t>(capability.mission)) &&
        process.WriteMemory(
            remote + kSpawnDriverGlobalOffset,
            static_cast<std::uint32_t>(capability.driver_global)) &&
        process.WriteMemory(
            remote + kSpawnCreateActorOffset,
            static_cast<std::uint32_t>(capability.create_actor)) &&
        process.WriteMemory(remote + kSpawnCompletionOffset, zero) &&
        process.WriteMemory(remote + kSpawnCreatedCountOffset, zero);
    for (std::uint32_t index = 0; written && index < count; ++index)
    {
        written = process.WriteMemory(
            remote + kSpawnDestinationsOffset + index * sizeof(Vector3),
            destinations[index]);
    }
    if (!written)
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }

    std::array<std::uint8_t, 5> patch{0xE9, 0, 0, 0, 0};
    const std::int32_t relative = static_cast<std::int32_t>(
        remote - (hook + patch.size()));
    std::memcpy(patch.data() + 1, &relative, sizeof(relative));
    if (!InstallHookSafely(
            process, hook, patch.data(), patch.size(),
            "le saut vers le code pose"))
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }

    const bool triggered = SendInventoryMainThreadTrigger(process);
    std::uint32_t completed = 0;
    if (triggered)
    {
        for (int attempt = 0; attempt < 500 && completed == 0; ++attempt)
        {
            (void)process.ReadMemory(
                remote + kSpawnCompletionOffset, completed);
            if (completed == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    (void)RestoreHookSafely(
        process, hook, original.data(), original.size());

    std::uint32_t created_count = 0;
    (void)process.ReadMemory(
        remote + kSpawnCreatedCountOffset, created_count);
    // Plus de plafond : si le stub a cree davantage que demande, il faut que
    // cela se VOIE. Le plafond precedent masquait exactement le defaut que le
    // joueur signalait.
    const std::uint32_t reported = created_count;
    if (created_count > count)
    {
        LogDiagnostic(
            "Creation d'acteurs: ANOMALIE, %u acteurs crees pour %u demandes "
            "(le trampoline a ete traverse plusieurs fois).",
            static_cast<unsigned>(created_count),
            static_cast<unsigned>(count));
        created_count = count;
    }
    for (std::uint32_t index = 0; index < created_count; ++index)
    {
        std::uint32_t actor = 0;
        if (process.ReadMemory(
                remote + kSpawnActorsOffset + index * sizeof(std::uint32_t),
                actor) &&
            IsSanePointer(actor))
        {
            created.push_back(actor);
        }
    }
    QueueRemotePageRelease(process, remote, kSpawnRemoteSize);

    // =================================================================
    // V113 - EMPECHER LE PLANTAGE QUAND UN ENNEMI TIRE SUR UN SOLDAT CREE
    // =================================================================
    //
    // Le moteur, des qu'un joueur encaisse, appelle :
    //
    //   void SetResistance(int r){
    //      resistance = r;
    //      mission.game_menu.SetHealth(menu_id, (float)resistance/init_...);
    //   }
    //
    // et `C_game_menu::SetHealth` n'a AUCUNE borne :
    //
    //   void C_game_menu::SetHealth(dword plr_id, float f){
    //      assert(pmenu[plr_id]);
    //      if(!pmenu[plr_id]) return;      // lit pmenu[-1] avant de garder
    //      pmenu[plr_id]->SetHealth(f);    // appel sur ce pointeur
    //   }
    //
    // Le constructeur de `C_player` pose `menu_id = AddPlayerMenu(1)`, qui rend
    // -1 des que les quatre places du bandeau sont prises. Sur un soldat cree
    // en surnombre, `menu_id` vaut donc -1, `pmenu[0xFFFFFFFF]` est lu hors du
    // tableau, et l'appel virtuel qui suit part sur un pointeur quelconque :
    // LE JEU S'ARRETE. C'est exactement ce que le joueur constate quand les
    // ennemis viennent le frapper.
    //
    // Le moteur offre lui-meme la coupure, sur le chemin exact du degat :
    //
    //   case CB_HIT:  if(no_hit_cheat) return 0;   // Actors.cpp:13462
    //
    // On la pose donc sur chaque soldat cree. `no_hit_cheat` (+0x2D4) est le
    // champ dont le trainer se sert deja pour l'immortalite : son emplacement
    // est etabli et valide en jeu, il n'est pas suppose.
    //
    // Les resistances sont posees dans la foulee : `init_resistance` valant
    // zero sur un acteur non initialise, la division du calcul ci-dessus
    // n'aurait aucun sens.
    //
    // RESERVE, dite franchement : cette garde couvre les degats (balles, coups)
    // mais pas `C_player::Explode`, qui appelle `SetResistance` par un autre
    // chemin. Une grenade tombant sur un soldat cree peut donc encore arreter
    // le jeu. Le remede definitif est l'initialisation de l'acteur, qui lui
    // donnera un vrai `menu_id`; en attendant, la ligne SUPPRIMER de la fenetre
    // remet tout d'aplomb.


    // V114 - placement de TOUS les soldats en un seul passage.
    //
    // La V110 appelait la routine de teleportation une fois par soldat. Chaque
    // appel pose un trampoline, declenche, attend jusqu'a une seconde, puis
    // restaure. Avec douze soldats ou plus la sequence s'etire et certains
    // appels n'aboutissent pas : le soldat concerne reste ou le moteur l'avait
    // mis, c'est-a-dire au loin. D'ou « certains a cote et les autres loin ».
    //
    // Un seul passage desormais : soit ils sont tous places, soit aucun ne
    // l'est, et le journal le dit.
    std::vector<std::uintptr_t> frames;
    std::vector<Vector3> frame_positions;
    frames.reserve(created.size());
    frame_positions.reserve(created.size());
    for (std::size_t index = 0; index < created.size(); ++index)
    {
        std::uintptr_t frame = 0;
        if (!process.ReadMemory(created[index] + kActorFrameOffset, frame) ||
            !IsSanePointer(frame))
        {
            LogDiagnostic(
                "Creation d'acteurs: soldat %u (acteur %08X) sans frame "
                "lisible, il restera ou le moteur l'a mis.",
                static_cast<unsigned>(index + 1),
                static_cast<unsigned>(created[index]));
            continue;
        }
        frames.push_back(frame);
        frame_positions.push_back(
            destinations[(std::min)(index, destinations.size() - 1)]);
    }
    const bool placed_all =
        SetFramePositionsOnMainThread(process, frames, frame_positions);
    LogDiagnostic(
        "Creation d'acteurs: placement groupe de %u soldat(s) resultat=%u.",
        static_cast<unsigned>(frames.size()), placed_all ? 1U : 0U);

    LogDiagnostic(
        "Creation d'acteurs (type %u): demandes=%u declenche=%u execute=%u "
        "crees=%u (compteur brut %u).",
        static_cast<unsigned>(actor_type), count, triggered ? 1U : 0U,
        completed, static_cast<unsigned>(created.size()),
        static_cast<unsigned>(reported));
    return !created.empty();
}

// =====================================================================
// V112 - supprimer les soldats crees
// =====================================================================
//
// POURQUOI c'est necessaire, et pas un confort.
//
// `C_game_mission::PlayerSwitch(bool forward, int id)` sert aux DEUX facons de
// changer de soldat, mais ne filtre pas pareil selon le chemin :
//
//   for(i=0; i<plrs.size(); i++)
//      if(plrs[i]->IsAlive() || plrs[i]->IsActive() || id != -1)
//         slist.Add(plrs[i], plrs[i]->GetMenuID());
//   slist.Sort();
//
// - Touche « soldat suivant » : `id == -1`. Seuls les acteurs VIVANTS ou ACTIFS
//   entrent dans la liste. Les soldats crees, qui ne sont pas initialises, en
//   sont donc exclus, et le passage au suivant continue de marcher.
//
// - Touches 1 2 3 4 : `id != -1`. La condition `|| id != -1` fait entrer TOUS
//   les acteurs de type joueur, soldats crees compris. La liste est ensuite
//   triee par `GetMenuID()`. Or le constructeur de `C_player` appelle
//   `AddPlayerMenu`, qui rend -1 des que les quatre places du bandeau sont
//   prises : les soldats crees en surnombre portent donc -1 et se retrouvent
//   EN TETE du tri. La touche 1 ne designe plus le premier soldat du joueur.
//   Et le moteur enchaine :
//
//      if(!slist[id]->IsAlive() || slist[id]->IsActive()) return -1;
//
//   `C_human::IsAlive()` vaut `stay_mode != SM_DEAD`; un soldat cree n'est pas
//   vivant, la fonction rend -1 et il ne se passe RIEN.
//
// D'ou l'asymetrie exacte rapportee par le joueur : le suivant marche, les
// touches directes ne marchent plus, y compris pour ses soldats officiels.
//
// Tant que l'initialisation n'est pas rejouee, le seul remede est de RETIRER
// les soldats crees. C'est ce que fait cette fonction, par le chemin du moteur
// lui-meme : `C_game_mission::DestroyActor`.
constexpr std::array<std::uint8_t, 16> kDestroyActorPrologue{
    0x83, 0xEC, 0x18, 0x55, 0x8B, 0xE9, 0x57, 0x89,
    0x6C, 0x24, 0x10, 0x8B, 0x45, 0x68, 0x85, 0xC0};

struct DestroyActorSearch
{
    DWORD process_id = 0;
    bool searched = false;
    ULONGLONG next_retry = 0;
    std::uintptr_t address = 0;
    unsigned candidates = 0;
};
DestroyActorSearch g_destroy_actor{};

std::uintptr_t FindDestroyActor(TrainerProcess& process)
{
    if (!process.IsConnected())
        return 0;
    const ULONGLONG now = GetTickCount64();
    if (g_destroy_actor.searched &&
        g_destroy_actor.process_id == process.ProcessId() &&
        (g_destroy_actor.address != 0 || now < g_destroy_actor.next_retry))
    {
        return g_destroy_actor.address;
    }
    const DWORD searched_process = process.ProcessId();
    g_destroy_actor = {};
    g_destroy_actor.process_id = searched_process;
    g_destroy_actor.searched = true;
    g_destroy_actor.next_retry = now + kSignatureRetryDelayMs;

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) || module.image_size == 0 ||
        module.image_size > 64U * 1024U * 1024U)
    {
        return 0;
    }
    std::vector<std::uint8_t> image(module.image_size);
    if (!process.ReadMemory(module.base_address, image.data(), image.size()))
    {
        const std::size_t chunk = 0x10000;
        std::fill(image.begin(), image.end(), std::uint8_t{0});
        for (std::size_t offset = 0; offset < image.size(); offset += chunk)
        {
            const std::size_t length =
                (std::min)(chunk, image.size() - offset);
            (void)process.ReadMemory(
                module.base_address + offset, image.data() + offset, length);
        }
    }
    std::uintptr_t found = 0;
    for (std::size_t offset = 0;
         offset + kDestroyActorPrologue.size() <= image.size(); ++offset)
    {
        if (std::memcmp(
                image.data() + offset, kDestroyActorPrologue.data(),
                kDestroyActorPrologue.size()) != 0)
        {
            continue;
        }
        ++g_destroy_actor.candidates;
        found = module.base_address + offset;
    }
    if (g_destroy_actor.candidates == 1)
    {
        g_destroy_actor.address = found;
        LogDiagnostic(
            "DestroyActor: TROUVEE a %08X (une seule candidate).",
            static_cast<unsigned>(found));
    }
    else
    {
        LogDiagnostic(
            "DestroyActor: INTROUVABLE (%u candidates). La suppression des "
            "soldats crees est refusee.",
            g_destroy_actor.candidates);
    }
    return g_destroy_actor.address;
}

// Retire du jeu, sur son propre thread, chacun des acteurs indiques.
bool DestroyActorsOnGameThread(
    TrainerProcess& process,
    std::uintptr_t mission,
    std::uintptr_t destroy_actor,
    const std::vector<std::uintptr_t>& actors)
{
    if (!IsSanePointer(mission) || !IsSanePointer(destroy_actor) ||
        actors.empty())
    {
        return false;
    }
    constexpr std::uintptr_t kProcessCheatHookRva = 0x0009'FC10;
    constexpr std::array<std::uint8_t, 5> kProcessCheatExpected{
        0xB8, 0x5C, 0x10, 0x00, 0x00};
    constexpr std::size_t kRemoteSize = 0x1000;
    constexpr std::size_t kCountOffset = 0x200;
    constexpr std::size_t kMissionOffset = 0x204;
    constexpr std::size_t kDestroyOffset = 0x208;
    constexpr std::size_t kCompletionOffset = 0x20C;
    constexpr std::size_t kActorsOffset = 0x300;

    const std::uint32_t count = static_cast<std::uint32_t>(
        (std::min)(actors.size(), std::size_t{kMaximumSpawnedSoldiers}));

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        kProcessCheatHookRva >= module.image_size)
    {
        return false;
    }
    const std::uintptr_t hook = module.base_address + kProcessCheatHookRva;
    std::array<std::uint8_t, 5> original{};
    if (!process.ReadMemory(hook, original.data(), original.size()) ||
        original != kProcessCheatExpected)
    {
        return false;
    }
    const std::uintptr_t remote = process.AllocateRemoteMemory(kRemoteSize);
    if (!IsSanePointer(remote))
        return false;

    std::vector<std::uint8_t> code;
    const auto byte = [&](std::uint8_t value) { code.push_back(value); };
    const auto dword = [&](std::uint32_t value)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(value));
        std::memcpy(code.data() + offset, &value, sizeof(value));
    };
    const auto slot = [&](std::size_t offset)
    {
        dword(static_cast<std::uint32_t>(remote + offset));
    };
    std::vector<std::size_t> to_end;

    byte(0x9C);                                   // pushfd
    byte(0x60);                                   // pushad
    // Meme garde de reentree que la creation : le site de cheat est traverse a
    // chaque image, un seul passage doit agir.
    byte(0x83); byte(0x3D); slot(kCompletionOffset); byte(0x00);
    byte(0x0F); byte(0x85);
    to_end.push_back(code.size()); dword(0);      // jne fin
    byte(0x33); byte(0xF6);                       // xor esi,esi
    const std::size_t loop_start = code.size();
    byte(0xA1); slot(kCountOffset);               // mov eax,[count]
    byte(0x3B); byte(0xF0);                       // cmp esi,eax
    byte(0x0F); byte(0x83);
    to_end.push_back(code.size()); dword(0);      // jae fin
    byte(0x8B); byte(0x04); byte(0xB5);
    slot(kActorsOffset);                          // mov eax,[esi*4+actors]
    byte(0x50);                                   // push acteur
    byte(0x8B); byte(0x0D); slot(kMissionOffset); // mov ecx,mission
    byte(0xFF); byte(0x15); slot(kDestroyOffset); // call [DestroyActor]
    byte(0x46);                                   // inc esi
    byte(0xE9);
    const std::size_t repeat = code.size();
    dword(0);
    const std::size_t finish = code.size();
    for (const std::size_t displacement : to_end)
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            finish - (displacement + sizeof(std::int32_t)));
        std::memcpy(code.data() + displacement, &relative, sizeof(relative));
    }
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            loop_start - (repeat + sizeof(std::int32_t)));
        std::memcpy(code.data() + repeat, &relative, sizeof(relative));
    }
    byte(0xC7); byte(0x05); slot(kCompletionOffset); dword(1);
    byte(0x61);                                   // popad
    byte(0x9D);                                   // popfd
    code.insert(code.end(), original.begin(), original.end());
    byte(0xE9);
    dword(static_cast<std::uint32_t>(
        (hook + original.size()) -
        (remote + code.size() + sizeof(std::int32_t))));

    const std::uint32_t zero = 0;
    bool written = code.size() < kCountOffset &&
        process.WriteMemory(remote, code.data(), code.size()) &&
        process.WriteMemory(remote + kCountOffset, count) &&
        process.WriteMemory(
            remote + kMissionOffset, static_cast<std::uint32_t>(mission)) &&
        process.WriteMemory(
            remote + kDestroyOffset,
            static_cast<std::uint32_t>(destroy_actor)) &&
        process.WriteMemory(remote + kCompletionOffset, zero);
    for (std::uint32_t index = 0; written && index < count; ++index)
    {
        written = process.WriteMemory(
            remote + kActorsOffset + index * sizeof(std::uint32_t),
            static_cast<std::uint32_t>(actors[index]));
    }
    if (!written)
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }
    std::array<std::uint8_t, 5> patch{0xE9, 0, 0, 0, 0};
    const std::int32_t relative =
        static_cast<std::int32_t>(remote - (hook + patch.size()));
    std::memcpy(patch.data() + 1, &relative, sizeof(relative));
    if (!InstallHookSafely(
            process, hook, patch.data(), patch.size(),
            "le saut vers le code pose"))
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }
    const bool triggered = SendInventoryMainThreadTrigger(process);
    std::uint32_t completed = 0;
    if (triggered)
    {
        for (int attempt = 0; attempt < 500 && completed == 0; ++attempt)
        {
            (void)process.ReadMemory(remote + kCompletionOffset, completed);
            if (completed == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    (void)RestoreHookSafely(
        process, hook, original.data(), original.size());
    QueueRemotePageRelease(process, remote, kRemoteSize);
    LogDiagnostic(
        "Suppression de soldats: demandes=%u declenche=%u execute=%u.",
        count, triggered ? 1U : 0U, completed);
    return completed != 0;
}

// =====================================================================
// V115 - `menu_id` mesure PAR LE CODE, et non plus par sondage
// =====================================================================
//
// La V114 cherchait `menu_id` en comparant les valeurs portees par les
// soldats du joueur. Son journal a montre la limite de la methode :
//
//   Creation d'acteurs: 5 soldat(s) sur 5 regles - menu_id non mesurable,
//   protection maintenue (immortels, mais le jeu ne s'arrete pas).
//
// Il fallait au moins trois soldats vivants dans la liste, condition qui
// n'etait pas remplie. Le repli s'est donc declenche : `no_hit_cheat` pose,
// donc soldats immortels, `menu_id` laisse a -1, donc touches 1 2 3 4 volees
// et plantage a la grenade. Un seul defaut, trois symptomes.
//
// La methode employee ici ne depend plus de l'etat de la partie. Le
// constructeur de `C_player` contient exactement ceci :
//
//   menu_id = mission.game_menu.AddPlayerMenu(1);
//
// ce qui se compile en un `call AddPlayerMenu` suivi du rangement du resultat
// dans le champ. Dans le binaire de reference, les DEUX sites qui appellent
// cette fonction sont suivis du meme `mov [esi+0x29C], eax`. Il suffit donc
// de retrouver `AddPlayerMenu` chez le joueur, de reperer ses appelants, et
// de LIRE le deplacement de l'instruction qui range le resultat. C'est une
// mesure faite sur son binaire, pas une transposition.
constexpr std::array<std::uint8_t, 18> kAddPlayerMenuPrologue{
    0x51, 0x57, 0x8B, 0xF9, 0x83, 0x7F, 0x18, 0x04, 0x75,
    0x08, 0x83, 0xC8, 0xFF, 0x5F, 0x59, 0xC2, 0x04, 0x00};
// Les deux octets qui portent le rang de `num_players` peuvent avoir bouge
// d'une version a l'autre; on ne les compare pas.
constexpr std::size_t kAddPlayerMenuWildcardFirst = 6;
constexpr std::size_t kAddPlayerMenuWildcardLast = 7;

struct MenuIdSearch
{
    DWORD process_id = 0;
    std::uintptr_t offset = 0;
    bool searched = false;
    ULONGLONG next_retry = 0;
};
MenuIdSearch g_menu_id{};

std::uintptr_t MeasurePlayerMenuIdOffset(TrainerProcess& process)
{
    if (!process.IsConnected())
        return 0;
    const ULONGLONG now = GetTickCount64();
    if (g_menu_id.searched && g_menu_id.process_id == process.ProcessId() &&
        (g_menu_id.offset != 0 || now < g_menu_id.next_retry))
    {
        return g_menu_id.offset;
    }
    const DWORD searched_process = process.ProcessId();
    g_menu_id = {};
    g_menu_id.process_id = searched_process;
    g_menu_id.searched = true;
    g_menu_id.next_retry = now + kSignatureRetryDelayMs;

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) || module.image_size == 0 ||
        module.image_size > 64U * 1024U * 1024U)
    {
        return 0;
    }
    std::vector<std::uint8_t> image(module.image_size);
    if (!process.ReadMemory(module.base_address, image.data(), image.size()))
    {
        const std::size_t chunk = 0x10000;
        std::fill(image.begin(), image.end(), std::uint8_t{0});
        for (std::size_t offset = 0; offset < image.size(); offset += chunk)
        {
            const std::size_t length =
                (std::min)(chunk, image.size() - offset);
            (void)process.ReadMemory(
                module.base_address + offset, image.data() + offset, length);
        }
    }

    // 1. Retrouver `AddPlayerMenu`.
    std::uintptr_t add_player_menu = 0;
    unsigned function_candidates = 0;
    for (std::size_t offset = 0;
         offset + kAddPlayerMenuPrologue.size() <= image.size(); ++offset)
    {
        bool match = true;
        for (std::size_t index = 0;
             match && index < kAddPlayerMenuPrologue.size(); ++index)
        {
            if (index >= kAddPlayerMenuWildcardFirst &&
                index <= kAddPlayerMenuWildcardLast)
            {
                continue;
            }
            match = image[offset + index] == kAddPlayerMenuPrologue[index];
        }
        if (!match)
            continue;
        ++function_candidates;
        add_player_menu = module.base_address + offset;
    }
    if (function_candidates != 1)
    {
        LogDiagnostic(
            "menu_id: AddPlayerMenu INTROUVABLE (%u candidates). Mesure "
            "impossible.",
            function_candidates);
        return 0;
    }

    // 2. Reperer ses appelants, et lire le rangement du resultat.
    //    `mov [reg+deplacement], eax` s'ecrit 89 4x dd (octet) ou 89 8x dddd.
    std::map<std::uintptr_t, unsigned> tally;
    unsigned call_sites = 0;
    for (std::size_t offset = 0; offset + 5 <= image.size(); ++offset)
    {
        if (image[offset] != 0xE8)
            continue;
        std::int32_t relative = 0;
        std::memcpy(&relative, image.data() + offset + 1, sizeof(relative));
        const std::uintptr_t target =
            module.base_address + offset + 5 + relative;
        if (target != add_player_menu)
            continue;
        ++call_sites;
        const std::size_t window_end =
            (std::min)(image.size(), offset + 5 + 24);
        for (std::size_t scan = offset + 5; scan + 6 <= window_end; ++scan)
        {
            if (image[scan] != 0x89)
                continue;
            const std::uint8_t modrm = image[scan + 1];
            if (modrm >= 0x80 && modrm <= 0x87 && modrm != 0x84)
            {
                std::int32_t displacement = 0;
                std::memcpy(
                    &displacement, image.data() + scan + 2,
                    sizeof(displacement));
                if (displacement > 0 && displacement < 0x1000)
                    ++tally[static_cast<std::uintptr_t>(displacement)];
                break;
            }
            if (modrm >= 0x40 && modrm <= 0x47 && modrm != 0x44)
            {
                const std::uintptr_t displacement = image[scan + 2];
                if (displacement != 0)
                    ++tally[displacement];
                break;
            }
        }
    }

    std::uintptr_t best = 0;
    unsigned best_count = 0;
    unsigned ties = 0;
    for (const auto& entry : tally)
    {
        if (entry.second > best_count)
        {
            best = entry.first;
            best_count = entry.second;
            ties = 1;
        }
        else if (entry.second == best_count)
        {
            ++ties;
        }
    }
    if (best_count != 0 && ties == 1)
    {
        g_menu_id.offset = best;
        LogDiagnostic(
            "menu_id: MESURE a +0x%03X (AddPlayerMenu=%08X, %u appelants, "
            "%u d'accord). Reference 2002 : +0x29C.",
            static_cast<unsigned>(best),
            static_cast<unsigned>(add_player_menu), call_sites, best_count);
    }
    else
    {
        LogDiagnostic(
            "menu_id: NON MESURABLE (AddPlayerMenu=%08X, %u appelants, %u "
            "deplacements a egalite). Les soldats crees resteront proteges.",
            static_cast<unsigned>(add_player_menu), call_sites, ties);
    }
    return g_menu_id.offset;
}

// =====================================================================
// V122 - UNE CASE DE BANDEAU LIBRE, PLUTOT QUE CELLE D'UN VRAI SOLDAT
// =====================================================================
//
// Le joueur a signale que la mort d'un soldat cree faisait apparaitre SON
// soldat d'origine numero 2 en squelette. Il a raison, et c'est la
// consequence directe du choix de la V114 : je donnais aux soldats crees le
// `menu_id` d'un vrai soldat, pour rester dans les bornes du tableau
// `pmenu[]`. Quand un soldat cree meurt, le moteur fait
// `SetDeathFace(menu_id)` - et marque donc la case d'un soldat bien vivant.
//
// Le desassemblage de `AddPlayerMenu` donne la vraie solution :
//
//   83 7F 18 04     cmp dword [edi+0x18], 4      ; num_players
//   75 08           jne suite
//   83 C8 FF        or eax,-1                    ; return -1
//   ...
//   8D 47 08        lea eax,[edi+0x08]           ; pmenu[]
//   ...             boucle : cherche une case NULLE
//
// et les deux fonctions dangereuses commencent toutes deux par
//
//   if(!pmenu[plr_id]) return;
//
// Une case NULLE est donc parfaitement inoffensive : le moteur y touche sans
// effet. C'est exactement ce qu'il faut pour un soldat cree - il reste dans
// les bornes, il ne plante pas, et il ne marque le portrait de personne.
//
// Les deux emplacements sont MESURES sur le binaire du joueur, lus dans les
// instructions elles-memes, et le pointeur du bandeau se lit sur n'importe
// quel soldat : `C_inventory::game_menu` est le premier membre de la partie
// inventaire, donc a `acteur + base` - la meme base que celle employee pour
// donner une arme.
struct GameMenuLayout
{
    DWORD process_id = 0;
    bool searched = false;
    ULONGLONG next_retry = 0;
    std::uintptr_t pmenu_offset = 0;
    std::uintptr_t player_count_offset = 0;
};
GameMenuLayout g_game_menu{};

bool MeasureGameMenuLayout(TrainerProcess& process)
{
    if (!process.IsConnected())
        return g_game_menu.pmenu_offset != 0;
    const ULONGLONG now = GetTickCount64();
    if (g_game_menu.searched &&
        g_game_menu.process_id == process.ProcessId() &&
        (g_game_menu.pmenu_offset != 0 || now < g_game_menu.next_retry))
    {
        return g_game_menu.pmenu_offset != 0;
    }
    const DWORD searched_process = process.ProcessId();
    g_game_menu = {};
    g_game_menu.process_id = searched_process;
    g_game_menu.searched = true;
    g_game_menu.next_retry = now + kSignatureRetryDelayMs;

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) || module.image_size == 0 ||
        module.image_size > 64U * 1024U * 1024U)
    {
        return false;
    }
    std::vector<std::uint8_t> image(module.image_size);
    if (!process.ReadMemory(module.base_address, image.data(), image.size()))
    {
        const std::size_t chunk = 0x10000;
        std::fill(image.begin(), image.end(), std::uint8_t{0});
        for (std::size_t offset = 0; offset < image.size(); offset += chunk)
        {
            const std::size_t length =
                (std::min)(chunk, image.size() - offset);
            (void)process.ReadMemory(
                module.base_address + offset, image.data() + offset, length);
        }
    }
    // `AddPlayerMenu`, reconnue comme pour la mesure de `menu_id`.
    std::uintptr_t function = 0;
    unsigned candidates = 0;
    for (std::size_t offset = 0;
         offset + kAddPlayerMenuPrologue.size() <= image.size(); ++offset)
    {
        bool match = true;
        for (std::size_t index = 0;
             match && index < kAddPlayerMenuPrologue.size(); ++index)
        {
            if (index >= kAddPlayerMenuWildcardFirst &&
                index <= kAddPlayerMenuWildcardLast)
            {
                continue;
            }
            match = image[offset + index] == kAddPlayerMenuPrologue[index];
        }
        if (!match)
            continue;
        ++candidates;
        function = module.base_address + offset;
    }
    if (candidates != 1)
        return false;

    const std::size_t body = static_cast<std::size_t>(
        function - module.base_address);
    // `cmp dword [edi+deplacement], 4` : le nombre de joueurs.
    if (image[body + 4] == 0x83 && image[body + 5] == 0x7F &&
        image[body + 7] == 0x04)
    {
        g_game_menu.player_count_offset = image[body + 6];
    }
    // `lea eax,[edi+deplacement]` : le tableau des cases de bandeau.
    for (std::size_t scan = body; scan + 3 <= body + 48; ++scan)
    {
        if (image[scan] == 0x8D && image[scan + 1] == 0x47)
        {
            g_game_menu.pmenu_offset = image[scan + 2];
            break;
        }
    }
    LogDiagnostic(
        "Bandeau: AddPlayerMenu=%08X, pmenu a +0x%02X, nombre de joueurs a "
        "+0x%02X (reference 2002 : +0x08 et +0x18).",
        static_cast<unsigned>(function),
        static_cast<unsigned>(g_game_menu.pmenu_offset),
        static_cast<unsigned>(g_game_menu.player_count_offset));
    return g_game_menu.pmenu_offset != 0;
}

// =====================================================================
// V126 - LA GARDE SUR LE BANDEAU : LA SOLUTION, PAS LE COMPROMIS
// =====================================================================
//
// Tout ce qui restait - perte des touches 1 2 3 4 en mission a quatre
// soldats, soldats crees invulnerables, squelette sur un vrai portrait,
// plantage a la grenade - vient d'une seule et meme chose : les deux
// fonctions du bandeau lisent `pmenu[identifiant]` SANS VERIFIER LES BORNES.
//
// Le desassemblage du binaire de reference est sans ambiguite :
//
//   SetHealth     8B 44 24 04       mov eax,[esp+4]        ; l'identifiant
//                 83 EC 2C 53 56
//                 8B 74 81 08       mov esi,[ecx+eax*4+8]  ; pmenu[id]
//                 33 DB 3B F3 0F 84 ...                    ; si nul, on sort
//
//   SetDeathFace  83 EC 10 56 57
//                 8B 7C 24 1C       mov edi,[esp+0x1C]     ; l'identifiant
//                 8B F1
//                 8B 44 BE 08       mov eax,[esi+edi*4+8]  ; pmenu[id]
//                 85 C0 0F 84 ...                          ; si nul, on sort
//
// Les deux savent deja ne rien faire quand la case est NULLE. Il leur manque
// seulement de ne rien faire quand l'indice est HORS DU TABLEAU.
//
// On pose donc cette verification a l'entree des deux fonctions. Des lors, un
// identifiant hors de portee devient parfaitement inoffensif - et cela change
// tout :
//
//   - les soldats crees recoivent un identifiant TRES GRAND, qui les range en
//     DERNIER dans la liste triee par `PlayerSwitch`. Les touches 1 2 3 4
//     designent de nouveau les soldats du joueur, meme en mission a QUATRE;
//   - ils peuvent encaisser et MOURIR, dans toutes les missions, puisque plus
//     aucune lecture hors tableau n'a lieu;
//   - aucun portrait du joueur n'est marque, ni en sante ni en squelette;
//   - une grenade qui les tue passe par le meme chemin, donc protegee elle
//     aussi.
//
// C'est la fin du compromis « quatre soldats = soldats invulnerables ».
// V140 - LES PROLOGUES SONT RALLONGES, ET POUR UNE RAISON PRECISE.
//
// Le journal du joueur a montre les cinq fonctions du bandeau « INTROUVABLE
// (0 candidates) », alors que la mesure de `menu_id` trouvait `AddPlayerMenu`
// trois millisecondes plus tot, dans la meme zone. La lecture de la memoire
// vive du jeu a donne la raison, sans ambiguite :
//
//   SetDeathFace    e9 db e8 f3 04 | 8b 7c 24 1c 8b f1 8b 44 be 08
//   SetHealth       e9 9b e6 f3 04 | 90 90 53 56 8b 74 81 08 33 db
//   SetPrgKeyColor  e9 ab e5 f3 04 | 90 90 90 50 8b 44 91 08
//   GetPrgKeyColor  e9 fb e5 f3 04 | 90 90 90 8b 44 24 08
//   AddPlayerMenu   51 57 8b f9 ...                        (intacte)
//
// LA GARDE ETAIT DEJA POSEE. Une execution precedente du trainer l'avait
// installee dans un jeu qui, lui, n'avait pas ete relance. Les cinq premiers
// octets etaient donc remplaces par un saut, et la recherche du prologue
// d'origine ne pouvait plus rien reconnaitre. `AddPlayerMenu`, jamais
// detournee, restait intacte : d'ou la contradiction apparente.
//
// Le trainer concluait « introuvable », refusait la garde, et rendait les
// soldats crees INVULNERABLES par precaution - defaisant une capacite que le
// joueur avait validee.
//
// La correction tient en deux points :
//
//   1. on cherche desormais la QUEUE du prologue, celle que le saut ne
//      recouvre jamais, et on remonte de `stolen` octets pour retrouver le
//      debut de la fonction;
//   2. si ce debut est un `E9`, la garde est deja en place et on ne la pose
//      pas une seconde fois - ce qui detruirait le trampoline existant.
//
// Les prologues sont rallonges ci-dessous pour que la queue reste assez
// longue - au moins dix octets - une fois les octets repris retires. Les
// octets ajoutes sont lus dans l'executable du joueur, et aucun ne contient
// d'adresse ni de saut relatif : ils ne bougent pas d'un lancement a l'autre.
constexpr std::array<std::uint8_t, 17> kSetHealthPrologue{
    0x8B, 0x44, 0x24, 0x04, 0x83, 0xEC, 0x2C, 0x53,
    0x56, 0x8B, 0x74, 0x81, 0x08, 0x33, 0xDB, 0x3B,
    0xF3};
constexpr std::array<std::uint8_t, 15> kSetDeathFacePrologue{
    0x83, 0xEC, 0x10, 0x56, 0x57, 0x8B, 0x7C, 0x24,
    0x1C, 0x8B, 0xF1, 0x8B, 0x44, 0xBE, 0x08};
// V128 - trois autres fonctions du bandeau indexent `pmenu[]` sans borne.
//
// En relisant TOUTES les methodes publiques de `C_game_menu` qui prennent un
// identifiant de joueur, il apparait que deux d'entre elles se gardent
// elles-memes - `DestroyPlayerMenu` et `SetPrgList` commencent par
// `cmp eax,4 / jae` - mais que trois ne le font pas :
//
//   SetPlayerFace   8B 4C BE 08    mov ecx,[esi+edi*4+8]
//   SetPrgKeyColor  8B 44 91 08    mov eax,[ecx+edx*4+8]
//   GetPrgKeyColor  8B 4C 81 08    mov ecx,[ecx+eax*4+8]
//
// `SetPlayerFace` est appelee par `CB_SETFACE`, que `TableUpdate` declenche;
// les deux autres par l'affichage des ordres. Avec un identifiant hors du
// tableau elles lisent hors de `pmenu[]` exactement comme `SetHealth` le
// faisait. La garde doit donc les couvrir aussi.
// V140 - `SetPlayerFace` : trouvee, apres onze versions d'echec.
//
// Elle etait « non identifiee » depuis la V129, et ma recherche par forme ne
// pouvait pas aboutir. La comparaison des deux binaires dit pourquoi :
//
//   2002    83 ec 10    56 57  8b 7c 24 1c  8b f1  8b 4c be 08  81 c1 fc ...
//   Deluxe  83 ec 10 55 56 57  8b 7c 24 20  8b f1  8b 4c be 08  81 c1 fc ...
//                        ^^                    ^^
//                 un registre empile      argument decale d'autant
//
// Deluxe empile `ebp` en plus, ce qui decale l'argument de 0x1C a 0x20. Ma
// forme exigeait `56 57` immediatement apres `83 EC 10`, et ne pouvait donc
// jamais correspondre.
//
// A noter, parce que cela a failli m'egarer une fois de plus : `SetHealth` et
// `SetDeathFace` sont toutes deux a exactement 0x99C0 de leur adresse de 2002,
// et ce decalage constant designe 00461210 pour `SetPlayerFace` - ou l'on
// trouve du code sans rapport. La transposition echoue encore. L'adresse
// retenue, 00461290, vient de la LECTURE de la partie distinctive de la
// fonction, `8B 4C BE 08 81 C1 FC 00 00 00`, qui n'apparait qu'une fois dans
// tout l'executable.
constexpr std::array<std::uint8_t, 18> kSetPlayerFacePrologue{
    0x83, 0xEC, 0x10, 0x55, 0x56, 0x57, 0x8B, 0x7C,
    0x24, 0x20, 0x8B, 0xF1, 0x8B, 0x4C, 0xBE, 0x08,
    0x81, 0xC1};
constexpr std::array<std::uint8_t, 19> kSetPrgKeyColorPrologue{
    0x8B, 0x44, 0x24, 0x0C, 0x8B, 0x54, 0x24, 0x04,
    0x50, 0x8B, 0x44, 0x91, 0x08, 0x8B, 0x54, 0x24,
    0x0C, 0x8B, 0x88};
constexpr std::array<std::uint8_t, 20> kGetPrgKeyColorPrologue{
    0x8B, 0x44, 0x24, 0x04, 0x8B, 0x4C, 0x81, 0x08,
    0x8B, 0x44, 0x24, 0x08, 0x8B, 0x91, 0xBC, 0x00,
    0x00, 0x00, 0x8B, 0x0C};

// Identifiant donne aux soldats crees : hors du tableau, donc sans effet une
// fois la garde posee, et assez grand pour les ranger en fin de tri.
constexpr std::int32_t kCreatedSoldierMenuId = 1000;
constexpr std::uint32_t kMenuSlotCount = 4;

// Octets repris par chaque detour. Ils doivent correspondre exactement a
// `GuardPlan::stolen` plus bas : c'est la longueur que le saut recouvre, donc
// celle qu'il faut ignorer en tete de prologue pour reconnaitre une fonction
// deja gardee.
constexpr std::size_t kSetHealthStolen = 7;
constexpr std::size_t kSetDeathFaceStolen = 5;
constexpr std::size_t kSetPlayerFaceStolen = 5;
constexpr std::size_t kSetPrgKeyColorStolen = 8;
constexpr std::size_t kGetPrgKeyColorStolen = 8;

struct MenuGuardState
{
    DWORD process_id = 0;
    bool installed = false;
    bool refused = false;
    ULONGLONG next_retry = 0;
    std::uintptr_t set_health = 0;
    std::uintptr_t set_death_face = 0;
    std::uintptr_t set_player_face = 0;
    std::uintptr_t set_prg_key_color = 0;
    std::uintptr_t get_prg_key_color = 0;
    std::uintptr_t page = 0;
};
MenuGuardState g_menu_guard{};

// Retrouve une fonction par la QUEUE de son prologue, en exigeant une
// candidate unique - et en acceptant qu'elle soit DEJA detournee.
//
// On ignore les `stolen` premiers octets, qui sont precisement ceux qu'un
// detour recouvre. La queue, elle, n'est jamais touchee. Une fois la queue
// trouvee, le debut de la fonction se deduit par soustraction, et on verifie
// qu'il presente l'une des deux formes attendues :
//
//   - les octets d'origine   -> fonction intacte, la garde est a poser;
//   - un `E9` en tete        -> garde deja posee par une execution
//                               precedente du trainer, sur un jeu qui n'a pas
//                               ete relance. On n'y touche pas.
//
// Chaque queue employee ici a ete verifiee UNIQUE dans l'executable du
// joueur; l'exigence de candidate unique est donc conservee telle quelle.
std::uintptr_t FindUniqueByPrologue(
    const std::vector<std::uint8_t>& image,
    std::uintptr_t base,
    const std::uint8_t* pattern,
    std::size_t length,
    std::size_t stolen,
    const char* name,
    bool* already_guarded)
{
    if (already_guarded != nullptr)
        *already_guarded = false;
    if (stolen >= length || length - stolen < 8)
        return 0;   // queue trop courte pour etre distinctive
    const std::uint8_t* tail = pattern + stolen;
    const std::size_t tail_length = length - stolen;

    std::uintptr_t found = 0;
    bool found_guarded = false;
    unsigned candidates = 0;
    for (std::size_t offset = stolen;
         offset + tail_length <= image.size(); ++offset)
    {
        if (std::memcmp(image.data() + offset, tail, tail_length) != 0)
            continue;
        const std::size_t head = offset - stolen;
        const bool intact =
            std::memcmp(image.data() + head, pattern, stolen) == 0;
        const bool detoured = image[head] == 0xE9;
        if (!intact && !detoured)
            continue;
        ++candidates;
        found = base + head;
        found_guarded = detoured;
    }
    if (candidates != 1)
    {
        LogDiagnostic(
            "Garde bandeau: %s INTROUVABLE (%u candidates, recherche par la "
            "queue du prologue sur %u octets).",
            name, candidates, static_cast<unsigned>(tail_length));
        return 0;
    }
    if (already_guarded != nullptr)
        *already_guarded = found_guarded;
    LogDiagnostic(
        "Garde bandeau: %s a %08X%s.", name, static_cast<unsigned>(found),
        found_guarded ? " - DEJA gardee par une execution precedente du "
                        "trainer, on n'y touche pas"
                      : "");
    return found;
}

// Pose la garde a l'entree des deux fonctions. Une seule fois par partie.
bool InstallMenuBoundsGuard(TrainerProcess& process)
{
    if (!process.IsConnected())
        return false;
    if (g_menu_guard.installed &&
        g_menu_guard.process_id == process.ProcessId())
    {
        return true;
    }
    const ULONGLONG now = GetTickCount64();
    if (g_menu_guard.refused &&
        g_menu_guard.process_id == process.ProcessId() &&
        now < g_menu_guard.next_retry)
    {
        return false;
    }
    const DWORD searched = process.ProcessId();
    g_menu_guard = {};
    g_menu_guard.process_id = searched;
    g_menu_guard.next_retry = now + kSignatureRetryDelayMs;

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) || module.image_size == 0 ||
        module.image_size > 64U * 1024U * 1024U)
    {
        g_menu_guard.refused = true;
        return false;
    }
    std::vector<std::uint8_t> image(module.image_size);
    if (!process.ReadMemory(module.base_address, image.data(), image.size()))
    {
        const std::size_t chunk = 0x10000;
        std::fill(image.begin(), image.end(), std::uint8_t{0});
        for (std::size_t offset = 0; offset < image.size(); offset += chunk)
        {
            const std::size_t length =
                (std::min)(chunk, image.size() - offset);
            (void)process.ReadMemory(
                module.base_address + offset, image.data() + offset, length);
        }
    }
    bool guarded_set_health = false;
    bool guarded_set_death_face = false;
    bool guarded_set_player_face = false;
    bool guarded_set_prg_key_color = false;
    bool guarded_get_prg_key_color = false;
    g_menu_guard.set_health = FindUniqueByPrologue(
        image, module.base_address, kSetHealthPrologue.data(),
        kSetHealthPrologue.size(), kSetHealthStolen, "SetHealth",
        &guarded_set_health);
    g_menu_guard.set_death_face = FindUniqueByPrologue(
        image, module.base_address, kSetDeathFacePrologue.data(),
        kSetDeathFacePrologue.size(), kSetDeathFaceStolen, "SetDeathFace",
        &guarded_set_death_face);
    // V129 - `SetPlayerFace` cherchee PAR SA FORME, non par transposition.
    //
    // Le journal du joueur a montre l'erreur : `SetPlayerFace INTROUVABLE
    // (0 candidates)`. J'avais recopie son prologue depuis le binaire de
    // reference, exactement la faute que je m'etais promis de ne plus faire.
    //
    // Elle partage sa forme avec `SetDeathFace`, qui a ete trouvee :
    //
    //   83 EC 10 56 57 8B 7C 24 ?? 8B F1 8B ?? BE 08
    //
    // Deux octets seulement changent - le deplacement de l'argument sur la
    // pile, et le registre de destination. On cherche donc cette forme avec
    // ces deux octets libres, et on retient la candidate qui n'est PAS
    // `SetDeathFace`.
    g_menu_guard.set_player_face = FindUniqueByPrologue(
        image, module.base_address, kSetPlayerFacePrologue.data(),
        kSetPlayerFacePrologue.size(), kSetPlayerFaceStolen, "SetPlayerFace",
        &guarded_set_player_face);
    if (g_menu_guard.set_player_face == 0)
    {
        std::vector<std::uintptr_t> shaped;
        const std::array<std::uint8_t, 15> shape{
            0x83, 0xEC, 0x10, 0x56, 0x57, 0x8B, 0x7C, 0x24,
            0x00, 0x8B, 0xF1, 0x8B, 0x00, 0xBE, 0x08};
        // V140 - la meme correction que pour les quatre autres : on cherche
        // la queue, que le detour ne recouvre jamais, puis on remonte des
        // cinq octets repris.
        for (std::size_t offset = kSetPlayerFaceStolen;
             offset + (shape.size() - kSetPlayerFaceStolen) <= image.size();
             ++offset)
        {
            bool match = true;
            for (std::size_t index = kSetPlayerFaceStolen;
                 match && index < shape.size(); ++index)
            {
                if (index == 8 || index == 12)
                    continue;   // octets libres
                match = image[offset + index - kSetPlayerFaceStolen] ==
                        shape[index];
            }
            if (!match)
                continue;
            const std::size_t head = offset - kSetPlayerFaceStolen;
            const bool intact =
                std::memcmp(image.data() + head, shape.data(),
                            kSetPlayerFaceStolen) == 0;
            const bool detoured = image[head] == 0xE9;
            if (!intact && !detoured)
                continue;
            if (detoured)
                guarded_set_player_face = true;
            shaped.push_back(module.base_address + head);
        }
        for (const std::uintptr_t candidate : shaped)
        {
            if (candidate != g_menu_guard.set_death_face)
            {
                if (g_menu_guard.set_player_face != 0)
                {
                    // Plusieurs candidates : on ne choisit pas au hasard.
                    g_menu_guard.set_player_face = 0;
                    break;
                }
                g_menu_guard.set_player_face = candidate;
            }
        }
        LogDiagnostic(
            "Garde bandeau: SetPlayerFace %s par la forme de secours "
            "(%u forme(s) trouvee(s)).",
            g_menu_guard.set_player_face != 0 ? "reconnue" : "non identifiee",
            static_cast<unsigned>(shaped.size()));
    }
    g_menu_guard.set_prg_key_color = FindUniqueByPrologue(
        image, module.base_address, kSetPrgKeyColorPrologue.data(),
        kSetPrgKeyColorPrologue.size(), kSetPrgKeyColorStolen,
        "SetPrgKeyColor", &guarded_set_prg_key_color);
    g_menu_guard.get_prg_key_color = FindUniqueByPrologue(
        image, module.base_address, kGetPrgKeyColorPrologue.data(),
        kGetPrgKeyColorPrologue.size(), kGetPrgKeyColorStolen,
        "GetPrgKeyColor", &guarded_get_prg_key_color);

    // V129 - LA GARDE N'EST PLUS « TOUT OU RIEN ».
    //
    // La V128 exigeait les CINQ fonctions. Une seule manquante - et c'est
    // arrive, `SetPlayerFace` - et AUCUNE garde n'etait posee : le joueur
    // reperdait ses touches 1 2 3 4 et ses soldats redevenaient invulnerables.
    // Une regression que j'ai introduite moi-meme, en liant des choses qui
    // n'avaient pas a l'etre.
    //
    // Chaque fonction est desormais gardee independamment. Et le verdict qui
    // compte - peut-on donner un identifiant hors du tableau ? - ne depend que
    // des DEUX fonctions du chemin des degats : `SetHealth` et `SetDeathFace`.
    // Les trois autres sont un supplement de surete, pas une condition.
    if (g_menu_guard.set_health == 0 || g_menu_guard.set_death_face == 0)
    {
        g_menu_guard.refused = true;
        LogDiagnostic(
            "Garde bandeau: REFUSEE (SetHealth=%08X SetDeathFace=%08X). Ce "
            "sont les deux seules indispensables; sans elles les soldats "
            "crees resteront proteges du tir.",
            static_cast<unsigned>(g_menu_guard.set_health),
            static_cast<unsigned>(g_menu_guard.set_death_face));
        return false;
    }

    // V140 - on n'alloue une page que s'il reste au moins une garde a poser.
    // Quand toutes sont deja en place - jeu non relance entre deux versions du
    // trainer - une allocation de plus serait perdue a chaque lancement.
    const bool needs_install =
        (g_menu_guard.set_health != 0 && !guarded_set_health) ||
        (g_menu_guard.set_death_face != 0 && !guarded_set_death_face) ||
        (g_menu_guard.set_player_face != 0 && !guarded_set_player_face) ||
        (g_menu_guard.set_prg_key_color != 0 && !guarded_set_prg_key_color) ||
        (g_menu_guard.get_prg_key_color != 0 && !guarded_get_prg_key_color);
    std::uintptr_t page = 0;
    if (needs_install)
    {
        page = process.AllocateRemoteMemory(0x1000);
        if (!IsSanePointer(page))
        {
            g_menu_guard.refused = true;
            return false;
        }
    }

    // Chaque garde occupe la moitie de la page. La forme est la meme :
    //
    //   cmp dword [esp + rang de l'argument], 4
    //   jb  suite            ; identifiant valide : on laisse faire
    //   ret <taille args>    ; hors tableau : on ne fait rien
    //   suite: <octets d'origine> ; jmp vers la suite de la fonction
    //
    // Les deux prologues repris sont sans adresse absolue ni saut relatif :
    // ils se deplacent tels quels.
    struct GuardPlan
    {
        std::uintptr_t target;
        std::size_t stolen;      // octets repris, au moins cinq
        std::uint8_t argument;   // deplacement de l'identifiant sur la pile
        std::uint16_t cleanup;   // octets d'arguments a liberer au retour
        std::size_t slot;
        const char* name;
        bool essential;          // sur le chemin des degats
        bool already;            // deja gardee par une execution precedente
    };
    const GuardPlan plans[] = {
        {g_menu_guard.set_health, kSetHealthStolen, 0x04, 8, 0x000,
         "SetHealth", true, guarded_set_health},
        {g_menu_guard.set_death_face, kSetDeathFaceStolen, 0x04, 4, 0x100,
         "SetDeathFace", true, guarded_set_death_face},
        {g_menu_guard.set_player_face, kSetPlayerFaceStolen, 0x04, 8, 0x200,
         "SetPlayerFace", false, guarded_set_player_face},
        {g_menu_guard.set_prg_key_color, kSetPrgKeyColorStolen, 0x04, 12,
         0x280, "SetPrgKeyColor", false, guarded_set_prg_key_color},
        {g_menu_guard.get_prg_key_color, kGetPrgKeyColorStolen, 0x04, 8,
         0x300, "GetPrgKeyColor", false, guarded_get_prg_key_color},
    };

    unsigned posed = 0;
    bool essential = true;
    for (const GuardPlan& plan : plans)
    {
        // Une fonction non identifiee est simplement sautee : elle n'annule
        // plus la pose des autres.
        if (plan.target == 0)
        {
            LogDiagnostic(
                "Garde bandeau: %s non identifiee, elle est sautee "
                "(les autres restent posees).",
                plan.name);
            continue;
        }
        // V140 - une garde deja en place ne se repose pas. Reecrire un detour
        // par-dessus un detour existant recopierait le saut lui-meme dans le
        // nouveau trampoline, et le jeu tournerait en rond a la premiere
        // blessure. On la compte comme posee, ce qu'elle est.
        if (plan.already)
        {
            ++posed;
            LogDiagnostic(
                "Garde bandeau: %s deja protegee (detour en place depuis une "
                "execution precedente du trainer; le jeu n'a pas ete relance "
                "entre-temps). Elle reste active telle quelle.",
                plan.name);
            continue;
        }
        if (page == 0)
        {
            if (plan.essential)
                essential = false;
            continue;
        }
        std::array<std::uint8_t, 16> original{};
        if (!process.ReadMemory(plan.target, original.data(), plan.stolen))
        {
            if (plan.essential)
                essential = false;
            continue;
        }
        std::vector<std::uint8_t> code;
        const auto byte = [&](std::uint8_t v) { code.push_back(v); };
        const auto dword = [&](std::uint32_t v)
        {
            const std::size_t offset = code.size();
            code.resize(offset + sizeof(v));
            std::memcpy(code.data() + offset, &v, sizeof(v));
        };
        // cmp dword [esp+arg], 4
        byte(0x83); byte(0x7C); byte(0x24); byte(plan.argument);
        byte(static_cast<std::uint8_t>(kMenuSlotCount));
        // jb suite  (saute le ret)
        byte(0x72); byte(0x03);
        // ret cleanup
        byte(0xC2);
        byte(static_cast<std::uint8_t>(plan.cleanup & 0xFF));
        byte(static_cast<std::uint8_t>((plan.cleanup >> 8) & 0xFF));
        // suite : octets d'origine, puis retour dans la fonction
        code.insert(
            code.end(), original.begin(), original.begin() + plan.stolen);
        byte(0xE9);
        dword(static_cast<std::uint32_t>(
            (plan.target + plan.stolen) -
            (page + plan.slot + code.size() + sizeof(std::uint32_t))));

        if (!process.WriteMemory(page + plan.slot, code.data(), code.size()))
        {
            if (plan.essential)
                essential = false;
            continue;
        }
        std::array<std::uint8_t, 16> detour{};
        detour[0] = 0xE9;
        const std::int32_t relative = static_cast<std::int32_t>(
            (page + plan.slot) - (plan.target + 5));
        std::memcpy(detour.data() + 1, &relative, sizeof(relative));
        for (std::size_t index = 5; index < plan.stolen; ++index)
            detour[index] = 0x90;   // nop, pour ne pas laisser d'octet orphelin
        if (!process.WriteProtectedMemory(
                plan.target, detour.data(), plan.stolen))
        {
            if (plan.essential)
                essential = false;
            continue;
        }
        ++posed;
        LogDiagnostic(
            "Garde bandeau: %s protegee (trampoline %08X, %u octets repris).",
            plan.name, static_cast<unsigned>(page + plan.slot),
            static_cast<unsigned>(plan.stolen));
    }
    if (!essential)
    {
        g_menu_guard.refused = true;
        LogDiagnostic(
            "Garde bandeau: une des deux gardes indispensables n'a pas pu "
            "etre posee. Les soldats crees resteront proteges du tir.");
        return false;
    }
    g_menu_guard.page = page;
    g_menu_guard.installed = true;
    LogDiagnostic(
        "Garde bandeau: %u fonction(s) sur 5 protegee(s), dont les deux "
        "indispensables.",
        posed);
    LogDiagnostic(
        "Garde bandeau: POSEE. Un identifiant hors du tableau n'a plus aucun "
        "effet. Les soldats crees peuvent donc mourir dans TOUTES les "
        "missions, y compris a quatre soldats, sans marquer vos portraits ni "
        "voler vos touches 1 2 3 4.");
    return true;
}

// =====================================================================
// V128 - LE « UNKNOWN » : TROUVE A LA LIGNE PRES
// =====================================================================
//
// Le nom affiche d'un soldat ne vient ni de sa frame, ni du bandeau. Il vient
// de sa TABLE, et le moteur le dit sans detour (Actors.cpp:12417) :
//
//   color = 0xffc0c0c0;
//   int i = tab->ItemI(TAB_I_HUM_FACE);
//   if(!i)
//      return "unknown";                      // <- litteralement
//   i = GT_GAME_MENU_SOLDIER_NAME + i - 1;
//   return all_txt[i];
//
// Le nom est choisi par le NUMERO DE VISAGE. La table d'un soldat cree est
// ouverte par le constructeur avec le modele par defaut, ou ce numero vaut
// zero - et le moteur renvoie alors le mot « unknown ». Ce n'etait donc ni
// `SetName`, ni le bandeau : les deux etaient corrects depuis le debut.
//
// Il suffit d'ecrire un numero non nul dans la table de l'acteur. Le trainer
// sait deja le faire : `weapon_mods` lit et ecrit les tables du jeu depuis des
// versions pour le recul et la dispersion des armes. On reprend exactement sa
// mecanique - un tableau de descripteurs, un tableau de donnees - appliquee
// cette fois a la table de l'acteur, dont le pointeur se trouve a
// `acteur+0x190` (releve dans le prologue de `TableUpdate`).
//
// Les numeros employes sont ceux des PROPRES soldats du joueur, releves dans
// leurs tables. Ils sont donc forcement valides, et le jeu affichera de vrais
// noms de soldats au lieu de « unknown ».
// V129 - l'emplacement de la table est MESURE, non transpose.
//
// Le journal du joueur a dit : « aucun numero de visage lisible chez vos
// soldats ». J'avais ecrit `acteur+0x190`, releve dans le prologue de
// `TableUpdate` du binaire DE REFERENCE - la meme faute que pour
// `SetPlayerFace`, et la meme que je repete depuis le debut de ce dossier.
//
// L'emplacement est desormais cherche sur le binaire DU JOUEUR, et la
// recherche se verifie elle-meme. Un emplacement n'est retenu que si, POUR
// TOUS ses soldats a la fois :
//
//   - il contient un pointeur sain;
//   - l'objet pointe presente le nombre de proprietes, les descripteurs, les
//     donnees et leur taille aux emplacements que `weapon_mods` emploie et que
//     le jeu valide a chaque tir;
//   - le descripteur de la propriete 65 s'annonce ENTIER;
//   - et la valeur qu'on y lit est un numero de visage plausible, non nul.
//
// Cette derniere condition est la plus importante : elle verifie d'un coup
// l'emplacement de la table, la disposition de l'objet ET l'indice de la
// propriete. Si l'un des trois etait faux, tout cela ne s'alignerait pas sur
// quatre soldats a la fois.
constexpr std::uint32_t kHumanFaceProperty = 65;   // TAB_I_HUM_FACE
constexpr std::int32_t kMaximumFaceNumber = 64;

// V137 - LA MESURE PAR LE GROUPE D'ENNEMI, ENFIN UNE CONTRAINTE SERREE.
//
// Trois tentatives de mesure de la table ont echoue, et la derniere a echoue
// pour une bonne raison : je verifiais le NUMERO DE VISAGE, qui peut
// legitimement valoir zero chez les soldats du joueur en partie reseau.
//
// Le code du moteur en offre une bien meilleure. Les deux fonctions qui
// decident de l'hostilite lisent le meme entier :
//
//   // C_player::IsEnemy(asker)
//   case ACTOR_ENEMY:
//      return (asker->GetTable(0)->ItemI(TAB_I_ENM_GROUP) == 0);
//
//   // C_enemy::IsEnemy(asker)
//   int my_group = tab->ItemI(TAB_I_ENM_GROUP);
//   case ACTOR_PLAYER:  return (my_group == 0);
//   case ACTOR_ENEMY:   ... return (my_group != his_group);
//
// Ce nombre ne peut valoir que 0 (allemand), 1 (russe) ou 2 (civil, ami de
// tous). Exiger cette valeur chez TOUS les ennemis d'une mission est une
// contrainte autrement plus serree qu'un visage non nul - et les ennemis, eux,
// portent forcement un groupe.
constexpr std::uint32_t kEnemyGroupProperty = 74;   // TAB_I_ENM_GROUP
constexpr std::int32_t kEnemyGroupGerman = 0;
constexpr std::int32_t kEnemyGroupRussian = 1;
constexpr std::int32_t kEnemyGroupCivilian = 2;
constexpr std::uintptr_t kTableItemCountOffset = 0x0C;
constexpr std::uintptr_t kTableDescriptorsOffset = 0x20;
constexpr std::uintptr_t kTableDataOffset = 0x24;
constexpr std::uintptr_t kTableDataSizeOffset = 0x28;
constexpr std::size_t kTableDescriptorSize = 8;
constexpr std::uint8_t kTableTypeInteger = 2;

#pragma pack(push, 1)
struct ActorTableDescriptor
{
    std::uint32_t offset = 0;
    std::uint8_t type = 0;
    std::uint8_t max_string_size = 0;
    std::uint16_t array_length = 0;
};
#pragma pack(pop)
static_assert(sizeof(ActorTableDescriptor) == kTableDescriptorSize);

// Adresse d'une propriete entiere dans la table d'un acteur, ou zero.
struct ActorTableSearch
{
    DWORD process_id = 0;
    bool searched = false;
    ULONGLONG next_retry = 0;
    std::uintptr_t offset = 0;
    // V141 - rang de `GetTable` dans la table des methodes, quand il a ete
    // LU. `TableUpdate` se trouve huit octets plus loin, et on ne s'en sert
    // que si ce rang a effectivement ete etabli par lecture.
    std::uintptr_t get_table_rank = 0;
};
ActorTableSearch g_actor_table{};

std::uintptr_t ResolveActorTableInteger(
    TrainerProcess& process,
    std::uintptr_t actor,
    std::uint32_t property,
    std::uintptr_t table_offset)
{
    if (table_offset == 0)
        return 0;
    const std::uintptr_t kActorTableOffset = table_offset;
    // V139 - on fait EXACTEMENT ce que fait `C_table::Item`, ni plus ni moins.
    //
    // Le desassemblage de `itabler2.dll`, la bibliotheque des fiches, donne la
    // fonction en entier :
    //
    //   8B 44 24 04   mov eax,[esp+4]        ; la fiche
    //   8B 48 20      mov ecx,[eax+0x20]     ; les descripteurs
    //   85 C9 / 74    ...                    ; nuls -> echec
    //   8B 50 24      mov edx,[eax+0x24]     ; les donnees
    //   85 D2 / 74    ...                    ; nulles -> echec
    //   8B 74 24 0C   mov esi,[esp+0x0C]     ; le numero demande
    //   8B 78 0C      mov edi,[eax+0x0C]     ; le nombre d'entrees
    //   3B F7 / 73    cmp / jae              ; hors bornes -> echec
    //   8B 04 F1      mov eax,[ecx+esi*8]    ; descripteur, pas de 8 octets
    //   83 F8 FF / 74 cmp eax,-1 / je        ; absent -> echec
    //   03 C2         add eax,edx            ; donnees + decalage
    //
    // LE MOTEUR NE LIT AUCUNE TAILLE DE DONNEES. Or j'exigeais un champ de
    // taille en +0x28, repris de la table des armes, et je rejetais tout
    // candidat dont ce champ ne me plaisait pas. C'est ce qui a fait echouer
    // les quatre mesures : le critere portait sur un champ que la fiche
    // n'emploie pas.
    //
    // On ne verifie donc plus que ce que le moteur verifie lui-meme, et on
    // ajoute seulement le controle de type - utile, mais non bloquant si le
    // descripteur se presente autrement.
    std::uintptr_t table = 0;
    std::uint32_t count = 0;
    std::uintptr_t descriptors = 0;
    std::uintptr_t data = 0;
    if (!IsSanePointer(actor) ||
        !process.ReadMemory(actor + kActorTableOffset, table) ||
        !IsSanePointer(table) ||
        !process.ReadMemory(table + kTableItemCountOffset, count) ||
        !process.ReadMemory(table + kTableDescriptorsOffset, descriptors) ||
        !process.ReadMemory(table + kTableDataOffset, data) ||
        property >= count || count == 0 || count > 4096 ||
        !IsSanePointer(descriptors) || !IsSanePointer(data))
    {
        return 0;
    }
    ActorTableDescriptor descriptor{};
    if (!process.ReadMemory(
            descriptors +
                static_cast<std::uintptr_t>(property) * kTableDescriptorSize,
            descriptor) ||
        descriptor.offset == std::numeric_limits<std::uint32_t>::max() ||
        descriptor.offset > 0x10000U)
    {
        return 0;
    }
    const std::uintptr_t address = data + descriptor.offset;
    return IsSanePointer(address) ? address : 0;
}

// Cherche l'emplacement de la table dans un acteur, et se verifie soi-meme.
std::uintptr_t MeasureActorTableOffset(
    TrainerProcess& process,
    const std::vector<std::uintptr_t>& reference_players,
    const std::vector<std::uintptr_t>& reference_enemies)
{
    if (!process.IsConnected() ||
        (reference_players.empty() && reference_enemies.empty()))
    {
        return g_actor_table.offset;
    }
    const ULONGLONG now = GetTickCount64();
    if (g_actor_table.searched &&
        g_actor_table.process_id == process.ProcessId() &&
        (g_actor_table.offset != 0 || now < g_actor_table.next_retry))
    {
        return g_actor_table.offset;
    }
    g_actor_table.searched = true;
    g_actor_table.process_id = process.ProcessId();
    g_actor_table.next_retry = now + kSignatureRetryDelayMs;
    g_actor_table.offset = 0;

    // =================================================================
    // V138 - L'OFFSET EST ECRIT DANS `GetTable`, IL SUFFIT DE LE LIRE
    // =================================================================
    //
    // Quatre mesures ont echoue : trois par un critere faux, la quatrieme par
    // une forme de code introuvable. Je cherchais compliqué alors que le
    // moteur donne la reponse en clair.
    //
    // `C_human::GetTable(int index)` est une fonction de six instructions :
    //
    //   8B 44 24 04          mov eax,[esp+4]        ; l'index
    //   85 C0                test eax,eax
    //   75 ??                jnz  (index != 0)
    //   8B 81 <deplacement>  mov eax,[ecx+deplacement]   ; return tab
    //
    // Le deplacement EST l'emplacement de la table dans l'acteur. Il n'y a
    // rien a deduire ni a balayer : on lit l'instruction que le moteur emploie
    // lui-meme, dans la vtable du soldat du joueur.
    //
    // `GetTable` occupe le rang 0xB4 ou 0xB8 - l'enumeration des virtuelles de
    // `C_actor` ancree sur `SetFrame = +0x88` donne 0xB4, mais on sait qu'une
    // methode a ete inseree quelque part avant `Explode`. Plutot que de
    // trancher, on essaie les deux et on retient celui dont le CODE presente
    // cette forme. La forme est trop particuliere pour se produire par hasard.
    if (!reference_players.empty())
    {
        static constexpr std::uintptr_t kFirstRank = 0xB0;
        static constexpr std::uintptr_t kLastRank = 0xC0;
        const std::uintptr_t actor = reference_players.front();
        std::uintptr_t vtable = 0;
        if (process.ReadMemory(actor, vtable) && IsSanePointer(vtable))
        {
            for (std::uintptr_t rank = kFirstRank; rank <= kLastRank;
                 rank += 4)
            {
                std::uintptr_t function = 0;
                std::array<std::uint8_t, 14> head{};
                if (!process.ReadMemory(vtable + rank, function) ||
                    !process.IsReadableCodeTarget(function) ||
                    !process.ReadMemory(function, head.data(), head.size()))
                {
                    continue;
                }
                if (head[0] != 0x8B || head[1] != 0x44 || head[2] != 0x24 ||
                    head[3] != 0x04 || head[4] != 0x85 || head[5] != 0xC0 ||
                    head[6] != 0x75 || head[8] != 0x8B || head[9] != 0x81)
                {
                    continue;
                }
                std::uint32_t displacement = 0;
                std::memcpy(&displacement, head.data() + 10,
                            sizeof(displacement));
                if (displacement == 0 || displacement > 0x1000)
                    continue;
                g_actor_table.offset =
                    static_cast<std::uintptr_t>(displacement);
                g_actor_table.get_table_rank = rank;
                LogDiagnostic(
                    "Table d'acteur: LUE DANS `GetTable` (vtable rang +0x%02X, "
                    "fonction %08X) -> la table est a +0x%03X. "
                    "Reference 2002 : +0x190.",
                    static_cast<unsigned>(rank),
                    static_cast<unsigned>(function), displacement);
                return g_actor_table.offset;
            }
        }
        LogDiagnostic(
            "Table d'acteur: `GetTable` non reconnue dans la vtable "
            "(rangs +0xB0 a +0xC0). On retombe sur le balayage.");
    }

    // V130 - on demande d'abord au CODE, avant de balayer.
    //
    // Le balayage de la V129 n'a rien trouve (« 0 emplacement(s) possible(s) »).
    // Plutot que d'elargir les criteres au hasard, on lit l'emplacement la ou
    // le moteur l'ecrit lui-meme. `C_player::TableUpdate` commence par
    //
    //   51 56 8B F1 57 6A ?? 8B 86 <deplacement sur 32 bits>
    //
    // c'est-a-dire `mov eax,[esi+deplacement]` : le pointeur de table. On
    // reconnait cette forme sur le binaire DU JOUEUR - deux octets laisses
    // libres - et on lit le deplacement dans l'instruction.
    std::uintptr_t from_code = 0;
    {
        RemoteModuleInfo module{};
        if (process.GetMainModuleInfo(module) && module.image_size != 0 &&
            module.image_size <= 64U * 1024U * 1024U)
        {
            std::vector<std::uint8_t> image(module.image_size);
            if (!process.ReadMemory(
                    module.base_address, image.data(), image.size()))
            {
                const std::size_t chunk = 0x10000;
                std::fill(image.begin(), image.end(), std::uint8_t{0});
                for (std::size_t at = 0; at < image.size(); at += chunk)
                {
                    const std::size_t length =
                        (std::min)(chunk, image.size() - at);
                    (void)process.ReadMemory(
                        module.base_address + at, image.data() + at, length);
                }
            }
            const std::array<std::uint8_t, 9> shape{
                0x51, 0x56, 0x8B, 0xF1, 0x57, 0x6A, 0x00, 0x8B, 0x86};
            std::map<std::uintptr_t, unsigned> tally;
            for (std::size_t at = 0;
                 at + shape.size() + 4 <= image.size(); ++at)
            {
                bool match = true;
                for (std::size_t index = 0;
                     match && index < shape.size(); ++index)
                {
                    if (index == 6)
                        continue;   // octet libre
                    match = image[at + index] == shape[index];
                }
                if (!match)
                    continue;
                std::int32_t displacement = 0;
                std::memcpy(
                    &displacement, image.data() + at + shape.size(),
                    sizeof(displacement));
                if (displacement > 0 && displacement < 0x1000)
                    ++tally[static_cast<std::uintptr_t>(displacement)];
            }
            unsigned best = 0;
            for (const auto& entry : tally)
            {
                if (entry.second > best)
                {
                    best = entry.second;
                    from_code = entry.first;
                }
            }
            LogDiagnostic(
                "Table d'acteur: le code de TableUpdate designe +0x%03X "
                "(%u forme(s) concordante(s)).",
                static_cast<unsigned>(from_code), best);
        }
    }

    // Le releve du code est verifie exactement comme les autres candidats :
    // s'il ne donne pas un numero de visage plausible chez tous les soldats,
    // il est ecarte comme les autres.
    constexpr std::uintptr_t kFirst = 0x40;
    constexpr std::uintptr_t kLast = 0x600;
    std::uintptr_t found = 0;
    unsigned candidates = 0;
    for (std::uintptr_t offset = kFirst; offset <= kLast; offset += 4)
    {
        // V131 - critere assoupli, et pour une raison precise.
        //
        // La V129 exigeait un numero de visage NON NUL chez tous les soldats.
        // Le journal a rendu « 0 emplacement(s) possible(s) », et le moteur
        // explique pourquoi (Actors.cpp:12410) :
        //
        //   if(net && mode==PLRMODE_ACTIVE){
        //      net->GetPlayerName(pid, buf, sizeof(buf));
        //      return buf;                       // le nom vient du RESEAU
        //   }
        //   int i = tab->ItemI(TAB_I_HUM_FACE);
        //   if(!i) return "unknown";
        //
        // En partie en reseau - le cas du joueur - le nom du soldat actif vient
        // du reseau, pas de la table. Ses propres soldats peuvent donc porter
        // un numero de visage nul en toute legitimite. Exiger le contraire
        // rejetait forcement tous les emplacements.
        //
        // On ne verifie donc plus que ce qui est structurel : une table
        // coherente, et une propriete 65 qui s'annonce ENTIERE avec une valeur
        // dans les bornes. La valeur elle-meme est relevee et journalisee.
        bool usable = true;
        std::int32_t first_face = -1;

        // Les ennemis d'abord : leur groupe ne peut valoir que 0, 1 ou 2.
        // C'est la contrainte qui manquait aux trois mesures precedentes.
        for (const std::uintptr_t enemy : reference_enemies)
        {
            const std::uintptr_t address = ResolveActorTableInteger(
                process, enemy, kEnemyGroupProperty, offset);
            std::int32_t group = -1;
            if (address == 0 || !process.ReadMemory(address, group) ||
                group < kEnemyGroupGerman || group > kEnemyGroupCivilian)
            {
                usable = false;
                break;
            }
        }
        if (!usable)
            continue;

        for (const std::uintptr_t player : reference_players)
        {
            const std::uintptr_t address = ResolveActorTableInteger(
                process, player, kHumanFaceProperty, offset);
            std::int32_t face = -1;
            if (address == 0 || !process.ReadMemory(address, face) ||
                face < 0 || face > kMaximumFaceNumber)
            {
                usable = false;
                break;
            }
            if (first_face < 0)
                first_face = face;
        }
        if (!usable)
            continue;
        ++candidates;
        found = offset;
        LogDiagnostic(
            "Table d'acteur: candidat +0x%03X, premier numero de visage lu = "
            "%d.",
            static_cast<unsigned>(offset), first_face);
    }
    if (candidates == 1)
    {
        g_actor_table.offset = found;
        LogDiagnostic(
            "Table d'acteur: MESUREE a +0x%03X sur %u soldat(s) et %u "
            "ennemi(s) - groupe d'ennemi dans les bornes chez tous. "
            "Reference 2002 : +0x190.",
            static_cast<unsigned>(found),
            static_cast<unsigned>(reference_players.size()),
            static_cast<unsigned>(reference_enemies.size()));
    }
    else
    {
        LogDiagnostic(
            "Table d'acteur: NON MESURABLE (%u emplacement(s) possible(s) sur "
            "%u soldat(s)). Le nom des soldats crees ne sera pas change.",
            candidates, static_cast<unsigned>(reference_players.size()));

        // V130 - on RELEVE ce que l'on voit reellement a l'emplacement que le
        // code designe, au lieu d'abandonner sans rien apprendre. Si la mesure
        // echoue encore, ces lignes diront laquelle des trois hypotheses -
        // emplacement, disposition de la table, indice de propriete - est
        // fausse.
        if (from_code != 0 && !reference_players.empty())
        {
            const std::uintptr_t actor = reference_players.front();
            std::uintptr_t table = 0;
            std::uint32_t count = 0;
            std::uintptr_t descriptors = 0;
            std::uintptr_t data = 0;
            std::uint32_t data_size = 0;
            (void)process.ReadMemory(actor + from_code, table);
            if (IsSanePointer(table))
            {
                (void)process.ReadMemory(
                    table + kTableItemCountOffset, count);
                (void)process.ReadMemory(
                    table + kTableDescriptorsOffset, descriptors);
                (void)process.ReadMemory(table + kTableDataOffset, data);
                (void)process.ReadMemory(
                    table + kTableDataSizeOffset, data_size);
            }
            LogDiagnostic(
                "Table d'acteur: releve a +0x%03X -> table=%08X "
                "proprietes=%u descripteurs=%08X donnees=%08X taille=%u.",
                static_cast<unsigned>(from_code),
                static_cast<unsigned>(table), count,
                static_cast<unsigned>(descriptors),
                static_cast<unsigned>(data), data_size);
            if (IsSanePointer(descriptors) && count > kHumanFaceProperty)
            {
                ActorTableDescriptor descriptor{};
                if (process.ReadMemory(
                        descriptors +
                            static_cast<std::uintptr_t>(kHumanFaceProperty) *
                                kTableDescriptorSize,
                        descriptor))
                {
                    std::int32_t value = -1;
                    if (IsSanePointer(data))
                    {
                        (void)process.ReadMemory(
                            data + descriptor.offset, value);
                    }
                    LogDiagnostic(
                        "Table d'acteur: propriete %u -> offset=%u type=%u "
                        "longueur=%u, valeur lue=%d.",
                        kHumanFaceProperty, descriptor.offset,
                        static_cast<unsigned>(descriptor.type),
                        static_cast<unsigned>(descriptor.array_length),
                        value);
                }
            }
        }
    }
    return g_actor_table.offset;
}

// Les ennemis vus par le radar, employes comme reference de mesure : leur
// groupe ne peut valoir que 0, 1 ou 2, ce qui donne enfin une verification
// serree de l'emplacement de la table.
std::vector<std::uintptr_t> g_reference_enemies{};

void CollectReferenceEnemies(const RadarSnapshot& snapshot)
{
    g_reference_enemies.clear();
    const std::uint32_t count = (std::min)(
        snapshot.entity_array.count,
        static_cast<std::uint32_t>(kMaximumRadarEntities));
    for (std::uint32_t index = 0;
         index < count && g_reference_enemies.size() < 8; ++index)
    {
        const RadarEntity& entity = snapshot.entity_array.entities[index];
        if (entity.team == EntityTeam::Enemy &&
            IsSanePointer(entity.actor_address))
        {
            g_reference_enemies.push_back(entity.actor_address);
        }
    }
}

// =====================================================================
// V139 - L'ETENDUE REELLE DU BLOC DE FICHE, MESUREE ET NON DEVINEE
// =====================================================================
//
// La recopie de fiche que le joueur a retenue consiste a copier le bloc de
// donnees d'un vrai soldat vers un soldat cree. Pour cela il faut savoir
// combien d'octets copier - et le desassemblage d'`itabler2.dll` a montre que
// LE MOTEUR NE RANGE AUCUNE TAILLE utilisable : `C_table::Item` ne lit que le
// nombre d'entrees, les descripteurs et les donnees.
//
//   8B 48 20   mov ecx,[this+0x20]     ; descripteurs
//   8B 50 24   mov edx,[this+0x24]     ; donnees
//   8B 78 0C   mov edi,[this+0x0C]     ; nombre d'entrees
//   8B 04 F1   mov eax,[ecx+index*8]   ; descripteur -> decalage
//   03 C2      add eax,edx             ; donnees + decalage
//
// L'etendue se deduit donc des descripteurs eux-memes : c'est le plus grand
// decalage present, augmente de la place de l'entree correspondante. On la
// RELEVE ici, sans rien ecrire, pour pouvoir copier au bon nombre d'octets a
// la version suivante plutot que de choisir une taille au jugement - ce qui
// est exactement l'erreur qui a coute cinq versions a ce dossier.
void ReportTableBlockExtent(
    TrainerProcess& process,
    std::uintptr_t actor,
    std::uintptr_t table_offset)
{
    if (table_offset == 0 || !IsSanePointer(actor))
        return;
    std::uintptr_t table = 0;
    std::uint32_t count = 0;
    std::uintptr_t descriptors = 0;
    std::uintptr_t data = 0;
    if (!process.ReadMemory(actor + table_offset, table) ||
        !IsSanePointer(table) ||
        !process.ReadMemory(table + kTableItemCountOffset, count) ||
        !process.ReadMemory(table + kTableDescriptorsOffset, descriptors) ||
        !process.ReadMemory(table + kTableDataOffset, data) ||
        count == 0 || count > 4096 ||
        !IsSanePointer(descriptors) || !IsSanePointer(data))
    {
        return;
    }
    std::uint32_t highest = 0;
    unsigned present = 0;
    unsigned strings = 0;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        ActorTableDescriptor descriptor{};
        if (!process.ReadMemory(
                descriptors +
                    static_cast<std::uintptr_t>(index) * kTableDescriptorSize,
                descriptor))
        {
            break;
        }
        if (descriptor.offset == std::numeric_limits<std::uint32_t>::max())
            continue;   // propriete absente de ce modele
        ++present;
        // La place occupee : une chaine tient `max_string_size` octets rangee
        // en clair, tout le reste tient quatre octets, le tout multiplie par
        // la longueur du tableau quand la propriete en est un.
        std::uint32_t width = descriptor.max_string_size != 0
                                  ? descriptor.max_string_size
                                  : 4U;
        if (descriptor.max_string_size != 0)
            ++strings;
        std::uint32_t length = descriptor.array_length != 0
                                   ? descriptor.array_length
                                   : 1U;
        if (length > 1024U)
            length = 1U;
        const std::uint32_t end = descriptor.offset + width * length;
        if (end > highest && end < 0x100000U)
            highest = end;
    }
    LogDiagnostic(
        "Fiche: %u propriete(s) declarees, %u presentes dont %u texte(s); le "
        "bloc de donnees s'etend sur %u octets (table=%08X donnees=%08X). "
        "C'est ce nombre qu'il faut pour la recopie.",
        count, present, strings, highest,
        static_cast<unsigned>(table), static_cast<unsigned>(data));
}

// Donne un visage - donc un NOM - a chaque soldat cree.
void NameCreatedSoldiersByFace(
    TrainerProcess& process,
    const std::vector<std::uintptr_t>& created,
    const std::vector<std::uintptr_t>& reference_players)
{
    const std::uintptr_t table_offset =
        MeasureActorTableOffset(process, reference_players, g_reference_enemies);
    if (table_offset == 0)
        return;

    // V139 - une seule fois, on releve l'etendue du bloc de fiche d'un vrai
    // soldat. Lecture pure : c'est la mesure qui manque a la recopie.
    static bool extent_reported = false;
    if (!extent_reported && !reference_players.empty())
    {
        extent_reported = true;
        ReportTableBlockExtent(
            process, reference_players.front(), table_offset);
    }

    // Les numeros de visage des soldats du joueur : forcement valides.
    std::vector<std::int32_t> faces;
    for (const std::uintptr_t player : reference_players)
    {
        const std::uintptr_t address = ResolveActorTableInteger(
            process, player, kHumanFaceProperty, table_offset);
        std::int32_t face = 0;
        if (address != 0 && process.ReadMemory(address, face) && face > 0)
            faces.push_back(face);
    }
    if (faces.empty())
    {
        // V131 - si les soldats du joueur portent tous un visage nul - ce qui
        // est normal en reseau, leur nom venant alors du reseau - on ne peut
        // pas reprendre leur numero. On n'en invente pas pour autant : le jeu
        // irait chercher un nom hors de sa table de textes.
        //
        // Le journal releve la situation, et la ligne « candidat » ci-dessus
        // dit quel numero les soldats portent reellement.
        LogDiagnostic(
            "Nom des soldats: aucun numero de visage lisible chez vos "
            "soldats; les soldats crees garderont « unknown ».");
        return;
    }
    unsigned named = 0;
    for (std::size_t index = 0; index < created.size(); ++index)
    {
        const std::uintptr_t address = ResolveActorTableInteger(
            process, created[index], kHumanFaceProperty, table_offset);
        if (address == 0)
            continue;
        const std::int32_t face = faces[index % faces.size()];
        if (process.WriteMemory(address, face))
            ++named;
    }
    LogDiagnostic(
        "Nom des soldats: numero de visage pose chez %u soldat(s) sur %u "
        "(%u numero(s) repris de vos soldats). C'est ce numero qui donne le "
        "nom affiche : zero valait « unknown ».",
        named, static_cast<unsigned>(created.size()),
        static_cast<unsigned>(faces.size()));
}

// =====================================================================
// V141 - LA RECOPIE DE FICHE, PUIS L'ETAPE QUE LE MOTEUR EXECUTE LUI-MEME
// =====================================================================
//
// Le joueur demande depuis longtemps que ses soldats crees PORTENT l'arme
// choisie dans la fenetre J. Elle entre bien dans leur inventaire, mais reste
// dans le sac. Sept facons de la leur mettre en main ont ete essayees, toutes
// fatales - et toutes s'y prenaient de la meme maniere : forcer l'etat depuis
// l'exterieur.
//
// Le moteur, lui, ne s'y prend pas ainsi. Voici comment IL arme un soldat de
// mission, mot pour mot (GameMission.cpp:1758) :
//
//   act = CreateActor(type);
//   act->SetFrame(frm);
//   act->MissionLoad(&lc.ck, 0);
//
// et `MissionLoad` d'un humain se reduit a UNE ligne (H&D.h:973) :
//
//   inline bool LoadTable(C_chunk *ck, LPC_table tab){
//      return tab->Open((dword)ck->GetHandle(),
//                       TABOPEN_FILEHANDLE | TABOPEN_UPDATE);
//   }
//
// La seule chose qui manque a un soldat cree, c'est LE CONTENU DE SA FICHE.
// Rien d'autre : le constructeur lui donne deja son jeu d'animations, sa main
// et son inventaire de base (Actors.cpp:3409). Le trainer appelle bien
// `CreateActor` et `SetFrame`; il n'a jamais rempli la fiche.
//
// Et `TABOPEN_UPDATE` declenche `TableUpdate`, que voici (Actors.cpp:7343) :
//
//   C_inventory::DeleteAllItems();                 // ne garde que les mains
//   for(i=0; i<tab->ArrayLen(TAB_I_HUM_INV_LIST); i++){
//      int itm = tab->ItemI(TAB_I_HUM_INV_LIST, i);
//      ...
//      C_inventory::AddItem(itm, tab->ItemI(TAB_I_HUM_INV_AMOUNT, i));
//      if(!i) SetSelectedInvItem(1, false);        // <-- L'ARME EN MAIN
//      C_inventory::Reload(C_inventory::NumItems()-1, true);
//   }
//
// C'est le moteur qui met l'arme dans la main, a partir de la liste
// d'inventaire ecrite dans la fiche - et seulement pour le PREMIER poste de
// cette liste, la designation etant sous `if(!i)`.
//
// POURQUOI LA RECOPIE EST OBLIGATOIRE, ET NON UN CONFORT
//
// `C_player::TableUpdate` commence par recalculer la sante (Actors.cpp:12790) :
//
//   float endurance = (tab->ItemI(TAB_I_HUM_BAR5_ENDURANCE)-30)/70.0f;
//   resistance = Max(1, 200 + (int)(endurance*1400.0f));
//
// Sur une fiche vide, l'endurance vaut zero : 200 + (-600) donne -400, et
// `Max(1, -400)` vaut UN. Appliquer la fiche sans l'avoir remplie d'abord
// ferait mourir les soldats au premier impact. La recopie que le joueur avait
// choisie n'est donc pas un agrement : sans elle, cette voie serait nuisible.
//
// POURQUOI LA RECOPIE EST SANS DANGER
//
// Le source de la bibliotheque des fiches le dit (ITabCore.h:131) :
//
//   dword num_items;      // +0x0C
//   S_desc_item *desc;    // +0x20
//   byte *data;           // +0x24
//   dword data_size;      // +0x28
//
// et les textes tiennent DANS le bloc de donnees - le descripteur porte un
// `max_string_size`, pas un pointeur. Le bloc est auto-suffisant : rien n'est
// partage, rien ne sera libere deux fois.
//
// Enfin, les trois tables de methodes qui portent `GetTable` sur le binaire du
// joueur rendent toutes LE MEME modele de fiche :
//
//   rang +4   004257F0 : b8 18 f1 4f 00  c2 04 00   -> mov eax,004FF118; ret 4
//
// Un vrai soldat et un soldat cree ont donc exactement la meme disposition de
// fiche. La recopie est licite, et on la refuse si le nombre de proprietes ou
// la taille du bloc different.
constexpr std::uint32_t kHumanInvListProperty = 8;     // TAB_I_HUM_INV_LIST
constexpr std::uint32_t kHumanInvAmountProperty = 9;   // TAB_I_HUM_INV_AMOUNT

// `GetTable`, `GetTemplate` et `TableUpdate` se suivent dans la declaration de
// `C_actor` (H&D.h:966). `GetTable` est mesuree a +0xB4 sur le binaire du
// joueur, et les deux rangs suivants ont ete LUS dans ses tables de methodes :
// +0xB8 rend une constante - le modele de fiche - et +0xBC porte trois
// implantations distinctes, une par classe d'humain, comme la hierarchie
// l'exige. `TableUpdate` est donc a +8 de `GetTable`.
constexpr std::uintptr_t kTableUpdateFromGetTable = 8;

struct ActorFiche
{
    std::uintptr_t table = 0;
    std::uintptr_t descriptors = 0;
    std::uintptr_t data = 0;
    std::uint32_t count = 0;
    std::uint32_t data_size = 0;
};

bool ReadActorFiche(
    TrainerProcess& process,
    std::uintptr_t actor,
    std::uintptr_t table_offset,
    ActorFiche& fiche)
{
    if (table_offset == 0 || !IsSanePointer(actor))
        return false;
    if (!process.ReadMemory(actor + table_offset, fiche.table) ||
        !IsSanePointer(fiche.table) ||
        !process.ReadMemory(
            fiche.table + kTableItemCountOffset, fiche.count) ||
        !process.ReadMemory(
            fiche.table + kTableDescriptorsOffset, fiche.descriptors) ||
        !process.ReadMemory(fiche.table + kTableDataOffset, fiche.data) ||
        !process.ReadMemory(
            fiche.table + kTableDataSizeOffset, fiche.data_size))
    {
        return false;
    }
    return fiche.count != 0 && fiche.count <= 4096 &&
           IsSanePointer(fiche.descriptors) && IsSanePointer(fiche.data) &&
           fiche.data_size != 0 && fiche.data_size <= 64U * 1024U;
}

// Decalage d'une propriete dans le bloc de donnees, ou -1 si absente.
std::int64_t FicheProperty(
    TrainerProcess& process,
    const ActorFiche& fiche,
    std::uint32_t property,
    ActorTableDescriptor& descriptor)
{
    if (property >= fiche.count)
        return -1;
    if (!process.ReadMemory(
            fiche.descriptors +
                static_cast<std::uintptr_t>(property) * kTableDescriptorSize,
            descriptor))
    {
        return -1;
    }
    if (descriptor.offset == std::numeric_limits<std::uint32_t>::max() ||
        descriptor.offset >= fiche.data_size)
    {
        return -1;
    }
    return static_cast<std::int64_t>(descriptor.offset);
}

// Recopie le bloc de fiche d'un vrai soldat vers chaque soldat cree.
unsigned CopyFicheFromDonor(
    TrainerProcess& process,
    std::uintptr_t donor,
    const std::vector<std::uintptr_t>& soldiers,
    std::uintptr_t table_offset)
{
    ActorFiche source{};
    if (!ReadActorFiche(process, donor, table_offset, source))
    {
        LogDiagnostic(
            "Recopie de fiche: la fiche de VOTRE soldat est illisible, rien "
            "n'est recopie.");
        return 0;
    }
    std::vector<std::uint8_t> block(source.data_size);
    if (!process.ReadMemory(source.data, block.data(), block.size()))
    {
        LogDiagnostic(
            "Recopie de fiche: bloc de %u octets illisible chez votre soldat.",
            source.data_size);
        return 0;
    }
    unsigned copied = 0;
    unsigned refused = 0;
    for (const std::uintptr_t soldier : soldiers)
    {
        ActorFiche target{};
        if (!ReadActorFiche(process, soldier, table_offset, target))
        {
            ++refused;
            continue;
        }
        // On ne recopie QUE si la disposition est identique. Deux fiches du
        // meme modele ont forcement le meme nombre de proprietes et la meme
        // taille de bloc; sinon ce ne sont pas les memes fiches, et la
        // recopie n'aurait aucun sens.
        if (target.count != source.count ||
            target.data_size != source.data_size)
        {
            ++refused;
            continue;
        }
        if (process.WriteMemory(target.data, block.data(), block.size()))
            ++copied;
        else
            ++refused;
    }
    LogDiagnostic(
        "Recopie de fiche: bloc de %u octets (%u proprietes) recopie chez %u "
        "soldat(s), refuse chez %u. Ils recoivent identite, visage, endurance "
        "et equipement de depart de votre soldat.",
        source.data_size, source.count, copied, refused);
    return copied;
}

// Ecrit l'arme choisie au PREMIER poste de la liste d'inventaire de la fiche.
// C'est ce poste, et lui seul, que `TableUpdate` met dans la main.
unsigned WriteFicheWeaponSlot(
    TrainerProcess& process,
    const std::vector<std::uintptr_t>& soldiers,
    std::uintptr_t table_offset,
    std::int32_t item_id,
    std::int32_t amount)
{
    unsigned written = 0;
    bool reported = false;
    for (const std::uintptr_t soldier : soldiers)
    {
        ActorFiche fiche{};
        if (!ReadActorFiche(process, soldier, table_offset, fiche))
            continue;
        ActorTableDescriptor list_desc{};
        ActorTableDescriptor amount_desc{};
        const std::int64_t list = FicheProperty(
            process, fiche, kHumanInvListProperty, list_desc);
        const std::int64_t quantity = FicheProperty(
            process, fiche, kHumanInvAmountProperty, amount_desc);
        if (!reported)
        {
            reported = true;
            LogDiagnostic(
                "Recopie de fiche: liste d'inventaire -> decalage %lld "
                "(%u poste(s)), quantites -> decalage %lld (%u poste(s)).",
                static_cast<long long>(list),
                static_cast<unsigned>(list_desc.array_length),
                static_cast<long long>(quantity),
                static_cast<unsigned>(amount_desc.array_length));
        }
        if (list < 0 || quantity < 0)
            continue;
        const std::uintptr_t list_address =
            fiche.data + static_cast<std::uintptr_t>(list);
        const std::uintptr_t amount_address =
            fiche.data + static_cast<std::uintptr_t>(quantity);
        if (process.WriteMemory(list_address, item_id) &&
            process.WriteMemory(amount_address, amount))
        {
            ++written;
        }
    }
    return written;
}

// Demande au moteur d'APPLIQUER la fiche, sur son propre thread.
//
// `TableUpdate(index, not_load_prg)` est appelee avec `not_load_prg = true` :
// ce second argument commande `if(!not_load_prg) LoadProgram();`, et un soldat
// cree n'a aucun programme de mission a charger.
bool ApplyFicheOnGameThread(
    TrainerProcess& process,
    const std::vector<std::uintptr_t>& soldiers,
    std::uintptr_t table_update_vtable_offset)
{
    if (soldiers.empty() || table_update_vtable_offset == 0)
        return false;
    constexpr std::uintptr_t kProcessCheatHookRva = 0x0009'FC10;
    constexpr std::array<std::uint8_t, 5> kExpected{
        0xB8, 0x5C, 0x10, 0x00, 0x00};
    constexpr std::size_t kRemoteSize = 0x1000;
    constexpr std::size_t kCountOffset = 0x200;
    constexpr std::size_t kCompletionOffset = 0x204;
    constexpr std::size_t kActorsOffset = 0x300;

    const std::uint32_t count = static_cast<std::uint32_t>(
        (std::min)(soldiers.size(), std::size_t{kMaximumSpawnedSoldiers}));

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        kProcessCheatHookRva >= module.image_size)
    {
        return false;
    }

    // V141 - ON VERIFIE LA METHODE AVANT DE L'APPELER.
    //
    // Le rang de `TableUpdate` a ete etabli par lecture, mais on ne saute pas
    // dans une adresse sur la foi d'un raisonnement. Chaque acteur vise doit
    // presenter, a ce rang de sa table de methodes, une adresse qui tombe
    // dans le code du jeu. Un seul manquant, et on refuse tout : mieux vaut
    // une arme qui reste dans le sac qu'un saut dans le vide.
    for (const std::uintptr_t soldier : soldiers)
    {
        std::uintptr_t vtable = 0;
        std::uintptr_t method = 0;
        if (!process.ReadMemory(soldier, vtable) || !IsSanePointer(vtable) ||
            !process.ReadMemory(
                vtable + table_update_vtable_offset, method) ||
            !process.IsReadableCodeTarget(method) ||
            method < module.base_address ||
            method >= module.base_address + module.image_size)
        {
            LogDiagnostic(
                "Application de fiche: REFUSEE - le soldat %08X ne presente "
                "pas de methode valide au rang +0x%02X (lue: %08X). Rien "
                "n'est appele.",
                static_cast<unsigned>(soldier),
                static_cast<unsigned>(table_update_vtable_offset),
                static_cast<unsigned>(method));
            return false;
        }
    }

    const std::uintptr_t hook = module.base_address + kProcessCheatHookRva;
    std::array<std::uint8_t, 5> original{};
    if (!process.ReadMemory(hook, original.data(), original.size()) ||
        original != kExpected)
    {
        return false;
    }
    const std::uintptr_t remote = process.AllocateRemoteMemory(kRemoteSize);
    if (!IsSanePointer(remote))
        return false;

    std::vector<std::uint8_t> code;
    const auto byte = [&](std::uint8_t v) { code.push_back(v); };
    const auto dword = [&](std::uint32_t v)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(v));
        std::memcpy(code.data() + offset, &v, sizeof(v));
    };
    const auto slot = [&](std::size_t offset)
    {
        dword(static_cast<std::uint32_t>(remote + offset));
    };
    std::vector<std::size_t> to_end;

    byte(0x9C);                                   // pushfd
    byte(0x60);                                   // pushad
    byte(0x83); byte(0x3D); slot(kCompletionOffset); byte(0x00);
    byte(0x0F); byte(0x85);
    to_end.push_back(code.size()); dword(0);      // jne fin
    byte(0x33); byte(0xF6);                       // xor esi,esi
    const std::size_t loop_start = code.size();
    byte(0xA1); slot(kCountOffset);               // mov eax,[count]
    byte(0x3B); byte(0xF0);                       // cmp esi,eax
    byte(0x0F); byte(0x83);
    to_end.push_back(code.size()); dword(0);      // jae fin
    byte(0x8B); byte(0x1C); byte(0xB5);
    slot(kActorsOffset);                          // mov ebx,[esi*4+acteurs]
    byte(0x85); byte(0xDB);                       // test ebx,ebx
    const std::size_t skip_soldier = code.size();
    byte(0x74); byte(0x00);                       // je suivant

    // TableUpdate(0, true) : this dans ecx, deux arguments sur la pile,
    // liberes par l'appelee (`ret 8`).
    byte(0x6A); byte(0x01);                       // push true (not_load_prg)
    byte(0x6A); byte(0x00);                       // push 0    (index)
    byte(0x8B); byte(0xCB);                       // mov ecx,acteur
    byte(0x8B); byte(0x03);                       // mov eax,[acteur] = vtable
    byte(0xFF); byte(0x90);                       // call [eax+deplacement]
    dword(static_cast<std::uint32_t>(table_update_vtable_offset));

    const std::size_t next_soldier = code.size();
    code[skip_soldier + 1] = static_cast<std::uint8_t>(
        next_soldier - (skip_soldier + 2));
    byte(0x46);                                   // inc esi
    byte(0xE9);
    const std::size_t repeat = code.size();
    dword(0);
    const std::size_t finish = code.size();
    for (const std::size_t displacement : to_end)
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            finish - (displacement + sizeof(std::int32_t)));
        std::memcpy(code.data() + displacement, &relative, sizeof(relative));
    }
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            loop_start - (repeat + sizeof(std::int32_t)));
        std::memcpy(code.data() + repeat, &relative, sizeof(relative));
    }
    byte(0xC7); byte(0x05); slot(kCompletionOffset); dword(1);
    byte(0x61);                                   // popad
    byte(0x9D);                                   // popfd
    code.insert(code.end(), original.begin(), original.end());
    byte(0xE9);
    dword(static_cast<std::uint32_t>(
        (hook + original.size()) -
        (remote + code.size() + sizeof(std::int32_t))));

    const std::uint32_t zero = 0;
    bool ok = code.size() < kCountOffset &&
        process.WriteMemory(remote, code.data(), code.size()) &&
        process.WriteMemory(remote + kCountOffset, count) &&
        process.WriteMemory(remote + kCompletionOffset, zero);
    for (std::uint32_t index = 0; ok && index < count; ++index)
    {
        ok = process.WriteMemory(
            remote + kActorsOffset + index * sizeof(std::uint32_t),
            static_cast<std::uint32_t>(soldiers[index]));
    }
    if (!ok)
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }
    std::array<std::uint8_t, 5> patch{0xE9, 0, 0, 0, 0};
    const std::int32_t relative =
        static_cast<std::int32_t>(remote - (hook + patch.size()));
    std::memcpy(patch.data() + 1, &relative, sizeof(relative));
    if (!InstallHookSafely(
            process, hook, patch.data(), patch.size(),
            "le saut vers le code pose"))
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }
    const bool triggered = SendInventoryMainThreadTrigger(process);
    std::uint32_t completed = 0;
    if (triggered)
    {
        for (int attempt = 0; attempt < 500 && completed == 0; ++attempt)
        {
            (void)process.ReadMemory(remote + kCompletionOffset, completed);
            if (completed == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    (void)RestoreHookSafely(
        process, hook, original.data(), original.size());
    LogDiagnostic(
        "Application de fiche: %u soldat(s) declenche=%u execute=%u.",
        count, triggered ? 1U : 0U, completed);
    return completed != 0;
}

// =====================================================================
// V142 - RALLIER LES ENNEMIS : LE SEUL CHEMIN CERTAIN
// =====================================================================
//
// Sept versions ont tente de faire porter une arme a un soldat CREE. Toutes
// ont echoue, et pour une raison qui n'est pas contournable de l'exterieur :
// un acteur cree par le trainer n'a pas de fiche, et le jeu ne remplit une
// fiche qu'en chargeant une mission.
//
// Un ennemi de la mission, lui, a TOUT. Le jeu l'a fabrique lui-meme : fiche
// complete, arme en main, posture, animations, intelligence. Le faire passer
// de notre cote ne demande donc rien a construire - seulement a changer un
// nombre.
//
// Ce nombre est `TAB_I_ENM_GROUP`, la propriete 74, et les DEUX fonctions qui
// decident de l'hostilite le lisent :
//
//   // C_player::IsEnemy(asker)   - « lui, est-il mon ennemi ? »
//   case ACTOR_ENEMY:
//      return (his_group == 0);            // groupe 1 -> non
//
//   // C_enemy::IsEnemy(asker)    - « moi, qui est mon ennemi ? »
//   int my_group = tab->ItemI(TAB_I_ENM_GROUP);
//   case ACTOR_PLAYER:  return (my_group == 0);       // ne tire plus sur nous
//   case ACTOR_ENEMY:   return (my_group != his_group); // 1 != 0 -> les Allemands
//
// Ecrire 1 (russe) le rend ami avec le joueur ET ennemi des Allemands, des
// deux cotes a la fois. Et son intelligence suit : `C_human::WatchHumans`
// choisit qui surveiller avec ce meme `IsEnemy` (Actors.cpp:3937), donc il
// cesse de nous chercher et se met a chercher les Allemands.
//
// Le groupe 2 (civil) n'est PAS retenu : le code dit « civil is friend of
// everyone », il ne se battrait donc pour personne. On veut des allies qui
// combattent, pas des neutres.
// V157 - UN CAMP BIEN A NOUS, ET NON CELUI DES RUSSES.
//
// Le joueur : « j'ai essaye une autre mission, les ennemis de mon cote ne
// tirent pas sur les autres ennemis, je ne sais pas pourquoi ».
//
// La cause est dans la derniere ligne de `C_enemy::IsEnemy` (Actors.cpp:15070) :
//
//   return (my_group != his_group);
//
// L'hostilite se decide par DIFFERENCE de groupe. Mettre nos rallies dans le
// groupe 1 (russe) marche tant que les ennemis de la mission sont allemands
// (groupe 0). Mais dans une mission ou ils sont DEJA russes, nos rallies se
// retrouvaient dans le meme camp qu'eux - et plus personne ne se battait.
//
// On leur donne donc un groupe qui n'appartient a personne : le 3.
//
//   - il n'est pas 0, donc `C_player::IsEnemy` rend faux : ils ne sont pas nos
//     ennemis, et `C_enemy::IsEnemy(joueur)` rend faux aussi : ils ne nous
//     tirent pas dessus;
//   - il n'est pas 2, donc ils ne sont pas « amis de tout le monde » - ils se
//     battent vraiment;
//   - et il differe de 0, de 1 et de 2, donc ils sont hostiles a TOUS les
//     ennemis de la mission, quel que soit le camp de celle-ci.
//
// Deux rallies partagent ce meme 3 : `3 != 3` est faux, ils ne se tirent donc
// pas dessus entre eux.
//
// Verifie sans danger cote carte : `Map_man.cpp:2050` fait
// `type = eg==0 ? 1 : 2;` - une comparaison, pas un indice de tableau. Un
// groupe 3 n'y depasse rien.
constexpr std::int32_t kEnemyGroupOurOwn = 3;

// V162 - ON PREFERE LE GROUPE 1, QUI A FAIT SES PREUVES.
//
// Le joueur : « dans la version V153 les ennemis de mon equipe frappaient les
// autres ennemis ». La V153 employait le groupe 1 (russe); la V157 est passee
// au groupe 3.
//
// Le raisonnement de la V157 reste juste - 3 n'appartient a personne, donc il
// est hostile a tous les camps - et rien dans le moteur ne le rejette : les
// seuls autres usages du groupe sont `!group ? 50 : 0` pour la voix, un
// `switch` de statistiques sur 0 et 2, et une comparaison a 2 pour les ordres.
// Mais le groupe 1 est celui que le joueur a VU fonctionner, et il n'y a
// aucune raison de lui preferer une valeur non eprouvee quand elle convient.
//
// On choisit donc, parmi 1 puis 3, la premiere valeur qu'aucun ennemi encore
// en face n'utilise. Face a des Allemands (groupe 0), c'est 1 - exactement la
// V153. Face a des Russes (groupe 1), c'est 3.
std::int32_t ChooseAllyGroup(const std::map<std::int32_t, unsigned>& camps)
{
    for (const std::int32_t candidate :
         {kEnemyGroupRussian, kEnemyGroupOurOwn})
    {
        if (camps.find(candidate) == camps.end())
            return candidate;
    }
    return kEnemyGroupOurOwn;
}

// Le groupe d'origine de chaque rallie, pour pouvoir le lui rendre.
//
// `LIBERER` remettait 0 - le camp allemand - ce qui, dans une mission ou les
// ennemis sont russes, en aurait fait des ennemis du joueur au lieu de les
// rendre a leur camp. On note donc ce qu'ils portaient avant.
std::map<std::uintptr_t, std::int32_t> g_rallied_origin{};

// Tous les ennemis vivants de la mission, releves a chaque image, classes du
// plus proche du joueur au plus loin.
std::vector<std::uintptr_t> g_mission_enemies{};
std::vector<Vector3> g_mission_enemy_positions{};
// Distance au plus proche ennemi encore hostile, pour l'afficher dans la
// fenetre : le joueur sait ainsi s'il va voir quelque chose.
float g_nearest_hostile_distance = -1.0f;
// Derniere position connue du joueur, retenue pour mesurer les distances et
// pour l'ordre « venir a moi ».
Vector3 g_last_player_position{};
bool g_last_player_position_valid = false;
// Ceux qui sont passes de notre cote.
std::vector<std::uintptr_t> g_rallied_enemies{};

bool IsRalliedEnemy(std::uintptr_t actor)
{
    return std::find(
               g_rallied_enemies.begin(), g_rallied_enemies.end(), actor) !=
        g_rallied_enemies.end();
}

// Releve TOUS les ennemis de la mission, rallies compris.
//
// Le test porte sur le TYPE de l'acteur, et non sur le camp affiche par le
// radar : un rallie s'y affiche desormais en allie, et se compter lui-meme
// hors de la mission ferait baisser le total a chaque ralliement.
void CollectMissionEnemies(TrainerProcess& process, const RadarSnapshot& snap)
{
    g_mission_enemies.clear();
    g_mission_enemy_positions.clear();
    g_last_player_position = snap.player.position;
    g_last_player_position_valid = true;
    const std::uint32_t count = (std::min)(
        snap.entity_array.count,
        static_cast<std::uint32_t>(kMaximumRadarEntities));
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const RadarEntity& entity = snap.entity_array.entities[index];
        std::uint32_t type = 0;
        if (!IsSanePointer(entity.actor_address) ||
            !process.ReadMemory(
                entity.actor_address + kActorTypeOffset, type) ||
            type != kActorTypeEnemy)
        {
            continue;
        }
        if (std::find(
                g_mission_enemies.begin(), g_mission_enemies.end(),
                entity.actor_address) == g_mission_enemies.end())
        {
            g_mission_enemies.push_back(entity.actor_address);
            g_mission_enemy_positions.push_back(entity.position);
        }
    }

    // V143 - DU PLUS PROCHE AU PLUS LOIN.
    // (la purge des rallies disparus suit le classement, plus bas)
    //
    // Le joueur a pose la bonne question : « quand je selectionne 1, est-ce
    // qu'il se place a cote de moi ? » Non - un rallie ne bouge pas, il change
    // seulement de camp. Mais si le trainer choisit le premier de la liste des
    // acteurs, ce peut etre un ennemi a l'autre bout de la carte : le joueur ne
    // verrait rien et croirait a un echec.
    //
    // On classe donc les ennemis par distance, et « rallier 1 » prend le plus
    // proche - celui que le joueur a sous les yeux.
    {
        std::vector<std::size_t> order(g_mission_enemies.size());
        for (std::size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        const Vector3 here = snap.player.position;
        const auto distance_to = [&](std::size_t i)
        {
            const Vector3& p = g_mission_enemy_positions[i];
            const float dx = p.x - here.x;
            const float dy = p.y - here.y;
            const float dz = p.z - here.z;
            return dx * dx + dy * dy + dz * dz;
        };
        std::stable_sort(
            order.begin(), order.end(),
            [&](std::size_t a, std::size_t b)
            { return distance_to(a) < distance_to(b); });
        std::vector<std::uintptr_t> sorted_actors;
        std::vector<Vector3> sorted_positions;
        sorted_actors.reserve(order.size());
        sorted_positions.reserve(order.size());
        for (const std::size_t i : order)
        {
            sorted_actors.push_back(g_mission_enemies[i]);
            sorted_positions.push_back(g_mission_enemy_positions[i]);
        }
        g_mission_enemies.swap(sorted_actors);
        g_mission_enemy_positions.swap(sorted_positions);
    }

    // V144 - LE COMPTE IMPOSSIBLE : « 66 sur 63 ennemis ».
    //
    // Le journal du joueur porte cette ligne, qui ne peut pas etre vraie :
    //
    //   Total de votre cote : 66 sur 63 ennemis de la mission.
    //
    // La purge de la V142 ne retirait un rallie que si son TYPE d'acteur
    // n'etait plus celui d'un ennemi. Or un ennemi detruit garde son type
    // jusqu'a ce que sa memoire serve a autre chose : les disparus restaient
    // donc comptes, et le total pouvait depasser l'effectif reel.
    //
    // Pire, cela faussait le calcul de ceux qui restent a rallier : trois
    // disparus comptes comme rallies rendaient trois hostiles de trop, et le
    // joueur en a rallie quarante la ou trente-sept restaient.
    //
    // On se fie desormais a la PRESENCE dans la liste d'acteurs du jeu, qui
    // vient d'etre relevee ci-dessus. Un rallie qui n'y figure plus a quitte
    // la partie, quelle qu'en soit la raison.
    g_rallied_enemies.erase(
        std::remove_if(
            g_rallied_enemies.begin(), g_rallied_enemies.end(),
            [&](std::uintptr_t actor)
            {
                return !IsSanePointer(actor) ||
                    std::find(
                        g_mission_enemies.begin(), g_mission_enemies.end(),
                        actor) == g_mission_enemies.end();
            }),
        g_rallied_enemies.end());
}

// Ceux qui restent a rallier : les ennemis encore allemands, du plus proche au
// plus loin. La distance du premier est relevee au passage.
std::vector<std::uintptr_t> EnemiesStillHostile()
{
    std::vector<std::uintptr_t> hostile;
    g_nearest_hostile_distance = -1.0f;
    for (std::size_t index = 0; index < g_mission_enemies.size(); ++index)
    {
        const std::uintptr_t actor = g_mission_enemies[index];
        if (IsRalliedEnemy(actor))
            continue;
        if (hostile.empty() && index < g_mission_enemy_positions.size() &&
            g_last_player_position_valid)
        {
            const Vector3& p = g_mission_enemy_positions[index];
            const float dx = p.x - g_last_player_position.x;
            const float dy = p.y - g_last_player_position.y;
            const float dz = p.z - g_last_player_position.z;
            g_nearest_hostile_distance =
                std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        hostile.push_back(actor);
    }
    return hostile;
}

// Ecrit le groupe demande dans la fiche, et RELIT pour verifier.
//
// On ne se contente jamais d'un `WriteMemory` qui rend vrai : le journal doit
// pouvoir dire, chiffre en main, combien d'ennemis portent reellement leur
// nouveau camp.
unsigned SetEnemyGroup(
    TrainerProcess& process,
    const std::vector<std::uintptr_t>& enemies,
    std::uintptr_t table_offset,
    std::int32_t group,
    std::vector<std::uintptr_t>& changed)
{
    changed.clear();
    if (table_offset == 0)
        return 0;
    for (const std::uintptr_t enemy : enemies)
    {
        const std::uintptr_t address = ResolveActorTableInteger(
            process, enemy, kEnemyGroupProperty, table_offset);
        if (address == 0)
            continue;
        std::int32_t before = -1;
        (void)process.ReadMemory(address, before);
        if (!process.WriteMemory(address, group))
            continue;
        std::int32_t after = -1;
        if (process.ReadMemory(address, after) && after == group)
        {
            changed.push_back(enemy);
            // V157 - on retient son camp d'origine, pour pouvoir le lui
            // rendre exactement. Le rendre au camp allemand par defaut serait
            // faux dans une mission ou les ennemis sont russes.
            if (group != kEnemyGroupGerman && before >= 0 &&
                g_rallied_origin.find(enemy) == g_rallied_origin.end())
            {
                g_rallied_origin[enemy] = before;
            }
        }
    }
    return static_cast<unsigned>(changed.size());
}

// =====================================================================
// V131 - LES SOLDATS DISPARUS SONT RETIRES DE LA LISTE
// =====================================================================
//
// Le journal du joueur montre une distribution d'arme visant UN SEUL soldat,
// qui n'aboutit pas, puis l'arret du jeu :
//
//   Miroir d'arme: objet 14 x1 ... chez 1 soldat(s) declenche=1 execute=0
//   Fenetre armes: seconde tentative echouee elle aussi.
//   Process: pid=0
//
// La liste des soldats crees n'etait JAMAIS purgee. Elle conservait les
// pointeurs de soldats morts et detruits par le moteur, ou effaces au
// rechargement d'une mission. Ecrire dans un acteur libere arrete le jeu, et
// c'est exactement ce qui s'est passe.
//
// Chaque acteur est donc verifie avant tout usage : sa table de methodes doit
// se trouver dans le module du jeu, et son champ de type doit encore annoncer
// un joueur. Un acteur libere ne presente presque jamais ces deux proprietes
// a la fois; les entrees qui echouent sont retirees et journalisees.
void PruneCreatedSoldiers(TrainerProcess& process)
{
    if (g_spawned_soldiers.empty() || !process.IsConnected())
        return;
    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) || module.image_size == 0)
        return;
    const std::size_t before = g_spawned_soldiers.size();
    g_spawned_soldiers.erase(
        std::remove_if(
            g_spawned_soldiers.begin(), g_spawned_soldiers.end(),
            [&](std::uintptr_t actor)
            {
                std::uintptr_t vtable = 0;
                std::uint32_t type = 0;
                if (!IsSanePointer(actor) ||
                    !process.ReadMemory(actor, vtable) ||
                    vtable < module.base_address ||
                    vtable >= module.base_address + module.image_size ||
                    !process.ReadMemory(actor + kActorTypeOffset, type) ||
                    type != kActorTypePlayer)
                {
                    return true;
                }
                return false;
            }),
        g_spawned_soldiers.end());
    if (g_spawned_soldiers.size() != before)
    {
        LogDiagnostic(
            "Soldats crees: %u entree(s) retiree(s) de la liste - acteurs "
            "disparus ou detruits par le jeu. Il en reste %u.",
            static_cast<unsigned>(before - g_spawned_soldiers.size()),
            static_cast<unsigned>(g_spawned_soldiers.size()));
    }
}

// =====================================================================
// V132 - VIDER LA MAIN DES SOLDATS CREES
// =====================================================================
//
// Le joueur constate que ses soldats recoivent bien l'arme - son journal le
// montre - mais qu'ils ne la portent pas, meme lorsqu'il prend leur controle
// et l'ouvre dans leur inventaire.
//
// Le moteur donne la raison, dans `C_human::SetGun` :
//
//   PI3D_frame hand = model->FindChildFrame("gun*", ENUMF_WILDMASK|ENUMF_ALL);
//   if(!hand) return;
//   gun = driver->CreateModel();
//   model_cache.Open(gun, name, mission.GetScene(), ...);
//   assert(!hand->NumChildren());        // <- LE MOTEUR ATTEND UNE MAIN VIDE
//   gun->LinkTo(hand);
//
// Or un soldat cree est la copie du soldat du joueur, DUPLIQUEE AU MOMENT OU
// CELUI-CI TENAIT UNE ARME. Sa main contient donc deja une arme copiee, qui
// n'appartient a aucun acteur et que le moteur ne connait pas.
//
// On vide donc la main a la creation. Les rangs employes viennent tous de la
// meme enumeration d'`I3D_frame`, dont TROIS points sont confirmes par le jeu
// lui-meme : `SetPos` au rang 3 (les teleportations), `SetName` et `GetName`
// aux rangs 28 et 29 (le moteur a rendu « Soldat 1 »), et `Duplicate` au rang
// 38 (les soldats portent l'uniforme du joueur). Les rangs intermediaires
// s'en deduisent arithmetiquement.
//
// Chaque etape se garde : un resultat nul arrete la sequence pour ce soldat
// sans toucher aux autres.
constexpr std::uintptr_t kFrameNumChildrenVtableOffset = 0x88;   // rang 34
constexpr std::uintptr_t kFrameGetChildVtableOffset = 0x8C;      // rang 35
constexpr std::uintptr_t kFrameFindChildVtableOffset = 0x94;     // rang 37
// V133 - les DRAPEAUX EXACTS du moteur, et pas un de plus.
//
// La V132 passait 0xFFFFFFFF a `FindChildFrame`, en croyant « tout accepter ».
// Le journal a rendu « 0 main(s) videe(s) sur 5 » avec `execute=1` : le code a
// tourne sans incident et n'a rien trouve.
//
// L'en-tete du moteur explique pourquoi :
//
//   #define ENUMF_ALL       0x0ffff
//   #define ENUMF_WILDMASK  0x10000
//
// et `SetGun` emploie exactement `ENUMF_WILDMASK | ENUMF_ALL`, soit 0x1FFFF.
// En posant tous les bits a un, j'activais une quinzaine de filtres inconnus
// au-dela de 0x1FFFF, et la recherche rejetait tout. Ce n'etait donc pas une
// preuve que la main est vide : c'etait une mesure faussee par mes soins.
constexpr std::uint32_t kEnumAllFrames = 0x1FFFFu;

bool EmptySoldierHandsOnGameThread(
    TrainerProcess& process,
    const std::vector<std::uintptr_t>& soldiers,
    std::uintptr_t control_actor)
{
    if (soldiers.empty())
        return false;

    constexpr std::uintptr_t kProcessCheatHookRva = 0x0009'FC10;
    constexpr std::array<std::uint8_t, 5> kExpected{
        0xB8, 0x5C, 0x10, 0x00, 0x00};
    constexpr std::size_t kRemoteSize = 0x1000;
    constexpr std::size_t kCountOffset = 0x200;
    constexpr std::size_t kCompletionOffset = 0x204;
    constexpr std::size_t kEmptiedOffset = 0x208;
    constexpr std::size_t kNameOffset = 0x20C;   // "gun*"
    constexpr std::size_t kFramesOffset = 0x300;
    // V134 - ce que la recherche trouve, frame par frame : 0 = main
    // introuvable, 1 = main trouvee et vide, 2 ou plus = main occupee.
    constexpr std::size_t kResultsOffset = 0x500;

    // V134 - LE SOLDAT DU JOUEUR SERT DE TEMOIN.
    //
    // La V133 a rendu « 0 main(s) videe(s) », et j'ai failli en conclure que
    // la main des copies est vide. Un resultat nul ne prouve pourtant rien
    // tant qu'on n'a pas verifie que la recherche fonctionne : le soldat du
    // joueur TIENT une arme, sa main existe forcement. On le place en derniere
    // position, on le releve, et on ne le touche pas - il ne s'agit pas de lui
    // retirer son arme.
    //
    // Si le temoin rend 0 lui aussi, c'est la RECHERCHE qui est fausse. S'il
    // rend 2 et les copies 0, c'est `Duplicate` qui ne reproduit pas la main.
    const std::uint32_t soldier_count = static_cast<std::uint32_t>(
        (std::min)(soldiers.size(), std::size_t{kMaximumSpawnedSoldiers}));
    const bool has_control = IsSanePointer(control_actor);
    const std::uint32_t count = soldier_count + (has_control ? 1U : 0U);

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        kProcessCheatHookRva >= module.image_size)
    {
        return false;
    }
    const std::uintptr_t hook = module.base_address + kProcessCheatHookRva;
    std::array<std::uint8_t, 5> original{};
    if (!process.ReadMemory(hook, original.data(), original.size()) ||
        original != kExpected)
    {
        return false;
    }
    const std::uintptr_t remote = process.AllocateRemoteMemory(kRemoteSize);
    if (!IsSanePointer(remote))
        return false;

    std::vector<std::uint8_t> code;
    const auto byte = [&](std::uint8_t v) { code.push_back(v); };
    const auto dword = [&](std::uint32_t v)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(v));
        std::memcpy(code.data() + offset, &v, sizeof(v));
    };
    const auto slot = [&](std::size_t offset)
    {
        dword(static_cast<std::uint32_t>(remote + offset));
    };
    std::vector<std::size_t> to_end;
    std::vector<std::size_t> to_next;

    byte(0x9C);                                   // pushfd
    byte(0x60);                                   // pushad
    byte(0x83); byte(0x3D); slot(kCompletionOffset); byte(0x00);
    byte(0x0F); byte(0x85);
    to_end.push_back(code.size()); dword(0);      // jne fin
    byte(0x33); byte(0xF6);                       // xor esi,esi
    const std::size_t loop_start = code.size();
    byte(0xA1); slot(kCountOffset);
    byte(0x3B); byte(0xF0);
    byte(0x0F); byte(0x83);
    to_end.push_back(code.size()); dword(0);      // jae fin

    // ebx = frames[i]
    byte(0x8B); byte(0x1C); byte(0xB5); slot(kFramesOffset);
    byte(0x85); byte(0xDB);
    to_next.push_back(code.size());
    byte(0x0F); byte(0x84); dword(0);             // je suivant

    // eax = frame->FindChildFrame("gun*", ENUMF_ALL)
    byte(0x68); dword(kEnumAllFrames);            // push ENUMF_ALL
    byte(0x68); slot(kNameOffset);                // push "gun*"
    byte(0x53);                                   // push frame
    byte(0x8B); byte(0x0B);                       // mov ecx,[ebx]
    byte(0xFF); byte(0x91); dword(
        static_cast<std::uint32_t>(kFrameFindChildVtableOffset));
    byte(0x85); byte(0xC0);
    to_next.push_back(code.size());
    byte(0x0F); byte(0x84); dword(0);             // je suivant
    byte(0x8B); byte(0xF8);                       // mov edi,eax = main

    // eax = main->NumChildren()
    byte(0x57);
    byte(0x8B); byte(0x0F);
    byte(0xFF); byte(0x91); dword(
        static_cast<std::uint32_t>(kFrameNumChildrenVtableOffset));
    // On enregistre 1 + le nombre d'objets tenus. Zero restera donc la marque
    // d'une main introuvable.
    byte(0x40);                                   // inc eax
    byte(0x89); byte(0x04); byte(0xB5); slot(kResultsOffset);
    byte(0x48);                                   // dec eax
    byte(0x85); byte(0xC0);
    to_next.push_back(code.size());
    byte(0x0F); byte(0x84); dword(0);             // je suivant  (main vide)

    // Le temoin est releve mais jamais touche.
    byte(0xA1); slot(kCountOffset);               // mov eax,[count]
    byte(0x48);                                   // dec eax = dernier indice
    byte(0x3B); byte(0xF0);                       // cmp esi,eax
    to_next.push_back(code.size());
    byte(0x0F); byte(0x84); dword(0);             // je suivant

    // eax = main->GetChild(0)
    byte(0x6A); byte(0x00);
    byte(0x57);
    byte(0x8B); byte(0x0F);
    byte(0xFF); byte(0x91); dword(
        static_cast<std::uint32_t>(kFrameGetChildVtableOffset));
    byte(0x85); byte(0xC0);
    to_next.push_back(code.size());
    byte(0x0F); byte(0x84); dword(0);             // je suivant

    // enfant->LinkTo(NULL, 0)
    byte(0x6A); byte(0x00);
    byte(0x6A); byte(0x00);
    byte(0x50);                                   // push enfant
    byte(0x8B); byte(0x08);                       // mov ecx,[eax]
    byte(0xFF); byte(0x91); dword(
        static_cast<std::uint32_t>(kFrameLinkToVtableOffset));
    byte(0xFF); byte(0x05); slot(kEmptiedOffset); // inc [vides]

    const std::size_t next = code.size();
    for (const std::size_t displacement : to_next)
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            next - (displacement + 2 + sizeof(std::int32_t)));
        std::memcpy(
            code.data() + displacement + 2, &relative, sizeof(relative));
    }
    byte(0x46);                                   // inc esi
    byte(0xE9);
    const std::size_t repeat = code.size();
    dword(0);
    const std::size_t finish = code.size();
    for (const std::size_t displacement : to_end)
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            finish - (displacement + sizeof(std::int32_t)));
        std::memcpy(code.data() + displacement, &relative, sizeof(relative));
    }
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            loop_start - (repeat + sizeof(std::int32_t)));
        std::memcpy(code.data() + repeat, &relative, sizeof(relative));
    }
    byte(0xC7); byte(0x05); slot(kCompletionOffset); dword(1);
    byte(0x61);
    byte(0x9D);
    code.insert(code.end(), original.begin(), original.end());
    byte(0xE9);
    dword(static_cast<std::uint32_t>(
        (hook + original.size()) -
        (remote + code.size() + sizeof(std::int32_t))));

    const std::uint32_t zero = 0;
    const char hand_name[] = "gun*";
    bool written = code.size() < kCountOffset &&
        process.WriteMemory(remote, code.data(), code.size()) &&
        process.WriteMemory(remote + kCountOffset, count) &&
        process.WriteMemory(remote + kCompletionOffset, zero) &&
        process.WriteMemory(remote + kEmptiedOffset, zero) &&
        process.WriteMemory(
            remote + kNameOffset, hand_name, sizeof(hand_name));
    for (std::uint32_t index = 0; written && index < count; ++index)
    {
        const std::uintptr_t actor =
            (index < soldier_count) ? soldiers[index] : control_actor;
        std::uintptr_t frame = 0;
        if (!process.ReadMemory(actor + kActorFrameOffset, frame) ||
            !IsSanePointer(frame))
        {
            frame = 0;
        }
        written =
            process.WriteMemory(
                remote + kFramesOffset + index * sizeof(std::uint32_t),
                static_cast<std::uint32_t>(frame)) &&
            process.WriteMemory(
                remote + kResultsOffset + index * sizeof(std::uint32_t),
                std::uint32_t{0});
    }
    if (!written)
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }
    std::array<std::uint8_t, 5> patch{0xE9, 0, 0, 0, 0};
    const std::int32_t relative =
        static_cast<std::int32_t>(remote - (hook + patch.size()));
    std::memcpy(patch.data() + 1, &relative, sizeof(relative));
    if (!InstallHookSafely(
            process, hook, patch.data(), patch.size(),
            "le saut vers le code pose"))
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }
    const bool triggered = SendInventoryMainThreadTrigger(process);
    std::uint32_t completed = 0;
    if (triggered)
    {
        for (int attempt = 0; attempt < 500 && completed == 0; ++attempt)
        {
            (void)process.ReadMemory(remote + kCompletionOffset, completed);
            if (completed == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    (void)RestoreHookSafely(
        process, hook, original.data(), original.size());
    std::uint32_t emptied = 0;
    (void)process.ReadMemory(remote + kEmptiedOffset, emptied);

    if (completed != 0)
    {
        char found[128] = {};
        int used = 0;
        const std::uint32_t shown = (std::min)(soldier_count, 6U);
        for (std::uint32_t index = 0; index < shown; ++index)
        {
            std::uint32_t result = 0;
            (void)process.ReadMemory(
                remote + kResultsOffset + index * sizeof(std::uint32_t),
                result);
            used += std::snprintf(
                found + used, sizeof(found) - used, "%u ", result);
        }
        std::uint32_t control_result = 0;
        if (has_control)
        {
            (void)process.ReadMemory(
                remote + kResultsOffset +
                    static_cast<std::uintptr_t>(soldier_count) *
                        sizeof(std::uint32_t),
                control_result);
        }
        LogDiagnostic(
            "Mains: soldats crees = %s| VOTRE soldat = %u. "
            "(0 = main introuvable, 1 = main vide, 2+ = main occupee.) "
            "Si votre soldat rend 0 lui aussi, c'est la recherche qui est "
            "fausse, pas la main des copies.",
            found, control_result);
    }

    QueueRemotePageRelease(process, remote, kRemoteSize);
    LogDiagnostic(
        "Mains des soldats: %u main(s) videe(s) sur %u soldat(s) "
        "declenche=%u execute=%u.",
        emptied, soldier_count, triggered ? 1U : 0U, completed);
    return completed != 0;
}

// Rend l'indice d'une case de bandeau LIBRE, ou -1. Une case libre est nulle :
// le moteur la traverse sans effet, ce qui est exactement ce qu'il faut.
int FindFreeMenuSlot(TrainerProcess& process, std::uintptr_t reference_actor)
{
    if (!MeasureGameMenuLayout(process) || !IsSanePointer(reference_actor))
        return -1;
    std::uintptr_t game_menu = 0;
    if (!process.ReadMemory(
            reference_actor + kInventoryBaseOffset, game_menu) ||
        !IsSanePointer(game_menu))
    {
        return -1;
    }
    constexpr int kMaximumPlayers = 4;
    for (int slot = 0; slot < kMaximumPlayers; ++slot)
    {
        std::uintptr_t entry = 0;
        if (!process.ReadMemory(
                game_menu + g_game_menu.pmenu_offset +
                    static_cast<std::uintptr_t>(slot) * sizeof(std::uint32_t),
                entry))
        {
            return -1;
        }
        if (entry == 0)
        {
            LogDiagnostic(
                "Bandeau: case %d libre (pmenu=%08X). Les soldats crees la "
                "prendront : le moteur n'y touche pas, donc plus de squelette "
                "sur vos vrais soldats.",
                slot, static_cast<unsigned>(game_menu));
            return slot;
        }
    }
    LogDiagnostic(
        "Bandeau: aucune case libre (pmenu=%08X, quatre soldats presents).",
        static_cast<unsigned>(game_menu));
    return -1;
}

// Pose toutes les positions en UN SEUL passage sur le thread du jeu.
//
// La V110 appelait `SetFramePositionOnMainThread` une fois par soldat. Chaque
// appel pose un trampoline, declenche, attend l'achevement jusqu'a une
// seconde, puis restaure. Avec douze soldats ou plus, la sequence est longue,
// et certains appels n'aboutissent pas : le soldat concerne reste ou le moteur
// l'avait mis, c'est-a-dire au loin. C'est exactement ce que le joueur
// decrivait - certains a cote, d'autres au loin.
//
// Ici, un seul trampoline, une seule attente, et une boucle qui appelle
// `SetPos` sur chaque frame. Soit tout est place, soit rien ne l'est.
bool SetFramePositionsOnMainThread(
    TrainerProcess& process,
    const std::vector<std::uintptr_t>& frames,
    const std::vector<Vector3>& positions)
{
    if (frames.empty() || frames.size() != positions.size())
        return false;

    constexpr std::uintptr_t kProcessCheatHookRva = 0x0009'FC10;
    constexpr std::array<std::uint8_t, 5> kExpected{
        0xB8, 0x5C, 0x10, 0x00, 0x00};
    constexpr std::size_t kRemoteSize = 0x1000;
    constexpr std::size_t kCountOffset = 0x200;
    constexpr std::size_t kCompletionOffset = 0x204;
    constexpr std::size_t kFramesOffset = 0x300;
    constexpr std::size_t kPositionsOffset = 0x500;

    const std::uint32_t count = static_cast<std::uint32_t>(
        (std::min)(frames.size(), std::size_t{kMaximumSpawnedSoldiers}));

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        kProcessCheatHookRva >= module.image_size)
    {
        return false;
    }
    const std::uintptr_t hook = module.base_address + kProcessCheatHookRva;
    std::array<std::uint8_t, 5> original{};
    if (!process.ReadMemory(hook, original.data(), original.size()) ||
        original != kExpected)
    {
        return false;
    }
    const std::uintptr_t remote = process.AllocateRemoteMemory(kRemoteSize);
    if (!IsSanePointer(remote))
        return false;

    std::vector<std::uint8_t> code;
    const auto byte = [&](std::uint8_t v) { code.push_back(v); };
    const auto dword = [&](std::uint32_t v)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(v));
        std::memcpy(code.data() + offset, &v, sizeof(v));
    };
    const auto slot = [&](std::size_t offset)
    {
        dword(static_cast<std::uint32_t>(remote + offset));
    };
    std::vector<std::size_t> to_end;

    byte(0x9C);                                   // pushfd
    byte(0x60);                                   // pushad
    byte(0x83); byte(0x3D); slot(kCompletionOffset); byte(0x00);
    byte(0x0F); byte(0x85);
    to_end.push_back(code.size()); dword(0);      // jne fin
    byte(0x33); byte(0xF6);                       // xor esi,esi
    const std::size_t loop_start = code.size();
    byte(0xA1); slot(kCountOffset);               // mov eax,[count]
    byte(0x3B); byte(0xF0);                       // cmp esi,eax
    byte(0x0F); byte(0x83);
    to_end.push_back(code.size()); dword(0);      // jae fin
    // edx = &positions[i]
    byte(0x6B); byte(0xD6); byte(0x0C);           // imul edx,esi,12
    byte(0x81); byte(0xC2); slot(kPositionsOffset);
    // ebx = frames[i]
    byte(0x8B); byte(0x1C); byte(0xB5); slot(kFramesOffset);
    byte(0x85); byte(0xDB);                       // test ebx,ebx
    byte(0x74); byte(0x0B);                       // je saut (11 octets)
    byte(0x52);                                   // push &position
    byte(0x53);                                   // push frame
    byte(0x8B); byte(0x0B);                       // mov ecx,[ebx]
    byte(0xFF); byte(0x51);
    byte(static_cast<std::uint8_t>(kFrameSetPositionVtableOffset));
    byte(0x46);                                   // inc esi
    byte(0xE9);
    const std::size_t repeat = code.size();
    dword(0);
    const std::size_t finish = code.size();
    for (const std::size_t displacement : to_end)
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            finish - (displacement + sizeof(std::int32_t)));
        std::memcpy(code.data() + displacement, &relative, sizeof(relative));
    }
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            loop_start - (repeat + sizeof(std::int32_t)));
        std::memcpy(code.data() + repeat, &relative, sizeof(relative));
    }
    byte(0xC7); byte(0x05); slot(kCompletionOffset); dword(1);
    byte(0x61);                                   // popad
    byte(0x9D);                                   // popfd
    code.insert(code.end(), original.begin(), original.end());
    byte(0xE9);
    dword(static_cast<std::uint32_t>(
        (hook + original.size()) -
        (remote + code.size() + sizeof(std::int32_t))));

    const std::uint32_t zero = 0;
    bool written = code.size() < kCountOffset &&
        process.WriteMemory(remote, code.data(), code.size()) &&
        process.WriteMemory(remote + kCountOffset, count) &&
        process.WriteMemory(remote + kCompletionOffset, zero);
    for (std::uint32_t index = 0; written && index < count; ++index)
    {
        written =
            process.WriteMemory(
                remote + kFramesOffset + index * sizeof(std::uint32_t),
                static_cast<std::uint32_t>(frames[index])) &&
            process.WriteMemory(
                remote + kPositionsOffset + index * sizeof(Vector3),
                positions[index]);
    }
    if (!written)
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }
    std::array<std::uint8_t, 5> patch{0xE9, 0, 0, 0, 0};
    const std::int32_t relative =
        static_cast<std::int32_t>(remote - (hook + patch.size()));
    std::memcpy(patch.data() + 1, &relative, sizeof(relative));
    if (!InstallHookSafely(
            process, hook, patch.data(), patch.size(),
            "le saut vers le code pose"))
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }
    const bool triggered = SendInventoryMainThreadTrigger(process);
    std::uint32_t completed = 0;
    if (triggered)
    {
        for (int attempt = 0; attempt < 500 && completed == 0; ++attempt)
        {
            (void)process.ReadMemory(remote + kCompletionOffset, completed);
            if (completed == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    (void)RestoreHookSafely(
        process, hook, original.data(), original.size());
    QueueRemotePageRelease(process, remote, kRemoteSize);
    LogDiagnostic(
        "Placement groupe: %u frame(s) declenche=%u execute=%u.",
        count, triggered ? 1U : 0U, completed);
    return completed != 0;
}

// =====================================================================
// V115 - NOMMER LES SOLDATS CREES
// =====================================================================
//
// Le joueur voit ses soldats s'appeler « unknown ». C'est attendu : le modele
// duplique reprend le nom de la frame source, et le jeu n'a pas d'entree de
// table pour eux. On pose donc un nom lisible sur chaque frame - « Soldat 1 »,
// « Soldat 2 »... - par `I3D_frame::SetName`, rang 28 de l'enumeration ancree
// sur `SetPos` (+0x0C), celle-la meme que la duplication a validee en jeu.
//
//   I3DMETHOD_(void,SetName)(const char *cp);   // rang 28 -> +0x70
//
// La chaine doit vivre dans le processus du jeu : elle est ecrite dans la page
// distante, a cote du code, et l'adresse passee au moteur.
constexpr std::uintptr_t kFrameSetNameVtableOffset = 0x70;   // rang 28
constexpr std::uintptr_t kFrameGetNameVtableOffset = 0x74;   // rang 29

// V122 - les chaines de noms vivent dans une page qui n'est JAMAIS rendue.
//
// La V121 ecrivait les noms dans la page du stub, puis rendait cette page
// aussitot apres. Si `I3D_frame::SetName` retient le pointeur qu'on lui donne
// au lieu de copier la chaine - ce qui est le cas de bien des implementations -
// le nom devenait un pointeur pendant des la page liberee. Le joueur voyait
// donc toujours « unknown », et ma relecture, faite elle aussi apres la remise
// en file de liberation, rendait un pointeur nul : elle ne mesurait pas
// `SetName`, elle mesurait ma propre erreur.
//
// Les noms sont desormais ecrits dans une page dediee, allouee une fois et
// conservee pour toute la duree de la partie. Quelques kilo-octets, et le
// probleme disparait quelle que soit la convention de `SetName`.
struct SoldierNameStorage
{
    DWORD process_id = 0;
    std::uintptr_t page = 0;
};
SoldierNameStorage g_soldier_names{};

constexpr std::size_t kSoldierNameStride = 24;
constexpr std::size_t kSoldierNamePageSize = 0x1000;

std::uintptr_t EnsureSoldierNamePage(TrainerProcess& process)
{
    if (g_soldier_names.page != 0 &&
        g_soldier_names.process_id == process.ProcessId())
    {
        return g_soldier_names.page;
    }
    const std::uintptr_t page =
        process.AllocateRemoteMemory(kSoldierNamePageSize);
    if (!IsSanePointer(page))
        return 0;
    g_soldier_names.process_id = process.ProcessId();
    g_soldier_names.page = page;
    LogDiagnostic(
        "Nom des soldats: page permanente allouee a %08X (jamais rendue, "
        "pour que les chaines survivent).",
        static_cast<unsigned>(page));
    return page;
}

bool NameCreatedSoldiersOnGameThread(
    TrainerProcess& process,
    const std::vector<std::uintptr_t>& soldiers)
{
    if (soldiers.empty())
        return false;

    constexpr std::uintptr_t kProcessCheatHookRva = 0x0009'FC10;
    constexpr std::array<std::uint8_t, 5> kExpected{
        0xB8, 0x5C, 0x10, 0x00, 0x00};
    constexpr std::size_t kRemoteSize = 0x1000;
    constexpr std::size_t kCountOffset = 0x200;
    constexpr std::size_t kCompletionOffset = 0x204;
    constexpr std::size_t kFramesOffset = 0x300;
    constexpr std::size_t kReadBackOffset = 0x900;
    constexpr std::size_t kNameStride = kSoldierNameStride;

    // Les chaines vivent ailleurs, dans une page qui n'est jamais rendue.
    const std::uintptr_t names_page = EnsureSoldierNamePage(process);
    if (names_page == 0)
        return false;

    const std::uint32_t count = static_cast<std::uint32_t>(
        (std::min)(soldiers.size(), std::size_t{kMaximumSpawnedSoldiers}));

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        kProcessCheatHookRva >= module.image_size)
    {
        return false;
    }
    const std::uintptr_t hook = module.base_address + kProcessCheatHookRva;
    std::array<std::uint8_t, 5> original{};
    if (!process.ReadMemory(hook, original.data(), original.size()) ||
        original != kExpected)
    {
        return false;
    }
    const std::uintptr_t remote = process.AllocateRemoteMemory(kRemoteSize);
    if (!IsSanePointer(remote))
        return false;

    std::vector<std::uint8_t> code;
    const auto byte = [&](std::uint8_t v) { code.push_back(v); };
    const auto dword = [&](std::uint32_t v)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(v));
        std::memcpy(code.data() + offset, &v, sizeof(v));
    };
    const auto slot = [&](std::size_t offset)
    {
        dword(static_cast<std::uint32_t>(remote + offset));
    };
    std::vector<std::size_t> to_end;

    byte(0x9C);
    byte(0x60);
    byte(0x83); byte(0x3D); slot(kCompletionOffset); byte(0x00);
    byte(0x0F); byte(0x85);
    to_end.push_back(code.size()); dword(0);
    byte(0x33); byte(0xF6);                       // xor esi,esi
    const std::size_t loop_start = code.size();
    byte(0xA1); slot(kCountOffset);
    byte(0x3B); byte(0xF0);
    byte(0x0F); byte(0x83);
    to_end.push_back(code.size()); dword(0);
    // ebx = frames[i]
    byte(0x8B); byte(0x1C); byte(0xB5); slot(kFramesOffset);
    byte(0x85); byte(0xDB);
    const std::size_t skip = code.size();
    byte(0x74); byte(0x00);                       // je suivant
    // edx = &noms[i], dans la page permanente
    byte(0x6B); byte(0xD6);
    byte(static_cast<std::uint8_t>(kNameStride));  // imul edx,esi,24
    byte(0x81); byte(0xC2);
    dword(static_cast<std::uint32_t>(names_page));
    byte(0x52);                                   // push nom
    byte(0x53);                                   // push frame
    byte(0x8B); byte(0x0B);                       // mov ecx,[ebx]
    byte(0xFF); byte(0x51);
    byte(static_cast<std::uint8_t>(kFrameSetNameVtableOffset));

    // V121 - on RELIT le nom juste apres l'avoir pose.
    //
    // Le joueur rapporte que ses soldats s'appellent toujours « unknown ».
    // `C_actor::GetName()` rend pourtant `frame->GetName()` (H&D.h:885), donc
    // poser le nom sur la frame devrait suffire. Ou bien le rang de `SetName`
    // n'est pas celui que je crois, ou bien le nom affiche vient d'ailleurs.
    // Plutot que de choisir entre les deux au hasard, on demande au moteur ce
    // que la frame s'appelle MAINTENANT, et le journal le dira.
    byte(0x53);                                   // push frame
    byte(0x8B); byte(0x0B);                       // mov ecx,[ebx]
    byte(0xFF); byte(0x51);
    byte(static_cast<std::uint8_t>(kFrameGetNameVtableOffset));
    byte(0x89); byte(0x04); byte(0xB5);
    slot(kReadBackOffset);                        // mov [esi*4+relu],eax

    const std::size_t next = code.size();
    code[skip + 1] = static_cast<std::uint8_t>(next - (skip + 2));
    byte(0x46);
    byte(0xE9);
    const std::size_t repeat = code.size();
    dword(0);
    const std::size_t finish = code.size();
    for (const std::size_t displacement : to_end)
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            finish - (displacement + sizeof(std::int32_t)));
        std::memcpy(code.data() + displacement, &relative, sizeof(relative));
    }
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            loop_start - (repeat + sizeof(std::int32_t)));
        std::memcpy(code.data() + repeat, &relative, sizeof(relative));
    }
    byte(0xC7); byte(0x05); slot(kCompletionOffset); dword(1);
    byte(0x61);
    byte(0x9D);
    code.insert(code.end(), original.begin(), original.end());
    byte(0xE9);
    dword(static_cast<std::uint32_t>(
        (hook + original.size()) -
        (remote + code.size() + sizeof(std::int32_t))));

    const std::uint32_t zero = 0;
    bool written = code.size() < kCountOffset &&
        process.WriteMemory(remote, code.data(), code.size()) &&
        process.WriteMemory(remote + kCountOffset, count) &&
        process.WriteMemory(remote + kCompletionOffset, zero);
    for (std::uint32_t index = 0; written && index < count; ++index)
    {
        std::uintptr_t frame = 0;
        if (!process.ReadMemory(
                soldiers[index] + kActorFrameOffset, frame) ||
            !IsSanePointer(frame))
        {
            frame = 0;
        }
        char name[kNameStride]{};
        (void)std::snprintf(
            name, sizeof(name), "Soldat %u", static_cast<unsigned>(index + 1));
        written =
            process.WriteMemory(
                remote + kFramesOffset + index * sizeof(std::uint32_t),
                static_cast<std::uint32_t>(frame)) &&
            process.WriteMemory(
                names_page + index * kNameStride, name, sizeof(name));
    }
    if (!written)
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }
    std::array<std::uint8_t, 5> patch{0xE9, 0, 0, 0, 0};
    const std::int32_t relative =
        static_cast<std::int32_t>(remote - (hook + patch.size()));
    std::memcpy(patch.data() + 1, &relative, sizeof(relative));
    if (!InstallHookSafely(
            process, hook, patch.data(), patch.size(),
            "le saut vers le code pose"))
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }
    const bool triggered = SendInventoryMainThreadTrigger(process);
    std::uint32_t completed = 0;
    if (triggered)
    {
        for (int attempt = 0; attempt < 500 && completed == 0; ++attempt)
        {
            (void)process.ReadMemory(remote + kCompletionOffset, completed);
            if (completed == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    (void)RestoreHookSafely(
        process, hook, original.data(), original.size());
    // La relecture se fait AVANT de rendre la page : la V121 lisait apres, et
    // ne mesurait donc que sa propre erreur.
    // Ce que le moteur repond, mot pour mot.
    if (completed != 0 && count != 0)
    {
        std::uintptr_t text = 0;
        char readback[32]{};
        if (process.ReadMemory(remote + kReadBackOffset, text) &&
            IsSanePointer(text) &&
            process.ReadMemory(text, readback, sizeof(readback) - 1))
        {
            LogDiagnostic(
                "Nom des soldats: le moteur rend « %s » pour le premier "
                "soldat (attendu « Soldat 1 »).",
                readback);
        }
        else
        {
            LogDiagnostic(
                "Nom des soldats: relecture impossible (pointeur %08X). Le "
                "rang de SetName ou de GetName n'est pas celui attendu.",
                static_cast<unsigned>(text));
        }
    }
    QueueRemotePageRelease(process, remote, kRemoteSize);
    LogDiagnostic(
        "Nom des soldats: %u nomme(s) declenche=%u execute=%u.",
        count, triggered ? 1U : 0U, completed);
    return completed != 0;
}

// =====================================================================
// V115 - LES SOLDATS CREES PORTENT L'ARME DU JOUEUR
// =====================================================================
//
// Le joueur veut que ses soldats portent EXACTEMENT l'arme qu'il tient, et
// qu'ils en changent en meme temps que lui. La touche J coupe le miroir : les
// soldats gardent alors ce qu'ils ont.
//
// Toutes les pieces necessaires etaient deja etablies et validees en jeu :
//
//   inventaire, debut       acteur + 0x5C   (weapon_mods.cpp)
//   inventaire, fin         acteur + 0x60
//   index selectionne       acteur + 0x258
//   objet, identifiant      objet  + 0x08
//   objet, reserve          objet  + 0x18
//   objet, balles           objet  + 0x1C
//
// Il ne manquait que la fonction qui AJOUTE un objet a un inventaire. C'est
// `C_inventory::AddItem(int objet, dword quantite)`, une methode de la classe
// de base `C_inventory`. Deux choses a etablir :
//
//   1. Son adresse chez le joueur. Reconnue par empreinte; les quatre octets
//      qui suivent le premier `A1` sont une adresse absolue, donc ignores.
//
//   2. Le rang de la partie « inventaire » dans un soldat. Dans le binaire de
//      reference, quatre des douze sites qui appellent AddItem la precedent
//      d'un `lea ecx,[objet+0x54]`. Et ce +0x54 se RECOUPE avec ce que le
//      trainer sait deja : le vecteur d'inventaire occupe le rang 8 de
//      `C_inventory`, et 0x54 + 0x08 = 0x5C, exactement l'emplacement que
//      `weapon_mods` emploie depuis des versions et que le jeu valide a chaque
//      utilisation. Les deux mesures, obtenues independamment, concordent.
//
// La selection se fait ensuite par la fonction native que le trainer emploie
// deja pour la rotation d'armes.
constexpr std::uintptr_t kActorInventoryEndOffset = 0x60;
constexpr std::uintptr_t kActorSelectedIndexOffset = 0x258;
constexpr std::uintptr_t kInventoryItemIdOffset = 0x08;
constexpr std::uintptr_t kInventoryItemReserveOffset = 0x18;
constexpr std::uintptr_t kInventoryItemBulletsOffset = 0x1C;
constexpr std::uintptr_t kSetSelectedItemRva = 0x0000'76E0;
constexpr std::array<std::uint8_t, 13> kSetSelectedItemPrologue{
    0x8B, 0x44, 0x24, 0x04, 0x56, 0x8B, 0xF1,
    0x39, 0x86, 0x58, 0x02, 0x00, 0x00};
// Prologue de `C_inventory::AddItem`. Le `A1 xx xx xx xx` d'ouverture charge
// une adresse absolue, qui differe d'un binaire a l'autre : on l'ignore.
constexpr std::array<std::uint8_t, 18> kAddItemBody{
    0x53, 0x55, 0x33, 0xDB, 0x56, 0x83, 0xF8, 0x06, 0x57,
    0x8B, 0xE9, 0x7C, 0x0C, 0x83, 0xF8, 0x09, 0x74, 0x07};

struct AddItemSearch
{
    DWORD process_id = 0;
    bool searched = false;
    ULONGLONG next_retry = 0;
    std::uintptr_t address = 0;
    unsigned candidates = 0;
};
AddItemSearch g_add_item{};

std::uintptr_t FindAddItem(TrainerProcess& process)
{
    if (!process.IsConnected())
        return 0;
    const ULONGLONG now = GetTickCount64();
    if (g_add_item.searched && g_add_item.process_id == process.ProcessId() &&
        (g_add_item.address != 0 || now < g_add_item.next_retry))
    {
        return g_add_item.address;
    }
    const DWORD searched_process = process.ProcessId();
    g_add_item = {};
    g_add_item.process_id = searched_process;
    g_add_item.searched = true;
    g_add_item.next_retry = now + kSignatureRetryDelayMs;

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) || module.image_size == 0 ||
        module.image_size > 64U * 1024U * 1024U)
    {
        return 0;
    }
    std::vector<std::uint8_t> image(module.image_size);
    if (!process.ReadMemory(module.base_address, image.data(), image.size()))
    {
        const std::size_t chunk = 0x10000;
        std::fill(image.begin(), image.end(), std::uint8_t{0});
        for (std::size_t offset = 0; offset < image.size(); offset += chunk)
        {
            const std::size_t length =
                (std::min)(chunk, image.size() - offset);
            (void)process.ReadMemory(
                module.base_address + offset, image.data() + offset, length);
        }
    }
    std::uintptr_t found = 0;
    for (std::size_t offset = 5;
         offset + kAddItemBody.size() <= image.size(); ++offset)
    {
        if (image[offset - 5] != 0xA1)
            continue;
        if (std::memcmp(
                image.data() + offset, kAddItemBody.data(),
                kAddItemBody.size()) != 0)
        {
            continue;
        }
        ++g_add_item.candidates;
        found = module.base_address + offset - 5;
    }
    if (g_add_item.candidates == 1)
    {
        g_add_item.address = found;
        LogDiagnostic(
            "AddItem: TROUVEE a %08X (une seule candidate).",
            static_cast<unsigned>(found));
    }
    else
    {
        LogDiagnostic(
            "AddItem: INTROUVABLE (%u candidates). Le miroir d'arme est "
            "refuse.",
            g_add_item.candidates);
    }
    return g_add_item.address;
}

// =====================================================================
// V119 - L'ETAPE MANQUANTE : CHARGER L'ARME
// =====================================================================
//
// Le moteur, quand il equipe un humain depuis sa table de mission, fait TROIS
// choses, pas deux (Actors.cpp:7360) :
//
//   C_inventory::AddItem(itm, tab->ItemI(TAB_I_HUM_INV_AMOUNT, i));
//   if(!i) SetSelectedInvItem(1, false);
//   C_inventory::Reload(C_inventory::NumItems()-1, true);   // <- celle-ci
//
// `Reload` charge les munitions dans l'arme. Je ne l'appelais pas : je donnais
// une arme VIDE et je demandais au soldat de la tenir. Le journal du joueur
// montrait chaque fois le meme decalage - distribution reussie
// (« execute=1 »), puis arret du jeu quatre cent cinquante millisecondes plus
// tard, au moment ou le moteur s'en sert.
//
// Seconde correction du meme ordre : la QUANTITE. Le moteur passe celle de la
// table du soldat, soit 1 pour une arme. Je passais deux cents a mille - le
// nombre de balles - ce qui revenait a donner deux cents fusils. La quantite
// est desormais 1, et les munitions viennent de `Reload`, comme dans le
// moteur.
constexpr std::array<std::uint8_t, 16> kReloadPrologue{
    0x51, 0x53, 0x8B, 0x5C, 0x24, 0x0C, 0x56, 0x57,
    0x8B, 0xF9, 0x8B, 0x47, 0x08, 0x8B, 0x0C, 0x98};

struct ReloadSearch
{
    DWORD process_id = 0;
    bool searched = false;
    ULONGLONG next_retry = 0;
    std::uintptr_t address = 0;
    unsigned candidates = 0;
};
ReloadSearch g_reload{};

std::uintptr_t FindInventoryReload(TrainerProcess& process)
{
    if (!process.IsConnected())
        return 0;
    const ULONGLONG now = GetTickCount64();
    if (g_reload.searched && g_reload.process_id == process.ProcessId() &&
        (g_reload.address != 0 || now < g_reload.next_retry))
    {
        return g_reload.address;
    }
    const DWORD searched_process = process.ProcessId();
    g_reload = {};
    g_reload.process_id = searched_process;
    g_reload.searched = true;
    g_reload.next_retry = now + kSignatureRetryDelayMs;

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) || module.image_size == 0 ||
        module.image_size > 64U * 1024U * 1024U)
    {
        return 0;
    }
    std::vector<std::uint8_t> image(module.image_size);
    if (!process.ReadMemory(module.base_address, image.data(), image.size()))
    {
        const std::size_t chunk = 0x10000;
        std::fill(image.begin(), image.end(), std::uint8_t{0});
        for (std::size_t offset = 0; offset < image.size(); offset += chunk)
        {
            const std::size_t length =
                (std::min)(chunk, image.size() - offset);
            (void)process.ReadMemory(
                module.base_address + offset, image.data() + offset, length);
        }
    }
    std::uintptr_t found = 0;
    for (std::size_t offset = 0;
         offset + kReloadPrologue.size() + 1 <= image.size(); ++offset)
    {
        if (std::memcmp(
                image.data() + offset, kReloadPrologue.data(),
                kReloadPrologue.size()) != 0)
        {
            continue;
        }
        // Le `A1 <adresse absolue>` qui suit varie d'un binaire a l'autre :
        // on verifie l'opcode, pas l'adresse.
        if (image[offset + kReloadPrologue.size()] != 0xA1)
            continue;
        ++g_reload.candidates;
        found = module.base_address + offset;
    }
    if (g_reload.candidates == 1)
    {
        g_reload.address = found;
        LogDiagnostic(
            "Reload: TROUVEE a %08X (une seule candidate).",
            static_cast<unsigned>(found));
    }
    else
    {
        LogDiagnostic(
            "Reload: INTROUVABLE (%u candidates). Le miroir d'arme est "
            "refuse : donner une arme sans la charger arrete le jeu.",
            g_reload.candidates);
    }
    return g_reload.address;
}

// L'arme que le joueur tient : identifiant, reserve et balles.
struct HeldWeapon
{
    std::int32_t item_id = -1;
    std::int32_t reserve = 0;
    std::int32_t bullets = 0;
};

bool ReadHeldWeapon(
    TrainerProcess& process, std::uintptr_t actor, HeldWeapon& weapon)
{
    weapon = {};
    if (!IsSanePointer(actor))
        return false;
    std::uintptr_t begin = 0;
    std::uintptr_t end = 0;
    std::int32_t selected = -1;
    if (!process.ReadMemory(actor + kActorInventoryBeginOffset, begin) ||
        !process.ReadMemory(actor + kActorInventoryEndOffset, end) ||
        !process.ReadMemory(actor + kActorSelectedIndexOffset, selected) ||
        !IsSanePointer(begin) || end < begin || selected < 0)
    {
        return false;
    }
    const std::uintptr_t count = (end - begin) / sizeof(std::uint32_t);
    if (count == 0 || count > 256 ||
        static_cast<std::uintptr_t>(selected) >= count)
    {
        return false;
    }
    std::uintptr_t item = 0;
    if (!process.ReadMemory(
            begin + static_cast<std::uintptr_t>(selected) * 4U, item) ||
        !IsSanePointer(item) ||
        !process.ReadMemory(item + kInventoryItemIdOffset, weapon.item_id) ||
        !process.ReadMemory(
            item + kInventoryItemReserveOffset, weapon.reserve) ||
        !process.ReadMemory(
            item + kInventoryItemBulletsOffset, weapon.bullets) ||
        weapon.item_id < 0 || weapon.item_id > 4096)
    {
        weapon = {};
        return false;
    }
    return true;
}

// Donne l'arme a chaque soldat indique, puis la lui fait tenir, en UN SEUL
// passage sur le thread du jeu.
//
//   int index = AddItem(objet, quantite);      // ecx = acteur + 0x54
//   if(index >= 0) SetSelectedItem(index);     // ecx = acteur
bool GiveWeaponOnGameThread(
    TrainerProcess& process,
    const std::vector<std::uintptr_t>& soldiers,
    std::uintptr_t add_item,
    std::uintptr_t set_selected_item,
    std::uintptr_t reload,
    std::int32_t item_id,
    std::int32_t amount,
    bool force_equip)
{
    if (soldiers.empty() || !IsSanePointer(add_item) ||
        !IsSanePointer(set_selected_item) || !IsSanePointer(reload) ||
        item_id < 0)
    {
        return false;
    }
    constexpr std::uintptr_t kProcessCheatHookRva = 0x0009'FC10;
    constexpr std::array<std::uint8_t, 5> kExpected{
        0xB8, 0x5C, 0x10, 0x00, 0x00};
    constexpr std::size_t kRemoteSize = 0x1000;
    constexpr std::size_t kCountOffset = 0x200;
    constexpr std::size_t kItemOffset = 0x204;
    constexpr std::size_t kAmountOffset = 0x208;
    constexpr std::size_t kAddItemOffset = 0x20C;
    constexpr std::size_t kSelectOffset = 0x210;
    constexpr std::size_t kReloadOffset = 0x214;
    constexpr std::size_t kCompletionOffset = 0x218;
    // V148 - DEUX TEMOINS, POUR QUE LE JEU DISE OU IL MEURT.
    //
    // Deux versions de suite ont montre exactement la meme signature dans le
    // journal du joueur :
    //
    //   paquet 1 ... resultat=1      <- toujours
    //   paquet 2 ... resultat=0      <- toujours, puis le jeu meurt
    //
    // `resultat=0` veut dire que le drapeau de fin - ecrit par la DERNIERE
    // instruction du code pose - n'est jamais arrive. Autrement dit le jeu
    // entre dans ce code et n'en ressort pas. J'ai suppose deux fois la cause,
    // et deux fois je me suis trompe : d'abord les munitions, puis les acteurs
    // morts. La verification posee en V147 n'a rejete personne, et le jeu est
    // mort quand meme.
    //
    // On arrete de supposer. Le code ecrit desormais, AVANT chaque appel, le
    // rang de l'homme qu'il traite et l'etape ou il en est. Le trainer relit
    // ces deux nombres pendant toute l'attente et retient les derniers vus.
    // Si le jeu meurt, le journal dira l'homme et l'appel exacts.
    constexpr std::size_t kWitnessActorOffset = 0x21C;
    constexpr std::size_t kWitnessPhaseOffset = 0x220;
    constexpr std::uint32_t kPhaseAddItem = 1;
    constexpr std::uint32_t kPhaseReload = 2;
    constexpr std::uint32_t kPhaseSelect = 3;
    constexpr std::uint32_t kPhaseDone = 4;
    constexpr std::size_t kActorsOffset = 0x300;

    const std::uint32_t count = static_cast<std::uint32_t>(
        (std::min)(soldiers.size(), std::size_t{kMaximumSpawnedSoldiers}));

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        kProcessCheatHookRva >= module.image_size)
    {
        return false;
    }
    const std::uintptr_t hook = module.base_address + kProcessCheatHookRva;
    std::array<std::uint8_t, 5> original{};
    if (!process.ReadMemory(hook, original.data(), original.size()) ||
        original != kExpected)
    {
        return false;
    }
    const std::uintptr_t remote = process.AllocateRemoteMemory(kRemoteSize);
    if (!IsSanePointer(remote))
        return false;

    std::vector<std::uint8_t> code;
    const auto byte = [&](std::uint8_t v) { code.push_back(v); };
    const auto dword = [&](std::uint32_t v)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(v));
        std::memcpy(code.data() + offset, &v, sizeof(v));
    };
    const auto slot = [&](std::size_t offset)
    {
        dword(static_cast<std::uint32_t>(remote + offset));
    };
    std::vector<std::size_t> to_end;

    byte(0x9C);                                   // pushfd
    byte(0x60);                                   // pushad
    byte(0x83); byte(0x3D); slot(kCompletionOffset); byte(0x00);
    byte(0x0F); byte(0x85);
    to_end.push_back(code.size()); dword(0);      // jne fin
    byte(0x33); byte(0xF6);                       // xor esi,esi
    const std::size_t loop_start = code.size();
    byte(0xA1); slot(kCountOffset);               // mov eax,[count]
    byte(0x3B); byte(0xF0);                       // cmp esi,eax
    byte(0x0F); byte(0x83);
    to_end.push_back(code.size()); dword(0);      // jae fin
    byte(0x8B); byte(0x1C); byte(0xB5);
    slot(kActorsOffset);                          // mov ebx,[esi*4+acteurs]
    byte(0x85); byte(0xDB);                       // test ebx,ebx
    const std::size_t skip_soldier = code.size();
    byte(0x74); byte(0x00);                       // je suivant

    // V148 - temoin : on note l'homme en cours, puis l'etape avant CHAQUE
    // appel. `eax` est libre ici, et tout le contexte est deja sauve par le
    // `pushad` du debut.
    byte(0x89); byte(0x35); slot(kWitnessActorOffset);   // mov [temoin],esi
    byte(0xC7); byte(0x05); slot(kWitnessPhaseOffset);
    dword(kPhaseAddItem);

    // AddItem(objet, quantite) : ecx = acteur + 0x54
    byte(0xFF); byte(0x35); slot(kAmountOffset);  // push quantite
    byte(0xFF); byte(0x35); slot(kItemOffset);    // push objet
    byte(0x8D); byte(0x4B);
    byte(static_cast<std::uint8_t>(kInventoryBaseOffset));  // lea ecx,[ebx+54]
    byte(0xFF); byte(0x15); slot(kAddItemOffset); // call [AddItem]
    byte(0x85); byte(0xC0);                       // test eax,eax
    const std::size_t skip_select = code.size();
    byte(0x78); byte(0x00);                       // js suivant
    byte(0x8B); byte(0xF8);                       // mov edi,eax = index

    // Reload(index, true) : ecx = acteur + base inventaire.
    // Sans cette etape l'arme est VIDE, et le moteur s'arrete des qu'il s'en
    // sert - c'est le decalage de quatre cent cinquante millisecondes qu'on
    // observait entre la distribution et la mort du jeu.
    byte(0xC7); byte(0x05); slot(kWitnessPhaseOffset);
    dword(kPhaseReload);
    byte(0x6A); byte(0x01);                       // push true
    byte(0x57);                                   // push index
    byte(0x8D); byte(0x4B);
    byte(static_cast<std::uint8_t>(kInventoryBaseOffset));
    byte(0xFF); byte(0x15); slot(kReloadOffset);  // call [Reload]

    // ==================================================================
    // V122 - ON NE FORCE PLUS L'EQUIPEMENT
    // ==================================================================
    //
    // Deux facons de designer l'arme tenue ont ete essayees, et toutes deux
    // ont arrete le jeu quatre cents millisecondes plus tard :
    //
    //   V116/V117  ecriture directe de l'index dans `acteur+0x258`
    //   V118/V119  appel de la fonction native de designation
    //
    // Une troisieme possibilite n'avait JAMAIS ete essayee : ne pas designer
    // du tout. Elle merite de l'etre, et c'est la moins invasive des trois.
    //
    // Le raisonnement est le suivant. Les mises a jour du bandeau sont
    // conditionnees dans le moteur par `if(active)` - or un soldat cree n'est
    // pas actif tant que le joueur ne le pilote pas. Ce n'est donc pas le
    // bandeau qui plante. Ce qui reste, c'est l'EQUIPEMENT lui-meme : mettre
    // l'arme dans la main demande au moteur d'animer un squelette dont
    // l'initialisation d'acteur n'a jamais eu lieu.
    //
    // On se contente donc de ce qui ne demande rien au moteur : l'objet est
    // cree, charge, et depose dans l'inventaire du soldat. Le moteur s'en
    // saisira de lui-meme s'il en est capable. Si le jeu tient, c'est
    // l'equipement force qui etait en cause, et nous l'aurons etabli sans
    // rien casser.
    //
    // L'appel de designation reste ecrit ci-dessous, mais desactive : il
    // suffira de retirer le `if (false)` pour le reprendre.
    // ==================================================================
    // V127 - L'EQUIPEMENT FORCE EST DEFINITIVEMENT ABANDONNE
    // ==================================================================
    //
    // La V126 avait parie que la garde posee sur `SetHealth` et
    // `SetDeathFace` rendrait l'equipement sur : un soldat qui sort une arme
    // fait bouger sa jauge, et sa jauge pointait alors hors du tableau. Le
    // pari etait raisonne, et le journal du joueur l'a dementi sans appel :
    //
    //   Garde bandeau: POSEE.
    //   Creation d'acteurs sur 5 : ... identifiant 1000 pose chez 5 ...
    //   Fenetre armes: arme 1 (objet 30) donnee a 5 soldat(s) resultat=1.
    //   Process: pid=0                                   <- juste apres
    //
    // La garde etait bien en place, et le jeu s'est arrete quand meme. Ce
    // n'est donc PAS le bandeau qui plante, c'est l'equipement lui-meme :
    // mettre l'arme dans la main demande au moteur d'animer un squelette que
    // `MissionLoad` n'a jamais prepare.
    //
    // Cinq facons de faire tenir l'arme ont ete essayees, toutes fatales.
    // Une seule ne l'est pas : ne pas la faire tenir. Le journal du joueur l'a
    // validee quatre fois - l'arme est creee, chargee, deposee, et le jeu
    // tient. C'est celle qui reste, et l'equipement ne sera pas retente avant
    // que l'initialisation d'acteur existe.
    // ==================================================================
    // V133 - L'EQUIPEMENT EST REPRIS, ET VOICI CE QUI A CHANGE
    // ==================================================================
    //
    // Il avait ete abandonne en V127 apres cinq tentatives fatales. Mais
    // TOUTES ces tentatives se sont deroulees dans des conditions qu'on sait
    // maintenant fausses :
    //
    //   V116/V117  base d'inventaire a +0x58 : `AddItem` ecrivait a cote
    //   V118/V119  idem, memoire deja corrompue avant l'equipement
    //   V126       `menu_id` a -1 sur certains soldats
    //
    // Autrement dit, on demandait au moteur d'equiper une arme QUI N'ETAIT
    // JAMAIS ENTREE DANS L'INVENTAIRE - le journal de la V130 l'a prouve :
    // « 1 objet(s), identifiants = 30 ».
    //
    // Les conditions sont aujourd'hui verifiees une par une par le journal du
    // joueur :
    //
    //   - l'arme entre reellement : « 3 objet(s), 30 71 101 »;
    //   - avec ses munitions : « 71(368+20) »;
    //   - la garde du bandeau est posee, `menu_id` vaut 1000 chez tous;
    //   - les acteurs detruits sont purges avant toute ecriture.
    //
    // L'equipement n'a donc jamais ete essaye dans des conditions saines. Ce
    // n'est pas une supposition de plus : c'est le premier essai valable.
    // V134 - COUPE DE NOUVEAU, ET CETTE FOIS ON SAIT POURQUOI.
    //
    // L'essai de la V133 a REUSSI la ou tous les autres avaient echoue : le
    // journal du joueur montre `selectionne=1`, ses soldats tenaient
    // reellement l'arme 71 avec ses 368 munitions. Puis le jeu s'est arrete.
    //
    // Le lien est dans la meme page de journal :
    //
    //   Mains des soldats: 0 main(s) videe(s) sur 5 soldat(s) execute=1.
    //
    // La main n'est pas trouvee. Or `SetGun` commence par
    //
    //   PI3D_frame hand = model->FindChildFrame("gun*", ...);
    //   if(!hand) return;                    // <- il abandonne ici
    //
    // et `SetSelectedInvItem` a DEJA pose `selected_inv_item = indx` avant de
    // l'appeler. Le soldat declare donc tenir une arme dont aucun modele n'a
    // ete attache, et le moteur la dereference au tour suivant.
    //
    // Ce n'est plus une hypothese : l'equipement ne peut aboutir que si la
    // main est trouvee. Tant que ce n'est pas le cas, l'activer revient a
    // poser un etat que le moteur ne peut pas soutenir.
    // V142 - LA SORTIE D'ARME REDEVIENT POSSIBLE, MAIS PAS POUR TOUT LE MONDE.
    //
    // Elle a ete coupee parce qu'elle tuait le jeu sur les soldats CREES : un
    // acteur sans fiche ne peut pas soutenir l'etat « je tiens une arme ».
    //
    // Un ennemi rallie, lui, est un acteur que le JEU a fabrique en chargeant
    // la mission : fiche complete, posture, animations. Lui demander de sortir
    // une arme est une operation ordinaire du moteur - il le fait de lui-meme
    // des qu'il change d'arme.
    //
    // La distinction n'est donc pas un pari : elle porte exactement sur ce qui
    // a ete etabli comme la cause. L'appelant decide, soldat par soldat.
    if (force_equip)
    {
    byte(0x8B); byte(0xC7);                       // mov eax,edi = index
    //
    // La V116 ecrivait l'index choisi directement dans `acteur+0x258`, en
    // croyant retirer une inconnue. Le journal du joueur a montre que c'etait
    // l'inverse : le jeu ne mourait pas pendant la distribution
    // (« execute=1, 4 arme(s) designee(s) ») mais QUATRE CENTS MILLISECONDES
    // APRES, au moment de l'affichage. La declaration de la classe explique
    // pourquoi :
    //
    //   void Tick(int time){
    //      for(int i=items.size(); i--; )
    //         items[i]->model->Tick(time);
    //   }
    //
    // `S_item::model` est un pointeur vers le modele 3D de l'objet, et il
    // reste NUL tant que `LoadModels` n'a pas tourne. En designant l'arme par
    // une simple ecriture, on demandait au moteur d'afficher un objet sans
    // modele : il partait le chercher au tour suivant et s'arretait.
    //
    // La fonction native, elle, fait ce travail. C'est precisement sa raison
    // d'etre, et c'est le seul chemin correct.
    byte(0xC7); byte(0x05); slot(kWitnessPhaseOffset);
    dword(kPhaseSelect);
    // ==============================================================
    // V151 - L'ERREUR QUI A COUTE HUIT VERSIONS : UN ARGUMENT MANQUANT
    // ==============================================================
    //
    // `SetSelectedInvItem` prend DEUX arguments, pas un :
    //
    //   void SetSelectedInvItem(int indx, bool net_send = true)
    //
    // La valeur par defaut est posee par l'appelant en C++; dans le binaire la
    // fonction en attend bel et bien deux. Son desassemblage sur le binaire du
    // joueur ne laisse aucun doute :
    //
    //   004076E0  8b 44 24 04   mov eax,[esp+4]     ; indx
    //             39 86 58 02   cmp [esi+0x258],eax
    //             ...
    //   +0x1AD    c2 08 00      ret 8               ; DEPILE HUIT OCTETS
    //
    // Or on n'en empilait QU'UN. A chaque appel la pile remontait donc de
    // quatre octets de trop - en plein dans les registres sauves par le
    // `pushad` du debut. Au `popad`, le jeu restaurait des valeurs prises a
    // cote, et repartait vers une adresse quelconque.
    //
    // Cela explique tout ce qui a resiste huit versions durant :
    //
    //   - parfois le jeu survit, parfois il meurt : cela depend de ce que les
    //     registres ramassent, et c'est du hasard pur;
    //   - le temoin de la V148 rapportait « jamais entre dans le code » parce
    //     que le processus etait deja detruit quand on relisait;
    //   - et les huit echecs sur les soldats CREES avaient la meme cause. Ce
    //     n'etait pas leur fiche manquante : c'etait notre appel.
    //
    // Le second argument vaut FAUX : sans lui, le moteur emettrait en plus un
    // message reseau bati sur une valeur ramassee au hasard sur la pile.
    byte(0x6A); byte(0x00);                       // push net_send = false
    byte(0x50);                                   // push index
    byte(0x8B); byte(0xCB);                       // mov ecx,acteur
    byte(0xFF); byte(0x15); slot(kSelectOffset);  // call [SetSelectedItem]
    }
    byte(0xC7); byte(0x05); slot(kWitnessPhaseOffset);
    dword(kPhaseDone);

    const std::size_t next_soldier = code.size();
    code[skip_soldier + 1] = static_cast<std::uint8_t>(
        next_soldier - (skip_soldier + 2));
    code[skip_select + 1] = static_cast<std::uint8_t>(
        next_soldier - (skip_select + 2));
    byte(0x46);                                   // inc esi
    byte(0xE9);
    const std::size_t repeat = code.size();
    dword(0);
    const std::size_t finish = code.size();
    for (const std::size_t displacement : to_end)
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            finish - (displacement + sizeof(std::int32_t)));
        std::memcpy(code.data() + displacement, &relative, sizeof(relative));
    }
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            loop_start - (repeat + sizeof(std::int32_t)));
        std::memcpy(code.data() + repeat, &relative, sizeof(relative));
    }
    byte(0xC7); byte(0x05); slot(kCompletionOffset); dword(1);
    byte(0x61);                                   // popad
    byte(0x9D);                                   // popfd
    code.insert(code.end(), original.begin(), original.end());
    byte(0xE9);
    dword(static_cast<std::uint32_t>(
        (hook + original.size()) -
        (remote + code.size() + sizeof(std::int32_t))));

    const std::uint32_t zero = 0;
    bool written = code.size() < kCountOffset &&
        process.WriteMemory(remote, code.data(), code.size()) &&
        process.WriteMemory(remote + kCountOffset, count) &&
        process.WriteMemory(
            remote + kItemOffset, static_cast<std::uint32_t>(item_id)) &&
        process.WriteMemory(
            remote + kAmountOffset, static_cast<std::uint32_t>(amount)) &&
        process.WriteMemory(
            remote + kAddItemOffset,
            static_cast<std::uint32_t>(add_item)) &&
        process.WriteMemory(
            remote + kSelectOffset,
            static_cast<std::uint32_t>(set_selected_item)) &&
        process.WriteMemory(
            remote + kReloadOffset, static_cast<std::uint32_t>(reload)) &&
        process.WriteMemory(remote + kCompletionOffset, zero) &&
        process.WriteMemory(remote + kWitnessActorOffset, zero) &&
        process.WriteMemory(remote + kWitnessPhaseOffset, zero);
    for (std::uint32_t index = 0; written && index < count; ++index)
    {
        written = process.WriteMemory(
            remote + kActorsOffset + index * sizeof(std::uint32_t),
            static_cast<std::uint32_t>(soldiers[index]));
    }
    if (!written)
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }
    std::array<std::uint8_t, 5> patch{0xE9, 0, 0, 0, 0};
    const std::int32_t relative =
        static_cast<std::int32_t>(remote - (hook + patch.size()));
    std::memcpy(patch.data() + 1, &relative, sizeof(relative));
    if (!InstallHookSafely(
            process, hook, patch.data(), patch.size(),
            "le saut vers le code pose"))
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }
    const bool triggered = SendInventoryMainThreadTrigger(process);
    std::uint32_t completed = 0;
    // V148 - on suit la progression a l'interieur du code pose. Les derniers
    // temoins LUS avec succes sont conserves : si le jeu meurt, ils disent
    // l'homme et l'appel exacts sur lesquels il s'est arrete.
    std::uint32_t last_actor = 0;
    std::uint32_t last_phase = 0;
    bool witness_seen = false;
    unsigned reads_ok = 0;
    unsigned reads_failed = 0;
    int first_failure_attempt = -1;
    if (triggered)
    {
        for (int attempt = 0; attempt < 500 && completed == 0; ++attempt)
        {
            (void)process.ReadMemory(remote + kCompletionOffset, completed);
            std::uint32_t witness_actor = 0;
            std::uint32_t witness_phase = 0;
            // V150 - on distingue « rien a lire » de « lecture impossible ».
            //
            // La V148 confondait les deux : un temoin a zero et un processus
            // qui ne repond plus donnaient la meme ligne de journal. Or ce
            // n'est pas du tout la meme chose - dans un cas le jeu tourne et
            // n'entre pas dans le code, dans l'autre il est deja mort.
            const bool read_ok =
                process.ReadMemory(
                    remote + kWitnessActorOffset, witness_actor) &&
                process.ReadMemory(
                    remote + kWitnessPhaseOffset, witness_phase);
            if (read_ok)
            {
                ++reads_ok;
                if (witness_phase != 0)
                {
                    last_actor = witness_actor;
                    last_phase = witness_phase;
                    witness_seen = true;
                }
            }
            else
            {
                ++reads_failed;
                if (first_failure_attempt < 0)
                    first_failure_attempt = attempt;
            }
            if (completed == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    if (completed == 0)
    {
        static const char* const kPhaseNames[] = {
            "aucune - le jeu n'est jamais entre dans le code",
            "AddItem (deposer l'objet)",
            "Reload (charger l'arme)",
            "SetSelectedInvItem (la SORTIR)",
            "termine pour cet homme"};
        const std::uint32_t phase =
            last_phase < 5 ? last_phase : 0;
        LogDiagnostic(
            "TEMOIN: le code pose n'a pas rendu la main. Dernier point vu : "
            "homme n%u sur %u (adresse %08X), etape %u = %s. Temoin lu : %s. "
            "Lectures reussies=%u, echouees=%u, premiere echouee au essai %d "
            "(soit %d ms apres le declenchement). %s",
            last_actor + 1, count,
            static_cast<unsigned>(
                last_actor < count ? soldiers[last_actor] : 0),
            last_phase, kPhaseNames[phase],
            witness_seen ? "oui" : "non",
            reads_ok, reads_failed, first_failure_attempt,
            first_failure_attempt < 0 ? -1 : first_failure_attempt * 2,
            reads_failed != 0
                ? "Le processus a cesse de repondre PENDANT l'attente : le jeu "
                  "est mort a ce moment-la, pas plus tard."
                : "Le processus a repondu jusqu'au bout : le jeu etait vivant "
                  "et n'est simplement jamais entre dans le code.");
    }
    (void)RestoreHookSafely(
        process, hook, original.data(), original.size());
    QueueRemotePageRelease(process, remote, kRemoteSize);
    // La designation est faite par le moteur, dans le passage ci-dessus.
    // On relit simplement, pour le journal, combien de soldats tiennent
    // effectivement le nouvel objet.
    unsigned holding = 0;
    if (completed != 0)
    {
        for (std::uint32_t index = 0; index < count; ++index)
        {
            std::uintptr_t begin = 0;
            std::uintptr_t end = 0;
            std::int32_t selected_index = -1;
            if (!process.ReadMemory(
                    soldiers[index] + kActorInventoryBeginOffset, begin) ||
                !process.ReadMemory(
                    soldiers[index] + kActorInventoryEndOffset, end) ||
                !process.ReadMemory(
                    soldiers[index] + kActorSelectedIndexOffset,
                    selected_index) ||
                !IsSanePointer(begin) || end <= begin || selected_index < 0)
            {
                continue;
            }
            const std::int32_t last = static_cast<std::int32_t>(
                (end - begin) / sizeof(std::uint32_t)) - 1;
            if (selected_index == last)
                ++holding;
        }
    }
    // V129 - on RELIT l'inventaire de chaque soldat, et on le journalise.
    //
    // Le joueur dit que ses soldats ne prennent pas l'arme choisie. Le trainer
    // affirmait « resultat=1 », mais cela ne prouvait que l'execution du code,
    // pas son effet. On lit donc ce que les soldats ont VRAIMENT, objet par
    // objet, au lieu de le supposer. Sans cette mesure, toute correction sur
    // l'equipement serait encore un coup dans le noir.
    if (completed != 0)
    {
        const std::uint32_t reported = (std::min)(count, 3U);
        for (std::uint32_t index = 0; index < reported; ++index)
        {
            std::uintptr_t begin = 0;
            std::uintptr_t end = 0;
            std::int32_t selected = -1;
            if (!process.ReadMemory(
                    soldiers[index] + kActorInventoryBeginOffset, begin) ||
                !process.ReadMemory(
                    soldiers[index] + kActorInventoryEndOffset, end) ||
                !process.ReadMemory(
                    soldiers[index] + kActorSelectedIndexOffset, selected) ||
                !IsSanePointer(begin) || end < begin)
            {
                LogDiagnostic(
                    "Inventaire du soldat %u : illisible.",
                    static_cast<unsigned>(index + 1));
                continue;
            }
            const std::uintptr_t items =
                (end - begin) / sizeof(std::uint32_t);
            char list[160] = {};
            int used = 0;
            for (std::uintptr_t entry = 0; entry < items && entry < 12;
                 ++entry)
            {
                std::uintptr_t item = 0;
                std::int32_t id = -1;
                std::int32_t reserve = -1;
                std::int32_t bullets = -1;
                if (process.ReadMemory(
                        begin + entry * sizeof(std::uint32_t), item) &&
                    IsSanePointer(item) &&
                    process.ReadMemory(item + kInventoryItemIdOffset, id) &&
                    process.ReadMemory(
                        item + kInventoryItemReserveOffset, reserve) &&
                    process.ReadMemory(
                        item + kInventoryItemBulletsOffset, bullets))
                {
                    // On dit aussi les munitions : c'est ce que le joueur voit
                    // a l'ecran, et c'est ce qui manquait pour comprendre.
                    used += std::snprintf(
                        list + used, sizeof(list) - used, "%d(%d+%d) ", id,
                        reserve, bullets);
                }
            }
            LogDiagnostic(
                "Inventaire du soldat %u : %u objet(s), selectionne=%d, "
                "identifiants = %s(l'arme demandee etait %d).",
                static_cast<unsigned>(index + 1),
                static_cast<unsigned>(items), selected, list, item_id);
        }
    }
    LogDiagnostic(
        "Miroir d'arme: objet %d x%d DEPOSE ET CHARGE chez %u soldat(s) "
        "declenche=%u execute=%u (%u l'ont deja en main). L'equipement n'est "
        "PAS force sur cette version.",
        item_id, amount, count, triggered ? 1U : 0U, completed, holding);
    return completed != 0;
}



// =====================================================================
// V147 - UN RALLIE MORT N'EST PLUS UNE CIBLE : LA VRAIE CAUSE DE L'ARRET
// =====================================================================
//
// Le journal du joueur date l'arret a la milliseconde, et cette fois il
// designe la file elle-meme :
//
//   13:29:35.980  Sortie d'arme: paquet de 4 homme(s) ... resultat=1
//   13:29:37.534  Sortie d'arme: paquet de 4 homme(s) ... resultat=0
//   13:29:37.550  Process: pid=0
//
// `resultat=0` ne veut pas dire « refuse ». Il veut dire que le jeu est ENTRE
// dans le code pose par le trainer et n'en est jamais ressorti : le drapeau de
// fin n'a jamais ete ecrit. Seize millisecondes plus tard, le processus a
// disparu.
//
// La chronologie donne le reste :
//
//   13:28:42  cinquante ennemis rallies
//   13:29:01  envoyes au combat par la carte
//   13:29:08  fenetre G ouverte  <- DERNIER nettoyage de la liste
//   13:29:35  distribution d'arme, vingt-sept secondes plus tard
//
// Or `CollectMissionEnemies` - la seule fonction qui retire de la liste les
// rallies disparus - n'est appelee QUE lorsque la fenetre G est ouverte. Elle
// se trouve apres la garde qui rend la main quand la fenetre est fermee.
//
// Pendant ces vingt-sept secondes, cinquante hommes se battaient. Certains
// sont morts, et le moteur a rendu leur memoire. Le trainer, lui, gardait
// leurs adresses. Le premier paquet est tombe sur des vivants; le second sur
// un mort, et `AddItem` a ecrit dans une memoire qui ne lui appartenait plus.
//
// CE N'EST DONC PAS LA QUANTITE DE MUNITIONS. Le joueur le supposait, et le
// moteur dit le contraire (`Inventory.cpp:360`) :
//
//   int C_inventory::AddItem(int itm, dword num){
//      if(b){ ... items[i]->amount += num; return i; }   // un simple entier
//      S_item *it = new S_item(itm, num);                // UN seul objet
//
// `num` est range dans un champ, il ne cree pas `num` objets. Et le PREMIER
// paquet a reussi avec exactement la meme valeur, 978.
//
// La correction : on ne touche plus jamais un acteur sans l'avoir verifie
// juste avant, et la liste est nettoyee a chaque image, fenetre ouverte ou non.
bool IsRalliedActorUsable(
    TrainerProcess& process,
    const RemoteModuleInfo& module,
    std::uintptr_t actor)
{
    if (!IsSanePointer(actor) || module.image_size == 0)
        return false;
    // La table des methodes est le signal le plus sur : celle d'un acteur
    // vivant pointe forcement dans le code du jeu. Une memoire rendue puis
    // reutilisee ne presente presque jamais cette forme.
    std::uintptr_t vtable = 0;
    if (!process.ReadMemory(actor, vtable) ||
        vtable < module.base_address ||
        vtable >= module.base_address + module.image_size)
    {
        return false;
    }
    std::uint32_t type = 0;
    if (!process.ReadMemory(actor + kActorTypeOffset, type) ||
        type != kActorTypeEnemy)
    {
        return false;
    }
    // Et un mort n'a rien a faire d'une arme.
    std::int32_t resistance = 0;
    if (!process.ReadMemory(actor + kPlayerResistanceOffset, resistance) ||
        resistance <= 0)
    {
        return false;
    }
    return true;
}

// Nettoie la liste des rallies. Appelee a CHAQUE image, fenetre ouverte ou non.
void PruneRalliedEnemies(TrainerProcess& process)
{
    if (g_rallied_enemies.empty() || !process.IsConnected())
        return;
    static ULONGLONG next_prune = 0;
    const ULONGLONG now = GetTickCount64();
    if (now < next_prune)
        return;
    next_prune = now + 250;

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module))
        return;
    const std::size_t before = g_rallied_enemies.size();
    g_rallied_enemies.erase(
        std::remove_if(
            g_rallied_enemies.begin(), g_rallied_enemies.end(),
            [&](std::uintptr_t actor)
            { return !IsRalliedActorUsable(process, module, actor); }),
        g_rallied_enemies.end());
    if (g_rallied_enemies.size() != before)
    {
        LogDiagnostic(
            "Rallies: %u homme(s) retire(s) de la liste (morts ou disparus); "
            "il en reste %u.",
            static_cast<unsigned>(before - g_rallied_enemies.size()),
            static_cast<unsigned>(g_rallied_enemies.size()));
    }
    // V154 - celui qu'on pilotait a la main ne doit pas rester designe s'il
    // vient de quitter la liste.
    if (g_free_move_actor != 0 && !IsRalliedEnemy(g_free_move_actor))
        ClearFreeMove("il n'est plus dans vos rallies");
}


// =====================================================================
// V158 - OUVRIR LE FEU TOUT DE SUITE, SANS ATTENDRE QU'ILS SE VOIENT
// =====================================================================
//
// Le joueur : « normalement des que ces ennemis sont de mon cote ils doivent
// directement, en temps reel, tirer sur les autres ».
//
// Le camp est bon - le journal le prouve : les votres sont au groupe 3, ceux
// d'en face au groupe 0, et `GetRelation` rend bien RELATION_ENEMY. Ce qui
// manque n'est pas l'hostilite, c'est le DECLENCHEMENT.
//
// Le moteur ne fait tirer un homme que lorsqu'il en a VU un autre. Voici la
// chaine, telle qu'elle est ecrite (Actors.cpp:3917 et 4906) :
//
//   WatchHumans()  -> ne retient que ceux qui sont a portee, DANS LE CONE DE
//                     VISION, et avec une ligne de vue degagee;
//   la visibilite s'accumule dans le temps (`seen_amount`);
//   AI_solution()  -> si un ennemi est SEEN, alors seulement :
//                     AddProgram(0, PRG_ATTACK, S_prg_add((dword)first_seen, ...));
//
// Or vos rallies et leurs anciens camarades se tournent le dos : ils etaient
// du meme cote une seconde plus tot, et rien ne leur donne de raison de se
// retourner. Deux groupes statiques qui ne se regardent pas ne se verront
// jamais. C'est pour cela qu'il ne se passait rien.
//
// On pose donc nous-memes l'ordre que l'IA se serait donne. C'est exactement
// le meme appel, avec exactement la meme forme :
//
//   AddProgram(0, PRG_ATTACK, S_prg_add((dword)cible, partie_du_corps), true)
//
// `S_prg_add` est un simple `dword d[5]` (H&D.h:556); pour PRG_ATTACK,
// `d[0]` porte la cible et `d[1]` la partie du corps visee (1 = la poitrine,
// celle que l'IA prend par defaut).
//
// L'ordre est INSERE EN TETE, comme le fait l'IA - on ne vide pas leur
// programme ici : une fois la cible abattue, ils reprennent ce qu'ils
// faisaient, et leur IA prend le relais puisqu'ils se seront vus.
// V160 - ON LES FAIT REGARDER, ON NE LEUR ORDONNE PLUS DE TIRER.
//
// La V158 posait `PRG_ATTACK`. Le journal du joueur montre `execute=1` - l'ordre
// arrivait bien - et pourtant personne ne tirait. La raison est dans
// l'execution de cet ordre (Actors.cpp:9581) :
//
//   fire_dist = GetType()==ACTOR_ENEMY ? AI_f(TAB_F_AI_MIN_SHOOT_DIST) : ...;
//   if(dist >= fire_dist){
//      if(MayHunt(sub_pos, key) && subject_seen){ ... il court vers lui ... }
//      del = true;                 // <-- SINON L'ORDRE EST SUPPRIME
//      break;
//   }
//
// Tout depend de `subject_seen`. Un ordre d'attaque sur une cible que l'homme
// n'a pas encore VUE est jete a la premiere image. Et plus haut, l'attaque
// exige aussi `if(!gun && !gun_mode) break;` - il faut tenir une arme.
//
// Le bon ordre est donc `PRG_WATCH` (Actors.cpp:10290), qui fait exactement ce
// qui manquait :
//
//   - il fait TOURNER l'homme vers un point et l'y fait regarder, avec un
//     balayage autour;
//   - il appelle `AI_solution` a chaque image avec une vigilance elevee
//     (`TAB_F_GAME_WATCH_CARE_WATCH`), ce qui accelere l'accumulation de
//     visibilite dans `WatchHumans`.
//
// Des que la cible est vue, c'est l'IA ELLE-MEME qui ajoute son `PRG_ATTACK`,
// par le chemin normal du jeu. On ne force plus rien : on leur donne seulement
// la raison de se retourner, qui est precisement ce qui leur manquait.
// V161 - REGARDER NE SUFFIT PAS : A CENT METRES, PERSONNE NE VOIT PERSONNE.
//
// La V160 les faisait se tourner vers l'ennemi le plus proche. Cela n'a rien
// change, et le journal du joueur dit pourquoi en une ligne :
//
//   Ralliement: ... le premier est a 104 metres de vous.
//
// Or la portee de vision est un reglage de mission - « Watch distance » dans
// la table d'IA (Actors.cpp:2209) - et `WatchHumans` s'en sert comme d'une
// borne stricte :
//
//   float watch_test_range = AI_f(TAB_F_AI_WATCH_MAX_DIST);
//   ...
//   if(((*a->GetPos()) - t.pos).Magnitude() < t.watch_test_range){
//      ... on l'ajoute a la liste de surveillance ...
//   }
//
// Au-dela, l'homme n'est meme pas AJOUTE a la liste. Se tourner vers un point
// situe a cent metres ne sert donc a rien : il n'y a personne a voir.
//
// Le moteur lui-meme resout ce cas, et d'une seule facon (Actors.cpp:9711) :
// quand il perd sa cible de vue, il ajoute un DEPLACEMENT vers elle, en
// courant, avec la raison « atteindre pour attaquer » :
//
//   AddProgram(++i, PRG_MOVE, S_prg_add((dword)&sub_pos, true, MR_ATTACK_REACH), true);
//
// C'est exactement cet ordre qu'on pose. Ils avancent vers l'ennemi le plus
// proche; des qu'ils entrent dans la portee de vision, ils le voient, et leur
// IA ajoute l'attaque elle-meme. On ne force toujours rien : on leur donne la
// seule chose qui manquait, la distance.
// V162 - LE DEPLACEMENT SEUL ETAIT MALFORME.
//
// Le joueur : « dans la V153 les ennemis de mon equipe frappaient les autres
// ennemis ». C'est vrai, et la raison est simple : EN V153 LE TRAINER NE LEUR
// AJOUTAIT AUCUN ORDRE. Ils gardaient le programme de la mission, leur IA
// tournait normalement, et quand un ennemi entrait dans leur champ ils
// l'attaquaient d'eux-memes.
//
// Mes ajouts des V158, V160 et V161 ont casse cela. Le dernier en particulier :
// je posais un `PRG_MOVE` avec la raison `MR_ATTACK_REACH` TOUT SEUL. Or le
// moteur ne s'en sert jamais seul - il l'insere TOUJOURS devant un
// `PRG_ATTACK` deja present (Actors.cpp:9711) :
//
//   if(stay_mode!=SM_STAY) AddProgram(++i, PRG_STAY_UP, S_prg_add());
//   AddProgram(++i, PRG_MOVE, S_prg_add((dword)&sub_pos, true, MR_ATTACK_REACH), true);
//
// Et le traitement de ce deplacement lit le programme SUIVANT, qu'il suppose
// etre l'attaque (Actors.cpp:9360) :
//
//   LPC_actor subject = next_key.GetSubject();
//   ...
//   del = 2;   //remove MOVE and ATTACK
//
// Un deplacement `MR_ATTACK_REACH` sans attaque derriere est donc un ordre
// incomplet : il interroge un programme qui n'est pas celui qu'il attend.
//
// On pose desormais LE COUPLE, dans l'ordre exact du moteur : d'abord
// l'attaque, puis le deplacement insere devant elle. La liste devient
// [MOVE, ATTACK, ...] - ils courent vers l'ennemi, et l'attaque prend le
// relais des qu'ils l'ont en vue.
constexpr std::uint8_t kProgramReachItem = 0;   // PRG_MOVE
constexpr std::uint8_t kProgramAttackItem = 4;  // PRG_ATTACK
constexpr std::uint32_t kMoveRun = 1;
constexpr std::uint32_t kMoveReasonAttackReach = 3;   // MR_ATTACK_REACH
constexpr std::uint32_t kAttackBodyPartBrest = 1;


// =====================================================================
// V163 - POURQUOI SEULS QUELQUES-UNS PARTAIENT
// =====================================================================
//
// Le joueur : « pourquoi juste quelques ennemis, pas tous, partent ? »
//
// Le journal montre pourtant « 10 homme(s) recoivent le COUPLE » : le trainer
// n'en ecarte aucun. C'est le JEU qui refuse pour certains, et la condition est
// dans `MayHunt` (Actors.cpp:3787) :
//
//   case ACTOR_ENEMY:
//      if(AI_e(TAB_E_AI_HUNT)==2){                     // (1) poursuite permise ?
//         const S_vector &origin_pos = program.back()->dest;   // (2) son poste
//         float dist = |notre_position - origin_pos|;
//         if(dist < (AI_f(TAB_F_AI_HUNT_MAX_DIST)-.5f)) return true;  // (3) rayon
//         ...
//      }
//      break;
//   return false;                                       // sinon : il ne bouge pas
//
// Trois conditions, toutes tirees des REGLAGES DE MISSION de cet homme :
//
//   1. sa mission doit l'autoriser a poursuivre (`Hunt enemy... = Yes`);
//   2. on mesure sa distance a son POSTE d'origine;
//   3. elle doit rester sous son rayon de poursuite.
//
// Beaucoup d'ennemis de mission sont des gardes statiques : leur reglage dit
// « ne poursuis pas ». Ceux-la abandonnaient l'ordre et restaient sur place.
// Ce n'etait donc ni un bug du trainer, ni un plafond que j'aurais pose.
//
// CE QUI REND LA CORRECTION POSSIBLE
//
// `AI_e` et `AI_f` ne lisent pas une table separee : ils lisent LA FICHE DE
// L'ACTEUR, celle-la meme ou nous ecrivons deja son camp (Actors.cpp:4855) :
//
//   byte AI_e(dword indx) const{
//      byte ret = tab->ItemE(indx);                     // la fiche de l'acteur
//      if(!ret) ret = mission.tab_property->ItemE(indx);
//      if(!ret) ret = tab_game_cfg->ItemE(indx);
//      return ret;
//   }
//
// Il suffit donc d'y ecrire deux nombres pour qu'un rallie ait le droit de
// poursuivre, et loin. Et Tables.h reserve explicitement les proprietes 96 a
// 127 a l'IA dans cette meme fiche.
constexpr std::uint32_t kAiHuntProperty = 96;         // TAB_E_AI_HUNT
constexpr std::uint32_t kAiHuntMaxDistProperty = 97;  // TAB_F_AI_HUNT_MAX_DIST
constexpr std::uint32_t kAiWatchMaxDistProperty = 98; // TAB_F_AI_WATCH_MAX_DIST
constexpr std::uint8_t kAiYes = 2;                    // ENUM_AI_NOYES : Yes
constexpr float kAllyHuntDistance = 500.0f;
constexpr float kAllyWatchDistance = 120.0f;

// Rend un rallie capable de poursuivre, et de voir plus loin.
//
// On n'ecrit que ce qui existe : si la propriete est absente de son modele de
// fiche, `ResolveActorTableInteger` rend zero et l'on n'y touche pas.
unsigned FreeAllyAiLimits(
    TrainerProcess& process,
    const std::vector<std::uintptr_t>& allies,
    std::uintptr_t table_offset)
{
    if (table_offset == 0)
        return 0;
    unsigned freed = 0;
    unsigned missing = 0;
    for (const std::uintptr_t ally : allies)
    {
        const std::uintptr_t hunt = ResolveActorTableInteger(
            process, ally, kAiHuntProperty, table_offset);
        const std::uintptr_t hunt_dist = ResolveActorTableInteger(
            process, ally, kAiHuntMaxDistProperty, table_offset);
        const std::uintptr_t watch_dist = ResolveActorTableInteger(
            process, ally, kAiWatchMaxDistProperty, table_offset);
        if (hunt == 0 || hunt_dist == 0)
        {
            ++missing;
            continue;
        }
        bool ok = process.WriteMemory(hunt, kAiYes) &&
            process.WriteMemory(hunt_dist, kAllyHuntDistance);
        // La portee de vision est un supplement : s'il n'a pas la propriete,
        // ce n'est pas une raison de renoncer au reste.
        if (watch_dist != 0)
            (void)process.WriteMemory(watch_dist, kAllyWatchDistance);
        if (ok)
            ++freed;
    }
    LogDiagnostic(
        "Reglages d'IA: %u allie(s) autorises a poursuivre (propriete %u = "
        "oui), rayon de poursuite porte a %.0f m et portee de vision a %.0f m; "
        "%u sans ces proprietes dans leur fiche. Sans cela, un garde statique "
        "de mission refuse de quitter son poste - c'est pourquoi seuls "
        "certains partaient.",
        freed, kAiHuntProperty, kAllyHuntDistance, kAllyWatchDistance,
        missing);
    return freed;
}

// Pour chaque rallie, la position de l'ennemi encore hostile le plus proche.
void BuildOpenFirePairs(
    std::vector<std::uintptr_t>& watchers,
    std::vector<std::uintptr_t>& subjects,
    std::vector<Vector3>& destinations,
    const std::vector<std::uintptr_t>* only);
bool OrderEngageOnGameThread(
    TrainerProcess& process,
    const std::vector<std::uintptr_t>& watchers,
    const std::vector<std::uintptr_t>& subjects,
    const std::vector<Vector3>& destinations);

// =====================================================================
// V164 - FEU ALL : ILS ENCHAINENT, JUSQU'AU DERNIER
// =====================================================================
//
// Le joueur : « des qu'il attaque un, il part vers un autre plus proche, etc.
// jusqu'a ce qu'ils les terminent tous ».
//
// La difficulte n'est pas de repeter l'ordre - c'est de ne pas l'empiler.
// `AddProgram(0, ...)` INSERE en tete : redonner l'ordre toutes les deux
// secondes a tout le monde ferait grossir sans fin la liste des programmes de
// chaque homme, et le dernier ordre pose masquerait celui d'avant sans jamais
// le retirer.
//
// On ne redonne donc un objectif qu'a CELUI DONT LA CIBLE EST TOMBEE. Le
// trainer retient qui vise qui; a chaque passage il regarde quelles cibles ne
// figurent plus parmi les ennemis encore hostiles - mortes, ralliees a leur
// tour, ou disparues - et ne renvoie que les hommes concernes.
//
// Tant que chacun a une cible vivante, RIEN N'EST ENVOYE : le mode ne coute
// alors aucune ecriture dans le jeu.
std::map<std::uintptr_t, std::uintptr_t> g_fire_all_target{};

void StopFireAll(GameplaySettings& settings, const char* why)
{
    if (!settings.fire_all_enabled)
        return;
    settings.fire_all_enabled = false;
    g_fire_all_target.clear();
    LogDiagnostic("Feu continu: arrete (%s).", why);
}

void UpdateFireAll(TrainerProcess& process, GameplaySettings& settings)
{
    if (settings.fire_all_toggle_requested)
    {
        settings.fire_all_toggle_requested = false;
        if (settings.fire_all_enabled)
        {
            StopFireAll(settings, "vous l'avez arrete");
        }
        else
        {
            settings.fire_all_enabled = true;
            g_fire_all_target.clear();
            LogDiagnostic(
                "Feu continu: ARME. Chaque allie prend l'ennemi le plus proche "
                "de lui, et des que celui-la tombe il repart sur le suivant, "
                "jusqu'au dernier.");
        }
    }
    if (!settings.fire_all_enabled || !process.IsConnected())
        return;
    if (g_rallied_enemies.empty())
    {
        StopFireAll(settings, "vous n'avez plus d'allie rallie");
        return;
    }
    // Un passage toutes les deux secondes suffit : c'est le temps qu'il faut
    // pour qu'un homme parcoure quelques metres, et cela evite de solliciter
    // le jeu sans raison.
    static ULONGLONG next_pass = 0;
    const ULONGLONG now = GetTickCount64();
    if (now < next_pass)
        return;
    next_pass = now + 2000;

    const std::vector<std::uintptr_t> hostile = EnemiesStillHostile();
    if (hostile.empty())
    {
        StopFireAll(settings, "il n'y a plus d'ennemi en face");
        return;
    }

    // Qui a besoin d'un nouvel objectif ?
    std::vector<std::uintptr_t> idle;
    for (const std::uintptr_t ally : g_rallied_enemies)
    {
        const auto assigned = g_fire_all_target.find(ally);
        const bool has_live_target =
            assigned != g_fire_all_target.end() &&
            std::find(hostile.begin(), hostile.end(), assigned->second) !=
                hostile.end();
        if (!has_live_target)
            idle.push_back(ally);
    }
    if (idle.empty())
        return;   // tout le monde a une cible vivante : rien a faire

    std::vector<std::uintptr_t> watchers;
    std::vector<std::uintptr_t> subjects;
    std::vector<Vector3> destinations;
    BuildOpenFirePairs(watchers, subjects, destinations, &idle);
    if (watchers.empty())
        return;

    std::vector<std::uintptr_t> donors = g_soldier_menu;
    const std::uintptr_t offset =
        MeasureActorTableOffset(process, donors, g_reference_enemies);
    if (offset != 0)
        (void)FreeAllyAiLimits(process, watchers, offset);
    const bool sent = OrderEngageOnGameThread(
        process, watchers, subjects, destinations);
    if (sent)
    {
        for (std::size_t i = 0; i < watchers.size(); ++i)
            g_fire_all_target[watchers[i]] = subjects[i];
    }
    LogDiagnostic(
        "Feu continu: %u allie(s) sans cible vivante repartent sur le plus "
        "proche, resultat=%u. %u autre(s) poursuivent la leur.",
        static_cast<unsigned>(watchers.size()), sent ? 1U : 0U,
        static_cast<unsigned>(g_rallied_enemies.size() - watchers.size()));
}

// Pour chaque rallie, la position de l'ennemi encore hostile le plus proche.
//
// Les positions viennent du releve fait a chaque image par
// `CollectMissionEnemies`, donc sans lecture supplementaire.
void BuildOpenFirePairs(
    std::vector<std::uintptr_t>& watchers,
    std::vector<std::uintptr_t>& subjects,
    std::vector<Vector3>& destinations,
    const std::vector<std::uintptr_t>* only = nullptr)
{
    watchers.clear();
    subjects.clear();
    destinations.clear();
    const std::vector<std::uintptr_t> hostile = EnemiesStillHostile();
    // V164 - `only` restreint le calcul a quelques hommes : le feu continu
    // ne renvoie que ceux dont la cible est tombee.
    const std::vector<std::uintptr_t>& pool =
        only != nullptr ? *only : g_rallied_enemies;
    if (hostile.empty() || pool.empty())
        return;
    const auto position_of = [&](std::uintptr_t actor, Vector3& out)
    {
        for (std::size_t i = 0; i < g_mission_enemies.size(); ++i)
        {
            if (g_mission_enemies[i] == actor &&
                i < g_mission_enemy_positions.size())
            {
                out = g_mission_enemy_positions[i];
                return true;
            }
        }
        return false;
    };
    for (const std::uintptr_t ally : pool)
    {
        Vector3 from{};
        if (!position_of(ally, from))
            continue;
        bool found = false;
        Vector3 best{};
        std::uintptr_t best_actor = 0;
        float best_distance = 0.0f;
        for (const std::uintptr_t foe : hostile)
        {
            Vector3 to{};
            if (!position_of(foe, to))
                continue;
            const float dx = to.x - from.x;
            const float dy = to.y - from.y;
            const float dz = to.z - from.z;
            const float d = dx * dx + dy * dy + dz * dz;
            if (!found || d < best_distance)
            {
                found = true;
                best = to;
                best_actor = foe;
                best_distance = d;
            }
        }
        if (found)
        {
            watchers.push_back(ally);
            subjects.push_back(best_actor);
            destinations.push_back(best);
        }
    }
}

bool OrderEngageOnGameThread(
    TrainerProcess& process,
    const std::vector<std::uintptr_t>& watchers,
    const std::vector<std::uintptr_t>& subjects,
    const std::vector<Vector3>& destinations)
{
    if (watchers.empty() || watchers.size() != destinations.size() ||
        watchers.size() != subjects.size() || !process.IsConnected())
    {
        return false;
    }
    constexpr std::uintptr_t kProcessCheatHookRva = 0x0009'FC10;
    constexpr std::array<std::uint8_t, 5> kExpected{
        0xB8, 0x5C, 0x10, 0x00, 0x00};
    constexpr std::size_t kRemoteSize = 0x1000;
    constexpr std::size_t kPrgAttackOffset = 0x300;
    constexpr std::size_t kPrgMoveOffset = 0x320;
    constexpr std::size_t kCompletionOffset = 0x330;
    constexpr std::size_t kCountOffset = 0x340;
    constexpr std::size_t kAttackersOffset = 0x400;
    constexpr std::size_t kSubjectsOffset = 0x600;
    constexpr std::size_t kDestinationsOffset = 0x800;
    constexpr std::size_t kMaximumPairs =
        (kSubjectsOffset - kAttackersOffset) / sizeof(std::uint32_t);
    // Les destinations tiennent chacune douze octets, a la suite.
    static_assert(sizeof(Vector3) == 12, "Vector3 doit faire douze octets");

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        kProcessCheatHookRva >= module.image_size)
    {
        return false;
    }
    std::vector<std::uint32_t> a_table;
    std::vector<std::uint32_t> s_table;
    std::vector<Vector3> d_table;
    const std::size_t room =
        (kRemoteSize - kDestinationsOffset) / sizeof(Vector3);
    for (std::size_t i = 0;
         i < watchers.size() && a_table.size() < kMaximumPairs &&
         a_table.size() < room;
         ++i)
    {
        if (!IsOrderableActor(process, module, watchers[i]) ||
            !IsOrderableActor(process, module, subjects[i]))
        {
            continue;
        }
        a_table.push_back(static_cast<std::uint32_t>(watchers[i]));
        s_table.push_back(static_cast<std::uint32_t>(subjects[i]));
        d_table.push_back(destinations[i]);
    }
    if (a_table.empty())
    {
        LogDiagnostic(
            "Ouverture du feu: aucun homme valide; rien n'est ordonne.");
        return false;
    }
    const std::uintptr_t hook = module.base_address + kProcessCheatHookRva;
    std::array<std::uint8_t, 5> original{};
    if (!process.ReadMemory(hook, original.data(), original.size()) ||
        original != kExpected)
    {
        return false;
    }
    const std::uintptr_t remote = process.AllocateRemoteMemory(kRemoteSize);
    if (!IsSanePointer(remote))
        return false;

    std::vector<std::uint8_t> code;
    const auto byte = [&](std::uint8_t v) { code.push_back(v); };
    const auto dword = [&](std::uint32_t v)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(v));
        std::memcpy(code.data() + offset, &v, sizeof(v));
    };
    const auto slot = [&](std::size_t offset)
    {
        dword(static_cast<std::uint32_t>(remote + offset));
    };
    std::vector<std::size_t> to_end;

    byte(0x9C);                              // pushfd
    byte(0x60);                              // pushad
    byte(0x33); byte(0xF6);                  // xor esi,esi
    const std::size_t loop_start = code.size();
    byte(0xA1); slot(kCountOffset);          // mov eax,[nombre]
    byte(0x3B); byte(0xF0);                  // cmp esi,eax
    byte(0x0F); byte(0x83);
    to_end.push_back(code.size()); dword(0); // jae fin
    byte(0x8B); byte(0x1C); byte(0xB5);
    slot(kAttackersOffset);                  // mov ebx,[esi*4+attaquants]
    byte(0x85); byte(0xDB);                  // test ebx,ebx
    const std::size_t skip = code.size();
    byte(0x74); byte(0x00);                  // je suivant
    // 1. L'ATTAQUE D'ABORD. Elle se retrouvera derriere le deplacement,
    //    puisque celui-ci sera insere en tete juste apres.
    byte(0x8B); byte(0x04); byte(0xB5);
    slot(kSubjectsOffset);                   // mov eax,[esi*4+cibles]
    byte(0xA3); slot(kPrgAttackOffset);      // mov [prg_attaque+0],eax
    byte(0x6A); byte(0x01);                  // push net_send = true
    byte(0x68); slot(kPrgAttackOffset);      // push &S_prg_add (attaque)
    byte(0x6A); byte(kProgramAttackItem);    // push PRG_ATTACK
    byte(0x6A); byte(0x00);                  // push pos = 0, en tete
    byte(0x8B); byte(0xCB);                  // mov ecx,homme
    byte(0x8B); byte(0x03);                  // mov eax,[homme]
    byte(0xFF); byte(0x50);
    byte(static_cast<std::uint8_t>(kActorAddProgramVtableOffset));

    // 2. PUIS LE DEPLACEMENT, insere DEVANT l'attaque. La liste devient
    //    [MOVE, ATTACK, ...], exactement la forme que le moteur emploie.
    //    L'adresse de la destination de cet homme : base + rang * douze.
    byte(0x8B); byte(0xC6);                  // mov eax,esi
    byte(0x8D); byte(0x04); byte(0x40);      // lea eax,[eax+eax*2]  (rang*3)
    byte(0xC1); byte(0xE0); byte(0x02);      // shl eax,2            (rang*12)
    byte(0x05); slot(kDestinationsOffset);   // add eax,base des destinations
    byte(0xA3); slot(kPrgMoveOffset);        // mov [prg_deplacement+0],eax
    byte(0x6A); byte(0x01);                  // push net_send = true
    byte(0x68); slot(kPrgMoveOffset);        // push &S_prg_add (deplacement)
    byte(0x6A); byte(kProgramReachItem);     // push PRG_MOVE
    byte(0x6A); byte(0x00);                  // push pos = 0, en tete
    byte(0x8B); byte(0xCB);                  // mov ecx,homme
    byte(0x8B); byte(0x03);                  // mov eax,[homme]
    byte(0xFF); byte(0x50);
    byte(static_cast<std::uint8_t>(kActorAddProgramVtableOffset));

    const std::size_t next = code.size();
    code[skip + 1] = static_cast<std::uint8_t>(next - (skip + 2));
    byte(0x46);                              // inc esi
    byte(0xE9);
    const std::size_t repeat = code.size();
    dword(0);
    const std::size_t finish = code.size();
    for (const std::size_t displacement : to_end)
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            finish - (displacement + sizeof(std::int32_t)));
        std::memcpy(code.data() + displacement, &relative, sizeof(relative));
    }
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            loop_start - (repeat + sizeof(std::int32_t)));
        std::memcpy(code.data() + repeat, &relative, sizeof(relative));
    }
    byte(0xC7); byte(0x05); slot(kCompletionOffset); dword(1);
    byte(0x61);                              // popad
    byte(0x9D);                              // popfd
    code.insert(code.end(), original.begin(), original.end());
    byte(0xE9);
    dword(static_cast<std::uint32_t>(
        (hook + original.size()) -
        (remote + code.size() + sizeof(std::int32_t))));

    std::array<std::uint32_t, 5> prg_attack{};
    prg_attack[0] = 0;                       // rempli par le code, par homme
    prg_attack[1] = kAttackBodyPartBrest;
    std::array<std::uint32_t, 5> prg_move{};
    prg_move[0] = 0;                         // rempli par le code, par homme
    prg_move[1] = kMoveRun;                  // en courant
    prg_move[2] = kMoveReasonAttackReach;    // pour aller au contact
    const std::uint32_t zero = 0;
    bool ok = code.size() < kPrgAttackOffset &&
        process.WriteMemory(remote, code.data(), code.size()) &&
        process.WriteMemory(
            remote + kPrgAttackOffset, prg_attack.data(),
            prg_attack.size() * sizeof(prg_attack[0])) &&
        process.WriteMemory(
            remote + kPrgMoveOffset, prg_move.data(),
            prg_move.size() * sizeof(prg_move[0])) &&
        process.WriteMemory(remote + kCompletionOffset, zero) &&
        process.WriteMemory(
            remote + kCountOffset,
            static_cast<std::uint32_t>(a_table.size())) &&
        process.WriteMemory(
            remote + kAttackersOffset, a_table.data(),
            a_table.size() * sizeof(std::uint32_t)) &&
        process.WriteMemory(
            remote + kSubjectsOffset, s_table.data(),
            s_table.size() * sizeof(std::uint32_t)) &&
        process.WriteMemory(
            remote + kDestinationsOffset, d_table.data(),
            d_table.size() * sizeof(Vector3));
    if (!ok)
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }
    std::array<std::uint8_t, 5> patch{0xE9, 0, 0, 0, 0};
    const std::int32_t relative =
        static_cast<std::int32_t>(remote - (hook + patch.size()));
    std::memcpy(patch.data() + 1, &relative, sizeof(relative));
    if (!InstallHookSafely(
            process, hook, patch.data(), patch.size(),
            "le saut vers le code pose"))
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }
    const bool triggered = SendInventoryMainThreadTrigger(process);
    std::uint32_t completed = 0;
    if (triggered)
    {
        for (int attempt = 0; attempt < 500 && completed == 0; ++attempt)
        {
            (void)process.ReadMemory(remote + kCompletionOffset, completed);
            if (completed == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    RestoreHookSafely(process, hook, original.data(), original.size());
    QueueRemotePageRelease(process, remote, kRemoteSize);
    LogDiagnostic(
        "Engagement: %u homme(s) recoivent le COUPLE [deplacement, attaque] "
        "vers leur ennemi le plus proche - exactement la forme du moteur - "
        "declenche=%u execute=%u.",
        static_cast<unsigned>(a_table.size()), triggered ? 1U : 0U, completed);
    return completed != 0;
}

// =====================================================================
// V156 - LA TOUCHE DE SWITCH CONTINUE SUR LES ALLIES RALLIES
// =====================================================================
//
// Le joueur : « il y a la touche qui permet le switch au suivant. Je ne veux
// pas modifier la logique de 1 2 3 4, mais pour l'autre touche : des que je
// termine mes camarades je passe aux ennemis qui sont avec moi, je peux les
// deplacer via noclip, switch je passe au suivant par ordre de nombres, et
// quand je les termine je reviens a mes soldats d'origine. »
//
// LE TRAINER N'INTERCEPTE AUCUNE TOUCHE. Il n'en a pas besoin, et cela evite
// de toucher aux touches 1 2 3 4 comme le joueur l'a demande.
//
// Il OBSERVE simplement quel soldat est actif. Le jeu, lui, fait son travail
// habituel : `PlayerSwitch` passe au suivant et boucle sur le premier quand il
// arrive au bout. C'est ce BOUCLAGE - passer du dernier soldat au premier -
// qui sert de signal : a ce moment-la, au lieu de laisser le joueur repartir
// sur ses propres hommes, on pose la camera sur le premier allie rallie et on
// le lui met en main.
//
// Chaque nouveau changement de soldat actif fait avancer d'un allie. Apres le
// dernier, on rend la main et le jeu reprend son cycle normal.
//
// Une precaution : appuyer sur « 1 » alors qu'on est sur le dernier soldat
// produirait le meme bouclage apparent. On ignore donc le signal si un chiffre
// vient d'etre frappe.
struct SwitchCycleState
{
    DWORD process_id = 0;
    std::uintptr_t last_active = 0;
    int last_index = -1;
    int ally_index = -1;      // -1 = on est sur ses propres soldats
    ULONGLONG last_digit_at = 0;
};

SwitchCycleState g_switch_cycle{};

void NoteSwitchCycleDigit()
{
    g_switch_cycle.last_digit_at = GetTickCount64();
}

void LeaveSwitchCycle(TrainerProcess& process, std::uintptr_t own_soldier)
{
    if (g_switch_cycle.ally_index < 0)
        return;
    g_switch_cycle.ally_index = -1;
    ClearFreeMove("le cycle est revenu a vos soldats");
    if (IsSanePointer(own_soldier))
    {
        LogDiagnostic(
            "Switch etendu: fin du tour des allies, retour a vos soldats.");
        (void)FocusCameraOnActor(process, own_soldier);
    }
}

void UpdateSwitchCycle(
    TrainerProcess& process,
    const RadarSnapshot* snapshot,
    GameplaySettings& settings)
{
    if (settings.switch_cycle_toggle_requested)
    {
        settings.switch_cycle_toggle_requested = false;
        settings.switch_cycle_enabled = !settings.switch_cycle_enabled;
        LogDiagnostic(
            "Switch etendu: %s.",
            settings.switch_cycle_enabled
                ? "la touche de switch continuera sur vos allies rallies"
                : "la touche de switch reste aux seuls soldats du jeu");
        if (!settings.switch_cycle_enabled && snapshot)
            LeaveSwitchCycle(process, snapshot->player_object_address);
    }
    if (!settings.switch_cycle_enabled || !process.IsConnected() ||
        !snapshot || !IsSanePointer(snapshot->player_object_address))
    {
        return;
    }
    if (g_switch_cycle.process_id != process.ProcessId())
    {
        g_switch_cycle = {};
        g_switch_cycle.process_id = process.ProcessId();
    }

    // V157 - LES SOLDATS DANS L'ORDRE DU JEU, ET NON DE LA MEMOIRE.
    //
    // Le journal du joueur ne portait aucune ligne « vos N soldats sont
    // passes » : le bouclage n'etait jamais reconnu.
    //
    // La cause : `CollectLocalPlayers` rend les soldats dans l'ordre de la
    // liste d'acteurs du moteur, alors que `PlayerSwitch` les TRIE par
    // `menu_id` avant de les faire defiler (`GameMission.cpp:3062`) :
    //
    //   slist.Add(plrs[i], plrs[i]->GetMenuID());
    //   slist.Sort();
    //
    // Mon test « du dernier au premier » portait donc sur un ordre qui n'etait
    // pas celui du jeu, et ne pouvait pratiquement jamais correspondre.
    std::vector<std::uintptr_t> own;
    CollectLocalPlayers(process, *snapshot, own);
    own.erase(
        std::remove_if(
            own.begin(), own.end(),
            [&](std::uintptr_t actor)
            {
                std::uint32_t owner = 0;
                return std::find(
                           g_spawned_soldiers.begin(),
                           g_spawned_soldiers.end(), actor) !=
                        g_spawned_soldiers.end() ||
                    !process.ReadMemory(actor + 0x34U, owner) || owner != 0;
            }),
        own.end());
    if (own.empty())
        return;
    // On trie comme le jeu : par identifiant de bandeau, croissant.
    {
        const std::uintptr_t menu_id_offset =
            MeasurePlayerMenuIdOffset(process);
        if (menu_id_offset != 0)
        {
            std::stable_sort(
                own.begin(), own.end(),
                [&](std::uintptr_t a, std::uintptr_t b)
                {
                    std::int32_t ia = 0;
                    std::int32_t ib = 0;
                    if (!process.ReadMemory(a + menu_id_offset, ia))
                        ia = 0;
                    if (!process.ReadMemory(b + menu_id_offset, ib))
                        ib = 0;
                    return ia < ib;
                });
        }
    }

    const std::uintptr_t active = snapshot->player_object_address;
    int index = -1;
    for (std::size_t i = 0; i < own.size(); ++i)
    {
        if (own[i] == active)
        {
            index = static_cast<int>(i);
            break;
        }
    }
    if (index < 0)
        return;

    const bool changed = (active != g_switch_cycle.last_active);
    const int previous = g_switch_cycle.last_index;
    g_switch_cycle.last_active = active;
    g_switch_cycle.last_index = index;
    if (!changed || previous < 0)
        return;

    // Un chiffre vient d'etre frappe : c'est un choix direct, pas un switch.
    const ULONGLONG now = GetTickCount64();
    if (now - g_switch_cycle.last_digit_at < 400)
        return;

    if (g_switch_cycle.ally_index >= 0)
    {
        // On etait deja sur un allie : on passe au suivant.
        const int next = g_switch_cycle.ally_index + 1;
        if (next >= static_cast<int>(g_rallied_enemies.size()))
        {
            LeaveSwitchCycle(process, active);
            return;
        }
        g_switch_cycle.ally_index = next;
        g_free_move_actor = g_rallied_enemies[next];
        LogDiagnostic(
            "Switch etendu: allie %d sur %u (%08X). Camera posee sur lui; le "
            "vol le deplace.",
            next + 1, static_cast<unsigned>(g_rallied_enemies.size()),
            static_cast<unsigned>(g_free_move_actor));
        (void)FocusCameraOnActor(process, g_free_move_actor);
        return;
    }

    // Bouclage du jeu : du dernier soldat au premier. C'est le signal.
    const bool wrapped =
        previous == static_cast<int>(own.size()) - 1 && index == 0 &&
        own.size() > 1;
    if (!wrapped || g_rallied_enemies.empty())
        return;
    g_switch_cycle.ally_index = 0;
    g_free_move_actor = g_rallied_enemies.front();
    LogDiagnostic(
        "Switch etendu: vos %u soldats sont passes, on continue sur les "
        "allies rallies. Allie 1 sur %u (%08X). Camera posee sur lui; le vol "
        "le deplace.",
        static_cast<unsigned>(own.size()),
        static_cast<unsigned>(g_rallied_enemies.size()),
        static_cast<unsigned>(g_free_move_actor));
    (void)FocusCameraOnActor(process, g_free_move_actor);
}

// =====================================================================
// V146 - LA SORTIE D'ARME PASSE PAR UNE FILE, ET NE SE REPETE PAS
// =====================================================================
//
// Le journal du joueur montre le jeu qui s'arrete deux secondes apres une
// distribution d'arme, et il en donne la cause a la milliseconde pres :
//
//   13:21:12.981  Miroir d'arme: objet 14 ... chez 25 soldat(s)
//                 (25 l'ont deja en main)
//   13:21:12.981  Fenetre armes: 25 allie(s) rallie(s) recoivent l'arme 14
//                 ET la sortent ... resultat=1
//   13:21:15.029  Process: pid=0
//
// TROIS choses dans la meme milliseconde :
//
//   1. le miroir d'arme et la fenetre J ont distribue TOUS LES DEUX, sur les
//      memes vingt-cinq hommes, dans la meme image;
//   2. chacun a pose son propre detour sur le site de triche, coup sur coup;
//   3. et les vingt-cinq AVAIENT DEJA cette arme en main - le journal le dit
//      lui-meme : « 25 l'ont deja en main », « selectionne=4 » sur l'objet 14.
//
// Autrement dit, on a demande au moteur de recharger cinquante modeles d'arme
// pour rien. `C_human::SetGun` n'est pas une ecriture : il libere le modele
// courant, en cree un neuf par `driver->CreateModel()`, le charge par
// `model_cache.Open(...)` et l'accroche a la main. Cinquante fois de suite,
// depuis un detour, dans une seule image.
//
// La correction porte sur les trois points a la fois :
//
//   - une SEULE file d'attente, alimentee par le miroir comme par la fenetre
//     J : deux demandes ne peuvent plus se chevaucher;
//   - elle est traitee par petits paquets, un par image au plus, avec un
//     repos entre deux : le moteur ne charge plus qu'une poignee de modeles a
//     la fois;
//   - et l'on SAUTE tout homme qui tient deja l'arme demandee, ce qui, dans le
//     cas du journal ci-dessus, aurait supprime la totalite du travail.
// V148 - UN SEUL HOMME A LA FOIS, ET ON S'ARRETE AU PREMIER ECHEC.
//
// Le journal du joueur montre deux fois la meme chose : le premier paquet
// aboutit, le second tue le jeu. Tant que la cause n'est pas etablie, deux
// precautions s'imposent.
//
// D'abord un homme a la fois : le temoin pose plus haut designera alors un
// acteur unique, sans ambiguite. Ensuite l'arret immediat de la file des que
// le code pose ne rend pas la main - continuer reviendrait a frapper un jeu
// qui vient peut-etre de mourir.
constexpr std::size_t kEquipBatchSize = 1;
constexpr ULONGLONG kEquipBatchRestMs = 400;

struct PendingEquipState
{
    DWORD process_id = 0;
    std::vector<std::uintptr_t> waiting;
    std::int32_t item_id = 0;
    std::int32_t amount = 0;
    ULONGLONG next_at = 0;
    unsigned equipped = 0;
    unsigned already = 0;
    unsigned failed = 0;
    unsigned gone = 0;      // morts ou detruits pendant l'attente
};

PendingEquipState g_pending_equip{};

// Met une troupe en file. Une demande nouvelle remplace la precedente : c'est
// toujours la derniere volonte du joueur qui compte.
void QueueEquipRequest(
    TrainerProcess& process,
    const std::vector<std::uintptr_t>& actors,
    std::int32_t item_id,
    std::int32_t amount)
{
    if (actors.empty() || item_id < 0)
        return;
    g_pending_equip = {};
    g_pending_equip.process_id = process.ProcessId();
    g_pending_equip.waiting = actors;
    g_pending_equip.item_id = item_id;
    g_pending_equip.amount = amount;
    g_pending_equip.next_at = GetTickCount64();
    LogDiagnostic(
        "Sortie d'arme: %u homme(s) mis en file pour l'objet %d, par paquets "
        "de %u avec %llu ms de repos. Ceux qui la tiennent deja seront sautes.",
        static_cast<unsigned>(actors.size()), item_id,
        static_cast<unsigned>(kEquipBatchSize),
        static_cast<unsigned long long>(kEquipBatchRestMs));
}

// Traite au plus un paquet par image.
void ProcessPendingEquip(TrainerProcess& process)
{
    if (!process.IsConnected() ||
        g_pending_equip.process_id != process.ProcessId())
    {
        if (g_pending_equip.process_id != 0 &&
            g_pending_equip.process_id != process.ProcessId())
        {
            g_pending_equip = {};
        }
        return;
    }
    if (g_pending_equip.waiting.empty())
    {
        if (g_pending_equip.equipped != 0 || g_pending_equip.already != 0 ||
            g_pending_equip.gone != 0 || g_pending_equip.failed != 0)
        {
            LogDiagnostic(
                "Sortie d'arme: termine pour l'objet %d - %u homme(s) l'ont "
                "sortie, %u la tenaient deja (sautes), %u sont morts ou ont "
                "disparu pendant l'attente, %u n'ont pas repondu.",
                g_pending_equip.item_id, g_pending_equip.equipped,
                g_pending_equip.already, g_pending_equip.gone,
                g_pending_equip.failed);
            const DWORD keep = g_pending_equip.process_id;
            g_pending_equip = {};
            g_pending_equip.process_id = keep;
        }
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (now < g_pending_equip.next_at)
        return;

    const std::uintptr_t add_item = FindAddItem(process);
    const std::uintptr_t reload = FindInventoryReload(process);
    RemoteModuleInfo module{};
    if (add_item == 0 || reload == 0 || !process.GetMainModuleInfo(module))
    {
        LogDiagnostic(
            "Sortie d'arme: adresses du moteur indisponibles, file abandonnee "
            "(AddItem=%08X Reload=%08X).",
            static_cast<unsigned>(add_item), static_cast<unsigned>(reload));
        const DWORD keep = g_pending_equip.process_id;
        g_pending_equip = {};
        g_pending_equip.process_id = keep;
        return;
    }
    const std::uintptr_t select = module.base_address + kSetSelectedItemRva;

    std::vector<std::uintptr_t> batch;
    while (!g_pending_equip.waiting.empty() && batch.size() < kEquipBatchSize)
    {
        const std::uintptr_t actor = g_pending_equip.waiting.front();
        g_pending_equip.waiting.erase(g_pending_equip.waiting.begin());
        // V147 - VERIFIE JUSTE AVANT, jamais sur la foi de la mise en file.
        //
        // Entre le moment ou le joueur choisit l'arme et celui ou son tour
        // arrive, un rallie a pu mourir : cinquante hommes par paquets de
        // quatre, cela dure. Ecrire dans un acteur que le moteur a detruit
        // arrete le jeu - c'est exactement ce que le journal a montre.
        if (!IsRalliedActorUsable(process, module, actor))
        {
            ++g_pending_equip.gone;
            continue;
        }
        // Celui qui tient deja cette arme n'a rien a recevoir. C'est le cas
        // qui a arrete le jeu : vingt-cinq rechargements de modele pour rien.
        HeldWeapon held{};
        if (ReadHeldWeapon(process, actor, held) &&
            held.item_id == g_pending_equip.item_id)
        {
            ++g_pending_equip.already;
            continue;
        }
        batch.push_back(actor);
    }
    g_pending_equip.next_at = now + kEquipBatchRestMs;
    if (batch.empty())
        return;

    const bool ok = GiveWeaponOnGameThread(
        process, batch, add_item, select, reload, g_pending_equip.item_id,
        g_pending_equip.amount, true);
    if (ok)
    {
        g_pending_equip.equipped += static_cast<unsigned>(batch.size());
    }
    else
    {
        g_pending_equip.failed += static_cast<unsigned>(batch.size());
    }
    LogDiagnostic(
        "Sortie d'arme: homme %08X pour l'objet %d resultat=%u "
        "(%u restant(s) en file).",
        static_cast<unsigned>(batch.front()), g_pending_equip.item_id,
        ok ? 1U : 0U,
        static_cast<unsigned>(g_pending_equip.waiting.size()));
    if (!ok)
    {
        // V148 - on ne continue pas apres un echec. Le code pose n'a pas rendu
        // la main : le jeu est peut-etre deja en train de mourir, et lui
        // envoyer les hommes suivants n'apprendrait rien de plus.
        LogDiagnostic(
            "Sortie d'arme: file ARRETEE apres cet echec (%u homme(s) "
            "abandonnes). La ligne TEMOIN ci-dessus dit ou le jeu s'est "
            "arrete.",
            static_cast<unsigned>(g_pending_equip.waiting.size()));
        g_pending_equip.waiting.clear();
    }
}

// =====================================================================
// V124 - LA FENETRE DES ARMES (touche J)
// =====================================================================
//
// Le joueur a propose lui-meme cette forme, et elle vaut mieux que le miroir :
// une fenetre ou l'on choisit une CIBLE - un soldat, un groupe, ou tous - puis
// une ARME, et les soldats designes recoivent exactement celle-la.
//
// Les armes proposees sont celles de l'inventaire du JOUEUR. Deux raisons :
// leurs identifiants sont forcement valides, puisque le jeu les lui a donnes;
// et il reconnait ce qu'il porte, ce qu'une liste de numeros nus ne
// permettrait pas.
//
// La distribution emploie le chemin etabli et eprouve de la V122 : `AddItem`
// puis `Reload`, sans forcer l'equipement - trois distributions successives
// ont ete observees sans que le jeu s'arrete, la ou toutes les tentatives
// d'equipement force le tuaient en quatre cents millisecondes.
struct WeaponChoice
{
    std::int32_t item_id = -1;
    std::int32_t reserve = 0;
    std::int32_t bullets = 0;
};

std::vector<WeaponChoice> g_weapon_choices{};
std::vector<std::wstring> g_weapon_menu_labels{};
int g_weapon_menu_selection = 0;
// V159 - la fenetre J se rouvre elle aussi sur la derniere arme choisie.
//
// Ici la structure est fixe - trois lignes d'en-tete, puis les armes - donc on
// retient le RANG DE L'ARME et non le numero de ligne : si le joueur ramasse
// ou perd une arme entre deux ouvertures, le rang reste celui qu'il visait.
// La cible, elle, est deja conservee par `settings.weapon_menu_target`.
bool g_weapon_last_valid = false;
std::size_t g_weapon_last_index = 0;
bool g_weapon_menu_just_opened = false;

// Releve l'inventaire du joueur : chaque objet, son identifiant et ses
// munitions.
void CollectPlayerWeapons(
    TrainerProcess& process,
    std::uintptr_t actor,
    std::vector<WeaponChoice>& choices)
{
    choices.clear();
    std::uintptr_t begin = 0;
    std::uintptr_t end = 0;
    if (!IsSanePointer(actor) ||
        !process.ReadMemory(actor + kActorInventoryBeginOffset, begin) ||
        !process.ReadMemory(actor + kActorInventoryEndOffset, end) ||
        !IsSanePointer(begin) || end <= begin)
    {
        return;
    }
    const std::uintptr_t count = (end - begin) / sizeof(std::uint32_t);
    if (count == 0 || count > 256)
        return;
    for (std::uintptr_t index = 0; index < count; ++index)
    {
        std::uintptr_t item = 0;
        WeaponChoice choice;
        if (!process.ReadMemory(
                begin + index * sizeof(std::uint32_t), item) ||
            !IsSanePointer(item) ||
            !process.ReadMemory(
                item + kInventoryItemIdOffset, choice.item_id) ||
            !process.ReadMemory(
                item + kInventoryItemReserveOffset, choice.reserve) ||
            !process.ReadMemory(
                item + kInventoryItemBulletsOffset, choice.bullets) ||
            choice.item_id < 0 || choice.item_id > 4096)
        {
            continue;
        }
        choices.push_back(choice);
    }
}

// Definie plus bas : demande a des soldats de sortir le dernier objet recu.
bool EquipLastItemOnGameThread(
    TrainerProcess& process,
    const std::vector<std::uintptr_t>& soldiers,
    std::uintptr_t set_selected_item);

void UpdateWeaponMenu(
    TrainerProcess& process,
    const RadarSnapshot* snapshot,
    GameplaySettings& settings)
{
    if (settings.weapon_menu_toggle_requested)
    {
        settings.weapon_menu_toggle_requested = false;
        settings.weapon_menu_open = !settings.weapon_menu_open;
        // V159 - on se replacera sur la derniere arme choisie.
        g_weapon_menu_just_opened = settings.weapon_menu_open;
    }
    if (!settings.weapon_menu_open || !process.IsConnected() || !snapshot ||
        !IsSanePointer(snapshot->player_object_address))
    {
        if (!settings.weapon_menu_open)
        {
            g_weapon_choices.clear();
            g_weapon_menu_labels.clear();
        }
        settings.weapon_menu_move = 0;
        settings.weapon_menu_target_move = 0;
        settings.weapon_menu_confirm_requested = false;
        return;
    }

    CollectPlayerWeapons(
        process, snapshot->player_object_address, g_weapon_choices);

    // V149 - LA FENETRE J NE VISE PLUS QUE LES RALLIES.
    //
    // Le joueur l'a demande sans ambiguite : « pourquoi dans la fenetre J tu
    // combines les soldats crees avec les rallies, supprime totalement les
    // soldats crees qui nous causent des problemes ».
    //
    // Il a raison sur le fond. Un soldat cree n'a pas de fiche, et huit
    // versions ont etabli qu'il ne peut pas soutenir l'etat « je tiens une
    // arme ». Le melanger aux rallies dans la meme fenetre ne pouvait donc
    // qu'entretenir la confusion, dans la fenetre comme dans le journal.
    //
    // Les soldats crees gardent tout le reste : creation, noms, sante,
    // mortalite, prise de controle, ordres par la carte, suppression. Ils ne
    // figurent simplement plus dans les fenetres d'armes.
    const std::vector<std::uintptr_t>& pool = g_rallied_enemies;

    // La cible : tous, un groupe, ou un homme precis.
    const int target_count = 2 + static_cast<int>(pool.size());
    if (settings.weapon_menu_target_move != 0 && target_count > 0)
    {
        settings.weapon_menu_target =
            (settings.weapon_menu_target + settings.weapon_menu_target_move +
             target_count) % target_count;
        settings.weapon_menu_target_move = 0;
    }
    if (settings.weapon_menu_target >= target_count)
        settings.weapon_menu_target = 0;

    wchar_t target_text[96]{};
    if (settings.weapon_menu_target == 0)
    {
        _snwprintf_s(
            target_text, _TRUNCATE, L"TOUS  (%u allies rallies)",
            static_cast<unsigned>(pool.size()));
    }
    else if (settings.weapon_menu_target == 1)
    {
        _snwprintf_s(
            target_text, _TRUNCATE, L"GROUPE  (%d premiers rallies)",
            settings.soldier_group_count);
    }
    else
    {
        _snwprintf_s(
            target_text, _TRUNCATE, L"Allie rallie %d",
            settings.weapon_menu_target - 1);
    }

    g_weapon_menu_labels.clear();
    {
        wchar_t line[128]{};
        _snwprintf_s(
            line, _TRUNCATE, L"CIBLE      %s", target_text);
        g_weapon_menu_labels.emplace_back(line);
        g_weapon_menu_labels.emplace_back(
            L"Gauche / Droite pour changer de cible");
        g_weapon_menu_labels.emplace_back(L"");
        for (std::size_t index = 0; index < g_weapon_choices.size(); ++index)
        {
            _snwprintf_s(
                line, _TRUNCATE, L"Arme %-2u   objet %-4d   %d balles",
                static_cast<unsigned>(index + 1),
                g_weapon_choices[index].item_id,
                g_weapon_choices[index].reserve +
                    g_weapon_choices[index].bullets);
            g_weapon_menu_labels.emplace_back(line);
        }
        if (g_weapon_choices.empty())
            g_weapon_menu_labels.emplace_back(L"(votre inventaire est vide)");
        g_weapon_menu_labels.emplace_back(L"");
        // V136 - la ligne dit ce que les mesures ont etabli, pour que le
        // joueur decide en connaissance de cause et non sur un avertissement
        // vague. Deux chemins opposes - ecriture directe de l'index (V116) et
        // appel natif complet (V133) - ont tous deux tue le jeu APRES coup :
        // ce n'est pas l'operation qui plante, c'est l'etat « ce soldat tient
        // une arme », que le moteur ne peut pas soutenir sur un acteur non
        // initialise. C'est la meme cause que les corps ecrases au sol.
        g_weapon_menu_labels.emplace_back(
            L"SORTIR l'arme  -  essai : arrete le jeu sur cette version");
    }

    // V159 - a l'ouverture, on se replace sur la derniere arme choisie.
    if (g_weapon_menu_just_opened)
    {
        g_weapon_menu_just_opened = false;
        int restored = 3;   // la premiere arme de la liste
        if (g_weapon_last_valid &&
            g_weapon_last_index < g_weapon_choices.size())
        {
            restored = 3 + static_cast<int>(g_weapon_last_index);
        }
        const int count = static_cast<int>(g_weapon_menu_labels.size());
        g_weapon_menu_selection =
            count > 0 ? (std::min)(restored, count - 1) : 0;
    }

    const int entry_count = static_cast<int>(g_weapon_menu_labels.size());
    if (settings.weapon_menu_move != 0 && entry_count > 0)
    {
        g_weapon_menu_selection =
            (g_weapon_menu_selection + settings.weapon_menu_move +
             entry_count) % entry_count;
        settings.weapon_menu_move = 0;
    }
    // Les trois premieres lignes sont de l'affichage : la selection se pose
    // toujours sur une arme.
    if (g_weapon_menu_selection < 3 && entry_count > 3)
        g_weapon_menu_selection = 3;
    if (g_weapon_menu_selection >= entry_count)
        g_weapon_menu_selection = entry_count - 1;

    if (!settings.weapon_menu_confirm_requested)
        return;
    settings.weapon_menu_confirm_requested = false;

    // La derniere ligne n'est pas une arme : c'est l'essai de sortie.
    const bool equip_request =
        static_cast<std::size_t>(g_weapon_menu_selection) + 1 ==
        g_weapon_menu_labels.size();

    const std::size_t weapon_index =
        static_cast<std::size_t>(g_weapon_menu_selection - 3);
    if (!equip_request && weapon_index >= g_weapon_choices.size())
        return;
    // V159 - on retient l'arme choisie pour la prochaine ouverture.
    if (!equip_request)
    {
        g_weapon_last_valid = true;
        g_weapon_last_index = weapon_index;
    }
    const WeaponChoice choice = equip_request
        ? WeaponChoice{}
        : g_weapon_choices[weapon_index];

    // Qui recoit.
    // V149 - les soldats crees ne recoivent plus d'arme par cette fenetre.
    const std::vector<std::uintptr_t>& troop = g_rallied_enemies;
    std::vector<std::uintptr_t> targets;
    if (settings.weapon_menu_target == 0)
    {
        targets = troop;
    }
    else if (settings.weapon_menu_target == 1)
    {
        const std::size_t group = (std::min)(
            static_cast<std::size_t>(
                (std::max)(1, settings.soldier_group_count)),
            troop.size());
        targets.assign(troop.begin(), troop.begin() + group);
    }
    else
    {
        const std::size_t one =
            static_cast<std::size_t>(settings.weapon_menu_target - 2);
        if (one < troop.size())
            targets.assign(1, troop[one]);
    }
    if (targets.empty())
    {
        LogDiagnostic("Fenetre armes: aucune cible, rien n'est distribue.");
        settings.weapon_menu_open = false;
        return;
    }

    // V131 - et juste avant d'ecrire dans ces acteurs.
    // V149 - seuls les rallies encore valides restent.
    targets.erase(
        std::remove_if(
            targets.begin(), targets.end(),
            [&](std::uintptr_t actor) { return !IsRalliedEnemy(actor); }),
        targets.end());
    if (targets.empty())
    {
        LogDiagnostic(
            "Fenetre armes: la cible n'existe plus, rien n'est distribue.");
        settings.weapon_menu_open = false;
        return;
    }

    if (equip_request)
    {
        // V150 - l'essai VOLONTAIRE de sortie d'arme. Il passe par la file,
        // un homme a la fois, avec le temoin de la V148 : si le jeu s'arrete,
        // le journal dira exactement ou. C'est le seul endroit du trainer qui
        // demande encore au moteur de SORTIR une arme.
        LogDiagnostic(
            "Fenetre armes: ESSAI VOLONTAIRE de sortie d'arme sur %u homme(s). "
            "Ce geste a arrete le jeu sur les quatre versions precedentes et "
            "sa cause n'est pas etablie. La ligne TEMOIN dira ou.",
            static_cast<unsigned>(targets.size()));
        settings.weapon_menu_open = false;
        g_weapon_menu_labels.clear();
        // L'arme visee est celle que le joueur porte : c'est la seule dont on
        // soit certain qu'elle existe et qu'elle lui appartienne.
        HeldWeapon mine{};
        if (ReadHeldWeapon(process, snapshot->player_object_address, mine))
        {
            QueueEquipRequest(
                process, targets, mine.item_id,
                (std::max)(mine.reserve + mine.bullets, 30));
        }
        else
        {
            LogDiagnostic(
                "Fenetre armes: essai annule - votre arme courante est "
                "illisible.");
        }
        return;
        RemoteModuleInfo equip_module{};
        if (!process.GetMainModuleInfo(equip_module))
        {
            settings.weapon_menu_open = false;
            return;
        }
        settings.weapon_menu_open = false;
        g_weapon_menu_labels.clear();
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        const bool equipped = EquipLastItemOnGameThread(
            process, targets,
            equip_module.base_address + kSetSelectedItemRva);
        LogDiagnostic(
            "Fenetre armes: sortie d'arme demandee a %u soldat(s) "
            "resultat=%u.",
            static_cast<unsigned>(targets.size()), equipped ? 1U : 0U);
        return;
    }

    const std::uintptr_t add_item = FindAddItem(process);
    const std::uintptr_t reload = FindInventoryReload(process);
    RemoteModuleInfo module{};
    std::uintptr_t select = 0;
    if (add_item == 0 || reload == 0 || !process.GetMainModuleInfo(module))
    {
        LogDiagnostic(
            "Fenetre armes: indisponible (AddItem=%08X Reload=%08X).",
            static_cast<unsigned>(add_item), static_cast<unsigned>(reload));
        settings.weapon_menu_open = false;
        return;
    }
    select = module.base_address + kSetSelectedItemRva;

    // V125 - la fenetre se ferme AVANT le declenchement.
    //
    // Le journal du joueur montrait une distribution refusee :
    //
    //   Miroir d'arme: objet 101 x1 ... declenche=1 execute=0
    //   Fenetre armes: arme 7 (objet 101) donnee a 11 soldat(s) resultat=0.
    //
    // Le miroir, lui, aboutissait toujours. La difference est que le miroir
    // agit pendant le jeu normal, alors que la fenetre etait ENCORE OUVERTE
    // au moment ou l'on demandait au jeu de traverser le site de cheat. On la
    // referme donc d'abord, et on laisse une image au jeu pour reprendre son
    // cours.
    settings.weapon_menu_open = false;
    g_weapon_menu_labels.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    // V132 - LA QUANTITE EST LA RESERVE DE MUNITIONS.
    //
    // Le joueur a vu ses soldats recevoir l'arme avec UNE seule munition. Je
    // passais 1, et je m'etais trompe sur la nature de ce nombre.
    //
    // Dans le moteur, `S_item` porte :
    //
    //   int itm;          // l'objet
    //   dword amount;     // <- la RESERVE, pas un nombre d'exemplaires
    //   dword bullets_in_stack;
    //
    // et `weapon_mods` lit depuis des versions cette meme `amount` comme la
    // reserve de l'arme (elle sert a la munition illimitee). En donnant 1, je
    // donnais litteralement une balle.
    //
    // On passe desormais la reserve de VOTRE arme - celle que la fenetre
    // affiche a cote de chaque ligne. Vos soldats recoivent donc l'arme avec
    // autant de munitions que vous en portez.
    const std::int32_t ammo =
        (std::max)(choice.reserve + choice.bullets, 30);

    // =================================================================
    // V141 - ON PASSE PAR LA FICHE, COMME LE MOTEUR LUI-MEME
    // =================================================================
    //
    // Jusqu'ici on deposait l'arme dans l'inventaire par un appel direct a
    // `AddItem`. Elle y arrivait bien - le joueur l'a constate - mais elle
    // restait dans le sac, et les sept facons de la lui mettre en main ont
    // toutes arrete le jeu.
    //
    // Le moteur, lui, ne met jamais une arme en main de l'exterieur. Il
    // remplit la FICHE du soldat, puis l'applique; et c'est `TableUpdate` qui
    // vide l'inventaire, y remet ce que la fiche indique, et DESIGNE le
    // premier poste - donc le met dans la main.
    //
    // On fait donc exactement cela, dans cet ordre :
    //
    //   1. recopier le bloc de fiche d'un de VOS soldats  (identite, visage,
    //      endurance - sans quoi `TableUpdate` ramenerait leur sante a 1);
    //   2. ecrire l'arme choisie au premier poste de la liste d'inventaire;
    //   3. demander au moteur d'appliquer la fiche, sur son propre thread.
    //
    // Si l'une des trois etapes echoue, on retombe sur la distribution par
    // `AddItem`, qui est validee et ne met rien en main - mais ne casse rien.
    // =================================================================
    // V150 - LA SORTIE D'ARME EST COUPEE. VOICI POURQUOI, ET CE QUI RESTE.
    // =================================================================
    //
    // Quatre versions ont cherche pourquoi le jeu s'arrete quand on demande a
    // un rallie de SORTIR une arme. Le temoin de la V148 a elimine les trois
    // appels du moteur - `AddItem`, `Reload`, `SetSelectedInvItem` : le code
    // pose n'a jamais execute une seule instruction. La V149 a suspendu tous
    // les threads du jeu avant d'ecrire, ce qui aurait du supprimer la course
    // sur le site de triche; le jeu s'est arrete quand meme, et le journal a
    // montre qu'on ne pouvait meme plus suspendre proprement ses threads
    // pendant cinq secondes - signe qu'ils mouraient deja.
    //
    // Je ne sais donc toujours pas ce qui tue le jeu, et je refuse de le
    // casser une cinquieme fois pour le chercher.
    //
    // CE QUI EST COUPE : la designation de l'arme (`SetSelectedInvItem`).
    // CE QUI RESTE    : l'arme est bien DONNEE, avec ses munitions, chargee.
    //
    // Ce chemin-la - deposer et charger, sans designer - est le seul qui n'ait
    // JAMAIS arrete le jeu, sur les soldats crees comme sur les rallies. Et
    // pour un rallie il perd peu : il porte deja sa propre arme et s'en sert.
    //
    // La derniere ligne de la fenetre reste disponible pour essayer la sortie
    // volontairement, en connaissance de cause.
    // V151 - LA SORTIE D'ARME EST RETABLIE.
    //
    // Elle avait ete coupee en V150 faute de savoir ce qui arretait le jeu. La
    // cause est maintenant etablie et corrigee : il manquait un argument a
    // l'appel de `SetSelectedInvItem`, qui depile huit octets (`ret 8`) alors
    // qu'on n'en empilait que quatre.
    //
    // On garde la file par petits paquets et le saut des hommes qui tiennent
    // deja l'arme : ces deux precautions ne coutent rien et evitent de
    // demander au moteur cinquante chargements de modele en une image.
    QueueEquipRequest(process, targets, choice.item_id, ammo);
    LogDiagnostic(
        "Fenetre armes: arme %u (objet %d) demandee pour %u allie(s) "
        "rallie(s). Ils la recevront ET la SORTIRONT, un par un.",
        static_cast<unsigned>(weapon_index + 1), choice.item_id,
        static_cast<unsigned>(targets.size()));
    return;
#if 0   // V149 - le chemin des soldats crees est retire de la fenetre J.

    bool given = false;
    bool by_fiche = false;
    if (g_actor_table.get_table_rank != 0)
    {
        std::vector<std::uintptr_t> donors = g_soldier_menu;
        donors.erase(
            std::remove_if(
                donors.begin(), donors.end(),
                [&](std::uintptr_t actor)
                {
                    return std::find(
                               g_spawned_soldiers.begin(),
                               g_spawned_soldiers.end(), actor) !=
                        g_spawned_soldiers.end();
                }),
            donors.end());
        const std::uintptr_t table_offset =
            MeasureActorTableOffset(process, donors, g_reference_enemies);
        if (!donors.empty() && table_offset != 0)
        {
            const unsigned copied = CopyFicheFromDonor(
                process, donors.front(), targets, table_offset);
            if (copied != 0)
            {
                const unsigned slots = WriteFicheWeaponSlot(
                    process, targets, table_offset, choice.item_id, ammo);
                LogDiagnostic(
                    "Recopie de fiche: arme %d posee au premier poste chez "
                    "%u soldat(s) sur %u.",
                    choice.item_id, slots,
                    static_cast<unsigned>(targets.size()));
                if (slots != 0)
                {
                    by_fiche = ApplyFicheOnGameThread(
                        process, targets,
                        g_actor_table.get_table_rank +
                            kTableUpdateFromGetTable);
                    given = by_fiche;
                }
            }
        }
        else
        {
            LogDiagnostic(
                "Recopie de fiche: pas de soldat de reference (%u) ou fiche "
                "non situee (+0x%03X); on retombe sur la remise simple.",
                static_cast<unsigned>(donors.size()),
                static_cast<unsigned>(table_offset));
        }
    }
    if (!given)
        given = GiveWeaponOnGameThread(
            process, targets, add_item, select, reload, choice.item_id, ammo,
            false);
    if (!given)
    {
        // Une seconde tentative, une fois la fenetre bien refermee. Le site de
        // cheat n'est traverse que si le jeu tourne normalement.
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        given = GiveWeaponOnGameThread(
            process, targets, add_item, select, reload, choice.item_id, ammo,
            false);
        LogDiagnostic(
            "Fenetre armes: seconde tentative %s.",
            given ? "reussie" : "echouee elle aussi");
    }
    LogDiagnostic(
        "Fenetre armes: arme %u (objet %d) donnee a %u soldat(s) "
        "resultat=%u, par %s.",
        static_cast<unsigned>(weapon_index + 1), choice.item_id,
        static_cast<unsigned>(targets.size()), given ? 1U : 0U,
        by_fiche ? "la fiche - elle doit arriver DANS LEUR MAIN"
                 : "remise simple - elle reste dans le sac");
#endif
}

std::size_t WeaponMenuEntryCount()
{
    return g_weapon_menu_labels.size();
}

const wchar_t* WeaponMenuEntry(std::size_t index)
{
    return index < g_weapon_menu_labels.size()
        ? g_weapon_menu_labels[index].c_str()
        : nullptr;
}

std::size_t WeaponMenuSelection()
{
    return static_cast<std::size_t>(
        g_weapon_menu_selection < 0 ? 0 : g_weapon_menu_selection);
}

// =====================================================================
// V135 - SORTIR L'ARME : UNE ACTION A PART, QUE LE JOUEUR DECLENCHE
// =====================================================================
//
// Le temoin de la V134 a tranche, et il dit l'inverse de ce que je croyais :
//
//   Mains: soldats crees = 1 1 1 1 1 1 | VOTRE soldat = 2.
//
// Votre soldat rend 2 : la main est trouvee, avec son arme dedans - la
// recherche fonctionne. Les soldats crees rendent 1 : la main est trouvee, et
// elle est DEJA VIDE. C'est exactement ce que `SetGun` attend.
//
// Mon diagnostic de la V134 etait donc faux. En V133, `SetGun` trouvait bien
// la main et allait jusqu'au bout : creation du modele d'arme, chargement
// depuis le disque, accrochage. C'est ce chemin-la qui arrete le jeu, pas
// l'absence de main.
//
// Je ne sais pas encore pourquoi, et je ne vais pas le deviner une septieme
// fois. Ce que je peux faire, c'est cesser de vous imposer l'essai : sortir
// l'arme devient une LIGNE A PART de la fenetre J, que vous declenchez quand
// vous voulez. Creer des soldats et leur donner des armes reste sans risque;
// seul cet essai-la peut arreter le jeu, et vous choisissez le moment.
bool EquipLastItemOnGameThread(
    TrainerProcess& process,
    const std::vector<std::uintptr_t>& soldiers,
    std::uintptr_t set_selected_item)
{
    if (soldiers.empty() || !IsSanePointer(set_selected_item))
        return false;

    constexpr std::uintptr_t kProcessCheatHookRva = 0x0009'FC10;
    constexpr std::array<std::uint8_t, 5> kExpected{
        0xB8, 0x5C, 0x10, 0x00, 0x00};
    constexpr std::size_t kRemoteSize = 0x1000;
    constexpr std::size_t kCountOffset = 0x200;
    constexpr std::size_t kSelectOffset = 0x204;
    constexpr std::size_t kCompletionOffset = 0x208;
    constexpr std::size_t kActorsOffset = 0x300;
    constexpr std::size_t kIndicesOffset = 0x500;

    const std::uint32_t count = static_cast<std::uint32_t>(
        (std::min)(soldiers.size(), std::size_t{kMaximumSpawnedSoldiers}));

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        kProcessCheatHookRva >= module.image_size)
    {
        return false;
    }
    const std::uintptr_t hook = module.base_address + kProcessCheatHookRva;
    std::array<std::uint8_t, 5> original{};
    if (!process.ReadMemory(hook, original.data(), original.size()) ||
        original != kExpected)
    {
        return false;
    }
    const std::uintptr_t remote = process.AllocateRemoteMemory(kRemoteSize);
    if (!IsSanePointer(remote))
        return false;

    std::vector<std::uint8_t> code;
    const auto byte = [&](std::uint8_t v) { code.push_back(v); };
    const auto dword = [&](std::uint32_t v)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(v));
        std::memcpy(code.data() + offset, &v, sizeof(v));
    };
    const auto slot = [&](std::size_t offset)
    {
        dword(static_cast<std::uint32_t>(remote + offset));
    };
    std::vector<std::size_t> to_end;

    byte(0x9C);
    byte(0x60);
    byte(0x83); byte(0x3D); slot(kCompletionOffset); byte(0x00);
    byte(0x0F); byte(0x85);
    to_end.push_back(code.size()); dword(0);
    byte(0x33); byte(0xF6);                       // xor esi,esi
    const std::size_t loop_start = code.size();
    byte(0xA1); slot(kCountOffset);
    byte(0x3B); byte(0xF0);
    byte(0x0F); byte(0x83);
    to_end.push_back(code.size()); dword(0);
    // ebx = acteur, eax = index
    byte(0x8B); byte(0x1C); byte(0xB5); slot(kActorsOffset);
    byte(0x85); byte(0xDB);
    const std::size_t skip = code.size();
    byte(0x74); byte(0x00);
    byte(0x8B); byte(0x04); byte(0xB5); slot(kIndicesOffset);
    // V151 - meme correction : `SetSelectedInvItem` depile huit octets
    // (`ret 8`), il faut donc empiler les DEUX arguments. Voir le commentaire
    // detaille dans `GiveWeaponOnGameThread`.
    byte(0x6A); byte(0x00);                       // push net_send = false
    byte(0x50);                                   // push index
    byte(0x8B); byte(0xCB);                       // mov ecx,acteur
    byte(0xFF); byte(0x15); slot(kSelectOffset);
    const std::size_t next = code.size();
    code[skip + 1] = static_cast<std::uint8_t>(next - (skip + 2));
    byte(0x46);
    byte(0xE9);
    const std::size_t repeat = code.size();
    dword(0);
    const std::size_t finish = code.size();
    for (const std::size_t displacement : to_end)
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            finish - (displacement + sizeof(std::int32_t)));
        std::memcpy(code.data() + displacement, &relative, sizeof(relative));
    }
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            loop_start - (repeat + sizeof(std::int32_t)));
        std::memcpy(code.data() + repeat, &relative, sizeof(relative));
    }
    byte(0xC7); byte(0x05); slot(kCompletionOffset); dword(1);
    byte(0x61);
    byte(0x9D);
    code.insert(code.end(), original.begin(), original.end());
    byte(0xE9);
    dword(static_cast<std::uint32_t>(
        (hook + original.size()) -
        (remote + code.size() + sizeof(std::int32_t))));

    const std::uint32_t zero = 0;
    bool written = code.size() < kCountOffset &&
        process.WriteMemory(remote, code.data(), code.size()) &&
        process.WriteMemory(remote + kCountOffset, count) &&
        process.WriteMemory(
            remote + kSelectOffset,
            static_cast<std::uint32_t>(set_selected_item)) &&
        process.WriteMemory(remote + kCompletionOffset, zero);
    for (std::uint32_t index = 0; written && index < count; ++index)
    {
        // L'index du DERNIER objet de son inventaire : la derniere arme
        // recue. Un soldat sans arme est simplement saute.
        std::uintptr_t begin = 0;
        std::uintptr_t end = 0;
        std::uint32_t actor = 0;
        std::int32_t last = -1;
        if (process.ReadMemory(
                soldiers[index] + kActorInventoryBeginOffset, begin) &&
            process.ReadMemory(
                soldiers[index] + kActorInventoryEndOffset, end) &&
            IsSanePointer(begin) && end > begin)
        {
            last = static_cast<std::int32_t>(
                (end - begin) / sizeof(std::uint32_t)) - 1;
        }
        if (last > 0)
            actor = static_cast<std::uint32_t>(soldiers[index]);
        written =
            process.WriteMemory(
                remote + kActorsOffset + index * sizeof(std::uint32_t),
                actor) &&
            process.WriteMemory(
                remote + kIndicesOffset + index * sizeof(std::uint32_t),
                static_cast<std::uint32_t>(last > 0 ? last : 0));
    }
    if (!written)
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }
    std::array<std::uint8_t, 5> patch{0xE9, 0, 0, 0, 0};
    const std::int32_t relative =
        static_cast<std::int32_t>(remote - (hook + patch.size()));
    std::memcpy(patch.data() + 1, &relative, sizeof(relative));
    if (!InstallHookSafely(
            process, hook, patch.data(), patch.size(),
            "le saut vers le code pose"))
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }
    const bool triggered = SendInventoryMainThreadTrigger(process);
    std::uint32_t completed = 0;
    if (triggered)
    {
        for (int attempt = 0; attempt < 500 && completed == 0; ++attempt)
        {
            (void)process.ReadMemory(remote + kCompletionOffset, completed);
            if (completed == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    (void)RestoreHookSafely(
        process, hook, original.data(), original.size());
    QueueRemotePageRelease(process, remote, kRemoteSize);
    LogDiagnostic(
        "Sortir l'arme: demande a %u soldat(s) declenche=%u execute=%u.",
        count, triggered ? 1U : 0U, completed);
    return completed != 0;
}

// Etat du miroir, et derniere arme distribuee pour ne redistribuer qu'au
// changement.
//
// V118 - il demarre ETEINT. Deux versions de suite se sont arretees sur ce
// passage, chaque fois pour une raison differente et chaque fois identifiee.
// Le joueur doit pouvoir jouer sans que le trainer prenne d'initiative sur
// l'inventaire de ses soldats : la touche J l'allume quand il veut l'essayer,
// et la fenetre G affiche l'etat en clair.
bool g_weapon_mirror_enabled = false;
std::int32_t g_weapon_mirror_last_item = -1;

void UpdateWeaponMirror(
    TrainerProcess& process,
    const RadarSnapshot* snapshot,
    GameplaySettings& settings)
{
    if (settings.weapon_mirror_toggle_requested)
    {
        settings.weapon_mirror_toggle_requested = false;
        g_weapon_mirror_enabled = !g_weapon_mirror_enabled;
        LogDiagnostic(
            "Miroir d'arme: %s par la touche J.",
            g_weapon_mirror_enabled
                ? "REPRIS - les soldats suivront de nouveau votre arme"
                : "COUPE - les soldats gardent l'arme qu'ils portent");
    }
    settings.weapon_mirror_enabled = g_weapon_mirror_enabled;

    // V145 - LE MIROIR COUVRE LES RALLIES, ET LEUR FAIT SORTIR L'ARME.
    //
    // Le joueur dit : « la partie ou ils portent la meme arme que moi ne
    // fonctionne pas ». Le miroir ne visait que `g_spawned_soldiers` - les
    // soldats CREES. Les ennemis rallies n'y figuraient pas, donc rien ne leur
    // arrivait.
    //
    // Ils sont ajoutes ici, et ils sont traites comme dans la fenetre J : eux
    // SORTENT l'arme, parce qu'ils ont la fiche complete que le jeu leur a
    // donnee, alors qu'un soldat cree ne peut pas soutenir cet etat.
    // V149 - le miroir ne concerne plus que les rallies, pour la meme raison.
    if (!g_weapon_mirror_enabled || g_rallied_enemies.empty() ||
        !process.IsConnected() || !snapshot ||
        !IsSanePointer(snapshot->player_object_address))
    {
        return;
    }
    HeldWeapon weapon{};
    if (!ReadHeldWeapon(process, snapshot->player_object_address, weapon))
        return;
    if (weapon.item_id == g_weapon_mirror_last_item)
        return;

    const std::uintptr_t add_item = FindAddItem(process);
    RemoteModuleInfo module{};
    std::uintptr_t set_selected_item = 0;
    std::array<std::uint8_t, kSetSelectedItemPrologue.size()> probe{};
    if (add_item == 0 || !process.GetMainModuleInfo(module) ||
        kSetSelectedItemRva + probe.size() >= module.image_size ||
        (set_selected_item = module.base_address + kSetSelectedItemRva) == 0 ||
        !process.ReadMemory(set_selected_item, probe.data(), probe.size()) ||
        probe != kSetSelectedItemPrologue)
    {
        static ULONGLONG next_report = 0;
        const ULONGLONG now = GetTickCount64();
        if (now >= next_report)
        {
            next_report = now + 10000ULL;
            LogDiagnostic(
                "Miroir d'arme: indisponible (AddItem=%08X select=%08X "
                "empreinte %s).",
                static_cast<unsigned>(add_item),
                static_cast<unsigned>(set_selected_item),
                probe == kSetSelectedItemPrologue ? "conforme" : "differente");
        }
        return;
    }
    // =================================================================
    // V120 - LE MIROIR D'ARME EST COUPE, ET VOICI POURQUOI
    // =================================================================
    //
    // Quatre causes distinctes ont ete trouvees et corrigees sur ce seul
    // passage, chacune reelle et prouvee par les journaux du joueur :
    //
    //   V116  base de `C_inventory` a +0x54 au lieu de +0x58
    //   V118  index ecrit a la main au lieu de passer par la fonction native
    //   V119  quantite de 200 a 1000 au lieu de 1
    //   V119  `Reload` jamais appelee, donc arme vide
    //
    // Et le resultat n'a pas bouge d'un iota :
    //
    //   Miroir d'arme: objet 101 x1 ... execute=1, 5 soldat(s) la tiennent.
    //   Process: pid=0                        <- 400 ms plus tard
    //
    // La distribution reussit a chaque fois; le jeu meurt toujours quand il
    // se SERT de l'arme. La conclusion s'impose : ce n'est pas la facon de
    // donner l'arme qui est en cause, c'est l'acteur qui la recoit.
    //
    // Les soldats crees n'ont pas ete initialises par `MissionLoad`
    // (GameMission.cpp:1783). C'est deja ce qui les laisse ECRASES AU SOL et
    // sans identite dans le bandeau. Un acteur dans cet etat ne peut pas se
    // servir d'un objet : le moteur va chercher, au premier usage, des
    // donnees que cette etape aurait du preparer.
    //
    // Continuer a essayer d'autres variantes reviendrait a faire perdre des
    // sessions au joueur pour rien. Le miroir est donc COUPE tant que
    // l'initialisation d'acteur n'est pas rejouee. Tout le code reste en
    // place; il suffira de retirer ce refus le jour ou cette piece existera.
    const std::uintptr_t reload = FindInventoryReload(process);
    if (reload == 0)
        return;
    // V132 - `amount` est la RESERVE de munitions, pas un nombre d'exemplaires.
    // On reprend celle de l'arme que le joueur porte.
    const std::int32_t amount =
        (std::max)(weapon.reserve + weapon.bullets, 30);
    bool mirrored = false;
    if (!g_rallied_enemies.empty())
    {
        // V151 - par la file, et ils la sortent : l'appel est desormais juste.
        QueueEquipRequest(
            process, g_rallied_enemies, weapon.item_id, amount);
        mirrored = true;
        LogDiagnostic(
            "Miroir d'arme: votre arme %d demandee pour %u allie(s) "
            "rallie(s); ils la sortiront un par un.",
            weapon.item_id,
            static_cast<unsigned>(g_rallied_enemies.size()));
    }
    if (mirrored)
    {
        g_weapon_mirror_last_item = weapon.item_id;
    }
}

// Bascule de controle : exactement ce que fait `C_game_mission::PlayerSwitch`,
// SetActive(false) sur le soldat courant puis SetActive(true) sur le choisi.
// Le soldat que le joueur pilotait AVANT d'avoir touche a la fenetre. Il
// est retenu au premier changement, et la fenetre offre une ligne pour y
// revenir : sans elle, le joueur restait prisonnier des soldats crees.
std::uintptr_t g_original_soldier = 0;

bool SwitchControlledSoldier(
    TrainerProcess& process,
    std::uintptr_t from,
    std::uintptr_t to)
{
    if (!IsSanePointer(to) || from == to)
        return false;
    if (g_original_soldier == 0 && IsSanePointer(from))
        g_original_soldier = from;

    constexpr std::uintptr_t kProcessCheatHookRva = 0x0009'FC10;
    constexpr std::array<std::uint8_t, 5> kProcessCheatExpected{
        0xB8, 0x5C, 0x10, 0x00, 0x00};
    constexpr std::size_t kSwitchRemoteSize = 0x400;
    constexpr std::size_t kSwitchCompletionOffset = 0x200;

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        kProcessCheatHookRva >= module.image_size)
    {
        return false;
    }
    const std::uintptr_t hook = module.base_address + kProcessCheatHookRva;
    std::array<std::uint8_t, 5> original{};
    if (!process.ReadMemory(hook, original.data(), original.size()) ||
        original != kProcessCheatExpected)
    {
        return false;
    }
    const std::uintptr_t remote =
        process.AllocateRemoteMemory(kSwitchRemoteSize);
    if (!IsSanePointer(remote))
        return false;

    std::vector<std::uint8_t> code;
    const auto byte = [&](std::uint8_t value) { code.push_back(value); };
    const auto dword = [&](std::uint32_t value)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(value));
        std::memcpy(code.data() + offset, &value, sizeof(value));
    };
    byte(0x9C); byte(0x60);
    if (IsSanePointer(from))
    {
        byte(0x6A); byte(0x00);                  // push show_msg = false
        byte(0x6A); byte(0x00);                  // push active = false
        byte(0xB9); dword(static_cast<std::uint32_t>(from));
        byte(0x8B); byte(0x01);
        byte(0xFF); byte(0x50);
        byte(static_cast<std::uint8_t>(kPlayerSetActiveVtableOffset));
    }
    byte(0x6A); byte(0x00);                      // push show_msg = false
    byte(0x6A); byte(0x01);                      // push active = true
    byte(0xB9); dword(static_cast<std::uint32_t>(to));
    byte(0x8B); byte(0x01);
    byte(0xFF); byte(0x50);
    byte(static_cast<std::uint8_t>(kPlayerSetActiveVtableOffset));
    byte(0xC7); byte(0x05);
    dword(static_cast<std::uint32_t>(remote + kSwitchCompletionOffset));
    dword(1);
    byte(0x61); byte(0x9D);
    code.insert(code.end(), original.begin(), original.end());
    byte(0xE9);
    const std::int32_t back = static_cast<std::int32_t>(
        (hook + original.size()) -
        (remote + code.size() + sizeof(std::int32_t)));
    dword(static_cast<std::uint32_t>(back));

    const std::uint32_t zero = 0;
    if (code.size() >= kSwitchCompletionOffset ||
        !process.WriteMemory(remote, code.data(), code.size()) ||
        !process.WriteMemory(remote + kSwitchCompletionOffset, zero))
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }
    std::array<std::uint8_t, 5> patch{0xE9, 0, 0, 0, 0};
    const std::int32_t relative = static_cast<std::int32_t>(
        remote - (hook + patch.size()));
    std::memcpy(patch.data() + 1, &relative, sizeof(relative));
    if (!InstallHookSafely(
            process, hook, patch.data(), patch.size(),
            "le saut vers le code pose"))
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }
    const bool triggered = SendInventoryMainThreadTrigger(process);
    std::uint32_t completed = 0;
    if (triggered)
    {
        for (int attempt = 0; attempt < 500 && completed == 0; ++attempt)
        {
            (void)process.ReadMemory(
                remote + kSwitchCompletionOffset, completed);
            if (completed == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    (void)RestoreHookSafely(
        process, hook, original.data(), original.size());
    QueueRemotePageRelease(process, remote, kSwitchRemoteSize);
    LogDiagnostic(
        "Bascule de controle: %08X -> %08X declenche=%u execute=%u.",
        static_cast<unsigned>(from), static_cast<unsigned>(to),
        triggered ? 1U : 0U, completed);
    return completed == 1U;
}

void UpdateSoldierMenu(
    TrainerProcess& process,
    const RadarSnapshot* snapshot,
    GameplaySettings& settings,
    GameplayStatus& status)
{
    (void)status;
    if (settings.soldier_menu_toggle_requested)
    {
        settings.soldier_menu_toggle_requested = false;
        settings.soldier_menu_open = !settings.soldier_menu_open;
        // V159 - on ne repart plus du haut : la selection sera replacee sur le
        // dernier choix une fois les lignes reconstruites.
        g_soldier_menu_just_opened = settings.soldier_menu_open;
    }
    if (!settings.soldier_menu_open || !process.IsConnected() || !snapshot ||
        !IsSanePointer(snapshot->player_object_address))
    {
        if (!settings.soldier_menu_open)
        {
            g_soldier_menu.clear();
            g_soldier_menu_labels.clear();
        }
        settings.soldier_menu_move = 0;
        settings.soldier_menu_confirm_requested = false;
        settings.soldier_menu_digit = -1;
        settings.soldier_menu_erase = false;
        g_soldier_count_typing = false;
        return;
    }

    // Deux lignes portent un nombre : CREER, et GROUPE. Ce qui est tape va
    // a la ligne choisie - le joueur regle exactement combien de soldats il
    // cree, et exactement combien il envoie sur la carte.
    int* typed = nullptr;
    int typed_maximum = static_cast<int>(kMaximumSpawnedSoldiers);
    if (g_soldier_menu_selection >= 0 &&
        static_cast<std::size_t>(g_soldier_menu_selection) <
            g_soldier_actions.size())
    {
        const SoldierMenuEntryData& chosen =
            g_soldier_actions[static_cast<std::size_t>(
                g_soldier_menu_selection)];
        if (chosen.action == SoldierMenuAction::Create)
        {
            typed = &settings.soldier_spawn_count;
        }
        else if (chosen.action == SoldierMenuAction::Group)
        {
            typed = &settings.soldier_group_count;
            // Un groupe ne peut pas depasser le nombre de soldats existants.
            typed_maximum = g_soldier_menu.empty()
                ? static_cast<int>(kMaximumSpawnedSoldiers)
                : static_cast<int>(g_soldier_menu.size());
        }
        else if (chosen.action == SoldierMenuAction::OpenFire)
        {
            typed = &settings.fire_count;
            typed_maximum = g_rallied_enemies.empty()
                ? 1 : static_cast<int>(g_rallied_enemies.size());
        }
        else if (chosen.action == SoldierMenuAction::FreeMoveAlly)
        {
            typed = &settings.free_move_index;
            typed_maximum = g_rallied_enemies.empty()
                ? 1 : static_cast<int>(g_rallied_enemies.size());
        }
        else if (chosen.action == SoldierMenuAction::RallyEnemies)
        {
            // V142 - on ne peut pas rallier plus d'ennemis qu'il n'y en a
            // encore en face. La borne est donc le nombre d'ennemis restes
            // allemands, releve a l'image precedente.
            typed = &settings.enemy_rally_count;
            const std::size_t hostile = EnemiesStillHostile().size();
            typed_maximum = hostile == 0 ? 1 : static_cast<int>(hostile);
        }
    }

    // Le premier chiffre frappe REMPLACE la valeur affichee - sans quoi il
    // faudrait effacer avant de saisir - et les suivants s'ajoutent a droite,
    // tant que le total ne depasse pas la limite de la ligne. Un zero en tete
    // est ignore, comme partout ailleurs.
    if (settings.soldier_menu_digit >= 0)
    {
        const int digit = settings.soldier_menu_digit;
        if (typed != nullptr)
        {
            int value = g_soldier_count_typing ? *typed * 10 + digit : digit;
            if (value > typed_maximum)
                value = typed_maximum;
            if (value >= 1)
            {
                *typed = value;
                g_soldier_count_typing = true;
            }
            // Journalise : si le joueur dit que la saisie ne repond pas, le
            // journal tranche - la touche est arrivee, ou elle n'est pas
            // arrivee. Plus de discussion possible.
            LogDiagnostic(
                "Fenetre soldats: chiffre %d frappe, nombre = %d (max %d).",
                digit, *typed, typed_maximum);
            // V156 - un chiffre frappe n'est pas un switch.
            NoteSwitchCycleDigit();
        }
        else
        {
            LogDiagnostic(
                "Fenetre soldats: chiffre %d frappe, mais la ligne choisie ne "
                "porte pas de nombre.",
                digit);
        }
        settings.soldier_menu_digit = -1;
    }
    if (settings.soldier_menu_erase)
    {
        if (typed != nullptr)
        {
            *typed = (std::max)(1, *typed / 10);
            g_soldier_count_typing = true;
        }
        settings.soldier_menu_erase = false;
    }
    if (settings.soldier_menu_adjust != 0)
    {
        // Les fleches restent acceptees pour un cran; elles closent la saisie
        // en cours pour que le chiffre suivant reparte d'une valeur nette.
        if (typed != nullptr)
        {
            *typed = (std::clamp)(
                *typed + settings.soldier_menu_adjust, 1, typed_maximum);
            g_soldier_count_typing = false;
        }
        settings.soldier_menu_adjust = 0;
    }
    settings.soldier_spawn_count = (std::clamp)(
        settings.soldier_spawn_count, 1,
        static_cast<int>(kMaximumSpawnedSoldiers));
    settings.soldier_group_count = (std::max)(1, settings.soldier_group_count);

    // V131 - purge d'abord : la liste ne doit contenir que des acteurs vivants.
    PruneCreatedSoldiers(process);
    // V137 - et on releve les ennemis, qui servent de reference de mesure.
    CollectReferenceEnemies(*snapshot);
    // V142 - la liste complete des ennemis de la mission, et le menage parmi
    // ceux qui sont deja passes de notre cote.
    CollectMissionEnemies(process, *snapshot);

    CollectLocalPlayers(process, *snapshot, g_soldier_menu);
    // Ne garder que les soldats de cette machine : la vie etendue accepte les
    // copies distantes, un ordre de deplacement non.
    g_soldier_menu.erase(
        std::remove_if(
            g_soldier_menu.begin(), g_soldier_menu.end(),
            [&](std::uintptr_t actor)
            {
                std::uint32_t owner = 0;
                return !process.ReadMemory(actor + 0x34U, owner) || owner != 0;
            }),
        g_soldier_menu.end());

    SpawnCapability capability{};
    const bool can_spawn =
        ResolveSpawnCapability(process, *snapshot, capability);

    // Lignes COURTES. Les libelles de la V108 depassaient la largeur de la
    // fenetre et se retrouvaient coupes : le joueur ne voyait pas la fin de
    // ses lignes. Le detail des touches est dans le pied de fenetre, une
    // seule fois, au lieu d'etre repete sur chaque ligne.
    const std::size_t troop_count = g_soldier_menu.size();
    const int group_count = (std::min)(
        settings.soldier_group_count,
        troop_count == 0 ? settings.soldier_group_count
                         : static_cast<int>(troop_count));
    g_soldier_menu_labels.clear();
    g_soldier_actions.clear();
    {
        // La fenetre est dessinee en police a chasse fixe : les colonnes
        // s'alignent vraiment, et la liste se lit d'un coup d'oeil.
        wchar_t line[128]{};
        const wchar_t* type_here =
            L"   <  tapez le nombre";
        _snwprintf_s(
            line, _TRUNCATE, L"CREER      %3d soldats%s",
            settings.soldier_spawn_count,
            !can_spawn ? L"   (indisponible)"
                       : (g_soldier_menu_selection == 0 ? type_here : L""));
        g_soldier_menu_labels.emplace_back(line);
        g_soldier_actions.push_back({SoldierMenuAction::Create, 0});

        _snwprintf_s(
            line, _TRUNCATE, L"GROUPE     %3d soldats%s", group_count,
            g_soldier_menu_selection == 1 ? type_here : L"");
        g_soldier_menu_labels.emplace_back(line);
        g_soldier_actions.push_back({SoldierMenuAction::Group, 0});

        _snwprintf_s(
            line, _TRUNCATE, L"TOUS       %3u soldats",
            static_cast<unsigned>(troop_count));
        g_soldier_menu_labels.emplace_back(line);
        g_soldier_actions.push_back({SoldierMenuAction::All, 0});

        // ==========================================================
        // V142 - LES ENNEMIS DE LA MISSION, COMPTES ET RALLIABLES
        // ==========================================================
        const std::size_t enemy_total = g_mission_enemies.size();
        const std::size_t rallied_total = g_rallied_enemies.size();
        const std::size_t hostile_total = EnemiesStillHostile().size();
        settings.enemy_rally_count = (std::clamp)(
            settings.enemy_rally_count, 1,
            hostile_total == 0 ? 1 : static_cast<int>(hostile_total));

        g_soldier_menu_labels.emplace_back(L"");
        g_soldier_actions.push_back({SoldierMenuAction::None, 0});

        _snwprintf_s(
            line, _TRUNCATE,
            L"ENNEMIS    %3u dans la mission, %3u de votre cote",
            static_cast<unsigned>(enemy_total),
            static_cast<unsigned>(rallied_total));
        g_soldier_menu_labels.emplace_back(line);
        g_soldier_actions.push_back({SoldierMenuAction::None, 0});

        // V143 - la ligne dit a quelle distance est le plus proche. Un
        // rallie NE SE DEPLACE PAS : si le plus proche est a 200 metres, le
        // joueur ne verra rien, et il vaut mieux qu'il le sache avant.
        wchar_t nearest[48]{};
        if (hostile_total != 0 && g_nearest_hostile_distance >= 0.0f)
        {
            _snwprintf_s(
                nearest, _TRUNCATE, L"  (le plus proche a %d m)",
                static_cast<int>(g_nearest_hostile_distance + 0.5f));
        }
        _snwprintf_s(
            line, _TRUNCATE, L"RALLIER    %3d ennemis%s%s",
            settings.enemy_rally_count,
            hostile_total == 0 ? L"   (aucun en face)" : nearest,
            hostile_total != 0 && g_soldier_menu_selection >= 0 &&
                    g_soldier_actions.size() ==
                        static_cast<std::size_t>(g_soldier_menu_selection)
                ? type_here
                : L"");
        g_soldier_menu_labels.emplace_back(line);
        g_soldier_actions.push_back({SoldierMenuAction::RallyEnemies, 0});

        if (rallied_total != 0)
        {
            _snwprintf_s(
                line, _TRUNCATE, L"LIBERER    %3u ennemis rallies",
                static_cast<unsigned>(rallied_total));
            g_soldier_menu_labels.emplace_back(line);
            g_soldier_actions.push_back(
                {SoldierMenuAction::ReleaseEnemies, 0});

            // V143 - « venir a moi », sans passer par la carte. C'est le
            // meme ordre de deplacement, avec VOTRE position comme
            // destination au lieu d'un clic.
            _snwprintf_s(
                line, _TRUNCATE, L"FEU ALL    %s",
                settings.fire_all_enabled
                    ? L"en cours : ils enchainent jusqu'au dernier"
                    : L"arrete  (Entree pour lancer l'enchainement)");
            g_soldier_menu_labels.emplace_back(line);
            g_soldier_actions.push_back({SoldierMenuAction::OpenFireAll, 0});

            settings.fire_count = (std::clamp)(
                settings.fire_count, 1, static_cast<int>(rallied_total));
            _snwprintf_s(
                line, _TRUNCATE,
                L"FEU        %3d allies vont au contact (sur %u)%s",
                settings.fire_count, static_cast<unsigned>(rallied_total),
                g_soldier_menu_selection >= 0 &&
                        g_soldier_actions.size() ==
                            static_cast<std::size_t>(g_soldier_menu_selection)
                    ? type_here
                    : L"");
            g_soldier_menu_labels.emplace_back(line);
            g_soldier_actions.push_back({SoldierMenuAction::OpenFire, 0});

            _snwprintf_s(
                line, _TRUNCATE, L"VENIR      les %3u rallies viennent a moi",
                static_cast<unsigned>(rallied_total));
            g_soldier_menu_labels.emplace_back(line);
            g_soldier_actions.push_back(
                {SoldierMenuAction::RalliedComeToMe, 0});

            _snwprintf_s(
                line, _TRUNCATE,
                L"ALLIES     envoyer les %3u rallies sur la carte",
                static_cast<unsigned>(rallied_total));
            g_soldier_menu_labels.emplace_back(line);
            g_soldier_actions.push_back({SoldierMenuAction::SendRallied, 0});

            _snwprintf_s(
                line, _TRUNCATE, L"SWITCH     %s",
                settings.switch_cycle_enabled
                    ? L"continue sur les allies apres vos soldats"
                    : L"reste aux seuls soldats du jeu");
            g_soldier_menu_labels.emplace_back(line);
            g_soldier_actions.push_back(
                {SoldierMenuAction::SwitchCycleToggle, 0});

            // V154 - la main libre : on prend un allie et on le pose ou on
            // veut, avec les memes touches que son propre vol.
            settings.free_move_index = (std::clamp)(
                settings.free_move_index, 1,
                static_cast<int>(rallied_total));
            _snwprintf_s(
                line, _TRUNCATE, L"MAIN LIBRE piloter l'allie %3d%s",
                settings.free_move_index,
                g_soldier_menu_selection >= 0 &&
                        g_soldier_actions.size() ==
                            static_cast<std::size_t>(g_soldier_menu_selection)
                    ? type_here
                    : L"");
            g_soldier_menu_labels.emplace_back(line);
            g_soldier_actions.push_back({SoldierMenuAction::FreeMoveAlly, 0});

            if (g_free_move_actor != 0)
            {
                g_soldier_menu_labels.emplace_back(
                    L"MAIN LIBRE -> rendre le pilotage a mon soldat");
                g_soldier_actions.push_back(
                    {SoldierMenuAction::FreeMoveRelease, 0});
            }
        }
        g_soldier_menu_labels.emplace_back(L"");
        g_soldier_actions.push_back({SoldierMenuAction::None, 0});

        if (g_created_soldiers_invulnerable && !g_spawned_soldiers.empty())
        {
            g_soldier_menu_labels.emplace_back(
                L"(4 soldats d'origine : les crees sont invulnerables)");
            g_soldier_actions.push_back({SoldierMenuAction::None, 0});
        }

        // Etat du miroir d'arme, et de quoi le basculer sans quitter la
        // fenetre - la touche J fait la meme chose en jeu.
        _snwprintf_s(
            line, _TRUNCATE, L"ARME       %s",
            settings.weapon_mirror_enabled
                ? L"ils prennent la votre (J pour arreter)"
                : L"ils gardent la leur  (J pour prendre la votre)");
        g_soldier_menu_labels.emplace_back(line);
        g_soldier_actions.push_back({SoldierMenuAction::WeaponMirror, 0});

        if (IsSanePointer(g_original_soldier) &&
            g_original_soldier != snapshot->player_object_address)
        {
            g_soldier_menu_labels.emplace_back(
                L"REVENIR a mon soldat d'origine");
            g_soldier_actions.push_back(
                {SoldierMenuAction::ReturnToOriginal, 0});
        }
        if (!g_spawned_soldiers.empty())
        {
            _snwprintf_s(
                line, _TRUNCATE, L"SUPPRIMER  %3u soldats crees",
                static_cast<unsigned>(g_spawned_soldiers.size()));
            g_soldier_menu_labels.emplace_back(line);
            g_soldier_actions.push_back(
                {SoldierMenuAction::RemoveCreated, 0});
        }
    }
    for (std::size_t index = 0; index < troop_count; ++index)
    {
        wchar_t label[128]{};
        const bool controlled =
            g_soldier_menu[index] == snapshot->player_object_address;
        _snwprintf_s(
            label, _TRUNCATE, L"Soldat %-3u %s",
            static_cast<unsigned>(index + 1),
            controlled ? L"- vous le pilotez" : L"- Entree : le piloter");
        g_soldier_menu_labels.emplace_back(label);
        g_soldier_actions.push_back({SoldierMenuAction::Control, index});

        // Une seconde ligne par soldat : l'envoyer seul sur un point de la
        // carte. Le joueur reprochait a juste titre de ne rien voir pour
        // « leur choisir une place dans la carte ».
        _snwprintf_s(
            label, _TRUNCATE, L"   -> envoyer le soldat %-3u sur la carte",
            static_cast<unsigned>(index + 1));
        g_soldier_menu_labels.emplace_back(label);
        g_soldier_actions.push_back({SoldierMenuAction::SendOne, index});
    }

    // V142 - une ligne par allie rallie, pour le commander seul.
    //
    // Le commandement passe par `AddProgram(0, PRG_MOVE, ...)`, qui est le
    // mecanisme NATIF des ennemis - c'est ainsi que la mission leur dicte
    // leurs rondes. Il fonctionne donc sur eux sans rien detourner.
    for (std::size_t index = 0; index < g_rallied_enemies.size(); ++index)
    {
        wchar_t label[128]{};
        _snwprintf_s(
            label, _TRUNCATE, L"   -> envoyer l'allie %-3u sur la carte",
            static_cast<unsigned>(index + 1));
        g_soldier_menu_labels.emplace_back(label);
        g_soldier_actions.push_back(
            {SoldierMenuAction::SendOneRallied, index});
    }

    // V159 - a l'ouverture, on se replace sur le dernier choix.
    if (g_soldier_menu_just_opened)
    {
        g_soldier_menu_just_opened = false;
        int restored = 0;
        if (g_soldier_last_valid)
        {
            for (std::size_t i = 0; i < g_soldier_actions.size(); ++i)
            {
                if (g_soldier_actions[i].action == g_soldier_last_action &&
                    g_soldier_actions[i].index == g_soldier_last_index)
                {
                    restored = static_cast<int>(i);
                    break;
                }
            }
        }
        g_soldier_menu_selection = restored;
    }

    const int entry_count = static_cast<int>(g_soldier_menu_labels.size());
    if (settings.soldier_menu_move != 0 && entry_count > 0)
    {
        g_soldier_menu_selection =
            (g_soldier_menu_selection + settings.soldier_menu_move +
             entry_count) % entry_count;
        settings.soldier_menu_move = 0;
    }
    if (g_soldier_menu_selection >= entry_count)
        g_soldier_menu_selection = 0;

    if (!settings.soldier_menu_confirm_requested)
        return;
    settings.soldier_menu_confirm_requested = false;
    g_soldier_count_typing = false;

    g_soldier_order_targets.clear();
    if (g_soldier_menu_selection < 0 ||
        static_cast<std::size_t>(g_soldier_menu_selection) >=
            g_soldier_actions.size())
    {
        return;
    }
    const SoldierMenuEntryData chosen =
        g_soldier_actions[static_cast<std::size_t>(g_soldier_menu_selection)];
    // V142 - une ligne d'information ne declenche rien et ne ferme pas la
    // fenetre : le joueur reste ou il est.
    if (chosen.action == SoldierMenuAction::None)
        return;
    // V159 - on retient ce choix pour la prochaine ouverture.
    g_soldier_last_valid = true;
    g_soldier_last_action = chosen.action;
    g_soldier_last_index = chosen.index;
    if (chosen.action == SoldierMenuAction::Create)
    {
        // Creer des soldats a partir du soldat pilote : le modele duplique
        // porte son uniforme, donc celui de la mission en cours.
        if (!can_spawn)
        {
            LogDiagnostic(
                "Creation de soldats refusee : les adresses du moteur ne "
                "sont pas toutes resolues sur cette version du jeu.");
            settings.soldier_menu_open = false;
            return;
        }
        // V130 - une limite sur le TOTAL vivant, pas seulement par creation.
        //
        // Le journal du joueur montre 116 soldats crees presents en meme temps
        // - 50, puis 21, puis quatre fois 10, puis 5 - avant que le jeu
        // s'arrete. La limite de cinquante ne portait que sur UNE creation :
        // rien n'empechait de les cumuler.
        //
        // Chaque soldat cree est un acteur que le moteur fait vivre a chaque
        // image sans qu'il ait ete initialise. Au-dela d'une certaine
        // quantite, le jeu ne tient pas. On refuse donc d'aller plus loin, et
        // on dit quoi faire.
        const std::size_t already = g_spawned_soldiers.size();
        if (already + static_cast<std::size_t>(settings.soldier_spawn_count) >
            kMaximumSpawnedSoldiers)
        {
            LogDiagnostic(
                "Creation refusee : %u soldat(s) cree(s) sont deja presents, "
                "et %d de plus depasserait la limite de %u. Employez la ligne "
                "SUPPRIMER de la fenetre G avant d'en creer d'autres - au-dela "
                "de cette quantite le jeu ne tient pas.",
                static_cast<unsigned>(already), settings.soldier_spawn_count,
                static_cast<unsigned>(kMaximumSpawnedSoldiers));
            settings.soldier_menu_open = false;
            g_soldier_menu_labels.clear();
            return;
        }

        std::vector<Vector3> destinations;
        destinations.reserve(
            static_cast<std::size_t>(settings.soldier_spawn_count));
        const float heading = snapshot->player.heading_radians;
        // V112 - placement RESSERRE, en couronnes autour du joueur.
        //
        // La grille precedente s'etirait devant lui : avec cinquante soldats
        // elle atteignait une vingtaine de metres. Or la hauteur employee est
        // celle du joueur, la meme pour tous - le trainer ne sait pas
        // interroger le relief. Sur un terrain qui monte, un soldat pose vingt
        // metres plus loin se retrouve SOUS LA TERRE; sur un terrain qui
        // descend, en l'air. C'est exactement ce que le joueur a decrit.
        //
        // Les couronnes gardent tout le monde a moins de neuf metres, ou le
        // relief a peu de chances d'avoir change de maniere sensible. Douze
        // soldats par couronne, la premiere a deux metres.
        constexpr int kPerRing = 12;
        constexpr float kFirstRing = 2.0f;
        constexpr float kRingStep = 1.5f;
        for (int index = 0; index < settings.soldier_spawn_count; ++index)
        {
            const int ring = index / kPerRing;
            const int seat = index % kPerRing;
            const float radius =
                kFirstRing + static_cast<float>(ring) * kRingStep;
            // Les couronnes sont decalees d'un demi-pas les unes par rapport
            // aux autres pour que les soldats ne s'alignent pas radialement.
            const float angle = heading +
                (static_cast<float>(seat) +
                 (ring % 2 ? 0.5f : 0.0f)) *
                    (6.2831853f / static_cast<float>(kPerRing));
            Vector3 destination = snapshot->player.position;
            destination.x += std::sin(angle) * radius;
            destination.z += std::cos(angle) * radius;
            destinations.push_back(destination);
        }
        std::vector<std::uintptr_t> created;
        const bool spawned = SpawnActorsOnGameThread(
            process, capability, snapshot->player_frame_address,
            destinations, static_cast<std::uint8_t>(kActorTypePlayer),
            created);
        if (spawned)
        {
            g_spawned_soldiers.insert(
                g_spawned_soldiers.end(), created.begin(), created.end());
            // De nouveaux soldats viennent d'arriver : ils n'ont pas d'arme.
            // On oublie la derniere arme distribuee pour que le miroir la leur
            // donne au prochain tour, sans attendre que le joueur change
            // d'arme.
            g_weapon_mirror_last_item = -1;

            // ==========================================================
            // V114 - `menu_id` valide plutot qu'immortalite
            // ==========================================================
            //
            // La V113 posait `no_hit_cheat` sur chaque soldat cree pour couper
            // le chemin qui menait au plantage. Cela marchait, mais les rendait
            // IMMORTELS - ce que le joueur a vu tout de suite, et a juste titre.
            //
            // `menu_id` est MESURE sur ses propres soldats : ils portent
            // forcement des identifiants distincts entre 0 et 3, et un seul
            // emplacement de la structure peut presenter cette propriete sur
            // trois soldats ou plus. On pose ensuite sur chaque soldat cree le
            // plus GRAND identifiant valide, ce qui donne deux choses a la
            // fois :
            //
            //   - `SetHealth` ne lit plus hors du tableau `pmenu[]`, donc plus
            //     de plantage quand un ennemi tire;
            //   - les soldats crees encaissent et MEURENT normalement.
            //
            // Le plus grand identifiant les range aussi en FIN de la liste
            // triee par `PlayerSwitch`, si bien que les touches 1 2 3 du jeu
            // continuent de designer les soldats du joueur.
            //
            // Contrepartie assumee : ils partagent la jauge du dernier soldat,
            // qui bougera quand ils sont touches. Defaut d'affichage, et de
            // loin le moindre des trois maux.
            //
            // Si la mesure n'aboutit pas, on retombe sur la protection de la
            // V113 : mieux vaut des soldats immortels qu'un jeu qui s'arrete.
            // Le journal dit lequel des deux cas s'applique.
            // Nommer les soldats crees. Le duplicata reprend le nom de
            // la frame source, que le jeu affiche « unknown » faute d'entree
            // de table. On pose donc un nom lisible sur chaque frame, par
            // `I3D_frame::SetName` (rang 28 de l'enumeration ancree sur
            // `SetPos`, celle-la meme que la duplication a validee en jeu).
            NameCreatedSoldiersOnGameThread(process, created);

            // Vider la main : le moteur exige une main vide pour y placer une
            // arme, et le duplicata en tient deja une, copiee du joueur.
            EmptySoldierHandsOnGameThread(
                process, created, snapshot->player_object_address);

            // Le nom AFFICHE ne vient pas de la frame mais du numero de
            // visage de la table : zero y vaut « unknown ».
            {
                std::vector<std::uintptr_t> reference_players = g_soldier_menu;
                reference_players.erase(
                    std::remove_if(
                        reference_players.begin(), reference_players.end(),
                        [&](std::uintptr_t actor)
                        {
                            return std::find(
                                       g_spawned_soldiers.begin(),
                                       g_spawned_soldiers.end(), actor) !=
                                g_spawned_soldiers.end();
                        }),
                    reference_players.end());
                NameCreatedSoldiersByFace(process, created, reference_players);
            }

            const std::uintptr_t menu_id_offset =
                MeasurePlayerMenuIdOffset(process);

            // ==========================================================
            // V126 - UN IDENTIFIANT HORS DU TABLEAU, RENDU INOFFENSIF
            // ==========================================================
            //
            // Toutes les regles precedentes tournaient autour d'un tableau de
            // QUATRE cases : chercher une case libre (V122), interdire celles
            // des vrais soldats (V123), se resoudre a l'invulnerabilite quand
            // il n'en restait aucune (V124). En mission a quatre soldats il
            // n'y avait aucune issue : soit un portrait du joueur etait
            // marque, soit ses soldats crees ne pouvaient pas encaisser - et
            // dans tous les cas leur identifiant a -1 les rangeait EN TETE du
            // tri de `PlayerSwitch`, ce qui lui volait ses touches 1 2 3 4.
            //
            // La garde posee a l'entree de `SetHealth` et `SetDeathFace`
            // supprime la contrainte a la racine : un identifiant hors du
            // tableau ne fait plus rien du tout. On donne donc aux soldats
            // crees un identifiant TRES GRAND, et les trois problemes tombent
            // ensemble :
            //
            //   - il les range en DERNIER dans le tri, donc les touches
            //     1 2 3 4 reviennent aux soldats du joueur, quelle que soit la
            //     taille de son escouade;
            //   - aucune lecture hors tableau n'a lieu, donc ils peuvent
            //     ENCAISSER ET MOURIR partout, y compris a quatre soldats;
            //   - aucun de ses portraits n'est marque, ni en sante ni en
            //     squelette, et une grenade passe par le meme chemin protege.
            const bool guard_ready = InstallMenuBoundsGuard(process);
            g_created_soldiers_invulnerable = !guard_ready;

            // La sante des soldats crees est celle du soldat pilote, relue a
            // chaque creation (V121). `init_resistance` valant zero sur un
            // acteur neuf, il faut de toute facon y poser quelque chose - et
            // la valeur du joueur est la seule qui ait un sens.
            std::int32_t model_init = 0;
            std::int32_t model_current = 0;
            if (!process.ReadMemory(
                    snapshot->player_object_address +
                        kPlayerInitResistanceOffset, model_init) ||
                !process.ReadMemory(
                    snapshot->player_object_address +
                        kPlayerResistanceOffset, model_current) ||
                model_init <= 0 || model_current <= 0)
            {
                model_init = 1600;
                model_current = 1600;
            }
            LogDiagnostic(
                "Creation d'acteurs: sante copiee sur votre soldat "
                "(init=%d, courante=%d).",
                model_init, model_current);

            unsigned health_set = 0;
            unsigned slot_set = 0;
            unsigned guard_set = 0;
            for (const std::uintptr_t actor : created)
            {
                if (process.WriteMemory(
                        actor + kPlayerInitResistanceOffset, model_init) &&
                    process.WriteMemory(
                        actor + kPlayerResistanceOffset, model_current))
                {
                    ++health_set;
                }
                if (guard_ready && menu_id_offset != 0 &&
                    process.WriteMemory(
                        actor + menu_id_offset, kCreatedSoldierMenuId))
                {
                    ++slot_set;
                }
                // Sans garde posee, on retombe sur la protection : mieux vaut
                // des soldats invulnerables qu'un jeu qui s'arrete.
                const std::int32_t no_hit = guard_ready ? 0 : 1;
                if (process.WriteMemory(
                        actor + kPlayerNoHitCheatOffset, no_hit))
                {
                    ++guard_set;
                }
            }
            LogDiagnostic(
                "Creation d'acteurs sur %u : sante posee chez %u, "
                "identifiant %d pose chez %u, etat de tir pose chez %u. %s",
                static_cast<unsigned>(created.size()), health_set,
                static_cast<int>(kCreatedSoldierMenuId), slot_set, guard_set,
                guard_ready
                    ? "Garde du bandeau active : ils meurent normalement et "
                      "vos touches 1 2 3 4 restent a vos soldats."
                    : "Garde du bandeau ABSENTE : ils sont invulnerables, "
                      "pour que le jeu ne s'arrete pas.");
        }
        LogDiagnostic(
            "Fenetre soldats: creation de %d soldat(s) resultat=%u.",
            settings.soldier_spawn_count, spawned ? 1U : 0U);
        settings.soldier_menu_open = false;
        g_soldier_menu_labels.clear();
        return;
    }
    if (chosen.action == SoldierMenuAction::Group)
    {
        // GROUPE : exactement le nombre demande, pris dans l'ordre de la
        // liste, sans jamais depasser l'effectif reel.
        const std::size_t group = (std::min)(
            static_cast<std::size_t>(
                (std::max)(1, settings.soldier_group_count)),
            g_soldier_menu.size());
        g_soldier_order_targets.assign(
            g_soldier_menu.begin(), g_soldier_menu.begin() + group);
    }
    else if (chosen.action == SoldierMenuAction::All)
    {
        g_soldier_order_targets = g_soldier_menu;
    }
    else if (chosen.action == SoldierMenuAction::WeaponMirror)
    {
        settings.weapon_mirror_toggle_requested = true;
        return;
    }
    else if (chosen.action == SoldierMenuAction::RallyEnemies)
    {
        // ==============================================================
        // V142 - LE RALLIEMENT : UNE ECRITURE D'ENTIER, ET RIEN D'AUTRE
        // ==============================================================
        //
        // On prend les N premiers ennemis encore allemands et on ecrit 1
        // (russe) dans leur propriete 74. Les deux fonctions d'hostilite du
        // moteur lisent ce nombre, et leur intelligence choisit ses cibles
        // avec les memes fonctions : ils cessent de nous chercher et se
        // mettent a chercher les Allemands.
        //
        // Aucune fiche a remplir, aucune main a forcer, aucune fonction du
        // moteur a appeler. C'est ce qui rend ce chemin sur la ou les sept
        // precedents ont echoue.
        const std::vector<std::uintptr_t> hostile = EnemiesStillHostile();
        if (hostile.empty())
        {
            LogDiagnostic(
                "Ralliement: aucun ennemi allemand en vue, rien a faire.");
            settings.soldier_menu_open = false;
            g_soldier_menu_labels.clear();
            return;
        }
        const std::size_t wanted = (std::min)(
            static_cast<std::size_t>(
                (std::max)(1, settings.enemy_rally_count)),
            hostile.size());
        const std::vector<std::uintptr_t> picked(
            hostile.begin(), hostile.begin() + wanted);

        std::vector<std::uintptr_t> donors = g_soldier_menu;
        donors.erase(
            std::remove_if(
                donors.begin(), donors.end(),
                [&](std::uintptr_t actor)
                {
                    return std::find(
                               g_spawned_soldiers.begin(),
                               g_spawned_soldiers.end(), actor) !=
                        g_spawned_soldiers.end();
                }),
            donors.end());
        const std::uintptr_t table_offset =
            MeasureActorTableOffset(process, donors, g_reference_enemies);
        if (table_offset == 0)
        {
            LogDiagnostic(
                "Ralliement: la fiche des acteurs n'est pas situee, refuse.");
            settings.soldier_menu_open = false;
            g_soldier_menu_labels.clear();
            return;
        }
        // V162 - on releve d'abord les camps en face, puis on choisit le
        // notre en consequence.
        std::map<std::int32_t, unsigned> camps;
        for (const std::uintptr_t enemy : g_mission_enemies)
        {
            if (IsRalliedEnemy(enemy))
                continue;
            const std::uintptr_t address = ResolveActorTableInteger(
                process, enemy, kEnemyGroupProperty, table_offset);
            std::int32_t value = -1;
            if (address != 0 && process.ReadMemory(address, value))
                ++camps[value];
        }
        const std::int32_t ally_group = ChooseAllyGroup(camps);
        std::vector<std::uintptr_t> changed;
        const unsigned turned = SetEnemyGroup(
            process, picked, table_offset, ally_group, changed);
        for (const std::uintptr_t actor : changed)
        {
            if (!IsRalliedEnemy(actor))
                g_rallied_enemies.push_back(actor);
        }
        LogDiagnostic(
            "Ralliement: les ennemis sont pris du PLUS PROCHE au plus loin; "
            "le premier est a %d metres de vous. Ils ne se deplacent pas - "
            "utilisez VENIR ou la carte.",
            g_nearest_hostile_distance >= 0.0f
                ? static_cast<int>(g_nearest_hostile_distance + 0.5f)
                : -1);
        {
            char list[160] = {};
            int used = 0;
            for (const auto& entry : camps)
            {
                used += _snprintf_s(
                    list + used, sizeof(list) - used, _TRUNCATE,
                    "%sgroupe %d : %u", used ? ", " : "", entry.first,
                    entry.second);
                if (used < 0 || used >= static_cast<int>(sizeof(list)) - 24)
                    break;
            }
            LogDiagnostic(
                "Ralliement: camps en face avant le ralliement -> %s. Les "
                "votres passent au groupe %d, qu'aucun d'eux n'utilise : ils "
                "sont donc hostiles a tous ces camps, et amis avec vous.",
                used > 0 ? list : "aucun", static_cast<int>(ally_group));
        }
        LogDiagnostic(
            "Ralliement: %u ennemi(s) demande(s), %u passe(s) de votre cote "
            "(groupe %d ecrit et RELU dans la propriete %u de leur fiche). "
            "Total de votre cote : %u sur %u ennemis de la mission.",
            static_cast<unsigned>(wanted), turned,
            static_cast<int>(ally_group), kEnemyGroupProperty,
            static_cast<unsigned>(g_rallied_enemies.size()),
            static_cast<unsigned>(g_mission_enemies.size()));

        // V163 - on leve d'abord leurs limites de poursuite, sinon les
        // gardes statiques refuseront de quitter leur poste.
        if (turned != 0)
            (void)FreeAllyAiLimits(process, changed, table_offset);

        // V160 - LE COMBAT S'ENGAGE TOUT DE SUITE, SANS ATTENDRE.
        //
        // Le joueur : « je veux qu'ils soient instantanement des le
        // ralliement ». Sans cela, deux groupes qui se tournent le dos ne se
        // verront jamais - c'est ce qui a ete etabli au chapitre 56.
        //
        // On les tourne donc vers leur ennemi le plus proche des la seconde
        // ou ils changent de camp. Leur IA fait le reste.
        if (turned != 0)
        {
            std::vector<std::uintptr_t> watchers;
            std::vector<std::uintptr_t> subjects;
            std::vector<Vector3> destinations;
            BuildOpenFirePairs(watchers, subjects, destinations);
            if (!watchers.empty())
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(60));
                const bool engaged = OrderEngageOnGameThread(
                    process, watchers, subjects, destinations);
                LogDiagnostic(
                    "Ralliement: %u nouvel(s) allie(s) envoye(s) au contact "
                    "de l'ennemi le plus proche des le ralliement, "
                    "resultat=%u.",
                    static_cast<unsigned>(watchers.size()),
                    engaged ? 1U : 0U);
            }
        }
        settings.soldier_menu_open = false;
        g_soldier_menu_labels.clear();
        return;
    }
    else if (chosen.action == SoldierMenuAction::ReleaseEnemies)
    {
        std::vector<std::uintptr_t> donors = g_soldier_menu;
        donors.erase(
            std::remove_if(
                donors.begin(), donors.end(),
                [&](std::uintptr_t actor)
                {
                    return std::find(
                               g_spawned_soldiers.begin(),
                               g_spawned_soldiers.end(), actor) !=
                        g_spawned_soldiers.end();
                }),
            donors.end());
        const std::uintptr_t table_offset =
            MeasureActorTableOffset(process, donors, g_reference_enemies);
        // V157 - chacun retrouve SON camp, pas forcement l'allemand.
        unsigned back = 0;
        for (const std::uintptr_t enemy : g_rallied_enemies)
        {
            const auto found = g_rallied_origin.find(enemy);
            const std::int32_t origin =
                found != g_rallied_origin.end() ? found->second
                                                : kEnemyGroupGerman;
            std::vector<std::uintptr_t> one_changed;
            back += SetEnemyGroup(
                process, std::vector<std::uintptr_t>{enemy}, table_offset,
                origin, one_changed);
        }
        LogDiagnostic(
            "Ralliement: %u ennemi(s) rendu(s) a LEUR camp d'origine sur %u.",
            back, static_cast<unsigned>(g_rallied_enemies.size()));
        g_rallied_enemies.clear();
        g_rallied_origin.clear();
        settings.soldier_menu_open = false;
        g_soldier_menu_labels.clear();
        return;
    }
    else if (chosen.action == SoldierMenuAction::OpenFireAll)
    {
        settings.fire_all_toggle_requested = true;
        settings.soldier_menu_open = false;
        g_soldier_menu_labels.clear();
        return;
    }
    else if (chosen.action == SoldierMenuAction::OpenFire)
    {
        // Chaque rallie prend l'ennemi encore hostile le plus proche de LUI.
        const std::vector<std::uintptr_t> hostile = EnemiesStillHostile();
        if (hostile.empty() || g_rallied_enemies.empty())
        {
            LogDiagnostic(
                "Ouverture du feu: %s, rien a ordonner.",
                hostile.empty() ? "plus aucun ennemi en face"
                                : "aucun allie rallie");
            settings.soldier_menu_open = false;
            g_soldier_menu_labels.clear();
            return;
        }
        std::vector<std::uintptr_t> watchers;
        std::vector<std::uintptr_t> subjects;
        std::vector<Vector3> destinations;
        BuildOpenFirePairs(watchers, subjects, destinations);
        // V163 - le joueur choisit combien partent; les autres restent en
        // place. Ils sont pris dans l'ordre de la liste, donc les premiers
        // rallies d'abord.
        const std::size_t wanted_fire = (std::min)(
            static_cast<std::size_t>((std::max)(1, settings.fire_count)),
            watchers.size());
        watchers.resize(wanted_fire);
        subjects.resize(wanted_fire);
        destinations.resize(wanted_fire);
        // Et on leve aussi leurs limites de poursuite : c'est la condition
        // pour qu'ils quittent leur poste.
        {
            std::vector<std::uintptr_t> donors = g_soldier_menu;
            const std::uintptr_t offset = MeasureActorTableOffset(
                process, donors, g_reference_enemies);
            if (offset != 0)
                (void)FreeAllyAiLimits(process, watchers, offset);
        }
        settings.soldier_menu_open = false;
        g_soldier_menu_labels.clear();
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        const bool fired = OrderEngageOnGameThread(
            process, watchers, subjects, destinations);
        LogDiagnostic(
            "Ouverture du feu: %u allie(s) avancent vers l'ennemi le plus "
            "proche d'eux, resultat=%u. Ils ne recoivent PAS l'ordre de tirer "
            "- c'est leur IA qui l'ajoute des qu'ils l'ont en vue, par le "
            "chemin normal du jeu.",
            static_cast<unsigned>(watchers.size()), fired ? 1U : 0U);
        return;
    }
    else if (chosen.action == SoldierMenuAction::RalliedComeToMe)
    {
        // Le meme ordre que par la carte, avec la position du joueur comme
        // destination. `AddProgram(0, PRG_MOVE, ...)` est le mecanisme natif
        // des ennemis : rien n'est detourne.
        const bool ordered =
            !g_rallied_enemies.empty() &&
            IssueMoveOrderOnGameThread(
                process, g_rallied_enemies, snapshot->player.position);
        LogDiagnostic(
            "Ralliement: %u allie(s) rappele(s) a votre position "
            "resultat=%u.",
            static_cast<unsigned>(g_rallied_enemies.size()),
            ordered ? 1U : 0U);
        settings.soldier_menu_open = false;
        g_soldier_menu_labels.clear();
        return;
    }
    else if (chosen.action == SoldierMenuAction::FreeMoveAlly)
    {
        const std::size_t one = static_cast<std::size_t>(
            (std::max)(1, settings.free_move_index) - 1);
        if (one < g_rallied_enemies.size())
        {
            g_free_move_actor = g_rallied_enemies[one];
            LogDiagnostic(
                "Main libre: l'allie %u (%08X) est pris en main. Activez le "
                "vol (touche V ou la case Noclip) et il se deplacera avec vos "
                "touches, meme la ou aucun chemin ne mene. Votre soldat, lui, "
                "ne bouge plus.",
                static_cast<unsigned>(one + 1),
                static_cast<unsigned>(g_free_move_actor));
            // V155 - et la camera va sur lui, pour qu'on le voie.
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
            (void)FocusCameraOnActor(process, g_free_move_actor);
        }
        else
        {
            LogDiagnostic(
                "Main libre: il n'y a pas d'allie numero %d.",
                settings.free_move_index);
        }
        settings.soldier_menu_open = false;
        g_soldier_menu_labels.clear();
        return;
    }
    else if (chosen.action == SoldierMenuAction::SwitchCycleToggle)
    {
        settings.switch_cycle_toggle_requested = true;
        return;
    }
    else if (chosen.action == SoldierMenuAction::FreeMoveRelease)
    {
        ClearFreeMove("vous l'avez rendu");
        // V155 - la camera revient sur votre propre soldat.
        settings.soldier_menu_open = false;
        g_soldier_menu_labels.clear();
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        (void)FocusCameraOnActor(process, snapshot->player_object_address);
        return;
    }
    else if (chosen.action == SoldierMenuAction::SendRallied)
    {
        g_soldier_order_targets = g_rallied_enemies;
        LogDiagnostic(
            "Ralliement: %u allie(s) arme(s) pour le prochain clic sur la "
            "carte native (K).",
            static_cast<unsigned>(g_soldier_order_targets.size()));
        settings.soldier_menu_open = false;
        g_soldier_menu_labels.clear();
        return;
    }
    else if (chosen.action == SoldierMenuAction::SendOneRallied)
    {
        if (chosen.index < g_rallied_enemies.size())
        {
            g_soldier_order_targets.assign(
                1, g_rallied_enemies[chosen.index]);
            LogDiagnostic(
                "Ralliement: allie %u arme pour le prochain clic sur la carte "
                "native (K).",
                static_cast<unsigned>(chosen.index + 1));
        }
        settings.soldier_menu_open = false;
        g_soldier_menu_labels.clear();
        return;
    }
    else if (chosen.action == SoldierMenuAction::SendOne)
    {
        if (chosen.index < g_soldier_menu.size())
        {
            g_soldier_order_targets.assign(
                1, g_soldier_menu[chosen.index]);
            LogDiagnostic(
                "Fenetre soldats: soldat %u arme pour le prochain clic sur la "
                "carte native (K).",
                static_cast<unsigned>(chosen.index + 1));
        }
        settings.soldier_menu_open = false;
        g_soldier_menu_labels.clear();
        return;
    }
    else if (chosen.action == SoldierMenuAction::RemoveCreated)
    {
        // Retirer les soldats crees rend au jeu son comportement normal :
        // les touches 1 2 3 4 designent de nouveau les soldats du joueur.
        // `capability` est deja resolue plus haut dans cette fonction : elle
        // porte le pointeur de mission dont DestroyActor a besoin.
        const std::uintptr_t destroy_actor = FindDestroyActor(process);
        std::vector<std::uintptr_t> doomed = g_spawned_soldiers;
        // Ne jamais supprimer celui que le joueur pilote : il se retrouverait
        // sans corps. On le quitte d'abord si besoin.
        const auto piloted = std::find(
            doomed.begin(), doomed.end(), snapshot->player_object_address);
        if (piloted != doomed.end() && IsSanePointer(g_original_soldier))
        {
            (void)SwitchControlledSoldier(
                process, snapshot->player_object_address, g_original_soldier);
        }
        const bool removed = DestroyActorsOnGameThread(
            process, capability.mission, destroy_actor, doomed);
        if (removed)
            g_spawned_soldiers.clear();
        LogDiagnostic(
            "Fenetre soldats: suppression de %u soldat(s) resultat=%u.",
            static_cast<unsigned>(doomed.size()), removed ? 1U : 0U);
        settings.soldier_menu_open = false;
        g_soldier_menu_labels.clear();
        return;
    }
    else if (chosen.action == SoldierMenuAction::ReturnToOriginal)
    {
        // Retour au soldat du debut. C'est la meme bascule que PlayerSwitch,
        // avec le seul rang de vtable que le jeu valide a chaque partie.
        const bool switched = SwitchControlledSoldier(
            process, snapshot->player_object_address, g_original_soldier);
        LogDiagnostic(
            "Fenetre soldats: retour au soldat d'origine %08X resultat=%u.",
            static_cast<unsigned>(g_original_soldier), switched ? 1U : 0U);
        settings.soldier_menu_open = false;
        g_soldier_menu_labels.clear();
        return;
    }
    else
    {
        // Une ligne de soldat : on prend son controle, comme le fait le jeu
        // lui-meme dans PlayerSwitch.
        if (chosen.index < g_soldier_menu.size())
        {
            (void)SwitchControlledSoldier(
                process, snapshot->player_object_address,
                g_soldier_menu[chosen.index]);
        }
        settings.soldier_menu_open = false;
        g_soldier_menu_labels.clear();
        return;
    }
    LogDiagnostic(
        "Fenetre soldats: %u soldat(s) armes pour le prochain clic sur la "
        "carte native (K).",
        static_cast<unsigned>(g_soldier_order_targets.size()));
    settings.soldier_menu_open = false;
    g_soldier_menu_labels.clear();
}

void UpdateVehicleMenu(
    TrainerProcess& process,
    const RadarSnapshot* snapshot,
    GameplaySettings& settings,
    GameplayStatus& status)
{
    if (settings.vehicle_menu_toggle_requested)
    {
        settings.vehicle_menu_toggle_requested = false;
        settings.vehicle_menu_open = !settings.vehicle_menu_open;
        g_vehicle_menu_selection = 0;
    }
    if (!settings.vehicle_menu_open || !process.IsConnected() || !snapshot ||
        !IsSanePointer(snapshot->player_object_address))
    {
        if (!settings.vehicle_menu_open)
        {
            g_vehicle_menu.clear();
            g_vehicle_menu_labels.clear();
        }
        settings.vehicle_menu_move = 0;
        settings.vehicle_menu_confirm_requested = false;
        return;
    }

    (void)CollectMissionVehicles(process, *snapshot, g_vehicle_menu);
    g_vehicle_menu_labels.clear();
    for (std::size_t index = 0; index < g_vehicle_menu.size(); ++index)
    {
        const MissionVehicle& entry = g_vehicle_menu[index];
        wchar_t label[96]{};
        _snwprintf_s(
            label, _TRUNCATE, L"%2u. %s  -  %.0f m%s",
            static_cast<unsigned>(index + 1),
            entry.type == kActorTypeAutoCannon ? L"Canon" : L"Vehicule",
            entry.distance,
            entry.version == 0
                ? L"      [Entree = voler,  C = recreer une copie]"
                : L"   (abime)   [Entree = voler,  C = recreer une copie]");
        g_vehicle_menu_labels.emplace_back(label);
    }
    if (g_vehicle_menu.empty())
    {
        g_vehicle_menu_selection = 0;
        settings.vehicle_menu_move = 0;
        settings.vehicle_menu_confirm_requested = false;
        return;
    }

    if (settings.vehicle_menu_move != 0)
    {
        const int count = static_cast<int>(g_vehicle_menu.size());
        g_vehicle_menu_selection =
            (g_vehicle_menu_selection + settings.vehicle_menu_move + count) %
            count;
        settings.vehicle_menu_move = 0;
    }
    if (g_vehicle_menu_selection >= static_cast<int>(g_vehicle_menu.size()))
        g_vehicle_menu_selection = 0;

    if (!settings.vehicle_menu_confirm_requested &&
        !settings.vehicle_menu_clone_requested)
    {
        return;
    }
    const bool clone_requested = settings.vehicle_menu_clone_requested;
    settings.vehicle_menu_confirm_requested = false;
    settings.vehicle_menu_clone_requested = false;

    const MissionVehicle& chosen =
        g_vehicle_menu[static_cast<std::size_t>(g_vehicle_menu_selection)];
    const Vector3 destination = PointInFrontOfPlayer(*snapshot, 5.0f);
    if (clone_requested)
    {
        // « Recreer une copie » : le modele est duplique, et l'acteur cree
        // se reconfigure tout seul depuis la hierarchie copiee - roues,
        // sieges, volant, moteur et collision sont retrouves par leurs noms
        // dans `C_automobil::SetFrame` (Vehicle.cpp:1784). Le vehicule obtenu
        // est donc conduisible, pas decoratif.
        SpawnCapability capability{};
        std::vector<std::uintptr_t> created;
        const bool ready =
            ResolveSpawnCapability(process, *snapshot, capability);
        const bool cloned = ready && SpawnActorsOnGameThread(
            process, capability, chosen.frame,
            std::vector<Vector3>{destination},
            static_cast<std::uint8_t>(kActorTypeAutomobile), created);
        LogDiagnostic(
            "Menu vehicules: copie de %08X resultat=%u (aptitude=%u).",
            static_cast<unsigned>(chosen.actor), cloned ? 1U : 0U,
            ready ? 1U : 0U);
        status.vehicle_repair = cloned
            ? VehicleActionStatus::Success
            : VehicleActionStatus::Failed;
        settings.vehicle_menu_open = false;
        g_vehicle_menu.clear();
        g_vehicle_menu_labels.clear();
        return;
    }

    (void)RepairVehicleFields(process, chosen.actor);
    const bool moved = SetFramePositionOnMainThread(
        process, chosen.frame, destination, chosen.actor,
        snapshot->scene_object_address);
    LogDiagnostic(
        "Menu vehicules: choix=%d acteur=%08X deplace=%u.",
        g_vehicle_menu_selection,
        static_cast<unsigned>(chosen.actor), moved ? 1U : 0U);
    status.vehicle_repair = moved
        ? VehicleActionStatus::Success
        : VehicleActionStatus::Failed;
    settings.vehicle_menu_open = false;
    g_vehicle_menu.clear();
    g_vehicle_menu_labels.clear();
}

void UpdateVehicleExit(
    TrainerProcess& process,
    const RadarSnapshot* snapshot,
    GameplaySettings& settings,
    GameplayStatus& status)
{
    if (!settings.vehicle_exit_requested)
        return;
    settings.vehicle_exit_requested = false;

    if (!process.IsConnected() || !snapshot ||
        !IsSanePointer(snapshot->player_object_address))
    {
        status.vehicle_exit = VehicleActionStatus::WaitingForPlayer;
        return;
    }
    std::uintptr_t using_item = 0;
    std::uint32_t type = 0;
    if (!process.ReadMemory(
            snapshot->player_object_address + kActorUsingItemOffset,
            using_item) ||
        !IsSanePointer(using_item) ||
        !process.ReadMemory(using_item + kActorTypeOffset, type) ||
        (type != kActorTypeAutomobile && type != kActorTypeAutoCannon))
    {
        LogDiagnostic(
            "Sortie vehicule: aucun vehicule occupe (using_item=%08X type=%u); "
            "appel natif refuse.",
            static_cast<unsigned>(using_item), static_cast<unsigned>(type));
        status.vehicle_exit = VehicleActionStatus::NoVehicle;
        return;
    }
    // La position de descente vient du vehicule lui-meme : `case 7` renvoie
    // `&(seat->entry ? entry : seat)->GetWorldPos()`, c'est-a-dire la position
    // MONDE MISE EN CACHE du frame d'entree. Tant que la hierarchie du vehicule
    // n'a pas ete reactualisee, ce cache date du moment ou l'on est monte :
    // c'est exactement ce que le joueur a constate, etre repose la ou il avait
    // pris la voiture. On lit donc la position reelle du vehicule AVANT la
    // sortie, et on repose le soldat a cote une fois la sortie native faite.
    std::uintptr_t vehicle_frame = 0;
    Vector3 vehicle_position{};
    const bool vehicle_located =
        process.ReadMemory(using_item + kActorFrameOffset, vehicle_frame) &&
        IsSanePointer(vehicle_frame) &&
        process.ReadMemory(
            vehicle_frame + kFrameWorldPositionOffset, vehicle_position) &&
        IsSaneWorldPosition(vehicle_position);

    // V103 : « sortir dans n'importe quel endroit ».
    // Le chemin natif de descente teste trois positions de degagement autour du
    // siege et renonce si aucune ne convient - vehicule en vol, coince contre
    // un mur, ou simplement en mouvement. On arrete donc le vehicule AVANT
    // d'appeler la descente, en le reposant a sa propre position : le stub de
    // placement commence par CB_USE_AUTO(13), l'arret natif, puis rejoue
    // SetPos/Update/SetFrameSector. Un vehicule immobile et correctement
    // raccroche a son secteur donne au test de collision toutes ses chances.
    if (vehicle_located)
    {
        const bool stopped = SetFramePositionOnMainThread(
            process, vehicle_frame, vehicle_position, using_item,
            snapshot->scene_object_address);
        LogDiagnostic(
            "Sortie vehicule: arret prealable du vehicule=%08X resultat=%u.",
            static_cast<unsigned>(using_item), stopped ? 1U : 0U);
    }

    const bool done = ApplyNativeActorCallback(
        process, snapshot, kUseAutomobileCallback, 0);
    // Le rappel natif peut reussir sans que la descente ait eu lieu : c'est le
    // cas quand aucune place n'a ete trouvee. La seule preuve fiable est que le
    // joueur n'occupe plus rien.
    std::uintptr_t still_using = 0;
    const bool left_vehicle = done &&
        process.ReadMemory(
            snapshot->player_object_address + kActorUsingItemOffset,
            still_using) &&
        !IsSanePointer(still_using);
    bool placed = false;
    if (left_vehicle && vehicle_located &&
        IsSanePointer(snapshot->player_frame_address))
    {
        Vector3 destination = vehicle_position;
        const float heading = snapshot->player.heading_radians;
        destination.x += std::sin(heading) * 3.0f;
        destination.z += std::cos(heading) * 3.0f;
        placed = SetFramePositionOnMainThread(
            process, snapshot->player_frame_address, destination);
    }
    LogDiagnostic(
        "Sortie vehicule: appel natif CB_USE_AUTO(0,0) vehicule=%08X "
        "rappel=%u sorti=%u repose_a_cote=%u.",
        static_cast<unsigned>(using_item), done ? 1U : 0U,
        left_vehicle ? 1U : 0U, placed ? 1U : 0U);
    status.vehicle_exit = left_vehicle
        ? VehicleActionStatus::Success
        : VehicleActionStatus::Failed;
}

void UpdateGameplayModifiers(
    TrainerProcess& game_process,
    const RadarSnapshot* radar_snapshot,
    GameplaySettings& settings,
    const GameplayInput& input,
    GameplayStatus& status)
{
    // The trainer UI and the game run on separate windows. During a network
    // mission a transient UI state must never tear down active damage guards:
    // once the user explicitly arms total protection, retain that decision for
    // this game process until the same card explicitly requests a disarm.
    struct AbsoluteProtectionLatch
    {
        DWORD process_id = 0;
        bool armed = false;
        AbsolutePlayerProtectionScope scope =
            AbsolutePlayerProtectionScope::ControlledPlayer;
    };
    static AbsoluteProtectionLatch protection_latch{};
    const DWORD protection_process_id = game_process.ProcessId();
    if (protection_latch.process_id != protection_process_id)
    {
        protection_latch = {};
        protection_latch.process_id = protection_process_id;
    }
    if (settings.absolute_player_protection_enabled)
    {
        protection_latch.armed = true;
        protection_latch.scope = settings.absolute_player_protection_scope;
    }
    else if (settings.absolute_player_protection_toggle_requested)
    {
        protection_latch.armed = false;
        LogDiagnostic("Absolute protection: explicit UI disarm accepted.");
    }
    else if (protection_latch.armed)
    {
        settings.absolute_player_protection_enabled = true;
        settings.absolute_player_protection_scope = protection_latch.scope;
        LogDiagnostic(
            "Absolute protection: session latch restored protection "
            "scope=%u after transient UI state.",
            static_cast<unsigned>(protection_latch.scope));
    }
    settings.absolute_player_protection_toggle_requested = false;

    // V95 : rend les pages des crochets retires des qu'aucun thread du jeu
    // n'y execute plus rien. Une seule sonde par image, sans attente : c'est
    // ce qui permet a un decochage d'etre applique instantanement.
    ProcessPendingRemoteReleases(game_process);
    // V147 - la liste des rallies est nettoyee a CHAQUE image, fenetre G
    // ouverte ou non. Elle ne l'etait qu'a l'ouverture de la fenetre, et le
    // trainer a garde vingt-sept secondes durant les adresses d'hommes morts
    // au combat.
    PruneRalliedEnemies(game_process);
    // V156 - la touche de switch du jeu continue sur les allies rallies.
    UpdateSwitchCycle(game_process, radar_snapshot, settings);
    // V164 - le feu continu : on ne renvoie que ceux dont la cible est tombee.
    UpdateFireAll(game_process, settings);
    // V146 - au plus un petit paquet de sorties d'arme par image. Voir
    // `QueueEquipRequest` : distribuer a vingt-cinq hommes d'un coup, deux
    // fois dans la meme image, a arrete le jeu.
    ProcessPendingEquip(game_process);
    // Une seule recherche par processus de jeu; le resultat est journalise.
    (void)FindCreateActor(game_process);

    ObserveInventoryState(game_process, radar_snapshot);
    std::uintptr_t controlled_vehicle = 0;
    const bool vehicle_context = ResolveControlledVehicle(
        game_process, radar_snapshot, controlled_vehicle);
    static int logged_noclip_hotkey = -1;
    const int noclip_hotkey = settings.noclip_hotkey_enabled ? 1 : 0;
    if (noclip_hotkey != logged_noclip_hotkey)
    {
        logged_noclip_hotkey = noclip_hotkey;
        LogDiagnostic(
            "Noclip: V hotkey enabled=%d (when 0, V is left to the game and "
            "cannot toggle noclip).",
            noclip_hotkey);
    }
    if (input.toggle_noclip_pressed)
    {
        settings.noclip_active = !settings.noclip_active;
        LogDiagnostic(
            "Noclip: V toggle requested, active=%d.",
            settings.noclip_active ? 1 : 0);
    }
    UpdateSpeedMultipliers(settings, input, vehicle_context);
    settings.aimbot_max_distance_m = (std::clamp)(
        settings.aimbot_max_distance_m,
        kMinimumAimbotDistance,
        kMaximumAimbotDistance);
    settings.bullet_track_radius_pixels = (std::clamp)(
        settings.bullet_track_radius_pixels,
        kMinimumBulletTrackRadius,
        kMaximumBulletTrackRadius);

    UpdateNoclip(
        game_process, radar_snapshot, settings, input,
        vehicle_context, controlled_vehicle, status);

    UpdateNetworkPositionMask(
        game_process, radar_snapshot, settings, input, status);
    UpdatePeerRemoteLifeMirror(game_process, radar_snapshot, settings);

    if (settings.noclip_active)
    {
        RestorePlayerSpeed(game_process);
        status.player_speed = PlayerSpeedStatus::Disabled;
    }
    else
    {
        UpdatePlayerSpeed(
            game_process, radar_snapshot, settings, vehicle_context, status);
    }

    ApplyPendingTeleport(
        game_process, radar_snapshot, controlled_vehicle, settings, status);

    UpdateTeleportVerification(game_process);

    UpdateEnemyInvisibility(
        game_process, radar_snapshot, settings, status);

    UpdateAbsolutePlayerProtection(
        game_process, radar_snapshot, settings, status);
    UpdateExtendedHealth(game_process, radar_snapshot, settings, status);
    UpdateVehicleExit(game_process, radar_snapshot, settings, status);
    UpdateVehicleRepair(
        game_process, radar_snapshot, controlled_vehicle, settings, status);
    UpdateVehicleMenu(game_process, radar_snapshot, settings, status);
    UpdateSoldierMenu(game_process, radar_snapshot, settings, status);
    UpdateWeaponMirror(game_process, radar_snapshot, settings);
    UpdateWeaponMenu(game_process, radar_snapshot, settings);
    ObserveReviveTargetHistory(game_process, radar_snapshot);
    PollNativePlayerRevive(game_process, status);
    TryReviveCurrentPlayer(
        game_process, radar_snapshot, settings, status);
    TryRepairCurrentPlayer(
        game_process, radar_snapshot, settings, status);

    UpdateAimbot(
        game_process, radar_snapshot, settings, status,
        ActiveRapidFireActor(game_process));

    UpdateVehicleSpeed(
        game_process, controlled_vehicle, settings, status);

    UpdateGameSpeed(game_process, settings, status);

    UpdateVehicleInvulnerability(
        game_process, controlled_vehicle, settings, status);

    bool native_map_open = false;
    const bool native_map_state_valid = ReadNativeMapOpen(
        game_process, radar_snapshot, native_map_open);

    // =================================================================
    // V145 - LA CARTE SERT AUX ORDRES, MEME SANS LA CASE DE TELEPORT
    // =================================================================
    //
    // Le joueur dit : « je ne peux pas l'envoyer sur carte dans une position,
    // je peux seulement leur demander de venir ». Son journal donne la raison
    // en une ligne :
    //
    //   [TEST TELEPORT #1] LBUTTON_EDGE enabled=0 map_open_cached=0 ...
    //
    // TOUTE la machinerie de la carte - l'ouverture par K, l'armement, la
    // lecture du clic - etait suspendue a `teleport_map_enabled`, la case
    // « teleportation par la carte ». Un joueur qui ne veut pas se teleporter
    // n'a aucune raison de la cocher, et il perdait du meme coup le seul
    // moyen d'envoyer ses hommes quelque part.
    //
    // Or les deux choses n'ont rien a voir. `ApplyPendingTeleport` traite deja
    // l'ordre en priorite sur la teleportation :
    //
    //   if (!g_soldier_order_targets.empty()) { ... ordre ... return; }
    //
    // Des qu'une troupe est armee pour un ordre, la carte doit donc repondre,
    // que la case soit cochee ou non. C'est ce que fait `map_serves_orders`.
    const bool map_serves_orders = !g_soldier_order_targets.empty();
    const bool map_usable =
        settings.teleport_map_enabled || map_serves_orders;
    // Une seule ligne par armement, pour que le journal dise sans ambiguite si
    // la carte a bien ete rendue disponible sans la case de teleportation.
    static bool last_map_serves_orders = false;
    if (map_serves_orders != last_map_serves_orders)
    {
        last_map_serves_orders = map_serves_orders;
        if (map_serves_orders)
        {
            LogDiagnostic(
                "Ordre par la carte: %u homme(s) armes. La carte repond "
                "maintenant a K et au clic MEME si la case de teleportation "
                "est decochee (elle vaut %d).",
                static_cast<unsigned>(g_soldier_order_targets.size()),
                settings.teleport_map_enabled ? 1 : 0);
        }
    }

    // Teleport ownership is deliberately independent from the game's native
    // map-active bit. Space may open the same map, but only a successful K
    // request arms a teleport session.
    struct TeleportMapArmState
    {
        DWORD process_id = 0;
        bool armed_by_k = false;
        bool waiting_for_k_open = false;
        ULONGLONG cursor_sync_until = 0;
        ULONGLONG next_cursor_sync = 0;
        std::uint32_t cursor_sync_count = 0;
    };
    static TeleportMapArmState map_arm{};
    const DWORD map_process = game_process.ProcessId();
    if (map_arm.process_id != map_process)
    {
        map_arm = {};
        map_arm.process_id = map_process;
    }
    if (!map_usable)
    {
        map_arm.armed_by_k = false;
        map_arm.waiting_for_k_open = false;
        map_arm.cursor_sync_until = 0;
    }

    if (map_usable && input.toggle_map_pressed)
    {
        LogDiagnostic(
            "[TEST TELEPORT] K_SESSION begin open_before=%d state_valid=%d "
            "armed_before=%d.",
            native_map_open ? 1 : 0,
            native_map_state_valid ? 1 : 0,
            map_arm.armed_by_k ? 1 : 0);
        const bool toggle_sent = SendNativeMapKey(game_process);
        if (native_map_open)
        {
            // K closes either kind of map. No teleport click is accepted from
            // this frame onward.
            map_arm.armed_by_k = false;
            map_arm.waiting_for_k_open = false;
            map_arm.cursor_sync_until = 0;
        }
        else if (toggle_sent)
        {
            map_arm.armed_by_k = true;
            map_arm.waiting_for_k_open = true;
            map_arm.cursor_sync_until = 0;
            map_arm.next_cursor_sync = 0;
            map_arm.cursor_sync_count = 0;
        }
        LogDiagnostic(
            "[TEST TELEPORT] K_SESSION send=%d armed_after=%d waiting_open=%d.",
            toggle_sent ? 1 : 0,
            map_arm.armed_by_k ? 1 : 0,
            map_arm.waiting_for_k_open ? 1 : 0);
        if (!toggle_sent)
            status.teleport = TeleportStatus::WriteFailed;
    }

    const ULONGLONG map_now = GetTickCount64();
    if (native_map_open && map_arm.waiting_for_k_open)
    {
        map_arm.waiting_for_k_open = false;
        map_arm.cursor_sync_until = map_now + 180U;
        map_arm.next_cursor_sync = 0;
        map_arm.cursor_sync_count = 0;
        LogDiagnostic(
            "[TEST TELEPORT] K_MAP_OPEN native-centre stabilization=180ms.");
    }
    else if (!native_map_open && map_arm.armed_by_k &&
             !map_arm.waiting_for_k_open)
    {
        map_arm.armed_by_k = false;
        map_arm.cursor_sync_until = 0;
    }

    settings.teleport_map_open =
        map_usable && native_map_state_valid && native_map_open;
    settings.teleport_map_armed = settings.teleport_map_open &&
        map_arm.armed_by_k;

    // DirectInput may apply one stale relative-mouse delta just after the map
    // appears. Reassert the source-defined centre over a short, bounded
    // window, then immediately return full cursor control to the player.
    if (settings.teleport_map_armed && radar_snapshot &&
        map_now <= map_arm.cursor_sync_until &&
        map_now >= map_arm.next_cursor_sync)
    {
        const bool synced = SyncNativeMapCursorToPlayer(
            game_process, *radar_snapshot);
        ++map_arm.cursor_sync_count;
        map_arm.next_cursor_sync = map_now + 35U;
        LogDiagnostic(
            "[TEST TELEPORT] PLAYER_CURSOR_SYNC attempt=%u result=%d.",
            map_arm.cursor_sync_count, synced ? 1 : 0);
    }

    // Log map state transitions only, so a full test session stays readable.
    static DWORD last_logged_map_process = 0;
    static std::uintptr_t last_logged_map_mission = 0;
    static int last_logged_map_open = -1;
    static int last_logged_map_valid = -1;
    const std::uintptr_t map_mission = radar_snapshot
        ? radar_snapshot->entity_list_object_address
        : 0;
    if (map_process != last_logged_map_process ||
        map_mission != last_logged_map_mission)
    {
        last_logged_map_process = map_process;
        last_logged_map_mission = map_mission;
        last_logged_map_open = -1;
        last_logged_map_valid = -1;
    }
    if (map_usable &&
        (last_logged_map_open != (native_map_open ? 1 : 0) ||
         last_logged_map_valid != (native_map_state_valid ? 1 : 0)))
    {
        last_logged_map_open = native_map_open ? 1 : 0;
        last_logged_map_valid = native_map_state_valid ? 1 : 0;
        LogDiagnostic(
            "[TEST TELEPORT] MAP_STATE open=%d state_valid=%d pid=%lu "
            "mission=%08X player=%08X armed_by_k=%d "
            "snapshot_world=(%.3f,%.3f,%.3f).",
            native_map_open ? 1 : 0,
            native_map_state_valid ? 1 : 0,
            static_cast<unsigned long>(map_process),
            static_cast<unsigned>(map_mission),
            static_cast<unsigned>(radar_snapshot
                ? radar_snapshot->player_object_address : 0),
            settings.teleport_map_armed ? 1 : 0,
            radar_snapshot ? radar_snapshot->player.position.x : 0.0f,
            radar_snapshot ? radar_snapshot->player.position.y : 0.0f,
            radar_snapshot ? radar_snapshot->player.position.z : 0.0f);
    }
    if (map_usable && settings.teleport_map_open &&
        settings.teleport_map_armed &&
        input.native_map_click_pressed && radar_snapshot)
    {
        LogDiagnostic(
            "Teleport: native map click at client (%.0f, %.0f).",
            input.native_map_client_x, input.native_map_client_y);
        Vector3 selected_ground{};
        const TeleportMapResult result = ResolveNativeMapClick(
            game_process,
            *radar_snapshot,
            input.native_map_client_x,
            input.native_map_client_y,
            selected_ground);
        LogDiagnostic(
            "[TEST TELEPORT] CLICK_RESOLVE result=%u click=(%.3f,%.3f) "
            "selected=(%.3f,%.3f,%.3f) player=(%.3f,%.3f,%.3f).",
            static_cast<unsigned>(result),
            input.native_map_client_x, input.native_map_client_y,
            selected_ground.x, selected_ground.y, selected_ground.z,
            radar_snapshot->player.position.x,
            radar_snapshot->player.position.y,
            radar_snapshot->player.position.z);
        if (result == TeleportMapResult::GroundSelected)
        {
            map_arm.armed_by_k = false;
            map_arm.cursor_sync_until = 0;
            settings.teleport_map_armed = false;
            settings.teleport_ground = selected_ground;
            settings.teleport_pending = true;
            g_pending_teleport = {};
            const bool close_sent = SendNativeMapKey(game_process);
            LogDiagnostic(
                "[TEST TELEPORT] MAP_CLOSE_SEND result=%d pending_before=%d.",
                close_sent ? 1 : 0,
                settings.teleport_pending ? 1 : 0);
            if (!close_sent)
            {
                settings.teleport_pending = false;
                status.teleport = TeleportStatus::WriteFailed;
            }
        }
        else if (result == TeleportMapResult::NoWalkableGround)
        {
            status.teleport = TeleportStatus::NoWalkableGround;
        }
        else if (result == TeleportMapResult::MapNotActive)
        {
            status.teleport = TeleportStatus::NativeMapUnavailable;
        }
        else if (result == TeleportMapResult::CursorBlockNotFound)
        {
            status.teleport = TeleportStatus::MapCursorUnavailable;
        }
        else if (result == TeleportMapResult::RayMissed)
        {
            status.teleport = TeleportStatus::MapRayMissed;
        }
        else if (result == TeleportMapResult::Unavailable)
        {
            status.teleport = TeleportStatus::WaitingForMission;
        }
    }

    if (input.grant_all_items_pressed)
    {
        status.granted_item_count = 0;
        if (!game_process.IsConnected())
        {
            status.grant_all_items = GrantAllItemsStatus::NotConnected;
        }
        else if (!radar_snapshot)
        {
            status.grant_all_items =
                GrantAllItemsStatus::InventoryUnavailable;
        }
        else
        {
            status.grant_all_items = GrantCompleteDeluxeInventory(
                game_process,
                radar_snapshot,
                status.granted_item_count)
                ? GrantAllItemsStatus::Success
                : GrantAllItemsStatus::Unsupported;
        }
    }
}

// V97 : joue `actor->cbProc(message, prm1, 0, 0)` une seule fois, sur le
// thread du jeu, pour le soldat pilote. C'est le mecanisme deja eprouve par
// l'ancien F3; il sert maintenant a la sortie forcee de vehicule
// (CB_USE_AUTO avec prm2 nul = « quitter le vehicule »).
bool ApplyNativeActorCallback(
    TrainerProcess& process,
    const RadarSnapshot* snapshot,
    std::uint32_t message,
    std::uint32_t prm1)
{
    if (!process.IsConnected() || !snapshot ||
        !IsSanePointer(snapshot->player_object_address))
    {
        LogDiagnostic("Rappel natif: joueur controle indisponible.");
        return false;
    }

    const std::uintptr_t player = snapshot->player_object_address;
    // V117 - meme garde que pour Fullhands : ces rappels rejouent des chemins
    // du moteur ecrits pour un soldat de mission. Sur un soldat cree, faute
    // d'initialisation, ils lisent des donnees qui n'existent pas et arretent
    // le jeu. On refuse, et on le dit.
    if (IsCreatedSoldier(player))
    {
        LogDiagnostic(
            "Rappel natif %u: REFUSE, vous pilotez un soldat cree (%08X). "
            "Revenez a un soldat d'origine (fenetre G, ligne REVENIR).",
            static_cast<unsigned>(message), static_cast<unsigned>(player));
        return false;
    }
    std::uint32_t type = 0;
    if (!process.ReadMemory(player + kActorTypeOffset, type) ||
        type != kActorTypePlayer)
    {
        LogDiagnostic(
            "Life Unlimited: controlled actor is not C_player "
            "(player=%08X type=%u).",
            static_cast<unsigned>(player), static_cast<unsigned>(type));
        return false;
    }

    // Absolute protection owns this verified Tick point continuously in order
    // to neutralise only fatal landings. Reuse its one-shot slot for F3 rather
    // than replacing a live detour for one frame.
    if (g_protected_fall.applied &&
        g_protected_fall.process_id == process.ProcessId())
    {
        return QueueActorCallbackThroughProtectedFallHook(
            process, player, message, prm1);
    }

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        module.image_size <= kLifeUnlimitedTickHookRva +
            kLifeUnlimitedTickHookPatchSize)
    {
        return false;
    }

    const std::uintptr_t hook =
        module.base_address + kLifeUnlimitedTickHookRva;
    constexpr std::array<std::uint8_t, kLifeUnlimitedTickHookPatchSize>
        expected{0x8B, 0x83, 0xCC, 0x00, 0x00, 0x00};
    std::array<std::uint8_t, kLifeUnlimitedTickHookPatchSize> original{};
    if (!process.ReadMemory(hook, original.data(), original.size()) ||
        original != expected)
    {
        LogDiagnostic(
            "Life Unlimited: C_human::Tick signature mismatch at %08X.",
            static_cast<unsigned>(hook));
        return false;
    }

    const std::uintptr_t remote =
        process.AllocateRemoteMemory(kLifeUnlimitedRemoteSize);
    if (!IsSanePointer(remote))
    {
        LogDiagnostic("Life Unlimited: game-thread page allocation failed.");
        return false;
    }
    const std::uintptr_t target = remote + kLifeUnlimitedTargetOffset;
    const std::uintptr_t completion = remote + kLifeUnlimitedCompletionOffset;
    std::vector<std::uint8_t> code;
    code.reserve(96);
    const auto byte = [&](std::uint8_t value) { code.push_back(value); };
    const auto dword = [&](std::uint32_t value)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(value));
        std::memcpy(code.data() + offset, &value, sizeof(value));
    };

    byte(0x9C);                              // pushfd
    byte(0x60);                              // pushad
    byte(0x3B); byte(0x1D);                  // cmp ebx,[target]
    dword(static_cast<std::uint32_t>(target));
    byte(0x75);                              // jne done
    const std::size_t other_actor_displacement = code.size();
    byte(0x00);
    // Disarm before invoking the callback. C_human::Tick may re-enter during
    // a network frame; a second callback would toggle immortality back off.
    byte(0xC7); byte(0x05);                  // mov [target],0
    dword(static_cast<std::uint32_t>(target));
    dword(0);
    // This is exactly C_game_mission::BroadcastMessage(CB_CHEAT, 14, 0) for
    // the controlled C_player only. The installed callback ABI has four
    // stack values and returns with ret 0x10.
    byte(0x6A); byte(0x00);                  // reserved
    byte(0x6A); byte(0x00);                  // prm2
    byte(0x6A); byte(static_cast<std::uint8_t>(prm1));
    byte(0x6A); byte(static_cast<std::uint8_t>(message));
    byte(0x8B); byte(0xCB);                  // mov ecx, ebx
    byte(0x8B); byte(0x01);                  // mov eax,[ecx]
    byte(0xFF); byte(0x50); byte(0x04);      // call cbProc
    byte(0xC7); byte(0x05);                  // completion = 1
    dword(static_cast<std::uint32_t>(completion));
    dword(1);
    const std::size_t done = code.size();
    code[other_actor_displacement] = static_cast<std::uint8_t>(
        done - (other_actor_displacement + 1));
    byte(0x61);                              // popad
    byte(0x9D);                              // popfd
    code.insert(code.end(), original.begin(), original.end());
    byte(0xE9);
    const std::int32_t return_relative = static_cast<std::int32_t>(
        (hook + original.size()) -
        (remote + code.size() + sizeof(std::int32_t)));
    dword(static_cast<std::uint32_t>(return_relative));

    const std::uint32_t pending = 0;
    if (code.size() > kLifeUnlimitedTargetOffset ||
        !process.WriteMemory(remote, code.data(), code.size()) ||
        !process.WriteMemory(target, static_cast<std::uint32_t>(player)) ||
        !process.WriteMemory(completion, pending))
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }

    std::array<std::uint8_t, kLifeUnlimitedTickHookPatchSize> patch{};
    patch.fill(0x90);
    patch[0] = 0xE9;
    const std::int32_t hook_relative = static_cast<std::int32_t>(
        remote - (hook + sizeof(std::int32_t) + 1));
    std::memcpy(patch.data() + 1, &hook_relative, sizeof(hook_relative));
    if (!InstallHookSafely(
            process, hook, patch.data(), patch.size(),
            "le saut vers le code pose"))
    {
        (void)process.FreeRemoteMemory(remote);
        return false;
    }

    std::uint32_t completed = 0;
    for (unsigned attempt = 0; attempt < 500 && completed == 0; ++attempt)
    {
        (void)process.ReadMemory(completion, completed);
        if (completed == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // Stop matching first, then restore the exact bytes. Keeping the page is
    // safer than freeing it if restoration or the execution-range check fail.
    const std::uint32_t none = 0;
    (void)process.WriteMemory(target, none);
    std::array<std::uint8_t, kLifeUnlimitedTickHookPatchSize> current{};
    const bool restored = process.ReadMemory(
            hook, current.data(), current.size()) &&
        (current == original ||
         (current == patch && RestoreHookSafely(
        process, hook, original.data(), original.size())));
    bool idle = false;
    if (restored)
    {
        for (unsigned attempt = 0; attempt < 250 && !idle; ++attempt)
        {
            bool executing = true;
            idle = process.IsAnyThreadExecutingRange(
                remote, kLifeUnlimitedRemoteSize, executing) && !executing;
            if (!idle)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    const bool released = restored && idle && process.FreeRemoteMemory(remote);
    LogDiagnostic(
        "Life Unlimited: native callback completed=%u restored=%d idle=%d "
        "freed=%d player=%08X.",
        completed, restored ? 1 : 0, idle ? 1 : 0, released ? 1 : 0,
        static_cast<unsigned>(player));
    return completed == 1U && restored && idle && released;
}

void RestoreGameplayModifiers(TrainerProcess& game_process)
{
    (void)RestorePlayerReviveHook(game_process);
    StopNoclipMovement(game_process);
    RemoveNoclipHook(game_process);
    StopNetworkPositionMask(game_process);
    RemoveNetworkPositionMaskHook(game_process);
    ClosePeerVisualCommandSender();
    g_pending_teleport = {};
    g_teleport_verify = {};
    RestorePlayerSpeed(game_process);
    RestoreEnemyInvisibility(game_process);
    RestorePlayerDeathHook(game_process);
    RestorePlayerBaseDeathHook(game_process);
    RestorePlayerExplosionHook(game_process);
    RestoreProtectedFallHook(game_process);
    RestoreNativeNoHitFlags(game_process);
    RestoreExtendedHealth(game_process);
    // Fermeture du trainer : la file peut encore contenir des pages qu'un
    // thread du jeu occupait a la seconde d'avant. Ici, et seulement ici, on
    // insiste brievement pour ne rien laisser alloue dans hde.exe. 100 ms au
    // maximum, invisibles a la fermeture.
    for (int attempt = 0; attempt < 20; ++attempt)
    {
        ProcessPendingRemoteReleases(game_process);
        if (!game_process.IsConnected())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (g_player_damage.applied && g_player_damage.protected_player != 0)
        RestorePlayerDamageHook(game_process);
    RestoreAimbot(game_process);
    DrainAndRestoreBulletTrackHook(game_process, true);
    RestoreVehicleSpeed(game_process);
    RestoreVehicleInvulnerability(game_process);
    RestoreGameSpeed(game_process);
}

bool ApplyNativeLifeUnlimited(
    TrainerProcess& process,
    const RadarSnapshot* snapshot)
{
    return ApplyNativeActorCallback(
        process, snapshot, kPlayerCheatCallback, kLifeUnlimitedCheat);
}

const char* PlayerSpeedStatusText(const GameplayStatus& status)
{
    switch (status.player_speed)
    {
    case PlayerSpeedStatus::Active:
        return "[OK] Multiplicateur appliqué au déplacement du joueur local.";
    case PlayerSpeedStatus::SuspendedInVehicle:
        return "Suspendu pendant la conduite : N accélère, B réduit.";
    case PlayerSpeedStatus::WaitingForMission:
        return "En attente du joueur local dans une mission.";
    case PlayerSpeedStatus::UnsupportedLayout:
        return "[ERREUR] Déplacement joueur non validé dans cette mission.";
    case PlayerSpeedStatus::WriteFailed:
        return "[ERREUR] Écriture Super Run refusée.";
    case PlayerSpeedStatus::Disabled:
    default:
        return "Désactivé : vitesse normale 1.0x.";
    }
}

const char* NoclipStatusText(const GameplayStatus& status)
{
    switch (status.noclip)
    {
    case NoclipStatus::Active:
        return "[OK] Noclip actif : votre position de vol est aussi publiée au réseau.";
    case NoclipStatus::NetworkPublishUnavailable:
        return "[ATTENTION] Noclip local actif, mais publication réseau indisponible.";
    case NoclipStatus::Armed:
        return "[READY] Noclip armed: press V to start flying.";
    case NoclipStatus::WaitingForMission:
        return "En attente du joueur et de la caméra dans une mission.";
    case NoclipStatus::SuspendedInVehicle:
        return "Véhicule non reconnu : reprenez le volant ou sortez du "
               "véhicule.";
    case NoclipStatus::InputCaptureUnavailable:
        return "[ERREUR] Filtre clavier sélectif indisponible ; activation annulée.";
    case NoclipStatus::UnsupportedLayout:
        return "[ERREUR] Frame joueur ou direction caméra non validé.";
    case NoclipStatus::WriteFailed:
        return "[ERREUR] Mise à jour Noclip refusée et désactivée.";
    case NoclipStatus::Disabled:
    default:
        return "Désactivé : V active le déplacement libre.";
    }
}

const char* NetworkPositionMaskStatusText(const GameplayStatus& status)
{
    switch (status.network_position_mask)
    {
    case NetworkPositionMaskStatus::Armed:
        return "[PRET] W fige la position puis cache le modele sur le PC client.";
    case NetworkPositionMaskStatus::Active:
        return "[OK] Position reseau figee. W masque le modele sur le PC client.";
    case NetworkPositionMaskStatus::RemoteVisualHidden:
        return "[OK] Position figee, modele cache sur le PC client et invisible pour les ennemis (vous seul). W vous remontre a la position reelle.";
    case NetworkPositionMaskStatus::RealPosition:
        return "[OK] Position reelle publiee et modele visible sur le PC client. W fige une nouvelle position.";
    case NetworkPositionMaskStatus::WaitingForMission:
        return "En attente du joueur local dans une mission.";
    case NetworkPositionMaskStatus::UnsupportedRevision:
        return "[ERREUR] Message de position non reconnu dans cette version du jeu.";
    case NetworkPositionMaskStatus::WriteFailed:
        return "[ERREUR] La position masquee n'a pas pu etre envoyee au hook.";
    case NetworkPositionMaskStatus::Disabled:
    default:
        return "Desactive : l'autre PC recoit votre position reelle.";
    }
}

const char* TeleportStatusText(const GameplayStatus& status)
{
    switch (status.teleport)
    {
    case TeleportStatus::WaitingForMapClose:
        return "Fermeture de la carte en cours avant la téléportation.";
    case TeleportStatus::Success:
        return "[OK] Joueur téléporté sur le sol sélectionné.";
    case TeleportStatus::WaitingForMission:
        return "[ERREUR] Joueur local indisponible dans la mission.";
    case TeleportStatus::NoWalkableGround:
        return "[ERREUR] Aucun sol praticable à cet endroit.";
    case TeleportStatus::UnsupportedLayout:
        return "[ERREUR] Position locale du joueur non validée.";
    case TeleportStatus::NativeMapUnavailable:
        return "[ERREUR] Carte native non détectée (K non reconnu par le jeu ?).";
    case TeleportStatus::MapCursorUnavailable:
        return "[ERREUR] Curseur natif de la carte introuvable en mémoire.";
    case TeleportStatus::MapRayMissed:
        return "[ERREUR] Le rayon du clic n'a touché aucune géométrie de carte.";
    case TeleportStatus::WriteFailed:
        return "[ERREUR] Téléportation refusée et annulée.";
    case TeleportStatus::Ready:
    default:
        return "Prêt : K ouvre la carte native du jeu; cliquez la destination.";
    }
}

const char* EnemyInvisibilityStatusText(const GameplayStatus& status)
{
    switch (status.enemy_invisibility)
    {
    case EnemyInvisibilityStatus::Active:
        return "[OK] Cible absente pour l'IA : perception, poursuite, attaque et voix neutralisées.";
    case EnemyInvisibilityStatus::ActiveNetworkHost:
        return "[OK] Partie réseau hébergée ici : l'IA est calculée sur ce PC. Toute l'escouade et tous les joueurs connectés sont couverts.";
    case EnemyInvisibilityStatus::HostAuthorityRequired:
        return "[ATTENTION] Partie rejointe : l'IA tourne sur le PC de l'hébergeur. Les hooks locaux sont posés mais ne peuvent pas aveugler cette IA — l'hébergeur doit lancer le trainer.";
    case EnemyInvisibilityStatus::ActiveNetworkUnknownRole:
        return "[OK] Partie réseau détectée : escouade et joueurs connectés protégés ici. Si vous avez REJOINT, l'hébergeur doit aussi lancer le trainer — sinon son IA continue de vous voir.";
    case EnemyInvisibilityStatus::WaitingForMission:
        return "En attente du joueur local dans une mission.";
    case EnemyInvisibilityStatus::HeldThroughMissionGap:
        return "[OK] Filtre maintenu : le jeu ne répond pas pendant une seconde (fréquent en réseau), les hooks restent posés.";
    case EnemyInvisibilityStatus::UnsupportedRevision:
        return "[ERREUR] Fonction de perception non reconnue.";
    case EnemyInvisibilityStatus::WriteFailed:
        return "[ERREUR] Patch de perception refusé.";
    case EnemyInvisibilityStatus::Disabled:
    default:
        return "Désactivé : perception ennemie normale.";
    }
}

const char* AbsolutePlayerProtectionStatusText(const GameplayStatus& status)
{
    switch (status.absolute_player_protection)
    {
    case AbsolutePlayerProtectionStatus::Active:
        return "[OK] Protection active selon la portée : tirs, morts réseau, explosions et chutes bloqués.";
    case AbsolutePlayerProtectionStatus::SharedDamageGuard:
        return "[OK] Joueur actuel protégé : garde de dégâts partagé avec invisibilité.";
    case AbsolutePlayerProtectionStatus::WaitingForPlayer:
        return "En attente du joueur actuel dans la mission.";
    case AbsolutePlayerProtectionStatus::UnsupportedRevision:
        return "[ERREUR] Chemin de dégâts ou de mort non reconnu.";
    case AbsolutePlayerProtectionStatus::WriteFailed:
        return "[ERREUR] Protection totale non appliquée.";
    case AbsolutePlayerProtectionStatus::Disabled:
    default:
        return "Désactivé : le joueur actuel peut subir des dégâts et mourir en réseau.";
    }
}

const char* ExtendedHealthStatusText(const GameplayStatus& status)
{
    switch (status.extended_health)
    {
    case ExtendedHealthStatus::Active:
        return "[OK] Sante etendue a 20000 sur vos soldats. Elle descend normalement.";
    case ExtendedHealthStatus::WaitingForPlayer:
        return "En attente du joueur local dans une mission.";
    case ExtendedHealthStatus::UnsupportedLayout:
        return "[ERREUR] Champs de vie non reconnus; aucune ecriture effectuee.";
    case ExtendedHealthStatus::WriteFailed:
        return "[ERREUR] Ecriture de la vie refusee.";
    case ExtendedHealthStatus::Disabled:
    default:
        return "Desactive : vie normale.";
    }
}

const char* VehicleExitStatusText(const GameplayStatus& status)
{
    switch (status.vehicle_exit)
    {
    case VehicleActionStatus::Success:
        return "sortie effectuee.";
    case VehicleActionStatus::NoVehicle:
        return "vous n'etes dans aucun vehicule.";
    case VehicleActionStatus::WaitingForPlayer:
        return "joueur indisponible.";
    case VehicleActionStatus::Failed:
        return "sortie refusee par le jeu.";
    case VehicleActionStatus::Ready:
    default:
        return "pret a vous faire sortir, meme en vol.";
    }
}

const char* VehicleRepairStatusText(const GameplayStatus& status)
{
    switch (status.vehicle_repair)
    {
    case VehicleActionStatus::Success:
        return "vehicule remis en etat.";
    case VehicleActionStatus::NoVehicle:
        return "G ne s'applique qu'au volant : montez dans un vehicule.";
    case VehicleActionStatus::WaitingForPlayer:
        return "joueur indisponible.";
    case VehicleActionStatus::Failed:
        return "remise en etat refusee.";
    case VehicleActionStatus::BodyNotRepairable:
        return "etat mecanique remis; carrosserie inchangee.";
    case VehicleActionStatus::Ready:
    default:
        return "G repare le vehicule ou vous etes; F6 amene un vehicule de la mission.";
    }
}

std::size_t SoldierMenuEntryCount()
{
    return g_soldier_menu_labels.size();
}

const wchar_t* SoldierMenuEntry(std::size_t index)
{
    return index < g_soldier_menu_labels.size()
        ? g_soldier_menu_labels[index].c_str()
        : nullptr;
}

int SoldierMenuSelection()
{
    return g_soldier_menu_selection;
}

std::size_t VehicleMenuEntryCount()
{
    return g_vehicle_menu_labels.size();
}

const wchar_t* VehicleMenuEntry(std::size_t index)
{
    return index < g_vehicle_menu_labels.size()
        ? g_vehicle_menu_labels[index].c_str()
        : nullptr;
}

int VehicleMenuSelection()
{
    return g_vehicle_menu_selection;
}

const char* ReviveCurrentPlayerStatusText(const GameplayStatus& status)
{
    switch (status.revive_current_player)
    {
    case ReviveCurrentPlayerStatus::Queued:
        return "Réanimation complète en cours...";
    case ReviveCurrentPlayerStatus::Revived:
        return "joueur actuel remis en état vivant.";
    case ReviveCurrentPlayerStatus::PlayerAlive:
        return "joueur actuel déjà vivant.";
    case ReviveCurrentPlayerStatus::WaitingForPlayer:
        return "joueur actuel indisponible.";
    case ReviveCurrentPlayerStatus::WriteFailed:
        return "réanimation refusée.";
    case ReviveCurrentPlayerStatus::Ready:
    default:
        return "prêt à ranimer le joueur actuel.";
    }
}

const char* AimbotStatusText(const GameplayStatus& status)
{
    switch (status.aimbot)
    {
    case AimbotStatus::Active:
        return "[OK] Caméra native verrouillée sur le visage vert le plus proche.";
    case AimbotStatus::WaitingForMission:
        return "En attente du joueur et de la caméra.";
    case AimbotStatus::NoTarget:
        return "Aucune tête verte dans la portée et l'écran.";
    case AimbotStatus::UnsupportedLayout:
        return "[ERREUR] Projection du visage non validée.";
    case AimbotStatus::WriteFailed:
        return "[ERREUR] Verrou caméra interne Aimbot refusé.";
    case AimbotStatus::Disabled:
    default:
        return "Désactivé : visée manuelle normale.";
    }
}

const char* BulletTrackStatusText(const GameplayStatus& status)
{
    switch (status.bullet_track)
    {
    case BulletTrackStatus::Active:
        return "[OK] Projectile redirigé vers la tête verte suivie en direct.";
    case BulletTrackStatus::WaitingForMission:
        return "En attente du joueur et de la caméra.";
    case BulletTrackStatus::NoTargetInCircle:
        return "Aucune tête verte exploitable à l'écran.";
    case BulletTrackStatus::UnsupportedLayout:
        return "[ERREUR] Orientation Bullet Track non validée.";
    case BulletTrackStatus::WriteFailed:
        return "[ERREUR] Hook projectile Bullet Track refusé.";
    case BulletTrackStatus::Disabled:
    default:
        return "Désactivé : trajectoire normale.";
    }
}

const char* VehicleInvulnerabilityStatusText(const GameplayStatus& status)
{
    switch (status.vehicle_invulnerability)
    {
    case VehicleInvulnerabilityStatus::WaitingForVehicle:
        return "En attente d'un véhicule conduit.";
    case VehicleInvulnerabilityStatus::Active:
        return "[OK] Véhicule indestructible : tirs, chutes et collisions "
               "sont ignorés.";
    case VehicleInvulnerabilityStatus::UnsupportedLayout:
        return "[ERREUR] Disposition du véhicule non reconnue.";
    case VehicleInvulnerabilityStatus::WriteFailed:
        return "[ERREUR] Écriture de la résistance impossible.";
    case VehicleInvulnerabilityStatus::Disabled:
    default:
        return "Désactivé : le véhicule encaisse les dégâts normalement.";
    }
}

const char* VehicleSpeedStatusText(const GameplayStatus& status)
{
    switch (status.vehicle_speed)
    {
    case VehicleSpeedStatus::Active:
        return "[OK] Boost prêt : maintenir Z accélère dès le premier tick moteur.";
    case VehicleSpeedStatus::WaitingForVehicle:
        return "En attente du siège conducteur d'un véhicule.";
    case VehicleSpeedStatus::UnsupportedLayout:
        return "[ERREUR] Vitesse véhicule non validée.";
    case VehicleSpeedStatus::WriteFailed:
        return "[ERREUR] Écriture vitesse véhicule refusée.";
    case VehicleSpeedStatus::Disabled:
    default:
        return "Désactivé : accélération et boîte du véhicule normales.";
    }
}

const char* GameSpeedStatusText(const GameplayStatus& status)
{
    switch (status.game_speed)
    {
    case GameSpeedStatus::Active:
        return "[OK] Temps du jeu accéléré : [9] augmente, [8] réduit.";
    case GameSpeedStatus::UnsupportedLayout:
        return "[ERREUR] Boucle de temps du jeu non reconnue.";
    case GameSpeedStatus::WriteFailed:
        return "[ERREUR] Écriture de la vitesse du jeu refusée.";
    case GameSpeedStatus::Disabled:
    default:
        return "Désactivé : le jeu tourne à sa vitesse normale.";
    }
}

// V142 - le radar demande si cet ennemi est passe de notre cote.
bool IsActorRalliedToPlayer(std::uintptr_t actor)
{
    return IsRalliedEnemy(actor);
}

const char* GrantAllItemsStatusText(const GameplayStatus& status)
{
    switch (status.grant_all_items)
    {
    case GrantAllItemsStatus::Success:
        return "[OK] Série remplacée entièrement; M est prêt pour la suivante.";
    case GrantAllItemsStatus::NotConnected:
        return "[ERREUR] hde.exe n'est pas connecté.";
    case GrantAllItemsStatus::InventoryUnavailable:
        return "[ERREUR] Inventaire local indisponible.";
    case GrantAllItemsStatus::Unsupported:
        return "[ERREUR] L'inventaire tactique n'a pas été confirmé.";
    case GrantAllItemsStatus::Ready:
    default:
        return "Prêt : M remplace le lot actuel par la série suivante "
               "(1/14 à 14/14).";
    }
}
}
