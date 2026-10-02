#pragma once

#include <D2RLPlugin/api.h>

#include <cstdint>

namespace d2rcc::game {
struct Unit;
}

// The seed a save keeps with an item. The game draws two seeds for a new item, two outputs in a row of its random
// stream:
//   the unit's seed      rolls what item init rolls: the durability, an armor's base defense, a stack's quantity,
//                        the picture. It is written to the save with the item, and put back at a load.
//   the item's own seed  rolls what the generator does: affixes, values, class skills, ethereal, sockets. No save
//                        keeps it; it is drawn again when the item is loaded, so nothing is tied to it later.
// So the seed that stays with an item says one thing a loot switch cares about: an armor's base defense. Under
// perfect rolls a new armor is made from a unit seed that rolls the top of its base's range, handed to the game's
// own item creation (its "use the request's seeds" argument): the game's item init rolls the durability, the
// defense and the picture from it, nothing is written to the item afterwards, and the seed in the save rolls that
// defense again for whoever checks. Everything else the switches do is done in the generator (loot.cpp), on the
// stream of the seed no save keeps.
// The seed is found by trying random ones inside the item's creation: one in (maxac - minac + 1) fits, a few
// dozen multiplications. Nothing is looked for on other threads, waited for or kept in a file.
namespace d2rcc::itemseed {

using game::Unit;

// --- the arithmetic (no game calls; tests/test_itemseed.cpp) ---

// The game's random stream: multiply with carry on {lo, hi}, the carry seeded 666.
constexpr uint32_t kMultiplier = 0x6AC690C5u;
struct Rng {
  uint32_t lo = 0;
  uint32_t hi = 666;
  constexpr explicit Rng(uint32_t seed = 0) : lo(seed) {}
  constexpr uint32_t step() {
    const uint64_t t = static_cast<uint64_t>(lo) * kMultiplier + hi;
    lo = static_cast<uint32_t>(t);
    hi = static_cast<uint32_t>(t >> 32);
    return lo;
  }
  // The game's roll (stream, n): 0 without a step when n is not above 0, else a step, masked when n is a power of
  // two.
  constexpr uint32_t roll(int32_t n) {
    if (n <= 0) return 0;
    const uint32_t v = step();
    const uint32_t un = static_cast<uint32_t>(n);
    return (un & (un - 1)) == 0 ? v & (un - 1) : v % un;
  }
};

// An armor's base (its Items row): what item init rolls from the unit's seed, in its order - the durability (half
// the base's, plus a roll of that half), then the base defense (minac plus a roll of maxac - minac + 1).
struct Armor {
  int32_t durability = 0;  // Items +0x121
  int32_t min_ac = 0;      // Items +0xD4
  int32_t max_ac = 0;      // Items +0xD8
  constexpr bool ranged() const { return max_ac > min_ac; }
};

// Item init rolls a base defense for an item whose type is the armor type or counts as it through its ItemTypes
// row's Equiv1 / Equiv2 chains, the way the game's own item-type test reads the table (a circlet counts as a helm,
// a helm as armor). `equivs(type, &equiv1, &equiv2)` reads a row's two; false when it cannot.
constexpr uint32_t kTypeArmor = 0x32;  // the type item init asks for (`mov edx, 32h`)
constexpr int kMaxEquivDepth = 10;
template <typename Equivs>
bool counts_as_armor(uint32_t type, const Equivs& equivs, int depth = 0) {
  if (type == kTypeArmor) return true;
  if (type == 0 || depth > kMaxEquivDepth) return false;
  uint16_t e1 = 0, e2 = 0;
  if (!equivs(type, &e1, &e2)) return false;
  return counts_as_armor(e1, equivs, depth + 1) || counts_as_armor(e2, equivs, depth + 1);
}

// The base defense item init rolls for the armor from `unit_seed`.
constexpr int32_t defense(const Armor& base, uint32_t unit_seed) {
  Rng rng(unit_seed);
  rng.roll(base.durability >> 1);
  return base.min_ac + static_cast<int32_t>(rng.roll(base.max_ac - base.min_ac + 1));
}

// A unit seed that rolls the armor's top base defense: `start`, then the seeds an odd stride apart, `tries` of
// them at most (`tried`: how many were looked at). False when none of them does.
constexpr bool top_defense_seed(const Armor& base, uint32_t start, uint32_t tries, uint32_t* seed,
                                uint32_t* tried = nullptr) {
  uint32_t s = start;
  for (uint32_t i = 0; i < tries; ++i, s += 0x9E3779B1u) {
    if (defense(base, s) != base.max_ac) continue;
    *seed = s;
    if (tried) *tried = i + 1;
    return true;
  }
  if (tried) *tried = tries;
  return false;
}

// The two seeds come in a row: the item's own is the unit's times the multiplier plus the stream's carry, and the
// carry is below the multiplier.
constexpr bool in_a_row(uint32_t unit_seed, uint32_t item_seed) {
  return static_cast<uint32_t>(item_seed - unit_seed * kMultiplier) < kMultiplier;
}
constexpr uint32_t item_seed_after(uint32_t unit_seed, uint32_t carry) {
  return unit_seed * kMultiplier + carry % kMultiplier;
}

// The base defense an armor made from a top-rolling unit seed ends up with, by what the generator made of it: a
// low quality item keeps three quarters (at least 1), enhanced defense among its properties makes the base the top
// plus one (the game's rule for armor), an ethereal item has half as much again.
constexpr int32_t kQualityLow = 1;
constexpr bool top_defense_kept(const Armor& base, int32_t quality, bool ethereal, int32_t has) {
  if (quality == kQualityLow) {
    const int32_t low = base.max_ac * 75 / 100;
    return has == (low < 1 ? 1 : low);
  }
  const int32_t top = ethereal ? base.max_ac * 3 / 2 : base.max_ac;
  const int32_t enhanced = ethereal ? (base.max_ac + 1) * 3 / 2 : base.max_ac + 1;
  return has == top || has == enhanced;
}

// --- the game's side (server thread) ---

void set_services(const D2RL::PluginContext* ctx, const D2RL::DataTableService* tables);
bool has_tables();
void game_left();  // the counters start again

// Item creation (game, request, use the request's seeds) -> item. `original` is the game's. A fresh armor with a
// range of base defense, under perfect rolls, is made from a chosen unit seed and an item seed that comes right
// after it; every other request goes to the game as it came.
using CreateItemFn = Unit*(__fastcall*)(void* game, uint8_t* request, int32_t use_seeds) noexcept;
Unit* create(CreateItemFn original, void* game, uint8_t* request, int32_t use_seeds);

struct Stats {
  unsigned chosen = 0;      // armor made from a chosen unit seed
  unsigned differed = 0;    // ... whose base defense came out otherwise than the seed rolls it
  unsigned longest_us = 0;  // the longest a seed took to find, in microseconds
  bool off = false;         // the choice is off for the session: the items came out otherwise
};
Stats stats();  // any thread

}  // namespace d2rcc::itemseed
