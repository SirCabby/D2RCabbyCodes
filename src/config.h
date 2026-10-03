#pragma once

#include <cstdint>
#include <string>

#include "log.h"

// The settings, kept in the TOML file D2RLoader owns for this plugin
// (<scope>/d2rloader/config/cabbycodes.toml; res/cabbycodes.toml is the
// embedded default it is created from). The panel edits them in the game and
// writes them back through the loader; a user can also edit the file. Reading
// and writing are text-in, text-out here so the loader's API stays in plugin.cpp.
namespace d2rcc::config {

enum class ExitMethod : int { kSaveAndExit = 0, kForceClose = 1 };

struct Settings {
  bool enabled = true;
  bool god_mode = false;
  bool infinite_mana = false;
  bool infinite_stamina = false;
  float exp_multiplier = 1.0f;      // 1.0 .. 1000.0
  int move_speed_bonus = 0;         // percent, 0 .. 300
  bool exit_before_death = false;
  int exit_below_life_percent = 0;  // 0: only a lethal hit; 1..99: also below this
  ExitMethod exit_method = ExitMethod::kSaveAndExit;
  bool terror_all_areas = false;     // every act terrorized in every game, as if each act's shard had been used
  bool area_level_automap = false;   // the area's level after its name on the automap
  bool area_level_waypoints = false; // ... and in the waypoint list
  bool reveal_map = false;           // the whole map of each area you are in, on the automap
  bool map_named_enemies = false;    // named enemies on the automap, and the spawn spots of super uniques and bosses
  bool map_landmarks = false;        // the landmarks the automap shows once you are near (waypoints, shrines...), at once
  bool map_exit_names = false;       // the name of the area each exit leads to, on the automap
  bool item_level = false;           // an item's level after its name (weapons, armor, jewelry, charms, jewels)
  bool monster_health_bars = false;  // a short life bar above every enemy on screen
  bool boss_health_bar = false;      // an act boss's life at the top centre while it is near
  int boss_bar_position = 0;         // the boss bar's top, percent of the screen height (0 .. 50)
  bool loot_filter_only = false;     // drops come only from what the loot filter shows; every pick drops
  bool chronicle_only = false;       // ... only the unique and set items the Chronicle misses (with the one above:
                                     // what either lets through)
  int extra_drops = 0;               // items added to every kill's or chest's drop (0 .. 20)
  bool extra_drops_elites_only = false;  // ... only to a champion's, a unique's, a boss's and a Herald's
  bool perfect_rolls = false;        // every stat value of a new item at the top of its range
  bool all_superior = false;         // new normal or inferior items become superior where they can
  bool all_ethereal = false;         // new items become ethereal where they can
  bool all_socketed = false;         // new normal or superior items get sockets where they can (a random number)
  bool max_affixes = false;          // new magic, rare and crafted items get as many affixes as their quality allows
  bool best_affixes = false;         // ... and the best version of each affix they get, at their item level
  float damage_multiplier = 1.0f;    // your hits and your minions' are multiplied, 1.0 .. 100.0
  bool invincible_mercenary = false; // the mercenary takes no damage
  bool invincible_minions = false;   // summons take no damage
  bool passive_mercenary = false;    // the mercenary fights nothing (it follows you)
  bool passive_minions = false;      // ... nor do your summons
  bool permanent_revives = false;    // your revives stay until they die (their time never runs out)
  bool cannot_be_frozen = false;     // hits never freeze or chill you
  bool cannot_be_poisoned = false;   // hits never poison you, and a poison already on you ends
  bool cannot_be_cursed = false;     // a monster's curse is never put on you, and a curse already on you ends
  bool infinite_town_portal = false; // town portal scrolls are not used up, tomes keep their charges
  bool infinite_identify = false;    // ... identify scrolls and tomes
  bool auto_identify = false;        // an item picked up is identified at once (a unique or set item into the
                                     // Chronicle, as identifying it puts it there)
  bool infinite_potions = false;     // potions are not used up
  bool infinite_keys = false;        // a key opens a locked chest or door without being used up
  bool no_durability_loss = false;   // your weapon and armor never lose durability
  bool infinite_gold = false;        // a vendor takes no gold (you still need to hold the price)
  bool infinite_imbue = false;       // Charsi imbues whenever asked, her quest done or not
  bool infinite_sockets = false;     // ... Larzuk adds sockets
  bool infinite_personalize = false; // ... Anya personalizes
  bool infinite_cube_ingredients = false;  // a cube recipe uses nothing up: the ingredients come back to the inventory
  bool ignore_requirements = false;  // your character can use any item its class can (no level, strength, dexterity)
  bool ignore_requirements_mercenary = false;  // ... your mercenary any item it can
  int home_town = 0;                 // the town your town portals lead to and you wake in after a death: 0 the
                                     // game's own (the town of the act you are in), 1 .. 5 that act's
  // Kept between games: when a character is loaded, the automap and the two Show Items keys are the way they
  // were when a game was last left (the Show Items ones only in the game's Toggle mode for that key).
  bool remember_automap = false;
  bool remember_show_items = false;
  bool remember_show_items_unfiltered = false;
  bool automap_was_open = false;             // what they remember (the plugin writes these)
  bool show_items_was_on = false;
  bool show_items_unfiltered_was_on = false;
  bool show_on_pause = true;
  int toggle_key = 0x76;  // VK_F7
  bool logging = false;  // the plugin's log is written (off, as installed: nothing but what is asked for by name)
  LogLevel log_level = LogLevel::kInfo;  // ... from this kind of line on: information, warnings or errors
  bool trace = false;
};

// Parses the [cabbycodes] table of a TOML text. Unknown keys are ignored, bad
// values keep their defaults; the result says how many keys were understood.
int parse(const std::string& toml, Settings* out);

// Rewrites the values of the [cabbycodes] table in place - comments and order
// survive - and appends any key the file lacks. Returns the new text.
std::string rewrite(const std::string& toml, const Settings& s);

// Clamps every field to its documented range.
void clamp(Settings* s);

// A virtual-key name ("F7", "INSERT", "HOME", ...) or a number ("0x76", "118"), 0 when unknown.
int key_from_name(const char* name);
const char* key_name(int vk);  // the canonical name for the file, or a hex string

const char* exit_method_name(ExitMethod m);
const char* log_level_name(LogLevel level);  // "info", "warning", "error": as the file has it
bool log_level_from_name(const char* name, LogLevel* out);

}  // namespace d2rcc::config
