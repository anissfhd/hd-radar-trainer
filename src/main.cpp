#include <Windows.h>
#include <windowsx.h>
#include <d3d9.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>

#include "cheat_sequence.h"
#include "diagnostics.h"
#include "gameplay_mods.h"
#include "radar.h"
#include "trainer_ui.h"
#include "trainer_process.h"
#include "weapon_mods.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND window, UINT message, WPARAM w_param, LPARAM l_param);

namespace
{
constexpr wchar_t kWindowClassName[] = L"HDPhase1TrainerWindow";
constexpr wchar_t kWindowTitle[] = L"HD Final Advanced V106";
constexpr wchar_t kGameExecutable[] = L"hde.exe";
constexpr ULONGLONG kProcessScanIntervalMs = 250;

IDirect3D9* g_d3d = nullptr;
IDirect3DDevice9* g_device = nullptr;
D3DPRESENT_PARAMETERS g_present_parameters{};
bool g_imgui_initialized = false;
std::atomic_bool g_noclip_keyboard_context{false};
std::atomic_bool g_noclip_keyboard_filter{false};
std::atomic_bool g_noclip_hotkey{false};
std::atomic_bool g_inventory_series_keyboard_filter{false};
std::atomic_bool g_inventory_series_down{false};
std::atomic_bool g_noclip_toggle_down{false};
std::atomic_bool g_noclip_forward_down{false};
std::atomic_bool g_noclip_backward_down{false};
std::atomic_bool g_noclip_left_down{false};
std::atomic_bool g_noclip_right_down{false};
std::atomic_bool g_noclip_up_down{false};
std::atomic_bool g_noclip_down_down{false};
// F10 is observed directly by the same global keyboard hook as noclip. It is
// deliberately not swallowed: the game keeps its native binding while the
// trainer still receives a reliable edge for the revive action.
std::atomic_bool g_revive_current_player_down{false};

void ResetNoclipCapturedKeys()
{
    g_noclip_toggle_down.store(false);
    g_noclip_forward_down.store(false);
    g_noclip_backward_down.store(false);
    g_noclip_left_down.store(false);
    g_noclip_right_down.store(false);
    g_noclip_up_down.store(false);
    g_noclip_down_down.store(false);
    g_inventory_series_down.store(false);
    g_revive_current_player_down.store(false);
}

LRESULT CALLBACK NoclipKeyboardFilter(
    int code, WPARAM w_param, LPARAM l_param)
{
    if (code == HC_ACTION && g_noclip_keyboard_context.load())
    {
        const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(l_param);
        if (key)
        {
            const DWORD scan = key->scanCode;
            const bool down = (key->flags & LLKHF_UP) == 0;
            const bool toggle_key = scan == 0x2FU; // physical V
            const bool movement_key = scan == 0x10U || scan == 0x11U ||
                scan == 0x12U || scan == 0x1FU || scan == 0x23U ||
                scan == 0x30U; // physical A/Z/E/S/H/B on AZERTY
            const bool native_lateral_key = scan == 0x1EU || scan == 0x20U;
            // The feature accepts both the physical French-AZERTY M position
            // and logical VK_M. Capture it here before swallowing it so M can
            // cycle lots without also opening the game's inventory screen.
            const bool inventory_series_key =
                scan == 0x27U || key->vkCode == static_cast<DWORD>('M');
            const bool revive_key = key->vkCode == VK_F10 || scan == 0x44U;

            if (inventory_series_key)
                g_inventory_series_down.store(down);
            if (revive_key)
                g_revive_current_player_down.store(down);

            switch (scan)
            {
            case 0x2FU: g_noclip_toggle_down.store(down); break;
            case 0x11U: g_noclip_forward_down.store(down); break;
            case 0x1FU: g_noclip_backward_down.store(down); break;
            case 0x10U: g_noclip_left_down.store(down); break;
            case 0x12U: g_noclip_right_down.store(down); break;
            case 0x23U: g_noclip_up_down.store(down); break;
            case 0x30U: g_noclip_down_down.store(down); break;
            default: break;
            }

            // V is only taken from the game when its hotkey is enabled. It
            // used to be swallowed unconditionally, so the game never saw the
            // key and any press toggled noclip even with the feature off.
            if ((inventory_series_key &&
                    g_inventory_series_keyboard_filter.load()) ||
                (toggle_key && g_noclip_hotkey.load()) ||
                ((movement_key || native_lateral_key) &&
                    g_noclip_keyboard_filter.load()))
            {
                return 1;
            }
        }
    }
    return CallNextHookEx(nullptr, code, w_param, l_param);
}

bool ClosePreviousTrainerInstance()
{
    const HWND previous_window = FindWindowW(kWindowClassName, nullptr);
    if (!previous_window)
        return true;

    DWORD previous_process_id = 0;
    GetWindowThreadProcessId(previous_window, &previous_process_id);
    if (previous_process_id == 0 ||
        previous_process_id == GetCurrentProcessId())
    {
        return true;
    }

    DWORD_PTR ignored = 0;
    SendMessageTimeoutW(
        previous_window,
        WM_CLOSE,
        0,
        0,
        SMTO_ABORTIFHUNG | SMTO_BLOCK,
        2'000,
        &ignored);

    const HANDLE previous_process = OpenProcess(
        SYNCHRONIZE, FALSE, previous_process_id);
    const ULONGLONG deadline = GetTickCount64() + 4'000;
    while (GetTickCount64() < deadline)
    {
        const bool shutdown_complete = previous_process
            ? WaitForSingleObject(previous_process, 0) == WAIT_OBJECT_0
            : !IsWindow(previous_window);
        if (shutdown_complete)
        {
            if (previous_process)
                CloseHandle(previous_process);
            return true;
        }
        Sleep(50);
    }

    if (previous_process)
        CloseHandle(previous_process);
    return false;
}

#if 0
// Retired flat interface. The themed UI in trainer_ui.cpp deliberately has
// no hotkey selector: F3/F4/F6/F7 are fixed product controls.
const char* HotkeyName(int virtual_key)
{
    for (const HotkeyOption& option : kHotkeyOptions)
    {
        if (option.virtual_key == virtual_key)
            return option.name;
    }

    return "Inconnue";
}
#endif

bool CreateD3DDevice(HWND window)
{
    g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_d3d)
        return false;

    ZeroMemory(&g_present_parameters, sizeof(g_present_parameters));
    g_present_parameters.Windowed = TRUE;
    g_present_parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_present_parameters.BackBufferFormat = D3DFMT_UNKNOWN;
    g_present_parameters.EnableAutoDepthStencil = TRUE;
    g_present_parameters.AutoDepthStencilFormat = D3DFMT_D16;
    g_present_parameters.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

    HRESULT result = g_d3d->CreateDevice(
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        window,
        D3DCREATE_HARDWARE_VERTEXPROCESSING,
        &g_present_parameters,
        &g_device);

    if (FAILED(result))
    {
        result = g_d3d->CreateDevice(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL,
            window,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &g_present_parameters,
            &g_device);
    }

    return SUCCEEDED(result);
}

void CleanupD3DDevice()
{
    if (g_device)
    {
        g_device->Release();
        g_device = nullptr;
    }

    if (g_d3d)
    {
        g_d3d->Release();
        g_d3d = nullptr;
    }
}

void ResetD3DDevice()
{
    if (g_imgui_initialized)
    {
        hd::ui::InvalidateDeviceObjects();
        ImGui_ImplDX9_InvalidateDeviceObjects();
    }

    const HRESULT result = g_device->Reset(&g_present_parameters);
    if (result == D3DERR_INVALIDCALL)
        OutputDebugStringW(L"[HDPhase1] Direct3D 9 Reset returned D3DERR_INVALIDCALL.\n");

    if (SUCCEEDED(result) && g_imgui_initialized)
    {
        ImGui_ImplDX9_CreateDeviceObjects();
        hd::ui::CreateDeviceObjects(g_device);
    }
}

