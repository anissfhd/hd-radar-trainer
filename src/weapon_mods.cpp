#include "weapon_mods.h"
#include "diagnostics.h"

#include <Windows.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

namespace hd
{
namespace
{
// Actor and inventory fields confirmed in the supported hde.exe x86 build.
constexpr std::uintptr_t kActorFrameOffset = 0x28;
constexpr std::uintptr_t kInventoryBeginOffset = 0x5C;
constexpr std::uintptr_t kInventoryEndOffset = 0x60;
constexpr std::uintptr_t kEquippedWeaponOffset = 0x244;
constexpr std::uintptr_t kSelectedIndexOffset = 0x258;
constexpr std::uintptr_t kShootCountdownOffset = 0x260;
constexpr std::uintptr_t kFrameActorBackReferenceOffset = 0x80;

// C_inventory_item is 0x20 bytes. The previous implementation incorrectly
// treated +0x2C and +0x3C, which are outside this object, as weapon fields.
constexpr std::uintptr_t kItemIdOffset = 0x08;
constexpr std::uintptr_t kItemReserveOffset = 0x18;
constexpr std::uintptr_t kItemBulletsOffset = 0x1C;

// Global C_table pointers and edition selector, expressed as module RVAs.
constexpr std::uintptr_t kWeaponTablePointerRva = 0x0010AAD0;
constexpr std::uintptr_t kGameModeRva = 0x001086E0;

// C_table and its packed S_desc_item layout (Insanity3D Tabler2).
constexpr std::uintptr_t kTableNumItemsOffset = 0x0C;
constexpr std::uintptr_t kTableDescriptorsOffset = 0x20;
constexpr std::uintptr_t kTableDataOffset = 0x24;
constexpr std::uintptr_t kTableDataSizeOffset = 0x28;
constexpr std::size_t kTableDescriptorSize = 8;
constexpr std::uint8_t kTableTypeInt = 2;
constexpr std::uint8_t kTableTypeFloat = 3;
constexpr std::uint8_t kTableTypeEnum = 4;

constexpr std::uint32_t kFireModeProperty = 0x0C;
constexpr std::array<std::uint32_t, 6> kSpreadProperties{
    0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22};
constexpr std::array<std::uint32_t, 3> kRecoilProperties{0x23, 0x24, 0x25};

// Fullhands can legitimately populate more than 64 Ultimate Mod catalog
// entries. Keep weapon-context resolution available for that native vector.
constexpr std::uint32_t kMaximumInventoryItems = 256;
constexpr std::uint32_t kMaximumTableProperties = 4096;
constexpr std::uint32_t kMaximumWeaponDefinitions = 4096;
constexpr std::int32_t kMaximumSaneAmmo = 2'000'000;
constexpr std::int32_t kInfiniteAmmo = 1'000'000;

// C_human::UseItem has already read the held-fire input at this point.  The
// five overwritten bytes are `test al,al / je ready / first byte of mov`.
// The trampoline reproduces that native path for every actor except the one
// whose currently equipped weapon has passed rapid-fire validation.
constexpr std::uintptr_t kUseItemAnimationGateRva = 0x0000'A02E;
constexpr std::size_t kUseItemAnimationGatePatchSize = 5;
constexpr std::size_t kRapidFireGateRemoteSize = 0x80;
constexpr std::size_t kRapidFireGateActorOffset = 0x60;

#pragma pack(push, 1)
struct RemoteTableDescriptor
{
    std::uint32_t offset = 0;
    std::uint8_t type = 0;
    std::uint8_t max_string_size = 0;
    std::uint16_t array_length = 0;
};
#pragma pack(pop)
static_assert(sizeof(RemoteTableDescriptor) == kTableDescriptorSize);

struct WeaponTable
{
    std::uintptr_t table = 0;
    std::uintptr_t descriptors = 0;
    std::uintptr_t data = 0;
    std::uint32_t data_size = 0;
    std::uint32_t property_count = 0;
};

struct WeaponContext
{
    DWORD process_id = 0;
    RemoteModuleInfo module{};
    std::uintptr_t actor = 0;
    std::uintptr_t frame = 0;
    std::uintptr_t equipped_weapon = 0;
    std::uintptr_t item = 0;
    std::uintptr_t item_vtable = 0;
    std::uint32_t item_id = 0;
    std::int32_t reserve = 0;
    std::int32_t bullets = 0;
    WeaponTable weapon_table{};
};

struct StableValuePatch
{
    std::uintptr_t address = 0;
    std::uint32_t original = 0;
    bool applied = false;
};

struct ModifierState
{
    WeaponContext context{};
    std::int32_t original_reserve = 0;
    std::int32_t original_bullets = 0;
    bool context_captured = false;
    bool rapid_applied = false;
    std::array<StableValuePatch, 9> stable_patches{};
    bool stable_applied = false;
};

ModifierState g_state{};

struct RapidFireGateState
{
    DWORD process_id = 0;
    std::uintptr_t hook = 0;
    std::uintptr_t remote = 0;
    std::array<std::uint8_t, kUseItemAnimationGatePatchSize> original{};
    std::array<std::uint8_t, kUseItemAnimationGatePatchSize> patch{};
    bool applied = false;
};

RapidFireGateState g_rapid_gate{};

bool IsSanePointer(std::uintptr_t address)
{
    return address >= 0x10000U && address <= 0x7FFF'FFFFU;
}

bool IsInsideModule(
    std::uintptr_t address, const RemoteModuleInfo& module, std::size_t size = 1)
{
    if (module.base_address == 0 || module.image_size == 0 || size == 0 ||
        address < module.base_address)
    {
        return false;
    }

    const std::uintptr_t offset = address - module.base_address;
    return offset <= module.image_size && size <= module.image_size - offset;
}

void ClearState()
{
    g_state = {};
}

bool SameLiveProcess(const TrainerProcess& process)
{
    return process.IsConnected() && g_state.context_captured &&
        g_state.context.process_id != 0 &&
        g_state.context.process_id == process.ProcessId();
}

bool ResolveTableValueAddress(
    TrainerProcess& process,
    const WeaponTable& table,
    std::uint32_t property,
    std::uint32_t array_index,
    std::uint8_t expected_type,
    std::size_t element_size,
    std::uintptr_t& address)
{
    address = 0;
    if (property >= table.property_count || !IsSanePointer(table.descriptors) ||
        !IsSanePointer(table.data) || element_size == 0)
    {
        return false;
    }

    RemoteTableDescriptor descriptor{};
    const std::uintptr_t descriptor_address = table.descriptors +
        static_cast<std::uintptr_t>(property) * kTableDescriptorSize;
    if (!process.ReadMemory(descriptor_address, descriptor) ||
        descriptor.type != expected_type ||
        descriptor.offset == std::numeric_limits<std::uint32_t>::max() ||
        descriptor.array_length == 0 || array_index >= descriptor.array_length)
    {
        return false;
    }

    const std::uint64_t relative = static_cast<std::uint64_t>(descriptor.offset) +
        static_cast<std::uint64_t>(array_index) * element_size;
    if (relative > table.data_size || element_size > table.data_size - relative)
        return false;

    const std::uint64_t resolved =
        static_cast<std::uint64_t>(table.data) + relative;
    if (resolved > std::numeric_limits<std::uintptr_t>::max() ||
        !IsSanePointer(static_cast<std::uintptr_t>(resolved)))
    {
        return false;
    }

    address = static_cast<std::uintptr_t>(resolved);
    return true;
}

bool ResolveWeaponTable(
    TrainerProcess& process,
    const RemoteModuleInfo& module,
    std::uint32_t item_id,
    WeaponTable& table)
{
    table = {};
    if (item_id >= kMaximumWeaponDefinitions ||
        !IsInsideModule(kGameModeRva + module.base_address, module, 4) ||
        !IsInsideModule(kWeaponTablePointerRva + module.base_address, module, 8))
    {
        return false;
    }

    std::uint32_t game_mode = 0;
    if (!process.ReadMemory(module.base_address + kGameModeRva, game_mode))
        return false;

    const std::uint32_t table_index =
        game_mode >= 6 && game_mode != 9 ? 1U : 0U;
    if (!process.ReadMemory(
            module.base_address + kWeaponTablePointerRva + table_index * 4U,
            table.table) ||
        !IsSanePointer(table.table) ||
        !process.ReadMemory(
            table.table + kTableNumItemsOffset, table.property_count) ||
        !process.ReadMemory(
            table.table + kTableDescriptorsOffset, table.descriptors) ||
        !process.ReadMemory(table.table + kTableDataOffset, table.data) ||
        !process.ReadMemory(table.table + kTableDataSizeOffset, table.data_size) ||
        table.property_count <= kRecoilProperties.back() ||
        table.property_count > kMaximumTableProperties ||
        !IsSanePointer(table.descriptors) || !IsSanePointer(table.data) ||
        table.data_size == 0 || table.data_size > 64U * 1024U * 1024U)
    {
        table = {};
        return false;
    }

    std::uintptr_t fire_mode_address = 0;
    std::uint8_t fire_mode = 0;
    if (!ResolveTableValueAddress(
            process, table, kFireModeProperty, item_id, kTableTypeEnum,
            sizeof(fire_mode), fire_mode_address) ||
        !process.ReadMemory(fire_mode_address, fire_mode) ||
        (fire_mode != 1 && fire_mode != 2))
    {
        table = {};
        return false;
    }

    return true;
}

bool ResolveWeaponContext(
    TrainerProcess& process, std::uintptr_t actor, WeaponContext& context)
{
    context = {};
    context.process_id = process.ProcessId();
    context.actor = actor;
    if (!process.IsConnected() || context.process_id == 0 ||
        !IsSanePointer(actor) || !process.GetMainModuleInfo(context.module))
    {
        return false;
    }

    std::uintptr_t inventory_begin = 0;
    std::uintptr_t inventory_end = 0;
    std::int32_t selected_index = -1;
    if (!process.ReadMemory(actor + kActorFrameOffset, context.frame) ||
        !process.ReadMemory(
            actor + kEquippedWeaponOffset, context.equipped_weapon) ||
        !process.ReadMemory(actor + kInventoryBeginOffset, inventory_begin) ||
        !process.ReadMemory(actor + kInventoryEndOffset, inventory_end) ||
        !process.ReadMemory(actor + kSelectedIndexOffset, selected_index) ||
        !IsSanePointer(context.frame) ||
        !IsSanePointer(context.equipped_weapon) ||
        !IsSanePointer(inventory_begin) || inventory_end < inventory_begin ||
        (inventory_end - inventory_begin) % sizeof(std::uint32_t) != 0)
    {
        return false;
    }

    const std::uintptr_t inventory_count =
        (inventory_end - inventory_begin) / sizeof(std::uint32_t);
    if (inventory_count == 0 || inventory_count > kMaximumInventoryItems ||
        selected_index < 0 ||
        static_cast<std::uintptr_t>(selected_index) >= inventory_count)
    {
        return false;
    }

    std::uintptr_t frame_actor = 0;
    if (!process.ReadMemory(
            context.frame + kFrameActorBackReferenceOffset, frame_actor) ||
        frame_actor != actor ||
        !process.ReadMemory(
            inventory_begin + static_cast<std::uintptr_t>(selected_index) * 4U,
            context.item) ||
        !IsSanePointer(context.item) ||
        !process.ReadMemory(context.item, context.item_vtable) ||
        !IsInsideModule(context.item_vtable, context.module, sizeof(std::uint32_t)) ||
        !process.ReadMemory(context.item + kItemIdOffset, context.item_id) ||
        !process.ReadMemory(context.item + kItemReserveOffset, context.reserve) ||
        !process.ReadMemory(context.item + kItemBulletsOffset, context.bullets) ||
        context.item_id >= kMaximumWeaponDefinitions || context.reserve < 0 ||
        context.bullets < 0 || context.reserve > kMaximumSaneAmmo ||
        context.bullets > kMaximumSaneAmmo ||
        !ResolveWeaponTable(
            process, context.module, context.item_id, context.weapon_table))
    {
        context = {};
        return false;
    }

    return true;
}

bool ContextMatches(const WeaponContext& left, const WeaponContext& right)
{
    return left.process_id == right.process_id && left.actor == right.actor &&
        left.item == right.item && left.item_vtable == right.item_vtable &&
        left.item_id == right.item_id &&
        left.weapon_table.table == right.weapon_table.table &&
        left.weapon_table.data == right.weapon_table.data;
}

bool CapturedItemStillMatches(TrainerProcess& process)
{
    if (!SameLiveProcess(process) || !IsSanePointer(g_state.context.item))
        return false;

    std::uintptr_t vtable = 0;
    std::uint32_t item_id = 0;
    return process.ReadMemory(g_state.context.item, vtable) &&
        process.ReadMemory(
            g_state.context.item + kItemIdOffset, item_id) &&
        vtable == g_state.context.item_vtable &&
        item_id == g_state.context.item_id;
}

void ClearRapidFireGate()
{
    g_rapid_gate = {};
}

void RestoreRapidFireGate(TrainerProcess& process)
{
    if (!g_rapid_gate.applied)
        return;
    if (!process.IsConnected() ||
        process.ProcessId() != g_rapid_gate.process_id)
    {
        ClearRapidFireGate();
        return;
    }

    (void)process.WriteProtectedMemory(
        g_rapid_gate.hook,
        g_rapid_gate.original.data(), g_rapid_gate.original.size());
    std::array<std::uint8_t, kUseItemAnimationGatePatchSize> current{};
    const bool restored = process.ReadMemory(
        g_rapid_gate.hook, current.data(), current.size()) &&
        current == g_rapid_gate.original;
    if (!restored)
        return;

    bool idle = false;
    for (int attempt = 0; attempt < 250 && !idle; ++attempt)
    {
        bool executing = true;
        idle = process.IsAnyThreadExecutingRange(
            g_rapid_gate.remote, kRapidFireGateRemoteSize, executing) &&
            !executing;
        if (!idle)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (idle && process.FreeRemoteMemory(g_rapid_gate.remote))
        ClearRapidFireGate();
}

bool UpdateRapidFireGate(TrainerProcess& process, std::uintptr_t actor)
{
    constexpr std::array<std::uint8_t, kUseItemAnimationGatePatchSize>
        kExpected{0x84, 0xC0, 0x74, 0x18, 0x8B};

    if (g_rapid_gate.applied &&
        g_rapid_gate.process_id != process.ProcessId())
    {
        RestoreRapidFireGate(process);
    }
    if (g_rapid_gate.applied)
    {
        const std::uint32_t actor32 = static_cast<std::uint32_t>(actor);
        return process.WriteMemory(
            g_rapid_gate.remote + kRapidFireGateActorOffset, actor32);
    }

    RemoteModuleInfo module{};
    if (!process.GetMainModuleInfo(module) ||
        kUseItemAnimationGateRva + kUseItemAnimationGatePatchSize >
            module.image_size ||
        !IsSanePointer(actor))
    {
        return false;
    }

    RapidFireGateState next{};
    next.process_id = process.ProcessId();
    next.hook = module.base_address + kUseItemAnimationGateRva;
    if (!process.ReadMemory(
            next.hook, next.original.data(), next.original.size()) ||
        next.original != kExpected)
    {
        LogDiagnostic(
            "Rapid fire: UseItem animation-gate signature mismatch at %08X.",
            static_cast<unsigned>(next.hook));
        return false;
    }
    next.remote = process.AllocateRemoteMemory(kRapidFireGateRemoteSize);
    if (!IsSanePointer(next.remote))
        return false;

    const auto relative32 = [](std::uintptr_t target,
                               std::uintptr_t next_instruction)
    {
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<std::intptr_t>(target) -
            static_cast<std::intptr_t>(next_instruction)));
    };
    const std::uintptr_t shared_actor =
        next.remote + kRapidFireGateActorOffset;
    const std::uintptr_t native_ready = next.hook + 0x1C;
    const std::uintptr_t native_continue = next.hook + 0x0A;
    std::vector<std::uint8_t> code;
    const auto byte = [&](std::uint8_t value) { code.push_back(value); };
    const auto dword = [&](std::uint32_t value)
    {
        const std::size_t offset = code.size();
        code.resize(offset + sizeof(value));
        std::memcpy(code.data() + offset, &value, sizeof(value));
    };

