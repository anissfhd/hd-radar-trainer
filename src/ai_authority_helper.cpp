#include "trainer_process.h"

#include <WinSock2.h>
#include <Windows.h>
#include <WS2tcpip.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <vector>

#if !defined(HD_AI_AUTHORITY_HOST) && !defined(HD_AI_AUTHORITY_CLIENT)
#error "Build this helper with exactly one authority role."
#endif

#if defined(HD_AI_AUTHORITY_HOST) && defined(HD_AI_AUTHORITY_CLIENT)
#error "Only one authority role may be selected."
#endif

namespace
{
constexpr std::uintptr_t kMissionPointerRva = 0x0010AD4C;
constexpr std::uintptr_t kMissionActorsOffset = 0x68;
constexpr std::uintptr_t kActorTypeOffset = 0x1C;
constexpr std::uintptr_t kActorNetworkIdOffset = 0x20;
constexpr std::uintptr_t kActorNetworkOwnerOffset = 0x34;
constexpr std::uintptr_t kActorFrameOffset = 0x28;
constexpr std::uintptr_t kFrameActorBackReferenceOffset = 0x80;
constexpr std::uintptr_t kFrameFlagsOffset = 0x0C;
constexpr std::uintptr_t kFrameWorldPositionOffset = 0xBC;
// Insanity3D's private FRMFLAGS_ON flag. I3D_frame::SetOn(false) clears this
// flag; the renderer then skips the whole model hierarchy. We write only this
// bit, only in the Client process, and preserve every other frame flag.
constexpr std::uint32_t kFrameOnFlag = 0x00020000U;
constexpr std::uint32_t kActorTypePlayer = 1;
constexpr std::uint32_t kActorTypeEnemy = 2;
constexpr std::uint32_t kMaximumActorCount = 2048;
constexpr std::uint32_t kPeerVisualCommandMagic = 0x50445648U; // "HVDP"
// V98, 3 septembre 2026 - version 5 du message.
// Deux ordres de plus, tous deux destines aux soldats que le CLIENT possede
// lui-meme : la vie etendue et le gel de la position qu'il publie. Ni l'un ni
// l'autre ne peut etre fait depuis l'hote, puisque ces soldats appartiennent a
// la machine de l'ami. La taille du message change, donc un ancien CLIENT
// rejette proprement le nouveau paquet au lieu de le mal interpreter.
constexpr std::uint16_t kPeerVisualCommandVersion = 6;
constexpr unsigned short kPeerVisualCommandPort = 48217;
constexpr DWORD kPeerVisualCommandTimeoutMs = 1500U;
constexpr std::uint16_t kPeerAllHostPlayersNetworkId = 0xFFFFU;
// A parked guard still jumps through its native prologue for every actor.
// This is deliberately distinct from the all-host-players sentinel above.
constexpr std::uint16_t kPeerNoHostPlayersNetworkId = 0xFFFEU;
constexpr std::uintptr_t kPlayerDieVtableOffset = 0x100;
constexpr std::uintptr_t kPlayerHitVtableOffset = 0x104;
constexpr std::uintptr_t kPlayerExplodeVtableOffset = 0xDC;
constexpr std::uintptr_t kHumanDieRva = 0x000208B0;
constexpr std::uintptr_t kHumanTickRva = 0x0001A560;
constexpr std::size_t kHumanTickPatchSize = 9;
constexpr std::uintptr_t kPlayerSetActiveVtableOffset = 0x6C;
// V98 - Sante Max applique par le CLIENT a ses propres soldats.
// Memes offsets que le trainer de l'hote, deduits du binaire de reference de
// 2002 puis corriges du decalage de 0x14 de la revision Deluxe. Toute ecriture
// est precedee d'une relecture de controle.
constexpr std::uintptr_t kPlayerResistanceOffset = 0x2C;
constexpr std::uintptr_t kPlayerInitResistanceOffset = 0x2D8;
constexpr std::int32_t kExtendedHealthValue = 20000;
constexpr std::int32_t kMaximumNativeInitResistance = 6000;
// V98 - gel de la position que CE PC publie pour ses propres soldats.
// Port exact du crochet de l'hote : le compilateur construit le message
// NM_HUMAN_POS a ce point, EBX y est le C_human en cours et les trois flottants
// serialises sont les locaux [ebp-10h/-0Ch/-08h]. Les reecrire publie l'ancre
// choisie sans toucher ni la position du modele, ni la physique locale.
constexpr std::uintptr_t kHumanPositionSendRva = 0x0001'BD97;
constexpr std::size_t kPositionMaskPatchSize = 9;
constexpr std::size_t kPositionMaskRemoteSize = 0x240;
constexpr std::size_t kPositionMaskActiveOffset = 0x180;
constexpr std::size_t kPositionMaskCountOffset = 0x184;
constexpr std::size_t kPositionMaskEntriesOffset = 0x188;
constexpr std::size_t kPositionMaskMaximumActors = 8;
constexpr std::uintptr_t kFrameWorldPositionOffsetLocal = 0xBC;
constexpr std::uintptr_t kHumanStayModeOffset = 0x254;
constexpr std::uintptr_t kPlayerModeOffset = 0x2B4;
constexpr std::uint32_t kHumanStayModeAlive = 1;
constexpr std::uint32_t kPlayerModeProgram = 2;
constexpr std::size_t kClientReviveRemoteSize = 0x240;
constexpr std::size_t kClientReviveStateOffset = 0x200;
constexpr std::size_t kClientReviveTargetOffset = 0x204;
constexpr std::uint32_t kClientReviveQueued = 1;
constexpr std::uint32_t kClientReviveCompleted = 2;
constexpr std::uint32_t kClientReviveRejected = 3;
constexpr std::size_t kRemoteDeathGuardPatchSize = 9;
constexpr std::size_t kRemoteExplodeGuardPatchSize = 10;
constexpr std::size_t kRemoteHitGuardPatchSize = 5;
constexpr std::size_t kRemoteDeathGuardRemoteSize = 0x100;
// Every remote guard keeps its selected host network id in this writable slot.
// Changing "current player" <-> "whole squad" must never require uninstalling
// a live guard: the entry point is shared by several player classes and a
// remove/reinstall race was making the Client read our E9 hook as an unknown
// native signature.
constexpr std::size_t kRemoteDeathGuardTargetOffset = 0xF0;
// A revealed host actor first receives the new native NM_HUMAN_POS packet,
// then H&D interpolates its model from the old anchor. Keeping the model off
// for this short hand-off removes that visible "fast run" without changing the
// game's position, velocity or network code.
constexpr DWORD kPeerVisualRevealMinimumHoldMs = 350U;
constexpr DWORD kPeerVisualRevealMaximumHoldMs = 1200U;
constexpr float kPeerVisualRevealStableDistance = 0.035f;
constexpr unsigned kPeerVisualRevealStableSamples = 2U;

struct RemoteVector32
{
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
};

struct Actor
{
    std::uintptr_t address = 0;
    std::uint32_t type = 0;
    std::uint16_t network_id = 0;
    std::uint32_t network_owner = 0;
};

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

struct Vector3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

static_assert(sizeof(PeerVisualCommandWire) == 70,
    "Host and Client must agree on the peer command layout.");

struct HiddenFrame
{
    std::uintptr_t actor = 0;
    std::uintptr_t frame = 0;
    bool was_on = false;
    Vector3 last_observed_position{};
    bool has_observed_position = false;
    unsigned stable_samples = 0;
};

struct ClientVisualState
{
    SOCKET socket = INVALID_SOCKET;
    bool winsock_ready = false;
    bool hide_requested = false;
    bool suppress_host_player_death_requested = false;
    bool suppress_host_player_damage_requested = false;
    // V98 : ordres portant sur les soldats de CETTE machine.
    bool extend_health_requested = false;
    bool mask_position_requested = false;
    // Derniere table de vie recue de l'hote.
    std::uint8_t health_count = 0;
    std::array<PeerHealthEntry, kPeerHealthSlots> health{};
    std::uint16_t visual_target_network_id = 0;
    std::uint16_t life_target_network_id = 0;
    std::uint32_t last_revive_sequence = 0;
    std::uint32_t pending_revive_sequence = 0;
    bool reveal_pending = false;
    DWORD last_command_tick = 0;
    DWORD reveal_requested_tick = 0;
    bool hide_reported = false;
    bool reveal_reported = false;
    std::vector<HiddenFrame> hidden_frames;
};

#if defined(HD_AI_AUTHORITY_CLIENT)
void ClientTrace(const wchar_t* format, ...)
{
    static std::FILE* file = nullptr;
    if (file == nullptr)
    {
        wchar_t path[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (length != 0 && length < MAX_PATH)
        {
            wchar_t* const slash = std::wcsrchr(path, L'\\');
            if (slash != nullptr)
            {
                *(slash + 1) = L'\0';
                if (wcscat_s(
                        path, MAX_PATH,
                        L"HD_AI_AUTHORITY_CLIENT_V88_TRACE.log") == 0)
                {
                    (void)_wfopen_s(&file, path, L"a, ccs=UTF-8");
                }
            }
        }
    }

    va_list arguments;
    va_start(arguments, format);
    std::vwprintf(format, arguments);
    va_end(arguments);

    if (file != nullptr)
    {
        std::fwprintf(file, L"[tick=%lu] ", static_cast<unsigned long>(GetTickCount()));
        va_start(arguments, format);
        std::vfwprintf(file, format, arguments);
        va_end(arguments);
        std::fflush(file);
    }
}
#endif

template <std::size_t PatchSize>
struct RemoteHostGuard
{
    DWORD process_id = 0;
    std::uintptr_t function = 0;
    std::uintptr_t remote = 0;
    std::uint16_t target_network_id = 0;
    std::array<std::uint8_t, PatchSize> original{};
    std::array<std::uint8_t, PatchSize> patch{};
    bool applied = false;
};

using RemoteHostDeathGuard = RemoteHostGuard<kRemoteDeathGuardPatchSize>;
using RemoteHostExplodeGuard = RemoteHostGuard<kRemoteExplodeGuardPatchSize>;
using RemoteHostHitGuard = RemoteHostGuard<kRemoteHitGuardPatchSize>;

struct ClientDeathMirrorState
{
    // The host squad can mix original and mod-added player classes.  Their
    // virtual tables may resolve these slots to different functions, so the
    // Client must mirror one guard for every distinct entry in squad mode.
    std::vector<RemoteHostDeathGuard> player_die;
    RemoteHostDeathGuard base_die{};
    std::vector<RemoteHostExplodeGuard> player_explode;
    std::vector<RemoteHostHitGuard> player_hit;
    bool active_reported = false;
};

struct ClientHostReviveState
{
    DWORD process_id = 0;
    std::uintptr_t function = 0;
    std::uintptr_t remote = 0;
    std::uintptr_t target = 0;
    std::uint32_t sequence = 0;
    std::array<std::uint8_t, kHumanTickPatchSize> original{};
    std::array<std::uint8_t, kHumanTickPatchSize> patch{};
    bool applied = false;
};

const wchar_t* RoleName()
{
#if defined(HD_AI_AUTHORITY_HOST)
    return L"HOTE";
#else
    return L"CLIENT";
#endif
}

bool ReadActors(
    hd::TrainerProcess& process,
    std::vector<Actor>& actors)
{
    actors.clear();

    hd::RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        kMissionPointerRva >= module.image_size)
    {
        return false;
    }

    std::uint32_t mission32 = 0;
    if (!process.ReadMemory(module.base_address + kMissionPointerRva, mission32) ||
        mission32 < 0x10000U)
    {
        return false;
    }

    RemoteVector32 vector{};
    const std::uintptr_t mission = mission32;
    if (!process.ReadMemory(mission + kMissionActorsOffset, vector) ||
        vector.begin < 0x10000U || vector.end < vector.begin)
    {
        return false;
    }

    const std::uint32_t byte_count = vector.end - vector.begin;
    if (byte_count % sizeof(std::uint32_t) != 0)
        return false;

    const std::uint32_t count = byte_count / sizeof(std::uint32_t);
    if (count == 0 || count > kMaximumActorCount)
        return false;

    std::vector<std::uint32_t> addresses(count);
    if (!process.ReadMemory(
            vector.begin, addresses.data(),
            addresses.size() * sizeof(addresses.front())))
    {
        return false;
    }

    actors.reserve(addresses.size());
    for (const std::uint32_t address32 : addresses)
    {
        if (address32 < 0x10000U)
            continue;

        Actor actor{};
        actor.address = address32;
        if (!process.ReadMemory(actor.address + kActorTypeOffset, actor.type) ||
            !process.ReadMemory(
                actor.address + kActorNetworkIdOffset, actor.network_id) ||
            !process.ReadMemory(
                actor.address + kActorNetworkOwnerOffset,
                actor.network_owner))
        {
            continue;
        }

        if (actor.type == kActorTypePlayer || actor.type == kActorTypeEnemy)
            actors.push_back(actor);
    }

    return !actors.empty();
}

std::uint32_t FindPeerPid(const std::vector<Actor>& actors)
{
    for (const Actor& actor : actors)
    {
        // In a two-PC game, a remote player's actor is owned by the other
        // process. On the client this is precisely the host PID.
        if (actor.type == kActorTypePlayer && actor.network_owner != 0)
            return actor.network_owner;
    }
    return 0;
}

bool HasRemotePlayer(const std::vector<Actor>& actors)
{
    return FindPeerPid(actors) != 0;
}

#if defined(HD_AI_AUTHORITY_CLIENT)
bool IsSanePointer(std::uintptr_t value)
{
    return value >= 0x10000U && value <= 0x7FFF'FFFFU;
}

bool InitialisePeerVisualReceiver(ClientVisualState& state)
{
    if (state.socket != INVALID_SOCKET)
        return true;
    if (!state.winsock_ready)
    {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            return false;
        state.winsock_ready = true;
    }

    state.socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (state.socket == INVALID_SOCKET)
        return false;

    const BOOL reuse = TRUE;
    (void)setsockopt(
        state.socket, SOL_SOCKET, SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    sockaddr_in bind_address{};
    bind_address.sin_family = AF_INET;
    bind_address.sin_port = htons(kPeerVisualCommandPort);
    bind_address.sin_addr.s_addr = INADDR_ANY;
    if (bind(
            state.socket, reinterpret_cast<const sockaddr*>(&bind_address),
            sizeof(bind_address)) == SOCKET_ERROR)
    {
        closesocket(state.socket);
        state.socket = INVALID_SOCKET;
        return false;
    }
    u_long non_blocking = 1;
    if (ioctlsocket(state.socket, FIONBIO, &non_blocking) == SOCKET_ERROR)
    {
        closesocket(state.socket);
        state.socket = INVALID_SOCKET;
        return false;
    }
    std::wprintf(
        L"[ok] canal visuel LAN V105 en ecoute (UDP %u).\n",
        static_cast<unsigned>(kPeerVisualCommandPort));
    ClientTrace(L"[trace] receiver started protocol=%u UDP=%u.\n",
        static_cast<unsigned>(kPeerVisualCommandVersion),
        static_cast<unsigned>(kPeerVisualCommandPort));
    return true;
}

void PollPeerVisualCommands(ClientVisualState& state)
{
    if (state.socket == INVALID_SOCKET)
        return;

    for (;;)
    {
        PeerVisualCommandWire command{};
        sockaddr_in sender{};
        int sender_size = sizeof(sender);
        const int received = recvfrom(
            state.socket, reinterpret_cast<char*>(&command), sizeof(command),
            0, reinterpret_cast<sockaddr*>(&sender), &sender_size);
        if (received == SOCKET_ERROR)
        {
            if (WSAGetLastError() != WSAEWOULDBLOCK)
                std::wprintf(L"[attention] canal visuel LAN: erreur %d.\n", WSAGetLastError());
            break;
        }
        // V99 : dire pourquoi un paquet est rejete. Sans cela, un compagnon
        // reste d'une generation precedente est indiscernable d'un hote qui
        // n'emet rien : les deux se traduisent par « il ne se passe rien ».
        if (command.magic == kPeerVisualCommandMagic &&
            (received != static_cast<int>(sizeof(command)) ||
             command.version != kPeerVisualCommandVersion))
        {
            static DWORD next_version_warning = 0;
            const DWORD now_tick = GetTickCount();
            if (now_tick >= next_version_warning)
            {
                next_version_warning = now_tick + 3000U;
                std::wprintf(
                    L"[attention] message de l'hote refuse : version %u, %d "
                    L"octets; ce compagnon attend la version %u sur %u "
                    L"octets. Mettez a jour le compagnon CLIENT.\n",
                    static_cast<unsigned>(command.version), received,
                    static_cast<unsigned>(kPeerVisualCommandVersion),
                    static_cast<unsigned>(sizeof(command)));
            }
        }
        if (received != static_cast<int>(sizeof(command)) ||
            command.magic != kPeerVisualCommandMagic ||
            command.version != kPeerVisualCommandVersion ||
            command.hide_host_models > 1U ||
            command.suppress_host_player_death > 1U ||
            command.suppress_host_player_damage > 1U ||
            command.extend_client_health > 1U ||
            command.mask_client_position > 1U)
        {
            continue;
        }
        const bool next_extend_health = command.extend_client_health != 0;
        const bool next_mask_position = command.mask_client_position != 0;
        if (next_extend_health != state.extend_health_requested)
        {
            std::wprintf(
                L"[vie locale] sante etendue de VOS soldats %ls par l'hote.\n",
                next_extend_health ? L"demandee" : L"relachee");
        }
        if (next_mask_position != state.mask_position_requested)
        {
            std::wprintf(
                L"[position] gel de la position publiee par CE PC %ls.\n",
                next_mask_position ? L"demande" : L"relache");
        }
        state.extend_health_requested = next_extend_health;
        state.mask_position_requested = next_mask_position;
        state.health_count = command.health_count <= kPeerHealthSlots
            ? command.health_count
            : static_cast<std::uint8_t>(kPeerHealthSlots);
        for (std::size_t index = 0; index < kPeerHealthSlots; ++index)
            state.health[index] = command.health[index];
        const bool next_hidden = command.hide_host_models != 0;
        const bool next_death_suppressed =
            command.suppress_host_player_death != 0;
        const bool next_damage_suppressed =
            command.suppress_host_player_damage != 0;
        const bool changed = next_hidden != state.hide_requested ||
            next_death_suppressed != state.suppress_host_player_death_requested ||
            next_damage_suppressed != state.suppress_host_player_damage_requested ||
            command.visual_target_network_id != state.visual_target_network_id ||
            command.life_target_network_id != state.life_target_network_id ||
            command.revive_sequence != state.last_revive_sequence;
        ClientTrace(
            L"[trace] RX hote=%08X bytes=%d hide=%u death_guard=%u damage_guard=%u visual_id=%u life_id=%u revive=%u changed=%u.\n",
            static_cast<unsigned>(ntohl(sender.sin_addr.s_addr)), received,
            static_cast<unsigned>(command.hide_host_models),
            static_cast<unsigned>(command.suppress_host_player_death),
            static_cast<unsigned>(command.suppress_host_player_damage),
            static_cast<unsigned>(command.visual_target_network_id),
            static_cast<unsigned>(command.life_target_network_id),
            static_cast<unsigned>(command.revive_sequence), changed ? 1U : 0U);
        if (next_hidden != state.hide_requested)
        {
            std::wprintf(
                L"[visuel] commande hote: modeles joueurs hote %ls.\n",
                next_hidden ? L"masques" : L"visibles");
        }
        if (!next_hidden && state.hide_requested &&
            !state.hidden_frames.empty())
        {
            // Do not reveal during the native remote-position interpolation.
            state.reveal_pending = true;
            state.reveal_requested_tick = GetTickCount();
            state.reveal_reported = false;
        }
        if (next_hidden)
        {
            state.reveal_pending = false;
            state.reveal_reported = false;
        }
        if (state.visual_target_network_id != command.visual_target_network_id &&
            !state.hidden_frames.empty())
        {
            // A switch from current-player to squad (or to another soldier)
            // must restore the old selection before hiding the new one.
            state.reveal_pending = false;
        }
        if (next_death_suppressed !=
            state.suppress_host_player_death_requested)
        {
            std::wprintf(
                L"[vie hote] mort reseau des joueurs hote %ls par le Client.\n",
                next_death_suppressed ? L"ignoree" : L"native");
        }
        if (next_damage_suppressed !=
            state.suppress_host_player_damage_requested)
        {
            std::wprintf(
                L"[vie hote] degats reseau des joueurs hote %ls par le Client.\n",
                next_damage_suppressed ? L"ignores" : L"natifs");
        }
        state.hide_requested = next_hidden;
        state.suppress_host_player_death_requested = next_death_suppressed;
        state.suppress_host_player_damage_requested = next_damage_suppressed;
        state.visual_target_network_id = command.visual_target_network_id;
        state.life_target_network_id = command.life_target_network_id;
        if (command.revive_sequence != 0 &&
            command.revive_sequence != state.last_revive_sequence)
        {
            // The death guard has kept the actor alive.  A completed native
            // revive on the host makes the Client re-enable any frame it had
            // temporarily hidden; it does not invoke C_player::Explode.
            state.last_revive_sequence = command.revive_sequence;
            state.pending_revive_sequence = command.revive_sequence;
            state.reveal_pending = false;
            std::wprintf(L"[vie hote] synchronisation native recue (soldat %u).\n",
                static_cast<unsigned>(command.life_target_network_id));
        }
        state.last_command_tick = GetTickCount();
    }

    if ((state.hide_requested || state.suppress_host_player_death_requested ||
         state.suppress_host_player_damage_requested ||
         state.extend_health_requested || state.mask_position_requested) &&
        static_cast<DWORD>(GetTickCount() - state.last_command_tick) >
            kPeerVisualCommandTimeoutMs)
    {
        state.hide_requested = false;
        state.suppress_host_player_death_requested = false;
        state.suppress_host_player_damage_requested = false;
        state.extend_health_requested = false;
        state.mask_position_requested = false;
        state.health_count = 0;
        state.visual_target_network_id = 0;
        state.life_target_network_id = 0;
        state.reveal_pending = false;
        std::wprintf(
            L"[securite] signal hote perdu: rendu et mort reseau restaures.\n");
        ClientTrace(L"[trace] timeout: host command absent > %lu ms; guards parked native.\n",
            static_cast<unsigned long>(kPeerVisualCommandTimeoutMs));
    }
}

bool ResolveActorFrame(
    hd::TrainerProcess& process,
    std::uintptr_t actor,
    std::uintptr_t& frame,
    std::uint32_t& flags)
{
    std::uint32_t frame32 = 0;
    std::uint32_t owner32 = 0;
    if (!process.ReadMemory(actor + kActorFrameOffset, frame32) ||
        !IsSanePointer(frame32))
    {
        return false;
    }
    frame = frame32;
    if (!process.ReadMemory(frame + kFrameActorBackReferenceOffset, owner32) ||
        owner32 != static_cast<std::uint32_t>(actor) ||
        !process.ReadMemory(frame + kFrameFlagsOffset, flags))
    {
        return false;
    }
    return true;
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

bool HaveRemoteHostModelsStoppedMoving(
    hd::TrainerProcess& process,
    ClientVisualState& state)
{
    bool all_stable = !state.hidden_frames.empty();
    for (HiddenFrame& record : state.hidden_frames)
    {
        std::uintptr_t frame = 0;
        std::uint32_t flags = 0;
        Vector3 model_position{};
        if (!ResolveActorFrame(process, record.actor, frame, flags) ||
            frame != record.frame ||
            !process.ReadMemory(
                frame + kFrameWorldPositionOffset, model_position) ||
            !IsSaneWorldPosition(model_position))
        {
            return false;
        }
        if (!record.has_observed_position)
        {
            record.last_observed_position = model_position;
            record.has_observed_position = true;
            record.stable_samples = 0;
            all_stable = false;
            continue;
        }
        const float dx = model_position.x - record.last_observed_position.x;
        const float dy = model_position.y - record.last_observed_position.y;
        const float dz = model_position.z - record.last_observed_position.z;
        record.last_observed_position = model_position;
        if (dx * dx + dy * dy + dz * dz <=
            kPeerVisualRevealStableDistance *
                kPeerVisualRevealStableDistance)
        {
            ++record.stable_samples;
        }
        else
        {
            record.stable_samples = 0;
        }
        if (record.stable_samples < kPeerVisualRevealStableSamples)
            all_stable = false;
    }
    return all_stable;
}

void RestoreRemoteHostModels(
    hd::TrainerProcess& process,
    ClientVisualState& state)
{
    unsigned restored = 0;
    for (const HiddenFrame& record : state.hidden_frames)
    {
        std::uintptr_t frame = 0;
        std::uint32_t flags = 0;
        if (!ResolveActorFrame(process, record.actor, frame, flags) ||
            frame != record.frame || !record.was_on)
        {
            continue;
        }
        const std::uint32_t restored_flags = flags | kFrameOnFlag;
        if ((flags & kFrameOnFlag) == 0 &&
            process.WriteMemory(record.frame + kFrameFlagsOffset, restored_flags))
        {
            ++restored;
        }
    }
    if (!state.hidden_frames.empty())
    {
        std::wprintf(L"[visuel] %u modele(s) hote restaure(s).\n", restored);
    }
    state.hidden_frames.clear();
    state.hide_reported = false;
    state.reveal_reported = false;
}

void ApplyPeerVisualMask(
    hd::TrainerProcess& process,
    const std::vector<Actor>& actors,
    ClientVisualState& state)
{
    if (!state.hide_requested)
    {
        if (state.reveal_pending)
        {
            const DWORD elapsed = static_cast<DWORD>(
                GetTickCount() - state.reveal_requested_tick);
            const bool minimum_hold_complete =
                elapsed >= kPeerVisualRevealMinimumHoldMs;
            const bool settled = minimum_hold_complete &&
                HaveRemoteHostModelsStoppedMoving(process, state);
            if (!settled && elapsed < kPeerVisualRevealMaximumHoldMs)
            {
                if (!state.reveal_reported)
                {
                    std::wprintf(
                        L"[visuel] synchronisation de la nouvelle position hote; "
                        L"modele maintenu cache.\n");
                    state.reveal_reported = true;
                }
                return;
            }
            if (!settled)
            {
                std::wprintf(
                    L"[attention] synchronisation visuelle expiree; modele restaure.\n");
            }
            state.reveal_pending = false;
        }
        RestoreRemoteHostModels(process, state);
        return;
    }

    // The selected-host-player mode must never leave a former squad member
    // hidden after the player switches soldier or changes the scope.
    if (!state.hidden_frames.empty() && state.visual_target_network_id != 0)
    {
        const bool stale_selection = std::any_of(
            state.hidden_frames.begin(), state.hidden_frames.end(),
            [&](const HiddenFrame& record)
            {
                const auto actor = std::find_if(
                    actors.begin(), actors.end(),
                    [&](const Actor& candidate)
                    {
                        return candidate.address == record.actor;
                    });
                return actor == actors.end() ||
                    actor->network_id != state.visual_target_network_id;
            });
        if (stale_selection)
            RestoreRemoteHostModels(process, state);
    }

    unsigned hidden = 0;
    for (const Actor& actor : actors)
    {
        // On the Client the host's squad members are the only player actors
        // owned by a non-zero remote PID. Local player actors and all enemies
        // are deliberately excluded.
        if (actor.type != kActorTypePlayer || actor.network_owner == 0)
            continue;
        if (state.visual_target_network_id != 0 &&
            actor.network_id != state.visual_target_network_id)
        {
            continue;
        }

        std::uintptr_t frame = 0;
        std::uint32_t flags = 0;
        if (!ResolveActorFrame(process, actor.address, frame, flags))
            continue;

        const auto existing = std::find_if(
            state.hidden_frames.begin(), state.hidden_frames.end(),
            [&](const HiddenFrame& record)
            {
                return record.actor == actor.address && record.frame == frame;
            });
        if (existing != state.hidden_frames.end())
        {
            // Switching to a hidden host soldier in spectator mode calls the
            // game's SetActive path and can turn FRMFLAGS_ON back on. The old
            // implementation skipped an already-recorded frame, leaving that
            // one-frame game write permanent. Keep enforcing the requested
            // hidden state on every Client helper pass instead.
            if (existing->was_on && (flags & kFrameOnFlag) != 0)
            {
                if (process.WriteMemory(
                        frame + kFrameFlagsOffset, flags & ~kFrameOnFlag))
                {
                    std::wprintf(
                        L"[visuel] reactivation spectateur bloquee pour un modele hote masque.\n");
                }
            }
            continue;
        }

        const bool was_on = (flags & kFrameOnFlag) != 0;
        if (was_on && !process.WriteMemory(
                frame + kFrameFlagsOffset, flags & ~kFrameOnFlag))
        {
            continue;
        }
        state.hidden_frames.push_back({actor.address, frame, was_on});
        if (was_on)
            ++hidden;
    }
    if (!state.hide_reported || hidden != 0)
    {
        std::wprintf(
            L"[visuel] %u modele(s) hote masque(s), IA et reseau inchanges.\n",
            hidden);
        state.hide_reported = true;
    }
}

template <std::size_t PatchSize>
bool RestoreRemoteHostGuard(
    hd::TrainerProcess& process,
    RemoteHostGuard<PatchSize>& state,
    const wchar_t* name)
{
    if (!state.applied)
        return true;
    const RemoteHostGuard<PatchSize> previous = state;
    if (!process.IsConnected() || process.ProcessId() != previous.process_id)
    {
        state = {};
        return true;
    }

    std::array<std::uint8_t, PatchSize> current{};
    if (!process.ReadMemory(
            previous.function, current.data(), current.size()) ||
        (current != previous.original &&
         (current != previous.patch ||
          !process.WriteProtectedMemory(
              previous.function,
              previous.original.data(), previous.original.size()))))
    {
        std::wprintf(L"[vie hote] retrait garde %ls impossible.\n", name);
        return false;
    }

    bool executing = true;
    if (!process.IsAnyThreadExecutingRange(
            previous.remote, kRemoteDeathGuardRemoteSize, executing) ||
        executing)
    {
        // The entry is native again; defer only the page release until no
        // thread is inside the tiny trampoline.
        return false;
    }
    if (!process.FreeRemoteMemory(previous.remote))
        return false;
    std::wprintf(L"[vie hote] garde %ls retire.\n", name);
    state = {};
    return true;
}

template <std::size_t PatchSize>
bool SetRemoteHostGuardTarget(
    hd::TrainerProcess& process,
    RemoteHostGuard<PatchSize>& state,
    std::uint16_t target_network_id,
    const wchar_t* name)
{
    if (!state.applied)
        return false;
    if (!process.IsConnected() || process.ProcessId() != state.process_id ||
        !process.WriteMemory(
            state.remote + kRemoteDeathGuardTargetOffset,
            target_network_id))
    {
        std::wprintf(L"[vie hote] mise a jour cible garde %ls impossible.\n", name);
        return false;
    }
    state.target_network_id = target_network_id;
    return true;
}

template <typename Guard>
bool ParkRemoteHostGuardSet(
    hd::TrainerProcess& process,
    std::vector<Guard>& guards,
    const wchar_t* name)
{
    bool parked = true;
    for (Guard& guard : guards)
    {
        if (guard.target_network_id != kPeerNoHostPlayersNetworkId)
        {
            parked = SetRemoteHostGuardTarget(
                process, guard, kPeerNoHostPlayersNetworkId, name) && parked;
        }
    }
    return parked;
}

template <std::size_t PatchSize>
bool ParkRemoteHostGuard(
    hd::TrainerProcess& process,
    RemoteHostGuard<PatchSize>& guard,
    const wchar_t* name)
{
    return !guard.applied || guard.target_network_id == kPeerNoHostPlayersNetworkId ||
        SetRemoteHostGuardTarget(
            process, guard, kPeerNoHostPlayersNetworkId, name);
}

template <std::size_t PatchSize>
bool InstallRemoteHostGuard(
    hd::TrainerProcess& process,
    std::uintptr_t function,
    const std::array<std::uint8_t, PatchSize>& expected,
    std::uint16_t target_network_id,
    RemoteHostGuard<PatchSize>& state,
    const wchar_t* name,
    std::uint16_t return_stack_bytes = 8U)
{
    if (state.applied)
        return state.process_id == process.ProcessId() &&
            state.function == function &&
            state.target_network_id == target_network_id;

    RemoteHostGuard<PatchSize> next{};
    next.process_id = process.ProcessId();
    next.function = function;
    next.target_network_id = target_network_id;
    if (!process.ReadMemory(
            function, next.original.data(), next.original.size()) ||
        next.original != expected)
    {
        std::wprintf(L"[vie hote] signature %ls non reconnue.\n", name);
        return false;
    }
    next.remote = process.AllocateRemoteMemory(kRemoteDeathGuardRemoteSize);
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

    // All three paths are thiscall, returning with ret 8. ECX is the actor.
    // Only a C_player owned by the non-zero host PID is blocked. The client's
    // player, every enemy and any local ally continue through native code.
    byte(0x83); byte(0x79); byte(0x1C); byte(kActorTypePlayer);
    byte(0x0F); byte(0x85);
    const std::size_t not_player_jump = code.size();
    dword(0);
    byte(0x83); byte(0x79); byte(0x34); byte(0x00);
    byte(0x0F); byte(0x84);
    const std::size_t local_player_jump = code.size();
    dword(0);
    // AX = requested target stored outside the executable code.  FFFF means
    // every host-owned player; otherwise compare it to actor+network_id.
    byte(0x66); byte(0xA1);
    dword(static_cast<std::uint32_t>(next.remote + kRemoteDeathGuardTargetOffset));
    byte(0x66); byte(0x3D); byte(0xFE); byte(0xFF);
    byte(0x0F); byte(0x84);
    const std::size_t parked_jump = code.size();
    dword(0);
    byte(0x66); byte(0x3D); byte(0xFF); byte(0xFF);
    byte(0x0F); byte(0x84);
    const std::size_t whole_squad_jump = code.size();
    dword(0);
    byte(0x66); byte(0x3B); byte(0x41); byte(0x20);
    byte(0x0F); byte(0x85);
    const std::size_t other_player_jump = code.size();
    dword(0);
    const std::size_t block = code.size();
    byte(0x33); byte(0xC0);                  // xor eax,eax
    byte(0xC2);
    byte(static_cast<std::uint8_t>(return_stack_bytes & 0xFFU));
    byte(static_cast<std::uint8_t>(return_stack_bytes >> 8U));

    const std::size_t native = code.size();
    code.insert(code.end(), expected.begin(), expected.end());
    byte(0xE9);
    dword(relative32(
        function + expected.size(),
        next.remote + code.size() + sizeof(std::uint32_t)));
    const auto patch_jump = [&](std::size_t displacement)
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            native - (displacement + sizeof(std::int32_t)));
        std::memcpy(code.data() + displacement, &relative, sizeof(relative));
    };
    patch_jump(not_player_jump);
    patch_jump(local_player_jump);
    patch_jump(other_player_jump);
    patch_jump(parked_jump);
    const auto patch_jump_to = [&](std::size_t displacement, std::size_t target)
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            target - (displacement + sizeof(std::int32_t)));
        std::memcpy(code.data() + displacement, &relative, sizeof(relative));
    };
    patch_jump_to(whole_squad_jump, block);

    if (code.size() > kRemoteDeathGuardRemoteSize ||
        kRemoteDeathGuardTargetOffset + sizeof(target_network_id) >
            kRemoteDeathGuardRemoteSize ||
        !process.WriteMemory(next.remote, code.data(), code.size()) ||
        !process.WriteMemory(
            next.remote + kRemoteDeathGuardTargetOffset, target_network_id))
    {
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }
    next.patch.fill(0x90);
    next.patch[0] = 0xE9;
    const std::uint32_t hook_relative = relative32(next.remote, function + 5U);
    std::memcpy(next.patch.data() + 1, &hook_relative, sizeof(hook_relative));
    if (!process.WriteProtectedMemory(
            function, next.patch.data(), next.patch.size()))
    {
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }
    state = next;
    std::wprintf(
        L"[vie hote] garde %ls active fn=%08X.\n",
        name, static_cast<unsigned>(function));
    return true;
}

