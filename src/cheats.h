#pragma once

#include <cstddef>
#include <cstdint>

#include "config.h"

// The cheats. Their switches are shared between the panel (drawn on the present
// thread) and the game's server thread, which calls the mod from the hooks in
// hooks_game.cpp - a hit, a stat-regeneration tick, a death - so the switches
// are plain atomics. Nothing touches game memory outside those hooks.
namespace d2rcc::cheats {

enum Kind : int {
  kGodMode = 0,
  kInfiniteMana,
  kInfiniteStamina,
  kExitBeforeDeath,
  kExpMultiplier,  // "enabled" when the multiplier is above 1
  kMoveSpeed,      // "enabled" when the bonus is above 0
  kTerrorAll,      // every act terrorized in every game, as if each act's Worldstone Shard had been used
  kAreaLevelAutomap,    // the area's level after its name in the automap's area text
  kAreaLevelWaypoints,  // ... and in the waypoint list
  kMonsterBars,         // a short life bar above every enemy on screen
  kBossBar,             // an act boss's name and life at the top centre while it is near
  kLootFilterOnly,      // drops come only from what the loot filter shows, and every pick drops
  kChronicleOnly,       // ... only the unique and set items the Chronicle misses (with the one above: either will do)
  kExtraDrops,          // "enabled" when the extra drops per kill or chest are above 0
  kExtraDropsElite,     // ... only for the kills that tend to drop better: champions, uniques, bosses, Heralds
  kPerfectRolls,        // every stat value of a new item at the top of its range
  kAllSuperior,         // every new normal or inferior item that can be superior is
  kAllEthereal,         // every new item that can be ethereal is
  kAllSocketed,         // every new normal or superior item that can have sockets has them (a random number)
  kMaxAffixes,          // every new magic, rare and crafted item has as many affixes as its quality can have
  kBestAffixes,         // ... and each affix it gets is the best version of that affix the item could have
  kDamageMultiplier,    // "enabled" when the multiplier is above 1: your hits, and your minions'
  kInvincibleMerc,      // the mercenary takes nothing from any hit
  kInvinciblePets,      // ... nor do your summons
  kPassiveMerc,         // the mercenary fights nothing: its AI finds no enemy, and it follows you
  kPassivePets,         // ... nor do your summons
  kPermanentRevives,    // your revives stay until they die: the end of their time is never run
  kNoImmunities,        // an enemy's immunity counts as no resistance: every element hurts it
  kNoAffixes,           // monsters made from now on get none of the affixes the game rolls for uniques
  kCannotBeFrozen,      // hits never freeze or chill you
  kCannotBePoisoned,    // hits never poison you, and a poison already on you ends
  kCannotBeCursed,      // a monster's curse is never put on you, and a curse already on you ends
  kInfiniteTownPortal,  // town portal scrolls stay, tomes keep their charges
  kInfiniteIdentify,    // ... identify scrolls and tomes too
  kAutoIdentify,        // an item picked up is identified at once (a unique or set item goes into the Chronicle)
  kInfinitePotions,     // a potion drunk stays where it was
  kInfiniteKeys,        // a key opens a locked chest or door and stays
  kNoDurabilityLoss,    // your weapon and armor never lose durability
  kInfiniteGold,        // a vendor takes no gold: buying, gambling, repairs, identifying, the mercenary
  kInfiniteImbue,       // Charsi imbues at every talk, her quest done or not, as often as asked
  kInfiniteSockets,     // ... Larzuk adds sockets
  kInfinitePersonalize, // ... Anya personalizes
  kInfiniteCubeIngredients,  // a Horadric Cube recipe uses nothing up: its ingredients come back to the inventory
  kIgnoreRequirements,  // your character uses any item its class can: an item's level, strength, dexterity not asked
  kIgnoreRequirementsMerc,  // ... and your mercenary any item it can
  kRevealMap,           // the whole map of the area you are in, and of each one you enter, on the automap
  kMapEnemies,          // named enemies on the automap (a red cross and the name), and their spawn spots
  kMapLandmarks,        // the landmarks the automap shows once you are near (waypoints, shrines...), at once
  kMapExits,            // the name of the area each exit leads to, on the automap (cave entrances, stairs, borders)
  kItemLevel,           // an item's level after its name (weapons, armor, rings, amulets, charms, jewels)
  kRememberAutomap,     // a loaded character gets the automap open or closed as it was when a game was last left
  kRememberShowItems,   // ... Show Items on or off (only in the game's Toggle mode for it)
  kRememberShowItemsUnfiltered,  // ... and Show Items (Unfiltered)
  kHomeTown,            // "enabled" when a town is chosen: your town portals lead to that act's town, and you
                        // wake there after a death
  kLogging,             // the plugin's log is written (plugin.cpp tells the log when the switch changes)
                        // (from which kind of line on is a number of its own, log_level)
  kRespec,              // an action, not a switch: only its why_not is used
  kPresets,             // ... loading a skill and attribute preset (saving needs only the tick)
  kCount
};

void init(const config::Settings& s);  // the start-up switches
const char* name(Kind k);
bool enabled(Kind k);
void set_enabled(Kind k, bool on);  // any thread

// The numbers: read and written from any thread.
float exp_multiplier();
void set_exp_multiplier(float m);
int move_speed_bonus();  // percent
void set_move_speed_bonus(int pct);
int exit_below_percent();  // 0: only on a lethal hit
void set_exit_below_percent(int pct);
config::ExitMethod exit_method();
void set_exit_method(config::ExitMethod m);
LogLevel log_level();  // the least severe kind of line the log takes (plugin.cpp tells the log)
void set_log_level(LogLevel least);
int boss_bar_position();  // the boss bar's top, in percent of the screen height
void set_boss_bar_position(int pct);
int extra_drops();  // items added to every kill's or chest's drop (0 .. 20)
void set_extra_drops(int n);
float damage_multiplier();  // 1 .. 100
void set_damage_multiplier(float m);
int home_town();  // 0: the town of the act you are in (the game's own); 1 .. 5: that act's town
void set_home_town(int act);

// Why a cheat cannot work right now (a routine not found, a hook refused), or
// null when it can. Set by hooks_game.cpp, shown by the panel.
const char* why_not(Kind k);
void set_why_not(Kind k, const char* reason);

// What the panel shows (built on the server thread, handed over under a lock).
struct Status {
  bool player_found = false;
  uint32_t player_id = 0;
  int hp = 0, max_hp = 0;      // in points (the game keeps 256ths)
  int mana = 0, max_mana = 0;
  int level = 0;
  int64_t experience = 0;
  int velocity = 0;            // the velocitypercent stat as the game sees it
  unsigned ticks = 0;          // stat-regeneration ticks seen for the player
  unsigned hits_absorbed = 0;  // god mode: hits on you taken away
  unsigned lethal_hits = 0;    // exit before death: hits held at 1 life
  unsigned exp_awards = 0;      // experience gains scaled
  unsigned exp_awards_merc = 0;  // ... of them the mercenary's own
  unsigned exp_full = 0;        // kills counted at their full worth (you and your mercenary)
  unsigned hits_scaled = 0;    // damage multiplier: hits by you or your minions scaled
  unsigned pet_hits = 0;       // invincible mercenary / minions: hits taken away
  unsigned effects_blocked = 0;  // cannot be frozen / poisoned: hits that would have
  unsigned poisons_ended = 0;    // cannot be poisoned: poisons already on you that the tick ended
  unsigned wear_skipped = 0;     // no durability loss: wear rolls on your gear not made
  unsigned payments_free = 0;    // infinite gold: payments a vendor did not take
  int64_t gold_kept = 0;         // ... and the gold they came to
  bool exit_armed = false;     // exit before death: life has been above the trigger since the switch went on
  int char_class = -1;         // the character's class (0 Amazon .. 7 Warlock)
  int stat_points = 0;         // attribute points to spend
  int skill_points = 0;        // skill points to spend
  char last_action[160] = {};
};
Status status();
void publish(const Status& st);
void note(const char* fmt, ...) __attribute__((format(gnu_printf, 1, 2)));

// The switches as settings, for writing the file.
void to_settings(config::Settings* s);

}  // namespace d2rcc::cheats
