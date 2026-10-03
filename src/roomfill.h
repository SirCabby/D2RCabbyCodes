#pragma once

#include <cstdint>

namespace d2rcc::game {
struct Unit;
}

// Rooms filled ahead, for Landmarks on the map (approved 2026-10-03: "Fill on area entry"). The objects an area rolls
// as the server first fills each of its rooms - most shrines and wells of Act 3's jungle, Kurast and Travincal, of Acts
// 4 and 5 and of the dungeons, Act 5's outdoor waypoints - exist nowhere before: the game rolls them with the room's own
// seed after the room's random monsters have taken theirs, and with what the area's other rooms have rolled so far
// (the object group step 0x510F00). So the game is let fill an area's rooms as the character enters it: on the server
// thread (the tick), every room of the area not built yet is built with the game's own CreateActiveRoom, a few a
// server frame, which puts it on its act's list of rooms; the game's own fill pass (0x408670) fills each room of that
// list it has not filled - its presets, random monsters, object groups and terror - as it fills a room a player comes
// near. Once the area's rooms are filled, every object in them is read (its class and place) and handed to mapmarks,
// which puts those with an icon on the automap's layer of the area. Placement is the game's own rolls and rules; only
// the order the rooms are filled in is the area's list's, not the one the character walks in. Only the areas whose
// object groups can roll an object with an icon (mapmarks::level_rolls_icons); each once per game.
//
// Named enemies on the map (approved 2026-10-03: "Fill and read"): a random unique, a champion, a Herald exists nowhere
// before the server fills its room (its random monsters, 0x503790). Under the switch every area but a town is filled the
// same way as it is entered, and the monsters in its rooms are read from the server's side and handed to mapmarks,
// which marks the named ones with the name the client would give them; an area filled before is read again each time
// it is entered.
namespace d2rcc::roomfill {

using game::Unit;

// After sites::derive() and game::bind(): whether the game's fill pass, a room's state and units read as expected.
void bind();
bool ready();
// Any thread: the character may stand in an area not filled yet (the loader's area and game events, a switch).
void arm();
// The server thread (the tick), under Landmarks on the map or Named enemies on the map.
void on_tick(void* game, Unit* player, uint32_t tick);
void game_left();

struct Stats {
  unsigned areas = 0;    // areas whose rooms were filled ahead this game
  unsigned rooms = 0;    // ... rooms built for the game to fill
  unsigned objects = 0;  // ... objects with an icon handed to the map
  unsigned monsters = 0; // ... named monsters handed to the map (the areas' first reads)
};
Stats stats();

}  // namespace d2rcc::roomfill
