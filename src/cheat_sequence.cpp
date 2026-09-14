#include "cheat_sequence.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <thread>

namespace hd
{
namespace
{
constexpr wchar_t kProfileRegistryKey[] = L"Software\\HDPhase1";
constexpr wchar_t kProfileRegistryValue[] = L"CheatSelection";
constexpr wchar_t kHotkeysRegistryValue[] = L"CheatHotkeys";
constexpr wchar_t kInventoryModeRegistryValue[] = L"InventoryMode";
constexpr wchar_t kInventoryItemCountRegistryValue[] = L"InventoryItemCount";
constexpr DWORD kImmortalityBit = 1U << 0;
constexpr DWORD kIronmanBit = 1U << 1;
constexpr DWORD kFullhandsBit = 1U << 2;
constexpr DWORD kFunnyheadBit = 1U << 3;
constexpr DWORD kSkipmissionBit = 1U << 4;
constexpr DWORD kDefaultCheatsMask = 0;

class ScopedInputBlock final
{
public:
    ScopedInputBlock()
        : active_(BlockInput(TRUE) != FALSE)
    {
    }

    ~ScopedInputBlock()
    {
        // Always attempt to restore physical input, including on every early
        // return or exception that unwinds ApplyCheatSequence.
        Release();
    }

    ScopedInputBlock(const ScopedInputBlock&) = delete;
    ScopedInputBlock& operator=(const ScopedInputBlock&) = delete;

    [[nodiscard]] bool IsActive() const
    {
        return active_;
    }

