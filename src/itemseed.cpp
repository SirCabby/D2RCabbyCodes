#include "itemseed.h"

#include <windows.h>

#include "cheats.h"
#include "game.h"
#include "log.h"
#include "mem.h"
#include "overlay.h"

namespace d2rcc::itemseed {
namespace {

const D2RL::PluginContext* g_ctx = nullptr;
const D2RL::DataTableService* g_tables = nullptr;
volatile LONG g_chosen = 0;
volatile LONG g_differed = 0;
volatile LONG g_longest_us = 0;
volatile LONG g_off = 0;
volatile LONG g_logged = 0;
volatile LONG g_differ_logged = 0;
volatile LONG g_row_warned = 0;
volatile LONG g_seed_warned = 0;

// The item creation request (200 bytes) and the game record.
constexpr uintptr_t kRequestClass = 0x20;     // the Items row
constexpr uintptr_t kRequestRestore = 0x3C;   // nonzero: an existing item is made again, from its own seeds
constexpr uintptr_t kRequestUnitSeed = 0x58;  // the two seeds, read when the routine is told to
constexpr uintptr_t kRequestItemSeed = 0x5C;
constexpr uintptr_t kGameBank = 0x106;        // the data tables the game's items come from
// The compiled Items row (Weapons, Armor and Misc in one table) and ItemTypes row.
constexpr uint32_t kItemsRowSize = 0x1C0;
constexpr uintptr_t kItemsMinAc = 0xD4;
constexpr uintptr_t kItemsMaxAc = 0xD8;
constexpr uintptr_t kItemsDurability = 0x121;  // u8
constexpr uintptr_t kItemsType = 0x12E;        // int16: its ItemTypes row, and a second one after it
constexpr uintptr_t kItemsType2 = 0x130;
constexpr uintptr_t kTypesEquiv1 = 0x04;  // u16: the types it counts as (0 none)
constexpr uintptr_t kTypesEquiv2 = 0x06;
constexpr int kStatArmor = 31;
// One seed in (maxac - minac + 1) rolls the top, and no armor's range is near this wide.
constexpr uint32_t kTries = 1u << 16;
// An armor that comes out otherwise this often: item init is not what was read, and the choice stops.
constexpr LONG kDifferLimit = 3;

bool on(cheats::Kind k) { return cheats::enabled(k) && !cheats::why_not(k); }

int64_t now_us() {
  static LARGE_INTEGER frequency = {};
  if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
  LARGE_INTEGER t{};
  QueryPerformanceCounter(&t);
  return frequency.QuadPart ? static_cast<int64_t>(t.QuadPart / frequency.QuadPart) * 1000000 +
                                  static_cast<int64_t>(t.QuadPart % frequency.QuadPart) * 1000000 / frequency.QuadPart
                            : 0;
}

uint32_t random32() {
  thread_local uint64_t s = 0;
  if (!s) {
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    s = (static_cast<uint64_t>(t.QuadPart) ^ 0x9E3779B97F4A7C15ull) * (GetCurrentThreadId() | 1u);
    if (!s) s = 1;
  }
  s ^= s << 13;
  s ^= s >> 7;
  s ^= s << 17;
  return static_cast<uint32_t>(s >> 16);
}

// An ItemTypes row's Equiv1 and Equiv2, through the loader.
bool type_equivs(D2RL::DataTables::Bank bank, uint32_t type, uint16_t* e1, uint16_t* e2) {
  using namespace D2RL::DataTables;
  RowView view{};
  view.structSize = RowViewSize;
  if (g_tables->getRow(g_ctx, bank, TableId::ItemTypes, type, &view) != Result::Success || !view.row ||
      view.rowSize < kTypesEquiv2 + sizeof(uint16_t))
    return false;
  const uintptr_t row = reinterpret_cast<uintptr_t>(view.row);
  return mem::read_safe(row + kTypesEquiv1, e1) && mem::read_safe(row + kTypesEquiv2, e2);
}

// A request for a new armor whose base defense has a range: its class and its base. The loader's tables answer
// on the game's thread only, which is where items are made.
bool fresh_armor(void* game, const uint8_t* request, uint32_t* item_class, Armor* base) {
  using namespace D2RL::DataTables;
  if (!game || !request || !has_tables()) return false;
  const uintptr_t at = reinterpret_cast<uintptr_t>(request);
  int32_t restore = 1;
  uint8_t bank = 0;
  if (!mem::read_safe(at + kRequestRestore, &restore) || restore != 0 ||
      !mem::read_safe(at + kRequestClass, item_class) ||
      !mem::read_safe(reinterpret_cast<uintptr_t>(game) + kGameBank, &bank) || bank < 1 || bank > 3)
    return false;
  RowView view{};
  view.structSize = RowViewSize;
  if (g_tables->findRowById(g_ctx, static_cast<Bank>(bank), TableId::Items, *item_class, &view) != Result::Success ||
      !view.row)
    return false;
  if (view.rowSize != kItemsRowSize) {
    if (log_once(&g_row_warned, LogLevel::kWarning))
      log_warn("unit seed: the Items rows are %u bytes, not %u - armor keeps the base defense the game rolls",
               view.rowSize, kItemsRowSize);
    return false;
  }
  const uintptr_t row = reinterpret_cast<uintptr_t>(view.row);
  int16_t type = 0, type2 = 0;
  uint8_t durability = 0;
  if (!mem::read_safe(row + kItemsType, &type) || !mem::read_safe(row + kItemsType2, &type2) ||
      !mem::read_safe(row + kItemsDurability, &durability) || !mem::read_safe(row + kItemsMinAc, &base->min_ac) ||
      !mem::read_safe(row + kItemsMaxAc, &base->max_ac))
    return false;
  base->durability = durability;
  if (!base->ranged() || base->min_ac < 0) return false;
  const auto equivs = [bank](uint32_t t, uint16_t* e1, uint16_t* e2) {
    return type_equivs(static_cast<Bank>(bank), t, e1, e2);
  };
  return (type > 0 && counts_as_armor(static_cast<uint32_t>(type), equivs)) ||
         (type2 > 0 && counts_as_armor(static_cast<uint32_t>(type2), equivs));
}

// The item the game made from the seeds: it carries the unit seed, and its base defense is what that seed rolls.
// (Its own seed may be another by now: a generator step that fails starts the item's stream again.)
void check(Unit* item, uint32_t item_class, const Armor& base, uint32_t unit_seed, uint32_t tried,
           int64_t took_us) {
  InterlockedIncrement(&g_chosen);
  const LONG took = took_us < 0 ? 0 : took_us > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<LONG>(took_us);
  for (LONG seen = g_longest_us; took > seen;) {
    const LONG was = InterlockedCompareExchange(&g_longest_us, took, seen);
    if (was == seen) break;
    seen = was;
  }
  game::ItemSeeds seeds;
  const bool read = game::item_seeds(item, &seeds);
  if (read && seeds.unit_seed != unit_seed && log_once(&g_seed_warned, LogLevel::kWarning))
    log_warn("unit seed: item creation did not take the unit seed it was given (%08X; the item has %08X)", unit_seed,
             seeds.unit_seed);
  if (!game::has_base_stat()) return;
  const int32_t quality = game::item_quality(item);
  const bool ethereal = (game::item_flags(item) & game::kItemEthereal) != 0;
  const int32_t has = game::get_base_stat(item, kStatArmor);
  const bool same = read && seeds.unit_seed == unit_seed && top_defense_kept(base, quality, ethereal, has);
  if (same) {
    if (overlay::settings().trace || log_first(&g_logged, 4))
      logf("unit seed: armor class %u (quality %d%s) made from unit seed %08X, found in %u tries (%lld us): base "
           "defense %d, the top of %d..%d as that seed rolls it",
           item_class, quality, ethereal ? ", ethereal" : "", unit_seed, tried, static_cast<long long>(took_us), has,
           base.min_ac, base.max_ac);
    return;
  }
  const LONG differed = InterlockedIncrement(&g_differed);
  if (overlay::settings().trace || log_first(&g_differ_logged, 8, LogLevel::kWarning))
    log_warn("unit seed: armor class %u (quality %d%s) made from unit seed %08X has base defense %d, not the top of "
             "%d..%d that seed rolls",
             item_class, quality, ethereal ? ", ethereal" : "", unit_seed, has, base.min_ac, base.max_ac);
  if (differed >= kDifferLimit && !InterlockedExchange(&g_off, 1))
    log_warn("unit seed: %ld armor pieces came out otherwise - new armor keeps the game's own seeds from now on",
             static_cast<long>(differed));
}

}  // namespace

void set_services(const D2RL::PluginContext* ctx, const D2RL::DataTableService* tables) {
  g_ctx = ctx;
  g_tables = tables;
}

bool has_tables() { return g_ctx && g_tables && g_tables->findRowById && g_tables->getRow; }

void game_left() {
  InterlockedExchange(&g_chosen, 0);
  InterlockedExchange(&g_differed, 0);
  InterlockedExchange(&g_longest_us, 0);
}

Unit* create(CreateItemFn original, void* game, uint8_t* request, int32_t use_seeds) {
  if (!original) return nullptr;
  // Seeds the caller gives are the caller's (an item made again, a load). Else only the unit seed of an armor has
  // anything to say about what a switch wants: its base defense, under perfect rolls.
  Armor base;
  uint32_t item_class = 0;
  if (use_seeds || g_off || !on(cheats::kPerfectRolls) || !fresh_armor(game, request, &item_class, &base))
    return original(game, request, use_seeds);
  const int64_t began = now_us();
  uint32_t unit_seed = 0, tried = 0;
  if (!top_defense_seed(base, random32(), kTries, &unit_seed, &tried)) return original(game, request, use_seeds);
  const uint32_t item_seed = item_seed_after(unit_seed, random32());
  const int64_t took = now_us() - began;
  // The request's own seeds are put back once the item is made: the caller's record is its own.
  const uintptr_t at = reinterpret_cast<uintptr_t>(request);
  uint32_t old_unit = 0, old_item = 0;
  if (!mem::read_safe(at + kRequestUnitSeed, &old_unit) || !mem::read_safe(at + kRequestItemSeed, &old_item))
    return original(game, request, use_seeds);
  if (!mem::store(at + kRequestUnitSeed, unit_seed) || !mem::store(at + kRequestItemSeed, item_seed)) {
    mem::store(at + kRequestUnitSeed, old_unit);
    return original(game, request, use_seeds);
  }
  Unit* item = original(game, request, 1);
  mem::store(at + kRequestUnitSeed, old_unit);
  mem::store(at + kRequestItemSeed, old_item);
  if (item) check(item, item_class, base, unit_seed, tried, took);
  return item;
}

Stats stats() {
  Stats s;
  s.chosen = static_cast<unsigned>(g_chosen);
  s.differed = static_cast<unsigned>(g_differed);
  s.longest_us = static_cast<unsigned>(g_longest_us);
  s.off = g_off != 0;
  return s;
}

}  // namespace d2rcc::itemseed
