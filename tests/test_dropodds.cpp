// The drop odds (src/dropodds.h): the chances the plugin works out against the game's own routines, written here
// a second time as the game has them - the quality roll (0x4421B0), the drop core's walk of a treasure class
// (0x4404F0) and what the generator makes of a request (0x442D60 with its unique and set steps, the ethereal and
// the socket roll), all on the game's random stream - and run a few hundred thousand times. Then a small game of
// its own (a few items, treasure classes, a loot filter): what drops when only what the filter shows is picked
// must be what drops when everything is rolled and what the filter hides is thrown away. What is checked is the
// arithmetic; that the routines are read right is for the game to say (the console's `cabbycodes dropcheck`).
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <vector>

#include "dropodds.h"
#include "game.h"
#include "itemgen.h"
#include "log.h"
#include "sites.h"

using namespace d2rcc;
using dropodds::Kind;
using dropodds::Leaf;
using dropodds::Odds;
using dropodds::Ratio;
using itemgen::Rng;

static int32_t g_magic_find = 0;       // the killer's
constexpr int kUniqueBits = 0x3638;    // where a game keeps which uniques came

// What dropodds.cpp asks of the plugin, as far as these checks go.
namespace d2rcc {
void logf(const char*, ...) {}
void log_warn(const char* format, ...) {
  va_list args;
  va_start(args, format);
  std::printf("warning: ");
  std::vprintf(format, args);
  std::printf("\n");
  va_end(args);
}
void log_error(const char*, ...) {}
long long log_spent_us() { return 0; }
bool log_first(volatile long* count, long limit, LogLevel) {
  const long n = *count + 1;
  *count = n;
  return n <= limit;
}
namespace game {
uint32_t unit_type(Unit*) { return kPlayer; }
int32_t get_stat(Unit*, int stat, uint16_t) { return stat == 80 ? g_magic_find : 0; }
bool owned_by_local_player(Unit*) { return false; }
Unit* local_server_player() { return nullptr; }
}  // namespace game
namespace sites {
int unique_bits_offset() { return kUniqueBits; }
}  // namespace sites
}  // namespace d2rcc

static int g_failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
      ++g_failures;                                                     \
    }                                                                   \
  } while (0)

// --- the quality roll, as the game has it ------------------------------------------------------------------------
static int roll_quality(Rng& rng, const Ratio& r, const Kind& k, int32_t d, int32_t mf, const uint16_t* mods) {
  const int32_t* row = &r.unique;
  if (k.normal_only) return 2;
  if (k.unique_only) return 7;
  if (k.always_magic && k.quest) return 7;
  int32_t with = 100;
  bool bonus = false;
  int32_t chance = 0;
  bool low = false;
  if (mf == 0) {
    chance = (row[0] - d / row[1]) * 0x80;
  } else if (mf < -99) {
    low = true;
  } else {
    with = mf + 100;
    bonus = true;
    const int32_t base = row[0] - d / row[1];
    chance = base * 0x80;
    int32_t e = with;
    if (0x6e < with) e = mf * 0xfa / (mf + 0xfa) + 100;
    if (e != 0) chance = base * 0x3200 / e;
  }
  if (!low) {
    if (chance <= row[2]) chance = row[2];
    chance -= static_cast<int32_t>(mods[3]) * chance / 1024;
    if (chance < 1) return 7;
    if (rng.roll(chance) < 0x80) return 7;
    int32_t base = row[6] - d / row[7];
    chance = base * 0x80;
    if (bonus) {
      int32_t e = with;
      if (0x6e < with) e = mf * 500 / (mf + 500) + 100;
      if (e != 0) chance = base * 0x3200 / e;
    }
    if (chance <= row[8]) chance = row[8];
    chance -= static_cast<int32_t>(mods[2]) * chance / 1024;
    if (chance < 1) return 5;
    if (rng.roll(chance) < 0x80) return 5;
    if (k.can_be_rare) {
      base = row[3] - d / row[4];
      chance = base * 0x80;
      if (bonus) {
        int32_t e = with;
        if (0x6e < with) e = mf * 600 / (mf + 600) + 100;
        if (e != 0) chance = base * 0x3200 / e;
      }
      if (chance <= row[5]) chance = row[5];
      chance -= static_cast<int32_t>(mods[1]) * chance / 1024;
      if (chance < 1) return 6;
      if (rng.roll(chance) < 0x80) return 6;
    }
    if (k.always_magic) return 4;
    base = row[9] - d / row[10];
    chance = base * 0x80;
    if (bonus) chance = base * 0x3200 / with;
    if (chance <= row[11]) chance = row[11];
    chance -= static_cast<int32_t>(mods[0]) * chance / 1024;
    if (chance < 1) return 4;
    if (rng.roll(chance) < 0x80) return 4;
  }
  int32_t c = row[12] - d / row[13];
  if (0 < c * 0x80) {
    if (0x7f < rng.roll(c * 0x80)) {
      c = row[14] - d / row[15];
      if (0 < c * 0x80 && 0x7f < rng.roll(c * 0x80)) return 1;
      return 2;
    }
  }
  return 3;
}

static void check_quality(const char* what, const Ratio& r, const Kind& k, int32_t d, int32_t mf,
                          const uint16_t* mods) {
  const Odds odds = dropodds::quality_odds(r, k, d, mf, mods);
  constexpr int kRolls = 400000;
  int counts[8] = {};
  Rng rng(0x1234567u + static_cast<uint32_t>(d * 977 + mf));
  for (int i = 0; i < kRolls; ++i) ++counts[roll_quality(rng, r, k, d, mf, mods)];
  double sum = 0, worst = 0;
  for (int q = 1; q <= 7; ++q) {
    sum += odds.of[q];
    const double expected = odds.of[q] * kRolls;
    const double sigma = std::sqrt(expected * (1 - odds.of[q]) + 1e-9);
    const double off = sigma > 0 ? std::fabs(counts[q] - expected) / (sigma < 1 ? 1 : sigma) : 0;
    if (off > worst) worst = off;
    if (odds.of[q] == 0) CHECK(counts[q] == 0);
  }
  std::printf("quality %-34s unique %.4f set %.4f rare %.4f magic %.4f superior %.4f normal %.4f low %.4f; "
              "%d rolls off by %.1f sigma at most\n",
              what, odds.of[7], odds.of[5], odds.of[6], odds.of[4], odds.of[3], odds.of[2], odds.of[1], kRolls, worst);
  CHECK(std::fabs(sum - 1) < 1e-9);
  CHECK(worst < 5);
}

// --- treasure classes, as the game keeps them ---------------------------------------------------------------------
struct TestClasses {
  std::vector<uint8_t> rows;                  // the records
  std::vector<std::vector<uint8_t>> entries;  // each record's entries
  uint32_t add(int32_t picks, const uint16_t* mods, uint8_t flags = 0) {
    const uint32_t id = static_cast<uint32_t>(rows.size() / dropodds::kTcSize);
    rows.resize(rows.size() + dropodds::kTcSize, 0);
    entries.emplace_back();
    uint8_t* r = record(id);
    std::memcpy(r + dropodds::kTcPicks, &picks, sizeof(picks));
    r[dropodds::kTcFlags] = flags;
    if (mods) std::memcpy(r + dropodds::kTcMods, mods, sizeof(uint16_t) * dropodds::kMods);
    return id;
  }
  uint8_t* record(uint32_t id) { return rows.data() + static_cast<size_t>(id) * dropodds::kTcSize; }
  // The treasure class has a condition of the game's own (a calc).
  void condition(uint32_t id, int32_t calc) {
    uint8_t* r = record(id);
    r[dropodds::kTcFlags] |= dropodds::kTcConditional;
    r[dropodds::kTcFlags2] |= dropodds::kTcCalcCondition;
    std::memcpy(r + dropodds::kTcCalc, &calc, sizeof(calc));
  }
  void entry(uint32_t id, uint32_t item, int32_t chance, uint8_t flags = 0, uint16_t extra = 0) {
    std::vector<uint8_t>& list = entries[id];
    int32_t total = 0;
    std::memcpy(&total, record(id) + dropodds::kTcTotal, sizeof(total));
    uint8_t e[dropodds::kEntrySize] = {};
    std::memcpy(e + dropodds::kEntryStartClassic, &total, sizeof(total));
    std::memcpy(e + dropodds::kEntryStart, &total, sizeof(total));
    std::memcpy(e + dropodds::kEntryItem, &item, sizeof(item));
    e[dropodds::kEntryFlags] = flags;
    std::memcpy(e + dropodds::kEntryExtra, &extra, sizeof(extra));
    list.insert(list.end(), e, e + sizeof(e));
    total += chance;
    std::memcpy(record(id) + dropodds::kTcTotal, &total, sizeof(total));
    std::memcpy(record(id) + dropodds::kTcTotalClassic, &total, sizeof(total));
  }
  // The entries' addresses go into the records once nothing is added any more.
  void finish() {
    for (uint32_t id = 0; id < entries.size(); ++id) {
      const uintptr_t at = reinterpret_cast<uintptr_t>(entries[id].data());
      const uint64_t count = entries[id].size() / dropodds::kEntrySize;
      std::memcpy(record(id) + dropodds::kTcEntries, &at, sizeof(at));
      std::memcpy(record(id) + dropodds::kTcCount, &count, sizeof(count));
    }
  }
};

