#pragma once

#include "radar.h"

namespace hd
{
// Options d'arme exposées par l'interface. Elles sont désactivées par défaut
// et ne ciblent que l'acteur local résolu par le snapshot radar.
struct WeaponSettings
{
    bool stable_precision = false;
    bool rapid_fire_unlimited = false;
};

// Point d'intégration isolé du chemin ESP. Les compteurs de tir et de munitions
// ciblent l'acteur local. Les valeurs de recul/dispersion de la définition de
// l'arme sélectionnée sont capturées puis restaurées au changement d'arme, à la
// désactivation de l'option ou à la fermeture normale du trainer.
void UpdateWeaponModifiers(
    TrainerProcess& game_process,
    const RadarSnapshot* radar_snapshot,
    const WeaponSettings& settings);

void RestoreWeaponModifiers(TrainerProcess& game_process);

// The actor is returned only once rapid fire has been validated for its
// currently equipped weapon. Gameplay uses it to re-arm that same actor on
// the game thread immediately after a real projectile is created.
std::uintptr_t ActiveRapidFireActor(TrainerProcess& game_process);
}
