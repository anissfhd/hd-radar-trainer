#pragma once

#include "radar.h"

namespace hd
{
enum class EnemyInvisibilityScope
{
    ControlledPlayer,
    WholeSquad
};

enum class NetworkPositionMaskScope
{
    ControlledPlayer,
    WholeSquad
};

enum class AbsolutePlayerProtectionScope
{
    ControlledPlayer,
    WholeSquad
};

struct GameplaySettings
{
    bool noclip_enabled = false;
    // The checkbox arms V; flight itself starts and stops with V.
    bool noclip_active = false;
    // The V hotkey is opt-in: it used to toggle noclip whether or not the
    // feature was enabled, and it was swallowed from the game either way.
    bool noclip_hotkey_enabled = false;
    // Fullhands is one control now. Each press of M replaces the complete
    // previous lot and advances through seven fixed catalogue series. M is
    // swallowed from the game, so it never opens the inventory at the same
    // time as the replacement.
    float noclip_speed_mps = 12.0f;

    // Keeps the position published to the other network peer at the point
    // where this switch was enabled. Local movement, collision and camera
    // remain native. It is intentionally separate from noclip.
    bool network_position_mask_enabled = false;
    NetworkPositionMaskScope network_position_mask_scope =
        NetworkPositionMaskScope::ControlledPlayer;
    // Internal one-shot latch for the optional W safety action. It resets only
    // when the network-position checkbox is explicitly turned off.
    bool network_position_mask_first_w_consumed = false;
    // Checking the box only arms the feature. W sets this request, which is
    // retained until a valid local player can be used to capture the anchor.
    bool network_position_mask_activation_requested = false;

    bool player_speed_enabled = false;
    float player_speed_multiplier = 1.0f;

    bool teleport_map_enabled = false;
    bool teleport_map_open = false;
    // Armed only when K opened the current native-map session. The normal
    // in-game map opened with Space remains read-only.
    bool teleport_map_armed = false;
    bool teleport_pending = false;
    Vector3 teleport_ground{};

    bool enemy_invisibility_enabled = false;
    EnemyInvisibilityScope enemy_invisibility_scope =
        EnemyInvisibilityScope::ControlledPlayer;

    // Protection locale stricte : elle ne vise que le soldat que le radar
    // identifie comme actuellement contrôlé. Elle est indépendante du filtre
    // de perception des ennemis.
    bool absolute_player_protection_enabled = false;
    // Set only by the Protection reseau totale card itself. It lets gameplay
    // distinguish a deliberate disarm click from a transient/stale UI value
    // while a network mission is still running.
    bool absolute_player_protection_toggle_requested = false;
    AbsolutePlayerProtectionScope absolute_player_protection_scope =
        AbsolutePlayerProtectionScope::ControlledPlayer;
    // Does not prevent local damage or death. It asks the already-installed
    // Client companion to ignore the host player's death transition, so F12
    // can perform a native local revive without showing a remote skeleton.
    bool remote_life_mirror_enabled = false;
    bool revive_current_player_requested = false;
    bool repair_current_player_requested = false;

    bool aimbot_enabled = false;
    float aimbot_max_distance_m = 180.0f;
    // V97 : la case « Illimité » neutralise entierement le curseur. Le
    // selecteur de cible sait deja traiter « aucune limite » - c'est ce que
    // fait Bullet Track en passant 0 - donc rien de nouveau n'est invente ici.
    bool aimbot_unlimited_distance = false;

    bool bullet_track_enabled = false;
    // Bullets through walls. The native resolver receives the selected live
    // head and owns the recipient reference. Finish substitutes that current
    // head only in its stack-local damage callback; projectile.hit_frm and all
    // engine-owned smart pointers remain untouched.
    bool bullets_through_walls = false;
    // Optional V2 path for an occluded Bullet Track target: once the native
    // resolver has validated it, end the projectile on its following tick
    // instead of preserving the normal first-flight delay.
    bool bullet_track_instant_wall_impact_v2 = false;
    float bullet_track_radius_pixels = 180.0f;

    bool vehicle_speed_enabled = false;
    bool vehicle_invulnerable_enabled = false;
    // Multiplies the engine's own 0.6 steering rate for the driven car only.
    float vehicle_steering_boost = 2.0f;
    float vehicle_speed_multiplier = 1.0f;

    // Vitesse globale du jeu. Le trainer multiplie le pas de temps que la
    // boucle principale distribue a toute la simulation, ce qui fait defiler
    // d'autant plus vite les attentes scriptees d'une mission.
    bool game_speed_enabled = false;
    float game_speed_multiplier = 1.0f;

    bool grant_all_items_requested = false;
    bool grant_all_items_consumed = false;

