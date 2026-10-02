#include "consumables.h"

#include <windows.h>

#include "cheats.h"
#include "game.h"
#include "log.h"
#include "mem.h"

namespace d2rcc::consumables {
namespace {

enum Kind : uint8_t { kNone = 0, kPortalScroll, kPortalTome, kIdentifyScroll, kIdentifyTome, kPotion, kKinds };
const char* const kKindNames[kKinds] = {"-", "town portal scroll", "town portal tome", "identify scroll",
                                        "identify tome", "potion"};

// The compiled Items row (Weapons, Armor and Misc in one table): the fields read here.
constexpr uint32_t kItemsRowSize = 0x1C0;
constexpr uintptr_t kItemsCode = 0x80;   // four characters, "tsc " and so on
constexpr uintptr_t kItemsSpell = 0x94;  // pSpell: the use effect (3 4 5 6 9 are the potions')
constexpr uintptr_t kItemsType = 0x12E;  // int16: its ItemTypes row
constexpr uintptr_t kItemsQuest = 0x13A;
constexpr uint32_t kMaxRows = 4096;
constexpr int kBanks = 4;  // by the unit's data-table bank, 1 classic .. 3 rotw

const D2RL::PluginContext* g_ctx = nullptr;
const D2RL::DataTableService* g_tables = nullptr;
uint8_t g_kind[kBanks][kMaxRows];
volatile LONG g_rows[kBanks] = {};  // rows classified per bank, 0 until known
volatile LONG g_tome_kept = 0;
volatile LONG g_keys_kept = 0;
volatile LONG g_key_logged = 0;
volatile LONG g_logged[kKinds] = {};
volatile LONG g_row_warned = 0;

constexpr uint32_t fourcc(const char (&s)[5]) { return D2RL::DataTables::MakeFourCC(s[0], s[1], s[2], s[3]); }

bool on(cheats::Kind k) { return cheats::enabled(k) && !cheats::why_not(k); }

Kind kind_of(Unit* item) {
  if (game::unit_type(item) != game::kItem) return kNone;
  const uint8_t bank = game::unit_table_bank(item);
  if (bank < 1 || bank >= kBanks) return kNone;
  const uint32_t row = game::unit_class(item);
  return row < static_cast<uint32_t>(g_rows[bank]) ? static_cast<Kind>(g_kind[bank][row]) : kNone;
}

void note_once(Kind k, const char* what) {
  if (log_once(&g_logged[k])) logf("consumables: a %s %s", kKindNames[k], what);
}

}  // namespace

void set_services(const D2RL::PluginContext* ctx, const D2RL::DataTableService* tables) {
  g_ctx = ctx;
  g_tables = tables;
}

bool has_tables() { return g_tables && g_tables->getTable; }

void collect() {
  using namespace D2RL::DataTables;
  if (!g_ctx || !has_tables()) return;
  static const uint32_t kPotionTypes[] = {fourcc("hpot"), fourcc("mpot"), fourcc("rpot"),
                                          fourcc("spot"), fourcc("apot"), fourcc("wpot")};
  for (int b = 1; b < kBanks; ++b) {
    if (g_rows[b]) continue;
    TableView view{};
    view.structSize = TableViewSize;
    if (g_tables->getTable(g_ctx, static_cast<Bank>(b), TableId::Items, &view) != Result::Success || !view.rows ||
        !view.rowCount)
      continue;
    if (view.rowSize != kItemsRowSize) {
      if (log_once(&g_row_warned, LogLevel::kWarning))
        log_warn("consumables: the Items rows are %u bytes, not %u - infinite scrolls, tomes and potions stay off",
                 view.rowSize, kItemsRowSize);
      continue;
    }
    // The potion types by their codes; without them the use effect tells (the potions' pSpell).
    int32_t potion_types[6];
    int types = 0;
    for (uint32_t code : kPotionTypes) {
      RowView row{};
      row.structSize = RowViewSize;
      if (g_tables->findRowByCode &&
          g_tables->findRowByCode(g_ctx, static_cast<Bank>(b), TableId::ItemTypes, code, &row) == Result::Success)
        potion_types[types++] = static_cast<int32_t>(row.rowIndex);
    }
    const uint32_t n = view.rowCount < kMaxRows ? view.rowCount : kMaxRows;
    const uintptr_t rows = reinterpret_cast<uintptr_t>(view.rows);
    int counts[kKinds] = {};
    for (uint32_t i = 0; i < n; ++i) {
      const uintptr_t row = rows + static_cast<uintptr_t>(i) * view.rowSize;
      uint32_t code = 0;
      int32_t spell = 0;
      int16_t type = -1;
      uint8_t quest = 1;
      Kind k = kNone;
      if (mem::read_safe(row + kItemsCode, &code) && mem::read_safe(row + kItemsSpell, &spell) &&
          mem::read_safe(row + kItemsType, &type) && mem::read_safe(row + kItemsQuest, &quest)) {
        if (code == fourcc("tsc ")) k = kPortalScroll;
        else if (code == fourcc("tbk ")) k = kPortalTome;
        else if (code == fourcc("isc ")) k = kIdentifyScroll;
        else if (code == fourcc("ibk ")) k = kIdentifyTome;
        else if (!quest) {
          bool potion = false;
          for (int t = 0; t < types && !potion; ++t) potion = type == potion_types[t];
          if (!types) potion = spell == 3 || spell == 4 || spell == 5 || spell == 6 || spell == 9;
          if (potion) k = kPotion;
        }
      }
      g_kind[b][i] = k;
      ++counts[k];
    }
    InterlockedExchange(&g_rows[b], static_cast<LONG>(n));
    logf("consumables: bank %d Items: %u rows; %d town portal and %d identify scrolls/tomes, %d potions (%s)", b, n,
         counts[kPortalScroll] + counts[kPortalTome], counts[kIdentifyScroll] + counts[kIdentifyTome], counts[kPotion],
         types ? "by their item types" : "by their use effect");
  }
}

bool ready(uint8_t bank) { return bank >= 1 && bank < kBanks && g_rows[bank] != 0; }

bool keep_after_use(Unit* item) {
  const Kind k = kind_of(item);
  const bool keep = (k == kPortalScroll && on(cheats::kInfiniteTownPortal)) ||
                    (k == kIdentifyScroll && on(cheats::kInfiniteIdentify)) ||
                    (k == kPotion && on(cheats::kInfinitePotions));
  if (keep) note_once(k, "stays after use");
  return keep;
}

bool keep_tome_charge(Unit* player, Unit* item) {
  if (!game::is_local_player(player)) return false;
  const Kind k = kind_of(item);
  const bool keep = (k == kPortalTome && on(cheats::kInfiniteTownPortal)) ||
                    (k == kIdentifyTome && on(cheats::kInfiniteIdentify));
  if (keep) {
    InterlockedIncrement(&g_tome_kept);
    note_once(k, "keeps its charge");
  }
  return keep;
}

bool keep_key(Unit* player) {
  if (!on(cheats::kInfiniteKeys) || !game::is_local_player(player) || !game::carries_key(player)) return false;
  InterlockedIncrement(&g_keys_kept);
  if (log_once(&g_key_logged)) logf("consumables: a key opened a lock and stays");
  return true;
}

unsigned tome_charges_kept() { return static_cast<unsigned>(g_tome_kept); }
unsigned keys_kept() { return static_cast<unsigned>(g_keys_kept); }

}  // namespace d2rcc::consumables
