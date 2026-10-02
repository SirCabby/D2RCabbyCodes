#include "dropodds.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>

#include "game.h"
#include "log.h"
#include "mem.h"
#include "sites.h"

namespace d2rcc::dropodds {
namespace {

const D2RL::PluginContext* g_ctx = nullptr;
const D2RL::DataTableService* g_tables = nullptr;
volatile LONG g_size_warned = 0;
volatile LONG g_forget = 0;  // a game was left: what the game thread keeps of the tables is read again

// The compiled rows read here (the game's own layouts; the loader's compiled files have the same).
constexpr uint32_t kItemsRowSize = 0x1C0;
constexpr uintptr_t kItemsCode = 0x80;  // and the codes of its exceptional and elite versions
constexpr uintptr_t kItemsUberCode = 0x88;
constexpr uintptr_t kItemsUltraCode = 0x8C;
constexpr uintptr_t kItemsLevel = 0x10D;
constexpr uintptr_t kItemsDurability = 0x121;
constexpr uintptr_t kItemsNoDurability = 0x122;
constexpr uintptr_t kItemsType = 0x12E;  // int16, and a second one after it
constexpr uintptr_t kItemsType2 = 0x130;
constexpr uintptr_t kItemsUnique = 0x139;
constexpr uintptr_t kItemsQuest = 0x13A;
constexpr uintptr_t kItemsStackable = 0x142;
constexpr uintptr_t kItemsHasInv = 0x147;
constexpr uintptr_t kItemsGemSockets = 0x148;
constexpr int16_t kTypeGold = 4;
constexpr int16_t kTypeNoUber = 0x26;  // the one type the game's exceptional-or-elite test leaves out
constexpr uint32_t kTypeWeapon = 0x2D, kTypeArmor = 0x32;
constexpr uint32_t kTypesRowSize = 0xE8;
constexpr uintptr_t kTypesEquiv1 = 0x04;  // u16
constexpr uintptr_t kTypesEquiv2 = 0x06;
constexpr uintptr_t kTypesMagic = 0x14;
constexpr uintptr_t kTypesRare = 0x15;
constexpr uintptr_t kTypesNormal = 0x16;
constexpr uintptr_t kTypesSockets = 0x18;       // u8[3] by item level
constexpr uintptr_t kTypesSocketLevels = 0x1B;  // u8[2]: the two thresholds
constexpr uintptr_t kTypesClass = 0x20;         // below 8: a class's own
constexpr uint32_t kRatioRowSize = 0x44;
constexpr uintptr_t kRatioVersion = 0x40;  // u16
constexpr uintptr_t kRatioUber = 0x42;
constexpr uintptr_t kRatioClass = 0x43;
constexpr uint32_t kUniqueRowSize = 0x15C;
constexpr uintptr_t kUniqueVersion = 0x24, kUniqueCode = 0x28, kUniqueFlags = 0x2C, kUniqueRarity = 0x34;
constexpr uintptr_t kUniqueLevel = 0x38, kUniqueLadder = 0x8C, kUniqueCalc = 0x90;
constexpr uint32_t kSetRowSize = 0x1CC;
constexpr uintptr_t kSetVersion = 0x22, kSetCode = 0x28, kSetFlags = 0x2C, kSetSet = 0x30, kSetLevel = 0x34;
constexpr uintptr_t kSetRarity = 0x38, kSetLadder = 0x8A, kSetCalc = 0x90;
constexpr int16_t kSetByRequest = 0x1D;  // a set whose items come only when the request says so
constexpr uint32_t kRowSpawnable = 1, kRowDisabled = 4;
// The game.
constexpr uintptr_t kGameType = 0x101;    // u8
constexpr uintptr_t kGameLadder = 0x108;  // int32
constexpr int kStatMagicFind = 80;
constexpr uint32_t kMaxEntries = 4096;
constexpr int kMaxDepth = 60;  // the drop core's own stack is 64 treasure classes deep
constexpr uint32_t kMaxClasses = 4096;

struct View {
  uintptr_t rows = 0;
  uint32_t count = 0;
  uint64_t revision = 0;  // the table load it is of
};

bool table(uint8_t bank, D2RL::DataTables::TableId id, uint32_t row_size, const char* name, View* out) {
  *out = View{};
  if (!g_ctx || !g_tables || !g_tables->getTable || bank < 1 || bank > 3) return false;
  D2RL::DataTables::TableView view{};
  view.structSize = D2RL::DataTables::TableViewSize;
  if (g_tables->getTable(g_ctx, static_cast<D2RL::DataTables::Bank>(bank), id, &view) !=
          D2RL::DataTables::Result::Success ||
      !view.rows || !view.rowCount)
    return false;
  if (view.rowSize != row_size) {
    if (log_once(&g_size_warned, LogLevel::kWarning))
      log_warn("drop odds: the %s rows are %u bytes, not %u - drops are rolled for, as before", name, view.rowSize,
               row_size);
    return false;
  }
  out->rows = reinterpret_cast<uintptr_t>(view.rows);
  out->count = view.rowCount;
  out->revision = view.revision;
  return true;
}

template <typename T>
T at(uintptr_t row, uintptr_t offset) {
  T v{};
  mem::read_safe(row + offset, &v);
  return v;
}

// --- what is known of an item's base --------------------------------------------------------------------------

struct Named {  // a unique or set row of a base that can come at all
  uint16_t row = 0;
  int16_t level = 0;
  int32_t weight = 1;
  int32_t calc = -1;        // a condition of its own (-1 none)
  bool spawnable = false;   // picked among when no row is asked for by name
  bool by_request = false;  // a set item that comes only when the request allows its set
};

struct Base {
  bool read = false;
  bool known = false;
  bool gold = false;
  bool equipment = false;  // a weapon or armor: what superior is for
  bool durable = false;    // ... with durability: what can be ethereal
  bool sockets = false;    // a base sockets can go in
  uint8_t most_sockets[3] = {};
  uint8_t socket_levels[2] = {};
  uint8_t gem_sockets = 0;
  uint8_t level = 0;
  Kind kind;
  Ratio ratio{};
  bool has_ratio = false;
  std::vector<Named> uniques, sets;
};

struct Tables {
  uint8_t bank = 0;
  void* game = nullptr;
  View items, types, ratios, uniques, sets, classes;
  std::vector<Base> bases;
};
Tables g_known[4];

struct Flat {
  std::vector<Leaf> leaves;
};
std::unordered_map<uintptr_t, Flat> g_flat;  // the treasure classes that drop the same for every source

bool counts_as(const View& types, uint32_t type, uint32_t target, int depth = 0) {
  if (type == 0 || type >= types.count || depth > 10) return false;
  if (type == target) return true;
  const uintptr_t row = types.rows + static_cast<uintptr_t>(type) * kTypesRowSize;
  return counts_as(types, at<uint16_t>(row, kTypesEquiv1), target, depth + 1) ||
         counts_as(types, at<uint16_t>(row, kTypesEquiv2), target, depth + 1);
}

// The game's test of a ladder season (0x403FE0).
bool ladder_ok(uintptr_t game, int16_t first, int16_t last) {
  return !(at<uint8_t>(game, kGameType) == 0 && first != 0 && last >= 0 &&
           (at<int32_t>(game, kGameLadder) == 0 || first > 0));
}

bool views_of(uint8_t bank, Tables* t) {
  using D2RL::DataTables::TableId;
  return table(bank, TableId::Items, kItemsRowSize, "Items", &t->items) &&
         table(bank, TableId::ItemTypes, kTypesRowSize, "ItemTypes", &t->types) &&
         table(bank, TableId::ItemRatio, kRatioRowSize, "ItemRatio", &t->ratios) &&
         table(bank, TableId::UniqueItems, kUniqueRowSize, "UniqueItems", &t->uniques) &&
         table(bank, TableId::SetItems, kSetRowSize, "SetItems", &t->sets) &&
         table(bank, TableId::TreasureClasses, kTcSize, "TreasureClasses", &t->classes) &&
         t->items.count <= kMaxClasses;
}

bool same(const View& a, const View& b) {
  return a.rows == b.rows && a.count == b.count && a.revision == b.revision;
}

// The tables of a game's items. `again`: the loader is asked for them anew (a session's start), and what was
// read of them is dropped when they are other ones.
Tables* tables_of(uint8_t bank, void* game, bool again = false) {
  if (bank < 2 || bank > 3) return nullptr;  // a classic game's drops are rolled for
  if (InterlockedExchange(&g_forget, 0)) {
    for (Tables& known : g_known) known = Tables{};
    g_flat.clear();
  }
  Tables& t = g_known[bank];
  const bool known = t.bank == bank && t.game == game;
  if (known && !again) return &t;
  Tables now;
  if (!views_of(bank, &now)) {
    t = Tables{};
    g_flat.clear();
    return nullptr;
  }
  if (known && same(now.items, t.items) && same(now.types, t.types) && same(now.ratios, t.ratios) &&
      same(now.uniques, t.uniques) && same(now.sets, t.sets) && same(now.classes, t.classes))
    return &t;
  t = now;
  t.bank = bank;
  t.game = game;
  t.bases.assign(t.items.count, Base{});
  g_flat.clear();
  return &t;
}

const Base& base_of(Tables& t, uint32_t item_class) {
  static const Base kNone;
  if (item_class >= t.bases.size()) return kNone;
  Base& b = t.bases[item_class];
  if (b.read) return b;
  b.read = true;
  const uintptr_t item = t.items.rows + static_cast<uintptr_t>(item_class) * kItemsRowSize;
  const int16_t type = at<int16_t>(item, kItemsType), type2 = at<int16_t>(item, kItemsType2);
  if (type <= 0 || static_cast<uint32_t>(type) >= t.types.count) return b;
  const uintptr_t kind = t.types.rows + static_cast<uintptr_t>(type) * kTypesRowSize;
  b.gold = type == kTypeGold;
  b.level = at<uint8_t>(item, kItemsLevel);
  b.kind.normal_only = at<uint8_t>(kind, kTypesNormal) != 0;
  b.kind.always_magic = at<uint8_t>(kind, kTypesMagic) != 0;
  b.kind.can_be_rare = at<uint8_t>(kind, kTypesRare) != 0;
  b.kind.unique_only = at<uint8_t>(item, kItemsUnique) != 0;
  b.kind.quest = at<uint8_t>(item, kItemsQuest) != 0;
  const auto is_a = [&](uint32_t target) {
    return counts_as(t.types, static_cast<uint32_t>(type), target) ||
           (type2 > 0 && counts_as(t.types, static_cast<uint32_t>(type2), target));
  };
  b.equipment = is_a(kTypeWeapon) || is_a(kTypeArmor);
  b.durable = b.equipment && !at<uint8_t>(item, kItemsNoDurability) && at<uint8_t>(item, kItemsDurability) != 0;
  b.sockets = at<uint8_t>(item, kItemsHasInv) != 0 && !at<uint8_t>(item, kItemsStackable);
  mem::copy_from(b.most_sockets, kind + kTypesSockets, sizeof(b.most_sockets));
  mem::copy_from(b.socket_levels, kind + kTypesSocketLevels, sizeof(b.socket_levels));
  b.gem_sockets = at<uint8_t>(item, kItemsGemSockets);
  // The ItemRatio row, as the game picks it (0x313F20): by whether the base is a class's own and whether it is
  // an exceptional or elite one, the latest version up to the expansion's.
  const uint32_t code = at<uint32_t>(item, kItemsCode);
  const bool class_own = at<uint8_t>(kind, kTypesClass) < 8;
  const bool uber = b.equipment &&
                    (at<uint32_t>(item, kItemsUberCode) == code || at<uint32_t>(item, kItemsUltraCode) == code) &&
                    type != kTypeNoUber && !b.kind.quest;
  int best = -1;
  for (uint32_t i = 0; i < t.ratios.count; ++i) {
    const uintptr_t row = t.ratios.rows + static_cast<uintptr_t>(i) * kRatioRowSize;
    const int version = at<uint16_t>(row, kRatioVersion);
    if ((at<uint8_t>(row, kRatioClass) != 0) != class_own || (at<uint8_t>(row, kRatioUber) != 0) != uber ||
        version > 100 || version < best)
      continue;
    best = version;
    b.has_ratio = mem::copy_from(&b.ratio, row, sizeof(b.ratio));
  }
  // The unique and the set items of the base that the game's steps pick among (0x443910, 0x58ACD0), but for
  // what changes while a game runs: the item's level, a row's own condition, a unique that came already.
  const uintptr_t game = reinterpret_cast<uintptr_t>(t.game);
  for (uint32_t i = 0; i < t.uniques.count && i < 0x1000; ++i) {
    const uintptr_t row = t.uniques.rows + static_cast<uintptr_t>(i) * kUniqueRowSize;
    if (at<uint32_t>(row, kUniqueCode) != code) continue;
    const uint32_t flags = at<uint32_t>(row, kUniqueFlags);
    if (flags & kRowDisabled) continue;
    if (!ladder_ok(game, at<int16_t>(row, kUniqueLadder), at<int16_t>(row, kUniqueLadder + 2))) continue;
    Named n;
    n.row = static_cast<uint16_t>(i);
    n.level = at<int16_t>(row, kUniqueLevel);
    n.weight = at<int32_t>(row, kUniqueRarity);
    if (n.weight < 1) n.weight = 1;
    n.calc = at<int32_t>(row, kUniqueCalc);
    n.spawnable = (flags & kRowSpawnable) != 0;
    b.uniques.push_back(n);
  }
  for (uint32_t i = 0; i < t.sets.count; ++i) {
    const uintptr_t row = t.sets.rows + static_cast<uintptr_t>(i) * kSetRowSize;
    if (at<uint32_t>(row, kSetCode) != code) continue;
    const uint32_t flags = at<uint32_t>(row, kSetFlags);
    if (flags & kRowDisabled) continue;
    if (!ladder_ok(game, at<int16_t>(row, kSetLadder), at<int16_t>(row, kSetLadder + 2))) continue;
    Named n;
    n.row = static_cast<uint16_t>(i);
    n.level = at<int16_t>(row, kSetLevel);
    n.weight = at<int32_t>(row, kSetRarity);
    if (n.weight == 0) n.weight = 1;
    n.calc = at<int32_t>(row, kSetCalc);
    n.spawnable = (flags & kRowSpawnable) != 0;
    n.by_request = at<int16_t>(row, kSetSet) == kSetByRequest;
    if (n.weight > 0) b.sets.push_back(n);
  }
  (void)kUniqueVersion;
  (void)kSetVersion;
  b.known = true;
  return b;
}

bool dropped(void* game, uint32_t unique_row) {
  const int bits = sites::unique_bits_offset();
  if (!bits || unique_row > 0x1000) return false;
  uint32_t word = 0;
  return mem::read_safe(reinterpret_cast<uintptr_t>(game) + static_cast<uintptr_t>(bits) +
                            static_cast<uintptr_t>(unique_row >> 5) * 4,
                        &word) &&
         (word & (1u << (unique_row & 31))) != 0;
}

int most_sockets(const Base& b, int32_t item_level) {
  int most = b.most_sockets[2];
  if (item_level <= b.socket_levels[0]) most = b.most_sockets[0];
  else if (item_level <= b.socket_levels[1]) most = b.most_sockets[1];
  return most < b.gem_sockets ? most : b.gem_sockets;
}

// Random numbers of the plugin's own (what drops is picked here, not by the source's seed).
double uniform() {
  thread_local uint64_t s = 0;
  if (!s) {
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    s = (static_cast<uint64_t>(t.QuadPart) ^ 0xD1B54A32D192ED03ull) * (GetCurrentThreadId() | 1u);
    if (!s) s = 1;
  }
  s ^= s << 13;
  s ^= s >> 7;
  s ^= s << 17;
  return static_cast<double>(s >> 11) / 9007199254740992.0;  // 53 bits
}

bool same_leaf(const Leaf& a, const Leaf& b) {
  return a.item_class == b.item_class && a.flags == b.flags && a.extra == b.extra &&
         std::memcmp(a.mods, b.mods, sizeof(a.mods)) == 0;
}

bool leaf_before(const Leaf& a, const Leaf& b) {
  if (a.item_class != b.item_class) return a.item_class < b.item_class;
  if (a.flags != b.flags) return a.flags < b.flags;
  if (a.extra != b.extra) return a.extra < b.extra;
  return std::memcmp(a.mods, b.mods, sizeof(a.mods)) < 0;
}

void merge(std::vector<Leaf>* leaves) {
  std::sort(leaves->begin(), leaves->end(), leaf_before);
  size_t kept = 0;
  for (size_t i = 0; i < leaves->size(); ++i) {
    if (kept && same_leaf((*leaves)[kept - 1], (*leaves)[i])) (*leaves)[kept - 1].weight += (*leaves)[i].weight;
    else (*leaves)[kept++] = (*leaves)[i];
  }
  leaves->resize(kept);
}

// The killer's magic find, with its owner's for a pet's kill (the quality roll's own sum); none for a killer that
// is neither a player nor a monster.
int32_t magic_find_of(Unit* killer) {
  if (!killer) return 0;
  const uint32_t type = game::unit_type(killer);
  if (type != game::kPlayer && type != game::kMonster) return 0;
  int32_t magic_find = game::get_stat(killer, kStatMagicFind);
  if (type == game::kMonster && game::owned_by_local_player(killer))
    if (Unit* owner = game::local_server_player()) magic_find += game::get_stat(owner, kStatMagicFind);
  return magic_find;
}

// The chance of a roll of `chance` to come out below 128, as the quality roll asks it.
double below_128(int32_t chance) { return chance <= 128 ? 1.0 : 128.0 / chance; }

bool follow(const Classes& classes, uintptr_t tc, int depth, std::vector<Leaf>* out, bool* conditional) {
  out->clear();
  const auto known = g_flat.find(tc);
  if (known != g_flat.end()) {
    *out = known->second.leaves;
    return true;
  }
  if (depth > kMaxDepth) return false;
  uint8_t record[kTcCount + sizeof(uint64_t)] = {};
  if (!mem::copy_from(record, tc, sizeof(record))) return false;
  const auto field = [&record](uintptr_t offset, void* v, size_t n) { std::memcpy(v, record + offset, n); };
  int32_t total = 0, picks = 0, nodrop = 0;
  uint8_t flags = 0;
  uint16_t mods[kMods] = {};
  uintptr_t entries = 0;
  uint64_t count = 0;
  field(kTcTotal, &total, sizeof(total));
  field(kTcPicks, &picks, sizeof(picks));
  field(kTcNoDrop, &nodrop, sizeof(nodrop));
  field(kTcFlags, &flags, sizeof(flags));
  field(kTcMods, mods, sizeof(mods));
  field(kTcEntries, &entries, sizeof(entries));
  field(kTcCount, &count, sizeof(count));
  if (count > kMaxEntries) return false;
  // NoDrop is skipped in a session's walks, but for a treasure class that keeps it.
  if ((flags & kTcKeepsNoDrop) && nodrop != 0) return false;
  bool depends = false;
  if (count && total > 0) {
    if (!mem::plausible(entries)) return false;
    std::vector<uint8_t> list(static_cast<size_t>(count) * kEntrySize);
    if (!mem::copy_from(list.data(), entries, list.size())) return false;
    struct Taken {
      size_t index;
      int32_t chance;
    };
    std::vector<Taken> taken;
    int32_t left = 0;
    for (size_t i = 0; i < count; ++i) {
      const uint8_t* e = list.data() + i * kEntrySize;
      int32_t start = 0, next = total;
      std::memcpy(&start, e + kEntryStart, sizeof(start));
      if (i + 1 < count) std::memcpy(&next, e + kEntrySize + kEntryStart, sizeof(next));
      const int32_t chance = next - start;
      if (chance <= 0) continue;
      if ((flags & kTcHasConditional) && (e[kEntryFlags] & kEntryConditional)) {
        depends = true;
        if (!classes.takes || !classes.takes(tc, e, chance)) continue;
      }
      taken.push_back(Taken{i, chance});
      left += chance;
    }
    // How often each entry comes in one walk: by its share of every pick, or, with the picks below 0, as many
    // times as its chance says, in the entries' order, until the picks are made.
    const int32_t many = picks < 0 ? -picks : picks > 1 ? picks : 1;
    int32_t before = 0;
    std::vector<Leaf> below;
    for (const Taken& t : taken) {
      double times = 0;
      if (picks < 0) {
        const int32_t end = std::min(before + t.chance, std::min(many, left));
        times = end > before ? end - before : 0;
      } else {
        times = static_cast<double>(many) * t.chance / left;
      }
      before += t.chance;
      if (times <= 0) continue;
      const uint8_t* e = list.data() + t.index * kEntrySize;
      uint32_t item = 0;
      std::memcpy(&item, e + kEntryItem, sizeof(item));
      if (e[kEntryFlags] & kEntryClass) {
        const uint32_t id = item & 0xFFFF;
        if (!id || id >= classes.count) continue;
        bool child_depends = false;
        if (!follow(classes, classes.rows + static_cast<uintptr_t>(id) * kTcSize, depth + 1, &below, &child_depends))
          return false;
        depends = depends || child_depends;
        for (Leaf& l : below) {
          for (int m = 0; m < kMods; ++m) l.mods[m] = std::max(l.mods[m], mods[m]);
          l.weight *= times;
          out->push_back(l);
        }
        continue;
      }
      if (item == 0xFFFFFFFFu) continue;
      Leaf l;
      l.item_class = item;
      l.flags = e[kEntryFlags] & (kEntryUnique | kEntrySet);
      std::memcpy(&l.extra, e + kEntryExtra, sizeof(l.extra));
      std::memcpy(l.mods, mods, sizeof(l.mods));
      l.entry = entries + t.index * kEntrySize;
      l.weight = times;
      out->push_back(l);
    }
    merge(out);
  }
  if (depends) *conditional = true;
  else g_flat[tc] = Flat{*out};
  return true;
}


// --- the forms an item that is asked for can take ---------------------------------------------------------------

// The rows the unique or set step is to come to for a form (a pool).
enum : uint8_t {
  kAsItComes = 0,     // no unique or set step, one with nothing to pick among, or the game's own pick among them all
  kUniqueNew = 1,     // a unique of the base that has not come yet
  kUniqueFailed = 2,  // the step fails on one that has: the item is rare (or magic)
  kUniqueWanted = 3,  // a unique of the base that has not come yet and whose row is wanted
  kSetWanted = 4,     // a set item of the base whose row is wanted
};

struct Form {
  double chance = 0;  // of everything that can come of what was asked
  double share = 0;   // ... of which this much is the unique or set step's doing (its pool's share)
  int32_t quality = 0;
  uint8_t pool = kAsItComes;
  int8_t ethereal = -1, sockets = -1;
};
constexpr int kMaxForms = 16;

// The rows the unique step picks among for what is asked: their rarities together, of those the ones that have not
// come yet, and of those the ones that are wanted. A row asked for by name is the only one, when it is one the step
// takes.
struct Shares {
  double all = 0, open = 0, wanted = 0;
  bool named = false;  // the row asked for by name is the one
  bool known = true;   // false: a row's own condition cannot be asked now
};

bool wanted_unique(const Request& request, uint32_t row) { return request.uniques && request.uniques->has(row); }
bool wanted_set(const Request& request, uint32_t row) { return request.sets && request.sets->has(row); }

// Whether a row with a condition of its own comes for the source: 1, 0, or -1 when that cannot be asked now.
int holds(const Request& request, int32_t calc) {
  if (calc == -1) return 1;
  return request.calc ? request.calc(calc) : -1;
}

Shares unique_shares(const Request& request, const Base& b, const Ask& ask) {
  Shares s;
  if (ask.named > 0) {
    for (const Named& n : b.uniques) {
      if (n.row != ask.named - 1 || n.level > ask.item_level) continue;
      s.named = true;
      s.all = 1;
      s.open = dropped(request.game, n.row) ? 0 : 1;
      s.wanted = s.open > 0 && wanted_unique(request, n.row) ? 1 : 0;
      return s;
    }
  }
  for (const Named& n : b.uniques) {
    if (!n.spawnable || n.level > ask.item_level) continue;
    const int comes = holds(request, n.calc);
    if (comes < 0) {
      s.known = false;
      return s;
    }
    if (!comes) continue;
    s.all += n.weight;
    if (dropped(request.game, n.row)) continue;
    s.open += n.weight;
    if (wanted_unique(request, n.row)) s.wanted += n.weight;
  }
  return s;
}

// Whether a row of the base's is in a unique pool: one still to come, one that came already, one still to come
// that is wanted.
bool in_pool(const Request& request, const Named& n, uint8_t pool) {
  const bool came = dropped(request.game, n.row);
  return pool == kUniqueFailed ? came : !came && (pool != kUniqueWanted || wanted_unique(request, n.row));
}

// One of the base's uniques for the unique step to be given by name: one still to come (or one that is wanted too),
// by their rarities, or one that came already (the step then fails, as it does when it picks such a one itself). 0:
// the request's own name does, or there is none to give.
int32_t unique_name(const Request& request, const Base& b, const Ask& ask, uint8_t pool) {
  const Shares s = unique_shares(request, b, ask);
  if (!s.known) return 0;
  if (s.named) return 0;
  const double among = pool == kUniqueNew ? s.open : pool == kUniqueWanted ? s.wanted : s.all - s.open;
  if (among <= 0) return 0;
  double roll = uniform() * among;
  int32_t name = 0;
  for (const Named& n : b.uniques) {
    if (!n.spawnable || n.level > ask.item_level || holds(request, n.calc) != 1) continue;
    if (!in_pool(request, n, pool)) continue;
    name = n.row + 1;
    roll -= n.weight;
    if (roll < 0) break;
  }
  return name;
}

// The rows the set step picks among for what is asked (0x58ACD0: the base's rows at the item's level, a set that
// comes only by the request's leave left out without it, then the spawnable ones whose condition holds, by their
// rarities), and of those the ones that are wanted. A row asked for by name is taken before any is picked.
struct SetShares {
  double all = 0, wanted = 0;
  bool named = false;
  bool known = true;
};

SetShares set_shares(const Request& request, const Base& b, const Ask& ask) {
  SetShares s;
  for (const Named& n : b.sets) {
    if (n.level > ask.item_level || (n.by_request && !(ask.flags & kRequestSet29))) continue;
    if (ask.named > 0 && n.row == ask.named - 1) {
      s = SetShares{};
      s.named = true;
      s.all = 1;
      s.wanted = wanted_set(request, n.row) ? 1 : 0;
      return s;
    }
    if (!n.spawnable) continue;
    const int comes = holds(request, n.calc);
    if (comes < 0) {
      s.known = false;  // the step may still come to the row named: it walks the rows in their order
      continue;
    }
    if (!comes) continue;
    s.all += n.weight;
    if (wanted_set(request, n.row)) s.wanted += n.weight;
  }
  if (!s.known) s = SetShares{0, 0, false, false};
  return s;
}

// One of the base's wanted set items for the set step to be given by name, by their rarities. 0: the request's own
// name does, or there is none to give.
int32_t set_name(const Request& request, const Base& b, const Ask& ask) {
  const SetShares s = set_shares(request, b, ask);
  if (!s.known || s.named || s.wanted <= 0) return 0;
  double roll = uniform() * s.wanted;
  int32_t name = 0;
  for (const Named& n : b.sets) {
    if (n.level > ask.item_level || (n.by_request && !(ask.flags & kRequestSet29))) continue;
    if (!n.spawnable || holds(request, n.calc) != 1 || !wanted_set(request, n.row)) continue;
    name = n.row + 1;
    roll -= n.weight;
    if (roll < 0) break;
  }
  return name;
}

// Every form of what is asked that the filter shows, and, of what it does not show, every form that is a unique or
// set item of a wanted row. False when it cannot be said.
bool forms_of(const Request& request, const Base& b, const Ask& ask, uint16_t shown, Form* out, int* count) {
  *count = 0;
  struct Path {
    double chance;
    int32_t quality;
    uint8_t pool;
    double wanted;        // the share of the path whose row is wanted
    uint8_t wanted_pool;  // ... and the pool that names such a row
  } paths[2];
  int n = 0;
  const int32_t asked = quality_asked(b.kind, ask.quality, request.all_superior);
  if (asked == 7) {
    const Shares s = unique_shares(request, b, ask);
    if (!s.known) return false;
    const int32_t failed = quality_made(b.kind, 7, b.equipment, false);
    if (s.all <= 0) {
      // Nothing to pick among: an item that must be unique stays one (without a row), any other fails.
      paths[n++] = Path{1, b.kind.unique_only ? 7 : failed, kAsItComes, 0, kAsItComes};
    } else {
      if (s.open > 0) paths[n++] = Path{s.open / s.all, 7, kUniqueNew, s.wanted / s.open, kUniqueWanted};
      if (s.open < s.all) paths[n++] = Path{1 - s.open / s.all, failed, kUniqueFailed, 0, kAsItComes};
    }
  } else if (asked == 5) {
    const SetShares s = set_shares(request, b, ask);
    if (!s.known) return false;
    const bool comes = s.all > 0;
    paths[n++] = Path{1, quality_made(b.kind, 5, b.equipment, comes), kAsItComes, comes ? s.wanted / s.all : 0,
                      kSetWanted};
  } else {
    paths[n++] = Path{1, quality_made(b.kind, asked, b.equipment, true), kAsItComes, 0, kAsItComes};
  }
  for (int i = 0; i < n; ++i) {
    const Path& p = paths[i];
    const int32_t q = p.quality;
    // Ethereal: a weapon or armor with durability, not of low quality, not a set item, unless the request says
    // never. Sockets: a normal or superior item whose base takes them at its level, unless the request says none.
    const bool can_be_ethereal = b.durable && q != 1 && q != 5 && !(ask.flags & kNeverEthereal);
    const bool can_have_sockets =
        (q == 2 || q == 3) && b.sockets && most_sockets(b, ask.item_level) > 0 && !(ask.flags & kNoSockets);
    const double ethereal = !can_be_ethereal                                         ? 0
                            : (ask.flags & kEthereal) || request.all_ethereal         ? 1
                                                                                      : 1 - (1 - ask.ethereal_mod / 1024.0) * 0.95;
    const double sockets = !can_have_sockets                                       ? 0
                           : (ask.flags & kSockets) || request.all_socketed         ? 1
                                                                                    : 1 - (1 - ask.sockets_mod / 1024.0) * 0.67;
    const bool plain = (shown >> ((q - 1) * 2) & 1) != 0, with = (shown >> ((q - 1) * 2 + 1) & 1) != 0;
    const Extras x = extras_odds(ethereal, sockets);
    // A form of the path: its share of the path (`share`, the part its pool decides), the chance of its ethereal and
    // socketed state within that, the pool it comes from.
    const auto add = [&](double share, double chance, int8_t e, int8_t s, uint8_t pool) {
      if (share <= 0 || chance <= 0 || *count >= kMaxForms) return;
      Form& f = out[(*count)++];
      f.chance = share * chance;
      f.share = share;
      f.quality = q;
      f.pool = pool;
      f.ethereal = e;
      f.sockets = s;
    };
    const int8_t no_e = ethereal > 0 ? 0 : -1, no_s = sockets > 0 ? 0 : -1;
    const bool sure = ethereal >= 1 || sockets >= 1;  // ethereal or socketed whatever is rolled
    // What the filter shows: the whole path, as the step picks among its rows.
    if (plain && with) {
      add(p.chance, 1, -1, -1, p.pool);
    } else if (plain) {
      add(p.chance, x.of[0], no_e, no_s, p.pool);
    } else if (with) {
      if (sure) {
        add(p.chance, 1, -1, -1, p.pool);
      } else {
        add(p.chance, x.of[1], 1, no_s, p.pool);
        add(p.chance, x.of[2], no_e, 1, p.pool);
        add(p.chance, x.of[3], 1, 1, p.pool);
      }
    }
    // Of what it does not show, the part whose row is wanted.
    if (p.wanted <= 0) continue;
    const double wanted = p.chance * p.wanted;
    if (plain && with) {
    } else if (plain) {
      if (sure) {
        add(wanted, 1, -1, -1, p.wanted_pool);
      } else {
        add(wanted, x.of[1], 1, no_s, p.wanted_pool);
        add(wanted, x.of[2], no_e, 1, p.wanted_pool);
        add(wanted, x.of[3], 1, 1, p.wanted_pool);
      }
    } else if (with) {
      add(wanted, x.of[0], no_e, no_s, p.wanted_pool);
    } else {
      add(wanted, 1, -1, -1, p.wanted_pool);
    }
  }
  return true;
}

// Whether the rows of a pool are named by the request itself (a treasure class entry that names its unique or set
// item).
bool named_by_request(const Request& request, const Base& b, const Ask& ask, uint8_t pool) {
  if (pool == kSetWanted) return set_shares(request, b, ask).named;
  return unique_shares(request, b, ask).named;
}

void force_of(const Request& request, const Base& b, const Ask& ask, const Form& f, Force* force) {
  *force = Force{};
  force->quality = f.quality;
  force->ethereal = f.ethereal;
  force->sockets = f.sockets;
  if (f.pool == kSetWanted) force->name = set_name(request, b, ask);
  else if (f.pool != kAsItComes) force->name = unique_name(request, b, ask, f.pool);
  // The row it comes out as, when it comes out as the unique or set item of one: the one given, or the request's.
  if (f.pool == kUniqueNew || f.pool == kUniqueWanted || f.pool == kSetWanted)
    force->row = force->name ? force->name - 1 : ask.named > 0 ? ask.named - 1 : -1;
  if (f.ethereal == 1) {
    force->set |= kEthereal;
    force->clear |= kNeverEthereal;
  } else if (f.ethereal == 0) {
    force->set |= kNeverEthereal;
    force->clear |= kEthereal;
  }
  if (f.sockets == 1) {
    force->set |= kSockets;
    force->clear |= kNoSockets;
  } else if (f.sockets == 0) {
    force->set |= kNoSockets;
    force->clear |= kSockets;
  }
}

constexpr double kWhole = 1 - 1e-9;

}  // namespace

Odds quality_odds(const Ratio& ratio, const Kind& kind, int32_t levels_above, int32_t magic_find,
                  const uint16_t mods[4]) {
  Odds o;
  if (kind.normal_only) {
    o.of[2] = 1;
    return o;
  }
  if (kind.unique_only || (kind.always_magic && kind.quest)) {
    o.of[7] = 1;
    return o;
  }
  double rest = 1;
  // Magic find counts in full up to 10, then less and less for the better qualities (unique 250, set 500, rare
  // 600); none at all leaves the chances as the row has them.
  const auto chance_of = [&](int32_t most, int32_t divisor, int32_t least, int32_t wears_off, uint16_t mod) {
    const int32_t base = most - (divisor ? levels_above / divisor : 0);
    int32_t chance = base * 128;
    if (magic_find != 0) {
      int32_t with = magic_find + 100;
      if (wears_off && with > 110) with = magic_find * wears_off / (magic_find + wears_off) + 100;
      if (with != 0) chance = base * 12800 / with;
    }
    if (chance <= least) chance = least;
    return chance - static_cast<int32_t>(static_cast<int64_t>(mod) * chance / 1024);
  };
  const auto take = [&](int quality, int32_t chance) {
    const double p = chance < 1 ? 1.0 : below_128(chance);
    o.of[quality] = rest * p;
    rest *= 1 - p;
  };
  if (magic_find >= -99) {
    take(7, chance_of(ratio.unique, ratio.unique_divisor, ratio.unique_min, 250, mods[3]));
    take(5, chance_of(ratio.set, ratio.set_divisor, ratio.set_min, 500, mods[2]));
    if (kind.can_be_rare) take(6, chance_of(ratio.rare, ratio.rare_divisor, ratio.rare_min, 600, mods[1]));
    if (kind.always_magic) {
      o.of[4] = rest;
      return o;
    }
    take(4, chance_of(ratio.magic, ratio.magic_divisor, ratio.magic_min, 0, mods[0]));
  }
  // Superior, then normal, else low quality: no magic find, no least chance, no mods.
  const int32_t superior =
      (ratio.superior - (ratio.superior_divisor ? levels_above / ratio.superior_divisor : 0)) * 128;
  double p = superior > 0 ? below_128(superior) : 1.0;
  o.of[3] = rest * p;
  rest *= 1 - p;
  const int32_t normal = (ratio.normal - (ratio.normal_divisor ? levels_above / ratio.normal_divisor : 0)) * 128;
  p = normal > 0 ? below_128(normal) : 1.0;
  o.of[2] = rest * p;
  o.of[1] = rest * (1 - p);
  return o;
}

int32_t quality_asked(const Kind& kind, int32_t quality, bool all_superior) {
  if (all_superior && (quality == 1 || quality == 2)) quality = 3;
  if (kind.always_magic && quality < 4) quality = kind.quest ? 7 : 4;
  if (!kind.can_be_rare && quality == 6) quality = 4;
  if (kind.unique_only) quality = 7;
  if (kind.normal_only) quality = 2;
  return quality;
}

int32_t quality_made(const Kind& kind, int32_t asked, bool equipment, bool made) {
  switch (asked) {
    case 1:
    case 3:
      return equipment ? asked : 2;
    case 5:
      return made ? 5 : 4;
    case 7:
      return made ? 7 : kind.can_be_rare ? 6 : 4;
    default:
      return asked;
  }
}

Extras extras_odds(double ethereal, double sockets) {
  Extras x;
  x.of[0] = (1 - ethereal) * (1 - sockets);
  x.of[1] = ethereal * (1 - sockets);
  x.of[2] = (1 - ethereal) * sockets;
  x.of[3] = ethereal * sockets;
  return x;
}

bool leaves_of(const Classes& classes, uintptr_t tc, std::vector<Leaf>* out, bool* conditional) {
  *conditional = false;
  if (!classes.rows || tc < classes.rows || (tc - classes.rows) % kTcSize != 0 ||
      (tc - classes.rows) / kTcSize >= classes.count)
    return false;
  return follow(classes, tc, 0, out, conditional);
}

Answer judge(const Request& request, const Ask& ask, Force* force) {
  *force = Force{};
  Tables* const t = tables_of(request.bank, request.game);
  if (!t || ask.quality < 1 || ask.quality > 7) return Answer::kAsItComes;
  const Base& b = base_of(*t, ask.item_class);
  // A quest item comes whatever the filter says; gold goes by its amount (the filter's, once it is made), and
  // nothing but the filter wants it.
  if (!b.known || b.kind.quest) return Answer::kAsItComes;
  if (b.gold) return request.filter ? Answer::kAsItComes : Answer::kNot;
  uint16_t shown = 0;
  if (request.filter && (!request.shown || !request.shown(ask.item_class, &shown))) return Answer::kAsItComes;
  Form forms[kMaxForms];
  int count = 0;
  if (!forms_of(request, b, ask, shown, forms, &count)) return Answer::kAsItComes;
  double all = 0;
  for (int i = 0; i < count; ++i) all += forms[i].chance;
  if (all >= kWhole) return Answer::kAsItComes;
  if (all <= 0) return Answer::kNot;
  double roll = uniform();
  if (roll >= all) return Answer::kNotThisTime;
  int chosen = 0;
  for (int i = 0; i < count; ++i) {
    chosen = i;
    roll -= forms[i].chance;
    if (roll < 0) break;
  }
  force_of(request, b, ask, forms[chosen], force);
  return Answer::kSo;
}

int may_be_wanted(const Request& request, uint32_t item_class, int32_t quality) {
  if (!request.uniques && !request.sets) return 0;
  Tables* const t = tables_of(request.bank, request.game);
  if (!t) return -1;
  const Base& b = base_of(*t, item_class);
  if (!b.known) return -1;
  if (b.gold || b.kind.quest || quality < 1 || quality > 7) return 0;
  const int32_t asked = quality_asked(b.kind, quality, request.all_superior);
  if (asked == 7) {
    for (const Named& n : b.uniques)
      if (wanted_unique(request, n.row) && !dropped(request.game, n.row)) return 1;
  } else if (asked == 5) {
    for (const Named& n : b.sets)
      if (wanted_set(request, n.row)) return 1;
  }
  return 0;
}

namespace {

// The item an entry of a treasure class asks the item maker for, but for its quality.
Ask ask_of(const Request& request, const Leaf& leaf, int32_t quality) {
  Ask ask;
  ask.item_class = leaf.item_class;
  ask.quality = quality;
  ask.named = leaf.flags & (kEntryUnique | kEntrySet) ? leaf.extra + 1 : 0;
  ask.item_level = request.item_level;
  ask.flags = request.flags;
  ask.ethereal_mod = leaf.mods[kModEthereal];
  ask.sockets_mod = leaf.mods[kModSockets];
  return ask;
}

// The qualities the drop core asks the item maker for, for an entry: the one the entry names a row of, the one
// its caller wants, else the quality roll's.
bool asked_odds(const Request& request, const Base& b, const Leaf& leaf, Odds* odds) {
  *odds = Odds{};
  if (leaf.flags & kEntryUnique) {
    odds->of[7] = 1;
  } else if (leaf.flags & kEntrySet) {
    odds->of[5] = 1;
  } else if (request.forced_quality >= 1 && request.forced_quality <= 7) {
    odds->of[request.forced_quality] = 1;
  } else if (request.forced_quality != 0 || !b.has_ratio) {
    return false;
  } else {
    *odds = quality_odds(b.ratio, b.kind, request.roll_level - b.level, magic_find_of(request.killer), leaf.mods);
  }
  return true;
}

// The treasure classes of the request's tables, with what the drop core says of their conditional entries.
Classes classes_of(const Request& request, const Tables& t) {
  Classes c;
  c.rows = t.classes.rows;
  c.count = t.classes.count;
  const uintptr_t rows = c.rows;
  const uint32_t count = c.count;
  const Request* const r = &request;
  c.takes = [r, rows, count](uintptr_t tc, const uint8_t* entry, int32_t) {
    if (!(entry[kEntryFlags] & kEntryClass)) return r->takes && r->takes(tc, entry);
    uint32_t id = 0;
    std::memcpy(&id, entry + kEntryItem, sizeof(id));
    id &= 0xFFFF;
    return id && id < count && r->holds && r->holds(rows + static_cast<uintptr_t>(id) * kTcSize);
  };
  return c;
}

}  // namespace

// What an outcome weighs now: its share of the walk, times what is left of its base's uniques (or of its wanted
// rows) for it.
void Plan::weigh(Outcome* o) {
  o->weight = 0;
  if (o->base <= 0) return;
  if (o->pool == kAsItComes) {
    o->weight = o->base;
    return;
  }
  Tables* const t = tables_of(request_.bank, request_.game);
  if (!t) return;
  const Base& b = base_of(*t, o->leaf.item_class);
  const Ask ask = ask_of(request_, o->leaf, o->quality);
  if (o->pool == kSetWanted) {
    const SetShares s = set_shares(request_, b, ask);
    if (s.known && s.all > 0) o->weight = o->base * s.wanted / s.all;
    return;
  }
  const Shares s = unique_shares(request_, b, ask);
  if (!s.known || s.all <= 0) return;
  const double share = o->pool == kUniqueNew      ? s.open / s.all
                       : o->pool == kUniqueWanted ? s.wanted / s.all
                                                  : 1 - s.open / s.all;
  o->weight = o->base * share;
}

namespace {

// Whether a base has a unique or set item of a wanted row at all (whatever the item level).
bool any_wanted(const Request& request, const Base& b) {
  for (const Named& n : b.uniques)
    if (wanted_unique(request, n.row)) return true;
  for (const Named& n : b.sets)
    if (wanted_set(request, n.row)) return true;
  return false;
}

}  // namespace

void Plan::add(const Leaf& leaf) {
  Tables* const t = tables_of(request_.bank, request_.game);
  if (!t) return;
  const Base& b = base_of(*t, leaf.item_class);
  if (!b.known) return;
  ++totals_.leaves;
  totals_.walk += leaf.weight;
  // A quest item comes once, with the drop itself.
  if (b.kind.quest) return;
  Outcome o;
  o.leaf = leaf;
  if (b.gold) {
    if (!request_.filter) return;  // only the filter shows gold (by its amount)
    o.gold = true;
    o.base = leaf.weight;
    outcomes_.push_back(o);
    return;
  }
  uint16_t shown = 0;
  if (request_.filter && (!request_.shown || !request_.shown(leaf.item_class, &shown))) shown = 0;
  if (!shown && !any_wanted(request_, b)) return;
  Odds odds;
  if (!asked_odds(request_, b, leaf, &odds)) return;
  struct Made {
    int32_t quality;
    Form form;
  };
  Made made[7 * kMaxForms];
  int count = 0;
  bool whole = true;
  for (int32_t quality = 7; quality >= 1; --quality) {
    if (odds.of[quality] <= 0) continue;
    Form forms[kMaxForms];
    int n = 0;
    if (!forms_of(request_, b, ask_of(request_, leaf, quality), shown, forms, &n)) {
      whole = false;  // it cannot be said what comes of this quality: not picked
      continue;
    }
    double all = 0;
    for (int i = 0; i < n; ++i) {
      all += forms[i].chance;
      made[count++] = Made{quality, forms[i]};
    }
    whole = whole && all >= kWhole;
  }
  // Shown whatever the quality roll and the generator make of it: the game rolls, as it would.
  if (whole && !(leaf.flags & (kEntryUnique | kEntrySet)) && !request_.forced_quality) {
    o.base = leaf.weight;
    outcomes_.push_back(o);
    return;
  }
  for (int i = 0; i < count; ++i) {
    const Form& f = made[i].form;
    o.quality = made[i].quality;
    o.comes = f.quality;
    o.pool = f.pool;
    o.ethereal = f.ethereal;
    o.sockets = f.sockets;
    o.base = f.share > 0 ? leaf.weight * odds.of[made[i].quality] * f.chance / f.share : 0;
    outcomes_.push_back(o);
  }
}

bool Plan::build(const Request& request) {
  request_ = request;
  outcomes_.clear();
  totals_ = Totals{};
  left_ = 0;
  why_ = nullptr;
  Tables* const t = tables_of(request.bank, request.game, true);
  if (!t) {
    why_ = "the game's tables are not the ones known (or a classic game)";
    return false;
  }
  // A treasure class with a condition of its own drops nothing when that does not hold.
  uint8_t flags = 0;
  if (!mem::read_safe(request.tc + kTcFlags, &flags)) {
    why_ = "the treasure class cannot be read";
    return false;
  }
  if ((flags & kTcConditional) && !(request_.holds && request_.holds(request.tc))) return true;
  if (request.tc < t->classes.rows || (request.tc - t->classes.rows) % kTcSize != 0 ||
      (request.tc - t->classes.rows) / kTcSize >= t->classes.count) {
    why_ = "the treasure class is not among the loader's TreasureClasses rows";
    return false;
  }
  std::vector<Leaf> leaves;
  if (!leaves_of(classes_of(request_, *t), request.tc, &leaves, &totals_.conditional)) {
    why_ = "a treasure class on the way cannot be read, keeps its NoDrop or is nested too deep";
    return false;
  }
  for (const Leaf& leaf : leaves) add(leaf);
  size_t kept = 0;
  for (Outcome& o : outcomes_) {
    weigh(&o);
    if (o.base <= 0) continue;
    left_ += o.weight;
    outcomes_[kept++] = o;
  }
  outcomes_.resize(kept);
  totals_.outcomes = static_cast<int>(kept);
  totals_.shown = left_;
  return true;
}

bool expected(const Request& request, std::vector<Expected>* out) {
  out->clear();
  Tables* const t = tables_of(request.bank, request.game, true);
  if (!t) return false;
  uint8_t flags = 0;
  if (!mem::read_safe(request.tc + kTcFlags, &flags)) return false;
  if ((flags & kTcConditional) && !(request.holds && request.holds(request.tc))) return true;
  std::vector<Leaf> leaves;
  bool conditional = false;
  if (!leaves_of(classes_of(request, *t), request.tc, &leaves, &conditional)) return false;
  for (const Leaf& leaf : leaves) {
    const Base& b = base_of(*t, leaf.item_class);
    Odds odds;
    if (!b.known || !asked_odds(request, b, leaf, &odds)) continue;
    for (int32_t quality = 1; quality <= 7; ++quality) {
      if (odds.of[quality] <= 0) continue;
      Expected e;
      e.item_class = leaf.item_class;
      e.quality = quality;
      e.count = leaf.weight * odds.of[quality];
      out->push_back(e);
    }
  }
  // An item more than one entry leads to, with mods of their own, is one line.
  std::sort(out->begin(), out->end(), [](const Expected& a, const Expected& b) {
    return a.item_class != b.item_class ? a.item_class < b.item_class : a.quality < b.quality;
  });
  size_t kept = 0;
  for (size_t i = 0; i < out->size(); ++i) {
    if (kept && (*out)[kept - 1].item_class == (*out)[i].item_class && (*out)[kept - 1].quality == (*out)[i].quality)
      (*out)[kept - 1].count += (*out)[i].count;
    else
      (*out)[kept++] = (*out)[i];
  }
  out->resize(kept);
  return true;
}

bool Plan::pick(Pick* out) {
  if (left_ <= 0) return false;
  double roll = uniform() * left_;
  int chosen = -1;
  for (size_t i = 0; i < outcomes_.size(); ++i) {
    if (outcomes_[i].weight <= 0) continue;
    chosen = static_cast<int>(i);
    roll -= outcomes_[i].weight;
    if (roll < 0) break;
  }
  if (chosen < 0) return false;
  const Outcome& o = outcomes_[static_cast<size_t>(chosen)];
  *out = Pick{};
  out->outcome = chosen;
  out->item_class = o.leaf.item_class;
  out->quality = o.quality;
  out->gold = o.gold;
  // The game's entry, alone in a treasure class of its own: from the first of its chances, and taken whatever
  // the condition it may have (the drop core was asked about that when the entries were followed).
  if (!mem::copy_from(out->entry, o.leaf.entry, kEntrySize)) return false;
  const int32_t start = 0;
  std::memcpy(out->entry + kEntryStartClassic, &start, sizeof(start));
  std::memcpy(out->entry + kEntryStart, &start, sizeof(start));
  out->entry[kEntryFlags] = static_cast<uint8_t>(out->entry[kEntryFlags] & ~kEntryConditional);
  std::memcpy(out->mods, o.leaf.mods, sizeof(out->mods));
  if (!o.quality) return true;
  Tables* const t = tables_of(request_.bank, request_.game);
  if (!t) return false;
  Form f;
  f.quality = o.comes;
  f.pool = o.pool;
  f.ethereal = o.ethereal;
  f.sockets = o.sockets;
  const Base& b = base_of(*t, o.leaf.item_class);
  const Ask ask = ask_of(request_, o.leaf, o.quality);
  force_of(request_, b, ask, f, &out->force);
  // A unique that is to come or to fail, or a wanted set item, needs the row that does it (its own name's, or one
  // given here).
  if (o.pool != kAsItComes && !out->force.name && !named_by_request(request_, b, ask, o.pool)) return false;
  return true;
}

void Plan::made(const Pick& pick, bool as_picked) {
  if (pick.outcome < 0 || static_cast<size_t>(pick.outcome) >= outcomes_.size()) return;
  // An outcome that did not give what it promised is not picked again. A unique that came leaves its base's
  // other uniques, a wanted set item that came its other wanted rows: every outcome of that class is weighed again.
  if (!as_picked) outcomes_[static_cast<size_t>(pick.outcome)].base = 0;
  left_ = 0;
  for (Outcome& o : outcomes_) {
    if (o.leaf.item_class == pick.item_class) weigh(&o);
    left_ += o.weight;
  }
}

void Plan::without_gold() {
  left_ = 0;
  for (Outcome& o : outcomes_) {
    if (o.gold) o.base = o.weight = 0;
    left_ += o.weight;
  }
}

void name_of(uintptr_t tc, char* out, size_t size) {
  if (!out || !size) return;
  std::memset(out, 0, size);
  if (size < 2 || !mem::copy_from(out, tc + kTcName, size - 1)) {
    out[0] = 0;
    return;
  }
  for (size_t i = 0; i < size - 1 && out[i]; ++i)
    if (static_cast<unsigned char>(out[i]) < 0x20 || static_cast<unsigned char>(out[i]) > 0x7E) out[i] = '?';
}

void set_services(const D2RL::PluginContext* ctx, const D2RL::DataTableService* tables) {
  g_ctx = ctx;
  g_tables = tables;
}

void reset() { InterlockedExchange(&g_forget, 1); }

}  // namespace d2rcc::dropodds
