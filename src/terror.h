#pragma once

#include <cstdint>

// All areas terrorized ("desecrated" in the game's code). A Worldstone Shard terrorizes its act: its use applies the
// act's manual zone (desecratedzones.json's manual_zones, one group per act, Act1-Manual .. Act5-Manual, each with
// every terrorizable level of the act) through the zone apply the half-hour rotation uses, as manual terror (kind 2)
// where the rotation's is kind 1. The rotation only ever takes kind 1 off, so a shard's terror lasts the game.
//
// While the switch is on, the tick applies every act's manual zone that way in the local player's game, and each
// rotation zone with a level no act zone covers (in 3.3 the Moo Moo Farm), and applies a zone again when some of its
// levels are found without manual terror. Switched off, the manual terror is taken off and the rotation's zone applied
// again, as a rotation does. All of it on the server thread (the tick), through the game's own routines.
namespace d2rcc::terror {

// The routines (sites.cpp names them from a shard's use and the zone job, and checks the layouts read here). Null when
// the switch can work, else why not.
const char* bind();

void on_tick(void* game, unsigned tick);  // the local player's game, every server frame (from the tick)
void game_left();                           // any thread: the next game starts over

// For the panel and the console (any thread).
struct Stats {
  bool in_game = false;     // the tick has run since the last game ended
  bool checked = false;     // the game was asked whether it has terror zones (while the switch was on)
  bool has_zones = false;   // ... and it has
  int areas = 0;            // levels terrorized in the game now, of either kind
  int zones = 0;            // zones the switch applied in this game
};
Stats stats();

}  // namespace d2rcc::terror