bool FindRemoteHostPlayerFunctions(
    hd::TrainerProcess& process,
    const std::vector<Actor>& actors,
    std::uint16_t target_network_id,
    std::vector<std::uintptr_t>& dies,
    std::vector<std::uintptr_t>& explodes,
    std::vector<std::uintptr_t>& hits)
{
    dies.clear();
    explodes.clear();
    hits.clear();
    for (const Actor& actor : actors)
    {
        if (actor.type != kActorTypePlayer || actor.network_owner == 0 ||
            (target_network_id != kPeerAllHostPlayersNetworkId &&
             actor.network_id != target_network_id))
            continue;
        std::uint32_t vtable32 = 0;
        std::uint32_t die32 = 0;
        std::uint32_t explode32 = 0;
        std::uint32_t hit32 = 0;
        if (!process.ReadMemory(actor.address, vtable32) ||
            !IsSanePointer(vtable32) ||
            !process.ReadMemory(vtable32 + kPlayerDieVtableOffset, die32) ||
            !process.ReadMemory(vtable32 + kPlayerExplodeVtableOffset, explode32) ||
            !process.ReadMemory(vtable32 + kPlayerHitVtableOffset, hit32) ||
            !process.IsReadableCodeTarget(die32) ||
            !process.IsReadableCodeTarget(explode32) ||
            !process.IsReadableCodeTarget(hit32))
        {
            continue;
        }
        dies.push_back(die32);
        explodes.push_back(explode32);
        hits.push_back(hit32);
    }
    const auto deduplicate = [](std::vector<std::uintptr_t>& functions)
    {
        std::sort(functions.begin(), functions.end());
        functions.erase(std::unique(functions.begin(), functions.end()),
            functions.end());
    };
    deduplicate(dies);
    deduplicate(explodes);
    deduplicate(hits);
    return !dies.empty() && !explodes.empty() && !hits.empty();
}