    byte(0x9C); byte(0x60);                  // pushfd; pushad
    byte(0xA1); dword(static_cast<std::uint32_t>(shared_actor));
    byte(0x85); byte(0xC0);                  // test actor,actor
    byte(0x0F); byte(0x84);                  // jz native path
    const std::size_t no_actor_jump = code.size();
    dword(0);
    byte(0x39); byte(0xD8);                  // cmp eax,ebx
    byte(0x0F); byte(0x85);                  // jne native path
    const std::size_t other_actor_jump = code.size();
    dword(0);
    byte(0xC7); byte(0x83); dword(kShootCountdownOffset); dword(0);
                                                // player.shoot_countdown=0
    byte(0x61); byte(0x9D);                  // popad; popfd
    byte(0xE9);
    dword(relative32(native_ready, next.remote + code.size() + 4));

    const std::size_t native_path = code.size();
    for (const std::size_t displacement : {no_actor_jump, other_actor_jump})
    {
        const std::uint32_t relative = relative32(
            next.remote + native_path,
            next.remote + displacement + 4);
        std::memcpy(code.data() + displacement, &relative, sizeof(relative));
    }
    byte(0x61); byte(0x9D);                  // popad; popfd
    byte(0x84); byte(0xC0);                  // displaced test al,al
    byte(0x0F); byte(0x84);                  // native je 0040A04A
    dword(relative32(native_ready, next.remote + code.size() + 4));
    byte(0x8B); byte(0x83); dword(0x1BC);    // displaced full mov eax,[ebx+1BC]
    byte(0xE9);
    dword(relative32(native_continue, next.remote + code.size() + 4));
    if (code.size() > kRapidFireGateActorOffset ||
        !process.WriteMemory(next.remote, code.data(), code.size()))
    {
        (void)process.FreeRemoteMemory(next.remote);
        return false;
    }