#if 0
bool SelectHotkey(
    hd::CheatHotkeys& hotkeys,
    std::size_t cheat_index)
{
    bool changed = false;
    const int current_key = hotkeys.virtual_keys[cheat_index];
    if (ImGui::BeginCombo("##Hotkey", HotkeyName(current_key)))
    {
        for (const HotkeyOption& option : kHotkeyOptions)
        {
            const bool selected = option.virtual_key == current_key;
            if (ImGui::Selectable(option.name, selected))
            {
                // Keep bindings unique. If the selected key was already used,
                // swap that cheat to this row's previous binding.
                for (std::size_t other = 0; other < hd::kCheatCount; ++other)
                {
                    if (other != cheat_index && option.virtual_key != 0 &&
                        hotkeys.virtual_keys[other] == option.virtual_key)
                    {
                        hotkeys.virtual_keys[other] = current_key;
                    }
                }

                hotkeys.virtual_keys[cheat_index] = option.virtual_key;
                changed = true;
            }

            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    return changed;
}

bool DrawCheatControl(
    const char* label,
    hd::CheatId cheat,
    hd::CheatHotkeys& hotkeys,
    hd::CheatId& clicked_cheat)
{
    const std::size_t index = static_cast<std::size_t>(cheat);
    ImGui::PushID(static_cast<int>(index));

    if (ImGui::Button(label, ImVec2(210.0f, 30.0f)))
        clicked_cheat = cheat;

    ImGui::SameLine();
    ImGui::Text("[%s]", HotkeyName(hotkeys.virtual_keys[index]));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(82.0f);
    const bool changed = SelectHotkey(hotkeys, index);

    ImGui::PopID();
    return changed;
}

hd::CheatId DrawTrainerWindow(
    const hd::TrainerProcess& game_process,
    hd::CheatHotkeys& cheat_hotkeys,
    hd::CheatSequenceResult sequence_result,
    bool& hotkey_profile_save_succeeded,
    hd::RadarRenderSettings& radar_render_settings,
    hd::WeaponSettings& weapon_settings,
    hd::GameplaySettings& gameplay_settings,
    hd::GameplayStatus& gameplay_status)
{
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);

    constexpr ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("##HDTrainerMainWindow", nullptr, window_flags);
    ImGui::TextUnformatted("HD Radar Trainer - Phase 2");
    ImGui::Separator();

    if (game_process.IsConnected())
    {
        ImGui::TextColored(
            ImVec4(0.30f, 0.90f, 0.40f, 1.0f),
            "[OK] Connecté à hde.exe (PID: %lu)",
            static_cast<unsigned long>(game_process.ProcessId()));
    }
    else
    {
        ImGui::TextColored(
            ImVec4(1.00f, 0.75f, 0.20f, 1.0f),
            "[WAITT] En attente du lancement de hde.exe...");

        if (game_process.LastError() != ERROR_SUCCESS)
        {
            ImGui::TextDisabled(
                "OpenProcess a échoué pour le PID %lu (erreur Windows %lu).",
                static_cast<unsigned long>(game_process.LastCandidateProcessId()),
                static_cast<unsigned long>(game_process.LastError()));
        }
    }

    ImGui::Spacing();
    ImGui::BeginChild("ControlPanel", ImVec2(0.0f, 0.0f), true);
    ImGui::TextUnformatted("Cheats");
    ImGui::Separator();

    hd::CheatId clicked_cheat = hd::CheatId::Count;
    bool hotkeys_changed = false;
    hotkeys_changed |= DrawCheatControl(
        "Life Unlimited", hd::CheatId::Immortality, cheat_hotkeys, clicked_cheat);
    hotkeys_changed |= DrawCheatControl(
        "Santé Max", hd::CheatId::Ironman, cheat_hotkeys, clicked_cheat);
    hotkeys_changed |= DrawCheatControl(
        "Big Heads", hd::CheatId::Funnyhead, cheat_hotkeys, clicked_cheat);
    hotkeys_changed |= DrawCheatControl(
        "Passer la Mission", hd::CheatId::Skipmission, cheat_hotkeys, clicked_cheat);

    if (hotkeys_changed)
    {
        hotkey_profile_save_succeeded = hd::SaveCheatHotkeys(cheat_hotkeys);
    }

    if (!hotkey_profile_save_succeeded)
    {
        ImGui::TextColored(
            ImVec4(1.0f, 0.35f, 0.30f, 1.0f),
            "Impossible d'enregistrer les raccourcis.");
    }

    ImGui::Spacing();
    const bool sequence_succeeded = sequence_result == hd::CheatSequenceResult::Success;
    const bool sequence_failed =
        sequence_result != hd::CheatSequenceResult::NeverRun && !sequence_succeeded;
    const ImVec4 sequence_color = sequence_succeeded
        ? ImVec4(0.30f, 0.90f, 0.40f, 1.0f)
        : (sequence_failed
            ? ImVec4(1.0f, 0.35f, 0.30f, 1.0f)
            : ImVec4(0.65f, 0.70f, 0.75f, 1.0f));
    ImGui::TextColored(
        sequence_color, "%s", hd::CheatSequenceResultText(sequence_result));

    ImGui::Spacing();
    ImGui::SeparatorText("Visuals / ESP");
    ImGui::Checkbox(
        "Afficher ESP Ennemis (Rouge/Vert)",
        &radar_render_settings.show_enemy_esp);
    ImGui::Checkbox(
        "Afficher ESP Allies (Bleu)",
        &radar_render_settings.show_ally_esp);

    ImGui::Spacing();
    ImGui::SeparatorText("Weapons / Armes");
    ImGui::Checkbox(
        "Stabilit\u00e9 et pr\u00e9cision 100 % (Sans recul ni dispersion)",
        &weapon_settings.stable_precision);
    ImGui::Checkbox(
        "Tir ultra-rapide et munitions illimit\u00e9es (Sans recharge)",
        &weapon_settings.rapid_fire_unlimited);
    ImGui::Checkbox(
        "Permettre aux balles de traverser les murs",
        &gameplay_settings.bullets_through_walls);
    ImGui::TextDisabled(
        "Necessite Bullet Track. La cible n'a plus besoin d'etre degagee : "
        "un ennemi derriere un mur ou dans une maison encaisse les degats.");
    ImGui::TextDisabled(
        "Options appliqu\u00e9es uniquement \u00e0 l'arme du joueur local.");

    ImGui::Spacing();
    ImGui::SeparatorText("Gameplay / Fonctions avancées");
    // A single control for both. Ticking the box arms the V hotkey at the same
    // time; V then toggles the flight without disarming itself, so it can turn
    // it back on. Unticking gives V back to the game.
    if (ImGui::Checkbox(
            "Noclip spatial (touche V)",
            &gameplay_settings.noclip_enabled))
    {
        gameplay_settings.noclip_hotkey_enabled =
            gameplay_settings.noclip_enabled;
    }
    ImGui::SameLine();
    ImGui::Text("%.1f m/s", gameplay_settings.noclip_speed_mps);
    ImGui::TextDisabled(
        "Z/S avancer-reculer, A/E gauche-droite, H monter, B descendre, "
        "F8/F9 vitesse. Refusé en véhicule.");
    ImGui::TextDisabled("%s", hd::NoclipStatusText(gameplay_status));

    ImGui::Checkbox(
        "Super Run joueur (F8 augmente / F9 réduit)",
        &gameplay_settings.player_speed_enabled);
    ImGui::SameLine();
    ImGui::Text("%.1fx", gameplay_settings.player_speed_multiplier);
    ImGui::TextDisabled("%s", hd::PlayerSpeedStatusText(gameplay_status));

    ImGui::Checkbox(
        "Téléportation via la carte native (K)",
        &gameplay_settings.teleport_map_enabled);
    ImGui::TextDisabled("%s", hd::TeleportStatusText(gameplay_status));
    ImGui::Checkbox(
        "Invisible pour les ennemis",
        &gameplay_settings.enemy_invisibility_enabled);
    if (gameplay_settings.enemy_invisibility_enabled)
    {
        if (ImGui::RadioButton(
                "Joueur contrôlé uniquement",
                gameplay_settings.enemy_invisibility_scope ==
                    hd::EnemyInvisibilityScope::ControlledPlayer))
        {
            gameplay_settings.enemy_invisibility_scope =
                hd::EnemyInvisibilityScope::ControlledPlayer;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton(
                "Escouade entière",
                gameplay_settings.enemy_invisibility_scope ==
                    hd::EnemyInvisibilityScope::WholeSquad))
        {
            gameplay_settings.enemy_invisibility_scope =
                hd::EnemyInvisibilityScope::WholeSquad;
        }
    }
    ImGui::TextDisabled(
        "%s", hd::EnemyInvisibilityStatusText(gameplay_status));

    ImGui::Checkbox(
        "Aimbot tête (sans tir automatique)",
        &gameplay_settings.aimbot_enabled);
    ImGui::SetNextItemWidth(280.0f);
    ImGui::SliderFloat(
        "Distance Aimbot (m)",
        &gameplay_settings.aimbot_max_distance_m,
        40.0f,
        220.0f,
        "%.0f m");
    ImGui::TextDisabled("%s", hd::AimbotStatusText(gameplay_status));

    ImGui::Checkbox(
        "Bullet Track tête (sans tir automatique)",
        &gameplay_settings.bullet_track_enabled);
    ImGui::Checkbox(
        "Bullet Track V2 : impact instantané derrière les murs",
        &gameplay_settings.bullet_track_instant_wall_impact_v2);
    ImGui::TextDisabled(
        "Option de test séparée : agit seulement sur une cible rouge avec "
        "traversée des murs activée.");
    ImGui::TextDisabled(
        "Acquisition globale : le curseur et le cercle ne limitent plus la cible.");
    ImGui::TextDisabled(
        "%s", hd::BulletTrackStatusText(gameplay_status));

    ImGui::Checkbox(
        "Super vitesse véhicule ([N] accélère, [B] réduit)",
        &gameplay_settings.vehicle_speed_enabled);
    ImGui::SameLine();
    ImGui::Text("%.1fx", gameplay_settings.vehicle_speed_multiplier);
    ImGui::TextDisabled(
        "[N] augmente le multiplicateur, [B] le réduit "
        "(minimum 1.0x = vitesse par défaut du jeu).");
    ImGui::TextDisabled(
        "[I] rend la direction plus vive, [U] la ramène vers le braquage "
        "d'origine du jeu. À régler au volant : plus la vitesse est haute, "
        "plus il faut appuyer sur [I].");
    ImGui::TextDisabled("%s", hd::VehicleSpeedStatusText(gameplay_status));

    ImGui::Checkbox(
        "Vitesse du jeu ([9] accélère, [8] réduit)",
        &gameplay_settings.game_speed_enabled);
    ImGui::SameLine();
    ImGui::Text("%.1fx", gameplay_settings.game_speed_multiplier);
    ImGui::TextDisabled(
        "Accélère toute la simulation, pour ne plus attendre pendant les "
        "phases scriptées : de 1.0x (vitesse normale) à 100.0x, avec un pas "
        "qui s'élargit à mesure que la vitesse monte.");
    ImGui::TextDisabled("%s", hd::GameSpeedStatusText(gameplay_status));

    ImGui::Checkbox(
        "Véhicule indestructible (jamais d'explosion)",
        &gameplay_settings.vehicle_invulnerable_enabled);
    ImGui::TextDisabled(
        "Tirs ennemis, chutes de grande hauteur et collisions n'endommagent "
        "plus le véhicule conduit.");
    ImGui::TextDisabled(
        "%s", hd::VehicleInvulnerabilityStatusText(gameplay_status));

    ImGui::BeginDisabled(gameplay_settings.grant_all_items_consumed);
    if (ImGui::Button("Armes et équipement — série suivante [M]"))
        gameplay_settings.grant_all_items_requested = true;
    ImGui::EndDisabled();
    ImGui::TextDisabled(
        "14 series fixes : M remplace completement la precedente sans ouvrir "
        "l'inventaire (9-10 objets par serie avec le catalogue installe).");
    ImGui::TextDisabled("%s", hd::GrantAllItemsStatusText(gameplay_status));

    ImGui::EndChild();

    ImGui::End();

    return clicked_cheat;
}
#endif

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    if (ImGui_ImplWin32_WndProcHandler(window, message, w_param, l_param))
        return TRUE;

    switch (message)
    {
    case WM_NCCALCSIZE:
        if (w_param != 0)
            return 0;
        break;

    case WM_NCHITTEST:
    {
        POINT cursor{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        ScreenToClient(window, &cursor);
        RECT client{};
        GetClientRect(window, &client);
        const LONG width = client.right - client.left;
        const LONG height = client.bottom - client.top;
        const float dpi = static_cast<float>(GetDpiForWindow(window)) / 96.0f;
        const LONG border = IsZoomed(window)
            ? 0
            : static_cast<LONG>(8.0f * dpi);
        const bool left = cursor.x >= 0 && cursor.x < border;
        const bool right = cursor.x < width && cursor.x >= width - border;
        const bool top = cursor.y >= 0 && cursor.y < border;
        const bool bottom = cursor.y < height && cursor.y >= height - border;
        if (top && left)
            return HTTOPLEFT;
        if (top && right)
            return HTTOPRIGHT;
        if (bottom && left)
            return HTBOTTOMLEFT;
        if (bottom && right)
            return HTBOTTOMRIGHT;
        if (left)
            return HTLEFT;
        if (right)
            return HTRIGHT;
        if (top)
            return HTTOP;
        if (bottom)
            return HTBOTTOM;

        const LONG caption_height = static_cast<LONG>(100.0f * dpi);
        const LONG controls_width = static_cast<LONG>(130.0f * dpi);
        const LONG controls_height = static_cast<LONG>(46.0f * dpi);
        if (cursor.y < caption_height &&
            !(cursor.x > width - controls_width &&
              cursor.y < controls_height))
        {
            return HTCAPTION;
        }
        return HTCLIENT;
    }

    case WM_GETMINMAXINFO:
    {
        auto* min_max = reinterpret_cast<MINMAXINFO*>(l_param);
        const HMONITOR monitor = MonitorFromWindow(
            window, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitor_info{};
        monitor_info.cbSize = sizeof(monitor_info);
        if (GetMonitorInfoW(monitor, &monitor_info))
        {
            const RECT& work = monitor_info.rcWork;
            const RECT& bounds = monitor_info.rcMonitor;
            min_max->ptMaxPosition.x = work.left - bounds.left;
            min_max->ptMaxPosition.y = work.top - bounds.top;
            min_max->ptMaxSize.x = work.right - work.left;
            min_max->ptMaxSize.y = work.bottom - work.top;
        }
        const float dpi = static_cast<float>(GetDpiForWindow(window)) / 96.0f;
        min_max->ptMinTrackSize.x = static_cast<LONG>(600.0f * dpi);
        min_max->ptMinTrackSize.y = static_cast<LONG>(460.0f * dpi);
        return 0;
    }

    case WM_SIZE:
        if (g_device && w_param != SIZE_MINIMIZED)
        {
            g_present_parameters.BackBufferWidth = LOWORD(l_param);
            g_present_parameters.BackBufferHeight = HIWORD(l_param);
            ResetD3DDevice();
        }
        return 0;

    case WM_DPICHANGED:
    {
        const auto* recommended = reinterpret_cast<const RECT*>(l_param);
        SetWindowPos(
            window,
            nullptr,
            recommended->left,
            recommended->top,
            recommended->right - recommended->left,
            recommended->bottom - recommended->top,
            SWP_NOACTIVATE | SWP_NOZORDER);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_SYSCOMMAND:
        if ((w_param & 0xFFF0U) == SC_KEYMENU)
            return 0;
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(window, message, w_param, l_param);
}
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, wchar_t*, int show_command)
{
    if (!ClosePreviousTrainerInstance())
    {
        MessageBoxW(
            nullptr,
            L"L'ancienne version du trainer n'a pas pu se fermer proprement. "
            L"Fermez-la puis relancez cette application afin d'éviter deux hooks simultanés.",
            kWindowTitle,
            MB_OK | MB_ICONWARNING);
        return 2;
    }

    hd::StartDiagnosticSession(
        "HD Final Advanced V106 - KEYS");

    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_CLASSDC;
    window_class.lpfnWndProc = WindowProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = static_cast<HBRUSH>(
        GetStockObject(BLACK_BRUSH));
    window_class.lpszClassName = kWindowClassName;

    if (!RegisterClassExW(&window_class))
        return 1;

    RECT work_area{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
    const int work_width = work_area.right - work_area.left;
    const int work_height = work_area.bottom - work_area.top;
    const float system_dpi = static_cast<float>(GetDpiForSystem()) / 96.0f;
    int window_width = (std::min)(
        static_cast<int>(820.0f * system_dpi), work_width - 36);
    int window_height = (std::min)(
        static_cast<int>(740.0f * system_dpi), work_height - 36);
    window_width = (std::max)(window_width, (std::min)(600, work_width - 12));
    window_height = (std::max)(window_height, (std::min)(460, work_height - 12));
    const int window_x = work_area.left + (work_width - window_width) / 2;
    const int window_y = work_area.top + (work_height - window_height) / 2;
    constexpr DWORD window_style =
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX |
        WS_MAXIMIZEBOX | WS_SYSMENU;

    HWND window = CreateWindowExW(
        WS_EX_APPWINDOW,
        kWindowClassName,
        kWindowTitle,
        window_style,
        window_x,
        window_y,
        window_width,
        window_height,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (!window)
    {
        UnregisterClassW(kWindowClassName, instance);
        return 1;
    }

    if (!CreateD3DDevice(window))
    {
        CleanupD3DDevice();
        DestroyWindow(window);
        UnregisterClassW(kWindowClassName, instance);
        MessageBoxW(nullptr, L"Impossible d'initialiser DirectX 9.", kWindowTitle, MB_ICONERROR);
        return 1;
    }

    ShowWindow(window, show_command);
    UpdateWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImGui::StyleColorsDark();
    hd::ui::Initialize(
        g_device,
        static_cast<float>(GetDpiForWindow(window)) / 96.0f);

    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX9_Init(g_device);
    g_imgui_initialized = true;

    hd::TrainerProcess game_process;
    // Product bindings are fixed. Legacy registry profiles are intentionally
    // ignored so the four visible cheats always remain F3/F4/F6/F7.
    hd::CheatHotkeys cheat_hotkeys{};
    hd::InventoryMode inventory_mode = hd::LoadInventoryMode();
    std::uint32_t inventory_item_count = hd::LoadInventoryItemCount();
    hd::CheatSequenceResult sequence_result = hd::CheatSequenceResult::NeverRun;
    // A cheat typed into the game needs H&D in front, but the trainer must
    // never put it there: ticking a box while the panel is open would throw
    // the player back into the mission and type there. Hold the request and
    // replay it when they return to the game themselves.
    hd::CheatId pending_cheat = hd::CheatId::Count;
    hd::InventoryMode pending_inventory_mode =
        hd::InventoryMode::ChooseSpecificItem;
    DWORD logged_connection_process_id = 0;
    HWND logged_connection_window = nullptr;
    hd::RadarRenderSettings radar_render_settings{};
    hd::WeaponSettings weapon_settings{};
    hd::GameplaySettings gameplay_settings{};
    hd::GameplayStatus gameplay_status{};
    hd::RadarSnapshot radar_snapshot{};
    DWORD radar_snapshot_process_id = 0;
    ULONGLONG last_radar_snapshot_tick = 0;
    ULONGLONG next_process_scan = 0;
    std::array<bool, hd::kCheatCount> cheat_hotkey_was_down{};
    bool map_hotkey_was_down = false;
    bool map_click_was_down = false;
    bool grant_all_hotkey_was_down = false;
    DWORD grant_all_hotkey_process_id = 0;
    std::uintptr_t grant_all_hotkey_mission = 0;
    std::uintptr_t grant_all_hotkey_player = 0;
    bool grant_all_hotkey_consumed = false;
    std::uint64_t fullhands_test_event = 0;
    std::uint64_t teleport_test_event = 0;
    bool vehicle_speed_up_was_down = false;
    bool vehicle_speed_down_was_down = false;
    bool steering_boost_up_was_down = false;
    bool steering_boost_down_was_down = false;
    bool game_speed_up_was_down = false;
    bool game_speed_down_was_down = false;
    bool noclip_toggle_was_down = false;
    bool network_position_publish_was_down = false;
    const wchar_t* network_position_notification = nullptr;
    ULONGLONG network_position_notification_until = 0;
    const HHOOK noclip_keyboard_hook = SetWindowsHookExW(
        WH_KEYBOARD_LL,
        NoclipKeyboardFilter,
        GetModuleHandleW(nullptr),
        0);
    hd::LogDiagnostic(
        "Noclip: selective keyboard hook installed=%d.",
        noclip_keyboard_hook ? 1 : 0);
    bool done = false;

    while (!done)
    {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (message.message == WM_QUIT)
                done = true;
        }

        if (done)
            break;

        const ULONGLONG now = GetTickCount64();
        if (now >= next_process_scan)
        {
            game_process.RefreshConnection(kGameExecutable);
            next_process_scan = now + kProcessScanIntervalMs;
            // Launching the trainer before the game used to leave it bound to
            // whatever window hde.exe owned while starting. Record every
            // change so a wrong election is visible instead of silent.
            const DWORD connected_process_id = game_process.ProcessId();
            const HWND connected_window = game_process.WindowHandle();
            if (connected_process_id != logged_connection_process_id ||
                connected_window != logged_connection_window)
            {
                hd::RemoteModuleInfo connected_module{};
                const bool module_read =
                    game_process.GetMainModuleInfo(connected_module);
                hd::LogDiagnostic(
                    "Process: pid=%lu window=%08X module_base=%08X "
                    "module_size=%u (was pid=%lu window=%08X).",
                    static_cast<unsigned long>(connected_process_id),
                    static_cast<unsigned>(
                        reinterpret_cast<std::uintptr_t>(connected_window)),
                    static_cast<unsigned>(
                        module_read ? connected_module.base_address : 0),
                    static_cast<unsigned>(
                        module_read ? connected_module.image_size : 0),
                    static_cast<unsigned long>(logged_connection_process_id),
                    static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(
                        logged_connection_window)));
                logged_connection_process_id = connected_process_id;
                logged_connection_window = connected_window;
            }
        }

        const bool radar_available =
            hd::ReadRadarSnapshot(game_process, radar_snapshot);
        if (radar_available)
        {
            radar_snapshot_process_id = game_process.ProcessId();
            last_radar_snapshot_tick = now;
        }
        // H&D nulls its global mission pointer for a frame or two during normal
        // play, not only when the trainer takes the foreground. Dropping the
        // snapshot there made every gameplay hook restore and reinstall in a
        // loop. Keep the last complete snapshot for a short grace period
        // whichever window is active, and mark it as cached: everything that
        // targets a live actor (Bullet Track, aimbot) refuses to act on it, so
        // no stale actor pointer is ever handed to the game.
        const bool cached_radar_available =
            !radar_available && game_process.IsConnected() &&
            radar_snapshot_process_id == game_process.ProcessId() &&
            now - last_radar_snapshot_tick <= 2000ULL;
        radar_snapshot.from_cache = cached_radar_available;
        const hd::RadarSnapshot* active_radar_snapshot =
            (radar_available || cached_radar_available)
            ? &radar_snapshot
            : nullptr;

        // M is reserved for catalogue rotation while the game has focus. The
        // low-level hook captures it for this edge detector and keeps it away
        // from the game's native inventory binding.
        const DWORD current_process_id = game_process.ProcessId();
        if (current_process_id != grant_all_hotkey_process_id)
        {
            grant_all_hotkey_process_id = current_process_id;
            grant_all_hotkey_mission = 0;
            grant_all_hotkey_player = 0;
            grant_all_hotkey_consumed = false;
        }
        gameplay_settings.grant_all_items_consumed =
            grant_all_hotkey_consumed;
        if (radar_available)
        {
            grant_all_hotkey_mission =
                radar_snapshot.entity_list_object_address;
            grant_all_hotkey_player = radar_snapshot.player_object_address;
        }
        hd::UpdateWeaponModifiers(
            game_process,
            active_radar_snapshot,
            weapon_settings);

        const bool reserved_input_active =
            game_process.IsGameWindowActive() || GetForegroundWindow() == window;
        const DWORD game_window_thread = GetWindowThreadProcessId(
            game_process.WindowHandle(), nullptr);
        const HKL game_keyboard_layout = game_window_thread != 0
            ? GetKeyboardLayout(game_window_thread)
            : GetKeyboardLayout(0);
        const bool map_click_down =
            (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        const auto physical_key_down = [game_keyboard_layout](UINT scan_code)
        {
            const UINT virtual_key = MapVirtualKeyExW(
                scan_code, MAPVK_VSC_TO_VK_EX, game_keyboard_layout);
            return virtual_key != 0 &&
                (GetAsyncKeyState(static_cast<int>(virtual_key)) & 0x8000) != 0;
        };
        // Physical K key on the French AZERTY keyboard.
        const bool map_hotkey_down = physical_key_down(0x25U);
        // Accept both the physical AZERTY M position and the logical M key.
        // The transition bit catches a short tap that begins and ends between
        // two rendered frames (notably after Fullhands stalls one frame while
        // its main-thread trampoline completes).
        const UINT grant_all_physical_vk = MapVirtualKeyExW(
            0x27U, MAPVK_VSC_TO_VK_EX, game_keyboard_layout);
        const SHORT grant_all_physical_state = grant_all_physical_vk != 0
            ? GetAsyncKeyState(static_cast<int>(grant_all_physical_vk))
            : 0;
        const SHORT grant_all_logical_state = grant_all_physical_vk == 'M'
            ? grant_all_physical_state
            : GetAsyncKeyState('M');
        const bool grant_all_hotkey_down =
            g_inventory_series_down.load() ||
            (grant_all_physical_state & 0x8000) != 0 ||
            (grant_all_logical_state & 0x8000) != 0;
        const bool grant_all_hotkey_transition =
            (grant_all_physical_state & 0x0001) != 0 ||
            (grant_all_logical_state & 0x0001) != 0;
        const bool game_input_active = game_process.IsGameWindowActive();
        g_noclip_keyboard_context.store(game_input_active);
        g_inventory_series_keyboard_filter.store(game_input_active);
        g_noclip_keyboard_filter.store(
            game_input_active && gameplay_settings.noclip_active);
        g_noclip_hotkey.store(gameplay_settings.noclip_hotkey_enabled);
        if (!game_input_active)
            ResetNoclipCapturedKeys();
        hd::GameplayInput gameplay_input{};
        const bool noclip_toggle_down =
            game_input_active && gameplay_settings.noclip_hotkey_enabled &&
            g_noclip_toggle_down.load();
        gameplay_input.toggle_noclip_pressed =
            noclip_toggle_down && !noclip_toggle_was_down;
        noclip_toggle_was_down = noclip_toggle_down;
        gameplay_input.noclip_input_capture_available =
            noclip_keyboard_hook != nullptr;
        const bool network_position_publish_down =
            game_input_active && gameplay_settings.network_position_mask_enabled &&
            (GetAsyncKeyState('W') & 0x8000) != 0;
        gameplay_input.publish_real_position_pressed =
            network_position_publish_down && !network_position_publish_was_down;
        network_position_publish_was_down = network_position_publish_down;
        gameplay_input.noclip_forward_down =
            game_input_active && g_noclip_forward_down.load();
        gameplay_input.noclip_backward_down =
            game_input_active && g_noclip_backward_down.load();
        gameplay_input.noclip_left_down =
            game_input_active && g_noclip_left_down.load();
        gameplay_input.noclip_right_down =
            game_input_active && g_noclip_right_down.load();
        gameplay_input.noclip_up_down =
            game_input_active && g_noclip_up_down.load();
        gameplay_input.noclip_down_down =
            game_input_active && g_noclip_down_down.load();
        gameplay_input.speed_up_down = reserved_input_active &&
            (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
        gameplay_input.speed_down_down = reserved_input_active &&
            (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
        // Vehicle multiplier: physical N (0x31) accelerates, physical B (0x30)
        // reduces. Identical physical positions on AZERTY and QWERTY.
        const bool vehicle_speed_up_down =
            game_input_active && !gameplay_settings.noclip_active &&
            physical_key_down(0x31U);
        const bool vehicle_speed_down_down =
            game_input_active && !gameplay_settings.noclip_active &&
            physical_key_down(0x30U);
        gameplay_input.vehicle_speed_up_pressed =
            vehicle_speed_up_down && !vehicle_speed_up_was_down;
        gameplay_input.vehicle_speed_down_pressed =
            vehicle_speed_down_down && !vehicle_speed_down_was_down;
        vehicle_speed_up_was_down = vehicle_speed_up_down;
        vehicle_speed_down_was_down = vehicle_speed_down_down;
        // Steering sensitivity: physical I (0x17) raises it, physical U (0x16)
        // lowers it. Same physical positions on AZERTY and QWERTY, and read
        // exactly like N/B so it can be adjusted while driving.
        const bool steering_boost_up_down =
            game_input_active && !gameplay_settings.noclip_active &&
            physical_key_down(0x17U);
        const bool steering_boost_down_down =
            game_input_active && !gameplay_settings.noclip_active &&
            physical_key_down(0x16U);
        gameplay_input.steering_boost_up_pressed =
            steering_boost_up_down && !steering_boost_up_was_down;
        gameplay_input.steering_boost_down_pressed =
            steering_boost_down_down && !steering_boost_down_was_down;
        steering_boost_up_was_down = steering_boost_up_down;
        steering_boost_down_was_down = steering_boost_down_down;
        // Vitesse du jeu : touches physiques 9 (scan 0x0A) et 8 (scan 0x09)
        // de la rangée du haut. Mêmes positions physiques en AZERTY et en
        // QWERTY, lues comme N/B pour pouvoir ajuster sans quitter la partie.
        // Ces deux touches sont AUSSI des chiffres. Quand la fenetre des
        // soldats est ouverte, le joueur y tape un nombre : la vitesse du jeu
        // ne doit pas bouger sous ses doigts.
        const bool typing_soldier_count = gameplay_settings.soldier_menu_open;
        const bool game_speed_up_down =
            game_input_active && !typing_soldier_count &&
            physical_key_down(0x0AU);
        const bool game_speed_down_down =
            game_input_active && !typing_soldier_count &&
            physical_key_down(0x09U);
        gameplay_input.game_speed_up_pressed =
            game_speed_up_down && !game_speed_up_was_down;
        gameplay_input.game_speed_down_pressed =
            game_speed_down_down && !game_speed_down_was_down;
        game_speed_up_was_down = game_speed_up_down;
        game_speed_down_was_down = game_speed_down_down;
        gameplay_input.toggle_map_pressed =
            game_input_active &&
            map_hotkey_down && !map_hotkey_was_down;
        if (gameplay_input.toggle_map_pressed)
        {
            ++teleport_test_event;
            hd::LogDiagnostic(
                "[TEST TELEPORT #%llu] K_EDGE game_active=%d map_enabled=%d "
                "map_open_cached=%d radar=%d game_pid=%lu mission=%08X "
                "player=%08X player_world=(%.3f,%.3f,%.3f).",
                static_cast<unsigned long long>(teleport_test_event),
                game_input_active ? 1 : 0,
                gameplay_settings.teleport_map_enabled ? 1 : 0,
                gameplay_settings.teleport_map_open ? 1 : 0,
                radar_available ? 1 : 0,
                static_cast<unsigned long>(current_process_id),
                static_cast<unsigned>(grant_all_hotkey_mission),
                static_cast<unsigned>(grant_all_hotkey_player),
                radar_available ? radar_snapshot.player.position.x : 0.0f,
                radar_available ? radar_snapshot.player.position.y : 0.0f,
                radar_available ? radar_snapshot.player.position.z : 0.0f);
        }
        gameplay_input.native_map_click_pressed =
            game_process.IsGameWindowActive() &&
            gameplay_settings.teleport_map_open &&
            gameplay_settings.teleport_map_armed &&
            map_click_down && !map_click_was_down;
        bool native_cursor_captured = false;
        if (gameplay_input.native_map_click_pressed)
        {
            POINT cursor{};
            if (GetCursorPos(&cursor) &&
                ScreenToClient(game_process.WindowHandle(), &cursor))
            {
                native_cursor_captured = true;
                gameplay_input.native_map_client_x =
                    static_cast<float>(cursor.x);
                gameplay_input.native_map_client_y =
                    static_cast<float>(cursor.y);
            }
        }
        if (game_process.IsGameWindowActive() &&
            map_click_down && !map_click_was_down)
        {
            ++teleport_test_event;
            hd::LogDiagnostic(
                "[TEST TELEPORT #%llu] LBUTTON_EDGE enabled=%d "
                "map_open_cached=%d accepted=%d cursor_captured=%d "
                "client=(%.3f,%.3f) foreground=%p game_window=%p.",
                static_cast<unsigned long long>(teleport_test_event),
                gameplay_settings.teleport_map_enabled ? 1 : 0,
                gameplay_settings.teleport_map_open ? 1 : 0,
                gameplay_input.native_map_click_pressed ? 1 : 0,
                native_cursor_captured ? 1 : 0,
                gameplay_input.native_map_client_x,
                gameplay_input.native_map_client_y,
                static_cast<void*>(GetForegroundWindow()),
                static_cast<void*>(game_process.WindowHandle()));
        }
        const bool grant_all_button_pressed =
            gameplay_settings.grant_all_items_requested &&
            !grant_all_hotkey_consumed;
        const bool grant_all_hotkey_event =
            grant_all_hotkey_transition ||
            (grant_all_hotkey_down && !grant_all_hotkey_was_down);
        const bool grant_all_hotkey_pressed =
            reserved_input_active && !grant_all_hotkey_consumed &&
            grant_all_hotkey_event;
        gameplay_input.grant_all_items_pressed =
            grant_all_button_pressed ||
            grant_all_hotkey_pressed;
        if (grant_all_hotkey_event || grant_all_button_pressed)
        {
            ++fullhands_test_event;
            const bool consumed_before = grant_all_hotkey_consumed;
            if (gameplay_input.grant_all_items_pressed)
                grant_all_hotkey_consumed = true;
            gameplay_settings.grant_all_items_consumed =
                grant_all_hotkey_consumed;
            hd::LogDiagnostic(
                "[TEST FULLHANDS #%llu] INPUT source=%s request_grant=%d "
                "consumed_before=%d consumed_after=%d reserved_active=%d "
                "radar=%d game_pid=%lu mission=%08X player=%08X "
                "mapped_vk=%02X physical_state=%04X logical_state=%04X.",
                static_cast<unsigned long long>(fullhands_test_event),
                grant_all_button_pressed ? "button" : "M key",
                gameplay_input.grant_all_items_pressed ? 1 : 0,
                consumed_before ? 1 : 0,
                grant_all_hotkey_consumed ? 1 : 0,
                reserved_input_active ? 1 : 0,
                radar_available ? 1 : 0,
                static_cast<unsigned long>(current_process_id),
                static_cast<unsigned>(grant_all_hotkey_mission),
                static_cast<unsigned>(grant_all_hotkey_player),
                grant_all_physical_vk,
                static_cast<unsigned short>(grant_all_physical_state),
                static_cast<unsigned short>(grant_all_logical_state));
        }
        gameplay_settings.grant_all_items_requested = false;

        // J : ouvre ou ferme la fenetre des armes. Le joueur y choisit une
        // cible - un soldat, un groupe, ou tous - puis une arme de son propre
        // inventaire, et les soldats designes la recoivent.
        {
            static bool weapon_menu_was_down = false;
            const bool down = game_process.IsGameWindowActive() &&
                (GetAsyncKeyState('J') & 0x8000) != 0;
            if (down && !weapon_menu_was_down)
                gameplay_settings.weapon_menu_toggle_requested = true;
            weapon_menu_was_down = down;
        }
        map_hotkey_was_down = map_hotkey_down;
        map_click_was_down = map_click_down;
        grant_all_hotkey_was_down = grant_all_hotkey_down;

        // Navigation de la liste des vehicules, uniquement quand elle est
        // ouverte et que le jeu est devant. Les touches restent transmises au
        // jeu, comme W pour le masque de position.
        {
            static bool menu_up_was_down = false;
            static bool menu_down_was_down = false;
            static bool menu_enter_was_down = false;
            static bool menu_escape_was_down = false;
            static bool menu_clone_was_down = false;
            static bool menu_left_was_down = false;
            static bool menu_right_was_down = false;
            const bool menu_active =
                (gameplay_settings.vehicle_menu_open ||
                 gameplay_settings.soldier_menu_open ||
                 gameplay_settings.weapon_menu_open) &&
                game_process.IsGameWindowActive();
            const bool soldier_menu = gameplay_settings.soldier_menu_open;
            const bool weapon_menu = gameplay_settings.weapon_menu_open;
            const bool up = menu_active &&
                (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
            const bool down = menu_active &&
                (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
            const bool enter = menu_active &&
                (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
            const bool escape = menu_active &&
                (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
            const bool left = menu_active && (soldier_menu || weapon_menu) &&
                (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0;
            const bool right = menu_active && (soldier_menu || weapon_menu) &&
                (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;
            const bool clone = menu_active && !soldier_menu &&
                (GetAsyncKeyState('C') & 0x8000) != 0;
            if (clone && !menu_clone_was_down)
                gameplay_settings.vehicle_menu_clone_requested = true;
            menu_clone_was_down = clone;
            if (left && !menu_left_was_down)
            {
                if (weapon_menu)
                    gameplay_settings.weapon_menu_target_move = -1;
                else
                    gameplay_settings.soldier_menu_adjust = -1;
            }
            if (right && !menu_right_was_down)
            {
                if (weapon_menu)
                    gameplay_settings.weapon_menu_target_move = 1;
                else
                    gameplay_settings.soldier_menu_adjust = 1;
            }
            menu_left_was_down = left;
            menu_right_was_down = right;

            // Saisie directe du nombre de soldats. Les chiffres sont lus a la
            // fois sur la rangee du haut et sur le pave numerique; sur un
            // clavier AZERTY le code virtuel reste VK_1..VK_0 quelle que soit
            // la position de la majuscule, la frappe passe donc dans les deux
            // dispositions.
            static bool menu_digit_was_down[10] = {};
            static bool menu_erase_was_down = false;
            for (int digit = 0; digit < 10; ++digit)
            {
                const bool digit_down = menu_active && soldier_menu &&
                    (((GetAsyncKeyState('0' + digit) & 0x8000) != 0) ||
                     ((GetAsyncKeyState(VK_NUMPAD0 + digit) & 0x8000) != 0));
                if (digit_down && !menu_digit_was_down[digit])
                    gameplay_settings.soldier_menu_digit = digit;
                menu_digit_was_down[digit] = digit_down;
            }
            const bool erase = menu_active && soldier_menu &&
                (GetAsyncKeyState(VK_BACK) & 0x8000) != 0;
            if (erase && !menu_erase_was_down)
                gameplay_settings.soldier_menu_erase = true;
            menu_erase_was_down = erase;
            if (up && !menu_up_was_down)
            {
                if (weapon_menu)
                    gameplay_settings.weapon_menu_move = -1;
                else if (soldier_menu)
                    gameplay_settings.soldier_menu_move = -1;
                else
                    gameplay_settings.vehicle_menu_move = -1;
            }
            if (down && !menu_down_was_down)
            {
                if (weapon_menu)
                    gameplay_settings.weapon_menu_move = 1;
                else if (soldier_menu)
                    gameplay_settings.soldier_menu_move = 1;
                else
                    gameplay_settings.vehicle_menu_move = 1;
            }
            if (enter && !menu_enter_was_down)
            {
                if (weapon_menu)
                    gameplay_settings.weapon_menu_confirm_requested = true;
                else if (soldier_menu)
                    gameplay_settings.soldier_menu_confirm_requested = true;
                else
                    gameplay_settings.vehicle_menu_confirm_requested = true;
            }
            if (escape && !menu_escape_was_down)
            {
                if (weapon_menu)
                    gameplay_settings.weapon_menu_toggle_requested = true;
                else if (soldier_menu)
                    gameplay_settings.soldier_menu_toggle_requested = true;
                else
                    gameplay_settings.vehicle_menu_toggle_requested = true;
            }
            menu_up_was_down = up;
            menu_down_was_down = down;
            menu_enter_was_down = enter;
            menu_escape_was_down = escape;
        }

        hd::UpdateGameplayModifiers(
            game_process,
            active_radar_snapshot,
            gameplay_settings,
            gameplay_input,
            gameplay_status);
        if (gameplay_settings.network_position_mask_enabled &&
            gameplay_input.publish_real_position_pressed)
        {
            if (gameplay_status.network_position_mask ==
                hd::NetworkPositionMaskStatus::RemoteVisualHidden)
            {
                network_position_notification = L"Maintenant invisible";
                network_position_notification_until = now + 2000U;
                hd::LogDiagnostic(
                    "Network position mask: local notification=invisible.");
            }
            else if (gameplay_status.network_position_mask ==
                hd::NetworkPositionMaskStatus::RealPosition)
            {
                network_position_notification = L"Maintenant visible";
                network_position_notification_until = now + 2000U;
                hd::LogDiagnostic(
                    "Network position mask: local notification=visible.");
            }
        }
        if (gameplay_input.grant_all_items_pressed)
        {
            hd::LogDiagnostic(
                "[TEST FULLHANDS #%llu] RESULT status=%u granted_count=%u.",
                static_cast<unsigned long long>(fullhands_test_event),
                static_cast<unsigned>(gameplay_status.grant_all_items),
                gameplay_status.granted_item_count);
            // M remains armed: each accepted press replaces the complete lot
            // and advances to the next of seven series.
            grant_all_hotkey_consumed = false;
            gameplay_settings.grant_all_items_consumed = false;
            hd::LogDiagnostic(
                gameplay_status.grant_all_items ==
                        hd::GrantAllItemsStatus::Success
                    ? "[TEST FULLHANDS #%llu] ACTION_REARMED_FOR_NEXT_SERIES."
                    : "[TEST FULLHANDS #%llu] ACTION_REARMED_AFTER_FAILURE.",
                static_cast<unsigned long long>(fullhands_test_event));
        }
        hd::CheatId hotkey_cheat = hd::CheatId::Count;
        for (std::size_t index = 0; index < hd::kCheatCount; ++index)
        {
            const hd::CheatId candidate = static_cast<hd::CheatId>(index);
            const int virtual_key = cheat_hotkeys.virtual_keys[index];
            // V98 : G et F6 agissent dans le jeu. Contrairement aux touches
            // de fonction, G est une lettre : ne jamais la lire pendant que
            // le joueur tape ailleurs.
            const bool in_game_only =
                candidate == hd::CheatId::VehicleRepair ||
                candidate == hd::CheatId::VehicleMenu;
            if (in_game_only && !game_process.IsGameWindowActive())
            {
                cheat_hotkey_was_down[index] = false;
                continue;
            }
            const bool key_is_down = candidate == hd::CheatId::ReviveCurrentPlayer
                ? (g_revive_current_player_down.load() ||
                   (GetAsyncKeyState(VK_F10) & 0x8000) != 0)
                : (virtual_key != 0 &&
                   (GetAsyncKeyState(virtual_key) & 0x8000) != 0);
            const bool key_pressed = key_is_down && !cheat_hotkey_was_down[index];
            cheat_hotkey_was_down[index] = key_is_down;

            if (key_pressed && hotkey_cheat == hd::CheatId::Count)
            {
                hotkey_cheat = candidate;
                if (candidate == hd::CheatId::ReviveCurrentPlayer)
                {
                    hd::LogDiagnostic(
                        "F10 EDGE: keyboard hook=%u async=%u game_active=%u.",
                        g_revive_current_player_down.load() ? 1U : 0U,
                        (GetAsyncKeyState(VK_F10) & 0x8000) != 0 ? 1U : 0U,
                        game_process.IsGameWindowActive() ? 1U : 0U);
                }
            }
        }

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        const hd::CheatId clicked_cheat = hd::ui::DrawTrainerWindow(
            window,
            game_process,
            sequence_result,
            radar_render_settings,
            weapon_settings,
            gameplay_settings,
            gameplay_status);

        const hd::CheatId requested_cheat = clicked_cheat != hd::CheatId::Count
            ? clicked_cheat
            : hotkey_cheat;
        // V97 : quatre des six commandes sont natives et se traitent dans la
        // mise a jour du gameplay, sans jamais taper au clavier ni exiger que
        // le jeu soit au premier plan.
        if (requested_cheat == hd::CheatId::Ironman)
        {
            gameplay_settings.extended_health_toggle_requested = true;
            hd::LogDiagnostic("F4 SANTE MAX: bascule demandee.");
        }
        else if (requested_cheat == hd::CheatId::Fullhands)
        {
            // Meme moteur que la touche M : le cheat `fullhands` du jeu est
            // encadre par `if(!net)` et ne fait rien en partie reseau.
            gameplay_settings.grant_all_items_requested = true;
            hd::LogDiagnostic("F5 FULLHANDS: serie suivante demandee (natif).");
        }
        else if (requested_cheat == hd::CheatId::VehicleExit)
        {
            gameplay_settings.vehicle_exit_requested = true;
            hd::LogDiagnostic("F3 SORTIE VEHICULE: demandee.");
        }
        else if (requested_cheat == hd::CheatId::VehicleRepair)
        {
            // V125 - G demande la reparation; a pied, ce chemin retombe sur
            // l'ouverture de la fenetre des soldats. Le journal du joueur
            // montrait les deux lignes cote a cote a chaque appui, ce qui
            // laissait croire qu'une reparation avait ete tentee alors qu'il
            // etait a pied. C'est `UpdateVehicleRepair` qui dit maintenant ce
            // qui a reellement eu lieu.
            gameplay_settings.vehicle_repair_requested = true;
        }
        else if (requested_cheat == hd::CheatId::VehicleMenu)
        {
            gameplay_settings.vehicle_menu_toggle_requested = true;
            hd::LogDiagnostic("F6 MENU VEHICULES: bascule demandee.");
        }
        else if (requested_cheat == hd::CheatId::ReviveCurrentPlayer)
        {
            // F10 is a game-state action, not a text cheat. It is consumed by
            // UpdateGameplayModifiers on this exact frame and never routes to
            // the native `newlife` text command (which H&D disables in LAN).
            gameplay_settings.revive_current_player_requested = true;
            hd::LogDiagnostic("F10 REQUEST queued for gameplay update.");
        }
        else if (requested_cheat == hd::CheatId::RepairCurrentPlayer)
        {
            // F12 is deliberately a local native model/scene transition.  It
            // repairs an already-alive player left visually as a skeleton by
            // an old explosion without sending a death packet to the peer.
            gameplay_settings.repair_current_player_requested = true;
            hd::LogDiagnostic("F12 REPAIR REQUEST queued for gameplay update.");
        }
        else if (requested_cheat != hd::CheatId::Count)
        {
            pending_cheat = requested_cheat;
            pending_inventory_mode =
                requested_cheat == hd::CheatId::Fullhands
                ? hd::InventoryMode::ChooseSpecificItem
                : inventory_mode;
            sequence_result = hd::CheatSequenceResult::GameNotActive;
        }
        // Replay as soon as H&D is in front of its own accord. A request made
        // from the panel therefore costs nothing until the player goes back,
        // and one made through its hotkey while playing runs on this very
        // frame, exactly as before.
        if (pending_cheat != hd::CheatId::Count &&
            game_process.IsGameWindowActive())
        {
            const hd::CheatId applied_cheat = pending_cheat;
            pending_cheat = hd::CheatId::Count;
            // Life Unlimited must not go through SendInput/BlockInput. H&D's
            // old cheat reader can reject those artificial keystrokes while
            // the trainer remains open. The native callback runs in the
            // player's own Tick instead.
            sequence_result = hd::ApplySingleCheat(
                game_process,
                applied_cheat,
                pending_inventory_mode,
                inventory_item_count);
            hd::LogDiagnostic(
                "Cheat: deferred request %d applied on return to the game "
                "(result=%d).",
                static_cast<int>(applied_cheat),
                static_cast<int>(sequence_result));
            // A refusal that only means "not active" would loop forever.
            if (sequence_result == hd::CheatSequenceResult::GameNotActive)
                pending_cheat = applied_cheat;
        }

        ImGui::Render();
        // Bullet Track targets every eligible green head globally; a circle
        // would incorrectly suggest that the cursor still limits acquisition.
        radar_render_settings.show_bullet_track_circle = false;
        radar_render_settings.bullet_track_radius_pixels =
            gameplay_settings.bullet_track_radius_pixels;
        radar_render_settings.show_bullet_track_v2_tracer =
            gameplay_status.bullet_track_visual_target_actor != 0;
        radar_render_settings.bullet_track_v2_tracer_target_actor =
            gameplay_status.bullet_track_visual_target_actor;
        radar_render_settings.transient_notification =
            now < network_position_notification_until
                ? network_position_notification
                : nullptr;
        std::vector<const wchar_t*> vehicle_menu_entries;
        radar_render_settings.menu_title = nullptr;
        radar_render_settings.menu_footer = nullptr;
        if (gameplay_settings.weapon_menu_open)
        {
            const std::size_t entry_count = hd::WeaponMenuEntryCount();
            vehicle_menu_entries.reserve(entry_count);
            for (std::size_t index = 0; index < entry_count; ++index)
            {
                const wchar_t* entry = hd::WeaponMenuEntry(index);
                if (entry)
                    vehicle_menu_entries.push_back(entry);
            }
            radar_render_settings.menu_title = L"ARMES DE VOS HOMMES";
            radar_render_settings.menu_footer =
                L"Gauche/Droite : la cible   Haut/Bas : l'arme   "
                L"Entree : la donner   Echap : fermer";
            radar_render_settings.vehicle_menu_selection =
                static_cast<std::int32_t>(hd::WeaponMenuSelection());
        }
        else if (gameplay_settings.soldier_menu_open)
        {
            const std::size_t entry_count = hd::SoldierMenuEntryCount();
            vehicle_menu_entries.reserve(entry_count);
            for (std::size_t index = 0; index < entry_count; ++index)
            {
                const wchar_t* entry = hd::SoldierMenuEntry(index);
                if (entry)
                    vehicle_menu_entries.push_back(entry);
            }
            // V142 - la fenetre porte desormais deux troupes : les soldats
            // crees et les ennemis rallies. Le titre le dit.
            radar_render_settings.menu_title = L"VOS SOLDATS ET VOS RALLIES";
            radar_render_settings.menu_footer =
                L"Tapez le nombre (1 puis 0 = 10)   Entree valide   "
                L"puis K ouvre la carte : cliquez ou ils doivent aller   "
                L"Echap ferme";
            radar_render_settings.vehicle_menu_selection =
                static_cast<std::int32_t>(hd::SoldierMenuSelection());
        }
        else if (gameplay_settings.vehicle_menu_open)
        {
            const std::size_t entry_count = hd::VehicleMenuEntryCount();
            vehicle_menu_entries.reserve(entry_count);
            for (std::size_t index = 0; index < entry_count; ++index)
            {
                const wchar_t* entry = hd::VehicleMenuEntry(index);
                if (entry)
                    vehicle_menu_entries.push_back(entry);
            }
            radar_render_settings.menu_footer =
                L"Haut/Bas : changer de ligne    Entree : voler ce vehicule    "
                L"C : en recreer une copie    Echap : fermer";
            radar_render_settings.vehicle_menu_selection =
                static_cast<std::int32_t>(hd::VehicleMenuSelection());
        }
        radar_render_settings.vehicle_menu_entries =
            vehicle_menu_entries.empty() ? nullptr : vehicle_menu_entries.data();
        radar_render_settings.vehicle_menu_count =
            static_cast<std::uint32_t>(vehicle_menu_entries.size());
        hd::RenderEspOverlay(
            active_radar_snapshot,
            radar_render_settings);

        g_device->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        g_device->Clear(
            0,
            nullptr,
            D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
            D3DCOLOR_RGBA(7, 8, 9, 255),
            1.0f,
            0);

        if (SUCCEEDED(g_device->BeginScene()))
        {
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_device->EndScene();
        }

        const HRESULT present_result = g_device->Present(nullptr, nullptr, nullptr, nullptr);
        if (present_result == D3DERR_DEVICELOST &&
            g_device->TestCooperativeLevel() == D3DERR_DEVICENOTRESET)
        {
            ResetD3DDevice();
        }

    }

    hd::RestoreWeaponModifiers(game_process);
    hd::RestoreGameplayModifiers(game_process);
    g_noclip_keyboard_filter.store(false);
    g_noclip_keyboard_context.store(false);
    g_noclip_hotkey.store(false);
    g_inventory_series_keyboard_filter.store(false);
    ResetNoclipCapturedKeys();
    if (noclip_keyboard_hook)
        UnhookWindowsHookEx(noclip_keyboard_hook);
    game_process.Disconnect();
    g_imgui_initialized = false;
    hd::ui::Shutdown();
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupD3DDevice();
    if (IsWindow(window))
        DestroyWindow(window);
    UnregisterClassW(kWindowClassName, instance);
    return 0;
}