template <typename Guard>
bool RestoreRemoteHostGuardSet(
    hd::TrainerProcess& process,
    std::vector<Guard>& guards,
    const wchar_t* name)
{
    bool restored = true;
    for (Guard& guard : guards)
        restored = RestoreRemoteHostGuard(process, guard, name) && restored;
    if (restored)
        guards.clear();
    return restored;
}

template <typename Guard, std::size_t PatchSize>
bool EnsureRemoteHostGuardSet(
    hd::TrainerProcess& process,
    std::vector<Guard>& guards,
    const std::vector<std::uintptr_t>& functions,
    const std::array<std::uint8_t, PatchSize>& expected,
    std::uint16_t target_network_id,
    const wchar_t* name,
    std::uint16_t return_stack_bytes = 8U)
{
    auto guard = guards.begin();
    while (guard != guards.end())
    {
        const bool stale = guard->process_id != process.ProcessId() ||
            std::find(functions.begin(), functions.end(), guard->function) ==
                functions.end();
        if (!stale)
        {
            if (guard->target_network_id != target_network_id &&
                !SetRemoteHostGuardTarget(
                    process, *guard, target_network_id, name))
            {
                return false;
            }
            ++guard;
            continue;
        }
        if (!RestoreRemoteHostGuard(process, *guard, name))
            return false;
        guard = guards.erase(guard);
    }
    for (const std::uintptr_t function : functions)
    {
        const bool present = std::any_of(guards.begin(), guards.end(),
            [&](const Guard& item) { return item.function == function; });
        if (present)
            continue;
        guards.emplace_back();
        if (!InstallRemoteHostGuard(
                process, function, expected, target_network_id,
                guards.back(), name, return_stack_bytes))
        {
            guards.pop_back();
            return false;
        }
    }
    return true;
}

