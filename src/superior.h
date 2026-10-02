#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Perfect rolls: a superior item's kind.
//
// A superior item gets one row of QualityItems, its kind: one property or two. 3.3 has eight kinds: attack rating,
// enhanced damage, enhanced defense, attack rating with enhanced damage, durability, and durability with each of
// the first three. The game's superior step (0x58B9E0) picks it: a roll on the item's own seed among the kinds,
// then the game's test whether the kind it came to goes on the item (0x3D40D0: a row has a byte per sort of item -
// armor, weapon, shield, scepter, wand, staff, bow, boots, gloves, belt), and the first kind that does is the
// item's. A base that is thrown or has no durability picks among the first four, the ones without durability. So
// an armor comes with durability alone one time in three, and half the weapons without enhanced damage. Perfect
// rolls put every value at its top; which kind is a pick, not a value.
//
// Under perfect rolls the test's hook (hooks_game.cpp, loot.cpp) says no to every kind but the best of those that go
// on the item, so the step's own loop comes to that one and makes it as it makes any. Nothing is written, and the
// item is one the game makes itself whenever its roll comes to that kind first.
//
// The best kind (Joshua, 2026-09-29): an armor's is enhanced defense with durability, the one kind that has both; a
// weapon's is enhanced damage with attack rating (no kind has all three of a weapon's bonuses). As a rule over
// whatever the table holds: a bonus counts by what it is (enhanced damage or defense 4, attack rating 2, anything
// else 1), times its top against the most any kind that goes on the item gives of it; the kind with the highest sum
// is the best, the bottoms of the ranges decide between equal sums, and between kinds equal in both the step's
// roll does.
namespace d2rcc::superior {

// The compiled QualityItems row: a byte per sort of item it goes on, then two properties.
constexpr uint32_t kRowSize = 0x2C;
constexpr size_t kRowMods = 0x0C;  // two {int32 property, param, min, max}; a property below 0 unused
constexpr size_t kRowModSize = 0x10;
constexpr int kRowModCount = 2;
// The compiled Properties row: seven {function, stat} slots (a function of 0 unused, a stat of 0xFFFF none).
constexpr uint32_t kPropertyRowSize = 0x30;
constexpr size_t kPropertyFunc = 0x18;  // u8[7]
constexpr size_t kPropertyStat = 0x20;  // u16[7]
constexpr int kPropertySlots = 7;
constexpr uint16_t kNoStat = 0xFFFF;

constexpr int kMaxKinds = 10;  // the step itself keeps a byte for each kind it tried, ten of them
constexpr int kMaxBonuses = kRowModCount * kPropertySlots;

// What a bonus is: its stat, or the function of a property that names no stat (enhanced damage is one: its
// function gives stats 17 and 18, or one more point of damage to a base too weak for a percentage to show).
constexpr uint32_t kByFunction = 0x10000;
constexpr uint32_t kEnhancedDamage = kByFunction | 7;
constexpr uint32_t kEnhancedDefense = 16;   // item_armor_percent
constexpr uint32_t kMaxDamagePercent = 17;  // item_maxdamage_percent
constexpr uint32_t kMinDamagePercent = 18;  // item_mindamage_percent
constexpr uint32_t kAttackRating = 19;      // tohit

constexpr int weight(uint32_t what) {
  return what == kEnhancedDamage || what == kEnhancedDefense || what == kMaxDamagePercent || what == kMinDamagePercent
             ? 4
             : what == kAttackRating ? 2 : 1;
}

struct Bonus {
  uint32_t what = 0;
  int32_t lo = 0, hi = 0;  // its range, as magnitudes
};
struct Kind {
  bool fits = false;  // the step picks among it for the item, and the game's test says it goes on it
  int count = 0;
  Bonus bonus[kMaxBonuses];
};

// How many of the table's kinds the step picks among for a base, from the first on: all of them, or the few (the
// step's own number, 4) for one that is thrown or has no durability.
constexpr int picked_among(int kinds, int few, bool thrown, bool no_durability) {
  const int n = thrown || no_durability ? few : kinds;
  return n < kinds ? n : kinds;
}

constexpr int32_t magnitude(int32_t v) { return v == INT32_MIN ? INT32_MAX : v < 0 ? -v : v; }

// The bonuses of a row. `property_row(id)` hands out the Properties row of a property (kPropertyRowSize bytes that
// stay until it is asked again), or null. False when a row is not to be had or the bonuses are more than fit.
template <typename PropertyRow>
bool bonuses_of(const uint8_t* row, PropertyRow&& property_row, Kind* out) {
  out->count = 0;
  for (int m = 0; m < kRowModCount; ++m) {
    int32_t mod[4] = {};  // property, param, min, max
    std::memcpy(mod, row + kRowMods + static_cast<size_t>(m) * kRowModSize, sizeof(mod));
    if (mod[0] < 0) continue;
    const uint8_t* const property = property_row(mod[0]);
    if (!property) return false;
    uint8_t funcs[kPropertySlots] = {};
    uint16_t stats[kPropertySlots] = {};
    std::memcpy(funcs, property + kPropertyFunc, sizeof(funcs));
    std::memcpy(stats, property + kPropertyStat, sizeof(stats));
    const int32_t a = magnitude(mod[2]), b = magnitude(mod[3]);
    for (int j = 0; j < kPropertySlots; ++j) {
      if (!funcs[j]) continue;
      if (out->count >= kMaxBonuses) return false;
      Bonus& bonus = out->bonus[out->count++];
      bonus.what = stats[j] == kNoStat ? kByFunction | funcs[j] : stats[j];
      bonus.lo = a < b ? a : b;
      bonus.hi = a < b ? b : a;
    }
  }
  return true;
}

// The kinds to take of the `count` the step picks among: a bit for each kind that fits and gives the most (one
// kind, or several that give the same). 0 when none fits.
inline uint32_t best(const Kind* kinds, int count) {
  if (count > kMaxKinds) count = kMaxKinds;
  const auto most = [kinds, count](uint32_t what) {
    int32_t top = 0;
    for (int k = 0; k < count; ++k)
      for (int i = 0; kinds[k].fits && i < kinds[k].count; ++i)
        if (kinds[k].bonus[i].what == what && kinds[k].bonus[i].hi > top) top = kinds[k].bonus[i].hi;
    return top > 0 ? top : 1;
  };
  constexpr double kEpsilon = 1e-9;
  double tops[kMaxKinds] = {}, bottoms[kMaxKinds] = {};
  double best_tops = -1, best_bottoms = -1;
  for (int k = 0; k < count; ++k) {
    if (!kinds[k].fits) continue;
    for (int i = 0; i < kinds[k].count; ++i) {
      const Bonus& b = kinds[k].bonus[i];
      const double share = static_cast<double>(weight(b.what)) / most(b.what);
      tops[k] += b.hi * share;
      bottoms[k] += b.lo * share;
    }
    if (tops[k] > best_tops + kEpsilon || (tops[k] > best_tops - kEpsilon && bottoms[k] > best_bottoms)) {
      best_tops = tops[k];
      best_bottoms = bottoms[k];
    }
  }
  uint32_t take = 0;
  for (int k = 0; k < count; ++k)
    if (kinds[k].fits && tops[k] > best_tops - kEpsilon && bottoms[k] > best_bottoms - kEpsilon) take |= 1u << k;
  return take;
}

}  // namespace d2rcc::superior
