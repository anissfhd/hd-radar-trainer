#include "radar.h"

#include "diagnostics.h"

#include <Windows.h>
#include <d3d9.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

#include <imgui_impl_dx9.h>

namespace hd
{
namespace
{
// H&D Deluxe / Insanity3D layout verified against the installed x86 build.
constexpr std::uint32_t kMissionSceneOffset = 0x10;
constexpr std::uint32_t kMissionActorsOffset = 0x68;
// Exact C_game_mission offsets from installed SwitchToMap at 004A7363:
// map_mgr is read at +0xA0 and map_active is set to one at +0xA4.
constexpr std::uint32_t kGameMissionMapManagerOffset = 0xA0;
constexpr std::uint32_t kGameMissionMapActiveOffset = 0xA4;
constexpr std::uint32_t kSceneActiveCameraOffset = 0x88;
constexpr std::uint32_t kSceneViewProjectionOffset = 0x8C;
// C_map_manager_imp::Tick begins with a relocatable five-byte absolute load
// in the supported hde.exe. A temporary detour here lets the game's own
// I3D_scene::UnmapScreenPoint run on the mission thread.
constexpr std::uintptr_t kMapManagerTickRva = 0x000B1AD0;
// Rendering timestamps recovered from the official Insanity3D sources and
// verified in the installed i3d2.dll vtable accessors for this x86 build.
constexpr std::uint32_t kSceneLastRenderTimeOffset = 0x18C;
// hde.exe's ballistic collision callback reads three byte properties from the
// collision-material table rooted at this revision-specific global pointer.
constexpr std::uint32_t kCollisionTablePointerRva = 0x0010AC04;
// hde.exe exposes a network role through two console variables of its own
// command table (entries at .rdata:0x004F9E98 and 0x004F9EA8), and the engine
// branches on them in 0x004713AD / 0x004716EA to pick the host or the client
// path. They are only a refinement, never the detector: a LAN test proved
// both stay 0 for a session started from the multiplayer menu, because only
// the console and the command line assign them.
constexpr std::uint32_t kNetHostRva = 0x0010ABE4;
constexpr std::uint32_t kNetJoinRva = 0x0010ABE5;
constexpr std::uint32_t kCollisionTablePropertyCountOffset = 0x0C;
constexpr std::uint32_t kCollisionTableDescriptorsOffset = 0x20;
constexpr std::uint32_t kCollisionTableDataOffset = 0x24;
// This installed i3d2.dll embeds its polymorphic C_bsp_tree at scene + 0x1C4.
// The layout below was confirmed from the IsBspBuild/TestCollision accessors
// and from the live vectors in the supported x86 build.
constexpr std::uint32_t kSceneBspTreeOffset = 0x1C4;
constexpr std::uint32_t kBspValidOffset = 0x04;
constexpr std::uint32_t kBspFacesOffset = 0x20;
constexpr std::size_t kBspFaceSize = 0x9C;
constexpr std::size_t kBspFaceOriginFrameOffset = 0x00;
constexpr std::size_t kBspFaceVertex0Offset = 0x14;
constexpr std::size_t kBspFaceVertex1Offset = 0x20;
constexpr std::size_t kBspFaceVertex2Offset = 0x2C;
constexpr std::uint32_t kFrameTypeOffset = 0x08;
constexpr std::uint32_t kFrameFlagsOffset = 0x0C;
constexpr std::uint32_t kFrameChildrenOffset = 0x20;
constexpr std::uint32_t kActorTypeOffset = 0x1C;
constexpr std::uint32_t kActorFrameOffset = 0x28;
constexpr std::uint32_t kActorHeadFrameOffset = 0x1C8;
constexpr std::uint32_t kActorStayModeOffset = 0x254;
constexpr std::uint32_t kActorActivePlayerOffset = 0x2B8;
constexpr std::uint32_t kFrameActorBackReferenceOffset = 0x80;
constexpr std::uint32_t kFrameWorldDirectionOffset = 0xAC;
constexpr std::uint32_t kFrameWorldPositionOffset = 0xBC;
constexpr std::uint32_t kVisualLastRenderTimeOffset = 0x194;
constexpr std::uint32_t kVisualCollisionMaterialOffset = 0x184;
constexpr std::uint32_t kFrameTypeVisual = 1;
constexpr std::uint32_t kFrameTypeCamera = 3;
constexpr std::uint32_t kFrameTypeVolume = 11;
constexpr std::uint32_t kFrameFlagNoCollision = 0x02;
constexpr std::uint32_t kActorTypePlayer = 1;
constexpr std::uint32_t kActorTypeEnemy = 2;
constexpr std::uint32_t kStayModeDead = 4;
constexpr std::uint32_t kMaximumSourceActors = 4096;
constexpr std::uint32_t kMaximumFrameChildren = 256;
constexpr std::size_t kMaximumActorFrames = 512;
constexpr std::size_t kMaximumBspFaces = 250'000;
constexpr std::uint32_t kMaximumCollisionProperties = 256;
constexpr std::uint32_t kMaximumCollisionMaterials = 4096;
constexpr std::size_t kCollisionBvhLeafSize = 8;
constexpr std::uint32_t kVisibilityFreshnessMs = 100;
constexpr float kMaximumSaneCoordinate = 10'000'000.0f;
constexpr float kMaximumAimFrameHorizontalDistance = 1.25f;
constexpr float kMinimumAimFrameHeight = -0.50f;
constexpr float kMaximumAimFrameHeight = 2.50f;
constexpr float kFallbackEyeHeight = 1.58f;
constexpr float kEyeInset = 0.04f;
constexpr float kHeadInset = 0.08f;
constexpr float kRayEndpointInset = 0.04f;
constexpr float kHeadSampleRadius = 0.105f;
constexpr float kHeadSampleVerticalRadius = 0.095f;
constexpr float kShoulderSampleRadius = 0.22f;
constexpr float kTorsoSampleRadius = 0.14f;
// The actor frame is rooted at the character's feet. H&D uses metre-like
// world units, so this produces a stable standing-human box without relying
// on model-specific bones that are not exposed by the external actor list.
constexpr float kApproximateHumanHeight = 1.75f;
constexpr float kBoxWidthToHeightRatio = 0.42f;
constexpr float kMinimumBoxHeight = 2.0f;
constexpr float kMinimumClipW = 0.01f;
constexpr float kMinimumWalkableNormalY = 0.5735764f; // cos(55 degrees)
constexpr float kTeleportMapPadding = 12.0f;
constexpr std::size_t kMaximumMapTrianglesDrawn = 14'000;
constexpr std::size_t kMapManagerCursorScanSize = 0x600;
constexpr std::uintptr_t kMapCursorMouseXOffset = 0x20;
constexpr std::uintptr_t kMapCursorMouseYOffset = 0x24;
constexpr wchar_t kOverlayWindowClass[] = L"HDSnaplineOverlayWindow";

struct RemoteVector32
{
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
    std::uint32_t capacity = 0;
};

struct ActorRecord
{
    std::uintptr_t actor = 0;
    std::uintptr_t frame = 0;
    std::uint32_t type = 0;
    std::uint32_t stay_mode = 0;
    Vector3 position{};
};

struct ActorFrameState
{
    std::uint32_t latest_render_time = 0;
    Vector3 highest_nearby_position{};
    bool found_visual_render_time = false;
    bool found_aim_position = false;
};

struct CollisionTriangle
{
    Vector3 a{};
    Vector3 b{};
    Vector3 c{};
    Vector3 bounds_min{};
    Vector3 bounds_max{};
    Vector3 centroid{};
};

struct CollisionOriginState
{
    std::uintptr_t frame = 0;
    std::uint32_t frame_type = 0;
    std::uint32_t frame_flags = 0;
    std::uint32_t material_id = 0;
    bool readable = false;
    bool has_material_id = false;
};

struct CollisionPropertyDescriptor
{
    std::uint32_t data_offset = UINT32_MAX;
    std::uint16_t type = 0;
    std::uint16_t count = 0;
};

struct MapCursorLayoutCandidate
{
    std::int32_t mode = 0;
    Vector3 world_minimum{};
    Vector3 world_maximum{};
    float zoom_maximum = 0.0f;
    std::int32_t mouse_x = 0;
    std::int32_t mouse_y = 0;
    Vector3 look_at{};
    float look_angle = 0.0f;
    float look_distance = 0.0f;
    float pan_speed = 0.0f;
};

struct CollisionBvhNode
{
    Vector3 bounds_min{};
    Vector3 bounds_max{};
    std::uint32_t left = UINT32_MAX;
    std::uint32_t right = UINT32_MAX;
    std::uint32_t first = 0;
    std::uint32_t count = 0;
};

struct StaticCollisionCache
{
    std::uintptr_t scene = 0;
    std::uint32_t face_begin = 0;
    std::uint32_t face_end = 0;
    bool excludes_ballistic_pass_through = false;
    bool initialized = false;
    bool valid = false;
    std::vector<CollisionTriangle> triangles;
    std::vector<std::uint32_t> indices;
    std::vector<CollisionBvhNode> nodes;
};

enum class RadarReadState
{
    Disconnected,
    RootUnavailable,
    MissionUnavailable,
    SceneUnavailable,
    CameraUnavailable,
    MatrixUnavailable,
    InvalidActorVector,
    ActorArrayUnreadable,
    LocalPlayerUnavailable,
    Ready
};

struct RadarRuntime
{
    DWORD game_process_id = 0;
    RadarReadState read_state = RadarReadState::Disconnected;
    // Last actor accepted as the controlled player. The engine's controlled
    // flag is not always carried by exactly one player actor, so this keeps
    // the choice from flipping between squad members - and, in a network
    // session, onto the remote human player.
    std::uintptr_t last_local_player = 0;
    // Where that actor was. The game writes actor+0x2B8 itself
    // (mov [esi+2B8],bl at 0x0042A383), and it can be clear on every player
    // actor at once. When the remembered address also disappears - a respawn
    // reallocates it - the nearest player actor to this position is the same
    // soldier, and it keeps the whole radar from dying for the rest of the
    // mission.
    Vector3 last_local_player_position{};
    bool has_last_local_player_position = false;
};

RadarRuntime g_runtime{};
StaticCollisionCache g_collision_cache{};

struct GameClientBounds
{
    HWND window = nullptr;
    POINT top_left{};
    int width = 0;
    int height = 0;
    std::uint64_t visible_area = 0;
};

struct GameWindowSearch
{
    DWORD process_id = 0;
    RECT virtual_desktop{};
    GameClientBounds best{};
};

bool GetGameClientBounds(
    HWND window,
    DWORD process_id,
    const RECT& virtual_desktop,
    GameClientBounds& bounds)
{
    if (!IsWindow(window) || !IsWindowVisible(window) || IsIconic(window))
        return false;

    DWORD window_process_id = 0;
    GetWindowThreadProcessId(window, &window_process_id);
    if (window_process_id != process_id)
        return false;

    RECT client{};
    if (!GetClientRect(window, &client))
        return false;

    POINT top_left{client.left, client.top};
    POINT bottom_right{client.right, client.bottom};
    if (!ClientToScreen(window, &top_left) ||
        !ClientToScreen(window, &bottom_right))
    {
        return false;
    }

    const int width = bottom_right.x - top_left.x;
    const int height = bottom_right.y - top_left.y;
    if (width <= 0 || height <= 0)
        return false;

    const RECT screen_client{
        top_left.x,
        top_left.y,
        bottom_right.x,
        bottom_right.y};
    RECT visible_client{};
    if (!IntersectRect(&visible_client, &screen_client, &virtual_desktop))
        return false;

    const std::uint64_t visible_width = static_cast<std::uint64_t>(
        visible_client.right - visible_client.left);
    const std::uint64_t visible_height = static_cast<std::uint64_t>(
        visible_client.bottom - visible_client.top);

    bounds.window = window;
    bounds.top_left = top_left;
    bounds.width = width;
    bounds.height = height;
    bounds.visible_area = visible_width * visible_height;
    return bounds.visible_area != 0;
}

BOOL CALLBACK FindLargestGameChildWindow(HWND window, LPARAM parameter)
{
    auto& search = *reinterpret_cast<GameWindowSearch*>(parameter);
    GameClientBounds candidate{};
    if (GetGameClientBounds(
            window,
            search.process_id,
            search.virtual_desktop,
            candidate) &&
        candidate.visible_area > search.best.visible_area)
    {
        search.best = candidate;
    }
    return TRUE;
}

BOOL CALLBACK FindLargestGameWindow(HWND window, LPARAM parameter)
{
    auto& search = *reinterpret_cast<GameWindowSearch*>(parameter);

    DWORD window_process_id = 0;
    GetWindowThreadProcessId(window, &window_process_id);
    if (window_process_id != search.process_id)
        return TRUE;

    FindLargestGameChildWindow(window, parameter);
    EnumChildWindows(window, FindLargestGameChildWindow, parameter);
    return TRUE;
}

bool FindGameClientBounds(DWORD process_id, GameClientBounds& bounds)
{
    bounds = {};
    if (process_id == 0)
        return false;

    const int virtual_left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int virtual_top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int virtual_width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int virtual_height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (virtual_width <= 0 || virtual_height <= 0)
        return false;

    GameWindowSearch search{};
    search.process_id = process_id;
    search.virtual_desktop = {
        virtual_left,
        virtual_top,
        virtual_left + virtual_width,
        virtual_top + virtual_height};
    EnumWindows(FindLargestGameWindow, reinterpret_cast<LPARAM>(&search));

    bounds = search.best;
    return bounds.window != nullptr && bounds.width > 0 && bounds.height > 0;
}

bool IsFinite(const Vector3& value)
{
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

bool IsSane(const Vector3& value)
{
    return IsFinite(value) &&
        std::fabs(value.x) <= kMaximumSaneCoordinate &&
        std::fabs(value.y) <= kMaximumSaneCoordinate &&
        std::fabs(value.z) <= kMaximumSaneCoordinate;
}

bool IsSane(const Matrix4x4& matrix)
{
    for (const auto& row : matrix.m)
    {
        for (const float value : row)
        {
            if (!std::isfinite(value) || std::fabs(value) > 1.0e9f)
                return false;
        }
    }

    const float w_axis_length_squared =
        matrix.m[0][3] * matrix.m[0][3] +
        matrix.m[1][3] * matrix.m[1][3] +
        matrix.m[2][3] * matrix.m[2][3];
    return w_axis_length_squared > 0.000001f;
}

bool UnmapNativeMapPointOnMainThread(
    const TrainerProcess& process,
    std::uintptr_t map_scene,
    std::int32_t screen_x,
    std::int32_t screen_y,
    Vector3& ray_origin,
    Vector3& ray_direction,
    Vector3& map_hit)
{
    constexpr std::size_t kRemoteSize = 0x300;
    constexpr std::size_t kResultOffset = 0x120;
    constexpr std::size_t kCompletionOffset = 0x124;
    constexpr std::size_t kCollisionResultOffset = 0x128;
    constexpr std::size_t kCollisionDataOffset = 0x140;
    constexpr std::size_t kCollisionDataSize = 0xA0;
    constexpr std::size_t kCollisionFromOffset = 0x04;
    constexpr std::size_t kCollisionDirectionOffset = 0x10;
    constexpr std::size_t kCollisionFlagsOffset = 0x1C;
    constexpr std::size_t kCollisionClosestHitOffset = 0x40;
    constexpr std::uint32_t kExactRayCollisionFlags = 0x0104'0000;
    constexpr std::array<std::uint8_t, 5> kExpected{
        0xA1, 0xE0, 0xAA, 0x50, 0x00};

    ray_origin = {};
    ray_direction = {};
    map_hit = {};
    RemoteModuleInfo module{};
    if (!process.IsConnected() ||
        map_scene < 0x10000U || map_scene > 0x7FFF'FFFFU ||
        !process.GetMainModuleInfo(module) ||
        kMapManagerTickRva + kExpected.size() > module.image_size)
    {
        LogDiagnostic("Teleport: native unmap prerequisites unavailable.");
        return false;
    }

    const std::uintptr_t hook = module.base_address + kMapManagerTickRva;
    std::uintptr_t scene_vtable = 0;
    std::uintptr_t unmap_screen_point = 0;
    std::uintptr_t test_collision = 0;
    if (!process.ReadMemory(map_scene, scene_vtable) ||
        scene_vtable < 0x10000U || scene_vtable > 0x7FFF'FFFFU ||
        !process.ReadMemory(scene_vtable + 0x34, unmap_screen_point) ||
        unmap_screen_point < 0x10000U ||
        unmap_screen_point > 0x7FFF'FFFFU ||
        !process.IsReadableCodeTarget(unmap_screen_point) ||
        !process.ReadMemory(scene_vtable + 0x6C, test_collision) ||
        test_collision < 0x10000U || test_collision > 0x7FFF'FFFFU ||
        !process.IsReadableCodeTarget(test_collision))
    {
        LogDiagnostic(
            "Teleport: map scene methods unusable "
            "(scene=%08X vtable=%08X unmap=%08X collision=%08X).",
            static_cast<unsigned>(map_scene),
            static_cast<unsigned>(scene_vtable),
            static_cast<unsigned>(unmap_screen_point),
            static_cast<unsigned>(test_collision));
        return false;
    }

    std::array<std::uint8_t, 5> original{};
    if (!process.ReadMemory(hook, original.data(), original.size()) ||
        original != kExpected)
    {
        LogDiagnostic(
            "Teleport: map Tick signature mismatch at %08X "
            "(read %02X %02X %02X %02X %02X).",
            static_cast<unsigned>(hook), original[0], original[1],
            original[2], original[3], original[4]);
        return false;
    }

    const std::uintptr_t remote = process.AllocateRemoteMemory(kRemoteSize);
    if (remote < 0x10000U || remote > 0x7FFF'FFFFU)
    {
        LogDiagnostic("Teleport: native unmap allocation failed.");
        return false;
    }
    const std::uintptr_t remote_collision_data =
        remote + kCollisionDataOffset;
    const std::uintptr_t remote_origin =
        remote_collision_data + kCollisionFromOffset;
    const std::uintptr_t remote_direction =
        remote_collision_data + kCollisionDirectionOffset;
    const std::uintptr_t remote_closest_hit =
        remote_collision_data + kCollisionClosestHitOffset;
    const std::uintptr_t remote_result = remote + kResultOffset;
    const std::uintptr_t completion = remote + kCompletionOffset;
    const std::uintptr_t remote_collision_result =
        remote + kCollisionResultOffset;
    const auto relative = [](std::uintptr_t target,
                             std::uintptr_t next_instruction)
    {
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<std::intptr_t>(target) -
            static_cast<std::intptr_t>(next_instruction)));
    };

    std::vector<std::uint8_t> code;
    code.reserve(96);
    const auto byte = [&](std::uint8_t value) { code.push_back(value); };
    const auto dword = [&](std::uint32_t value)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(value));
        std::memcpy(code.data() + offset, &value, sizeof(value));
    };
    byte(0x9C);                            // pushfd
    byte(0x60);                            // pushad
    byte(0x68); dword(static_cast<std::uint32_t>(remote_direction));
                                           // push &direction
    byte(0x68); dword(static_cast<std::uint32_t>(remote_origin));
                                           // push &origin
    byte(0x68); dword(static_cast<std::uint32_t>(screen_y));
    byte(0x68); dword(static_cast<std::uint32_t>(screen_x));
    byte(0xB8); dword(static_cast<std::uint32_t>(map_scene));
                                           // mov eax,map scene
    byte(0x50);                            // push scene (__stdcall this)
    byte(0x8B); byte(0x00);                // mov eax,[eax]
    byte(0xFF); byte(0x50); byte(0x34);    // call [vtable+34]
    byte(0xA3); dword(static_cast<std::uint32_t>(remote_result));
                                           // mov [result],eax
    byte(0x85); byte(0xC0);                // test eax,eax (I3D_OK)
    byte(0x0F); byte(0x85);                // jne publish completion
    const std::size_t unmap_failed_displacement = code.size();
    dword(0);
    byte(0xB8); dword(static_cast<std::uint32_t>(map_scene));
                                           // mov eax,map scene
    byte(0x68); dword(static_cast<std::uint32_t>(remote_collision_data));
                                           // push &I3D_collision_data
    byte(0x50);                            // push scene (__stdcall this)
    byte(0x8B); byte(0x00);                // mov eax,[eax]
    byte(0xFF); byte(0x50); byte(0x6C);    // call TestCollision
    byte(0x0F); byte(0xB6); byte(0xC0);    // movzx eax,al
    byte(0xA3); dword(static_cast<std::uint32_t>(
        remote_collision_result));         // mov [collision result],eax
    const std::size_t publish_completion = code.size();
    byte(0xC7); byte(0x05);
    dword(static_cast<std::uint32_t>(completion));
    dword(1U);                             // mov [completion],1
    byte(0x61);                            // popad
    byte(0x9D);                            // popfd
    for (const std::uint8_t value : original)
        byte(value);                       // relocated mov eax,[0050AAE0]
    byte(0xE9);
    const std::uintptr_t jump_next = remote + code.size() + 4U;
    dword(relative(hook + original.size(), jump_next));