// V98 - Sante Max chez l'ami.
// L'hote ne peut rien faire pour ces soldats : leur vie appartient a CETTE
// machine. Sur ordre, le compagnon etend donc lui-meme la reserve de vie de
// ses propres soldats, puis la rend exactement telle qu'elle etait des que
// l'ordre retombe - ou des que l'hote se tait, par le delai de securite.
struct ClientHealthEntry
{
    std::uintptr_t actor = 0;
    std::int32_t resistance = 0;
    std::int32_t init_resistance = 0;
};

struct ClientExtendedHealthState
{
    DWORD process_id = 0;
    bool applied = false;
    bool reported = false;
    std::vector<ClientHealthEntry> entries;
};

bool ReadClientHealth(
    hd::TrainerProcess& process,
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

void RestoreClientExtendedHealth(
    hd::TrainerProcess& process,
    ClientExtendedHealthState& state)
{
    if (!state.applied)
        return;
    if (process.IsConnected() && process.ProcessId() == state.process_id)
    {
        for (const ClientHealthEntry& entry : state.entries)
        {
            std::uint32_t type = 0;
            if (!process.ReadMemory(entry.actor + kActorTypeOffset, type) ||
                type != kActorTypePlayer)
            {
                continue;
            }
            (void)process.WriteMemory(
                entry.actor + kPlayerInitResistanceOffset,
                entry.init_resistance);
            (void)process.WriteMemory(
                entry.actor + kPlayerResistanceOffset, entry.init_resistance);
        }
        std::wprintf(
            L"[vie locale] vie normale rendue a %u de vos soldats.\n",
            static_cast<unsigned>(state.entries.size()));
    }
    state.entries.clear();
    state.applied = false;
    state.reported = false;
}

void UpdateClientExtendedHealth(
    hd::TrainerProcess& process,
    const std::vector<Actor>& actors,
    const ClientVisualState& visual_state,
    ClientExtendedHealthState& state)
{
    if (state.process_id != process.ProcessId())
    {
        state.entries.clear();
        state.applied = false;
        state.reported = false;
        state.process_id = process.ProcessId();
    }
    if (!visual_state.extend_health_requested)
    {
        RestoreClientExtendedHealth(process, state);
        return;
    }

    const bool first_pass = !state.applied;
    unsigned touched = 0;
    unsigned refused = 0;
    for (const Actor& actor : actors)
    {
        // V101 : toutes les copies d'acteur joueur, la sienne comme celles des
        // autres PC. Chaque machine soustrait la resistance sur SA copie; si
        // une seule garde sa vie normale, c'est elle qui declenche la mort et
        // l'annonce aux autres.
        if (actor.type != kActorTypePlayer)
            continue;
        std::int32_t resistance = 0;
        std::int32_t init_resistance = 0;
        if (!ReadClientHealth(
                process, actor.address, resistance, init_resistance))
        {
            ++refused;
            continue;
        }
        const bool known = std::any_of(
            state.entries.begin(), state.entries.end(),
            [&](const ClientHealthEntry& item)
            {
                return item.actor == actor.address;
            });
        if (!known)
        {
            if (init_resistance == kExtendedHealthValue)
                continue;
            state.entries.push_back(
                ClientHealthEntry{actor.address, resistance, init_resistance});
        }
        else if (!first_pass && init_resistance == kExtendedHealthValue)
        {
            // Deja etendu : laisser la vie descendre normalement.
            ++touched;
            continue;
        }
        const std::int32_t extended = kExtendedHealthValue;
        std::int32_t verify = 0;
        if (process.WriteMemory(
                actor.address + kPlayerInitResistanceOffset, extended) &&
            process.WriteMemory(
                actor.address + kPlayerResistanceOffset, extended) &&
            process.ReadMemory(
                actor.address + kPlayerInitResistanceOffset, verify) &&
            verify == extended)
        {
            ++touched;
        }
        else
        {
            ++refused;
        }
    }
    // V102 : aligner chaque copie sur la valeur publiee par l'hote, pour que
    // les deux ecrans affichent exactement la meme vie.
    unsigned aligned = 0;
    for (std::uint8_t slot = 0; slot < visual_state.health_count; ++slot)
    {
        const PeerHealthEntry& entry = visual_state.health[slot];
        if (entry.network_id == 0 || entry.resistance <= 0)
            continue;
        for (const Actor& actor : actors)
        {
            if (actor.type != kActorTypePlayer ||
                actor.network_id != entry.network_id)
            {
                continue;
            }
            std::int32_t current = 0;
            if (process.ReadMemory(
                    actor.address + kPlayerResistanceOffset, current) &&
                current != entry.resistance &&
                process.WriteMemory(
                    actor.address + kPlayerResistanceOffset, entry.resistance))
            {
                ++aligned;
            }
            break;
        }
    }

    if (touched != 0)
        state.applied = true;
    if (state.applied && !state.reported)
    {
        state.reported = true;
        std::wprintf(
            L"[vie locale] sante etendue appliquee a %u de vos soldats "
            L"(%u refus).\n", touched, refused);
    }
    if (refused != 0 && touched == 0)
    {
        static DWORD next_warning = 0;
        const DWORD now = GetTickCount();
        if (now >= next_warning)
        {
            next_warning = now + 5000U;
            std::wprintf(
                L"[vie locale] pas encore applicable (mission en cours de "
                L"chargement ?), nouvel essai automatique.\n");
        }
    }
    // Etat periodique : « l'ordre est bien recu et voici ce qu'il produit ».
    static DWORD next_status = 0;
    const DWORD status_now = GetTickCount();
    if (status_now >= next_status)
    {
        next_status = status_now + 5000U;
        std::wprintf(
            L"[vie locale] ordre actif : %u copies etendues, %u alignees sur "
            L"l'hote, %u refus, %u memorisees.\n",
            touched, aligned, refused,
            static_cast<unsigned>(state.entries.size()));
    }
}

// Table d'ancres publiee au crochet : {acteur, x, y, z}, seize octets par
// entree, exactement la disposition attendue par le trampoline.
struct ClientPositionMaskState
{
    DWORD process_id = 0;
    std::uintptr_t hook = 0;
    std::uintptr_t remote = 0;
    std::array<std::uint8_t, kPositionMaskPatchSize> original{};
    std::array<std::uint8_t, kPositionMaskPatchSize> patch{};
    bool applied = false;
    bool armed = false;
    bool reported = false;
};

void RemoveClientPositionMask(
    hd::TrainerProcess& process,
    ClientPositionMaskState& state)
{
    if (!state.applied)
        return;
    if (process.IsConnected() && process.ProcessId() == state.process_id)
    {
        const std::uint32_t inactive = 0;
        (void)process.WriteMemory(
            state.remote + kPositionMaskActiveOffset, inactive);
        std::array<std::uint8_t, kPositionMaskPatchSize> current{};
        if (process.ReadMemory(
                state.hook, current.data(), current.size()) &&
            current == state.patch)
        {
            (void)process.WriteProtectedMemory(
                state.hook, state.original.data(), state.original.size());
        }
        bool executing = true;
        for (unsigned attempt = 0; attempt < 50; ++attempt)
        {
            if (!process.IsAnyThreadExecutingRange(
                    state.remote, kPositionMaskRemoteSize, executing) ||
                !executing)
            {
                break;
            }
            Sleep(2);
        }
        if (!executing)
            (void)process.FreeRemoteMemory(state.remote);
        std::wprintf(L"[position] gel de la position publiee retire.\n");
    }
    state = ClientPositionMaskState{};
}

bool InstallClientPositionMask(
    hd::TrainerProcess& process,
    ClientPositionMaskState& state)
{
    hd::RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        module.image_size <= kHumanPositionSendRva + kPositionMaskPatchSize)
    {
        return false;
    }
    ClientPositionMaskState next{};
    next.process_id = process.ProcessId();
    next.hook = module.base_address + kHumanPositionSendRva;
    constexpr std::array<std::uint8_t, kPositionMaskPatchSize> kExpected{
        0x8B, 0x4D, 0xF4,
        0x8D, 0x83, 0x10, 0x02, 0x00, 0x00};
    if (!process.ReadMemory(
            next.hook, next.original.data(), next.original.size()) ||
        next.original != kExpected)
    {
        std::wprintf(
            L"[attention] position: signature d'envoi non reconnue.\n");
        return false;
    }
    next.remote = process.AllocateRemoteMemory(kPositionMaskRemoteSize);
    if (!IsSanePointer(next.remote))
        return false;

    std::vector<std::uint8_t> code;
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
        std::memcpy(code.data() + displacement, &relative, sizeof(relative));
    };

    byte(0x9C);                              // pushfd
    byte(0x60);                              // pushad
    byte(0x83); byte(0x3D); absolute(kPositionMaskActiveOffset); byte(0x01);
    const std::size_t inactive = near_jump(0x85);
    byte(0x8B); byte(0x0D); absolute(kPositionMaskCountOffset);
    byte(0xBA); absolute(kPositionMaskEntriesOffset);
    const std::size_t scan = code.size();
    byte(0x85); byte(0xC9);
    const std::size_t no_match = near_jump(0x84);
    byte(0x3B); byte(0x1A);                  // cmp ebx,[edx]
    const std::size_t matched = near_jump(0x84);
    byte(0x83); byte(0xC2); byte(0x10);
    byte(0x49);
    byte(0xE9);
    const std::size_t repeat_scan = code.size();
    dword(0);
    const std::size_t write_anchor = code.size();
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        byte(0x8B); byte(0x42);
        byte(static_cast<std::uint8_t>(4U + axis * sizeof(float)));
        byte(0x89); byte(0x45);
        byte(static_cast<std::uint8_t>(0xF0U + axis * sizeof(float)));
    }
    const std::size_t done = code.size();
    patch_jump(inactive, done);
    patch_jump(no_match, done);
    patch_jump(matched, write_anchor);
    const std::int32_t scan_relative = static_cast<std::int32_t>(
        scan - (repeat_scan + sizeof(std::int32_t)));
    std::memcpy(code.data() + repeat_scan, &scan_relative,
        sizeof(scan_relative));
    byte(0x61);                              // popad
    byte(0x9D);                              // popfd
    code.insert(code.end(), next.original.begin(), next.original.end());
    byte(0xE9);
    const std::int32_t back = static_cast<std::int32_t>(
        (next.hook + next.original.size()) -
        (next.remote + code.size() + sizeof(std::int32_t)));
    dword(static_cast<std::uint32_t>(back));

    const std::uint32_t zero = 0;
    if (code.size() > kPositionMaskActiveOffset ||
        !process.WriteMemory(next.remote, code.data(), code.size()) ||
        !process.WriteMemory(next.remote + kPositionMaskActiveOffset, zero) ||
        !process.WriteMemory(next.remote + kPositionMaskCountOffset, zero))
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
    next.applied = true;
    state = next;
    std::wprintf(L"[position] crochet d'envoi installe.\n");
    return true;
}

