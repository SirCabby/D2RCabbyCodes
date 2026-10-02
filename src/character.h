#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "presets.h"

namespace d2rcc::game {
struct Unit;
}

// The character's points: a respec (the Token of Absolution's own reset: every skill and attribute
// point spent comes back) and named presets of what the points were spent on. The panel asks, the
// stat-regeneration tick does it on the game's server thread with the game's own routines, and
// the watchdog thread writes the presets file.
namespace d2rcc::character {

void set_config_dir(const char* utf8_dir);  // the folder of the settings file (trailing separator)
void load();                                // plugin load: read the presets file
void save_if_changed(bool now = false);     // watchdog thread (now: ignore a failed write's back-off)

// Any thread (the panel, the console). A save is taken by the next UI frame, from the client's copy
// of the character (the UI keeps running while the Esc menu pauses a single-player game). A reset or
// a load changes the character, so it waits for the game's own tick: it happens once the game runs.
void request_respec();
void request_save(const char* name);  // the character's points now, under this name (same name and class: replaced)
bool request_load(const char* name, int char_class);  // reset, then spend the points the preset says
void cancel_reset();                  // a waiting respec or load is dropped
bool remove(const char* name, int char_class);
std::vector<presets::Preset> list();  // a copy, every class
struct Pending {
  bool save = false;
  bool respec = false;
  std::string load;  // the preset a load waits with, "" for none
};
Pending pending();
std::string last_result();  // what the last respec, save or load did (for the panel)

// The UI thread (the loader's UI callback, once a frame): a waiting save, read from the client's copy.
// A read of a flag while nothing waits, as is the tick's.
void on_ui();

// The tick, on the server thread (a save too, should the UI callback not be running). True when
// the character was reset (a respec or a preset load): its life may have dropped with its vitality,
// which is not a hit.
bool on_tick(void* game, game::Unit* player);
void game_left();  // pending requests are dropped

}  // namespace d2rcc::character