    next.patch[0] = 0xE9;
    const std::uint32_t hook_relative = relative32(
        next.remote, next.hook + 5);
    std::memcpy(next.patch.data() + 1, &hook_relative, sizeof(hook_relative));
    next.applied = true;
    g_rapid_gate = next;
    if (!process.WriteProtectedMemory(
            next.hook, next.patch.data(), next.patch.size()) ||
        !process.WriteMemory(shared_actor, static_cast<std::uint32_t>(actor)))
    {
        RestoreRapidFireGate(process);
        return false;
    }
    LogDiagnostic(
        "Rapid fire: native held-trigger animation gate bypass installed "
        "UseItem=%08X.", static_cast<unsigned>(next.hook));
    return true;
}

void RestoreRapidValues(TrainerProcess& process)
{
    if (!g_state.rapid_applied)
        return;

    if (CapturedItemStillMatches(process))
    {
        (void)process.WriteMemory(
            g_state.context.item + kItemReserveOffset,
            g_state.original_reserve);
        (void)process.WriteMemory(
            g_state.context.item + kItemBulletsOffset,
            g_state.original_bullets);
    }
    g_state.rapid_applied = false;
}

void RestoreStableValues(TrainerProcess& process)
{
    if (!g_state.stable_applied)
        return;

    if (SameLiveProcess(process))
    {
        for (StableValuePatch& patch : g_state.stable_patches)
        {
            if (!patch.applied || !IsSanePointer(patch.address))
                continue;

            std::uint32_t current = 0;
            // Do not overwrite a value changed by another component after us.
            if (process.ReadMemory(patch.address, current) && current == 0)
                (void)process.WriteMemory(patch.address, patch.original);
            patch.applied = false;
        }
    }
    g_state.stable_applied = false;
}

void RestoreAll(TrainerProcess& process)
{
    RestoreRapidFireGate(process);
    if (!SameLiveProcess(process))
    {
        ClearState();
        return;
    }

    RestoreRapidValues(process);
    RestoreStableValues(process);
    ClearState();
}

void CaptureContext(const WeaponContext& context)
{
    g_state = {};
    g_state.context = context;
    g_state.original_reserve = context.reserve;
    g_state.original_bullets = context.bullets;
    g_state.context_captured = true;
}

bool ApplyStableTableValues(TrainerProcess& process)
{
    if (!g_state.stable_applied)
    {
        std::array<std::uint32_t, 9> properties{};
        for (std::size_t index = 0; index < kSpreadProperties.size(); ++index)
            properties[index] = kSpreadProperties[index];
        for (std::size_t index = 0; index < kRecoilProperties.size(); ++index)
            properties[index + kSpreadProperties.size()] =
                kRecoilProperties[index];

        for (std::size_t index = 0; index < properties.size(); ++index)
        {
            const std::uint8_t type =
                index < kSpreadProperties.size() ?
                kTableTypeFloat : kTableTypeInt;
            std::uintptr_t address = 0;
            std::uint32_t original = 0;
            if (!ResolveTableValueAddress(
                    process, g_state.context.weapon_table, properties[index],
                    g_state.context.item_id, type, sizeof(std::uint32_t), address) ||
                !process.ReadMemory(address, original))
            {
                RestoreStableValues(process);
                return false;
            }

            if (index >= kSpreadProperties.size())
            {
                if (static_cast<std::int32_t>(original) < 0 || original > 100U)
                    return false;
            }
            else
            {
                float original_float = 0.0f;
                static_assert(sizeof(original_float) == sizeof(original));
                std::memcpy(&original_float, &original, sizeof(original_float));
                if (!std::isfinite(original_float) || original_float < 0.0f ||
                    original_float > 180.0f)
                {
                    return false;
                }
            }

            g_state.stable_patches[index].address = address;
            g_state.stable_patches[index].original = original;
        }
        g_state.stable_applied = true;
    }

    constexpr std::uint32_t zero = 0;
    for (StableValuePatch& patch : g_state.stable_patches)
    {
        if (!process.WriteMemory(patch.address, zero))
        {
            RestoreStableValues(process);
            return false;
        }
        patch.applied = true;
    }
    return true;
}

void ApplyRapidValues(TrainerProcess& process)
{
    if (!g_state.rapid_applied)
    {
        std::int32_t reserve = 0;
        std::int32_t bullets = 0;
        if (!process.ReadMemory(
                g_state.context.item + kItemReserveOffset, reserve) ||
            !process.ReadMemory(
                g_state.context.item + kItemBulletsOffset, bullets) ||
            reserve < 0 || bullets < 0 || reserve > kMaximumSaneAmmo ||
            bullets > kMaximumSaneAmmo)
        {
            return;
        }
        g_state.original_reserve = reserve;
        g_state.original_bullets = bullets;
    }

    constexpr std::int32_t ready_to_fire = 0;
    const bool countdown_written = process.WriteMemory(
        g_state.context.actor + kShootCountdownOffset, ready_to_fire);
    const bool reserve_written = process.WriteMemory(
        g_state.context.item + kItemReserveOffset, kInfiniteAmmo);
    const bool bullets_written = process.WriteMemory(
        g_state.context.item + kItemBulletsOffset, kInfiniteAmmo);
    if (!countdown_written || !reserve_written || !bullets_written)
    {
        if (reserve_written)
        {
            (void)process.WriteMemory(
                g_state.context.item + kItemReserveOffset,
                g_state.original_reserve);
        }
        if (bullets_written)
        {
            (void)process.WriteMemory(
                g_state.context.item + kItemBulletsOffset,
                g_state.original_bullets);
        }
        return;
    }
    g_state.rapid_applied = true;
}
}