// Capture l'ancre de chaque soldat local et arme le crochet. L'ancre est prise
// une seule fois, a l'arrivee de l'ordre : c'est ce qui fige la position vue
// par les autres PC pendant que le joueur continue de se deplacer normalement
// chez lui.
void UpdateClientPositionMask(
    hd::TrainerProcess& process,
    const std::vector<Actor>& actors,
    const ClientVisualState& visual_state,
    ClientPositionMaskState& state)
{
    if (state.applied && state.process_id != process.ProcessId())
        state = ClientPositionMaskState{};
    if (!visual_state.mask_position_requested)
    {
        RemoveClientPositionMask(process, state);
        return;
    }
    if (!state.applied && !InstallClientPositionMask(process, state))
        return;
    if (state.armed)
        return;

    struct Entry
    {
        std::uint32_t actor = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };
    std::array<Entry, kPositionMaskMaximumActors> entries{};
    std::uint32_t count = 0;
    for (const Actor& actor : actors)
    {
        if (actor.type != kActorTypePlayer || actor.network_owner != 0)
            continue;
        if (count >= entries.size())
            break;
        std::uint32_t frame = 0;
        Vector3 position{};
        if (!process.ReadMemory(actor.address + kActorFrameOffset, frame) ||
            !IsSanePointer(frame) ||
            !process.ReadMemory(
                frame + kFrameWorldPositionOffsetLocal, position))
        {
            continue;
        }
        entries[count].actor = static_cast<std::uint32_t>(actor.address);
        entries[count].x = position.x;
        entries[count].y = position.y;
        entries[count].z = position.z;
        ++count;
    }
    if (count == 0)
        return;
    const std::uint32_t active = 1;
    if (!process.WriteMemory(
            state.remote + kPositionMaskEntriesOffset, entries.data(),
            sizeof(Entry) * count) ||
        !process.WriteMemory(state.remote + kPositionMaskCountOffset, count) ||
        !process.WriteMemory(state.remote + kPositionMaskActiveOffset, active))
    {
        return;
    }
    state.armed = true;
    if (!state.reported)
    {
        state.reported = true;
        std::wprintf(
            L"[position] position publiee figee pour %u de vos soldats.\n",
            count);
    }
}