template <typename T>
static T get(const uint8_t* p, uintptr_t offset) {
  T v{};
  std::memcpy(&v, p + offset, sizeof(T));
  return v;
}

struct Key {
  uint32_t item;
  uint16_t named;  // the unique or set row an entry names, and which of the two (the entry's flags)
  uint8_t flags;
  uint16_t mods[dropodds::kMods];
  bool operator<(const Key& o) const {
    if (item != o.item) return item < o.item;
    if (named != o.named) return named < o.named;
    if (flags != o.flags) return flags < o.flags;
    return std::memcmp(mods, o.mods, sizeof(mods)) < 0;
  }
};

// The drop core's walk with NoDrop skipped: a treasure class's picks one after the other, a pick by a roll of the
// chances together or, below 0, by its number; a treasure class among the entries walked before the next pick.
using Drops = std::function<void(const uint8_t* entry, const uint16_t* mods)>;
static void walk(Rng& rng, TestClasses& c, uint32_t id, const uint16_t* above,
                 const std::function<bool(const uint8_t*)>& takes, const Drops& drops, int depth = 0) {
  if (depth > 60) return;
  const uint8_t* r = c.record(id);
  const int32_t picks = get<int32_t>(r, dropodds::kTcPicks);
  const uint8_t flags = r[dropodds::kTcFlags];
  uint16_t mods[dropodds::kMods];
  for (int m = 0; m < dropodds::kMods; ++m) {
    const uint16_t own = get<uint16_t>(r, dropodds::kTcMods + 2 * m);
    mods[m] = above && above[m] != 0 ? (above[m] < own ? own : above[m]) : own;
  }
  const int32_t whole = picks < 0 ? -picks : picks;
  int32_t left = whole > 1 ? whole : 1;
  const std::vector<uint8_t>& list = c.entries[id];
  const size_t count = list.size() / dropodds::kEntrySize;
  const int32_t total = get<int32_t>(r, dropodds::kTcTotal);
  while (left != 0) {
    if (total == 0) break;
    // The entries the walk takes, each with where its share starts.
    std::vector<std::pair<const uint8_t*, int32_t>> taken;
    int32_t chances = total, out = 0;
    for (size_t i = 0; i < count; ++i) {
      const uint8_t* e = list.data() + i * dropodds::kEntrySize;
      const int32_t start = get<int32_t>(e, dropodds::kEntryStart);
      const int32_t chance = (i + 1 < count ? get<int32_t>(e + dropodds::kEntrySize, dropodds::kEntryStart) : total) - start;
      if ((flags & dropodds::kTcHasConditional) && (e[dropodds::kEntryFlags] & dropodds::kEntryConditional) &&
          !takes(e)) {
        out += chance;
        continue;
      }
      taken.emplace_back(e, start - out);
    }
    chances -= out;
    if (chances == 0 || taken.empty()) break;
    int32_t roll = 0;
    if (picks < 0) {
      roll = whole - left;
      if (chances <= roll) break;
    } else {
      roll = static_cast<int32_t>(rng.roll(chances));
    }
    --left;
    const uint8_t* e = taken[0].first;
    for (const auto& t : taken)
      if (t.second <= roll) e = t.first;
    const uint32_t item = get<uint32_t>(e, dropodds::kEntryItem);
    if (e[dropodds::kEntryFlags] & dropodds::kEntryClass) walk(rng, c, item & 0xFFFF, mods, takes, drops, depth + 1);
    else if (item != 0xFFFFFFFFu) drops(e, mods);
  }
}

static void check_walk(const char* what, TestClasses& c, uint32_t top,
                       const std::function<bool(const uint8_t*)>& takes, bool expect_conditional) {
  dropodds::Classes classes;
  classes.rows = reinterpret_cast<uintptr_t>(c.rows.data());
  classes.count = static_cast<uint32_t>(c.entries.size());
  classes.takes = [&takes](uintptr_t, const uint8_t* e, int32_t) { return takes(e); };
  std::vector<Leaf> leaves;
  bool conditional = false;
  const bool ok = dropodds::leaves_of(classes, classes.rows + static_cast<uintptr_t>(top) * dropodds::kTcSize,
                                      &leaves, &conditional);
  CHECK(ok);
  CHECK(conditional == expect_conditional);
  constexpr int kWalks = 300000;
  std::map<Key, double> made;
  Rng rng(0xC0FFEEu + top);
  const Drops tally = [&made](const uint8_t* e, const uint16_t* mods) {
    Key k{};
    k.item = get<uint32_t>(e, dropodds::kEntryItem);
    k.flags = e[dropodds::kEntryFlags] & (dropodds::kEntryUnique | dropodds::kEntrySet);
    k.named = get<uint16_t>(e, dropodds::kEntryExtra);
    std::memcpy(k.mods, mods, sizeof(k.mods));
    made[k] += 1;
  };
  for (int i = 0; i < kWalks; ++i) walk(rng, c, top, nullptr, takes, tally);
  double worst = 0, walked = 0, worked = 0;
  for (const Leaf& l : leaves) {
    Key k{};
    k.item = l.item_class;
    k.flags = l.flags;
    k.named = l.extra;
    std::memcpy(k.mods, l.mods, sizeof(k.mods));
    const double seen = made.count(k) ? made[k] : 0;
    made.erase(k);
    const double expected = l.weight * kWalks;
    // A share of every pick varies as a roll does; a count that is sure does not vary at all.
    const double sigma = std::sqrt(expected) + 1;
    const double off = std::fabs(seen - expected) / sigma;
    if (off > worst) worst = off;
    walked += seen;
    worked += expected;
  }
  std::printf("walk    %-34s %zu leaves, %.3f items a walk (%.3f walked); off by %.1f sigma at most; %zu leaves "
              "walked that were not worked out\n",
              what, leaves.size(), worked / kWalks, walked / kWalks, worst, made.size());
  CHECK(made.empty());
  CHECK(worst < 5);
}