    void Release()
    {
        if (active_)
        {
            BlockInput(FALSE);
            active_ = false;
        }
    }

private:
    bool active_ = false;
};

DWORD SelectionToMask(const CheatSelection& selection)
{
    DWORD mask = 0;
    if (selection.immortality)
        mask |= kImmortalityBit;
    if (selection.ironman)
        mask |= kIronmanBit;
    if (selection.fullhands)
        mask |= kFullhandsBit;
    if (selection.funnyhead)
        mask |= kFunnyheadBit;
    if (selection.skipmission)
        mask |= kSkipmissionBit;
    return mask;
}

CheatSelection MaskToSelection(DWORD mask)
{
    CheatSelection selection{};
    selection.immortality = (mask & kImmortalityBit) != 0;
    selection.ironman = (mask & kIronmanBit) != 0;
    selection.fullhands = (mask & kFullhandsBit) != 0;
    selection.funnyhead = (mask & kFunnyheadBit) != 0;
    selection.skipmission = (mask & kSkipmissionBit) != 0;
    return selection;
}

bool SendScanCode(WORD scan_code, bool key_up, bool extended_key)
{
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = 0;
    input.ki.wScan = scan_code;
    input.ki.dwFlags = KEYEVENTF_SCANCODE;
    if (key_up)
        input.ki.dwFlags |= KEYEVENTF_KEYUP;
    if (extended_key)
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;

    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

bool SendCharacter(char character, HKL keyboard_layout)
{
    const SHORT key_mapping = VkKeyScanExA(character, keyboard_layout);
    if (key_mapping == -1)
        return false;

    const WORD mapping = static_cast<WORD>(key_mapping);
    const WORD virtual_key = LOBYTE(mapping);
    const BYTE modifiers = HIBYTE(mapping);

    // Every cheat code is lowercase ASCII and therefore needs no modifier.
    // Reject an unexpected mapping rather than leaving Shift/Ctrl/Alt stuck.
    if (modifiers != 0)
        return false;

    const UINT mapped_scan_code = MapVirtualKeyExW(
        virtual_key, MAPVK_VK_TO_VSC_EX, keyboard_layout);
    if (mapped_scan_code == 0)
        return false;

    const WORD scan_code = LOBYTE(mapped_scan_code);
    const BYTE scan_prefix = HIBYTE(mapped_scan_code);
    const bool extended_key = scan_prefix == 0xE0 || scan_prefix == 0xE1;

    if (!SendScanCode(scan_code, false, extended_key))
        return false;

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    if (!SendScanCode(scan_code, true, extended_key))
        return false;

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return true;
}

bool SendVirtualKeyWithTiming(WORD virtual_key, HKL keyboard_layout)
{
    const UINT mapped_scan_code = MapVirtualKeyExW(
        virtual_key, MAPVK_VK_TO_VSC_EX, keyboard_layout);
    if (mapped_scan_code == 0)
        return false;

    const WORD scan_code = LOBYTE(mapped_scan_code);
    const BYTE scan_prefix = HIBYTE(mapped_scan_code);
    const bool extended_key = scan_prefix == 0xE0 || scan_prefix == 0xE1;

    if (!SendScanCode(scan_code, false, extended_key))
        return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    if (!SendScanCode(scan_code, true, extended_key))
        return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return true;
}

bool SendCheatCode(const char* code, HKL keyboard_layout)
{
    for (const char* character = code; *character != '\0'; ++character)
    {
        if (!SendCharacter(*character, keyboard_layout))
            return false;
    }

    return true;
}

const char* CheatCode(CheatId cheat)
{
    switch (cheat)
    {
    case CheatId::Skipmission:
        return "skipmission";
    // Tous les autres sont natifs : ils ne passent plus par une frappe.
    default:
        return nullptr;
    }
}

CheatSequenceResult ApplyCodes(
    const TrainerProcess& game_process,
    const char* const* codes,
    std::size_t code_count,
    bool inventory_cheat,
    InventoryMode inventory_mode,
    std::uint32_t inventory_item_count)
{
    if (!game_process.IsConnected())
        return CheatSequenceResult::NotConnected;

    if (!codes || code_count == 0)
        return CheatSequenceResult::NothingSelected;

    // SendInput generated by this thread remains enabled while physical mouse
    // and keyboard input is blocked. The RAII guard guarantees unblocking.
    ScopedInputBlock input_block;
    if (!input_block.IsActive())
        return CheatSequenceResult::InputBlockFailed;

    MessageBeep(MB_OK);

    const HWND game_window = game_process.WindowHandle();
    if (!IsWindow(game_window))
        return CheatSequenceResult::NotConnected;

    // Never pull H&D in front. The keystrokes below go to the foreground
    // window, so the game has to be active -- but it must become active
    // because the player went back to it, not because the trainer forced it
    // while they were still filling in the panel. The caller keeps the
    // request pending and replays it on its own.
    if (!game_process.IsGameWindowActive())
        return CheatSequenceResult::GameNotActive;

    const DWORD game_thread_id = GetWindowThreadProcessId(game_window, nullptr);
    const HKL keyboard_layout = GetKeyboardLayout(game_thread_id);

    if (!SendCheatCode("iwantcheat", keyboard_layout))
        return CheatSequenceResult::InputFailed;

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    for (std::size_t index = 0; index < code_count; ++index)
    {
        if (!game_process.IsGameWindowActive())
            return CheatSequenceResult::GameNotActive;

        if (index != 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(75));

        if (!codes[index] || !SendCheatCode(codes[index], keyboard_layout))
            return CheatSequenceResult::InputFailed;
    }

    if (inventory_cheat)
    {
        if (inventory_mode == InventoryMode::ChooseSpecificItem)
        {
            // fullhands opens the native "Select item" dialog. Restore input
            // immediately so the user can choose an item manually.
            input_block.Release();
            return CheatSequenceResult::Success;
        }

        const std::uint32_t safe_item_count = (std::clamp)(
            inventory_item_count,
            1U,
            kMaximumInventoryItemCount);
        for (std::uint32_t item_index = 0;
             item_index < safe_item_count;
             ++item_index)
        {
            // The first dialog is already open. Reopen it for every following
            // item, reset to the top, walk to the requested row and validate.
            if (item_index != 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                if (!SendCheatCode("fullhands", keyboard_layout))
                    return CheatSequenceResult::InputFailed;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            if (!game_process.IsGameWindowActive())
                return CheatSequenceResult::GameNotActive;
            if (!SendVirtualKeyWithTiming(VK_HOME, keyboard_layout))
                return CheatSequenceResult::InputFailed;

            for (std::uint32_t step = 0; step < item_index; ++step)
            {
                if (!SendVirtualKeyWithTiming(VK_DOWN, keyboard_layout))
                    return CheatSequenceResult::InputFailed;
            }

            if (!SendVirtualKeyWithTiming(VK_RETURN, keyboard_layout))
                return CheatSequenceResult::InputFailed;
        }
    }

    return CheatSequenceResult::Success;
}
}

CheatSelection LoadCheatProfile()
{
    // No registry value means a genuine first launch: start with an empty
    // profile and let the user explicitly choose every enabled cheat.
    DWORD mask = kDefaultCheatsMask;
    DWORD mask_size = sizeof(mask);
    DWORD value_type = 0;

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kProfileRegistryKey, 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS)
    {
        if (RegQueryValueExW(
                key,
                kProfileRegistryValue,
                nullptr,
                &value_type,
                reinterpret_cast<BYTE*>(&mask),
                &mask_size) != ERROR_SUCCESS ||
            value_type != REG_DWORD)
        {
            mask = kDefaultCheatsMask;
        }

        RegCloseKey(key);
    }

    return MaskToSelection(mask);
}

bool SaveCheatProfile(const CheatSelection& selection)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            kProfileRegistryKey,
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE,
            nullptr,
            &key,
            nullptr) != ERROR_SUCCESS)
    {
        return false;
    }

    const DWORD mask = SelectionToMask(selection);
    const LONG result = RegSetValueExW(
        key,
        kProfileRegistryValue,
        0,
        REG_DWORD,
        reinterpret_cast<const BYTE*>(&mask),
        sizeof(mask));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