    // V97 - Sante Max (F4). Interrupteur natif, en remplacement du cheat
    // `ironman` tape au clavier : le moteur l'ignore purement et simplement en
    // partie reseau (`case 8: if(!net)` dans C_player::cbProc). Actif, il
    // etend la reserve de vie et la remplit; inactif, il rend la vie normale.
    bool extended_health_enabled = false;
    bool extended_health_toggle_requested = false;
    // Sortie forcee du vehicule courant, meme en vol (F3).
    bool vehicle_exit_requested = false;
    // G : remet en etat le vehicule pilote, ou le plus proche, et le pose
    // devant le joueur lorsqu'il n'est pas dedans.
    bool vehicle_repair_requested = false;
    // G a pied : fenetre des soldats. Selection d'un soldat, d'un groupe ou
    // de tous, puis ordre de deplacement donne par la carte native.
    bool soldier_menu_open = false;
    bool soldier_menu_toggle_requested = false;
    int soldier_menu_move = 0;
    // Le nombre de soldats se TAPE au clavier : chiffre par chiffre, de 1
    // a 50, avec le retour arriere pour effacer. Les fleches gauche/droite
    // restent acceptees pour un reglage d'un cran.
    int soldier_menu_adjust = 0;
    int soldier_menu_digit = -1;     // 0 a 9, -1 quand rien n'est frappe
    bool soldier_menu_erase = false; // retour arriere
    int soldier_spawn_count = 5;
    // Taille du groupe a qui l'ordre de deplacement est donne. Elle se tape
    // elle aussi, sur sa propre ligne : le joueur choisit exactement combien
    // de soldats partent, au lieu des quatre premiers imposes.
    int soldier_group_count = 4;
    // V142 - RALLIEMENT D'ENNEMIS.
    //
    // Le nombre d'ennemis a faire passer de notre cote se tape lui aussi, sur
    // sa propre ligne de la fenetre G, exactement comme le nombre de soldats a
    // creer. Le maximum est le nombre d'ennemis reellement presents dans la
    // mission, compte a chaque image.
    int enemy_rally_count = 1;
    // V154 - MAIN LIBRE : deplacer un allie a la main, comme le noclip.
    //
    // Le joueur ne veut pas seulement lui donner une destination et le
    // regarder y marcher : il veut le prendre et le poser ou il veut, y
    // compris la ou aucun chemin ne mene. C'est exactement ce que le noclip
    // fait deja pour lui-meme.
    //
    // Ce nombre designe l'allie a piloter, et se tape comme les autres.
    int free_move_index = 1;
    // V163 - combien d'allies partent au contact quand on valide FEU.
    // Le maximum est le nombre de rallies vivants.
    int fire_count = 1;
    // V164 - FEU ALL : ils enchainent les cibles jusqu'a la derniere.
    bool fire_all_enabled = false;
    bool fire_all_toggle_requested = false;
    // V156 - la touche de switch du jeu continue sur les allies rallies.
    //
    // Le joueur : « des que je termine mes camarades je passe aux ennemis qui
    // sont avec moi; switch je passe au suivant par ordre de nombres; quand je
    // les termine je reviens a mes soldats d'origine ».
    //
    // Les touches 1 2 3 4 ne sont pas touchees : elles restent au jeu.
    bool switch_cycle_enabled = true;
    bool switch_cycle_toggle_requested = false;
    bool soldier_menu_confirm_requested = false;
    // Miroir d'arme : les soldats crees portent l'arme du joueur et en
    // changent avec lui. La touche J coupe le miroir; ils gardent alors ce
    // qu'ils ont.
    bool weapon_mirror_enabled = false;
    bool weapon_mirror_toggle_requested = false;
    // V124 - fenetre des armes, ouverte par J. Le joueur choisit une cible
    // (un soldat, un groupe, ou tous) puis une arme prise dans son propre
    // inventaire; les soldats designes recoivent exactement celle-la.
    bool weapon_menu_open = false;
    bool weapon_menu_toggle_requested = false;
    int weapon_menu_move = 0;
    int weapon_menu_target = 0;      // 0 = tous, 1 = groupe, 2+ = soldat n-2
    int weapon_menu_target_move = 0;
    bool weapon_menu_confirm_requested = false;
    // F6 : liste des vehicules de la mission, affichee dans le jeu.
    bool vehicle_menu_open = false;
    bool vehicle_menu_toggle_requested = false;
    int vehicle_menu_move = 0;
    bool vehicle_menu_confirm_requested = false;
    // Seconde action de la liste des vehicules : creer une copie conduisible
    // du vehicule choisi, au lieu de deplacer celui de la mission.
    bool vehicle_menu_clone_requested = false;
};

struct GameplayInput
{
    bool toggle_noclip_pressed = false;
    bool noclip_forward_down = false;
    bool noclip_backward_down = false;
    bool noclip_left_down = false;
    bool noclip_right_down = false;
    bool noclip_up_down = false;
    bool noclip_down_down = false;
    bool noclip_input_capture_available = false;
    // W is deliberately passed through to the game. While the network mask is
    // armed, each press alternates between the masked anchor and the true
    // location without returning to the trainer window.
    bool publish_real_position_pressed = false;
    bool speed_up_down = false;
    bool speed_down_down = false;
    // Physical AZERTY N/B keys, edge-triggered. N accelerates the vehicle
    // multiplier, B reduces it down to 1.0x (the game's default speed).
    bool vehicle_speed_up_pressed = false;
    bool vehicle_speed_down_pressed = false;
    bool steering_boost_up_pressed = false;
    bool steering_boost_down_pressed = false;
    // Touches physiques 9 et 8 de la rangee du haut, en front montant. 9
    // accelere le jeu, 8 le ramene vers sa vitesse normale.
    bool game_speed_up_pressed = false;
    bool game_speed_down_pressed = false;
    bool toggle_map_pressed = false;
    bool native_map_click_pressed = false;
    // Raw Win32 client position of the map click, captured at the click edge.
    // Diagnostic only: the teleport ray is built from the game's own
    // accumulated cursor coordinates read from the map manager.
    float native_map_client_x = 0.0f;
    float native_map_client_y = 0.0f;
    bool grant_all_items_pressed = false;
};

enum class GrantAllItemsStatus
{
    Ready,
    Success,
    NotConnected,
    InventoryUnavailable,
    Unsupported
};

enum class PlayerSpeedStatus
{
    Disabled,
    SuspendedInVehicle,
    WaitingForMission,
    Active,
    UnsupportedLayout,
    WriteFailed
};

enum class NoclipStatus
{
    Disabled,
    Armed,
    WaitingForMission,
    SuspendedInVehicle,
    InputCaptureUnavailable,
    Active,
    NetworkPublishUnavailable,
    UnsupportedLayout,
    WriteFailed
};

enum class NetworkPositionMaskStatus
{
    Disabled,
    Armed,
    WaitingForMission,
    Active,
    // The real network position remains masked, and the companion Client
    // helper has been asked to hide the host-owned player models locally.
    RemoteVisualHidden,
    RealPosition,
    UnsupportedRevision,
    WriteFailed
};

enum class TeleportStatus
{
    Ready,
    WaitingForMapClose,
    WaitingForMission,
    Success,
    NoWalkableGround,
    UnsupportedLayout,
    WriteFailed,
    NativeMapUnavailable,
    MapCursorUnavailable,
    MapRayMissed
};

enum class EnemyInvisibilityStatus
{
    Disabled,
    WaitingForMission,
    Active,
    // Network session hosted by this process: the local hooks own the AI, so
    // the whole squad and every connected player are covered.
    ActiveNetworkHost,
    // The trainer momentarily cannot read the mission, but the filter is
    // still installed in the game and still working. Reported instead of
    // WaitingForMission, which used to pull the hooks out.
    HeldThroughMissionGap,
    // Network session detected from the multiplayer socket, but the engine's
    // role variables were not set - which is the normal case for a session
    // started from the in-game menu. The whole-squad filter is applied, and
    // the text tells the player what to check if they joined.
    ActiveNetworkUnknownRole,
    // Network session this process only joined. The authoritative simulation
    // - and therefore every enemy perception and attack decision - runs on
    // the host, so the local hooks cannot make the player unseen. Reported
    // instead of Active so the panel never claims a protection it does not
    // have.
    HostAuthorityRequired,
    UnsupportedRevision,
    WriteFailed
};

enum class AbsolutePlayerProtectionStatus
{
    Disabled,
    WaitingForPlayer,
    Active,
    SharedDamageGuard,
    UnsupportedRevision,
    WriteFailed
};

enum class ReviveCurrentPlayerStatus
{
    Ready,
    Queued,
    Revived,
    PlayerAlive,
    WaitingForPlayer,
    WriteFailed
};

enum class AimbotStatus
{
    Disabled,
    WaitingForMission,
    NoTarget,
    Active,
    UnsupportedLayout,
    WriteFailed
};

enum class VehicleSpeedStatus
{
    Disabled,
    WaitingForVehicle,
    Active,
    UnsupportedLayout,
    WriteFailed
};

enum class ExtendedHealthStatus
{
    Disabled,
    Active,
    WaitingForPlayer,
    UnsupportedLayout,
    WriteFailed
};

enum class VehicleActionStatus
{
    Ready,
    Success,
    NoVehicle,
    WaitingForPlayer,
    Failed,
    // Etat mecanique remis, mais la carrosserie enfoncee ne peut pas etre
    // redressee en place : seule C_version::SetVersion le fait, et elle n'est
    // pas virtuelle donc son adresse n'est pas derivable de la vtable.
    BodyNotRepairable
};

enum class GameSpeedStatus
{
    Disabled,
    Active,
    UnsupportedLayout,
    WriteFailed
};

enum class VehicleInvulnerabilityStatus
{
    Disabled,
    WaitingForVehicle,
    Active,
    UnsupportedLayout,
    WriteFailed
};

enum class BulletTrackStatus
{
    Disabled,
    WaitingForMission,
    NoTargetInCircle,
    Active,
    UnsupportedLayout,
    WriteFailed
};

struct GameplayStatus
{
    NoclipStatus noclip = NoclipStatus::Disabled;
    NetworkPositionMaskStatus network_position_mask =
        NetworkPositionMaskStatus::Disabled;
    PlayerSpeedStatus player_speed = PlayerSpeedStatus::Disabled;
    TeleportStatus teleport = TeleportStatus::Ready;
    EnemyInvisibilityStatus enemy_invisibility =
        EnemyInvisibilityStatus::Disabled;
    AbsolutePlayerProtectionStatus absolute_player_protection =
        AbsolutePlayerProtectionStatus::Disabled;
    ReviveCurrentPlayerStatus revive_current_player =
        ReviveCurrentPlayerStatus::Ready;
    AimbotStatus aimbot = AimbotStatus::Disabled;
    BulletTrackStatus bullet_track = BulletTrackStatus::Disabled;
    // Published to the local overlay only while V2 is aiming at an occluded
    // target. It is intentionally not part of any game-memory publication.
    std::uintptr_t bullet_track_visual_target_actor = 0;
    VehicleSpeedStatus vehicle_speed = VehicleSpeedStatus::Disabled;
    VehicleInvulnerabilityStatus vehicle_invulnerability =
        VehicleInvulnerabilityStatus::Disabled;
    GameSpeedStatus game_speed = GameSpeedStatus::Disabled;
    ExtendedHealthStatus extended_health = ExtendedHealthStatus::Disabled;
    VehicleActionStatus vehicle_exit = VehicleActionStatus::Ready;
    VehicleActionStatus vehicle_repair = VehicleActionStatus::Ready;
    GrantAllItemsStatus grant_all_items = GrantAllItemsStatus::Ready;
    std::uint32_t granted_item_count = 0;
};

void UpdateGameplayModifiers(
    TrainerProcess& game_process,
    const RadarSnapshot* radar_snapshot,
    GameplaySettings& settings,
    const GameplayInput& input,
    GameplayStatus& status);

void RestoreGameplayModifiers(TrainerProcess& game_process);

// Executes the native callback behind the manual `immortality` command on
// the controlled player's game thread. No keyboard input is simulated.
bool ApplyNativeLifeUnlimited(
    TrainerProcess& process,
    const RadarSnapshot* snapshot);

const char* PlayerSpeedStatusText(const GameplayStatus& status);
const char* NoclipStatusText(const GameplayStatus& status);
const char* NetworkPositionMaskStatusText(const GameplayStatus& status);
const char* TeleportStatusText(const GameplayStatus& status);
const char* EnemyInvisibilityStatusText(const GameplayStatus& status);
const char* AbsolutePlayerProtectionStatusText(const GameplayStatus& status);
const char* ReviveCurrentPlayerStatusText(const GameplayStatus& status);
const char* AimbotStatusText(const GameplayStatus& status);
const char* BulletTrackStatusText(const GameplayStatus& status);
const char* VehicleSpeedStatusText(const GameplayStatus& status);
const char* VehicleInvulnerabilityStatusText(const GameplayStatus& status);
const char* GameSpeedStatusText(const GameplayStatus& status);
const char* ExtendedHealthStatusText(const GameplayStatus& status);
const char* VehicleExitStatusText(const GameplayStatus& status);
const char* VehicleRepairStatusText(const GameplayStatus& status);

// Liste des vehicules de la mission, publiee pour l'affichage in-game.
// Liste des soldats et de leur ordre courant, publiee pour l'affichage.
std::size_t WeaponMenuEntryCount();
const wchar_t* WeaponMenuEntry(std::size_t index);
std::size_t WeaponMenuSelection();

std::size_t SoldierMenuEntryCount();
const wchar_t* SoldierMenuEntry(std::size_t index);
int SoldierMenuSelection();

std::size_t VehicleMenuEntryCount();
const wchar_t* VehicleMenuEntry(std::size_t index);
int VehicleMenuSelection();
const char* GrantAllItemsStatusText(const GameplayStatus& status);
}
