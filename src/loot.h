#pragma once

#include <D2RLPlugin/api.h>

#include <cstdint>

namespace d2rcc::game {
struct Unit;
}

// Loot: what monsters and objects (chests, barrels, urns, bodies, racks) drop, and the items the game
// makes. Everything but the counters runs on the game's server thread, called from the hooks in
// hooks_game.cpp:
//   the treasure-class drop core    only what the loot filter shows drops, every pick drops, extra drops (for
//                                    every kill and chest, or only a champion's, unique's, boss's and Herald's)
//   item creation                    ... by picks among what the filter shows of what the treasure class drops:
//                                    an item the filter hides is not made, one it shows in some forms is made in one
//   the item generator's quality     new items superior / ethereal / socketed wherever the game allows it
//   the property roll                perfect rolls: every value at the top of its range (not the picks)
//   the class item skill step        perfect rolls: a class item's skill lines, as many as can be, each at its top
//   the superior step's test         perfect rolls: a superior item's kind, the best of those that go on the item
//   the magic, rare, crafted steps   max affixes: after the game's own pass, the item gets as many affixes as its
//                                    quality can have, from the game's own picker by the step's own rules
//   the affix picker                 best affixes: each affix the game picks for a fresh item becomes the best version
//                                    of that affix the item could have had (the same stats, the biggest numbers)
//   the automatic affix picker       ... and so does a base's own automatic affix (a paladin shield's all res)
// All of it happens in the generator, on the stream of the item's own seed, which no save keeps. What the seed a
// save does keep rolls (an armor's base defense) is not touched here: itemseed.cpp has the item made from a seed
// that rolls it.
namespace d2rcc::loot {

using game::Unit;

void set_services(const D2RL::PluginContext* ctx, const D2RL::DataTableService* tables);
void game_left();  // the counters start again

// A loot filter panel (the game's editor: "LootFilter...") opened or closed (UI thread). While any
// is open the rules may be changing under the server thread, so nothing is filtered until they close.
void note_filter_panel(const char* name, bool open);

// The treasure-class drop core (game, source, killer, tc, forced quality, item level, skip NoDrop,
// items out, count out, most items). `original` is the game's own routine.
using TcDropCoreFn = void(__fastcall*)(void* game, Unit* source, Unit* killer, const void* tc, int32_t forced_quality,
                                      int32_t item_level, int32_t skip_nodrop, Unit** out, int32_t* out_count,
                                      int32_t out_max) noexcept;
void drop(TcDropCoreFn original, void* game, Unit* source, Unit* killer, const void* tc, int32_t forced_quality,
          int32_t item_level, int32_t skip_nodrop, Unit** out, int32_t* out_count, int32_t out_max);

// The loot filter asked before an item is made (the item creation hook asks, for every item). Inside a drop
// session under the loot filter switch the generator's request is judged by the forms the item can take (its
// quality, what the generator makes of that, ethereal, sockets: dropodds.h): when the filter shows them all the
// item is made as it comes; when it shows some, by their chance one of those is made (the request is told which:
// the row asked for by name, its flags) or none; when it shows none, none. `refuse` answers true for an item that
// is not to be made, and the hook hands the drop core no item, as the game's own item maker does when it finds no
// room. `made` takes every item a request did make and gives the request its own back: the game's own test of
// the real item checks the answers, and so does one drop in 32 (later in 1024) that would have been refused and
// is made all the same; three answers that do not hold turn the question off for the session.
bool refuse(uint8_t* request);
void made(uint8_t* request, Unit* item);

// The console's dropcheck: at the next monster's drop its treasure class is walked so many times by the drop core
// itself, with nothing made, and what it asked for is set against what the odds say (the log has the outcome).
void check_drops(int walks);

// The item generator's quality step returned `quality` for a new item; the quality to use instead.
// The request is the generator's: +0x3C restore, +0x40 the quality asked for, +0xC0 flags.
int32_t quality_step(Unit* item, uint8_t* request, int32_t quality);

// Perfect rolls. The call sites of the property roll that pick an id rather than a value (their
// return addresses, at most kMaxChoiceSites), and the game's code range: rolls from anywhere else in it are values.
constexpr int kMaxChoiceSites = 16;
void set_choice_sites(const uintptr_t* return_addresses, int count, uintptr_t text_begin, uintptr_t text_end);
bool choice_sites_known();
// The game rolled `rolled` in [a, b] for `unit`, called from `return_address`: the value to use.
int32_t property_roll(Unit* unit, int32_t a, int32_t b, int32_t rolled, uintptr_t return_address);
// The bonus to give the class item skill step for `item` (the game's own, `bonus`, is the item level for some
// requests and 0 for the rest): it adds to the step's roll for how many skill lines, and half of it to each line's
// roll for +1, +2 or +3.
int32_t class_skill_bonus(Unit* item, int32_t bonus);
// A superior item's kind (superior.h). The game's test whether a kind (a QualityItems row) goes on an item, which
// the superior step asks of each kind its roll comes to until one does; `original` is the game's. For a fresh item
// the first question has the original asked about every kind the step picks among, and from then on the answer is no
// for every kind but the best of those that go on the item. Any other item gets the game's answers. `made` checks
// what came of it: an item the step was told no for must be superior and of the kind it was told to take; the third
// that is not turns it off for the session.
using SuperiorFitsFn = int32_t(__fastcall*)(Unit* item, const uint8_t* row) noexcept;
int32_t superior_fits(SuperiorFitsFn original, Unit* item, const uint8_t* row);

// Max affixes: the item generator's affix steps. `original` is the game's step; it runs first, as it would, and an
// item it made short of the most its quality can have gets more affixes (magic: a prefix and a suffix; rare: six, a
// jewel four; crafted: four random ones), picked one at a time by the game's picker as the step itself would: a
// coin for the side, at most three a side, a side with nothing left to pick closed. The request's forced ids
// (+0xA8 three prefixes, +0xB4 three suffixes; -1 forbids a magic item's side) are passed as the step passes them.
using MagicAffixesFn = int32_t(__fastcall*)(Unit** item, uint8_t* request) noexcept;
using AffixesFn = int32_t(__fastcall*)(Unit* item, uint8_t* request) noexcept;
int32_t magic_affixes(MagicAffixesFn original, Unit** item, uint8_t* request);
int32_t rare_affixes(AffixesFn original, Unit* item, uint8_t* request);
int32_t crafted_affixes(AffixesFn original, Unit* item, uint8_t* request);

// Best affixes: the affix picker (item, spawnable rows only, must pick, apply the affix's properties, prefix, forced
// id) -> the affix id or 0. `original` is the game's. Inside an affix step making a fresh item, a pick the request
// did not force is the game's own pick, then replaced by the best row of its group that gives the same stats and
// that the game could have picked for the item (its item types, its affix level, rare-only rows on rare items); the
// properties applied are the kept row's. Any other pick is the game's, unchanged.
using PickAffixFn = int32_t(__fastcall*)(Unit* item, int32_t spawnable, int32_t must, int32_t apply, int32_t prefix,
                                         int32_t forced) noexcept;
int32_t pick_affix(PickAffixFn original, Unit* item, int32_t spawnable, int32_t must, int32_t apply, int32_t prefix,
                   int32_t forced);
// ... and the automatic affix, a base's own (a paladin shield's all res or attack rating, an orb's life or mana): the
// picker's twin with the base's AutoMagic group (its Items row's), which the generator calls last and whose row it
// writes and applies itself. For the fresh item whose quality step ran last on the thread, the game's own pick, then
// the best row of the same group that gives the same stats and that the game could have picked for the item.
using AutoAffixFn = int32_t(__fastcall*)(Unit* item, int32_t spawnable, int32_t must, int32_t apply, int32_t prefix,
                                         int32_t forced, int32_t group) noexcept;
int32_t pick_auto_affix(AutoAffixFn original, Unit* item, int32_t spawnable, int32_t must, int32_t apply,
                        int32_t prefix, int32_t forced, int32_t group);

struct Stats {
  unsigned replaced = 0;  // drops the loot filter hides, replaced by ones it shows
  unsigned omitted = 0;   // picks left empty: nothing the filter shows turned up
  unsigned extra = 0;     // extra drops added
  unsigned slowest_us = 0;  // the longest a drop's search for what the filter shows took, in microseconds
  unsigned unmade = 0;      // drops not made at all: the filter hides whatever they could have become
  bool ask_first_off = false;  // the filter's answers did not hold: every drop is made and then shown to it
  unsigned picked = 0;      // drops picked among what the filter shows of what the treasure class drops
  unsigned missed = 0;      // items made in a form that was worked out, and that came out otherwise
  bool odds_off = false;    // too many of them: drops are rolled for until the filter shows one
  unsigned maxed = 0;     // values rolled at the top of their range
  unsigned skill_items = 0;  // class items whose skill lines were rolled at their top
  unsigned kinds = 0;        // superior items given the best kind, where the game's roll came to another first
  bool kinds_off = false;    // ... items came out otherwise: a superior item's kind is the game's pick again
  unsigned affixes = 0;      // affixes added to new magic, rare and crafted items
  unsigned upgraded = 0;     // affixes the game picked, replaced by a better version of the same affix
};
Stats stats();  // any thread

}  // namespace d2rcc::loot
