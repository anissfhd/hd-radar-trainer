#pragma once

#include <Windows.h>
#include <d3d9.h>

#include "cheat_sequence.h"
#include "gameplay_mods.h"
#include "radar.h"
#include "trainer_process.h"
#include "weapon_mods.h"

namespace hd::ui
{
void Initialize(IDirect3DDevice9* device, float dpi_scale);
void InvalidateDeviceObjects();
void CreateDeviceObjects(IDirect3DDevice9* device);
void Shutdown();

CheatId DrawTrainerWindow(
    HWND window,
    const TrainerProcess& game_process,
    CheatSequenceResult sequence_result,
    RadarRenderSettings& radar_render_settings,
    WeaponSettings& weapon_settings,
    GameplaySettings& gameplay_settings,
    GameplayStatus& gameplay_status);
}
