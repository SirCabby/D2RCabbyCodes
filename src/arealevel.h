#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

// Area levels: the monster level of each area in the current difficulty, shown
// after the area's name in the automap's area text and in the waypoint list.
//
// The game's Levels table (through the loader's data-table service, read on the
// game thread once per game) gives each level's MonLvl columns. The client's
// level-name routine is hooked; while the automap or the waypoint panel is
// asking, it answers with the name plus " [level]".
namespace d2rcc::arealevel {

void set_services(const D2RL::PluginContext* ctx, const D2RL::DataTableService* tables);

// Game thread (the stat-regeneration tick): read the Levels table once per game.
bool collect();
bool ready();
void reset();  // a game was left: the next game reads the table again

// UI thread: the level for the current difficulty (-1 when unknown), and the
// name with the level appended, in a buffer that stays valid (one per level).
int level_of(int level_id);
const char* with_level(int level_id, const char* name);

}  // namespace d2rcc::arealevel