// --- a small game ----------------------------------------------------------------------------------------------
//
// Tables as the game keeps them (only the fields that are read), the generator as the game has it, a loot filter
// that goes by an item's class, its quality and whether it is ethereal or socketed, and the rows a Chronicle misses.
namespace world {

constexpr uint32_t kItemsRow = 0x1C0, kTypesRow = 0xE8, kRatioRow = 0x44, kUniqueRow = 0x15C, kSetRow = 0x1CC;
constexpr int kTypes = 0x60;
// Item types: the two the game's own tests ask for, and some that count as them or do not.
constexpr int16_t kGold = 4, kBodyArmor = 3, kRing = 0x0A, kCharm = 0x0D, kSword = 0x1E, kKey = 0x29;
constexpr int16_t kWeapon = 0x2D, kArmor = 0x32, kPotion = 0x4C;
enum Class : uint32_t { kSwordItem, kPlate, kRingItem, kCharmItem, kPotionItem, kGoldItem, kBlade, kRelic, kKeyItem, kItems };

std::vector<uint8_t> items, types, ratios, uniques, sets, game;
TestClasses classes;
std::vector<uint16_t> shown(kItems, 0);  // the filter: a bit per quality and ethereal-or-socketed
bool filter = true;                      // the filter's answers count
bool chronicle = false;                  // ... and the rows the Chronicle misses: a unique or set item of one of them
dropodds::Rows missing_uniques, missing_sets;
std::set<int32_t> holding;    // the game's conditions that hold
bool all_superior = false, all_ethereal = false, all_socketed = false;  // the loot switches
int32_t item_level = 60;

template <typename T>
void put(std::vector<uint8_t>& table, uint32_t row_size, uint32_t row, uintptr_t offset, T value) {
  std::memcpy(table.data() + static_cast<size_t>(row) * row_size + offset, &value, sizeof(T));
}
template <typename T>
T take(const std::vector<uint8_t>& table, uint32_t row_size, uint32_t row, uintptr_t offset) {
  T v{};
  std::memcpy(&v, table.data() + static_cast<size_t>(row) * row_size + offset, sizeof(T));
  return v;
}
constexpr uint32_t code(char a, char b, char c) {
  return static_cast<uint8_t>(a) | static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8 |
         static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16 | 0x20u << 24;
}

void item(uint32_t cls, uint32_t its_code, int16_t type, uint8_t level, uint8_t durability, bool no_durability,
          bool sockets, uint8_t most_sockets, bool unique_only = false) {
  put<uint32_t>(items, kItemsRow, cls, 0x80, its_code);
  put<uint32_t>(items, kItemsRow, cls, 0x88, its_code + 1);  // its exceptional and elite versions are others
  put<uint32_t>(items, kItemsRow, cls, 0x8C, its_code + 2);
  put<uint8_t>(items, kItemsRow, cls, 0x10D, level);
  put<uint8_t>(items, kItemsRow, cls, 0x121, durability);
  put<uint8_t>(items, kItemsRow, cls, 0x122, no_durability ? 1 : 0);
  put<int16_t>(items, kItemsRow, cls, 0x12E, type);
  put<uint8_t>(items, kItemsRow, cls, 0x139, unique_only ? 1 : 0);
  put<uint8_t>(items, kItemsRow, cls, 0x147, sockets ? 1 : 0);
  put<uint8_t>(items, kItemsRow, cls, 0x148, most_sockets);
}

void type(int16_t id, int16_t counts_as, bool magic, bool rare, bool normal) {
  put<uint16_t>(types, kTypesRow, id, 0x04, static_cast<uint16_t>(counts_as));
  put<uint8_t>(types, kTypesRow, id, 0x14, magic ? 1 : 0);
  put<uint8_t>(types, kTypesRow, id, 0x15, rare ? 1 : 0);
  put<uint8_t>(types, kTypesRow, id, 0x16, normal ? 1 : 0);
  const uint8_t most[3] = {2, 3, 4}, levels[2] = {25, 40};
  std::memcpy(types.data() + static_cast<size_t>(id) * kTypesRow + 0x18, most, sizeof(most));
  std::memcpy(types.data() + static_cast<size_t>(id) * kTypesRow + 0x1B, levels, sizeof(levels));
}

void ratio(uint32_t row, const Ratio& r, uint16_t version, bool uber, bool class_own) {
  std::memcpy(ratios.data() + static_cast<size_t>(row) * kRatioRow, &r, sizeof(r));
  put<uint16_t>(ratios, kRatioRow, row, 0x40, version);
  put<uint8_t>(ratios, kRatioRow, row, 0x42, uber ? 1 : 0);
  put<uint8_t>(ratios, kRatioRow, row, 0x43, class_own ? 1 : 0);
}

// Row flags: 1 it is picked among, 2 it comes any number of times, 4 it never comes.
void unique(uint32_t row, uint32_t its_code, uint32_t flags, int32_t rarity, int16_t level, int32_t calc = -1) {
  put<uint16_t>(uniques, kUniqueRow, row, 0x24, 100);
  put<uint32_t>(uniques, kUniqueRow, row, 0x28, its_code);
  put<uint32_t>(uniques, kUniqueRow, row, 0x2C, flags);
  put<int32_t>(uniques, kUniqueRow, row, 0x34, rarity);
  put<int16_t>(uniques, kUniqueRow, row, 0x38, level);
  put<int32_t>(uniques, kUniqueRow, row, 0x90, calc);
}

void set_item(uint32_t row, uint32_t its_code, uint32_t flags, int32_t rarity, int16_t level, int16_t set) {
  put<uint16_t>(sets, kSetRow, row, 0x22, 100);
  put<uint32_t>(sets, kSetRow, row, 0x28, its_code);
  put<uint32_t>(sets, kSetRow, row, 0x2C, flags);
  put<int16_t>(sets, kSetRow, row, 0x30, set);
  put<int16_t>(sets, kSetRow, row, 0x34, level);
  put<int32_t>(sets, kSetRow, row, 0x38, rarity);
  put<int32_t>(sets, kSetRow, row, 0x90, -1);
}

bool came(uint32_t unique_row) {
  return (take<uint32_t>(game, 1, 0, kUniqueBits + (unique_row >> 5) * 4) >> (unique_row & 31) & 1) != 0;
}
void came(uint32_t unique_row, bool yes) {
  uint32_t word = take<uint32_t>(game, 1, 0, kUniqueBits + (unique_row >> 5) * 4);
  word = yes ? word | 1u << (unique_row & 31) : word & ~(1u << (unique_row & 31));
  put<uint32_t>(game, 1, 0, kUniqueBits + (unique_row >> 5) * 4, word);
}

// The game's test of an item's type (0x373890).
bool counts_as(int type, int target, int depth = 0) {
  if (type <= 0 || type >= kTypes || depth > 8) return false;
  if (type == target) return true;
  return counts_as(take<uint16_t>(types, kTypesRow, type, 0x04), target, depth + 1) ||
         counts_as(take<uint16_t>(types, kTypesRow, type, 0x06), target, depth + 1);
}

struct Item {
  uint32_t cls = 0;
  int quality = 0;
  bool ethereal = false, socketed = false;
  int row = -1;  // the unique or the set item it is
  bool operator<(const Item& o) const {
    if (cls != o.cls) return cls < o.cls;
    if (quality != o.quality) return quality < o.quality;
    if (ethereal != o.ethereal) return ethereal < o.ethereal;
    if (socketed != o.socketed) return socketed < o.socketed;
    return row < o.row;
  }
};

// The unique step (0x443910) for an expansion item: the row asked for by name when it is one of the base's that
// may come at the item's level, else one of those that are picked among, by their rarity; none when the one it
// comes to came already. -2: nothing to pick among for an item that must be unique (it stays one).
int unique_step(Rng& rng, uint32_t cls, int named, int level) {
  const uint32_t its_code = take<uint32_t>(items, kItemsRow, cls, 0x80);
  std::vector<std::pair<int, int>> among;  // the row, where its share of the rarities starts
  int total = 0, picked = -1;
  const uint32_t rows = static_cast<uint32_t>(uniques.size() / kUniqueRow);
  for (uint32_t i = 0; i < rows && picked < 0; ++i) {
    if (take<uint32_t>(uniques, kUniqueRow, i, 0x28) != its_code) continue;
    const uint32_t flags = take<uint32_t>(uniques, kUniqueRow, i, 0x2C);
    if (flags & 4) continue;
    if (take<int16_t>(uniques, kUniqueRow, i, 0x38) > level) continue;
    if (named != 0 && named - 1 == static_cast<int>(i)) {
      picked = static_cast<int>(i);
      break;
    }
    const int32_t calc = take<int32_t>(uniques, kUniqueRow, i, 0x90);
    if (!(flags & 1) || (calc != -1 && !holding.count(calc))) continue;
    among.emplace_back(static_cast<int>(i), total);
    const int32_t rarity = take<int32_t>(uniques, kUniqueRow, i, 0x34);
    total += rarity < 1 ? 1 : rarity;
  }
  if (picked < 0) {
    if (among.empty()) return take<uint8_t>(items, kItemsRow, cls, 0x139) ? -2 : -1;
    const int roll = static_cast<int>(rng.roll(total));
    size_t at = 1;
    while (at < among.size() && roll >= among[at].second) ++at;
    picked = among[at - 1].first;
  }
  return came(static_cast<uint32_t>(picked)) ? -1 : picked;
}

// The set step (0x58ACD0) for an expansion item.
int set_step(Rng& rng, uint32_t cls, int named, int level, uint32_t flags) {
  const uint32_t its_code = take<uint32_t>(items, kItemsRow, cls, 0x80);
  std::vector<std::pair<int, int>> among;  // the row, its rarity
  uint32_t total = 0;
  const uint32_t rows = static_cast<uint32_t>(sets.size() / kSetRow);
  for (uint32_t i = 0; i < rows; ++i) {
    if (take<uint32_t>(sets, kSetRow, i, 0x28) != its_code) continue;
    const uint32_t row_flags = take<uint32_t>(sets, kSetRow, i, 0x2C);
    if (row_flags & 4) continue;
    if (take<int16_t>(sets, kSetRow, i, 0x34) > level) continue;
    if (take<int16_t>(sets, kSetRow, i, 0x30) == 0x1D && !(flags & 1)) continue;
    if (named != 0 && named - 1 == static_cast<int>(i)) return static_cast<int>(i);
    const int32_t calc = take<int32_t>(sets, kSetRow, i, 0x90);
    if (!(row_flags & 1) || (calc != -1 && !holding.count(calc))) continue;
    int32_t rarity = take<int32_t>(sets, kSetRow, i, 0x38);
    if (rarity == 0) rarity = 1;
    among.emplace_back(static_cast<int>(i), rarity);
    total += static_cast<uint32_t>(rarity);
  }
  if (!total) return -1;
  int roll = static_cast<int>(rng.roll(static_cast<int32_t>(total)));
  for (const auto& a : among) {
    if (roll < a.second) return a.first;
    roll -= a.second;
  }
  return -1;
}

// The generator (0x442D60) as far as the loot filter can tell its work apart: the quality step under the loot
// switches, what the item must be, the quality's own step and what comes of one that fails, the ethereal roll and
// the socket roll. `flags` is the request's (2 never ethereal, 4 ethereal, 8 no sockets, 0x10 sockets).
Item generate(Rng& rng, uint32_t cls, int asked, int named, uint32_t flags, int level) {
  Item made;
  made.cls = cls;
  const int16_t its_type = take<int16_t>(items, kItemsRow, cls, 0x12E);
  const bool always_magic = take<uint8_t>(types, kTypesRow, its_type, 0x14) != 0;
  const bool can_be_rare = take<uint8_t>(types, kTypesRow, its_type, 0x15) != 0;
  const bool normal_only = take<uint8_t>(types, kTypesRow, its_type, 0x16) != 0;
  const bool weapon = counts_as(its_type, kWeapon), armor = counts_as(its_type, kArmor);
  int q = asked;
  if (all_superior && (q == 1 || q == 2)) q = 3;
  if (all_ethereal) flags |= 4;
  if (all_socketed) flags |= 0x10;
  if (always_magic && q < 4) q = 4;
  if (!can_be_rare && q == 6) q = 4;
  if (take<uint8_t>(items, kItemsRow, cls, 0x139)) q = 7;
  if (normal_only) q = 2;
  switch (q) {
    case 1:
    case 3:
      if (!weapon && !armor) q = 2;
      break;
    case 5:
      made.row = set_step(rng, cls, named, level, flags);
      if (made.row < 0) q = 4;
      break;
    case 7:
      made.row = unique_step(rng, cls, named, level);
      if (made.row == -1) q = can_be_rare ? 6 : 4;
      break;
    default:
      break;
  }
  if (made.row < 0) made.row = -1;
  made.quality = q;
  const bool durable =
      !take<uint8_t>(items, kItemsRow, cls, 0x122) && take<uint8_t>(items, kItemsRow, cls, 0x121) != 0;
  if (!(flags & 2) && (weapon || armor) && durable && q != 1 && q != 5) {
    const uint32_t roll = rng.roll(100);
    made.ethereal = (flags & 4) || roll <= 4;
  }
  if (q == 2 || q == 3) {
    int most = take<uint8_t>(types, kTypesRow, its_type, 0x1A);
    if (level <= take<uint8_t>(types, kTypesRow, its_type, 0x1B)) most = take<uint8_t>(types, kTypesRow, its_type, 0x18);
    else if (level <= take<uint8_t>(types, kTypesRow, its_type, 0x1C)) most = take<uint8_t>(types, kTypesRow, its_type, 0x19);
    most = std::min<int>(most, take<uint8_t>(items, kItemsRow, cls, 0x148));
    if (take<uint8_t>(items, kItemsRow, cls, 0x147) && !take<uint8_t>(items, kItemsRow, cls, 0x142) && most > 0) {
      uint32_t roll = rng.roll(100);
      if (!(flags & 8)) {
        if (flags & 0x10) roll = 0;
        made.socketed = roll <= 0x20;
      }
    }
  }
  return made;
}

bool filter_shows(const Item& it) {
  if (it.cls == kGoldItem) return true;
  return (shown[it.cls] >> ((it.quality - 1) * 2 + (it.ethereal || it.socketed ? 1 : 0)) & 1) != 0;
}

bool chronicle_misses(const Item& it) {
  if (it.row < 0) return false;
  if (it.quality == 7) return missing_uniques.has(static_cast<uint32_t>(it.row));
  return it.quality == 5 && missing_sets.has(static_cast<uint32_t>(it.row));
}

// What a drop that stays must be: what the filter shows, or a unique or set item the Chronicle misses.
bool wanted(const Item& it) { return (filter && filter_shows(it)) || (chronicle && chronicle_misses(it)); }

// What the quality roll goes by, from the tables as the game reads them.
Kind kind_of(uint32_t cls) {
  const int16_t its_type = take<int16_t>(items, kItemsRow, cls, 0x12E);
  Kind k;
  k.always_magic = take<uint8_t>(types, kTypesRow, its_type, 0x14) != 0;
  k.can_be_rare = take<uint8_t>(types, kTypesRow, its_type, 0x15) != 0;
  k.normal_only = take<uint8_t>(types, kTypesRow, its_type, 0x16) != 0;
  k.unique_only = take<uint8_t>(items, kItemsRow, cls, 0x139) != 0;
  return k;
}

const Ratio kRatio{400, 1, 6400, 100, 2, 3200, 160, 2, 5600, 34, 3, 192, 12, 8, 2, 2};

// What the drop core asks the item maker for, for an entry its walk came to: the quality (the entry's own, or the
// quality roll's), the row named, and the flags its two rolls for the treasure classes' chances give.
struct Asked {
  uint32_t cls = 0;
  int quality = 0, named = 0;
  uint32_t flags = 0;
};
Asked ask(Rng& rng, const uint8_t* entry, const uint16_t* mods, int forced_quality) {
  Asked a;
  a.cls = get<uint32_t>(entry, dropodds::kEntryItem);
  const uint8_t flags = entry[dropodds::kEntryFlags];
  if (flags & dropodds::kEntryUnique) {
    a.quality = 7;
    a.named = get<uint16_t>(entry, dropodds::kEntryExtra) + 1;
  } else if (flags & dropodds::kEntrySet) {
    a.quality = 5;
    a.named = get<uint16_t>(entry, dropodds::kEntryExtra) + 1;
  } else if (forced_quality) {
    a.quality = forced_quality;
  } else {
    a.quality = roll_quality(rng, kRatio, kind_of(a.cls),
                             item_level - take<uint8_t>(items, kItemsRow, a.cls, 0x10D), g_magic_find, mods);
  }
  if (mods[dropodds::kModEthereal] && (rng.step() & 0x3FF) < mods[dropodds::kModEthereal]) a.flags |= 4;
  if (mods[dropodds::kModSockets] && (rng.step() & 0x3FF) < mods[dropodds::kModSockets]) a.flags |= 0x10;
  return a;
}

D2RL::DataTables::Result __cdecl get_table(const D2RL::PluginContext*, D2RL::DataTables::Bank,
                                           D2RL::DataTables::TableId id, D2RL::DataTables::TableView* view) noexcept {
  using D2RL::DataTables::TableId;
  const std::vector<uint8_t>* table = nullptr;
  uint32_t size = 0;
  switch (id) {
    case TableId::Items: table = &items, size = kItemsRow; break;
    case TableId::ItemTypes: table = &types, size = kTypesRow; break;
    case TableId::ItemRatio: table = &ratios, size = kRatioRow; break;
    case TableId::UniqueItems: table = &uniques, size = kUniqueRow; break;
    case TableId::SetItems: table = &sets, size = kSetRow; break;
    case TableId::TreasureClasses: table = &classes.rows, size = dropodds::kTcSize; break;
    default: return D2RL::DataTables::Result::NotFound;
  }
  view->rows = table->data();
  view->rowCount = static_cast<uint32_t>(table->size() / size);
  view->rowSize = size;
  view->revision = 1;
  return D2RL::DataTables::Result::Success;
}

uint32_t g_top = 0, g_plain = 0;

void build() {
  shown.assign(kItems, 0);
  items.assign(static_cast<size_t>(kItems) * kItemsRow, 0);
  types.assign(static_cast<size_t>(kTypes) * kTypesRow, 0);
  ratios.assign(4 * kRatioRow, 0);
  uniques.assign(12 * kUniqueRow, 0);
  sets.assign(4 * kSetRow, 0);
  game.assign(0x4000, 0);
  game[0x101] = 3;  // no ladder game
  for (int t = 0; t < kTypes; ++t) put<uint8_t>(types, kTypesRow, t, 0x20, 0xFF);  // no class's own
  type(kWeapon, 0, false, true, false);
  type(kArmor, 0, false, true, false);
  type(kSword, kWeapon, false, true, false);
  type(kBodyArmor, kArmor, false, true, false);
  type(kRing, 0, true, true, false);
  type(kCharm, 0, true, false, false);
  type(kPotion, 0, false, false, true);
  type(kGold, 0, false, false, false);
  type(kKey, 0, false, false, false);
  item(kSwordItem, code('s', 'w', 'd'), kSword, 40, 44, false, true, 4);
  item(kPlate, code('p', 'l', 't'), kBodyArmor, 30, 60, false, true, 3);
  item(kRingItem, code('r', 'i', 'n'), kRing, 1, 0, true, false, 0);
  item(kCharmItem, code('c', 'm', '1'), kCharm, 10, 0, true, false, 0);
  item(kPotionItem, code('h', 'p', '5'), kPotion, 1, 0, true, false, 0);
  item(kGoldItem, code('g', 'l', 'd'), kGold, 1, 0, true, false, 0);
  item(kBlade, code('7', 'c', 'r'), kSword, 50, 0, true, true, 6);  // no durability: never ethereal
  item(kRelic, code('r', 'l', 'c'), kKey, 1, 0, true, false, 0, true);  // must be unique
  item(kKeyItem, code('k', 'e', 'y'), kKey, 1, 0, true, false, 0);
  // The ratio the game takes is the expansion's for a base that is no exceptional or elite one and no class's own.
  const Ratio other{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  ratio(0, other, 0, false, false);
  ratio(1, kRatio, 100, false, false);
  ratio(2, other, 100, true, false);
  ratio(3, other, 100, false, true);
  // The sword's uniques: two that are picked among (the second came already), one above the item's level, one
  // that never comes, one that comes by name only, one whose condition holds and one whose does not.
  unique(0, code('s', 'w', 'd'), 1, 5, 20);
  unique(1, code('s', 'w', 'd'), 1, 2, 50);
  unique(2, code('s', 'w', 'd'), 1, 9, 90);
  unique(3, code('s', 'w', 'd'), 1 | 4, 9, 10);
  unique(4, code('s', 'w', 'd'), 0, 9, 10);
  unique(5, code('s', 'w', 'd'), 1, 3, 10, 5);
  unique(6, code('s', 'w', 'd'), 1, 9, 10, 6);
  unique(7, code('r', 'i', 'n'), 1, 1, 1);
  unique(8, code('r', 'i', 'n'), 1, 3, 1);
  unique(9, code('r', 'l', 'c'), 1, 1, 1);
  unique(10, code('p', 'l', 't'), 1, 1, 1);  // the armor's only one, and it came: a unique armor is rare
  unique(11, code('7', 'c', 'r'), 1 | 2, 1, 1);  // comes any number of times
  came(1, true);
  came(8, true);
  came(10, true);
  holding = {5};
  set_item(0, code('s', 'w', 'd'), 1, 1, 30, 3);
  set_item(1, code('p', 'l', 't'), 1, 1, 99, 3);     // above the item's level: a set armor is magic
  set_item(2, code('r', 'i', 'n'), 1, 1, 1, 0x1D);   // only when the request allows its set
  set_item(3, code('s', 'w', 'd'), 1, 3, 20, 4);     // the sword's second, three times as likely
  // The treasure classes.
  TestClasses& c = classes;
  c = TestClasses{};
  c.add(1, nullptr);
  const uint32_t equip = c.add(1, nullptr);
  c.entry(equip, kSwordItem, 10);
  c.entry(equip, kPlate, 6);
  c.entry(equip, kBlade, 3);
  c.entry(equip, kKeyItem, 2);
  const uint32_t good = c.add(1, nullptr);
  c.entry(good, kRingItem, 5);
  c.entry(good, kCharmItem, 3);
  c.entry(good, kRelic, 1);
  const uint32_t junk = c.add(2, nullptr);
  c.entry(junk, kPotionItem, 8);
  c.entry(junk, kGoldItem, 4, 0, 512);
  const uint32_t bonus = c.add(1, nullptr);
  c.entry(bonus, kRingItem, 1);
  c.condition(bonus, 5);
  const uint32_t never = c.add(1, nullptr);
  c.entry(never, kPotionItem, 1);
  c.condition(never, 6);
  const uint16_t mods[dropodds::kMods] = {512, 800, 800, 800, 100, 200};
  const uint32_t item_tc = c.add(1, mods, dropodds::kTcHasConditional);
  c.entry(item_tc, equip, 60, dropodds::kEntryClass);
  c.entry(item_tc, good, 20, dropodds::kEntryClass);
  c.entry(item_tc, kSwordItem, 2, dropodds::kEntryUnique, 4);  // the unique that comes by name only
  c.entry(item_tc, kSwordItem, 1, dropodds::kEntrySet, 0);
  c.entry(item_tc, bonus, 5, dropodds::kEntryClass | dropodds::kEntryConditional);
  c.entry(item_tc, never, 5, dropodds::kEntryClass | dropodds::kEntryConditional);
  const uint32_t plain = c.add(3, nullptr);  // no mods: low, normal and superior items come of it
  c.entry(plain, equip, 5, dropodds::kEntryClass);
  c.entry(plain, good, 1, dropodds::kEntryClass);
  const uint32_t top = c.add(-5, nullptr);
  c.entry(top, item_tc, 2, dropodds::kEntryClass);
  c.entry(top, junk, 2, dropodds::kEntryClass);
  c.entry(top, plain, 1, dropodds::kEntryClass);
  c.finish();
  g_top = top;
  g_plain = plain;
}

// Whether the drop core takes an entry that has a condition: a treasure class's own.
bool takes(const uint8_t* entry) {
  if (!(entry[dropodds::kEntryFlags] & dropodds::kEntryConditional)) return true;
  if (!(entry[dropodds::kEntryFlags] & dropodds::kEntryClass)) return true;
  const uint8_t* record = classes.record(get<uint32_t>(entry, dropodds::kEntryItem) & 0xFFFF);
  return !(record[dropodds::kTcFlags2] & dropodds::kTcCalcCondition) ||
         holding.count(get<int32_t>(record, dropodds::kTcCalc)) != 0;
}

dropodds::Request request(uint32_t tc, int forced_quality) {
  dropodds::Request r;
  r.game = game.data();
  r.source = reinterpret_cast<game::Unit*>(game.data());
  r.killer = reinterpret_cast<game::Unit*>(game.data());
  r.tc = reinterpret_cast<uintptr_t>(classes.record(tc));
  r.roll_level = item_level;
  r.item_level = item_level;
  r.forced_quality = forced_quality;
  r.bank = 3;
  r.all_superior = all_superior;
  r.all_ethereal = all_ethereal;
  r.all_socketed = all_socketed;
  r.holds = [](uintptr_t record) {
    const uint8_t* at = reinterpret_cast<const uint8_t*>(record);
    return !(at[dropodds::kTcFlags2] & dropodds::kTcCalcCondition) ||
           holding.count(get<int32_t>(at, dropodds::kTcCalc)) != 0;
  };
  r.takes = [](uintptr_t, const uint8_t*) { return true; };
  r.calc = [](int32_t calc) { return holding.count(calc) ? 1 : 0; };
  r.filter = filter;
  r.shown = [](uint32_t cls, uint16_t* bits) {
    if (cls >= shown.size()) return false;
    *bits = shown[cls];
    return true;
  };
  r.uniques = chronicle ? &missing_uniques : nullptr;
  r.sets = chronicle ? &missing_sets : nullptr;
  return r;
}

using Tally = std::map<Item, double>;

const char* const kNames[kItems] = {"sword", "plate", "ring", "charm", "potion", "gold", "blade", "relic", "key"};

// Two counts of the same drops: every kind of item has the same share of both, within what chance allows.
void compare(const char* what, const Tally& rolled, const Tally& picked) {
  double all_rolled = 0, all_picked = 0;
  for (const auto& r : rolled) all_rolled += r.second;
  for (const auto& r : picked) all_picked += r.second;
  std::set<Item> kinds;
  for (const auto& r : rolled) kinds.insert(r.first);
  for (const auto& r : picked) kinds.insert(r.first);
  double worst = 0;
  Item worst_item;
  for (const Item& k : kinds) {
    const double a = rolled.count(k) ? rolled.at(k) : 0, b = picked.count(k) ? picked.at(k) : 0;
    const double p = (a + b) / (all_rolled + all_picked);
    const double sigma = std::sqrt(p * (1 - p) * (1 / all_rolled + 1 / all_picked));
    const double off = sigma > 0 ? std::fabs(a / all_rolled - b / all_picked) / sigma : 0;
    if (off > worst) worst = off, worst_item = k;
  }
  std::printf("drops   %-42s %zu kinds of item; %.0f rolled for, %.0f picked; off by %.1f sigma at most (%s, "
              "quality %d%s%s, row %d)\n",
              what, kinds.size(), all_rolled, all_picked, worst, kNames[worst_item.cls], worst_item.quality,
              worst_item.ethereal ? ", ethereal" : "", worst_item.socketed ? ", socketed" : "", worst_item.row);
  CHECK(all_rolled > 1000 && all_picked > 1000);
  CHECK(worst < 5);
}

// One treasure class three ways: everything rolled and what the filter hides thrown away; the game's own walk
// with the filter asked before an item is made; and picks among what the filter shows.
void check(const char* what, uint32_t tc, int forced_quality) {
  dropodds::reset();
  const dropodds::Request r = request(tc, forced_quality);
  constexpr int kWalks = 150000;
  Tally rolled, walked, picked;
  double shown_a_walk = 0, kept_a_walk = 0;
  Rng rng(0xD1AB10u + tc * 131u + static_cast<uint32_t>(forced_quality));
  const std::function<bool(const uint8_t*)> taken = takes;
  for (int i = 0; i < kWalks; ++i)
    walk(rng, classes, tc, nullptr, taken, [&](const uint8_t* entry, const uint16_t* mods) {
      const Asked a = ask(rng, entry, mods, forced_quality);
      const Item it = generate(rng, a.cls, a.quality, a.named, a.flags, item_level);
      if (!wanted(it)) return;
      rolled[it] += 1;
      shown_a_walk += 1.0 / kWalks;
    });
  int hidden_all_the_same = 0, other_than_said = 0;
  for (int i = 0; i < kWalks; ++i)
    walk(rng, classes, tc, nullptr, taken, [&](const uint8_t* entry, const uint16_t* mods) {
      Asked a = ask(rng, entry, mods, forced_quality);
      dropodds::Ask asked;
      asked.item_class = a.cls;
      asked.quality = a.quality;
      asked.named = a.named;
      asked.item_level = item_level;
      asked.flags = a.flags;
      dropodds::Force force;
      const dropodds::Answer answer = dropodds::judge(r, asked, &force);
      if (answer == dropodds::Answer::kNot || answer == dropodds::Answer::kNotThisTime) return;
      if (answer == dropodds::Answer::kSo) {
        if (force.name) a.named = force.name;
        a.flags = (a.flags & ~force.clear) | force.set;
      }
      const Item it = generate(rng, a.cls, a.quality, a.named, a.flags, item_level);
      if (!wanted(it)) {
        ++hidden_all_the_same;
        return;
      }
      if (answer == dropodds::Answer::kSo &&
          (it.quality != force.quality || (force.ethereal >= 0 && it.ethereal != (force.ethereal == 1)) ||
           (force.sockets >= 0 && it.socketed != (force.sockets == 1)) || (force.row >= 0 && it.row != force.row)))
        ++other_than_said;
      walked[it] += 1;
      kept_a_walk += 1.0 / kWalks;
    });
  CHECK(hidden_all_the_same == 0);
  CHECK(other_than_said == 0);
  CHECK(std::fabs(shown_a_walk - kept_a_walk) < 0.02);
  dropodds::Plan plan;
  CHECK(plan.build(r));
  std::printf("plan    %-42s %d items lead to %d outcomes; a walk makes %.3f items, %.3f of them shown (%.3f rolled "
              "for, %.3f with the filter asked first)\n",
              what, plan.totals().leaves, plan.totals().outcomes, plan.totals().walk, plan.totals().shown,
              shown_a_walk, kept_a_walk);
  CHECK(std::fabs(plan.totals().shown - shown_a_walk) < 0.02);
  const uint32_t bits = take<uint32_t>(game, 1, 0, kUniqueBits);
  const int picks = static_cast<int>(kWalks * (shown_a_walk > 0.2 ? shown_a_walk : 0.2));
  hidden_all_the_same = other_than_said = 0;
  int unmade = 0;
  for (int i = 0; i < picks && !plan.empty(); ++i) {
    dropodds::Pick pick;
    if (!plan.pick(&pick)) {
      ++unmade;
      continue;
    }
    // The drop core with the pick's entry in a treasure class of its own, then the item creation hook.
    Asked a = ask(rng, pick.entry, pick.mods, pick.quality);
    if (pick.force.name) a.named = pick.force.name;
    a.flags = (a.flags & ~pick.force.clear) | pick.force.set;
    const Item it = generate(rng, a.cls, a.quality, a.named, a.flags, item_level);
    if (!wanted(it)) ++hidden_all_the_same;
    else if (pick.quality && (it.quality != pick.force.quality ||
                              (pick.force.ethereal >= 0 && it.ethereal != (pick.force.ethereal == 1)) ||
                              (pick.force.sockets >= 0 && it.socketed != (pick.force.sockets == 1)) ||
                              (pick.force.row >= 0 && it.row != pick.force.row)))
      ++other_than_said;
    else picked[it] += 1;
    // A unique that came is one the plan has no more, and so is a set item of a row the Chronicle missed (the drop
    // takes it out of the rows, as loot.cpp has the Chronicle do): said so, then taken back for the next pick.
    if (it.quality == 7 && it.row >= 0 && !(take<uint32_t>(uniques, kUniqueRow, it.row, 0x2C) & 2)) {
      came(static_cast<uint32_t>(it.row), true);
      plan.made(pick, true);
      put<uint32_t>(game, 1, 0, kUniqueBits, bits);
    }
    if (chronicle && it.quality == 5 && it.row >= 0 && missing_sets.has(static_cast<uint32_t>(it.row))) {
      missing_sets.set(static_cast<uint32_t>(it.row), false);
      plan.made(pick, true);
      missing_sets.set(static_cast<uint32_t>(it.row), true);
    }
    plan.made(pick, true);
  }
  CHECK(unmade == 0);
  CHECK(hidden_all_the_same == 0);
  CHECK(other_than_said == 0);
  compare(what, rolled, walked);
  compare("... picked", rolled, picked);
}

// What the filter shows of each class, by quality: a letter a quality (l n s m S r u; a capital one the quality
// whether ethereal or socketed or not, a small one only when it is neither, one with ' only when it is one).
uint16_t bits_of(const char* text) {
  uint16_t bits = 0;
  for (const char* p = text; *p; ++p) {
    int q = 0;
    switch (*p | 0x20) {
      case 'l': q = 1; break;
      case 'n': q = 2; break;
      case 's': q = 3; break;
      case 'm': q = 4; break;
      case 'e': q = 5; break;  // a set item
      case 'r': q = 6; break;
      case 'u': q = 7; break;
      default: continue;
    }
    const bool capital = !(*p & 0x20), only_with = p[1] == '\'';
    if (capital || !only_with) bits = static_cast<uint16_t>(bits | 1u << ((q - 1) * 2));
    if (capital || only_with) bits = static_cast<uint16_t>(bits | 1u << ((q - 1) * 2 + 1));
  }
  return bits;
}

void run() {
  static D2RL::PluginContext context{};
  static D2RL::DataTableService service{};
  service.serviceSize = D2RL::DataTableServiceSize;
  service.serviceVersion = D2RL::DataTableService::AbiVersion;
  service.getTable = get_table;
  dropodds::set_services(&context, &service);
  build();
  // A filter of every sort of answer: a quality shown whatever the item, only a plain one, only an ethereal or
  // socketed one; uniques with the rare they fail to, and without.
  shown[kSwordItem] = bits_of("U r n' s' E");
  shown[kPlate] = bits_of("R n m'");
  shown[kRingItem] = bits_of("U r");
  shown[kCharmItem] = bits_of("M");
  shown[kPotionItem] = 0;
  shown[kBlade] = bits_of("n' s u");
  shown[kRelic] = bits_of("U");
  shown[kKeyItem] = bits_of("N");
  g_magic_find = 250;
  check("a unique's drop", g_top, 0);
  check("plain items", g_plain, 0);
  check("... with a quality asked for", g_plain, 3);
  g_magic_find = 0;
  shown[kSwordItem] = bits_of("u' R l' s");
  shown[kPlate] = bits_of("u m e n s'");
  shown[kRingItem] = bits_of("M e");
  shown[kBlade] = bits_of("N S' U'");
  shown[kKeyItem] = bits_of("s l");
  check("another filter, no magic find", g_top, 0);
  check("... plain items", g_plain, 0);
  all_superior = all_ethereal = all_socketed = true;
  g_magic_find = 120;
  check("all superior, ethereal and socketed", g_top, 0);
  check("... plain items", g_plain, 0);
  shown[kSwordItem] = bits_of("U r n' s' E");
  shown[kPlate] = bits_of("R n m'");
  shown[kBlade] = bits_of("n' s u");
  check("... the first filter", g_plain, 0);
  all_superior = all_ethereal = all_socketed = false;
  // The Chronicle: the uniques and set items it misses drop whatever the filter says. Of the sword's uniques it
  // misses one that is picked among (0), one above the item's level (2), the one that comes by name only (4) and
  // the one whose condition holds (5); the ring's that has not come (7), the relic's (9) and the blade's that comes
  // any number of times (11); of the set items the sword's second (3), not the one the treasure class names (0).
  missing_uniques.clear();
  missing_sets.clear();
  for (const uint32_t row : {0u, 2u, 4u, 5u, 7u, 9u, 11u}) missing_uniques.set(row);
  missing_sets.set(3);
  chronicle = true;
  filter = false;
  g_magic_find = 300;
  check("the Chronicle alone", g_top, 0);
  check("... plain items", g_plain, 0);
  check("... every item asked for as a unique", g_plain, 7);
  check("... as a set item", g_plain, 5);
  filter = true;
  shown[kSwordItem] = bits_of("U r n' s' E");
  shown[kPlate] = bits_of("R n m'");
  shown[kRingItem] = bits_of("u' r");
  shown[kCharmItem] = bits_of("M");
  shown[kBlade] = bits_of("n' s u'");
  shown[kRelic] = 0;
  shown[kKeyItem] = bits_of("N");
  check("the filter or the Chronicle", g_top, 0);
  check("... plain items", g_plain, 0);
  check("... every item asked for as a unique", g_plain, 7);
  all_superior = all_ethereal = all_socketed = true;
  check("... all superior, ethereal and socketed", g_top, 0);
  all_superior = all_ethereal = all_socketed = false;
  // What the treasure class drops of it: the set items it misses once one has dropped, none.
  {
    dropodds::reset();
    filter = false;
    dropodds::Plan sets_only;
    missing_uniques.clear();
    CHECK(sets_only.build(request(g_top, 5)));
    CHECK(!sets_only.empty());
    dropodds::Pick one;
    CHECK(sets_only.pick(&one) && one.force.row == 3 && one.force.name == 4);
    missing_sets.set(3, false);
    sets_only.made(one, true);
    CHECK(sets_only.empty());
    filter = true;
  }
  // A Chronicle that misses nothing: with the filter off, a plan with nothing in it.
  missing_uniques.clear();
  missing_sets.clear();
  filter = false;
  dropodds::reset();
  dropodds::Plan complete;
  CHECK(complete.build(request(g_top, 0)));
  CHECK(complete.empty());
  filter = true;
  chronicle = false;
  // Nothing the filter shows: a plan with nothing in it.
  for (uint16_t& bits : shown) bits = 0;
  dropodds::reset();
  dropodds::Plan nothing;
  CHECK(nothing.build(request(g_plain, 0)));
  CHECK(nothing.empty());
  dropodds::Pick pick;
  CHECK(!nothing.pick(&pick));
}

// --- a treasure class as large as the game's ----------------------------------------------------------------------
//
// As many items, uniques and treasure classes as a unique monster's drop in Hell leads to, and a filter that shows
// what a grail filter does: what working it out costs.
double now_ms() {
  LARGE_INTEGER t{}, f{};
  QueryPerformanceCounter(&t);
  QueryPerformanceFrequency(&f);
  return 1000.0 * static_cast<double>(t.QuadPart) / static_cast<double>(f.QuadPart);
}

void large() {
  constexpr uint32_t kWeapons = 300, kArmors = 200, kOthers = 100, kAll = kWeapons + kArmors + kOthers;
  constexpr uint32_t kGoldAt = kAll - 1;
  shown.assign(kAll, 0);
  items.assign(static_cast<size_t>(kAll) * kItemsRow, 0);
  uniques.assign(static_cast<size_t>(400) * kUniqueRow, 0);
  sets.assign(static_cast<size_t>(130) * kSetRow, 0);
  game.assign(0x4000, 0);
  game[0x101] = 3;
  for (uint32_t i = 0; i < kAll; ++i) {
    const uint32_t its_code = 0x20000000u | (i + 1);
    const uint8_t level = static_cast<uint8_t>(1 + i * 7 % 87);
    if (i < kWeapons) item(i, its_code, kSword, level, 40, false, true, 6);
    else if (i < kWeapons + kArmors) item(i, its_code, kBodyArmor, level, 30, false, true, 4);
    else if (i == kGoldAt) item(i, its_code, kGold, 1, 0, true, false, 0);
    else item(i, its_code, i % 3 == 0 ? kRing : i % 3 == 1 ? kCharm : kPotion, level, 0, true, false, 0);
  }
  for (uint32_t u = 0; u < 400; ++u) unique(u, 0x20000000u | (u * 3 % kAll + 1), 1, 1 + u % 5, static_cast<int16_t>(u % 80));
  for (uint32_t u = 0; u < 130; ++u) set_item(u, 0x20000000u | (u * 4 % kAll + 1), 1, 1 + u % 3, static_cast<int16_t>(u % 70), 3);
  TestClasses& c = classes;
  c = TestClasses{};
  c.add(1, nullptr);
  // The items by their levels, three levels a treasure class, weapons and armor apart.
  std::vector<uint32_t> weapons, armors;
  for (int level = 3; level <= 87; level += 3) {
    const uint32_t w = c.add(1, nullptr), a = c.add(1, nullptr);
    for (uint32_t i = 0; i < kWeapons + kArmors; ++i) {
      const int its = take<uint8_t>(items, kItemsRow, i, 0x10D);
      if (its > level || its <= level - 3) continue;
      c.entry(i < kWeapons ? w : a, i, 1 + static_cast<int32_t>(i % 3));
    }
    weapons.push_back(w);
    armors.push_back(a);
  }
  // Equipment: the three highest of those, then the chain down to the lowest.
  uint32_t below = 0;
  for (size_t at = 2; at < weapons.size(); ++at) {
    const uint32_t e = c.add(1, nullptr);
    c.entry(e, weapons[at - 2], 2, dropodds::kEntryClass);
    c.entry(e, armors[at - 2], 1, dropodds::kEntryClass);
    c.entry(e, weapons[at - 1], 6, dropodds::kEntryClass);
    c.entry(e, armors[at - 1], 3, dropodds::kEntryClass);
    c.entry(e, weapons[at], 14, dropodds::kEntryClass);
    c.entry(e, armors[at], 7, dropodds::kEntryClass);
    if (below) c.entry(e, below, 1530, dropodds::kEntryClass);
    below = e;
  }
  const uint32_t good = c.add(1, nullptr);
  for (uint32_t i = kWeapons + kArmors; i < kGoldAt; ++i)
    if (i % 3 != 2) c.entry(good, i, 1 + static_cast<int32_t>(i % 4));
  const uint32_t potions = c.add(2, nullptr);
  for (uint32_t i = kWeapons + kArmors; i < kGoldAt; ++i)
    if (i % 3 == 2) c.entry(potions, i, 1 + static_cast<int32_t>(i % 4));
  const uint16_t mods[dropodds::kMods] = {1024, 800, 800, 800, 0, 0};
  const uint32_t item_tc = c.add(1, mods);
  c.entry(item_tc, below, 75, dropodds::kEntryClass);
  c.entry(item_tc, good, 6, dropodds::kEntryClass);
  const uint32_t top = c.add(-4, nullptr);
  c.entry(top, item_tc, 1, dropodds::kEntryClass);
  c.entry(top, potions, 2, dropodds::kEntryClass);
  const uint32_t ordinary = c.add(1, nullptr);
  c.entry(ordinary, kGoldAt, 21);
  c.entry(ordinary, below, 16, dropodds::kEntryClass);
  c.entry(ordinary, potions, 21, dropodds::kEntryClass);
  c.entry(ordinary, good, 2, dropodds::kEntryClass);
  c.finish();
  // A grail filter: the uniques and set items of 180 bases, five bases when ethereal or socketed.
  for (uint32_t i = 0; i < kAll; i += 3) shown[i] = bits_of("U");
  for (uint32_t i = 1; i < kAll; i += 25) shown[i] = static_cast<uint16_t>(shown[i] | bits_of("E"));
  for (uint32_t i = 2; i < 300; i += 60) shown[i] = bits_of("n' s'");
  item_level = 88;
  g_magic_find = 300;
  for (const uint32_t tc : {top, ordinary}) {
    dropodds::reset();
    const dropodds::Request r = request(tc, 0);
    dropodds::Plan plan;
    const double began = now_ms();
    CHECK(plan.build(r));
    const double first = now_ms() - began;
    double again = 1e9;
    for (int i = 0; i < 20; ++i) {
      const double from = now_ms();
      dropodds::Plan next;
      CHECK(next.build(r));
      again = std::min(again, now_ms() - from);
    }
    const double from = now_ms();
    int picked = 0;
    for (int i = 0; i < 1000; ++i) {
      dropodds::Pick pick;
      if (plan.pick(&pick)) ++picked;
    }
    const double picks = now_ms() - from;
    CHECK(picked == 1000);
    std::printf("large   %s: %d items lead to %d outcomes the filter shows; a walk makes %.2f items, %.4f of them "
                "shown; worked out in %.3f ms the first time, %.3f ms after; a pick %.4f ms\n",
                tc == top ? "a unique's drop" : "an ordinary monster's", plan.totals().leaves,
                plan.totals().outcomes, plan.totals().walk, plan.totals().shown, first, again, picks / 1000);
    CHECK(again < 5);
  }
}

}  // namespace world

int main() {
  // The six rows of 3.3's ItemRatio: by version (the expansion's are used), exceptional or elite, a class's own.
  const Ratio normal{400, 1, 6400, 100, 2, 3200, 160, 2, 5600, 34, 3, 192, 12, 8, 2, 2};
  const Ratio uber{160, 1, 6400, 100, 2, 3200, 160, 2, 5600, 34, 3, 192, 12, 8, 1, 1};
  const Ratio class_own{240, 3, 6400, 80, 3, 3200, 120, 3, 5600, 17, 6, 192, 9, 8, 2, 2};
  const uint16_t none[4] = {0, 0, 0, 0};
  const uint16_t champion[4] = {1024, 800, 800, 800};  // a unique's or champion's item pick: magic, rare, set, unique
  const uint16_t boss[4] = {1024, 972, 983, 983};
  Kind plain;
  plain.can_be_rare = true;
  Kind jewelry = plain;
  jewelry.always_magic = true;
  Kind charm;
  charm.always_magic = true;
  Kind potion;
  potion.normal_only = true;
  check_quality("a base of level 60 at 85, no magic find", normal, plain, 25, 0, none);
  check_quality("... 300 magic find", normal, plain, 25, 300, none);
  check_quality("... from a unique's item pick", normal, plain, 25, 300, champion);
  check_quality("an elite base from a boss, 500", uber, plain, 12, 500, boss);
  check_quality("a class's own, 8 magic find", class_own, plain, 40, 8, none);
  check_quality("a ring", normal, jewelry, 84, 150, champion);
  check_quality("a charm (never rare)", normal, charm, 60, 0, none);
  check_quality("a potion", normal, potion, 80, 300, none);
  check_quality("a base above the item's level", normal, plain, -20, 40, none);
  check_quality("magic find of -50", normal, plain, 30, -50, none);

  const auto always = [](const uint8_t*) { return true; };
  {
    // A unique monster's: its item, then two picks of two potions each; the item's treasure class has the mods.
    TestClasses c;
    c.add(1, nullptr);  // 0: none (the game has no treasure class 0)
    const uint32_t potions = c.add(2, nullptr);
    c.entry(potions, 600, 1);
    c.entry(potions, 601, 2);
    c.entry(potions, 602, 4);
    const uint16_t item_mods[dropodds::kMods] = {1024, 800, 800, 800, 0, 0};
    const uint32_t equip_a = c.add(1, nullptr);
    c.entry(equip_a, 10, 3);
    c.entry(equip_a, 11, 1);
    const uint32_t equip_b = c.add(1, nullptr);
    c.entry(equip_b, 20, 2);
    c.entry(equip_b, 21, 6);
    c.entry(equip_b, equip_a, 1530, dropodds::kEntryClass);
    const uint32_t item = c.add(1, item_mods);
    c.entry(item, equip_b, 75, dropodds::kEntryClass);
    c.entry(item, 300, 6);
    const uint32_t unique = c.add(-4, nullptr);
    c.entry(unique, item, 1, dropodds::kEntryClass);
    c.entry(unique, potions, 2, dropodds::kEntryClass);
    const uint32_t terror = c.add(-2, nullptr);
    c.entry(terror, unique, 1, dropodds::kEntryClass);
    c.finish();
    check_walk("a unique's, terrorized", c, terror, always, false);
    check_walk("a unique's item pick", c, item, always, false);
  }
  {
    // A boss's seven picks, a treasure class with mods of its own below one with others, and an entry the drop
    // core takes only for some (a Herald's charm).
    TestClasses c;
    c.add(1, nullptr);
    const uint16_t low[dropodds::kMods] = {0, 500, 500, 500, 0, 0};
    const uint16_t high[dropodds::kMods] = {1024, 983, 983, 983, 0, 100};
    const uint32_t charms = c.add(1, low);
    c.entry(charms, 900, 30, dropodds::kEntryUnique, 427);
    c.entry(charms, 900, 12, dropodds::kEntryUnique, 431);
    const uint32_t goods = c.add(3, low);
    c.entry(goods, 40, 5);
    c.entry(goods, 41, 9);
    const uint32_t boss_tc = c.add(7, high, dropodds::kTcHasConditional);
    c.entry(boss_tc, 50, 15);
    c.entry(boss_tc, charms, 4, dropodds::kEntryClass | dropodds::kEntryConditional);
    c.entry(boss_tc, goods, 11, dropodds::kEntryClass);
    c.entry(boss_tc, 51, 3, dropodds::kEntryConditional);
    const uint32_t picks_in_order = c.add(-5, nullptr, dropodds::kTcHasConditional);
    c.entry(picks_in_order, 60, 2);
    c.entry(picks_in_order, 61, 1, dropodds::kEntryConditional);
    c.entry(picks_in_order, boss_tc, 1, dropodds::kEntryClass);
    c.entry(picks_in_order, 62, 3);
    c.finish();
    check_walk("a boss's, every entry taken", c, boss_tc, always, true);
    check_walk("... the conditional ones not", c, boss_tc,
               [](const uint8_t* e) { return (e[dropodds::kEntryFlags] & dropodds::kEntryConditional) == 0; }, true);
    check_walk("picks in order over a condition", c, picks_in_order,
               [](const uint8_t* e) { return (e[dropodds::kEntryFlags] & dropodds::kEntryConditional) == 0; }, true);
    check_walk("... with it taken", c, picks_in_order, always, true);
  }
  // What the generator is asked and what comes of it.
  {
    Kind jewel;
    jewel.always_magic = jewel.can_be_rare = true;
    Kind charm_kind;
    charm_kind.always_magic = true;
    Kind weapon;
    weapon.can_be_rare = true;
    CHECK(dropodds::quality_asked(jewel, 2, false) == 4);
    CHECK(dropodds::quality_asked(jewel, 6, false) == 6);
    CHECK(dropodds::quality_asked(charm_kind, 6, false) == 4);
    CHECK(dropodds::quality_asked(weapon, 1, true) == 3);
    CHECK(dropodds::quality_asked(weapon, 2, false) == 2);
    CHECK(dropodds::quality_asked(potion, 7, true) == 2);
    CHECK(dropodds::quality_made(weapon, 7, true, false) == 6);
    CHECK(dropodds::quality_made(charm_kind, 7, false, false) == 4);
    CHECK(dropodds::quality_made(weapon, 5, true, false) == 4);
    CHECK(dropodds::quality_made(weapon, 3, false, true) == 2);
    CHECK(dropodds::quality_made(weapon, 1, true, true) == 1);
    const dropodds::Extras x = dropodds::extras_odds(0.05, 0.33);
    CHECK(std::fabs(x.of[0] + x.of[1] + x.of[2] + x.of[3] - 1) < 1e-12);
    CHECK(std::fabs(x.of[0] - 0.95 * 0.67) < 1e-12 && std::fabs(x.of[3] - 0.05 * 0.33) < 1e-12);
  }
  world::run();
  world::large();
  if (g_failures) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("test_dropodds: ok\n");
  return 0;
}