CheatHotkeys LoadCheatHotkeys()
{
    CheatHotkeys hotkeys{};
    DWORD value_type = 0;
    DWORD data_size = static_cast<DWORD>(
        hotkeys.virtual_keys.size() * sizeof(hotkeys.virtual_keys[0]));

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kProfileRegistryKey, 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS)
    {
        const LONG result = RegQueryValueExW(
            key,
            kHotkeysRegistryValue,
            nullptr,
            &value_type,
            reinterpret_cast<BYTE*>(hotkeys.virtual_keys.data()),
            &data_size);
        RegCloseKey(key);

        const DWORD expected_size = static_cast<DWORD>(
            hotkeys.virtual_keys.size() * sizeof(hotkeys.virtual_keys[0]));
        if (result != ERROR_SUCCESS || value_type != REG_BINARY || data_size != expected_size)
            return CheatHotkeys{};

        for (int virtual_key : hotkeys.virtual_keys)
        {
            if (virtual_key < 0 || virtual_key > 0xFE)
                return CheatHotkeys{};
        }

        // Migrate previous profiles automatically. F1/F2 belong to the game;
        // F8/F9/F12 are reserved by the advanced gameplay controls. F10/F11
        // are deliberately left to the game's native actions.
        const bool uses_reserved_key = std::find(
            hotkeys.virtual_keys.begin(),
            hotkeys.virtual_keys.end(),
            VK_F1) != hotkeys.virtual_keys.end() ||
            std::find(
                hotkeys.virtual_keys.begin(),
                hotkeys.virtual_keys.end(),
                VK_F2) != hotkeys.virtual_keys.end() ||
            std::find(
                hotkeys.virtual_keys.begin(),
                hotkeys.virtual_keys.end(),
                VK_F8) != hotkeys.virtual_keys.end() ||
            std::find(
                hotkeys.virtual_keys.begin(),
                hotkeys.virtual_keys.end(),
                VK_F9) != hotkeys.virtual_keys.end() ||
            std::find(
                hotkeys.virtual_keys.begin(),
                hotkeys.virtual_keys.end(),
                VK_F12) != hotkeys.virtual_keys.end();
        if (uses_reserved_key)
        {
            hotkeys = CheatHotkeys{};
            SaveCheatHotkeys(hotkeys);
        }
    }

    return hotkeys;
}

bool SaveCheatHotkeys(const CheatHotkeys& hotkeys)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            kProfileRegistryKey,
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE,
            nullptr,
            &key,
            nullptr) != ERROR_SUCCESS)
    {
        return false;
    }

    const DWORD data_size = static_cast<DWORD>(
        hotkeys.virtual_keys.size() * sizeof(hotkeys.virtual_keys[0]));
    const LONG result = RegSetValueExW(
        key,
        kHotkeysRegistryValue,
        0,
        REG_BINARY,
        reinterpret_cast<const BYTE*>(hotkeys.virtual_keys.data()),
        data_size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

InventoryMode LoadInventoryMode()
{
    DWORD stored_mode = static_cast<DWORD>(InventoryMode::ChooseSpecificItem);
    DWORD data_size = sizeof(stored_mode);
    DWORD value_type = 0;

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kProfileRegistryKey, 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS)
    {
        const LONG result = RegQueryValueExW(
            key,
            kInventoryModeRegistryValue,
            nullptr,
            &value_type,
            reinterpret_cast<BYTE*>(&stored_mode),
            &data_size);
        RegCloseKey(key);

        if (result != ERROR_SUCCESS || value_type != REG_DWORD ||
            stored_mode > static_cast<DWORD>(InventoryMode::CollectAllItems))
        {
            return InventoryMode::ChooseSpecificItem;
        }
    }

    return static_cast<InventoryMode>(stored_mode);
}