void UpdateRemoteHostDeathMirror(
    hd::TrainerProcess& process,
    const std::vector<Actor>& actors,
    const ClientVisualState& visual_state,
    ClientDeathMirrorState& state)
{
    if (!visual_state.suppress_host_player_damage_requested)
        (void)ParkRemoteHostGuardSet(process, state.player_hit, L"C_player::Hit");
    if (!visual_state.suppress_host_player_death_requested)
    {
        (void)ParkRemoteHostGuardSet(process, state.player_die, L"C_player::Die");
        (void)ParkRemoteHostGuard(process, state.base_die, L"C_human::Die");
        (void)ParkRemoteHostGuardSet(process, state.player_explode, L"C_player::Explode");
    }
    if ((!visual_state.suppress_host_player_death_requested &&
         !visual_state.suppress_host_player_damage_requested) ||
        visual_state.life_target_network_id == 0)
    {
        // Do not unhook while a game thread can still be in a trampoline.
        // Parking changes behavior to native immediately and keeps hook state
        // coherent for the next realtime checkbox change.
        (void)ParkRemoteHostGuardSet(process, state.player_die, L"C_player::Die");
        (void)ParkRemoteHostGuard(process, state.base_die, L"C_human::Die");
        (void)ParkRemoteHostGuardSet(process, state.player_explode, L"C_player::Explode");
        (void)ParkRemoteHostGuardSet(process, state.player_hit, L"C_player::Hit");
        state.active_reported = false;
        return;
    }

    hd::RemoteModuleInfo module{};
    std::vector<std::uintptr_t> player_dies;
    std::vector<std::uintptr_t> player_explodes;
    std::vector<std::uintptr_t> player_hits;
    if (!process.GetMainModuleInfo(module) ||
        !FindRemoteHostPlayerFunctions(
            process, actors, kPeerAllHostPlayersNetworkId,
            player_dies, player_explodes, player_hits) ||
        kHumanDieRva + kRemoteDeathGuardPatchSize > module.image_size)
    {
        return;
    }
    const std::uintptr_t base_die = module.base_address + kHumanDieRva;
    constexpr std::array<std::uint8_t, kRemoteDeathGuardPatchSize> kPlayerDieExpected{
        0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xBC, 0x00, 0x00, 0x00};
    constexpr std::array<std::uint8_t, kRemoteDeathGuardPatchSize> kBaseDieExpected{
        0x83, 0xEC, 0x44, 0x53, 0x55, 0x8B, 0xE9, 0x56, 0x57};
    // Ten complete instructions bytes: the ninth byte was the first half of
    // `xor ecx, ecx` in V76.  Copying it alone corrupted the Client's native
    // path and caused the bazooka crash.
    constexpr std::array<std::uint8_t, kRemoteExplodeGuardPatchSize> kExplodeExpected{
        0x83, 0xEC, 0x44, 0x53, 0x55, 0x56, 0x8B, 0xF1, 0x33, 0xC9};
    constexpr std::array<std::uint8_t, kRemoteHitGuardPatchSize> kHitExpected{
        0x83, 0xEC, 0x6C, 0x53, 0x55};

    if (state.base_die.applied &&
        (state.base_die.process_id != process.ProcessId() ||
         state.base_die.function != base_die) &&
        !RestoreRemoteHostGuard(process, state.base_die, L"C_human::Die"))
    {
        return;
    }
    if (state.base_die.applied &&
        state.base_die.target_network_id != visual_state.life_target_network_id &&
        !SetRemoteHostGuardTarget(
            process, state.base_die, visual_state.life_target_network_id,
            L"C_human::Die"))
    {
        return;
    }

    const bool death_active = !visual_state.suppress_host_player_death_requested ||
        (EnsureRemoteHostGuardSet(
             process, state.player_die, player_dies, kPlayerDieExpected,
             visual_state.life_target_network_id, L"C_player::Die") &&
         (state.base_die.applied || InstallRemoteHostGuard(
             process, base_die, kBaseDieExpected, visual_state.life_target_network_id,
             state.base_die, L"C_human::Die")) &&
         EnsureRemoteHostGuardSet(
             process, state.player_explode, player_explodes, kExplodeExpected,
             visual_state.life_target_network_id, L"C_player::Explode"));
    const bool damage_active = !visual_state.suppress_host_player_damage_requested ||
        EnsureRemoteHostGuardSet(
            process, state.player_hit, player_hits, kHitExpected,
            visual_state.life_target_network_id, L"C_player::Hit", 0x14U);
    const bool active = death_active && damage_active;
    if (active && !state.active_reported)
    {
        std::wprintf(
            L"[vie hote] actif: le Client ignore les degats et morts du soldat hote choisi.\n");
        state.active_reported = true;
    }
}

