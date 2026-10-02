#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

namespace d2rcc::game {
struct Unit;
}

// Item level after an item's name - "Grand Charm (91)" - the way the game's own ShowLevel column
// (weapons.txt, armor.txt, misc.txt) shows it, but switched from the panel. Two parts, both put in
// by hooks_game.cpp through the loader:
//   the item writer   the server puts a level in every item it sends the client: the real one for
//                     an Items row with ShowLevel, a placeholder 1 for the rest. A one-instruction
//                     patch makes it the real one always (the network form only; a save writes the
//                     real level on its own branch). It stays for the session, so the client already
//                     knows every level when the switch goes on.
//   the name builder  the client's item name (inv.cpp; D2RCore's tooltip code calls it). After it,
//                     under the switch, " (N)" for weapons (not throwing potions), armor, rings,
//                     amulets, charms and jewels - never a quest item, and not a row whose own
//                     ShowLevel already shows it (a D2RMM mod's).
// Which rows qualify comes from the game's Items and ItemTypes tables (through the loader, on the
// game thread, like consumables.cpp); the name builder runs on the UI thread.
namespace d2rcc::itemlevel {

using game::Unit;

void set_services(const D2RL::PluginContext* ctx, const D2RL::DataTableService* tables,
                  const D2RL::LocalizationService* strings);
bool has_tables();

// Game thread (the tick): classify every Items row of the three table banks. Once is enough; a
// bank that could not be read is tried again at the next call.
void collect();
bool ready(uint8_t bank);  // the bank (a unit's +0x1BD, 1 classic .. 3 rotw) is classified

// UI thread, right after the game's item-name builder filled `name` (cap bytes): the item's level
// after it, under the switch, for the items above.
void append(Unit* item, char* name, size_t cap);

unsigned names_shown();  // this session: names the level was added to

}  // namespace d2rcc::itemlevel
