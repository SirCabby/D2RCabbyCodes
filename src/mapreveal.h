#pragma once

#include <windows.h>

// Map reveal: the whole map of the area the player is in, on the automap, and of every area entered
// after it. Nothing is hooked. On the game's UI thread (the loader's UI callback: the thread the client
// builds its rooms and its automap on), the level the local player stands in is read from the client's
// own copy of the map - when something says the player may stand in an area not revealed yet (the
// loader's events: an area or act entered, a game joined; the switch), and once a second besides. Once the
// automap has moved to that level's layer, every room of the level is built for play if it is not (the
// game's CreateActiveRoom) and handed to the client DRLG's automap callback, which puts all of the room on
// the automap: the game's own way of revealing a room whole. A large area takes a few frames, and until
// it is done the work goes on every frame. Each level is revealed once per act visit.
namespace d2rcc::mapreveal {

void bind(bool ui_thread);  // after game::bind(): what the game offers; sets the switch's why_not
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
