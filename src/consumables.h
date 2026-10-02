#pragma once

#include <D2RLPlugin/api.h>

#include <cstdint>

namespace d2rcc::game {
struct Unit;
}

// Infinite town portal, identify and potions. Which items count comes from the game's Items table
// (through the loader, on the game thread): an item's code names the town portal and identify
// scrolls and tomes, its item type the potions (healing, mana, rejuvenation, stamina, antidote,
// thawing - never a quest item). Two hooks in hooks_game.cpp ask here:
//   ITEMS_ShouldRemoveOnUse   whether a used item goes. The client asks it to build its use request
//                             and the server asks it to check that request, so a scroll or potion
//                             under its switch stays on both sides (UI and game threads).
//   the tome quantity update  the item-use effect takes a tome's charge through it; under the
//                             switch that one call is skipped (merging tomes still moves charges).
// Infinite keys needs no table: a locked chest or door asks the server's key use for a key, and
// under the switch the local player's key opens it without being taken (the hook asks here).
namespace d2rcc::consumables {

using game::Unit;

void set_services(const D2RL::PluginContext* ctx, const D2RL::DataTableService* tables);
bool has_tables();

// Game thread (the tick): classify every Items row of the three table banks. Once is enough; a
// bank that could not be read is tried again at the next call.
void collect();
bool ready(uint8_t bank);  // the bank (a unit's +0x1BD, 1 classic .. 3 rotw) is classified

bool keep_after_use(Unit* item);                  // any thread: the used scroll or potion stays
bool keep_tome_charge(Unit* player, Unit* item);  // server thread: the use takes no charge
// Server thread: the key use opens the lock without taking a key - the player carries one it would take.
bool keep_key(Unit* player);

unsigned tome_charges_kept();  // this session (the counters the panel shows)
unsigned keys_kept();

}  // namespace d2rcc::consumables
