#pragma once

#include <windows.h>

#include <cstdint>

#include "config.h"

// Kept between games: the automap, Show Items and Show Items (Unfiltered) the way they were when a game was
// last left, with any character. Nothing is hooked. On the game's UI thread (the loader's UI callback, where
// the game runs its key bindings) the three are read from the game's own state while a game runs - when a
// key, a panel or a switch of ours says they may have changed (the game's UI messages, through the loader),
// and once a second besides - and what they were goes into the settings file. When a character is loaded,
// once it stands in the game, each that differs is changed the way its key changes it: the key-action table's
// press function, which is D2RCore's wrapper for the Automap and Show Items keys. D2RCore keeps its own memory
// of those two for the session (the first game of a session it closes the map, its memory being empty); a
// press through its wrapper keeps that memory in step, and until the loading screen is gone the plugin keeps
// putting them back (every frame, for those seconds).
// A Show Items key is kept only in the game's Toggle mode for it (Item Name Display, Unfiltered Item Name
// Display): in Hold mode a press would hold it on, in Timed mode it goes off by itself.
namespace d2rcc::remember {

enum Which : int { kAutomap = 0, kShowItems, kShowItemsUnfiltered, kCount };

void init(const config::Settings& s);   // what was remembered, from the settings file
void to_settings(config::Settings* s);  // ... and back, for writing it
void bind(bool ui_thread);              // after game::bind(): what the game offers; sets the switches' why_not
void on_ui(ULONGLONG now);              // UI thread, every frame: a flag read until there is something to look at
void look();                            // any thread: a key, a panel, a switch - the three may be another way now
void game_joined(uint64_t session);     // any thread: a game began (its first call per session counts)
void loading_screen_gone();             // any thread: the game's loading screen went away
void leaving();                         // any thread: the game is being left (Save and Exit, the front end)
void game_left();                       // any thread
bool save_waits();                      // the watchdog: a change waits to have held long enough
bool save_due(ULONGLONG now);           // the watchdog: a change has held long enough (or the game ended): write

bool remembered(Which w);   // on, or open
int display_mode(Which w);  // a Show Items key's display mode (game::ItemNameDisplay), -1 unknown or the automap

}  // namespace d2rcc::remember
