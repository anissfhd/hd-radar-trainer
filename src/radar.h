#pragma once

#include "trainer_process.h"

#include <array>
#include <cstdint>

namespace hd
{
struct Vector3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Vector2
{
    float x = 0.0f;
    float y = 0.0f;
};

struct Matrix4x4
{
    float m[4][4]{};
};

struct PlayerPosition
{
    Vector3 position{};
    float heading_radians = 0.0f;
};

enum class EntityTeam : std::uint32_t
{
    Unknown = 0,
    Ally = 1,
    Enemy = 2
};

// Role of the local hde.exe inside a network session. A session is detected
// from the multiplayer socket the game owns, which is authoritative because
// it is observed from outside the process. The host/client refinement comes
// from the engine's `net_host` / `net_join` console variables when they are
// set. The distinction matters because a client process skips the
// authoritative simulation, so enemy AI perception is decided on the host.
enum class NetworkRole : std::uint32_t
{
    Offline = 0,
    Host = 1,
    Client = 2,
    // Reserved: a session is running but the side is unknown. Not produced
    // today - no reliable signal for it has been found yet.
    UnknownRole = 3
};

enum class TeleportMapResult
{
    Unavailable,
    Ready,
    GroundSelected,
    NoWalkableGround,
    MapNotActive,
    CursorBlockNotFound,
    RayMissed
};

// V142 - defini dans gameplay_mods.cpp.
//
// Un ennemi que le joueur a fait passer de son cote ne doit plus apparaitre
// comme une cible : ni en rouge sur le radar, ni dans les aides a la visee.
// Le moteur, lui, le sait deja - sa propriete de groupe a change. Cette
// fonction permet au radar de le savoir aussi.
bool IsActorRalliedToPlayer(std::uintptr_t actor);

struct RadarEntity
{
    Vector3 position{};
    Vector3 head_position{};
    std::uintptr_t actor_address = 0;
    std::uintptr_t frame_address = 0;
    std::uintptr_t head_frame_address = 0;
    EntityTeam team = EntityTeam::Unknown;
    std::uint32_t stay_mode = 0;
    std::uint32_t active = 0;
    std::uint32_t directly_visible = 0;
    // Set when the engine rendered this actor recently, whether or not the
    // ballistic path to it is clear. Shooting through walls needs this: an
    // enemy the engine no longer renders is not simulated either, and forcing
    // damage on one leaves its AI holding references it never cleans up.
    std::uint32_t rendered_recently = 0;
};

constexpr std::size_t kMaximumRadarEntities = 256;

struct EntityArray
{
    std::uint32_t count = 0;
    std::array<RadarEntity, kMaximumRadarEntities> entities{};
};

struct RadarSnapshot
{
    PlayerPosition player{};
    Vector3 player_aim_position{};
    EntityArray entity_array{};
    Matrix4x4 view_projection{};
    std::uintptr_t player_object_address = 0;
    std::uintptr_t player_frame_address = 0;
    std::uintptr_t entity_list_object_address = 0;
    std::uintptr_t entity_array_address = 0;
    std::uintptr_t scene_object_address = 0;
    std::uintptr_t camera_object_address = 0;
    std::uint32_t source_entity_count = 0;
    std::uint32_t readable_entity_count = 0;
    float game_client_width = 0.0f;
    float game_client_height = 0.0f;
    // Network role of this process, plus the census used to keep the local
    // player stable. `active_player_count` is how many player actors carried
    // the controlled flag on this pass: anything other than exactly one means
    // the flag is ambiguous and the previous choice must be kept.
    NetworkRole network_role = NetworkRole::Offline;
    // Raw evidence behind network_role, logged so a LAN test can be read back
    // afterwards instead of reproduced.
    std::uint8_t net_host_byte = 0;
    std::uint8_t net_join_byte = 0;
    std::uint32_t player_actor_count = 0;
    std::uint32_t active_player_count = 0;
    bool entity_array_valid = false;
    bool view_projection_valid = false;
    // Set when the mission pointer was transiently unreadable and this is the
    // previous complete snapshot. Hooks stay installed, but nothing that
    // targets a live actor may act on it.
    bool from_cache = false;
};

struct RadarRenderSettings
{
    float esp_alpha = 0.95f;
    bool show_enemy_esp = false;
    bool show_ally_esp = false;
    bool show_bullet_track_circle = false;
    // Cosmetic V2 tracer only. The gameplay code publishes the exact actor
    // selected for an instant wall impact; this overlay value is never sent to
    // the game or used for damage.
    bool show_bullet_track_v2_tracer = false;
    std::uintptr_t bullet_track_v2_tracer_target_actor = 0;
    // A short local-only confirmation shown after W changes the network-mask
    // state. It is owned by the trainer loop and never reaches game memory.
    const wchar_t* transient_notification = nullptr;
    // V98 : liste des vehicules de la mission, dessinee dans le jeu par la
    // fenetre superposee. Vide = rien n'est affiche.
    const wchar_t* menu_title = nullptr;
    // Ligne d'aide affichee sous la fenetre : elle dit quelle touche fait
    // quoi, pour qu'aucune commande ne reste a deviner.
    const wchar_t* menu_footer = nullptr;
    const wchar_t* const* vehicle_menu_entries = nullptr;
    std::uint32_t vehicle_menu_count = 0;
    std::int32_t vehicle_menu_selection = 0;
    float bullet_track_radius_pixels = 180.0f;
};

bool ReadRadarSnapshot(
    const TrainerProcess& game_process,
    RadarSnapshot& snapshot);

bool WorldToScreen(
    const Vector3& world_position,
    Vector2& screen_position,
    const Matrix4x4& view_projection,
    float screen_width,
    float screen_height);

void RenderEspOverlay(
    const RadarSnapshot* snapshot,
    const RadarRenderSettings& settings);

TeleportMapResult RenderTeleportMapCanvas(
    const RadarSnapshot* snapshot,
    float width,
    float height,
    Vector3& selected_ground);

TeleportMapResult ResolveNativeMapClick(
    const TrainerProcess& game_process,
    const RadarSnapshot& snapshot,
    float client_x,
    float client_y,
    Vector3& selected_ground);

// Places both the game's internal map cursor and the Win32 pointer on the
// exact screen projection of the controlled player when the map opens.
bool SyncNativeMapCursorToPlayer(
    const TrainerProcess& game_process,
    const RadarSnapshot& snapshot);
}