    const std::uint32_t unmap_failed_relative = relative(
        remote + publish_completion,
        remote + unmap_failed_displacement + 4U);
    std::memcpy(
        code.data() + unmap_failed_displacement,
        &unmap_failed_relative,
        sizeof(unmap_failed_relative));

    const std::uint32_t pending = 0;
    std::array<std::uint8_t, kCollisionDataSize> collision_data{};
    const float initial_closest_hit = 1.0e16f;
    std::memcpy(
        collision_data.data() + kCollisionFlagsOffset,
        &kExactRayCollisionFlags,
        sizeof(kExactRayCollisionFlags));
    std::memcpy(
        collision_data.data() + kCollisionClosestHitOffset,
        &initial_closest_hit,
        sizeof(initial_closest_hit));
    if (!process.WriteMemory(
            remote_collision_data,
            collision_data.data(),
            collision_data.size()) ||
        !process.WriteMemory(remote_result, pending) ||
        !process.WriteMemory(remote_collision_result, pending) ||
        !process.WriteMemory(completion, pending) ||
        !process.WriteMemory(remote, code.data(), code.size()))
    {
        LogDiagnostic("Teleport: native unmap remote write failed.");
        (void)process.FreeRemoteMemory(remote);
        return false;
    }

    std::array<std::uint8_t, 5> jump{0xE9, 0, 0, 0, 0};
    const std::uint32_t hook_relative =
        relative(remote, hook + jump.size());
    std::memcpy(jump.data() + 1, &hook_relative, sizeof(hook_relative));
    if (!process.WriteProtectedMemory(hook, jump.data(), jump.size()))
    {
        std::array<std::uint8_t, 5> current{};
        bool restored = process.ReadMemory(
            hook, current.data(), current.size()) && current == original;
        if (!restored && current == jump)
        {
            (void)process.WriteProtectedMemory(
                hook, original.data(), original.size());
            restored = process.ReadMemory(
                hook, current.data(), current.size()) && current == original;
        }
        bool idle = false;
        if (restored)
        {
            bool executing = false;
            idle = process.IsAnyThreadExecutingRange(
                remote, kRemoteSize, executing) && !executing;
        }
        if (restored && idle)
            (void)process.FreeRemoteMemory(remote);
        LogDiagnostic(
            "Teleport: map Tick hook failed (restored=%d idle=%d).",
            restored ? 1 : 0, idle ? 1 : 0);
        return false;
    }