bool SaveInventoryMode(InventoryMode mode)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            kProfileRegistryKey,
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE,
            nullptr,
            &key,
            nullptr) != ERROR_SUCCESS)
    {
        return false;
    }

    const DWORD stored_mode = static_cast<DWORD>(mode);
    const LONG result = RegSetValueExW(
        key,
        kInventoryModeRegistryValue,
        0,
        REG_DWORD,
        reinterpret_cast<const BYTE*>(&stored_mode),
        sizeof(stored_mode));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

std::uint32_t LoadInventoryItemCount()
{
    DWORD item_count = kDefaultInventoryItemCount;
    DWORD data_size = sizeof(item_count);
    DWORD value_type = 0;

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kProfileRegistryKey, 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS)
    {
        const LONG result = RegQueryValueExW(
            key,
            kInventoryItemCountRegistryValue,
            nullptr,
            &value_type,
            reinterpret_cast<BYTE*>(&item_count),
            &data_size);
        RegCloseKey(key);

        if (result != ERROR_SUCCESS || value_type != REG_DWORD ||
            item_count == 0 || item_count > kMaximumInventoryItemCount)
        {
            return kDefaultInventoryItemCount;
        }
    }

    return item_count;
}

bool SaveInventoryItemCount(std::uint32_t item_count)
{
    item_count = (std::clamp)(item_count, 1U, kMaximumInventoryItemCount);

    HKEY key = nullptr;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            kProfileRegistryKey,
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE,
            nullptr,
            &key,
            nullptr) != ERROR_SUCCESS)
    {
        return false;
    }

    const LONG result = RegSetValueExW(
        key,
        kInventoryItemCountRegistryValue,
        0,
        REG_DWORD,
        reinterpret_cast<const BYTE*>(&item_count),
        sizeof(item_count));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

CheatSequenceResult ApplyCheatSequence(
    const TrainerProcess& game_process,
    const CheatSelection& selection)
{
    std::array<const char*, kCheatCount> codes{};
    std::size_t code_count = 0;
    const std::array<bool, kCheatCount> selected{{
        selection.immortality,
        selection.ironman,
        selection.fullhands,
        selection.funnyhead,
        selection.skipmission
    }};

    for (std::size_t index = 0; index < selected.size(); ++index)
    {
        if (selected[index])
            codes[code_count++] = CheatCode(static_cast<CheatId>(index));
    }

    return ApplyCodes(
        game_process,
        codes.data(),
        code_count,
        false,
        InventoryMode::ChooseSpecificItem,
        0);
}

CheatSequenceResult ApplySingleCheat(
    const TrainerProcess& game_process,
    CheatId cheat,
    InventoryMode inventory_mode,
    std::uint32_t inventory_item_count)
{
    const char* code = CheatCode(cheat);
    const bool inventory_cheat = cheat == CheatId::Fullhands;
    return code
        ? ApplyCodes(
            game_process,
            &code,
            1,
            inventory_cheat,
            inventory_mode,
            inventory_item_count)
        : CheatSequenceResult::NothingSelected;
}

const char* CheatSequenceResultText(CheatSequenceResult result)
{
    switch (result)
    {
    case CheatSequenceResult::Success:
        return "[OK] Séquence de cheats envoyée.";
    case CheatSequenceResult::NotConnected:
        return "[ERREUR] hde.exe n'est pas connecté.";
    case CheatSequenceResult::GameNotActive:
        return "[ATTENTE] Demande enregistrée : elle sera appliquée dès votre "
               "retour dans le jeu.";
    case CheatSequenceResult::NothingSelected:
        return "[ERREUR] Aucun cheat n'est sélectionné.";
    case CheatSequenceResult::InputBlockFailed:
        return "[ERREUR] BlockInput a échoué (lancez le Trainer en administrateur).";
    case CheatSequenceResult::InputFailed:
        return "[ERREUR] Échec de l'envoi des touches au jeu.";
    case CheatSequenceResult::NeverRun:
    default:
        return "Prêt à appliquer la séquence.";
    }
}
}