bool RestoreClientHostRevive(
    hd::TrainerProcess& process,
    ClientHostReviveState& state)
{
    if (!state.applied)
        return true;
    const ClientHostReviveState previous = state;
    if (!process.IsConnected() || process.ProcessId() != previous.process_id)
    {
        state = {};
        return true;
    }
    std::array<std::uint8_t, kHumanTickPatchSize> current{};
    if (!process.ReadMemory(previous.function, current.data(), current.size()) ||
        (current != previous.original &&
         (current != previous.patch || !process.WriteProtectedMemory(
             previous.function, previous.original.data(), previous.original.size()))))
    {
        return false;
    }
    bool executing = true;
    if (!process.IsAnyThreadExecutingRange(
            previous.remote, kClientReviveRemoteSize, executing) || executing)
    {
        return false;
    }
    if (!process.FreeRemoteMemory(previous.remote))
        return false;
    state = {};
    return true;
}

bool QueueClientHostRevive(
    hd::TrainerProcess& process,
    const std::vector<Actor>& actors,
    std::uint16_t target_network_id,
    std::uint32_t sequence,
    ClientHostReviveState& state)
{
    if (target_network_id == 0 || target_network_id == kPeerAllHostPlayersNetworkId ||
        sequence == 0 || state.applied)
    {
        return false;
    }
    const auto target_it = std::find_if(
        actors.begin(), actors.end(), [&](const Actor& actor)
        {
            return actor.type == kActorTypePlayer && actor.network_owner != 0 &&
                actor.network_id == target_network_id;
        });
    if (target_it == actors.end())
        return false;

    hd::RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        module.image_size <= kHumanTickRva + kHumanTickPatchSize)
    {
        return false;
    }
    ClientHostReviveState next{};
    next.process_id = process.ProcessId();
    next.function = module.base_address + kHumanTickRva;
    next.target = target_it->address;
    next.sequence = sequence;
    constexpr std::array<std::uint8_t, kHumanTickPatchSize> kTickExpected{
        0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x6C, 0x03, 0x00, 0x00};
    constexpr std::array<std::uint8_t, 9> kSetActiveExpected{
        0x53, 0x8B, 0x5C, 0x24, 0x08, 0x56, 0x8B, 0xF1, 0x57};
    std::uint32_t vtable = 0;
    std::uint32_t set_active = 0;
    std::array<std::uint8_t, kSetActiveExpected.size()> signature{};
    if (!process.ReadMemory(next.function, next.original.data(), next.original.size()) ||
        next.original != kTickExpected ||
        !process.ReadMemory(next.target, vtable) || !IsSanePointer(vtable) ||
        !process.ReadMemory(vtable + kPlayerSetActiveVtableOffset, set_active) ||
        !process.IsReadableCodeTarget(set_active) ||
        !process.ReadMemory(set_active, signature.data(), signature.size()) ||
        signature != kSetActiveExpected)
    {
        return false;
    }
    next.remote = process.AllocateRemoteMemory(kClientReviveRemoteSize);
    if (!IsSanePointer(next.remote))
        return false;
    const auto relative32 = [](std::uintptr_t target, std::uintptr_t next_ip)
    {
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<std::intptr_t>(target) - static_cast<std::intptr_t>(next_ip)));
    };
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
        dword(static_cast<std::uint32_t>(next.remote + offset));
    };
    const auto near_jump = [&](std::uint8_t condition)
    {
        byte(0x0F); byte(condition);
        const std::size_t displacement = code.size(); dword(0);
        return displacement;
    };
    const auto patch_jump = [&](std::size_t displacement, std::size_t target)
    {
        const std::int32_t relative = static_cast<std::int32_t>(
            target - (displacement + sizeof(std::int32_t)));
        std::memcpy(code.data() + displacement, &relative, sizeof(relative));
    };
    byte(0x9C); byte(0x60);                  // pushfd, pushad
    byte(0x83); byte(0x3D); slot(kClientReviveStateOffset);
    byte(static_cast<std::uint8_t>(kClientReviveQueued));
    const std::size_t not_queued = near_jump(0x85);
    byte(0xA1); slot(kClientReviveTargetOffset); // eax=target
    byte(0x85); byte(0xC0);
    const std::size_t rejected_target = near_jump(0x84);
    byte(0xC7); byte(0x80); dword(kHumanStayModeOffset); dword(kHumanStayModeAlive);
    byte(0xC7); byte(0x80); dword(kPlayerModeOffset); dword(kPlayerModeProgram);
    byte(0x8B); byte(0x10);                  // edx=vtable
    byte(0x8B); byte(0x92); dword(kPlayerSetActiveVtableOffset);
    byte(0x85); byte(0xD2);
    const std::size_t rejected_method = near_jump(0x84);
    byte(0x6A); byte(0x01); byte(0x6A); byte(0x01);
    byte(0x8B); byte(0xC8); byte(0xFF); byte(0xD2);
    byte(0xC7); byte(0x05); slot(kClientReviveStateOffset); dword(kClientReviveCompleted);
    byte(0xE9); const std::size_t success_done = code.size(); dword(0);
    const std::size_t rejected = code.size();
    byte(0xC7); byte(0x05); slot(kClientReviveStateOffset); dword(kClientReviveRejected);
    const std::size_t done = code.size();
    patch_jump(not_queued, done);
    patch_jump(rejected_target, rejected);
    patch_jump(rejected_method, rejected);
    patch_jump(success_done, done);
    byte(0x61); byte(0x9D);
    code.insert(code.end(), kTickExpected.begin(), kTickExpected.end());
    byte(0xE9); dword(relative32(next.function + kHumanTickPatchSize,
        next.remote + code.size() + sizeof(std::uint32_t)));
    if (code.size() >= kClientReviveStateOffset ||
        !process.WriteMemory(next.remote, code.data(), code.size()) ||
        !process.WriteMemory(next.remote + kClientReviveTargetOffset,
            static_cast<std::uint32_t>(next.target)) ||
        !process.WriteMemory(next.remote + kClientReviveStateOffset, kClientReviveQueued))
    {
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }
    next.patch.fill(0x90); next.patch[0] = 0xE9;
    const std::uint32_t hook_relative = relative32(next.remote, next.function + 5U);
    std::memcpy(next.patch.data() + 1, &hook_relative, sizeof(hook_relative));
    if (!process.WriteProtectedMemory(next.function, next.patch.data(), next.patch.size()))
    {
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }
    state = next;
    return true;
}

