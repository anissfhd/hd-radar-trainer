#pragma once

#include "trainer_process.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace hd
{
struct CheatSelection
{
    bool immortality = false;
    bool ironman = false;
    bool fullhands = false;
    bool funnyhead = false;
    bool skipmission = false;
};

// V97 : « Life Unlimited » et « Big Heads » ont ete retires a la demande du
// joueur - la Protection reseau totale fait mieux que le premier, le second
// n'est que cosmetique. Des cinq restants, un seul est encore tape au clavier
// (Skipmission, le seul que le moteur honore reellement en partie reseau).
enum class CheatId : std::uint32_t
{
    Ironman = 0,          // F4  Sante Max, natif, interrupteur
    Fullhands,            // F5  natif, meme moteur que la touche M
    Skipmission,          // F7  cheat tape; valide en reseau
    ReviveCurrentPlayer,  // F10 natif
    RepairCurrentPlayer,  // F12 natif
    VehicleExit,          // F3  sortie forcee d'un vehicule
    VehicleMenu,          // F6  liste des vehicules de la mission
    VehicleRepair,        // G   remise en etat et placement devant soi
    Count
};

constexpr std::size_t kCheatCount = static_cast<std::size_t>(CheatId::Count);

struct CheatHotkeys
{
    std::array<int, kCheatCount> virtual_keys{
        VK_F4, VK_F5, VK_F7, VK_F10, VK_F12, VK_F3, VK_F6, 'G'};
};

enum class InventoryMode : std::uint32_t
{
    ChooseSpecificItem = 0,
    CollectAllItems = 1
};

constexpr std::uint32_t kDefaultInventoryItemCount = 12;
constexpr std::uint32_t kMaximumInventoryItemCount = 32;

enum class CheatSequenceResult
{
    NeverRun,
    Success,
    NotConnected,
    GameNotActive,
    NothingSelected,
    InputBlockFailed,
    InputFailed
};

CheatSelection LoadCheatProfile();
bool SaveCheatProfile(const CheatSelection& selection);
CheatHotkeys LoadCheatHotkeys();
bool SaveCheatHotkeys(const CheatHotkeys& hotkeys);
InventoryMode LoadInventoryMode();
bool SaveInventoryMode(InventoryMode mode);
std::uint32_t LoadInventoryItemCount();
bool SaveInventoryItemCount(std::uint32_t item_count);

CheatSequenceResult ApplyCheatSequence(
    const TrainerProcess& game_process,
    const CheatSelection& selection);

CheatSequenceResult ApplySingleCheat(
    const TrainerProcess& game_process,
    CheatId cheat,
    InventoryMode inventory_mode,
    std::uint32_t inventory_item_count);

const char* CheatSequenceResultText(CheatSequenceResult result);
}