void UpdateWeaponModifiers(
    TrainerProcess& process,
    const RadarSnapshot* radar_snapshot,
    const WeaponSettings& settings)
{
    if (!settings.stable_precision && !settings.rapid_fire_unlimited)
    {
        RestoreAll(process);
        return;
    }

    if (!process.IsConnected() || radar_snapshot == nullptr ||
        !IsSanePointer(radar_snapshot->player_object_address))
    {
        RestoreAll(process);
        return;
    }

    WeaponContext context{};
    if (!ResolveWeaponContext(
            process, radar_snapshot->player_object_address, context))
    {
        RestoreAll(process);
        return;
    }

    if (!g_state.context_captured ||
        !ContextMatches(g_state.context, context))
    {
        RestoreAll(process);
        CaptureContext(context);
    }
    else
    {
        g_state.context.frame = context.frame;
        g_state.context.equipped_weapon = context.equipped_weapon;
    }

    if (settings.stable_precision)
        (void)ApplyStableTableValues(process);
    else
    {
        RestoreStableValues(process);
    }

    if (settings.rapid_fire_unlimited)
    {
        ApplyRapidValues(process);
        if (g_state.rapid_applied)
            (void)UpdateRapidFireGate(process, g_state.context.actor);
    }
    else
    {
        RestoreRapidFireGate(process);
        RestoreRapidValues(process);
    }
}

void RestoreWeaponModifiers(TrainerProcess& process)
{
    RestoreAll(process);
}

std::uintptr_t ActiveRapidFireActor(TrainerProcess& process)
{
    return g_state.rapid_applied && CapturedItemStillMatches(process)
        ? g_state.context.actor
        : 0;
}
}