    std::uint32_t completed = 0;
    int polls = 0;
    for (; polls < 500 && completed == 0; ++polls)
    {
        (void)process.ReadMemory(completion, completed);
        if (completed == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    (void)process.WriteProtectedMemory(hook, original.data(), original.size());
    std::array<std::uint8_t, 5> restored_bytes{};
    const bool restored = process.ReadMemory(
        hook, restored_bytes.data(), restored_bytes.size()) &&
        restored_bytes == original;
    bool idle = false;
    if (restored)
    {
        for (int attempt = 0; attempt < 250 && !idle; ++attempt)
        {
            bool executing = false;
            idle = process.IsAnyThreadExecutingRange(
                remote, kRemoteSize, executing) && !executing;
            if (!idle)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    std::uint32_t native_result = 0xFFFF'FFFFU;
    std::uint32_t collision_result = 0;
    float closest_hit = (std::numeric_limits<float>::infinity)();
    const bool output_read = completed == 1U && restored && idle &&
        process.ReadMemory(remote_result, native_result) &&
        process.ReadMemory(remote_collision_result, collision_result) &&
        process.ReadMemory(remote_origin, ray_origin) &&
        process.ReadMemory(remote_direction, ray_direction) &&
        process.ReadMemory(remote_closest_hit, closest_hit);
    const float direction_length_squared =
        ray_direction.x * ray_direction.x +
        ray_direction.y * ray_direction.y +
        ray_direction.z * ray_direction.z;
    const float direction_length =
        direction_length_squared > 0.0f
            ? std::sqrt(direction_length_squared)
            : 0.0f;
    if (output_read && direction_length > 0.000001f &&
        std::isfinite(closest_hit))
    {
        const float ray_scale = closest_hit / direction_length;
        map_hit = {
            ray_origin.x + ray_direction.x * ray_scale,
            ray_origin.y + ray_direction.y * ray_scale,
            ray_origin.z + ray_direction.z * ray_scale};
    }
    const bool output_valid = output_read && native_result == 0U &&
        collision_result == 1U && closest_hit > 0.0f &&
        IsSane(ray_origin) && IsSane(ray_direction) &&
        IsSane(map_hit) &&
        std::isfinite(direction_length_squared) &&
        direction_length_squared > 0.000001f;
    const bool page_released = restored && idle &&
        process.FreeRemoteMemory(remote);
    LogDiagnostic(
        "Teleport: native map pick (%d,%d) completion=%u unmap=%08X "
        "collision=%u distance=%.3f restored=%d idle=%d freed=%d polls=%d "
        "origin=(%.2f,%.2f,%.2f) dir=(%.4f,%.4f,%.4f) "
        "hit=(%.2f,%.2f,%.2f).",
        screen_x, screen_y, completed, native_result, collision_result,
        closest_hit,
        restored ? 1 : 0, idle ? 1 : 0, page_released ? 1 : 0, polls,
        ray_origin.x, ray_origin.y, ray_origin.z,
        ray_direction.x, ray_direction.y, ray_direction.z,
        map_hit.x, map_hit.y, map_hit.z);
    return output_valid && page_released;
}

bool ReadNativeMapCursor(
    const TrainerProcess& game_process,
    std::uintptr_t map_manager,
    float client_width,
    float client_height,
    float& cursor_x,
    float& cursor_y,
    std::uint32_t* matched_offset = nullptr)
{
    std::array<std::uint8_t, kMapManagerCursorScanSize> bytes{};
    if (!game_process.ReadMemory(
            map_manager, bytes.data(), bytes.size()))
    {
        return false;
    }

    // The installed class keeps these source fields consecutively, but its
    // old MSVC6 base classes make the absolute offset revision-dependent.
    // Locate the block by the exact invariants used by C_map_manager_imp::Init
    // instead of guessing another hard-coded offset.
    for (std::size_t offset = 0;
         offset + sizeof(MapCursorLayoutCandidate) <= bytes.size();
         offset += alignof(std::uint32_t))
    {
        MapCursorLayoutCandidate candidate{};
        std::memcpy(&candidate, bytes.data() + offset, sizeof(candidate));
        if (candidate.mode < 0 || candidate.mode > 6 ||
            !IsSane(candidate.world_minimum) ||
            !IsSane(candidate.world_maximum) ||
            !IsSane(candidate.look_at) ||
            !std::isfinite(candidate.zoom_maximum) ||
            !std::isfinite(candidate.look_angle) ||
            !std::isfinite(candidate.look_distance) ||
            !std::isfinite(candidate.pan_speed))
        {
            continue;
        }
        const Vector3 extent{
            candidate.world_maximum.x - candidate.world_minimum.x,
            candidate.world_maximum.y - candidate.world_minimum.y,
            candidate.world_maximum.z - candidate.world_minimum.z};
        if (extent.x <= 0.01f || extent.y < 0.0f || extent.z <= 0.01f)
            continue;
        const float expected_zoom = (std::max)(
            48.0f, (std::max)(extent.x, extent.z));
        const float expected_pan = std::sqrt(
            extent.x * extent.x + extent.y * extent.y +
            extent.z * extent.z) / 80.0f;
        if (std::fabs(candidate.zoom_maximum - expected_zoom) >
                (std::max)(0.1f, expected_zoom * 0.01f) ||
            std::fabs(candidate.pan_speed - expected_pan) >
                (std::max)(0.01f, expected_pan * 0.03f) ||
            candidate.look_distance < 47.0f ||
            candidate.look_distance > candidate.zoom_maximum + 1.0f ||
            candidate.look_at.x < candidate.world_minimum.x - 1.0f ||
            candidate.look_at.x > candidate.world_maximum.x + 1.0f ||
            candidate.look_at.z < candidate.world_minimum.z - 1.0f ||
            candidate.look_at.z > candidate.world_maximum.z + 1.0f ||
            candidate.mouse_x < 0 || candidate.mouse_y < 0 ||
            candidate.mouse_x > static_cast<std::int32_t>(client_width) ||
            candidate.mouse_y > static_cast<std::int32_t>(client_height))
        {
            continue;
        }
        cursor_x = static_cast<float>(candidate.mouse_x);
        cursor_y = static_cast<float>(candidate.mouse_y);
        if (matched_offset)
            *matched_offset = static_cast<std::uint32_t>(offset);
        return true;
    }
    return false;
}

bool ReadRemotePointer(
    const TrainerProcess& process,
    std::uintptr_t address,
    std::uintptr_t& pointer)
{
    std::uint32_t pointer32 = 0;
    if (!process.ReadMemory(address, pointer32) || pointer32 == 0)
        return false;

    pointer = static_cast<std::uintptr_t>(pointer32);
    return true;
}

bool GetRemoteVectorCount(
    const RemoteVector32& vector,
    std::uint32_t maximum_count,
    std::uint32_t& count)
{
    if (vector.begin == 0)
    {
        count = 0;
        return vector.end == 0 && vector.capacity == 0;
    }

    if (vector.end < vector.begin ||
        vector.capacity < vector.end)
    {
        return false;
    }

    const std::uint32_t used_bytes = vector.end - vector.begin;
    const std::uint32_t capacity_bytes = vector.capacity - vector.begin;
    if (used_bytes % sizeof(std::uint32_t) != 0 ||
        capacity_bytes % sizeof(std::uint32_t) != 0)
    {
        return false;
    }

    count = used_bytes / sizeof(std::uint32_t);
    const std::uint32_t capacity = capacity_bytes / sizeof(std::uint32_t);
    return count <= capacity && count <= maximum_count;
}

bool GetActorCount(const RemoteVector32& vector, std::uint32_t& count)
{
    return GetRemoteVectorCount(vector, kMaximumSourceActors, count) &&
        vector.begin != 0;
}

float Component(const Vector3& value, int axis)
{
    return axis == 0 ? value.x : axis == 1 ? value.y : value.z;
}

Vector3 Minimum(const Vector3& a, const Vector3& b)
{
    return {
        (std::min)(a.x, b.x),
        (std::min)(a.y, b.y),
        (std::min)(a.z, b.z)};
}

Vector3 Maximum(const Vector3& a, const Vector3& b)
{
    return {
        (std::max)(a.x, b.x),
        (std::max)(a.y, b.y),
        (std::max)(a.z, b.z)};
}

Vector3 Subtract(const Vector3& a, const Vector3& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vector3 Cross(const Vector3& a, const Vector3& b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x};
}

float Dot(const Vector3& a, const Vector3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 ReadVector3(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    Vector3 value{};
    if (offset <= bytes.size() && bytes.size() - offset >= sizeof(value))
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

std::uint32_t ReadU32(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset)
{
    std::uint32_t value = 0;
    if (offset <= bytes.size() && bytes.size() - offset >= sizeof(value))
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

bool ReadCollisionMaterialId(
    const TrainerProcess& process,
    std::uintptr_t frame,
    std::uint32_t frame_type,
    std::uint32_t& material_id)
{
    material_id = 0;
    if (frame_type == kFrameTypeVisual)
    {
        return process.ReadMemory(
            frame + kVisualCollisionMaterialOffset, material_id);
    }

    if (frame_type != kFrameTypeVolume)
        return false;

    // The installed volume class exposes GetCollisionMaterial at vtable +
    // 0xE8. Decode only its small read-only accessor so this remains robust
    // if the volume field is placed differently from the visual field.
    std::uint32_t vtable = 0;
    std::uint32_t getter = 0;
    std::array<std::uint8_t, 16> code{};
    if (!process.ReadMemory(frame, vtable) || vtable == 0 ||
        !process.ReadMemory(
            static_cast<std::uintptr_t>(vtable) + 0xE8, getter) ||
        getter == 0 ||
        !process.ReadMemory(getter, code.data(), code.size()))
    {
        return false;
    }

    std::uint32_t field_offset = 0;
    if (code[0] == 0x8B && code[1] == 0x44 && code[2] == 0x24 &&
        code[3] == 0x04 && code[4] == 0x8B && code[5] == 0x80)
    {
        std::memcpy(&field_offset, code.data() + 6, sizeof(field_offset));
    }
    else if (code[0] == 0x8B && code[1] == 0x44 && code[2] == 0x24 &&
             code[3] == 0x04 && code[4] == 0x8B && code[5] == 0x40)
    {
        field_offset = code[6];
    }
    else
    {
        return false;
    }

    return field_offset <= 0x1000 &&
        process.ReadMemory(frame + field_offset, material_id);
}

bool ReadBallisticPassThroughMaterials(
    const TrainerProcess& process,
    std::vector<std::uint32_t>& material_ids)
{
    material_ids.clear();
    RemoteModuleInfo module{};
    std::uintptr_t table = 0;
    if (!process.GetMainModuleInfo(module) || module.base_address == 0 ||
        !ReadRemotePointer(
            process,
            module.base_address + kCollisionTablePointerRva,
            table))
    {
        return false;
    }

    std::uint32_t property_count = 0;
    std::uint32_t descriptors = 0;
    std::uint32_t data = 0;
    if (!process.ReadMemory(
            table + kCollisionTablePropertyCountOffset, property_count) ||
        property_count > kMaximumCollisionProperties ||
        !process.ReadMemory(
            table + kCollisionTableDescriptorsOffset, descriptors) ||
        descriptors == 0 ||
        !process.ReadMemory(table + kCollisionTableDataOffset, data) ||
        data == 0)
    {
        return false;
    }

    // hde.exe callback 0x00435920 checks properties 8, 11 and 9, then also
    // rejects material 42 explicitly. A rejected hit is penetrable to shots.
    constexpr std::array<std::uint32_t, 3> property_indices{8, 11, 9};
    for (const std::uint32_t property_index : property_indices)
    {
        if (property_index >= property_count)
            return false;

        CollisionPropertyDescriptor descriptor{};
        if (!process.ReadMemory(
                static_cast<std::uintptr_t>(descriptors) +
                    property_index * sizeof(descriptor),
                descriptor) ||
            descriptor.count > kMaximumCollisionMaterials)
        {
            return false;
        }
        if (descriptor.data_offset == UINT32_MAX || descriptor.count == 0)
            continue;

        std::vector<std::uint8_t> values(descriptor.count);
        if (!process.ReadMemory(
                static_cast<std::uintptr_t>(data) + descriptor.data_offset,
                values.data(),
                values.size()))
        {
            return false;
        }
        for (std::uint32_t id = 0; id < values.size(); ++id)
        {
            if (values[id] != 0)
                material_ids.push_back(id);
        }
    }

    material_ids.push_back(42);
    std::sort(material_ids.begin(), material_ids.end());
    material_ids.erase(
        std::unique(material_ids.begin(), material_ids.end()),
        material_ids.end());
    return true;
}

std::uint32_t BuildCollisionBvhNode(
    StaticCollisionCache& cache,
    std::uint32_t first,
    std::uint32_t end)
{
    const float infinity = (std::numeric_limits<float>::infinity)();
    CollisionBvhNode node{};
    node.bounds_min = {infinity, infinity, infinity};
    node.bounds_max = {-infinity, -infinity, -infinity};
    Vector3 centroid_min{infinity, infinity, infinity};
    Vector3 centroid_max{-infinity, -infinity, -infinity};

    for (std::uint32_t index = first; index < end; ++index)
    {
        const CollisionTriangle& triangle =
            cache.triangles[cache.indices[index]];
        node.bounds_min = Minimum(node.bounds_min, triangle.bounds_min);
        node.bounds_max = Maximum(node.bounds_max, triangle.bounds_max);
        centroid_min = Minimum(centroid_min, triangle.centroid);
        centroid_max = Maximum(centroid_max, triangle.centroid);
    }

    const std::uint32_t node_index =
        static_cast<std::uint32_t>(cache.nodes.size());
    cache.nodes.push_back(node);

    const std::uint32_t count = end - first;
    const Vector3 centroid_extent = Subtract(centroid_max, centroid_min);
    int split_axis = 0;
    if (centroid_extent.y > centroid_extent.x)
        split_axis = 1;
    if (Component(centroid_extent, 2) > Component(centroid_extent, split_axis))
        split_axis = 2;

    if (count <= kCollisionBvhLeafSize ||
        Component(centroid_extent, split_axis) <= 0.0001f)
    {
        cache.nodes[node_index].first = first;
        cache.nodes[node_index].count = count;
        return node_index;
    }

    const std::uint32_t middle = first + count / 2;
    std::nth_element(
        cache.indices.begin() + first,
        cache.indices.begin() + middle,
        cache.indices.begin() + end,
        [&cache, split_axis](std::uint32_t left, std::uint32_t right)
        {
            return Component(cache.triangles[left].centroid, split_axis) <
                Component(cache.triangles[right].centroid, split_axis);
        });

    const std::uint32_t left = BuildCollisionBvhNode(cache, first, middle);
    const std::uint32_t right = BuildCollisionBvhNode(cache, middle, end);
    cache.nodes[node_index].left = left;
    cache.nodes[node_index].right = right;
    return node_index;
}

bool EnsureStaticCollisionGeometry(
    const TrainerProcess& process,
    std::uintptr_t scene,
    bool exclude_ballistic_pass_through)
{
    const std::uintptr_t bsp = scene + kSceneBspTreeOffset;
    std::uint8_t bsp_valid = 0;
    RemoteVector32 faces{};
    if (!process.ReadMemory(bsp + kBspValidOffset, bsp_valid) ||
        bsp_valid == 0 ||
        !process.ReadMemory(bsp + kBspFacesOffset, faces))
    {
        LogDiagnostic(
            "Geometry: BSP unavailable (scene=%08X bsp=%08X valid=%u "
            "begin=%08X end=%08X capacity=%08X).",
            static_cast<unsigned>(scene), static_cast<unsigned>(bsp),
            bsp_valid,
            static_cast<unsigned>(faces.begin),
            static_cast<unsigned>(faces.end),
            static_cast<unsigned>(faces.capacity));
        g_collision_cache = {};
        return false;
    }

    if (g_collision_cache.initialized &&
        g_collision_cache.scene == scene &&
        g_collision_cache.face_begin == faces.begin &&
        g_collision_cache.face_end == faces.end &&
        g_collision_cache.excludes_ballistic_pass_through ==
            exclude_ballistic_pass_through)
    {
        return g_collision_cache.valid;
    }

    StaticCollisionCache next{};
    next.scene = scene;
    next.face_begin = faces.begin;
    next.face_end = faces.end;
    next.excludes_ballistic_pass_through =
        exclude_ballistic_pass_through;
    next.initialized = true;

    if (faces.begin == 0 || faces.end < faces.begin ||
        faces.capacity < faces.end)
    {
        g_collision_cache = std::move(next);
        return false;
    }

    const std::size_t byte_count =
        static_cast<std::size_t>(faces.end - faces.begin);
    if (byte_count == 0 || byte_count % kBspFaceSize != 0)
    {
        g_collision_cache = std::move(next);
        return false;
    }

    const std::size_t face_count = byte_count / kBspFaceSize;
    if (face_count > kMaximumBspFaces)
    {
        g_collision_cache = std::move(next);
        return false;
    }

    std::vector<std::uint8_t> face_bytes(byte_count);
    if (!process.ReadMemory(faces.begin, face_bytes.data(), face_bytes.size()))
    {
        g_collision_cache = std::move(next);
        return false;
    }

    RemoteVector32 faces_after_read{};
    if (!process.ReadMemory(bsp + kBspFacesOffset, faces_after_read) ||
        faces_after_read.begin != faces.begin ||
        faces_after_read.end != faces.end)
    {
        g_collision_cache = std::move(next);
        return false;
    }

    std::vector<std::uintptr_t> origin_frames;
    origin_frames.reserve(face_count);
    for (std::size_t index = 0; index < face_count; ++index)
    {
        const std::uint32_t frame = ReadU32(
            face_bytes,
            index * kBspFaceSize + kBspFaceOriginFrameOffset);
        if (frame != 0)
            origin_frames.push_back(static_cast<std::uintptr_t>(frame));
    }
    std::sort(origin_frames.begin(), origin_frames.end());
    origin_frames.erase(
        std::unique(origin_frames.begin(), origin_frames.end()),
        origin_frames.end());

    std::vector<std::uint32_t> pass_through_materials;
    const bool pass_through_table_ready =
        exclude_ballistic_pass_through &&
        ReadBallisticPassThroughMaterials(process, pass_through_materials);
    std::vector<CollisionOriginState> origin_states;
    origin_states.reserve(origin_frames.size());
    for (const std::uintptr_t frame : origin_frames)
    {
        CollisionOriginState state{};
        state.frame = frame;
        state.readable =
            process.ReadMemory(frame + kFrameTypeOffset, state.frame_type) &&
            process.ReadMemory(frame + kFrameFlagsOffset, state.frame_flags);
        if (state.readable)
        {
            state.has_material_id = ReadCollisionMaterialId(
                process, frame, state.frame_type, state.material_id);
        }
        origin_states.push_back(state);
    }

    next.triangles.reserve(face_count);
    for (std::size_t index = 0; index < face_count; ++index)
    {
        const std::size_t base = index * kBspFaceSize;
        const std::uintptr_t origin_frame = static_cast<std::uintptr_t>(
            ReadU32(face_bytes, base + kBspFaceOriginFrameOffset));
        const auto origin_it = std::lower_bound(
            origin_states.begin(),
            origin_states.end(),
            origin_frame,
            [](const CollisionOriginState& state, std::uintptr_t frame)
            {
                return state.frame < frame;
            });
        if (origin_it != origin_states.end() &&
            origin_it->frame == origin_frame && origin_it->readable)
        {
            if ((origin_it->frame_flags & kFrameFlagNoCollision) != 0)
                continue;
            if (pass_through_table_ready && origin_it->has_material_id &&
                std::binary_search(
                    pass_through_materials.begin(),
                    pass_through_materials.end(),
                    origin_it->material_id))
            {
                continue;
            }
        }

        CollisionTriangle triangle{};
        triangle.a = ReadVector3(
            face_bytes, base + kBspFaceVertex0Offset);
        triangle.b = ReadVector3(
            face_bytes, base + kBspFaceVertex1Offset);
        triangle.c = ReadVector3(
            face_bytes, base + kBspFaceVertex2Offset);
        if (!IsSane(triangle.a) || !IsSane(triangle.b) ||
            !IsSane(triangle.c))
        {
            continue;
        }

        const Vector3 edge1 = Subtract(triangle.b, triangle.a);
        const Vector3 edge2 = Subtract(triangle.c, triangle.a);
        const Vector3 normal = Cross(edge1, edge2);
        if (Dot(normal, normal) <= 0.00000001f)
            continue;

        triangle.bounds_min = Minimum(
            triangle.a, Minimum(triangle.b, triangle.c));
        triangle.bounds_max = Maximum(
            triangle.a, Maximum(triangle.b, triangle.c));
        triangle.centroid = {
            (triangle.a.x + triangle.b.x + triangle.c.x) / 3.0f,
            (triangle.a.y + triangle.b.y + triangle.c.y) / 3.0f,
            (triangle.a.z + triangle.b.z + triangle.c.z) / 3.0f};
        next.triangles.push_back(triangle);
    }

    if (next.triangles.empty())
    {
        g_collision_cache = std::move(next);
        return false;
    }

    next.indices.resize(next.triangles.size());
    for (std::size_t index = 0; index < next.indices.size(); ++index)
        next.indices[index] = static_cast<std::uint32_t>(index);
    next.nodes.reserve(next.triangles.size() * 2);
    BuildCollisionBvhNode(
        next, 0, static_cast<std::uint32_t>(next.indices.size()));
    next.valid = !next.nodes.empty();
    g_collision_cache = std::move(next);
    return g_collision_cache.valid;
}

bool SegmentIntersectsBounds(
    const Vector3& origin,
    const Vector3& direction,
    const Vector3& bounds_min,
    const Vector3& bounds_max,
    float maximum_t)
{
    float minimum_t = 0.0f;
    for (int axis = 0; axis < 3; ++axis)
    {
        const float start = Component(origin, axis);
        const float delta = Component(direction, axis);
        const float minimum = Component(bounds_min, axis);
        const float maximum = Component(bounds_max, axis);
        if (std::fabs(delta) < 0.0000001f)
        {
            if (start < minimum || start > maximum)
                return false;
            continue;
        }

        const float reciprocal = 1.0f / delta;
        float near_t = (minimum - start) * reciprocal;
        float far_t = (maximum - start) * reciprocal;
        if (near_t > far_t)
            std::swap(near_t, far_t);
        minimum_t = (std::max)(minimum_t, near_t);
        maximum_t = (std::min)(maximum_t, far_t);
        if (minimum_t > maximum_t)
            return false;
    }
    return true;
}

bool SegmentIntersectsTriangle(
    const Vector3& origin,
    const Vector3& direction,
    const CollisionTriangle& triangle,
    float minimum_t,
    float maximum_t)
{
    const Vector3 edge1 = Subtract(triangle.b, triangle.a);
    const Vector3 edge2 = Subtract(triangle.c, triangle.a);
    const Vector3 p = Cross(direction, edge2);
    const float determinant = Dot(edge1, p);
    if (std::fabs(determinant) < 0.0000001f)
        return false;

    const float inverse_determinant = 1.0f / determinant;
    const Vector3 from_a = Subtract(origin, triangle.a);
    const float u = Dot(from_a, p) * inverse_determinant;
    if (u < 0.0f || u > 1.0f)
        return false;

    const Vector3 q = Cross(from_a, edge1);
    const float v = Dot(direction, q) * inverse_determinant;
    if (v < 0.0f || u + v > 1.0f)
        return false;

    const float t = Dot(edge2, q) * inverse_determinant;
    return t > minimum_t && t < maximum_t;
}

bool IsSegmentBlockedByStaticGeometry(
    const Vector3& origin,
    const Vector3& target)
{
    if (!g_collision_cache.valid || g_collision_cache.nodes.empty())
        return true;

    const Vector3 direction = Subtract(target, origin);
    const float length_squared = Dot(direction, direction);
    if (!std::isfinite(length_squared) || length_squared <= 0.000001f)
        return true;

    const float length = std::sqrt(length_squared);
    const float endpoint_inset = (std::min)(kRayEndpointInset / length, 0.25f);
    const float minimum_t = endpoint_inset;
    const float maximum_t = 1.0f - endpoint_inset;

    std::array<std::uint32_t, 128> pending{};
    std::size_t pending_count = 1;
    pending[0] = 0;
    while (pending_count != 0)
    {
        const std::uint32_t node_index = pending[--pending_count];
        if (node_index >= g_collision_cache.nodes.size())
            return true;
        const CollisionBvhNode& node = g_collision_cache.nodes[node_index];
        if (!SegmentIntersectsBounds(
                origin,
                direction,
                node.bounds_min,
                node.bounds_max,
                maximum_t))
        {
            continue;
        }

        if (node.count != 0)
        {
            if (node.first > g_collision_cache.indices.size() ||
                node.count > g_collision_cache.indices.size() - node.first)
            {
                return true;
            }
            for (std::uint32_t offset = 0; offset < node.count; ++offset)
            {
                const std::uint32_t triangle_index =
                    g_collision_cache.indices[node.first + offset];
                if (triangle_index >= g_collision_cache.triangles.size())
                    return true;
                if (SegmentIntersectsTriangle(
                        origin,
                        direction,
                        g_collision_cache.triangles[triangle_index],
                        minimum_t,
                        maximum_t))
                {
                    return true;
                }
            }
            continue;
        }

        if (node.left == UINT32_MAX || node.right == UINT32_MAX ||
            pending_count + 2 > pending.size())
        {
            return true;
        }
        pending[pending_count++] = node.left;
        pending[pending_count++] = node.right;
    }
    return false;
}

bool IsWalkableTriangle(const CollisionTriangle& triangle)
{
    const Vector3 edge1 = Subtract(triangle.b, triangle.a);
    const Vector3 edge2 = Subtract(triangle.c, triangle.a);
    const Vector3 normal = Cross(edge1, edge2);
    const float length_squared = Dot(normal, normal);
    if (!std::isfinite(length_squared) || length_squared <= 0.00000001f)
        return false;
    return std::fabs(normal.y) / std::sqrt(length_squared) >=
        kMinimumWalkableNormalY;
}

bool FindHighestWalkableGround(float x, float z, Vector3& ground)
{
    bool found = false;
    float highest_y = -(std::numeric_limits<float>::infinity)();
    constexpr float epsilon = 0.0001f;
    for (const CollisionTriangle& triangle : g_collision_cache.triangles)
    {
        if (!IsWalkableTriangle(triangle) ||
            x < triangle.bounds_min.x - epsilon ||
            x > triangle.bounds_max.x + epsilon ||
            z < triangle.bounds_min.z - epsilon ||
            z > triangle.bounds_max.z + epsilon)
        {
            continue;
        }

        const float denominator =
            (triangle.b.z - triangle.c.z) *
                (triangle.a.x - triangle.c.x) +
            (triangle.c.x - triangle.b.x) *
                (triangle.a.z - triangle.c.z);
        if (std::fabs(denominator) <= epsilon)
            continue;
        const float u =
            ((triangle.b.z - triangle.c.z) * (x - triangle.c.x) +
             (triangle.c.x - triangle.b.x) * (z - triangle.c.z)) /
            denominator;
        const float v =
            ((triangle.c.z - triangle.a.z) * (x - triangle.c.x) +
             (triangle.a.x - triangle.c.x) * (z - triangle.c.z)) /
            denominator;
        const float w = 1.0f - u - v;
        if (u < -epsilon || v < -epsilon || w < -epsilon)
            continue;

        const float y = u * triangle.a.y + v * triangle.b.y +
            w * triangle.c.y;
        if (std::isfinite(y) && (!found || y > highest_y))
        {
            highest_y = y;
            found = true;
        }
    }
    if (found)
        ground = {x, highest_y, z};
    return found;
}

bool HasClearBallisticAimZone(
    const Vector3& eye,
    const Vector3& feet,
    Vector3 head)
{
    // Keep the sample cloud inside a human-sized hit area. The highest actor
    // descendant is normally the head joint, but this clamp prevents a raised
    // weapon attachment from enlarging the test above the character.
    const float body_height = (std::clamp)(
        head.y - feet.y, 0.75f, 1.90f);
    head.y = feet.y + body_height;

    const Vector3 to_target = Subtract(head, eye);
    const float horizontal_length_squared =
        to_target.x * to_target.x + to_target.z * to_target.z;
    Vector3 right{1.0f, 0.0f, 0.0f};
    if (horizontal_length_squared > 0.000001f)
    {
        const float inverse_length =
            1.0f / std::sqrt(horizontal_length_squared);
        right = {
            -to_target.z * inverse_length,
            0.0f,
            to_target.x * inverse_length};
    }

    std::array<Vector3, 16> targets{};
    std::size_t target_count = 0;
    const auto add_target =
        [&targets, &target_count, &right](
            const Vector3& center,
            float side,
            float vertical)
        {
            if (target_count >= targets.size())
                return;
            targets[target_count++] = {
                center.x + right.x * side,
                center.y + vertical,
                center.z + right.z * side};
        };

    // Nine points cover the face/head silhouette. This is what lets a narrow
    // exposed cheek or top of the head count without extending outside the
    // actual head-sized target.
    add_target(head, 0.0f, 0.0f);
    add_target(head, kHeadSampleRadius, 0.0f);
    add_target(head, -kHeadSampleRadius, 0.0f);
    add_target(head, 0.0f, kHeadSampleVerticalRadius);
    add_target(head, 0.0f, -kHeadSampleVerticalRadius);
    add_target(head, kHeadSampleRadius * 0.68f,
        kHeadSampleVerticalRadius * 0.68f);
    add_target(head, -kHeadSampleRadius * 0.68f,
        kHeadSampleVerticalRadius * 0.68f);
    add_target(head, kHeadSampleRadius * 0.68f,
        -kHeadSampleVerticalRadius * 0.68f);
    add_target(head, -kHeadSampleRadius * 0.68f,
        -kHeadSampleVerticalRadius * 0.68f);

    const Vector3 shoulders{
        feet.x, feet.y + body_height * 0.78f, feet.z};
    add_target(shoulders, 0.0f, 0.0f);
    add_target(shoulders, kShoulderSampleRadius, 0.0f);
    add_target(shoulders, -kShoulderSampleRadius, 0.0f);

    const Vector3 torso{
        feet.x, feet.y + body_height * 0.60f, feet.z};
    add_target(torso, 0.0f, 0.0f);
    add_target(torso, kTorsoSampleRadius, 0.0f);
    add_target(torso, -kTorsoSampleRadius, 0.0f);

    add_target(
        Vector3{feet.x, feet.y + body_height * 0.42f, feet.z},
        0.0f,
        0.0f);

    for (std::size_t index = 0; index < target_count; ++index)
    {
        if (!IsSegmentBlockedByStaticGeometry(eye, targets[index]))
            return true;
    }
    return false;
}

bool ReadActorFrameState(
    const TrainerProcess& process,
    std::uintptr_t root_frame,
    const Vector3& root_position,
    ActorFrameState& state)
{
    state = {};
    if (root_frame == 0)
        return false;

    std::vector<std::uintptr_t> pending_frames{root_frame};
    pending_frames.reserve(kMaximumActorFrames);
    std::vector<std::uintptr_t> visited_frames;
    visited_frames.reserve(kMaximumActorFrames);
    state.highest_nearby_position = root_position;
    state.found_aim_position = IsSane(root_position);

    for (std::size_t cursor = 0;
         cursor < pending_frames.size() &&
         visited_frames.size() < kMaximumActorFrames;
         ++cursor)
    {
        const std::uintptr_t frame = pending_frames[cursor];
        if (frame == 0 ||
            std::find(visited_frames.begin(), visited_frames.end(), frame) !=
                visited_frames.end())
        {
            continue;
        }
        visited_frames.push_back(frame);

        std::uint32_t frame_type = 0;
        if (!process.ReadMemory(frame + kFrameTypeOffset, frame_type))
            continue;

        if (frame_type == kFrameTypeVisual)
        {
            std::uint32_t render_time = 0;
            if (process.ReadMemory(
                    frame + kVisualLastRenderTimeOffset,
                    render_time) &&
                render_time != 0 &&
                (!state.found_visual_render_time ||
                 static_cast<std::int32_t>(
                     render_time - state.latest_render_time) > 0))
            {
                state.latest_render_time = render_time;
                state.found_visual_render_time = true;
            }
        }

        Vector3 frame_position{};
        if (process.ReadMemory(
                frame + kFrameWorldPositionOffset, frame_position) &&
            IsSane(frame_position))
        {
            const float delta_x = frame_position.x - root_position.x;
            const float delta_z = frame_position.z - root_position.z;
            const float delta_y = frame_position.y - root_position.y;
            if (delta_x * delta_x + delta_z * delta_z <=
                    kMaximumAimFrameHorizontalDistance *
                        kMaximumAimFrameHorizontalDistance &&
                delta_y >= kMinimumAimFrameHeight &&
                delta_y <= kMaximumAimFrameHeight &&
                (!state.found_aim_position ||
                 frame_position.y > state.highest_nearby_position.y))
            {
                state.highest_nearby_position = frame_position;
                state.found_aim_position = true;
            }
        }

        RemoteVector32 children{};
        std::uint32_t child_count = 0;
        if (!process.ReadMemory(frame + kFrameChildrenOffset, children) ||
            !GetRemoteVectorCount(
                children, kMaximumFrameChildren, child_count) ||
            child_count == 0)
        {
            continue;
        }

        std::vector<std::uint32_t> child_addresses(child_count);
        if (!process.ReadMemory(
                children.begin,
                child_addresses.data(),
                child_addresses.size() * sizeof(std::uint32_t)))
        {
            continue;
        }

        for (const std::uint32_t child : child_addresses)
        {
            if (child != 0 && pending_frames.size() < kMaximumActorFrames)
                pending_frames.push_back(static_cast<std::uintptr_t>(child));
        }
    }

    return !visited_frames.empty();
}

bool ReadActor(
    const TrainerProcess& process,
    std::uintptr_t actor_address,
    ActorRecord& actor)
{
    if (actor_address == 0)
        return false;

    std::uint32_t type = 0;
    if (!process.ReadMemory(actor_address + kActorTypeOffset, type) ||
        (type != kActorTypePlayer && type != kActorTypeEnemy))
    {
        return false;
    }

    std::uint32_t stay_mode = 0;
    if (!process.ReadMemory(actor_address + kActorStayModeOffset, stay_mode) ||
        stay_mode == kStayModeDead)
    {
        return false;
    }

    std::uintptr_t frame_address = 0;
    if (!ReadRemotePointer(
            process, actor_address + kActorFrameOffset, frame_address))
    {
        return false;
    }

    std::uintptr_t actor_back_reference = 0;
    Vector3 world_position{};
    if (!ReadRemotePointer(
            process,
            frame_address + kFrameActorBackReferenceOffset,
            actor_back_reference) ||
        actor_back_reference != actor_address ||
        !process.ReadMemory(
            frame_address + kFrameWorldPositionOffset, world_position) ||
        !IsSane(world_position))
    {
        return false;
    }

    actor.actor = actor_address;
    actor.frame = frame_address;
    actor.type = type;
    actor.stay_mode = stay_mode;
    actor.position = world_position;
    return true;
}

// The ESP is drawn with GDI on a layered window. Earlier revisions used a
// second Direct3D 9 device; the driver refuses to create one while H&D owns
// the adapter, so the overlay could stay dead for a whole session and only
// appeared after the user left the game and came back. GDI has no adapter
// dependency at all: ticking the checkbox takes effect on the next frame.
// Pure black is the transparency key, so no drawing may use RGB(0,0,0).
struct EspCanvas
{
    HDC dc = nullptr;

    void Line(
        float x0,
        float y0,
        float x1,
        float y1,
        COLORREF color,
        int thickness) const
    {
        const HPEN pen = CreatePen(PS_SOLID, thickness, color);
        if (!pen)
            return;
        const HGDIOBJ previous = SelectObject(dc, pen);
        MoveToEx(dc, std::lround(x0), std::lround(y0), nullptr);
        LineTo(dc, std::lround(x1), std::lround(y1));
        SelectObject(dc, previous);
        DeleteObject(pen);
    }

    void Frame(
        float left,
        float top,
        float right,
        float bottom,
        COLORREF color,
        int thickness) const
    {
        const HPEN pen = CreatePen(PS_SOLID, thickness, color);
        if (!pen)
            return;
        const HGDIOBJ previous_pen = SelectObject(dc, pen);
        const HGDIOBJ previous_brush =
            SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(
            dc,
            std::lround(left),
            std::lround(top),
            std::lround(right),
            std::lround(bottom));
        SelectObject(dc, previous_brush);
        SelectObject(dc, previous_pen);
        DeleteObject(pen);
    }

    void Circle(
        float center_x,
        float center_y,
        float radius,
        COLORREF color,
        int thickness) const
    {
        Frame_Ellipse(center_x, center_y, radius, color, thickness, false);
    }

    void Disc(
        float center_x,
        float center_y,
        float radius,
        COLORREF color) const
    {
        Frame_Ellipse(center_x, center_y, radius, color, 1, true);
    }

    // V110 - fond opaque derriere les fenetres du jeu. Sans lui le texte se
    // confond avec le decor et devient illisible des que la scene est claire.
    void Panel(
        float left,
        float top,
        float right,
        float bottom,
        COLORREF color) const
    {
        RECT area{
            std::lround(left), std::lround(top),
            std::lround(right), std::lround(bottom)};
        const HBRUSH brush = CreateSolidBrush(color);
        if (!brush)
            return;
        FillRect(dc, &area, brush);
        DeleteObject(brush);
    }

    // V110 - ligne de menu.
    //
    // `CenteredText` dessine dans une boite de 48 pixels de haut avec une
    // police de 30. La fenetre des soldats espacait ses lignes de 26 : chaque
    // boite empietait de vingt-deux pixels sur la suivante et les lignes se
    // chevauchaient. C'est la raison pour laquelle les textes etaient illisibles
    // et les lignes tronquees. Ici la boite fait exactement la hauteur d'une
    // ligne, et le texte est aligne a GAUCHE : une liste alignee a gauche se
    // lit, une liste centree se lit mal.
    static constexpr float kMenuLineHeight = 34.0f;

    void MenuLine(
        float left,
        float top,
        float width,
        const wchar_t* text,
        COLORREF color,
        bool centered = false) const
    {
        if (!text || text[0] == L'\0')
            return;
        const HFONT font = CreateFontW(
            -24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Consolas");
        if (!font)
            return;
        const HGDIOBJ previous_font = SelectObject(dc, font);
        const int previous_background = SetBkMode(dc, TRANSPARENT);
        const COLORREF previous_color = SetTextColor(dc, RGB(8, 8, 8));
        RECT bounds{
            std::lround(left), std::lround(top),
            std::lround(left + width),
            std::lround(top + kMenuLineHeight)};
        const UINT format = (centered ? DT_CENTER : DT_LEFT) | DT_VCENTER |
            DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
        for (int offset = -1; offset <= 1; offset += 2)
        {
            RECT outline = bounds;
            OffsetRect(&outline, offset, 0);
            DrawTextW(dc, text, -1, &outline, format);
            outline = bounds;
            OffsetRect(&outline, 0, offset);
            DrawTextW(dc, text, -1, &outline, format);
        }
        SetTextColor(dc, color);
        DrawTextW(dc, text, -1, &bounds, format);
        SetTextColor(dc, previous_color);
        SetBkMode(dc, previous_background);
        SelectObject(dc, previous_font);
        DeleteObject(font);
    }

    void CenteredText(
        float center_x,
        float top,
        float width,
        const wchar_t* text,
        COLORREF color) const
    {
        if (!text || text[0] == L'\0')
            return;

        const HFONT font = CreateFontW(
            -30, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (!font)
            return;

        const HGDIOBJ previous_font = SelectObject(dc, font);
        const int previous_background = SetBkMode(dc, TRANSPARENT);
        const COLORREF previous_color = SetTextColor(dc, RGB(8, 8, 8));
        RECT bounds{
            std::lround(center_x - width * 0.5f), std::lround(top),
            std::lround(center_x + width * 0.5f), std::lround(top + 48.0f)};
        constexpr UINT format = DT_CENTER | DT_VCENTER | DT_SINGLELINE |
            DT_NOPREFIX;
        for (int offset_x = -2; offset_x <= 2; ++offset_x)
        {
            for (int offset_y = -2; offset_y <= 2; ++offset_y)
            {
                if (offset_x == 0 && offset_y == 0)
                    continue;
                RECT outline = bounds;
                OffsetRect(&outline, offset_x, offset_y);
                DrawTextW(dc, text, -1, &outline, format);
            }
        }
        SetTextColor(dc, color);
        DrawTextW(dc, text, -1, &bounds, format);
        SetTextColor(dc, previous_color);
        SetBkMode(dc, previous_background);
        SelectObject(dc, previous_font);
        DeleteObject(font);
    }

private:
    void Frame_Ellipse(
        float center_x,
        float center_y,
        float radius,
        COLORREF color,
        int thickness,
        bool filled) const
    {
        const HPEN pen = CreatePen(PS_SOLID, thickness, color);
        if (!pen)
            return;
        const HBRUSH brush = filled ? CreateSolidBrush(color) : nullptr;
        const HGDIOBJ previous_pen = SelectObject(dc, pen);
        const HGDIOBJ previous_brush = SelectObject(
            dc, filled ? static_cast<HGDIOBJ>(brush)
                       : GetStockObject(NULL_BRUSH));
        Ellipse(
            dc,
            std::lround(center_x - radius),
            std::lround(center_y - radius),
            std::lround(center_x + radius),
            std::lround(center_y + radius));
        SelectObject(dc, previous_brush);
        SelectObject(dc, previous_pen);
        if (brush)
            DeleteObject(brush);
        DeleteObject(pen);
    }
};

// The colour-key overlay cannot blend, so the requested alpha is folded into
// the colour itself. The floor keeps a dimmed line from turning into the
// transparent key.
COLORREF DimColor(int red, int green, int blue, float alpha)
{
    const auto scale = [alpha](int channel)
    {
        const int value = static_cast<int>(
            static_cast<float>(channel) * (std::clamp)(alpha, 0.20f, 1.0f));
        return static_cast<int>((std::clamp)(value, 12, 255));
    };
    return RGB(scale(red), scale(green), scale(blue));
}

void DrawSnaplines(
    const EspCanvas& canvas,
    const RadarSnapshot* snapshot,
    const RadarRenderSettings& settings,
    float width,
    float height)
{
    constexpr COLORREF outline_color = RGB(8, 8, 8);

    if (settings.transient_notification)
    {
        canvas.CenteredText(
            width * 0.5f,
            height * 0.16f,
            (std::min)(width - 32.0f, 760.0f),
            settings.transient_notification,
            std::wcscmp(settings.transient_notification,
                         L"Maintenant invisible") == 0
                ? RGB(70, 235, 125)
                : RGB(255, 220, 85));
    }

    if (settings.vehicle_menu_entries != nullptr &&
        settings.vehicle_menu_count != 0)
    {
        // V110 - la fenetre est dessinee dans un panneau opaque, ligne par
        // ligne, avec un interligne EGAL a la hauteur de boite du texte. La
        // version precedente espacait ses lignes de 26 pixels alors que chaque
        // ligne etait dessinee dans une boite de 48 : elles se chevauchaient,
        // et c'est pour cela que le joueur ne lisait pas ses lignes en entier.
        const float line_height = EspCanvas::kMenuLineHeight;
        constexpr std::uint32_t kVisibleLines = 14U;

        std::uint32_t first = 0;
        if (settings.vehicle_menu_count > kVisibleLines)
        {
            const std::int32_t selection =
                settings.vehicle_menu_selection < 0
                    ? 0
                    : settings.vehicle_menu_selection;
            const std::uint32_t last_start =
                settings.vehicle_menu_count - kVisibleLines;
            const std::uint32_t centered =
                static_cast<std::uint32_t>(selection) > kVisibleLines / 2U
                    ? static_cast<std::uint32_t>(selection) - kVisibleLines / 2U
                    : 0U;
            first = (std::min)(centered, last_start);
        }
        const std::uint32_t shown = (std::min)(
            kVisibleLines, settings.vehicle_menu_count - first);

        // Titre, lignes, et pied : chacun occupe exactement une ligne.
        const float box_width = (std::min)(width - 80.0f, 900.0f);
        const float box_left = (width - box_width) * 0.5f;
        const float rows = static_cast<float>(shown) + 3.0f;
        const float box_top = (height - rows * line_height) * 0.5f;
        const float padding = 18.0f;

        canvas.Panel(
            box_left - padding, box_top - padding,
            box_left + box_width + padding,
            box_top + rows * line_height + padding,
            RGB(12, 14, 18));

        float y = box_top;
        canvas.MenuLine(
            box_left, y, box_width,
            settings.menu_title != nullptr
                ? settings.menu_title
                : L"VEHICULES DE LA MISSION",
            RGB(255, 220, 85), true);
        y += line_height * 1.5f;

        for (std::uint32_t drawn = 0; drawn < shown; ++drawn)
        {
            const std::uint32_t index = first + drawn;
            const bool selected =
                static_cast<std::int32_t>(index) ==
                settings.vehicle_menu_selection;
            // Un marqueur en debut de ligne, pour que la ligne choisie se voie
            // meme si la couleur passe mal sur le decor.
            canvas.MenuLine(
                box_left, y, 40.0f, selected ? L"->" : L"",
                RGB(70, 235, 125));
            canvas.MenuLine(
                box_left + 44.0f, y, box_width - 44.0f,
                settings.vehicle_menu_entries[index],
                selected ? RGB(70, 235, 125) : RGB(225, 225, 225));
            y += line_height;
        }
        if (settings.menu_footer != nullptr)
        {
            canvas.MenuLine(
                box_left, y + line_height * 0.4f, box_width,
                settings.menu_footer, RGB(255, 220, 85), true);
        }
    }

    if (settings.show_bullet_track_circle)
    {
        const float radius = (std::clamp)(
            settings.bullet_track_radius_pixels, 40.0f, 500.0f);
        const float center_x = width * 0.5f;
        const float center_y = height * 0.5f;
        canvas.Circle(center_x, center_y, radius, outline_color, 4);
        canvas.Circle(
            center_x, center_y, radius, RGB(255, 210, 55), 2);
        canvas.Disc(center_x, center_y, 2.0f, RGB(255, 225, 90));
    }

    // Keep only the targeting circle when both ESP categories and the V2
    // cosmetic tracer are off.
    if (!settings.show_enemy_esp && !settings.show_ally_esp &&
        !settings.show_bullet_track_v2_tracer)
        return;

    if (!snapshot || !snapshot->entity_array_valid ||
        !snapshot->view_projection_valid)
    {
        return;
    }

    if (settings.show_bullet_track_v2_tracer &&
        settings.bullet_track_v2_tracer_target_actor != 0)
    {
        const std::size_t entity_count = (std::min<std::size_t>)(
            snapshot->entity_array.count, kMaximumRadarEntities);
        for (std::size_t index = 0; index < entity_count; ++index)
        {
            const RadarEntity& entity = snapshot->entity_array.entities[index];
            if (entity.active == 0 ||
                entity.actor_address !=
                    settings.bullet_track_v2_tracer_target_actor ||
                !IsSane(entity.head_position))
            {
                continue;
            }

            Vector2 head_screen{};
            if (WorldToScreen(
                    entity.head_position, head_screen,
                    snapshot->view_projection, width, height))
            {
                // This is a local, one-frame visual flash, not a game
                // projectile. Its two strokes keep the target readable over
                // bright maps while V2 retains an immediate native impact.
                const float origin_x = width * 0.5f;
                const float origin_y = height * 0.5f;
                canvas.Line(
                    origin_x, origin_y, head_screen.x, head_screen.y,
                    RGB(55, 10, 0), 6);
                canvas.Line(
                    origin_x, origin_y, head_screen.x, head_screen.y,
                    RGB(255, 170, 35), 2);
                canvas.Disc(head_screen.x, head_screen.y, 3.0f,
                            RGB(255, 235, 125));
            }
            break;
        }
    }

    const float origin_x = width * 0.5f;
    const float origin_y = 1.0f;
    const float alpha = settings.esp_alpha;

    const std::size_t count = (std::min<std::size_t>)(
        snapshot->entity_array.count, kMaximumRadarEntities);
    for (std::size_t index = 0; index < count; ++index)
    {
        const RadarEntity& entity = snapshot->entity_array.entities[index];
        if (entity.active == 0 || !IsSane(entity.position))
            continue;

        const bool enemy = entity.team == EntityTeam::Enemy;
        const bool ally = entity.team == EntityTeam::Ally;
        if ((!enemy && !ally) ||
            (enemy && !settings.show_enemy_esp) ||
            (ally && !settings.show_ally_esp))
        {
            continue;
        }

        const Vector3 feet_position = entity.position;
        Vector3 head_position = feet_position;
        head_position.y += kApproximateHumanHeight;

        Vector2 head_screen{};
        if (!WorldToScreen(
                head_position,
                head_screen,
                snapshot->view_projection,
                width,
                height))
        {
            continue;
        }

        const COLORREF esp_color = enemy
            ? (entity.directly_visible != 0
                ? DimColor(55, 230, 90, alpha)
                : DimColor(255, 55, 50, alpha))
            : DimColor(55, 145, 255, alpha);

        // A snapline depends only on the head projection. A distant or
        // partially clipped box must never suppress its line.
        if (head_screen.x >= 0.0f && head_screen.x <= width &&
            head_screen.y >= 0.0f && head_screen.y <= height)
        {
            canvas.Line(
                origin_x, origin_y, head_screen.x, head_screen.y,
                outline_color, 4);
            canvas.Line(
                origin_x, origin_y, head_screen.x, head_screen.y,
                esp_color, 2);
        }

        Vector2 feet_screen{};
        if (!WorldToScreen(
                feet_position,
                feet_screen,
                snapshot->view_projection,
                width,
                height))
        {
            continue;
        }

        const float projected_height = std::fabs(feet_screen.y - head_screen.y);
        if (!std::isfinite(projected_height) ||
            projected_height > height * 2.0f)
        {
            continue;
        }

        // Preserve a visible two-pixel box for very distant actors instead of
        // dropping the actor entirely as the previous six-pixel gate did.
        const float box_height =
            (std::max)(projected_height, kMinimumBoxHeight);
        const float box_center_y = (head_screen.y + feet_screen.y) * 0.5f;
        const float box_top = box_center_y - box_height * 0.5f;
        const float box_bottom = box_center_y + box_height * 0.5f;
        const float box_center_x = (head_screen.x + feet_screen.x) * 0.5f;
        const float half_box_width =
            box_height * kBoxWidthToHeightRatio * 0.5f;
        const float box_left = box_center_x - half_box_width;
        const float box_right = box_center_x + half_box_width;

        if (box_right < 0.0f || box_left > width ||
            box_bottom < 0.0f || box_top > height)
        {
            continue;
        }

        canvas.Frame(
            box_left, box_top, box_right, box_bottom, outline_color, 4);
        canvas.Frame(box_left, box_top, box_right, box_bottom, esp_color, 2);
    }
}

LRESULT CALLBACK OverlayWindowProcedure(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param)
{
    if (message == WM_NCHITTEST)
        return HTTRANSPARENT;
    if (message == WM_ERASEBKGND)
        return 1;
    return DefWindowProcW(window, message, w_param, l_param);
}

class SnaplineOverlay final
{
public:
    ~SnaplineOverlay()
    {
        Shutdown();
    }

    void Render(
        DWORD game_process_id,
        const RadarSnapshot* snapshot,
        const RadarRenderSettings& settings)
    {
    if (!settings.show_enemy_esp && !settings.show_ally_esp &&
        !settings.show_bullet_track_circle &&
        !settings.show_bullet_track_v2_tracer &&
        !settings.transient_notification &&
        settings.vehicle_menu_count == 0)
        {
            ReportState("off: no ESP checkbox is enabled");
            Hide();
            return;
        }

        const HWND foreground_window = GetForegroundWindow();
        DWORD foreground_process_id = 0;
        if (game_process_id == 0 || !IsWindow(foreground_window))
        {
            ReportState("hidden: no game process or no foreground window");
            Hide();
            return;
        }
        GetWindowThreadProcessId(foreground_window, &foreground_process_id);
        const bool foreground_is_game =
            foreground_process_id == game_process_id;
        const bool foreground_is_trainer =
            foreground_process_id == GetCurrentProcessId();
        if ((!foreground_is_game && !foreground_is_trainer) ||
            IsIconic(foreground_window))
        {
            ReportState("hidden: another application owns the foreground");
            Hide();
            return;
        }

        // H&D can expose several top-level and child HWNDs. In particular, its
        // cached "main" handle can temporarily refer to a tiny off-screen
        // status window. Projecting with that 158x26-style client is what
        // compresses otherwise valid ESP coordinates into the upper-left.
        // Resolve the largest visible client owned by hde.exe every frame.
        GameClientBounds client{};
        if (!FindGameClientBounds(game_process_id, client))
        {
            ReportState("hidden: no readable game client rectangle");
            Hide();
            return;
        }

        if (!EnsureWindow())
            return;

        const int width = client.width;
        const int height = client.height;

        // Always insert at HWND_TOPMOST. Passing the trainer window here would
        // place the overlay behind a non-topmost window, which strips
        // WS_EX_TOPMOST and leaves the ESP under the game until the next
        // foreground change.
        SetWindowPos(
            window_,
            HWND_TOPMOST,
            client.top_left.x,
            client.top_left.y,
            width,
            height,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);

        if (!EnsureBackbuffer(width, height))
        {
            ReportState("failed: overlay backbuffer allocation");
            return;
        }

        PatBlt(memory_dc_, 0, 0, width, height, BLACKNESS);
        const EspCanvas canvas{memory_dc_};
        DrawSnaplines(
            canvas,
            snapshot,
            settings,
            static_cast<float>(width),
            static_cast<float>(height));

        const HDC window_dc = GetDC(window_);
        if (!window_dc)
        {
            ReportState("failed: overlay window device context");
            return;
        }
        BitBlt(window_dc, 0, 0, width, height, memory_dc_, 0, 0, SRCCOPY);
        ReleaseDC(window_, window_dc);

        ReportState(
            foreground_is_trainer
                ? "visible: rendering over the game, trainer in foreground"
                : "visible: rendering over the game");
    }

private:
    bool EnsureWindow()
    {
        if (window_)
            return true;

        instance_ = GetModuleHandleW(nullptr);
        if (!class_registered_)
        {
            WNDCLASSEXW window_class{};
            window_class.cbSize = sizeof(window_class);
            window_class.lpfnWndProc = OverlayWindowProcedure;
            window_class.hInstance = instance_;
            window_class.lpszClassName = kOverlayWindowClass;

            if (!RegisterClassExW(&window_class) &&
                GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            {
                ReportState("failed: overlay window class registration");
                return false;
            }
            class_registered_ = true;
        }

        constexpr DWORD extended_style =
            WS_EX_TOPMOST |
            WS_EX_TOOLWINDOW |
            WS_EX_TRANSPARENT |
            WS_EX_LAYERED |
            WS_EX_NOACTIVATE;
        window_ = CreateWindowExW(
            extended_style,
            kOverlayWindowClass,
            L"H&D 3D ESP Snaplines",
            WS_POPUP,
            0,
            0,
            64,
            64,
            nullptr,
            nullptr,
            instance_,
            nullptr);
        if (!window_)
        {
            ReportState("failed: overlay window creation");
            return false;
        }

        SetLayeredWindowAttributes(window_, RGB(0, 0, 0), 255, LWA_COLORKEY);
        ReportState("ready: GDI overlay window created");
        return true;
    }

    bool EnsureBackbuffer(int width, int height)
    {
        if (memory_dc_ && width == bitmap_width_ && height == bitmap_height_)
            return true;

        ReleaseBackbuffer();
        const HDC screen_dc = GetDC(nullptr);
        if (!screen_dc)
            return false;
        memory_dc_ = CreateCompatibleDC(screen_dc);
        ReleaseDC(nullptr, screen_dc);
        if (!memory_dc_)
            return false;

        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = (std::max)(width, 1);
        info.bmiHeader.biHeight = -(std::max)(height, 1);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        bitmap_ = CreateDIBSection(
            memory_dc_, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!bitmap_)
        {
            ReleaseBackbuffer();
            return false;
        }
        previous_bitmap_ = SelectObject(memory_dc_, bitmap_);
        bitmap_width_ = width;
        bitmap_height_ = height;
        return true;
    }

    void ReleaseBackbuffer()
    {
        if (memory_dc_ && previous_bitmap_)
        {
            SelectObject(memory_dc_, previous_bitmap_);
            previous_bitmap_ = nullptr;
        }
        if (bitmap_)
        {
            DeleteObject(bitmap_);
            bitmap_ = nullptr;
        }
        if (memory_dc_)
        {
            DeleteDC(memory_dc_);
            memory_dc_ = nullptr;
        }
        bitmap_width_ = 0;
        bitmap_height_ = 0;
    }

    void Hide() const
    {
        if (IsWindow(window_))
            ShowWindow(window_, SW_HIDE);
    }

    void Shutdown()
    {
        ReleaseBackbuffer();
        if (IsWindow(window_))
            DestroyWindow(window_);
        window_ = nullptr;

        if (class_registered_ && instance_)
            UnregisterClassW(kOverlayWindowClass, instance_);
        class_registered_ = false;
    }

    // One journal line per state change only. The overlay renders every frame
    // and must never flood the test log.
    void ReportState(const char* state)
    {
        if (last_state_ && std::strcmp(last_state_, state) == 0)
            return;
        last_state_ = state;
        LogDiagnostic("[TEST ESP] %s.", state);
    }

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HDC memory_dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_bitmap_ = nullptr;
    int bitmap_width_ = 0;
    int bitmap_height_ = 0;
    bool class_registered_ = false;
    const char* last_state_ = nullptr;
};

SnaplineOverlay& GetOverlay()
{
    static SnaplineOverlay overlay;
    return overlay;
}
}

bool SyncNativeMapCursorToPlayer(
    const TrainerProcess& game_process,
    const RadarSnapshot& snapshot)
{
    std::uintptr_t map_manager = 0;
    std::uintptr_t map_scene = 0;
    Matrix4x4 map_view_projection{};
    if (!game_process.IsConnected() ||
        snapshot.entity_list_object_address < 0x10000U ||
        snapshot.entity_list_object_address > 0x7FFF'FFFFU ||
        !game_process.ReadMemory(
            snapshot.entity_list_object_address +
                kGameMissionMapManagerOffset,
            map_manager) ||
        map_manager < 0x10000U || map_manager > 0x7FFF'FFFFU ||
        !game_process.ReadMemory(
            map_manager + kMissionSceneOffset, map_scene) ||
        map_scene < 0x10000U || map_scene > 0x7FFF'FFFFU ||
        !game_process.ReadMemory(
            map_scene + kSceneViewProjectionOffset,
            map_view_projection) ||
        !IsSane(map_view_projection) ||
        snapshot.game_client_width <= 0.0f ||
        snapshot.game_client_height <= 0.0f)
    {
        LogDiagnostic(
            "Teleport: native-centre sync skipped (map manager unavailable).");
        return false;
    }

    float ignored_x = 0.0f;
    float ignored_y = 0.0f;
    std::uint32_t cursor_block_offset = 0;
    if (!ReadNativeMapCursor(
            game_process,
            map_manager,
            snapshot.game_client_width,
            snapshot.game_client_height,
            ignored_x,
            ignored_y,
            &cursor_block_offset))
    {
        LogDiagnostic(
            "Teleport: cursor sync skipped (block not found in %08X).",
            static_cast<unsigned>(map_manager));
        return false;
    }

    // The map duplicates the controlled actor model at its exact world
    // position. Project that model origin through the native map camera.
    Vector2 player_screen{};
    if (!WorldToScreen(
            snapshot.player.position,
            player_screen,
            map_view_projection,
            snapshot.game_client_width,
            snapshot.game_client_height))
    {
        LogDiagnostic(
            "Teleport: controlled-player map projection failed.");
        return false;
    }

    // Map_man.cpp renders its arrow at:
    //   mouse_y / (screen_aspect_ratio * 1.33333333f)
    // where screen_aspect_ratio is height/width. Therefore mouse_y and the
    // visible/raycast screen Y are different on widescreen resolutions. Store
    // the inverse-converted value so the visible arrow lands on the projected
    // actor instead of below it.
    const float vertical_cursor_factor =
        (snapshot.game_client_height / snapshot.game_client_width) *
        (4.0f / 3.0f);
    if (!std::isfinite(vertical_cursor_factor) ||
        vertical_cursor_factor <= 0.01f)
    {
        LogDiagnostic("Teleport: invalid native cursor aspect factor.");
        return false;
    }
    const std::int32_t mouse_x = static_cast<std::int32_t>(
        std::lround(player_screen.x));
    const std::int32_t mouse_y = static_cast<std::int32_t>(
        std::lround(player_screen.y * vertical_cursor_factor));
    if (mouse_x < 0 || mouse_y < 0 ||
        mouse_x > static_cast<std::int32_t>(snapshot.game_client_width) ||
        mouse_y > static_cast<std::int32_t>(snapshot.game_client_height))
    {
        LogDiagnostic(
            "Teleport: converted player cursor outside client "
            "(projected=%.1f,%.1f internal=%d,%d factor=%.4f).",
            player_screen.x, player_screen.y,
            mouse_x, mouse_y, vertical_cursor_factor);
        return false;
    }
    const bool written =
        game_process.WriteMemory(
            map_manager + cursor_block_offset + kMapCursorMouseXOffset,
            mouse_x) &&
        game_process.WriteMemory(
            map_manager + cursor_block_offset + kMapCursorMouseYOffset,
            mouse_y);

    float internal_after_x = 0.0f;
    float internal_after_y = 0.0f;
    std::uint32_t after_block = 0;
    const bool readback = ReadNativeMapCursor(
        game_process, map_manager,
        snapshot.game_client_width, snapshot.game_client_height,
        internal_after_x, internal_after_y, &after_block);

    LogDiagnostic(
        "[TEST TELEPORT] PLAYER_NATIVE_CENTER manager=%08X actor=%08X "
        "before=(%.0f,%.0f) player_screen=(%.3f,%.3f) "
        "aspect_factor=%.6f internal=(%d,%d) visible_after=(%.3f,%.3f) "
        "manager+%X write=%d readback=%d "
        "internal_after=(%.0f,%.0f) after_block=%X Win32_untouched=1 "
        "world=(%.3f,%.3f,%.3f).",
        static_cast<unsigned>(map_manager),
        static_cast<unsigned>(snapshot.player_object_address),
        ignored_x, ignored_y,
        player_screen.x, player_screen.y, vertical_cursor_factor,
        mouse_x, mouse_y,
        static_cast<float>(mouse_x),
        static_cast<float>(mouse_y) / vertical_cursor_factor,
        cursor_block_offset,
        written ? 1 : 0, readback ? 1 : 0,
        internal_after_x, internal_after_y, after_block,
        snapshot.player.position.x,
        snapshot.player.position.y,
        snapshot.player.position.z);
    return written && readback &&
        static_cast<std::int32_t>(std::lround(internal_after_x)) == mouse_x &&
        static_cast<std::int32_t>(std::lround(internal_after_y)) == mouse_y;
}

// File-local: the role is published to the rest of the trainer through
// RadarSnapshot::network_role rather than as a separate entry point.
//
// This only ever reports Host or Client when the engine's own variables say
// so, which happens for sessions started from the console or the command
// line. A session started from the multiplayer menu leaves them at 0 and is
// reported as Offline - proven by a LAN test where both bytes stayed 0
// throughout. Nothing depends on getting a positive answer here: the filter
// is whole-squad in every mode, so it covers connected players regardless.
// Owning the UDP multiplayer port is deliberately NOT used as a signal:
// hde.exe binds 2302 on every interface at startup, in single player too.
static NetworkRole ReadNetworkRole(
    const TrainerProcess& game_process,
    RadarSnapshot& snapshot)
{
    std::uint8_t net_host = 0;
    std::uint8_t net_join = 0;
    RemoteModuleInfo module{};
    if (game_process.GetMainModuleInfo(module) &&
        module.base_address != 0 &&
        kNetJoinRva + 1U <= module.image_size)
    {
        if (!game_process.ReadMemory(
                module.base_address + kNetHostRva, net_host))
        {
            net_host = 0;
        }
        if (!game_process.ReadMemory(
                module.base_address + kNetJoinRva, net_join))
        {
            net_join = 0;
        }
    }

    snapshot.net_host_byte = net_host;
    snapshot.net_join_byte = net_join;

    if (net_host != 0)
        return NetworkRole::Host;
    if (net_join != 0)
        return NetworkRole::Client;
    return NetworkRole::Offline;
}

const char* RadarReadStateName(RadarReadState value)
{
    switch (value)
    {
    case RadarReadState::Disconnected: return "disconnected";
    case RadarReadState::RootUnavailable: return "root-unavailable";
    case RadarReadState::MissionUnavailable: return "mission-unavailable";
    case RadarReadState::SceneUnavailable: return "scene-unavailable";
    case RadarReadState::CameraUnavailable: return "camera-unavailable";
    case RadarReadState::MatrixUnavailable: return "matrix-unavailable";
    case RadarReadState::InvalidActorVector: return "invalid-actor-vector";
    case RadarReadState::ActorArrayUnreadable: return "actor-array-unreadable";
    case RadarReadState::LocalPlayerUnavailable:
        return "local-player-unavailable";
    case RadarReadState::Ready: return "ready";
    default: return "?";
    }
}

// Names the exact step that failed. Without it a failed read is indis-
// tinguishable from any other, and a snapshot that stayed unavailable for
// more than a minute in a network session could not be explained at all.
//
// A transient camera/matrix hand-off may however alternate READY and
// MATRIX_UNAVAILABLE every frame.  Logging each flicker is much worse than
// losing that noise: LogDiagnostic opens, writes and closes the file for each
// line.  Require a state to remain stable briefly before reporting it.
void LogReadStateTransition()
{
    constexpr DWORD kStableReadStateBeforeLoggingMs = 750U;
    static bool initialized = false;
    static RadarReadState observed = RadarReadState::Disconnected;
    static RadarReadState reported = RadarReadState::Disconnected;
    static DWORD observed_since = 0;

    const DWORD now = GetTickCount();
    if (!initialized)
    {
        initialized = true;
        observed = g_runtime.read_state;
        reported = g_runtime.read_state;
        observed_since = now;
        LogDiagnostic(
            "Radar read state -> %s.", RadarReadStateName(reported));
        return;
    }
    if (observed != g_runtime.read_state)
    {
        observed = g_runtime.read_state;
        observed_since = now;
        return;
    }
    if (reported == observed ||
        static_cast<DWORD>(now - observed_since) <
            kStableReadStateBeforeLoggingMs)
    {
        return;
    }
    reported = observed;
    LogDiagnostic(
        "Radar read state -> %s.", RadarReadStateName(reported));
}

bool ReadRadarSnapshotImpl(
    const TrainerProcess& game_process,
    RadarSnapshot& snapshot)
{
    // A restarted game reuses addresses freely, so the remembered controlled
    // player must not survive a change of process.
    const DWORD current_process_id = game_process.ProcessId();
    if (g_runtime.game_process_id != current_process_id)
    {
        g_runtime.last_local_player = 0;
        g_runtime.has_last_local_player_position = false;
    }
    g_runtime.game_process_id = current_process_id;

    if (!game_process.IsConnected())
    {
        g_runtime.read_state = RadarReadState::Disconnected;
        return false;
    }

    const std::uintptr_t tick_class_root =
        game_process.GetHDTickClassPointerAddress();
    if (tick_class_root == 0)
    {
        g_runtime.read_state = RadarReadState::RootUnavailable;
        return false;
    }

    RadarSnapshot next{};
    next.network_role = ReadNetworkRole(game_process, next);
    RECT game_client{};
    if (GetClientRect(game_process.WindowHandle(), &game_client))
    {
        next.game_client_width = static_cast<float>(
            game_client.right - game_client.left);
        next.game_client_height = static_cast<float>(
            game_client.bottom - game_client.top);
    }
    if (!ReadRemotePointer(
            game_process, tick_class_root, next.entity_list_object_address))
    {
        g_runtime.read_state = RadarReadState::MissionUnavailable;
        return false;
    }

    if (!ReadRemotePointer(
            game_process,
            next.entity_list_object_address + kMissionSceneOffset,
            next.scene_object_address))
    {
        g_runtime.read_state = RadarReadState::SceneUnavailable;
        return false;
    }

    std::uint32_t camera_frame_type = 0;
    if (!ReadRemotePointer(
            game_process,
            next.scene_object_address + kSceneActiveCameraOffset,
            next.camera_object_address) ||
        !game_process.ReadMemory(
            next.camera_object_address + kFrameTypeOffset,
            camera_frame_type) ||
        camera_frame_type != kFrameTypeCamera)
    {
        g_runtime.read_state = RadarReadState::CameraUnavailable;
        return false;
    }

    if (!game_process.ReadMemory(
            next.scene_object_address + kSceneViewProjectionOffset,
            next.view_projection) ||
        !IsSane(next.view_projection))
    {
        g_runtime.read_state = RadarReadState::MatrixUnavailable;
        return false;
    }
    next.view_projection_valid = true;

    RemoteVector32 actor_vector{};
    std::uint32_t actor_count = 0;
    if (!game_process.ReadMemory(
            next.entity_list_object_address + kMissionActorsOffset,
            actor_vector) ||
        !GetActorCount(actor_vector, actor_count))
    {
        g_runtime.read_state = RadarReadState::InvalidActorVector;
        return false;
    }

    next.source_entity_count = actor_count;
    next.entity_array_address = actor_vector.begin;
    std::vector<std::uint32_t> actor_addresses(actor_count);
    if (actor_count != 0 &&
        !game_process.ReadMemory(
            next.entity_array_address,
            actor_addresses.data(),
            actor_addresses.size() * sizeof(std::uint32_t)))
    {
        g_runtime.read_state = RadarReadState::ActorArrayUnreadable;
        return false;
    }

    std::vector<ActorRecord> actors;
    actors.reserve(actor_count);
    ActorRecord local_player{};

    // Every actor carrying the controlled flag on this pass, in list order,
    // plus the previous choice if it is still alive. Taking the last match
    // unconditionally is what used to make the protected player flip between
    // squad members several times a second.
    std::vector<ActorRecord> active_players;
    ActorRecord previous_player{};

    for (const std::uint32_t address : actor_addresses)
    {
        ActorRecord actor{};
        if (!ReadActor(
                game_process, static_cast<std::uintptr_t>(address), actor))
        {
            continue;
        }

        ++next.readable_entity_count;
        actors.push_back(actor);

        if (actor.type == kActorTypePlayer)
        {
            ++next.player_actor_count;
            if (actor.actor == g_runtime.last_local_player)
                previous_player = actor;
            std::uint8_t is_active_player = 0;
            if (game_process.ReadMemory(
                    actor.actor + kActorActivePlayerOffset,
                    is_active_player) &&
                is_active_player == 1)
            {
                active_players.push_back(actor);
            }
        }
    }

    next.active_player_count =
        static_cast<std::uint32_t>(active_players.size());

    // Nearest player actor to where the local player last stood. Used only
    // when identity is otherwise lost; a respawn reallocates the actor, so
    // the address no longer matches anything, but the position does.
    const auto nearest_to_last_position =
        [&](const std::vector<ActorRecord>& candidates) -> ActorRecord
    {
        ActorRecord best{};
        if (!g_runtime.has_last_local_player_position || candidates.empty())
            return best;
        float best_distance = std::numeric_limits<float>::max();
        for (const ActorRecord& candidate : candidates)
        {
            const float dx =
                candidate.position.x - g_runtime.last_local_player_position.x;
            const float dy =
                candidate.position.y - g_runtime.last_local_player_position.y;
            const float dz =
                candidate.position.z - g_runtime.last_local_player_position.z;
            const float distance = dx * dx + dy * dy + dz * dz;
            if (distance < best_distance)
            {
                best_distance = distance;
                best = candidate;
            }
        }
        return best;
    };

    std::vector<ActorRecord> player_actors;
    for (const ActorRecord& actor : actors)
    {
        if (actor.type == kActorTypePlayer)
            player_actors.push_back(actor);
    }

    if (active_players.size() == 1)
    {
        // Unambiguous: exactly one player actor claims to be controlled.
        local_player = active_players.front();
    }
    else if (previous_player.actor != 0)
    {
        // Zero matches (the game clears the flag on every player actor at
        // times) or several matches. Either way the previous choice is still
        // a live player actor, so keep it rather than re-electing one every
        // frame.
        local_player = previous_player;
    }
    else if (!active_players.empty())
    {
        // Several claimants and no history: prefer the one where the local
        // player was, otherwise the first, never whichever came last.
        const ActorRecord nearest = nearest_to_last_position(active_players);
        local_player =
            nearest.actor != 0 ? nearest : active_players.front();
    }
    else if (player_actors.size() == 1)
    {
        // Only one player actor exists at all, so there is nothing to
        // confuse it with.
        local_player = player_actors.front();
    }
    else
    {
        // No actor carries the flag and the remembered address is gone. This
        // used to fail the whole read, and the snapshot then stayed dead for
        // the rest of the mission - over a minute in the logged network
        // session, with every gameplay feature blind behind it. Fall back to
        // the player actor standing where the local player last was.
        local_player = nearest_to_last_position(player_actors);
    }

    if (local_player.actor == 0)
    {
        static std::uint64_t next_census_log = 0;
        const std::uint64_t now_ms =
            static_cast<std::uint64_t>(GetTickCount64());
        if (now_ms >= next_census_log)
        {
            next_census_log = now_ms + 5000ULL;
            LogDiagnostic(
                "Radar: no local player. actors=%u players=%u flagged=%u "
                "remembered=%08X have_position=%u.",
                static_cast<unsigned>(actors.size()),
                static_cast<unsigned>(player_actors.size()),
                static_cast<unsigned>(active_players.size()),
                static_cast<unsigned>(g_runtime.last_local_player),
                g_runtime.has_last_local_player_position ? 1U : 0U);
        }
        g_runtime.read_state = RadarReadState::LocalPlayerUnavailable;
        return false;
    }

    g_runtime.last_local_player = local_player.actor;
    g_runtime.last_local_player_position = local_player.position;
    g_runtime.has_last_local_player_position = true;

    next.player_object_address = local_player.actor;
    next.player_frame_address = local_player.frame;
    next.player.position = local_player.position;
    Vector3 direction{};
    if (game_process.ReadMemory(
            local_player.frame + kFrameWorldDirectionOffset, direction) &&
        IsFinite(direction) &&
        direction.x * direction.x + direction.z * direction.z > 0.000001f)
    {
        next.player.heading_radians = std::atan2(direction.x, direction.z);
    }

    ActorFrameState local_frame_state{};
    ReadActorFrameState(
        game_process,
        local_player.frame,
        local_player.position,
        local_frame_state);
    Vector3 eye_position = local_frame_state.found_aim_position
        ? local_frame_state.highest_nearby_position
        : Vector3{
            local_player.position.x,
            local_player.position.y + kFallbackEyeHeight,
            local_player.position.z};
    eye_position.y -= kEyeInset;
    next.player_aim_position = eye_position;

    const bool collision_geometry_ready = EnsureStaticCollisionGeometry(
        game_process, next.scene_object_address, true);

    for (const ActorRecord& actor : actors)
    {
        if (actor.actor == local_player.actor)
            continue;
        if (next.entity_array.count >= kMaximumRadarEntities)
            break;

        RadarEntity& entity =
            next.entity_array.entities[next.entity_array.count++];
        entity.position = actor.position;
        entity.head_position = {
            actor.position.x,
            actor.position.y + kFallbackEyeHeight - kHeadInset,
            actor.position.z};
        entity.actor_address = actor.actor;
        entity.frame_address = actor.frame;
        entity.stay_mode = actor.stay_mode;
        // V142 - un ennemi rallie est un ALLIE. Sans cela il resterait
        // rouge sur le radar et resterait une cible pour les aides a la
        // visee : le joueur tirerait sur ses propres hommes.
        entity.team =
            (actor.type == kActorTypeEnemy &&
             !IsActorRalliedToPlayer(actor.actor))
                ? EntityTeam::Enemy
                : EntityTeam::Ally;
        entity.active = 1;

        if (entity.team == EntityTeam::Enemy)
        {
            // C_human::frm_head is the model's actual head frame. Prefer its
            // world position over the former "highest nearby frame" heuristic,
            // which could select hair, equipment or a raised hand.
            std::uintptr_t head_frame = 0;
            Vector3 exact_head{};
            if (ReadRemotePointer(
                    game_process,
                    actor.actor + kActorHeadFrameOffset,
                    head_frame) &&
                game_process.ReadMemory(
                    head_frame + kFrameWorldPositionOffset,
                    exact_head) &&
                IsFinite(exact_head) &&
                std::fabs(exact_head.x) <= kMaximumSaneCoordinate &&
                std::fabs(exact_head.y) <= kMaximumSaneCoordinate &&
                std::fabs(exact_head.z) <= kMaximumSaneCoordinate)
            {
                entity.head_position = exact_head;
                entity.head_frame_address = head_frame;
            }

            ActorFrameState frame_state{};
            if (ReadActorFrameState(
                    game_process,
                    actor.frame,
                    actor.position,
                    frame_state) &&
                frame_state.found_visual_render_time)
            {
                std::uint32_t scene_render_time = 0;
                if (game_process.ReadMemory(
                        next.scene_object_address +
                            kSceneLastRenderTimeOffset,
                        scene_render_time) &&
                    scene_render_time != 0)
                {
                    const std::uint32_t render_age =
                        scene_render_time - frame_state.latest_render_time;
                    if (render_age <= kVisibilityFreshnessMs)
                        entity.rendered_recently = 1U;
                    if (render_age <= kVisibilityFreshnessMs &&
                        collision_geometry_ready)
                    {
                        const Vector3 head_target = entity.head_position;
                        entity.directly_visible = HasClearBallisticAimZone(
                            eye_position,
                            actor.position,
                            head_target) ? 1U : 0U;
                    }
                }
            }
        }
    }

    next.entity_array_valid = true;
    snapshot = next;
    g_runtime.read_state = RadarReadState::Ready;
    return true;
}

// Wrapper so the failing step is named on every exit path.
bool ReadRadarSnapshot(
    const TrainerProcess& game_process,
    RadarSnapshot& snapshot)
{
    const bool ok = ReadRadarSnapshotImpl(game_process, snapshot);
    // LogReadStateTransition(); // désactivé pour éviter le lag disque à chaque frame
    return ok;
}

bool WorldToScreen(
    const Vector3& world_position,
    Vector2& screen_position,
    const Matrix4x4& view_projection,
    float screen_width,
    float screen_height)
{
    screen_position = {};
    if (screen_width <= 0.0f || screen_height <= 0.0f ||
        !IsSane(world_position) || !IsSane(view_projection))
    {
        return false;
    }

    // The matrix at scene + 0x8C uses the Insanity3D row-vector convention:
    // [x y z 1] * viewProjection. Reading it as matrix * column vector (the
    // transposed formula) collapses this game's live actor projections.
    const float clip_w =
        world_position.x * view_projection.m[0][3] +
        world_position.y * view_projection.m[1][3] +
        world_position.z * view_projection.m[2][3] +
        view_projection.m[3][3];
    if (!std::isfinite(clip_w) || clip_w <= kMinimumClipW)
        return false;

    const float clip_x =
        world_position.x * view_projection.m[0][0] +
        world_position.y * view_projection.m[1][0] +
        world_position.z * view_projection.m[2][0] +
        view_projection.m[3][0];
    const float clip_y =
        world_position.x * view_projection.m[0][1] +
        world_position.y * view_projection.m[1][1] +
        world_position.z * view_projection.m[2][1] +
        view_projection.m[3][1];

    if (!std::isfinite(clip_x) || !std::isfinite(clip_y))
        return false;

    const float inverse_w = 1.0f / clip_w;
    const float normalized_x = clip_x * inverse_w;
    const float normalized_y = clip_y * inverse_w;
    if (!std::isfinite(normalized_x) || !std::isfinite(normalized_y))
        return false;

    screen_position.x = (normalized_x + 1.0f) * 0.5f * screen_width;
    screen_position.y = (1.0f - normalized_y) * 0.5f * screen_height;
    return std::isfinite(screen_position.x) &&
           std::isfinite(screen_position.y);
}

void RenderEspOverlay(
    const RadarSnapshot* snapshot,
    const RadarRenderSettings& settings)
{
    GetOverlay().Render(
        g_runtime.game_process_id,
        snapshot,
        settings);
}

TeleportMapResult RenderTeleportMapCanvas(
    const RadarSnapshot* snapshot,
    float width,
    float height,
    Vector3& selected_ground)
{
    selected_ground = {};
    if (!snapshot || !snapshot->entity_array_valid ||
        !g_collision_cache.valid || g_collision_cache.triangles.empty() ||
        width < 80.0f || height < 80.0f)
    {
        ImGui::Dummy(ImVec2((std::max)(width, 1.0f),
                            (std::max)(height, 1.0f)));
        return TeleportMapResult::Unavailable;
    }

    const float infinity = (std::numeric_limits<float>::infinity)();
    Vector3 minimum{infinity, infinity, infinity};
    Vector3 maximum{-infinity, -infinity, -infinity};
    for (const CollisionTriangle& triangle : g_collision_cache.triangles)
    {
        if (!IsWalkableTriangle(triangle))
            continue;
        minimum = Minimum(minimum, triangle.bounds_min);
        maximum = Maximum(maximum, triangle.bounds_max);
    }
    const float extent_x = maximum.x - minimum.x;
    const float extent_z = maximum.z - minimum.z;
    if (!IsSane(minimum) || !IsSane(maximum) || extent_x <= 0.01f ||
        extent_z <= 0.01f)
    {
        ImGui::Dummy(ImVec2(width, height));
        return TeleportMapResult::Unavailable;
    }

    const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
    const ImVec2 canvas_size(width, height);
    ImGui::InvisibleButton("##teleport-map-canvas", canvas_size);
    const ImVec2 canvas_max(canvas_min.x + width, canvas_min.y + height);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(canvas_min, canvas_max, IM_COL32(13, 18, 24, 255));
    draw->PushClipRect(canvas_min, canvas_max, true);

    const float usable_width = width - kTeleportMapPadding * 2.0f;
    const float usable_height = height - kTeleportMapPadding * 2.0f;
    const float scale = (std::min)(usable_width / extent_x,
                                   usable_height / extent_z);
    const float drawn_width = extent_x * scale;
    const float drawn_height = extent_z * scale;
    const ImVec2 origin(
        canvas_min.x + (width - drawn_width) * 0.5f,
        canvas_min.y + (height - drawn_height) * 0.5f);
    const auto project = [&](const Vector3& point)
    {
        return ImVec2(origin.x + (point.x - minimum.x) * scale,
                      origin.y + (maximum.z - point.z) * scale);
    };

    const std::size_t stride = (std::max)(
        std::size_t{1},
        (g_collision_cache.triangles.size() +
         kMaximumMapTrianglesDrawn - 1) /
            kMaximumMapTrianglesDrawn);
    for (std::size_t index = 0;
         index < g_collision_cache.triangles.size(); index += stride)
    {
        const CollisionTriangle& triangle =
            g_collision_cache.triangles[index];
        if (!IsWalkableTriangle(triangle))
            continue;
        const float normalized_height = (std::clamp)(
            (triangle.centroid.y - minimum.y) /
                (std::max)(maximum.y - minimum.y, 0.001f),
            0.0f, 1.0f);
        const int shade = 42 + static_cast<int>(normalized_height * 58.0f);
        draw->AddTriangleFilled(
            project(triangle.a), project(triangle.b), project(triangle.c),
            IM_COL32(shade, shade + 10, shade + 14, 190));
    }

    for (std::uint32_t index = 0;
         index < snapshot->entity_array.count; ++index)
    {
        const RadarEntity& entity = snapshot->entity_array.entities[index];
        if (entity.active == 0)
            continue;
        const ImVec2 point = project(entity.position);
        const ImU32 color = entity.team == EntityTeam::Enemy
            ? IM_COL32(230, 70, 70, 255)
            : IM_COL32(70, 140, 245, 255);
        draw->AddCircleFilled(point, 3.0f, color);
    }
    const ImVec2 player = project(snapshot->player.position);
    draw->AddCircleFilled(player, 5.0f, IM_COL32(255, 225, 70, 255));
    draw->AddCircle(player, 7.0f, IM_COL32(20, 20, 20, 255), 0, 2.0f);
    draw->AddRect(canvas_min, canvas_max, IM_COL32(125, 145, 165, 255));
    draw->PopClipRect();

    if (!ImGui::IsItemClicked(ImGuiMouseButton_Left))
        return TeleportMapResult::Ready;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (mouse.x < origin.x || mouse.x > origin.x + drawn_width ||
        mouse.y < origin.y || mouse.y > origin.y + drawn_height)
    {
        return TeleportMapResult::NoWalkableGround;
    }
    const float world_x = minimum.x + (mouse.x - origin.x) / scale;
    const float world_z = maximum.z - (mouse.y - origin.y) / scale;
    return FindHighestWalkableGround(world_x, world_z, selected_ground)
        ? TeleportMapResult::GroundSelected
        : TeleportMapResult::NoWalkableGround;
}

TeleportMapResult ResolveNativeMapClick(
    const TrainerProcess& game_process,
    const RadarSnapshot& snapshot,
    float client_x,
    float client_y,
    Vector3& selected_ground)
{
    selected_ground = {};
    if (!game_process.IsConnected() ||
        snapshot.game_client_width <= 0.0f ||
        snapshot.game_client_height <= 0.0f)
    {
        LogDiagnostic("Teleport: click ignored (not connected or no client bounds).");
        return TeleportMapResult::Unavailable;
    }

    std::uint8_t map_active = 0;
    std::uintptr_t map_manager = 0;
    std::uintptr_t map_scene = 0;
    const bool active_read = game_process.ReadMemory(
        snapshot.entity_list_object_address + kGameMissionMapActiveOffset,
        map_active);
    const bool manager_read = game_process.ReadMemory(
        snapshot.entity_list_object_address + kGameMissionMapManagerOffset,
        map_manager);
    const bool scene_read = manager_read &&
        map_manager >= 0x10000U && map_manager <= 0x7FFF'FFFFU &&
        game_process.ReadMemory(map_manager + kMissionSceneOffset, map_scene);
    if (!active_read || map_active != 1 ||
        !manager_read || !scene_read ||
        map_scene < 0x10000U || map_scene > 0x7FFF'FFFFU)
    {
        LogDiagnostic(
            "Teleport: native map state invalid "
            "(active_read=%d active=%u manager_read=%d manager=%08X "
            "scene_read=%d scene=%08X).",
            active_read ? 1 : 0, active_read ? map_active : 0,
            manager_read ? 1 : 0, static_cast<unsigned>(map_manager),
            scene_read ? 1 : 0, static_cast<unsigned>(map_scene));
        return TeleportMapResult::MapNotActive;
    }

    // H&D hides/centres the Win32 pointer while independently accumulating
    // raw mouse motion in these native fields. The native values are the
    // cursor the player actually sees on the map; the Win32 click commonly
    // remains fixed at the screen centre and must never replace them.
    std::uint32_t cursor_block_offset = 0;
    float native_cursor_x = 0.0f;
    float native_cursor_y = 0.0f;
    if (!ReadNativeMapCursor(
            game_process,
            map_manager,
            snapshot.game_client_width,
            snapshot.game_client_height,
            native_cursor_x,
            native_cursor_y,
            &cursor_block_offset))
    {
        LogDiagnostic(
            "Teleport: native cursor block not found in map manager %08X.",
            static_cast<unsigned>(map_manager));
        return TeleportMapResult::CursorBlockNotFound;
    }
    const std::int32_t exact_cursor_x = static_cast<std::int32_t>(
        std::lround(native_cursor_x));
    const float vertical_cursor_factor =
        (snapshot.game_client_height / snapshot.game_client_width) *
        (4.0f / 3.0f);
    if (!std::isfinite(vertical_cursor_factor) ||
        vertical_cursor_factor <= 0.01f)
    {
        LogDiagnostic("Teleport: invalid cursor aspect during map pick.");
        return TeleportMapResult::CursorBlockNotFound;
    }
    const float visible_cursor_y =
        native_cursor_y / vertical_cursor_factor;
    const std::int32_t exact_cursor_y = static_cast<std::int32_t>(
        std::lround(visible_cursor_y));
    LogDiagnostic(
        "[TEST TELEPORT] CURSOR_PICK manager+%X win32=(%.0f,%.0f) "
        "native_internal=(%.0f,%.0f) aspect_factor=%.6f "
        "native_visible=(%.0f,%.3f) selected=(%d,%d) source=NATIVE_VISIBLE.",
        cursor_block_offset, client_x, client_y,
        native_cursor_x, native_cursor_y, vertical_cursor_factor,
        native_cursor_x, visible_cursor_y,
        exact_cursor_x, exact_cursor_y);

    Vector3 ray_origin{};
    Vector3 ray_direction{};
    Vector3 map_hit{};
    if (!UnmapNativeMapPointOnMainThread(
            game_process,
            map_scene,
            exact_cursor_x,
            exact_cursor_y,
            ray_origin,
            ray_direction,
            map_hit))
    {
        LogDiagnostic("Teleport: native map pick failed.");
        return TeleportMapResult::RayMissed;
    }

    // The map itself already performed the exact native ray collision used by
    // H&D's own destination picker. A second mission-scene ground search can
    // choose another floor at the same X/Z (bridge, roof, upper storey), which
    // is the visible offset reported by the user. Preserve the native hit.
    if (!IsSane(map_hit))
    {
        LogDiagnostic(
            "Teleport: native map returned an invalid hit "
            "(%.3f, %.3f, %.3f).",
            map_hit.x, map_hit.y, map_hit.z);
        return TeleportMapResult::NoWalkableGround;
    }
    selected_ground = map_hit;
    LogDiagnostic(
        "Teleport: exact native map hit selected (%.3f, %.3f, %.3f).",
        selected_ground.x, selected_ground.y, selected_ground.z);
    return TeleportMapResult::GroundSelected;
}
}
