#pragma once

#include <windows.h>

// The area pass: the map of the area the player is in, and of every area entered after it, on the client's side. Four
// switches ride on it: Reveal the map, Landmarks on the map, Named enemies on the map and Area names at exits
// (mapmarks.h). Nothing is hooked. On the game's UI thread (the loader's UI callback: the thread the client builds its
// rooms and its automap on), the level the local player stands in is read from the client's own copy of the map - when
// something says the player may stand in an area not revealed yet (the loader's events: an area or act entered, a game
// joined; a switch), and once a second besides. Once the automap has moved to that level's layer, every room of the
// level is built for play if it is not (the game's CreateActiveRoom, which also makes a room's preset units and links
// it to the areas next to it) and, as the switches ask, handed to the client DRLG's automap callback, which puts all of
// the room on the automap (the game's own way of revealing a room whole), and to mapmarks for the landmarks, the named
// enemies' spawn spots its layout places and its exits. A large area takes a few frames, and until it is done the work
// goes on every frame. Each level is gone over once per act visit for each switch.
namespace d2rcc::mapreveal {

void bind(bool ui_thread);  // after game::bind(): what the game offers; sets the reveal switch's why_not
void on_ui(ULONGLONG now);  // UI thread, every frame: a read of a flag until there is an area to look at
// Any thread: the player may stand in an area not revealed yet - in this level, when the loader said which
// (else -1).
void arm(int level);
void game_left();           // any thread: the next frame forgets the game

struct Stats {
  unsigned areas = 0;  // revealed this game
  unsigned rooms = 0;  // ... and their rooms
};
Stats stats();

}  // namespace d2rcc::mapreveal