void UpdateClientHostRevive(
    hd::TrainerProcess& process,
    const std::vector<Actor>& actors,
    ClientVisualState& visual_state,
    ClientHostReviveState& state)
{
    if (state.applied)
    {
        std::uint32_t result = 0;
        if (process.ReadMemory(state.remote + kClientReviveStateOffset, result) &&
            result != kClientReviveQueued)
        {
            std::wprintf(L"[vie hote] F12 Client sequence=%u resultat=%u.\n",
                state.sequence, result);
            (void)RestoreClientHostRevive(process, state);
        }
        return;
    }
    if (visual_state.pending_revive_sequence == 0)
        return;
    const std::uint32_t sequence = visual_state.pending_revive_sequence;
    visual_state.pending_revive_sequence = 0;
    if (!QueueClientHostRevive(process, actors,
            visual_state.life_target_network_id, sequence, state))
    {
        std::wprintf(L"[vie hote] F12 Client non applique (acteur indisponible).\n");
    }
}

#endif

unsigned int ApplyAuthority(
    hd::TrainerProcess& process,
    const std::vector<Actor>& actors,
    std::uint32_t desired_owner,
    unsigned int& enemy_count)
{
    unsigned int changed = 0;
    enemy_count = 0;
    for (const Actor& actor : actors)
    {
        if (actor.type != kActorTypeEnemy)
            continue;

        ++enemy_count;
        if (actor.network_owner == desired_owner)
            continue;

        if (process.WriteMemory(
                actor.address + kActorNetworkOwnerOffset, desired_owner))
        {
            ++changed;
        }
    }
    return changed;
}

void PrintUsage()
{
    std::wprintf(
        L"HD AI Authority V105 - %ls\n"
        L"\n"
        L"Laissez cette fenetre ouverte pendant toute la partie reseau.\n"
        L"Demarrez-la avant de lancer la mission, sur le PC %ls.\n"
        L"Elle attend automatiquement hde.exe et la partie reseau.\n"
#if defined(HD_AI_AUTHORITY_CLIENT)
        L"Elle recoit aussi le masquage visuel demande par le trainer de l'hote.\n"
#endif
        L"\n"
        L"Fermer la fenetre (ou Ctrl+C) arrete l'application des regles.\n\n",
        RoleName(), RoleName());
}
}

int wmain()
{
    PrintUsage();

    hd::TrainerProcess process;
    DWORD attached_pid = 0;
    DWORD last_report_tick = 0;
    bool announced_waiting_for_game = false;
    bool announced_waiting_for_network = false;
    bool announced_active = false;
#if defined(HD_AI_AUTHORITY_CLIENT)
    ClientVisualState visual_state{};
    ClientDeathMirrorState death_mirror_state{};
    ClientHostReviveState client_revive_state{};
    ClientExtendedHealthState client_health_state{};
    ClientPositionMaskState client_position_state{};
    if (!InitialisePeerVisualReceiver(visual_state))
    {
        std::wprintf(
            L"[attention] canal visuel LAN indisponible; l'autorite IA reste active.\n");
    }
#endif

    for (;;)
    {
#if defined(HD_AI_AUTHORITY_CLIENT)
        PollPeerVisualCommands(visual_state);
#endif
        if (!process.RefreshConnection(L"hde.exe"))
        {
            if (!announced_waiting_for_game)
            {
                std::wprintf(L"[attente] hde.exe n'est pas encore lance.\n");
                announced_waiting_for_game = true;
            }
            attached_pid = 0;
            announced_waiting_for_network = false;
            announced_active = false;
#if defined(HD_AI_AUTHORITY_CLIENT)
            // hde.exe is gone, so no remote frame may be touched. Keeping the
            // command state only is safe; its timeout will reveal at next game.
            visual_state.hidden_frames.clear();
            visual_state.hide_reported = false;
            death_mirror_state = {};
            client_revive_state = {};
            client_health_state = {};
            client_position_state = {};
#endif
            Sleep(500);
            continue;
        }

        announced_waiting_for_game = false;
        if (process.ProcessId() != attached_pid)
        {
            attached_pid = process.ProcessId();
            announced_waiting_for_network = false;
            announced_active = false;
            std::wprintf(
                L"[ok] hde.exe detecte (PID %lu). Attente de la mission reseau...\n",
                static_cast<unsigned long>(attached_pid));
        }

        std::vector<Actor> actors;
        if (!ReadActors(process, actors) || !HasRemotePlayer(actors))
        {
#if defined(HD_AI_AUTHORITY_CLIENT)
            RestoreRemoteHostModels(process, visual_state);
            (void)RestoreRemoteHostGuardSet(
                process, death_mirror_state.player_die, L"C_player::Die");
            (void)RestoreRemoteHostGuard(
                process, death_mirror_state.base_die, L"C_human::Die");
            (void)RestoreRemoteHostGuardSet(
                process, death_mirror_state.player_explode, L"C_player::Explode");
            (void)RestoreRemoteHostGuardSet(
                process, death_mirror_state.player_hit, L"C_player::Hit");
            (void)RestoreClientHostRevive(process, client_revive_state);
            death_mirror_state.active_reported = false;
#endif
            if (!announced_waiting_for_network)
            {
                std::wprintf(
                    L"[attente] aucune partie reseau a deux joueurs detectee.\n");
                announced_waiting_for_network = true;
            }
            announced_active = false;
            Sleep(200);
            continue;
        }

#if defined(HD_AI_AUTHORITY_HOST)
        const std::uint32_t desired_owner = 0;
#else
        const std::uint32_t desired_owner = FindPeerPid(actors);
#endif

        unsigned int enemy_count = 0;
        const unsigned int changed =
            ApplyAuthority(process, actors, desired_owner, enemy_count);
#if defined(HD_AI_AUTHORITY_CLIENT)
        ApplyPeerVisualMask(process, actors, visual_state);
        UpdateClientExtendedHealth(
            process, actors, visual_state, client_health_state);
        UpdateClientPositionMask(
            process, actors, visual_state, client_position_state);
        UpdateRemoteHostDeathMirror(
            process, actors, visual_state, death_mirror_state);
        UpdateClientHostRevive(
            process, actors, visual_state, client_revive_state);
#endif
        const DWORD now = GetTickCount();
        if (!announced_active || changed != 0 ||
            static_cast<DWORD>(now - last_report_tick) >= 5000U)
        {
            std::wprintf(
                L"[actif] %u ennemis: %u correction(s), proprietaire IA=%08lX\n",
                enemy_count, changed,
                static_cast<unsigned long>(desired_owner));
            announced_active = true;
            last_report_tick = now;
        }

        Sleep(100);
    }
}
