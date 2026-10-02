#include "itemlevel.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <iterator>

#include "cheats.h"
#include "game.h"
#include "log.h"
#include "mem.h"

namespace d2rcc::itemlevel {
namespace {

enum Kind : uint8_t { kHidden = 0, kWeapon, kArmor, kJewelry, kKinds };

// The compiled Items row (Weapons, Armor and Misc in one table) and ItemTypes row: the fields read here.
constexpr uint32_t kItemsRowSize = 0x1C0;
constexpr uintptr_t kItemsCode = 0x80;        // four characters, "hax " and so on
constexpr uintptr_t kItemsShowLevel = 0x10E;  // the ShowLevel column: the game adds the level itself
constexpr uintptr_t kItemsType = 0x12E;       // int16: its ItemTypes row
constexpr uintptr_t kItemsQuest = 0x13A;
constexpr uintptr_t kTypesCode = 0x00;
constexpr uintptr_t kTypesEquiv1 = 0x04;  // uint16: the types it counts as (0 for none: the blank first row)
constexpr uintptr_t kTypesEquiv2 = 0x06;
constexpr uint32_t kMaxRows = 4096;
constexpr uint32_t kMaxTypes = 1024;
constexpr int kBanks = 4;  // by the unit's data-table bank, 1 classic .. 3 rotw
constexpr int kMaxEquivDepth = 10;

const D2RL::PluginContext* g_ctx = nullptr;
const D2RL::DataTableService* g_tables = nullptr;
const D2RL::LocalizationService* g_strings = nullptr;
uint8_t g_kind[kBanks][kMaxRows];
volatile LONG g_rows[kBanks] = {};  // rows classified per bank, 0 until known
volatile LONG g_shown = 0;
volatile LONG g_shown_logged = 0;
volatile LONG g_row_warned = 0;
volatile LONG g_types_warned = 0;
// What the game puts between the name and the level: its string "space" (a space in every language but
// Japanese, where it is empty). Read on the game thread before a bank is published, then only read.
char g_space[16] = " ";
bool g_space_read = false;

constexpr uint32_t fourcc(const char (&s)[5]) { return D2RL::DataTables::MakeFourCC(s[0], s[1], s[2], s[3]); }

// The misc items that show their level, by their own item types (the 3.3 colossal jewel and sunder charm types are
// rotw only).
constexpr uint32_t kJewelryTypes[] = {fourcc("amul"), fourcc("ring"), fourcc("scha"), fourcc("mcha"),
                                      fourcc("lcha"), fourcc("csch"), fourcc("jewl"), fourcc("cjwl")};

// The ItemTypes table of one bank, as the loader shows it.
struct Types {
  uintptr_t rows = 0;
  uint32_t size = 0;
  uint32_t count = 0;
};

// Whether ItemTypes row `type` is `target` or counts as it through its Equiv1/Equiv2 chains (how the
// game's own item-type test reads the table: a javelin counts as a weapon, a circlet as armor).
bool counts_as(const Types& t, uint32_t type, uint32_t target, int depth = 0) {
  if (type == 0 || type >= t.count || depth > kMaxEquivDepth) return false;
  if (type == target) return true;
  const uintptr_t row = t.rows + static_cast<uintptr_t>(type) * t.size;
  uint16_t e1 = 0, e2 = 0;
  if (!mem::read_safe(row + kTypesEquiv1, &e1) || !mem::read_safe(row + kTypesEquiv2, &e2)) return false;
  return counts_as(t, e1, target, depth + 1) || counts_as(t, e2, target, depth + 1);
}

int32_t type_row(D2RL::DataTables::Bank bank, uint32_t code) {
  using namespace D2RL::DataTables;
  RowView row{};
  row.structSize = RowViewSize;
  if (!g_tables->findRowByCode ||
      g_tables->findRowByCode(g_ctx, bank, TableId::ItemTypes, code, &row) != Result::Success)
    return -1;
  return static_cast<int32_t>(row.rowIndex);
}

void read_space() {
  if (g_space_read || !g_strings || !g_strings->getStringByKey) return;
  g_space_read = true;
  char text[sizeof(g_space)] = {};
  uint32_t need = 0;
  if (g_strings->getStringByKey(g_ctx, "d2r:space", text, sizeof(text), &need) == D2RL::Localization::Result::Success)
    std::memcpy(g_space, text, sizeof(g_space));
  else
    logf("item level: the game's string \"space\" not found - a space goes between the name and the level");
}

}  // namespace

void set_services(const D2RL::PluginContext* ctx, const D2RL::DataTableService* tables,
                  const D2RL::LocalizationService* strings) {
  g_ctx = ctx;
  g_tables = tables;
  g_strings = strings;
}

bool has_tables() { return g_tables && g_tables->getTable; }

void collect() {
  using namespace D2RL::DataTables;
  if (!g_ctx || !has_tables()) return;
  for (int b = 1; b < kBanks; ++b) {
    if (g_rows[b]) continue;
    const Bank bank = static_cast<Bank>(b);
    TableView items{};
    items.structSize = TableViewSize;
    TableView types{};
    types.structSize = TableViewSize;
    if (g_tables->getTable(g_ctx, bank, TableId::Items, &items) != Result::Success || !items.rows || !items.rowCount ||
        g_tables->getTable(g_ctx, bank, TableId::ItemTypes, &types) != Result::Success || !types.rows ||
        !types.rowCount)
      continue;
    if (items.rowSize != kItemsRowSize) {
      if (log_once(&g_row_warned, LogLevel::kWarning))
        log_warn("item level: the Items rows are %u bytes, not %u - the item level stays off", items.rowSize,
                 kItemsRowSize);
      continue;
    }
    const Types t{reinterpret_cast<uintptr_t>(types.rows), types.rowSize,
                  types.rowCount < kMaxTypes ? types.rowCount : kMaxTypes};
    const int32_t weapon = type_row(bank, fourcc("weap"));
    const int32_t armor = type_row(bank, fourcc("armo"));
    const int32_t thrown_potion = type_row(bank, fourcc("tpot"));
    uint32_t weapon_code = 0;
    if (weapon <= 0 || armor <= 0 || t.size < kTypesEquiv2 + 2 ||
        !mem::read_safe(t.rows + static_cast<uintptr_t>(weapon) * t.size + kTypesCode, &weapon_code) ||
        weapon_code != fourcc("weap")) {
      if (log_once(&g_types_warned, LogLevel::kWarning))
        log_warn("item level: bank %d ItemTypes: the weapon and armor types not found as expected - the item level "
                 "stays off",
                 b);
      continue;
    }
    // What each item type shows as: weapons and armor through the type chains (throwing potions count as weapons
    // there and are left out), the misc items by their own types.
    int32_t jewelry[std::size(kJewelryTypes)];
    for (size_t j = 0; j < std::size(kJewelryTypes); ++j) jewelry[j] = type_row(bank, kJewelryTypes[j]);
    uint8_t type_kind[kMaxTypes] = {};
    for (uint32_t ty = 1; ty < t.count; ++ty) {
      Kind k = kHidden;
      if (static_cast<int32_t>(ty) == thrown_potion) k = kHidden;
      else if (counts_as(t, ty, static_cast<uint32_t>(weapon))) k = kWeapon;
      else if (counts_as(t, ty, static_cast<uint32_t>(armor))) k = kArmor;
      else
        for (int32_t j : jewelry)
          if (j > 0 && static_cast<int32_t>(ty) == j) k = kJewelry;
      type_kind[ty] = k;
    }
    read_space();
    const uint32_t n = items.rowCount < kMaxRows ? items.rowCount : kMaxRows;
    const uintptr_t rows = reinterpret_cast<uintptr_t>(items.rows);
    int counts[kKinds] = {};
    int by_data = 0;
    for (uint32_t i = 0; i < n; ++i) {
      const uintptr_t row = rows + static_cast<uintptr_t>(i) * items.rowSize;
      uint32_t code = 0;
      int16_t type = -1;
      uint8_t shows = 0;
      uint8_t quest = 1;
      Kind k = kHidden;
      if (mem::read_safe(row + kItemsCode, &code) && mem::read_safe(row + kItemsType, &type) &&
          mem::read_safe(row + kItemsShowLevel, &shows) && mem::read_safe(row + kItemsQuest, &quest) && code &&
          code != fourcc("    ") && !quest && type > 0 && static_cast<uint32_t>(type) < t.count) {
        k = static_cast<Kind>(type_kind[type]);
        if (k != kHidden && shows) {
          ++by_data;  // the game adds this one's level itself
          k = kHidden;
        }
      }
      g_kind[b][i] = k;
      ++counts[k];
    }
    InterlockedExchange(&g_rows[b], static_cast<LONG>(n));
    logf("item level: bank %d Items: %u rows; %d weapons, %d armor pieces and %d rings, amulets, charms and jewels "
         "get their level (%d already shown by the game's data)",
         b, n, counts[kWeapon], counts[kArmor], counts[kJewelry], by_data);
  }
}

bool ready(uint8_t bank) { return bank >= 1 && bank < kBanks && g_rows[bank] != 0; }

void append(Unit* item, char* name, size_t cap) {
  if (!name || cap < 2 || !cheats::enabled(cheats::kItemLevel) || cheats::why_not(cheats::kItemLevel)) return;
  if (game::unit_type(item) != game::kItem) return;
  const uint8_t bank = game::unit_table_bank(item);
  if (!ready(bank)) return;
  const uint32_t row = game::unit_class(item);
  if (row >= static_cast<uint32_t>(g_rows[bank]) || g_kind[bank][row] == kHidden) return;
  int32_t level = game::item_level(item);
  if (level < 1) level = 1;  // as the game's own level getter answers
  const size_t len = strnlen(name, cap);
  if (len + 1 >= cap) return;
  std::snprintf(name + len, cap - len, "%s(%d)", g_space, level);
  InterlockedIncrement(&g_shown);
  if (log_once(&g_shown_logged)) logf("item level: shown after an item's name (level %d)", level);
}

unsigned names_shown() { return static_cast<unsigned>(g_shown); }

}  // namespace d2rcc::itemlevel
