#include "itemgen.h"

#include <algorithm>
#include <cstring>

namespace d2rcc::itemgen {
namespace {

// --- the rows' fields (the loader's compiled tables name them; the generator's code reads the same offsets) ---
// An affix row (MagicSuffix, MagicPrefix, AutoMagic).
constexpr uint32_t kAffixVersion = 0x22;    // u16
constexpr uint32_t kAffixMods = 0x24;       // three {int32 property, param, min, max}
constexpr uint32_t kAffixSpawnable = 0x54;  // u8
constexpr uint32_t kAffixLevel = 0x58;      // int32
constexpr uint32_t kAffixGroup = 0x5C;      // int32
constexpr uint32_t kAffixMaxLevel = 0x60;   // int32 (0 none)
constexpr uint32_t kAffixRare = 0x64;       // u8
constexpr uint32_t kAffixClass = 0x66;      // u8 (0xFF none)
constexpr uint32_t kAffixTypes = 0x6A;      // int16[7]
constexpr uint32_t kAffixNotTypes = 0x78;   // int16[5]
constexpr uint32_t kAffixFrequency = 0x82;  // u8
constexpr uint32_t kModSize = 0x10;
// A Properties row.
constexpr uint32_t kPropVal = 0x0A;   // u16[7]
constexpr uint32_t kPropFunc = 0x18;  // u8[7]
constexpr uint32_t kPropStat = 0x20;  // u16[7]
constexpr int kPropSlots = 7;
constexpr int kPropFunctions = 37;    // the generator's function table
constexpr uint16_t kStatSockets = 0xC2;
constexpr uint16_t kStatArmorPercent = 0x10, kStatArmor = 0x1F;
// An Items row.
constexpr uint32_t kItemCode = 0x80;
constexpr uint32_t kItemBits = 0xE4;   // bitfield1: bit 0, the game's imbue takes the item
constexpr uint32_t kCodeWirtsLeg = 0x2067656C;  // "leg "
constexpr uint32_t kItemMinAc = 0xD4;
constexpr uint32_t kItemMaxAc = 0xD8;
constexpr uint32_t kItemVersion = 0xFE;  // u16
constexpr uint32_t kItemAutoPrefix = 0x100;  // u16: its automagic group (0 none)
constexpr uint32_t kItemLevel = 0x10D;
constexpr uint32_t kItemWidth = 0x11E;
constexpr uint32_t kItemHeight = 0x11F;
constexpr uint32_t kItemDurability = 0x121;
constexpr uint32_t kItemNoDurability = 0x122;
constexpr uint32_t kItemType = 0x12E;   // int16
constexpr uint32_t kItemType2 = 0x130;  // int16
constexpr uint32_t kItemUnique = 0x139;
constexpr uint32_t kItemQuest = 0x13A;
constexpr uint32_t kItemStackable = 0x142;
constexpr uint32_t kItemHasInv = 0x147;
constexpr uint32_t kItemGemSockets = 0x148;
constexpr uint32_t kItemMagicLevel = 0x150;
// An ItemTypes row.
constexpr uint32_t kTypeEquiv1 = 0x04;  // u16
constexpr uint32_t kTypeEquiv2 = 0x06;
constexpr uint32_t kTypeThrowable = 0x10;
constexpr uint32_t kTypeMagic = 0x14;   // always magic
constexpr uint32_t kTypeRare = 0x15;    // can be rare
constexpr uint32_t kTypeNormal = 0x16;  // normal only
constexpr uint32_t kTypeSockets = 0x18;        // u8[3]: by item level
constexpr uint32_t kTypeSocketLevels = 0x1B;   // u8[2]: the two thresholds
constexpr uint32_t kTypeStaffMods = 0x1F;      // the class whose skills a class item rolls (8 and up none)
constexpr uint32_t kTypeClass = 0x20;          // the class its affixes are kept to
// A UniqueItems / SetItems row.
constexpr uint32_t kUniqueVersion = 0x24, kUniqueCode = 0x28, kUniqueFlags = 0x2C, kUniqueRarity = 0x34;
constexpr uint32_t kUniqueLevel = 0x38, kUniqueLadder = 0x8C, kUniqueCalc = 0x90, kUniqueMods = 0x98;
constexpr int kUniqueModCount = 12;
constexpr uint32_t kSetVersion = 0x22, kSetCode = 0x28, kSetFlags = 0x2C, kSetLevel = 0x34, kSetRarity = 0x38;
constexpr uint32_t kSetLadder = 0x8A, kSetCalc = 0x90, kSetMods = 0x98, kSetBonusMods = 0x128;
constexpr uint32_t kSetSet = 0x30;      // int16: its set
constexpr int16_t kSetByRequest = 0x1D;  // a set whose items come only when the request's flags say so (bit 0)
constexpr int kSetModCount = 9, kSetBonusCount = 10;
constexpr uint32_t kFlagSpawnable = 1, kFlagDisabled = 4;
// A QualityItems row: a byte per kind of item it goes on, then two mods.
constexpr uint32_t kQualityMods = 0x0C;
constexpr int kQualityModCount = 2;
// A rare name row.
constexpr uint32_t kNameVersion = 0x0E, kNameTypes = 0x10, kNameNotTypes = 0x1E;

// Item types the generator asks by number.
constexpr int kTypeWeapon = 0x2D, kTypeArmor = 0x32, kTypeJewel = 0x3A;
constexpr int kSpecialTypes[] = {0x0D, 0x28, 0x07, 0x16, 0x12};  // charm, a quest type, body part, scroll, book
// Qualities.
enum : int32_t { kInferior = 1, kNormal = 2, kSuperior = 3, kMagic = 4, kSet = 5, kRare = 6, kUnique = 7, kCrafted = 8 };
// Request flags.
constexpr uint32_t kNeverEthereal = 2, kEthereal = 4, kNoSockets = 8, kSockets = 0x10, kSkillBonus = 0x20;

constexpr int kMaxGroups = 64;  // distinct affix groups a side's candidates may have (3.3: 42)

template <typename T>
T at(const uint8_t* row, uint32_t offset) {
  T v;
  std::memcpy(&v, row + offset, sizeof(T));
  return v;
}

// One property of a row: what its functions roll.
struct Mod {
  int32_t param = 0, lo = 0, hi = 0;  // the row's min and max, as written
  uint8_t fn[kPropSlots] = {};
  uint16_t val[kPropSlots] = {};
  uint8_t count = 0;
  bool unsupported = false;  // a property group, a function the model does not follow
  bool top_defense = false;  // enhanced defense: an armor's base defense becomes its top plus one (0x3D5AC0)
};

// A row the generator can pick: an affix, a unique or set item, a superior kind.
struct Row {
  int32_t id = 0;         // an affix's id (row + 1); else the row
  uint32_t weight = 0;
  int32_t group = 0;
  uint8_t group_bit = 0;        // its group among its side's
  uint8_t other_bit = 0xFF;     // ... and among the other side's, when that side has the group too
  bool top = false;             // the best version of its affix among the candidates
  uint16_t mods_at = 0, mods_count = 0;
};

struct Side {
  std::vector<Row> rows;  // in table order
  uint32_t total = 0;
  uint32_t group_total[kMaxGroups] = {};
  int32_t group_id[kMaxGroups] = {};
  int groups = 0;
  bool overflow = false;  // a weight the picker refuses, or more groups than the model keeps
};

struct SkillTier {
  std::vector<int16_t> skills;
  std::vector<uint8_t> usable;  // the item is of the skill's item type (or the skill needs none)
};

}  // namespace

class Plan {
 public:
  Request request;
  int32_t quality = 0;  // after the generator's "must be" fixes
  int32_t chain[6] = {};
  int chain_length = 0;
  int32_t ilvl = 1;
  bool jewel = false;
  int most_magic = 0, most_rare = 0, most_crafted = 0;  // the most affixes each step can make for the item
  bool can_be_rare = false;
  bool always_unique = false;
  bool rare_names = false;      // both rare name lists have a name for the item
  int rare_prefix_names = 0, rare_suffix_names = 0;
  bool special_type = false;    // a type the normal step treats on its own
  bool quest = false;
  bool ethereal_base = false;   // a weapon or armor with durability, in an expansion game
  bool durable_base = false;    // ... whatever the request says of ethereal
  int32_t socket_n = 0;         // sockets the socket roll can give (0: no roll)
  int32_t auto_group = 0;
  int skill_class = 8;
  bool skills_new_mode = false;
  int32_t skill_first = 0;
  SkillTier tiers[6];
  int32_t superior_n = 0;
  uint8_t superior_fits[10] = {};
  std::vector<Row> superior;  // by QualityItems row
  // The affix candidates: prefix / suffix, for a magic item [0] and for a rare, crafted or tempered one [1].
  Side side[2][2];
  Side autos[2];
  std::vector<Row> uniques, sets;
  uint32_t unique_total = 0, set_total = 0;
  bool unique_unsupported = false, set_unsupported = false;
  std::vector<Mod> mods;
  std::vector<Skill> skills;  // the charged skill affixes ask a skill's levels
  std::vector<int32_t> unfit;  // affixes that only their item types ruled out
  uint64_t print = 0;

  // Whether the chain's step `i` can make the item, for some seed.
  bool possible(int i, uint64_t dropped) const {
    switch (chain[i]) {
      case kUnique: {
        if (unique_unsupported) return true;
        if (uniques.empty()) return always_unique;
        for (size_t u = 0; u < uniques.size(); ++u)
          if (!((dropped >> u) & 1)) return true;
        return false;
      }
      case kSet: return set_unsupported || set_total != 0;
      case kRare: return can_be_rare && rare_names && most_rare > 0;
      case kCrafted: return rare_names;
      case kMagic: return most_magic > 0;
      case kSuperior:
        for (int k = 0; k < superior_n; ++k)
          if (superior_fits[k]) return true;
        return false;
      default: return true;
    }
  }
};

namespace {

// --- building a plan ---

struct Builder {
  const Tables& t;
  Plan& p;
  const uint8_t* item = nullptr;
  const uint8_t* type = nullptr;
  std::vector<uint8_t> kinds;  // by item type: the item counts as it
  uint16_t version = 0;
  int32_t alvl = 1;
  int32_t ilvl = 1;
  int affix_class = 8;
  bool magic_level = false;
  bool sockets_possible = false;

  Builder(const Tables& tables, Plan& plan) : t(tables), p(plan) {}

  const uint8_t* type_row(int i) const {
    return i >= 0 && static_cast<uint32_t>(i) < t.types_count ? t.types.data() + static_cast<size_t>(i) * kTypesRow
                                                                : nullptr;
  }
  void walk(int ty, int depth) {
    if (ty <= 0 || static_cast<uint32_t>(ty) >= t.types_count || depth > 16 || kinds[ty]) return;
    kinds[ty] = 1;
    const uint8_t* r = type_row(ty);
    walk(at<uint16_t>(r, kTypeEquiv1), depth + 1);
    walk(at<uint16_t>(r, kTypeEquiv2), depth + 1);
  }
  bool is_a(int ty) const { return ty > 0 && static_cast<uint32_t>(ty) < kinds.size() && kinds[ty]; }
  // The game's type lists: the first excluded type the item is rules the row out, the first listed one it is
  // lets it in; a list ends at its first empty entry.
  bool listed(const uint8_t* row, uint32_t types_at, int types, uint32_t not_at, int nots) const {
    for (int k = 0; k < nots; ++k) {
      const int16_t e = at<int16_t>(row, not_at + 2 * k);
      if (e <= 0) break;
      if (is_a(e)) return false;
    }
    for (int k = 0; k < types; ++k) {
      const int16_t ty = at<int16_t>(row, types_at + 2 * k);
      if (ty <= 0) break;
      if (is_a(ty)) return true;
    }
    return false;
  }
  int max_sockets() const {
    int m = at<uint8_t>(type, kTypeSockets + 2);
    if (ilvl <= at<uint8_t>(type, kTypeSocketLevels)) m = at<uint8_t>(type, kTypeSockets);
    else if (ilvl <= at<uint8_t>(type, kTypeSocketLevels + 1)) m = at<uint8_t>(type, kTypeSockets + 1);
    const int gems = at<uint8_t>(item, kItemGemSockets);
    return m < gems ? m : gems;
  }
  const uint8_t* prop_row(int32_t prop) const {
    return prop >= 0 && static_cast<uint32_t>(prop) < t.props_count
               ? t.props.data() + static_cast<size_t>(prop) * kPropertyRow
               : nullptr;
  }
  // The generator's test of an affix on an item (0x3D4220): a socket affix only on a base that can have sockets,
  // then the row's item types.
  bool affix_fits(const uint8_t* row) const {
    const uint8_t* mod = row + kAffixMods;
    if (at<int16_t>(mod, 0) >= 0 && at<int16_t>(mod, 2) == 0 && !sockets_possible) {
      const uint8_t* pr = prop_row(at<int32_t>(mod, 0));
      if (pr && at<uint16_t>(pr, kPropStat) == kStatSockets) return false;
    }
    return listed(row, kAffixTypes, 7, kAffixNotTypes, 5);
  }

  Mod compile(const uint8_t* mod) const {
    Mod m;
    m.param = at<int32_t>(mod, 4);
    m.lo = at<int32_t>(mod, 8);
    m.hi = at<int32_t>(mod, 12);
    const int16_t kind = at<int16_t>(mod, 2);
    if (kind == 1) {
      m.unsupported = true;  // a property group: picks of its own
      return m;
    }
    if (kind != 0) return m;
    const uint8_t* pr = prop_row(at<int32_t>(mod, 0));
    if (!pr) return m;
    for (int k = 0; k < kPropSlots; ++k) {
      const uint8_t f = at<uint8_t>(pr, kPropFunc + k);
      if (f == 0 || f >= kPropFunctions || (f >= 26 && f <= 35)) break;  // the dispatcher stops at an empty entry
      if (f == 25) m.unsupported = true;
      m.fn[m.count] = f;
      m.val[m.count] = at<uint16_t>(pr, kPropVal + 2 * k);
      const uint16_t stat = at<uint16_t>(pr, kPropStat + 2 * k);
      if (f == 2 && (stat == kStatArmorPercent || stat == kStatArmor)) m.top_defense = true;
      ++m.count;
    }
    return m;
  }
  // A row's mods: an affix's stop at its first empty one; a unique's and a set item's are each looked at.
  void compile_mods(const uint8_t* first, int count, bool stop_at_empty, Row* row) {
    row->mods_at = static_cast<uint16_t>(p.mods.size());
    for (int k = 0; k < count; ++k) {
      const uint8_t* mod = first + static_cast<size_t>(k) * kModSize;
      if (at<int16_t>(mod, 0) < 0) {
        if (stop_at_empty) break;
        continue;
      }
      p.mods.push_back(compile(mod));
    }
    row->mods_count = static_cast<uint16_t>(p.mods.size() - row->mods_at);
  }

  // The candidates of one side, as the picker collects them for every pick, but for the groups the item has.
  void collect(uint32_t first, uint32_t last, bool rare_only, bool spawnable_only, int32_t only_group, Side* out) {
    for (uint32_t i = first; i < last && i < t.affix_count; ++i) {
      const uint8_t* r = t.affix.data() + static_cast<size_t>(i) * kAffixRow;
      if (spawnable_only && !at<uint8_t>(r, kAffixSpawnable)) continue;
      if (at<uint16_t>(r, kAffixVersion) >= 100 && version < 100) continue;
      const int32_t level = at<int32_t>(r, kAffixLevel), max_level = at<int32_t>(r, kAffixMaxLevel);
      if (!(level <= alvl && (max_level == 0 || alvl <= max_level))) continue;
      if (rare_only && !at<uint8_t>(r, kAffixRare)) continue;
      const bool fits = affix_fits(r);
      if (only_group && at<int32_t>(r, kAffixGroup) != only_group) continue;
      const uint8_t frequency = at<uint8_t>(r, kAffixFrequency);
      if (!frequency) continue;
      const uint8_t cls = at<uint8_t>(r, kAffixClass);
      if (cls != 0xFF && affix_class != 8 && cls != affix_class) continue;
      if (!fits) {
        if (!rare_only) p.unfit.push_back(static_cast<int32_t>(i) + 1);
        continue;
      }
      const int64_t weight = static_cast<int64_t>(magic_level ? level : 1) * frequency;
      if (weight < 0 || weight > 0xFFFFFFFFll - out->total) {
        out->overflow = true;  // the picker gives up on such a table
        return;
      }
      Row row;
      row.id = static_cast<int32_t>(i) + 1;
      row.weight = static_cast<uint32_t>(weight);
      row.group = at<int32_t>(r, kAffixGroup);
      int g = 0;
      while (g < out->groups && out->group_id[g] != row.group) ++g;
      if (g == out->groups) {
        if (out->groups == kMaxGroups) {
          out->overflow = true;
          return;
        }
        out->group_id[out->groups++] = row.group;
      }
      row.group_bit = static_cast<uint8_t>(g);
      out->group_total[g] += row.weight;
      out->total += row.weight;
      compile_mods(r + kAffixMods, 3, true, &row);
      out->rows.push_back(row);
    }
  }

  // What a row gives, to tell the versions of one affix from each other: per function the stat (or the function,
  // when it has no stat), the value a class skill property names its class by, and the skill or tab the param
  // names; and how much: the range's top and bottom, as magnitudes.
  struct Part {
    uint32_t stat, val;
    int32_t layer, lo, hi;
  };
  static int32_t magnitude(int32_t v) { return v == INT32_MIN ? INT32_MAX : v < 0 ? -v : v; }
  bool parts_of(const Row& row, const uint8_t* affix, std::vector<Part>* out) const {
    out->clear();
    for (int k = 0; k < 3; ++k) {
      const uint8_t* mod = affix + kAffixMods + static_cast<size_t>(k) * kModSize;
      const int32_t prop = at<int32_t>(mod, 0);
      if (prop < 0) continue;
      const uint8_t* pr = prop_row(prop);
      if (!pr) return false;
      const int32_t param = at<int32_t>(mod, 4), lo = at<int32_t>(mod, 8), hi = at<int32_t>(mod, 12);
      bool amount = false;
      for (int j = 0; j < kPropSlots; ++j) {
        const uint8_t f = at<uint8_t>(pr, kPropFunc + j);
        amount = amount || (f >= 14 && f <= 17);
      }
      for (int j = 0; j < kPropSlots; ++j) {
        const uint8_t f = at<uint8_t>(pr, kPropFunc + j);
        if (!f) continue;
        int32_t a = lo, b = hi;
        if ((f == 14 || f == 17) && param) a = b = param;
        else if (f == 15) b = lo;
        else if (f == 16) a = hi;
        a = magnitude(a);
        b = magnitude(b);
        const uint16_t stat = at<uint16_t>(pr, kPropStat + 2 * j);
        Part part;
        part.stat = stat == 0xFFFF ? 0x10000u | f : stat;
        part.val = at<uint16_t>(pr, kPropVal + 2 * j);
        part.layer = amount ? 0 : param;
        part.lo = a < b ? a : b;
        part.hi = a < b ? b : a;
        out->push_back(part);
      }
    }
    (void)row;
    std::sort(out->begin(), out->end(), [](const Part& a, const Part& b) {
      if (a.stat != b.stat) return a.stat < b.stat;
      if (a.val != b.val) return a.val < b.val;
      return a.layer < b.layer;
    });
    return true;
  }
  static bool same_stats(const std::vector<Part>& a, const std::vector<Part>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
      if (a[i].stat != b[i].stat || a[i].val != b[i].val || a[i].layer != b[i].layer) return false;
    return true;
  }
  // Among the candidates, the rows of one group and class that give the same stats are one affix in its versions;
  // the top one gives the most (each stat's top against the most any version gives, then the bottoms).
  void mark_tops(Side* side) {
    const size_t n = side->rows.size();
    std::vector<std::vector<Part>> parts(n);
    std::vector<uint8_t> known(n), cls(n);
    for (size_t i = 0; i < n; ++i) {
      const uint8_t* r = t.affix.data() + static_cast<size_t>(side->rows[i].id - 1) * kAffixRow;
      known[i] = parts_of(side->rows[i], r, &parts[i]) && !parts[i].empty();
      cls[i] = at<uint8_t>(r, kAffixClass);
    }
    std::vector<uint8_t> done(n);
    for (size_t i = 0; i < n; ++i) {
      if (done[i]) continue;
      std::vector<size_t> family{i};
      if (known[i])
        for (size_t j = i + 1; j < n; ++j)
          if (!done[j] && known[j] && side->rows[j].group == side->rows[i].group && cls[j] == cls[i] &&
              same_stats(parts[j], parts[i]))
            family.push_back(j);
      std::vector<int32_t> most(parts[i].size(), 0);
      for (const size_t f : family)
        for (size_t k = 0; k < parts[f].size(); ++k) most[k] = std::max(most[k], parts[f][k].hi);
      size_t best = i;
      double best_tops = -1, best_bottoms = -1;
      for (const size_t f : family) {
        double tops = 0, bottoms = 0;
        for (size_t k = 0; k < parts[f].size(); ++k) {
          const double m = most[k] > 0 ? most[k] : 1;
          tops += parts[f][k].hi / m;
          bottoms += parts[f][k].lo / m;
        }
        constexpr double kEpsilon = 1e-9;
        if (tops > best_tops + kEpsilon || (tops > best_tops - kEpsilon && bottoms > best_bottoms + kEpsilon)) {
          best = f;
          best_tops = tops;
          best_bottoms = bottoms;
        }
        done[f] = 1;
      }
      // Versions that give exactly as much as the best are as good.
      for (const size_t f : family) {
        double tops = 0, bottoms = 0;
        for (size_t k = 0; k < parts[f].size(); ++k) {
          const double m = most[k] > 0 ? most[k] : 1;
          tops += parts[f][k].hi / m;
          bottoms += parts[f][k].lo / m;
        }
        side->rows[f].top = f == best || (tops > best_tops - 1e-9 && bottoms > best_bottoms - 1e-9);
      }
    }
  }
  void link_sides(Side* a, const Side& b) {
    for (Row& r : a->rows)
      for (int g = 0; g < b.groups; ++g)
        if (b.group_id[g] == r.group) r.other_bit = static_cast<uint8_t>(g);
  }

  bool ladder_ok(int16_t first, int16_t last) const {
    const Request& q = p.request;
    return !(q.game_type == 0 && first != 0 && last >= 0 && (q.game_ladder == 0 || first > 0));
  }
  // The unique or set items of the base the generator picks among, by their rarity.
  void collect_named(const std::vector<uint8_t>& rows, uint32_t count, uint32_t row_size, bool unique,
                     std::vector<Row>* out, uint32_t* total, bool* unsupported) {
    const uint32_t code = at<uint32_t>(item, kItemCode);
    for (uint32_t i = 0; i < count; ++i) {
      const uint8_t* r = rows.data() + static_cast<size_t>(i) * row_size;
      if (at<uint32_t>(r, unique ? kUniqueCode : kSetCode) != code) continue;
      if (at<uint16_t>(r, unique ? kUniqueVersion : kSetVersion) >= 100 && version < 100) continue;
      const uint32_t flags = at<uint32_t>(r, unique ? kUniqueFlags : kSetFlags);
      if (flags & kFlagDisabled) continue;
      const uint32_t ladder = unique ? kUniqueLadder : kSetLadder;
      if (!ladder_ok(at<int16_t>(r, ladder), at<int16_t>(r, ladder + 2))) continue;
      if (at<int16_t>(r, unique ? kUniqueLevel : kSetLevel) > ilvl) continue;
      if (!unique && at<int16_t>(r, kSetSet) == kSetByRequest && !(p.request.flags & 1)) continue;
      if (!(flags & kFlagSpawnable)) continue;
      if (at<int32_t>(r, unique ? kUniqueCalc : kSetCalc) != -1) {
        *unsupported = true;  // a drop condition the source decides
        return;
      }
      Row row;
      row.id = static_cast<int32_t>(i);
      int32_t weight = unique ? at<int16_t>(r, kUniqueRarity) : at<int32_t>(r, kSetRarity);
      if (unique ? weight < 1 : weight == 0) weight = 1;
      if (weight < 0) {
        *unsupported = true;
        return;
      }
      row.weight = static_cast<uint32_t>(weight);
      if (unique) {
        compile_mods(r + kUniqueMods, kUniqueModCount, false, &row);
      } else {
        compile_mods(r + kSetMods, kSetModCount, false, &row);
        Row bonus;
        compile_mods(r + kSetBonusMods, kSetBonusCount, false, &bonus);
        row.mods_count = static_cast<uint16_t>(row.mods_count + bonus.mods_count);
      }
      *total += row.weight;
      out->push_back(row);
    }
  }

  const char* build(const Request& request) {
    p.request = request;
    if (!t.complete()) return "the game's tables are not read";
    if (request.item_class >= t.items_count) return "an item class outside the table";
    if (request.version < 100) return "a classic item";
    if (request.quality < kNormal || request.quality > kCrafted) return "a quality the seed rolls, or a special one";
    item = t.items.data() + static_cast<size_t>(request.item_class) * kItemsRow;
    const int ty = at<int16_t>(item, kItemType), ty2 = at<int16_t>(item, kItemType2);
    type = type_row(ty);
    if (!type) return "an item without an item type";
    kinds.assign(t.types_count, 0);
    walk(ty, 0);
    if (ty2 > 0) walk(ty2, 0);
    version = request.version;
    ilvl = request.ilvl < 1 ? 1 : request.ilvl;
    p.ilvl = ilvl;
    p.skills = t.skills;
    p.quest = at<uint8_t>(item, kItemQuest) != 0;
    if (p.quest) return "a quest item";
    // The affix level, as the picker works it out.
    const int base = at<uint8_t>(item, kItemLevel), magic = at<uint8_t>(item, kItemMagicLevel);
    const int level = ilvl < base ? base : ilvl;
    int a = magic ? level + magic : level < 99 - base / 2 ? level - base / 2 : 2 * level - 99;
    alvl = a < 1 ? 1 : a > 99 ? 99 : a;
    magic_level = magic != 0;
    const int cls = at<uint8_t>(type, kTypeClass);
    affix_class = cls < 8 ? cls : 8;
    sockets_possible = at<uint8_t>(item, kItemHasInv) != 0 && max_sockets() != 0;
    // The generator's "must be" fixes.
    int32_t q = request.quality;
    if (at<uint8_t>(type, kTypeMagic) && q < kMagic) q = kMagic;
    p.can_be_rare = at<uint8_t>(type, kTypeRare) != 0;
    if (!p.can_be_rare && q == kRare) q = kMagic;
    p.always_unique = at<uint8_t>(item, kItemUnique) != 0;
    if (p.always_unique) q = kUnique;
    if (at<uint8_t>(type, kTypeNormal)) q = kNormal;
    p.quality = q;
    static const int32_t kChains[9][6] = {{},        {},        {2},          {3, 2},          {4, 3, 2},
                                          {5, 4, 3, 2}, {6, 4, 3, 2}, {7, 6, 4, 3, 2}, {8, 2}};
    static const int kLengths[9] = {0, 0, 1, 2, 3, 4, 4, 5, 2};
    p.chain_length = kLengths[q];
    for (int i = 0; i < p.chain_length; ++i) p.chain[i] = kChains[q][i];
    p.jewel = ty == kTypeJewel;
    for (const int special : kSpecialTypes) p.special_type = p.special_type || is_a(special);
    // Ethereal: a weapon or armor that has durability (an indestructible affix still rules it out, per seed).
    p.durable_base = (is_a(kTypeWeapon) || is_a(kTypeArmor)) && !at<uint8_t>(item, kItemNoDurability) &&
                     at<uint8_t>(item, kItemDurability) != 0;
    p.ethereal_base = !(request.flags & kNeverEthereal) && p.durable_base;
    // Sockets: a base with an inventory picture for them, not stackable, by its type, level and difficulty.
    if (at<uint8_t>(item, kItemHasInv) && !at<uint8_t>(item, kItemStackable) && max_sockets() > 0) {
      const int most = max_sockets();
      const int cap = request.difficulty == 0 ? 3 : request.difficulty == 1 ? 4 : request.difficulty == 2 ? 6 : most;
      p.socket_n = most < cap ? most : cap;
    }
    p.auto_group = at<uint16_t>(item, kItemAutoPrefix);
    // The rare names: only whether each list has any for the item (a pick is one step then).
    for (uint32_t i = 0; i < t.rare_suffix_count + t.rare_prefix_count; ++i) {
      const uint8_t* r = t.rare_names.data() + static_cast<size_t>(i) * kRareNameRow;
      if (at<uint16_t>(r, kNameVersion) >= 100 && version < 100) continue;
      if (!listed(r, kNameTypes, 7, kNameNotTypes, 4)) continue;
      ++(i < t.rare_suffix_count ? p.rare_suffix_names : p.rare_prefix_names);
    }
    p.rare_names = p.rare_prefix_names > 0 && p.rare_suffix_names > 0;
    // The class skill step.
    const int staff = at<uint8_t>(type, kTypeStaffMods);
    p.skill_class = staff < 8 ? staff : 8;
    if (p.skill_class < 8) {
      int first = -1, count = 0;
      for (size_t i = 0; i < t.skills.size(); ++i)
        if (t.skills[i].char_class == p.skill_class) {
          if (first < 0) first = static_cast<int>(i);
          ++count;
        }
      if (count < 1) p.skill_class = 8;
      else {
        p.skill_first = first;
        p.skills_new_mode = request.game_bank == 3;
        if (!p.skills_new_mode) return "class skills by the old rule";
        static const int kLevels[6] = {1, 6, 12, 18, 24, 30};
        for (int s = first; s < first + count && static_cast<size_t>(s) < t.skills.size(); ++s)
          for (int k = 0; k < 6; ++k)
            if (t.skills[s].req_level == kLevels[k]) {
              p.tiers[k].skills.push_back(static_cast<int16_t>(s));
              p.tiers[k].usable.push_back(t.skills[s].item_type < 1 || is_a(t.skills[s].item_type));
            }
      }
    }
    // The superior kinds.
    p.superior_n = static_cast<int32_t>(t.quality_count);
    if (at<uint8_t>(type, kTypeThrowable) || at<uint8_t>(item, kItemNoDurability)) p.superior_n = 4;
    if (p.superior_n > 10 || static_cast<uint32_t>(p.superior_n) > t.quality_count) return "the superior kinds";
    static const struct {
      int flag, type;
    } kKinds[9] = {{2, 2}, {3, 0x18}, {4, 0x19}, {5, 0x1A}, {6, 0x1B}, {6, 0x23}, {7, 0x0F}, {8, 0x10}, {9, 0x13}};
    for (int i = 0; i < p.superior_n; ++i) {
      const uint8_t* r = t.quality.data() + static_cast<size_t>(i) * kQualityRow;
      bool fits = false;
      if (at<uint8_t>(r, 1) && is_a(kTypeWeapon) && !(ty >= 0x18 && ty <= 0x1B) && ty != 0x23) fits = true;
      else if (at<uint8_t>(r, 0) && is_a(kTypeArmor) && (ty > 0x13 || !((0x98004u >> ty) & 1))) fits = true;
      else
        for (const auto& k : kKinds) fits = fits || (ty == k.type && at<uint8_t>(r, k.flag));
      p.superior_fits[i] = fits;
      Row row;
      row.id = i;
      compile_mods(r + kQualityMods, kQualityModCount, true, &row);
      p.superior.push_back(row);
    }
    // The affixes.
    for (int rare = 0; rare < 2; ++rare) {
      collect(t.prefix_at, t.auto_at, rare != 0, true, 0, &p.side[0][rare]);
      collect(t.suffix_at, t.prefix_at, rare != 0, true, 0, &p.side[1][rare]);
      if (p.side[0][rare].overflow || p.side[1][rare].overflow) return "an affix table the picker gives up on";
      link_sides(&p.side[0][rare], p.side[1][rare]);
      link_sides(&p.side[1][rare], p.side[0][rare]);
      mark_tops(&p.side[0][rare]);
      mark_tops(&p.side[1][rare]);
      if (p.auto_group) {
        collect(t.auto_at, t.affix_count, rare != 0, false, p.auto_group, &p.autos[rare]);
        if (p.autos[rare].overflow) return "an automagic table the picker gives up on";
        mark_tops(&p.autos[rare]);
      }
    }
    const auto sides = [](const Side& a, const Side& b) {
      return (a.groups < 3 ? a.groups : 3) + (b.groups < 3 ? b.groups : 3);
    };
    p.most_magic = (p.side[0][0].rows.empty() ? 0 : 1) + (p.side[1][0].rows.empty() ? 0 : 1);
    p.most_rare = std::min(p.jewel ? 4 : 6, sides(p.side[0][1], p.side[1][1]));
    p.most_crafted = std::min(4, sides(p.side[0][1], p.side[1][1]));
    collect_named(t.unique, t.unique_count, kUniqueRow, true, &p.uniques, &p.unique_total, &p.unique_unsupported);
    collect_named(t.sets, t.sets_count, kSetItemRow, false, &p.sets, &p.set_total, &p.set_unsupported);
    if (p.uniques.size() > 64) return "more unique items of one base than the model keeps";
    return nullptr;
  }
};

uint64_t fnv(uint64_t h, const void* data, size_t n) {
  const uint8_t* b = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 0x100000001B3ull;
  return h;
}
template <typename T>
uint64_t fnv(uint64_t h, const T& v) {
  return fnv(h, &v, sizeof(T));
}

// Everything a seed's outcome depends on (not the item level as such: what it lets in), and the model's version:
// seeds kept from one session to the next go by it.
uint64_t print_of(const Plan& p) {
  uint64_t h = 0xCBF29CE484222325ull;
  h = fnv(h, kModelVersion);
  h = fnv(h, p.request.item_class);
  h = fnv(h, p.request.flags);
  h = fnv(h, p.request.version);
  h = fnv(h, p.quality);
  h = fnv(h, p.socket_n);
  h = fnv(h, p.ethereal_base);
  h = fnv(h, p.rare_prefix_names);
  h = fnv(h, p.rare_suffix_names);
  h = fnv(h, p.skill_class);
  h = fnv(h, p.request.difficulty);
  h = fnv(h, p.request.game_bank);
  bool charged = false;
  for (const Mod& m : p.mods)
    for (int k = 0; k < m.count; ++k) charged = charged || m.fn[k] == 19;
  if (charged) h = fnv(h, p.ilvl);  // a charged skill's level and charges come from the item level
  if (p.skill_class < 8) {
    // The class skill step reads the item level itself.
    h = fnv(h, p.request.ilvl);
    h = fnv(h, p.request.flags & kSkillBonus);
  }
  if (p.quality == kCrafted) h = fnv(h, p.request.ilvl > 0x46 ? 4 : p.request.ilvl > 0x32 ? 3 : p.request.ilvl > 0x1E ? 2 : 1);
  const auto rows = [&h](const std::vector<Row>& v) {
    for (const Row& r : v) {
      h = fnv(h, r.id);
      h = fnv(h, r.weight);
      h = fnv(h, r.group);
      h = fnv(h, r.top);
      h = fnv(h, r.mods_at);
      h = fnv(h, r.mods_count);
    }
    h = fnv(h, v.size());
  };
  for (int s = 0; s < 2; ++s)
    for (int r = 0; r < 2; ++r) rows(p.side[s][r].rows);
  rows(p.autos[0].rows);
  rows(p.autos[1].rows);
  rows(p.uniques);
  rows(p.sets);
  for (int i = 0; i < p.superior_n; ++i) h = fnv(h, p.superior_fits[i]);
  rows(p.superior);
  for (const Mod& m : p.mods) {
    h = fnv(h, m.param);
    h = fnv(h, m.lo);
    h = fnv(h, m.hi);
    h = fnv(h, m.fn, sizeof(m.fn));
    h = fnv(h, m.val, sizeof(m.val));
    h = fnv(h, m.count);
    h = fnv(h, m.unsupported);
    h = fnv(h, m.top_defense);
  }
  if (p.skill_class < 8) {
    h = fnv(h, p.skills_new_mode);
    h = fnv(h, p.skill_first);
    for (const SkillTier& tier : p.tiers) {
      for (const int16_t skill : tier.skills) h = fnv(h, skill);
      for (const uint8_t usable : tier.usable) h = fnv(h, usable);
      h = fnv(h, tier.skills.size());
    }
  }
  h = fnv(h, p.jewel);
  h = fnv(h, p.can_be_rare);
  h = fnv(h, p.always_unique);
  h = fnv(h, p.special_type);
  h = fnv(h, p.auto_group);
  h = fnv(h, p.chain_length);
  // A charged skill's charges come from the skill's levels.
  if (charged)
    for (const Skill& k : p.skills) {
      h = fnv(h, k.req_level);
      h = fnv(h, k.max_level);
    }
  return h;
}

// --- following the generator ---

struct Run {
  const Plan& p;
  const Search& s;
  const bool strict;         // give up at the first thing wanted that is not there
  const bool strict_values;  // ... at the first value below its top
  Rng rng;
  uint32_t seed = 0;
  int32_t quality = 0;
  int32_t prefix[3] = {}, suffix[3] = {};
  int prefixes = 0, suffixes = 0;
  uint64_t have[2] = {};  // the groups the item has, among the prefix side's and the suffix side's
  int32_t file_index = -1;
  bool indestructible = false;
  bool top_defense = false;   // a property of the item's gives an armor its top base defense plus one
  bool ethereal_mod = false;  // a property of the item's makes it ethereal (0x3CCC60), whatever the roll
  bool socket_mod = false;    // ... or gives it sockets (0x3D14E0)
  bool dead = false;         // strict: not everything wanted
  bool lost = false;         // the model cannot follow this seed
  bool all = true;
  int skill_lines = 0;
  // What is wanted and what is there, for the score.
  double values = 0;
  int value_count = 0;
  int picks = 0, tops = 0;
  int made = 0, most = 0;

  int wanted = 0;  // the first step of the chain that can make the item

  Run(const Plan& plan, const Search& search, Tier tier)
      : p(plan), s(search), strict(tier != Tier::kAllWeighed), strict_values(tier == Tier::kEverything) {
    while (wanted + 1 < p.chain_length && !p.possible(wanted, s.dropped)) ++wanted;
  }

  void miss() {
    all = false;
    if (strict) dead = true;
  }
  void miss_value() {
    all = false;
    if (strict_values) dead = true;
  }
  // A value between the row's min and max (0x3D5860): no step when they are the same.
  int32_t value(int32_t a, int32_t b) {
    if (a == b) return a;
    const int32_t lo = a < b ? a : b, hi = a < b ? b : a;
    const int64_t n = static_cast<int64_t>(hi) - lo + 1;
    if (n > INT32_MAX) {
      lost = true;
      return lo;
    }
    const int32_t v = lo + static_cast<int32_t>(rng.roll(static_cast<int32_t>(n)));
    if (s.wants.perfect) {
      values += static_cast<double>(v - lo) / static_cast<double>(hi - lo);
      ++value_count;
      if (v != hi) miss_value();
    }
    return v;
  }
  // A charged skill's charges (D2RCore): the skill's level (the row's, or worked out from the item level and the
  // skill's required level), the most charges (the row's, or from the level), then one step for how many are left.
  int32_t charges(const Mod& m) {
    if (m.param < 0 || static_cast<size_t>(m.param) >= p.skills.size()) {
      lost = true;
      return 0;
    }
    const Skill& skill = p.skills[m.param];
    int32_t level = m.hi;
    if (level < 1) {
      const int32_t req = skill.req_level, gap = p.ilvl - req;
      if (level == 0) {
        const int32_t cap = skill.max_level > 0 ? skill.max_level : 20;
        const int32_t t = ((gap < 0 ? gap + 3 : gap) >> 2) + 1;
        level = gap > -4 ? (t < cap ? t : cap) : 1;
      } else {
        const uint32_t span = 99 - req < 2 ? 1u : static_cast<uint32_t>(99 - req);
        const uint32_t per = static_cast<uint32_t>(-level);
        const int32_t step = span < per ? 1 : static_cast<int32_t>(span / per);
        const int32_t v = gap / step;
        level = v > 1 ? v : 1;
      }
    }
    int32_t n = m.lo;
    if (n == 0) n = 5;
    else {
      if (n < 0) {
        int32_t t = -(level * n);
        if (t < 0) t = 7 - level * n;
        n = (t >> 3) - n;
      }
      n = n < 1 ? 1 : n > 0xFF ? 0xFF : n;
    }
    const int32_t least = n >> 3;
    const uint32_t r = rng.step() % static_cast<uint32_t>(n - least);
    const int32_t left = static_cast<int32_t>(r) + least + 1;
    if (s.wants.perfect && n - least > 1) {
      values += static_cast<double>(r) / static_cast<double>(n - least - 1);
      ++value_count;
      if (left != n) miss_value();
    }
    return left;
  }
  void apply(const Mod& m) {
    if (m.unsupported) {
      lost = true;
      return;
    }
    int32_t prev = 0;
    if (m.top_defense) top_defense = true;
    for (int k = 0; k < m.count && !dead && !lost; ++k) {
      int32_t v = 0;
      switch (m.fn[k]) {
        case 1: case 2: case 13: case 21: case 22:
          v = value(m.lo, m.hi);
          break;
        case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 24:
          v = prev ? prev : value(m.lo, m.hi);
          break;
        case 11: case 18:
          v = m.fn[k] == 11 ? 1 : 0;
          break;
        case 23:
          ethereal_mod = true;
          break;
        case 12: case 36:  // a skill or a class picked by the roll: any will do
          if (m.lo != m.hi) {
            const int32_t lo = m.lo < m.hi ? m.lo : m.hi, hi = m.lo < m.hi ? m.hi : m.lo;
            rng.roll(hi - lo + 1);
          }
          v = m.fn[k] == 12 ? m.param : m.val[k];
          break;
        case 14:
          v = prev;
          if (prev < 1) {
            v = value(m.lo, m.hi);
            if (v < 1) v = m.param;
          }
          socket_mod = true;
          break;
        case 15: v = m.lo; break;
        case 16: v = m.hi; break;
        case 17:
          v = m.param ? m.param : value(m.lo, m.hi);
          break;
        case 19: v = charges(m); break;
        case 20:
          indestructible = true;
          v = 1;
          break;
        default:
          lost = true;
          break;
      }
      if (k == 0) prev = v;
    }
  }
  void apply(const Row& row) {
    for (int k = 0; k < row.mods_count && !dead && !lost; ++k) apply(p.mods[row.mods_at + k]);
  }
  const Row* affix(int32_t id, int side, int rare) const {
    for (const Row& r : p.side[side][rare].rows)
      if (r.id == id) return &r;
    return nullptr;
  }

  // The picker: a coin (which decides only when a pick is not a must), the candidates the item's groups leave,
  // a roll of their weights plus one. 0 when it picks nothing.
  const Row* pick(const Side& side, uint64_t excluded, bool must, bool wanted) {
    const bool coin = (rng.step() & 1) != 0;
    if (!coin && !must) return nullptr;
    uint32_t total = side.total;
    for (int g = 0; g < side.groups; ++g)
      if ((excluded >> g) & 1) total -= side.group_total[g];
    const Row* last = nullptr;
    // An empty list, or weights that add up to the most the picker counts, pick nothing.
    if (side.rows.empty() || total == 0xFFFFFFFFu) return nullptr;
    bool any = false;
    for (const Row& r : side.rows)
      if (!((excluded >> r.group_bit) & 1)) {
        any = true;
        break;
      }
    if (!any) return nullptr;
    uint32_t r = rng.step() % (total + 1);
    const Row* picked = nullptr;
    for (const Row& row : side.rows) {
      if ((excluded >> row.group_bit) & 1) continue;
      last = &row;
      if (r < row.weight) {
        picked = &row;
        break;
      }
      r -= row.weight;
    }
    if (!picked) picked = last;
    if (wanted && s.wants.best) {
      ++picks;
      if (picked->top) ++tops;
      else miss();
    }
    return picked;
  }
  const Row* pick_affix(bool is_prefix, bool must) {
    const int rare = quality == kRare || quality == kCrafted || quality == 9 ? 1 : 0;
    const int side = is_prefix ? 0 : 1;
    const Row* row = pick(p.side[side][rare], have[side], must, true);
    if (!row) return nullptr;
    have[side] |= 1ull << row->group_bit;
    if (row->other_bit != 0xFF) have[1 - side] |= 1ull << row->other_bit;
    (is_prefix ? prefix[prefixes++] : suffix[suffixes++]) = row->id;
    return row;
  }

  void class_skills() {
    if (p.skill_class >= 8 || dead || lost) return;
    const int32_t ilvl = p.request.ilvl < 1 ? 1 : p.request.ilvl;
    const int32_t bonus = p.request.flags & kSkillBonus ? p.request.ilvl : 0;
    const int32_t r = static_cast<int32_t>(rng.roll(100)) + bonus;
    int lines = 3;
    if (r < 0x5B) {
      if (r < 0x47) {
        if (r < 0x1F && bonus == 0) lines = 0;
        else lines = 1;
      } else {
        lines = 2;
      }
    }
    if (s.wants.perfect) {
      values += lines / 3.0;
      ++value_count;
      if (lines != 3) miss_value();
    }
    if (!lines || dead) return;
    int base = 5;
    if (ilvl < 0x25) base = ilvl > 0x18 ? 4 : ilvl < 0x13 ? (ilvl > 0x0B ? 2 : 1) : 3;
    int chosen[3] = {-1, -1, -1};
    for (int line = 0; line < lines && !dead; ++line) {
      const int32_t t = static_cast<int32_t>(rng.roll(100));
      int tier = t < 0x51 ? (t < 0x1F ? (t < 0x0B ? base - 2 : base - 1) : base) : base + 1;
      if (tier < 1) tier = 1;
      if (quality == kInferior && tier > 4) tier = 4;
      if (tier > 6) {
        lost = true;
        return;
      }
      const SkillTier& list = p.tiers[tier - 1];
      int found = -1;
      for (int tries = 6; tries > 0 && !list.skills.empty(); --tries) {
        const uint32_t i = rng.roll(static_cast<int32_t>(list.skills.size()));
        const int skill = list.skills[i];
        if (list.usable[i] && skill != chosen[0] && skill != chosen[1] && skill != chosen[2]) {
          found = skill;
          break;
        }
      }
      if (found < 0) {
        if (s.wants.perfect) {
          ++value_count;
          miss_value();
        }
        continue;
      }
      chosen[line] = found;
      int level = 1;
      if (quality != kInferior) {
        const int32_t l = static_cast<int32_t>(rng.roll(100)) + bonus / 2;
        level = l >= 0x5A ? 3 : l > 0x3B ? 2 : 1;
      }
      ++skill_lines;
      if (s.wants.perfect) {
        values += (level - 1) / 2.0;
        ++value_count;
        if (level != 3) miss_value();
      }
    }
  }

  bool normal_step() {
    if (p.special_type) {
      lost = true;
      return false;
    }
    class_skills();
    return true;
  }
  bool superior_step() {
    const int32_t n = p.superior_n;
    uint8_t tried[10] = {};
    int32_t r = 0;
    for (;;) {
      do r = static_cast<int32_t>(rng.roll(n));
      while (tried[r]);
      if (p.superior_fits[r]) break;
      tried[r] = 1;
      bool left = false;
      for (int i = 0; i < n; ++i) left = left || !tried[i];
      if (!left) return false;
    }
    file_index = r;
    apply(p.superior[r]);
    class_skills();
    return true;
  }
  bool magic_step() {
    const Row* pre = pick_affix(true, false);
    if (pre) apply(*pre);
    if (dead || lost) return true;
    const Row* suf = pick_affix(false, pre == nullptr);
    if (suf) apply(*suf);
    if (!pre && !suf) return false;
    if (s.wants.most) {
      most = p.most_magic;
      made = (pre ? 1 : 0) + (suf ? 1 : 0);
      if (made != most) miss();
    }
    class_skills();
    return true;
  }
  bool names() {
    if (p.rare_prefix_names) rng.step();
    if (p.rare_suffix_names) rng.step();
    return p.rare_names;
  }
  void apply_slots() {
    const int rare = 1;
    for (int k = 0; k < 3 && !dead && !lost; ++k) {
      if (prefix[k])
        if (const Row* r = affix(prefix[k], 0, rare)) apply(*r);
      if (suffix[k] && !dead && !lost)
        if (const Row* r = affix(suffix[k], 1, rare)) apply(*r);
    }
  }
  bool rare_step() {
    if (!p.can_be_rare) return false;
    if (!names()) return false;
    static const int kCounts[8] = {3, 4, 4, 5, 5, 5, 6, 6};
    const int count = p.jewel ? static_cast<int>(rng.step() & 1) + 3 : kCounts[rng.step() & 7];
    const int top = p.most_rare;
    if (s.wants.most) {
      most = top;
      if (count < top) miss();
      if (dead) return true;
    }
    bool prefix_closed = false, suffix_closed = false;
    for (int i = 0; i < count && !dead && !lost; ++i) {
      bool is_prefix = true;
      if (prefix_closed) {
        if (suffix_closed) break;
        is_prefix = false;
      } else if (!suffix_closed && (rng.step() & 1)) {
        is_prefix = false;
      }
      const Row* row = pick_affix(is_prefix, true);
      if (!row) {
        (is_prefix ? prefix_closed : suffix_closed) = true;
        --i;
      } else if ((is_prefix ? prefixes : suffixes) > 2) {
        (is_prefix ? prefix_closed : suffix_closed) = true;
      }
    }
    if (dead || lost) return true;
    if (!prefixes && !suffixes) return false;
    if (s.wants.most) {
      made = prefixes + suffixes;
      if (made < top) miss();
    }
    apply_slots();
    class_skills();
    return true;
  }
  bool crafted_step() {
    if (!names()) return false;
    const int32_t ilvl = p.request.ilvl;
    int least = ilvl > 0x1E ? 2 : 1;
    if (ilvl > 0x32) least = 3;
    if (ilvl > 0x46) least = 4;
    const int rolled = static_cast<int>(rng.roll(5));
    const int count = rolled > least ? rolled : least;
    if (s.wants.most) {
      most = p.most_crafted;
      if (count < most) miss();
      if (dead) return true;
    }
    for (int i = 0; i < count && !dead && !lost; ++i) {
      const bool coin = (rng.step() & 1) != 0;
      const bool is_prefix = !(prefixes == 3 || (suffixes != 3 && coin));
      pick_affix(is_prefix, true);
    }
    if (dead || lost) return true;
    if (s.wants.most) {
      made = prefixes + suffixes;
      if (made < most) miss();
    }
    apply_slots();
    class_skills();
    return true;
  }
  bool unique_step() {
    if (p.unique_unsupported) {
      lost = true;
      return false;
    }
    if (p.uniques.empty()) return p.always_unique;
    uint32_t r = rng.roll(static_cast<int32_t>(p.unique_total));
    size_t pick = p.uniques.size() - 1;
    uint32_t sum = 0;
    for (size_t i = 0; i < p.uniques.size(); ++i) {
      sum += p.uniques[i].weight;
      if (r < sum) {
        pick = i;
        break;
      }
    }
    if ((s.dropped >> pick) & 1) return false;
    file_index = p.uniques[pick].id;
    apply(p.uniques[pick]);
    return true;
  }
  bool set_step() {
    if (p.set_unsupported) {
      lost = true;
      return false;
    }
    if (!p.set_total) return false;
    uint32_t r = rng.roll(static_cast<int32_t>(p.set_total));
    for (const Row& row : p.sets) {
      if (r < row.weight) {
        file_index = row.id;
        apply(row);
        return true;
      }
      r -= row.weight;
    }
    return false;
  }

  void restart(int32_t q) {
    rng = Rng(seed);
    quality = q;
    for (int i = 0; i < 3; ++i) prefix[i] = suffix[i] = 0;
    prefixes = suffixes = 0;
    have[0] = have[1] = 0;
    file_index = -1;
    indestructible = false;
    ethereal_mod = socket_mod = top_defense = false;
    skill_lines = 0;
    values = 0;
    value_count = picks = tops = made = most = 0;
    all = true;
  }

  Outcome run(uint32_t item_seed) {
    Outcome o;
    seed = item_seed;
    bool ok = false;
    for (int i = 0; i < p.chain_length && !ok; ++i) {
      restart(p.chain[i]);
      // A quality below the one that can come is not what is wanted.
      if (i > wanted && strict) {
        dead = true;
        break;
      }
      switch (quality) {
        case kNormal: ok = normal_step(); break;
        case kSuperior: ok = superior_step(); break;
        case kMagic: ok = magic_step(); break;
        case kSet: ok = set_step(); break;
        case kRare: ok = rare_step(); break;
        case kUnique: ok = unique_step(); break;
        case kCrafted: ok = crafted_step(); break;
        default: lost = true; break;
      }
      if (dead || lost) break;
    }
    if (lost || dead) {
      o.followed = !lost;
      return o;
    }
    if (!ok) {
      // Nothing came of any quality: the game's own assertion case.
      lost = true;
      return o;
    }
    o.primary = quality == p.chain[wanted];
    if (!o.primary) all = false;
    // Ethereal, sockets, the automatic affix.
    bool ethereal = false;
    const bool can_be_ethereal = p.ethereal_base && !indestructible && quality != kInferior && quality != kSet;
    if (can_be_ethereal) {
      const uint32_t r = rng.roll(100);
      ethereal = (p.request.flags & kEthereal) || r <= 4;
    }
    // Ethereal is wanted of an item that can be. One that a property of its own makes ethereal is; a unique item
    // that is indestructible cannot be, whatever the seed (its properties are its own, not the seed's pick).
    const bool could_be_ethereal =
        p.ethereal_base && quality != kInferior && quality != kSet && !(quality == kUnique && indestructible);
    if (s.wants.ethereal && could_be_ethereal && !ethereal && !ethereal_mod) miss();
    if (dead) return o;
    int32_t sockets = 0;
    const bool rolls_sockets = (quality == kNormal || quality == kSuperior) && p.socket_n > 0;
    if (rolls_sockets) {
      uint32_t r = rng.roll(100);
      if (p.request.flags & kSockets) r = 0;
      if (!(p.request.flags & kNoSockets) && r <= 0x20) sockets = static_cast<int32_t>(seed % p.socket_n) + 1;
      if (s.wants.sockets && !(p.request.flags & kNoSockets) && !sockets) miss();
      if (dead) return o;
    }
    int32_t auto_affix = 0;
    // The automatic affix of the base (a class item's): not for a set or a unique item.
    if (p.auto_group && quality != kSet && quality != kUnique) {
      const int rare = quality == kRare || quality == kCrafted ? 1 : 0;
      // The automagic groups are their own: none of them is among the item's.
      const Side& side = p.autos[rare];
      if (const Row* row = pick(side, 0, true, true)) {
        auto_affix = row->id;
        apply(*row);
      }
      if (dead || lost) {
        o.followed = !lost;
        return o;
      }
    }
    o.followed = true;
    o.all = all;
    o.quality = quality;
    for (int i = 0; i < 3; ++i) {
      o.prefix[i] = prefix[i];
      o.suffix[i] = suffix[i];
    }
    o.auto_affix = auto_affix;
    o.file_index = file_index;
    o.ethereal = ethereal;
    o.ethereal_mod = ethereal_mod;
    o.sockets = sockets;
    o.socket_mod = socket_mod;
    o.top_defense = top_defense;
    o.skill_lines = skill_lines;
    o.rng_lo = rng.lo;
    o.rng_hi = rng.hi;
    // How much of what is wanted is there: the quality first, then the flags, the count, the versions, the values.
    double score = o.primary ? 64 : 0;
    if (s.wants.ethereal && could_be_ethereal) score += ethereal || ethereal_mod ? 16 : 0;
    if (s.wants.sockets && rolls_sockets) score += sockets ? 16 : 0;
    if (s.wants.most && most) score += 8.0 * made / most;
    if (s.wants.best && picks) score += 4.0 * tops / picks;
    if (s.wants.perfect && value_count) score += 4.0 * values / value_count;
    o.score = score;
    return o;
  }
};

}  // namespace

Prints prints_of(const Tables& t) {
  const auto print = [](const std::vector<uint8_t>& rows) {
    const uint64_t h = fnv(0xCBF29CE484222325ull, rows.data(), rows.size());
    return static_cast<uint32_t>(h ^ (h >> 32));
  };
  Prints p;
  p.affix = print(t.affix);
  p.rare_names = print(t.rare_names);
  p.items = print(t.items);
  p.types = print(t.types);
  p.props = print(t.props);
  p.unique = print(t.unique);
  p.sets = print(t.sets);
  p.quality = print(t.quality);
  uint64_t h = 0xCBF29CE484222325ull;
  for (const Skill& s : t.skills) {
    h = fnv(h, s.char_class);
    h = fnv(h, s.req_level);
    h = fnv(h, s.max_level);
    h = fnv(h, s.item_type);
  }
  p.skills = static_cast<uint32_t>(h ^ (h >> 32));
  return p;
}

bool Tables::complete() const {
  return affix_count && affix.size() >= static_cast<size_t>(affix_count) * kAffixRow && suffix_at <= prefix_at &&
         prefix_at <= auto_at && auto_at <= affix_count && props_count &&
         props.size() >= static_cast<size_t>(props_count) * kPropertyRow && items_count &&
         items.size() >= static_cast<size_t>(items_count) * kItemsRow && types_count &&
         types.size() >= static_cast<size_t>(types_count) * kTypesRow &&
         unique.size() >= static_cast<size_t>(unique_count) * kUniqueRow &&
         sets.size() >= static_cast<size_t>(sets_count) * kSetItemRow && quality_count &&
         quality.size() >= static_cast<size_t>(quality_count) * kQualityRow &&
         rare_names.size() >= static_cast<size_t>(rare_suffix_count + rare_prefix_count) * kRareNameRow &&
         !skills.empty();
}

std::shared_ptr<const Plan> make_plan(const Tables& tables, const Request& request, const char** why) {
  auto plan = std::make_shared<Plan>();
  Builder b(tables, *plan);
  const char* reason = b.build(request);
  if (why) *why = reason;
  if (reason) return nullptr;
  plan->print = print_of(*plan);
  return plan;
}

uint64_t fingerprint(const Plan& plan) { return plan.print; }
const Request& request_of(const Plan& plan) { return plan.request; }
bool ethereal_base(const Plan& plan) { return plan.durable_base; }

bool can_be_imbued(const Tables& t, uint32_t item_class) {
  if (!t.complete() || item_class >= t.items_count) return false;
  const uint8_t* item = t.items.data() + static_cast<size_t>(item_class) * kItemsRow;
  if (!(at<uint32_t>(item, kItemBits) & 1)) return false;
  if (at<uint8_t>(item, kItemQuest) && at<uint32_t>(item, kItemCode) != kCodeWirtsLeg) return false;
  if (at<uint8_t>(item, kItemUnique)) return false;
  const int type = at<int16_t>(item, kItemType);
  if (type <= 0 || static_cast<uint32_t>(type) >= t.types_count) return false;
  return at<uint8_t>(t.types.data() + static_cast<size_t>(type) * kTypesRow, kTypeMagic) == 0;
}

int unique_candidates(const Plan& plan, int32_t* rows, int capacity) {
  int n = 0;
  for (const Row& r : plan.uniques)
    if (n < capacity) rows[n++] = r.id;
  return n;
}

Outcome simulate(const Plan& plan, const Search& search, uint32_t seed) {
  Run run(plan, search, Tier::kAllWeighed);
  return run.run(seed);
}

void affix_candidates(const Plan& plan, std::vector<int32_t>* fit, std::vector<int32_t>* unfit) {
  fit->clear();
  for (int side = 0; side < 2; ++side)
    for (const Row& r : plan.side[side][0].rows) fit->push_back(r.id);
  for (const Row& r : plan.autos[0].rows) fit->push_back(r.id);
  *unfit = plan.unfit;
}

Found search(const Plan& plan, const Search& what, uint32_t start, uint32_t stride, uint64_t count,
             const volatile long* stop) {
  Found found;
  stride |= 1;
  uint32_t seed = start;
  // Everything wanted first: a seed that misses anything is dropped at its first miss.
  for (uint64_t i = 0; i < count; ++i, seed += stride) {
    if (stop && (i & 0xFFF) == 0 && *stop) break;
    ++found.tried;
    Run run(plan, what, Tier::kEverything);
    const Outcome o = run.run(seed);
    if (o.followed && o.all) {
      found.any = found.all = true;
      found.seed = seed;
      found.score = o.score;
      return found;
    }
  }
  return found;
}

Found search_best(const Plan& plan, const Search& what, Tier tier, uint32_t start, uint32_t stride,
                  uint64_t count, const volatile long* stop) {
  Found found;
  stride |= 1;
  uint32_t seed = start;
  for (uint64_t i = 0; i < count; ++i, seed += stride) {
    if (stop && (i & 0xFF) == 0 && *stop) break;
    ++found.tried;
    Run run(plan, what, tier);
    const Outcome o = run.run(seed);
    // A seed dropped on the way gave up something that had to be there; one that came through is weighed.
    if (!o.followed || run.dead) continue;
    if (!found.any || o.score > found.score) {
      found.any = true;
      found.all = o.all;
      found.seed = seed;
      found.score = o.score;
      if (o.all) return found;
    }
  }
  return found;
}

UnitPlan make_unit_plan(const Tables& tables, uint32_t item_class) {
  UnitPlan u;
  if (!tables.complete() || item_class >= tables.items_count) return u;
  const uint8_t* item = tables.items.data() + static_cast<size_t>(item_class) * kItemsRow;
  // An armor: its type, or one it counts as, is the armor type.
  std::vector<uint8_t> seen(tables.types_count, 0);
  std::vector<int> todo{at<int16_t>(item, kItemType), at<int16_t>(item, kItemType2)};
  bool armor = false;
  while (!todo.empty()) {
    const int ty = todo.back();
    todo.pop_back();
    if (ty <= 0 || static_cast<uint32_t>(ty) >= tables.types_count || seen[ty]) continue;
    seen[ty] = 1;
    armor = armor || ty == kTypeArmor;
    const uint8_t* r = tables.types.data() + static_cast<size_t>(ty) * kTypesRow;
    todo.push_back(at<uint16_t>(r, kTypeEquiv1));
    todo.push_back(at<uint16_t>(r, kTypeEquiv2));
  }
  if (!armor) return u;
  u.armor = true;
  u.durability_n = at<uint8_t>(item, kItemDurability) >> 1;
  u.min_ac = at<int32_t>(item, kItemMinAc);
  u.defense_n = at<int32_t>(item, kItemMaxAc) - u.min_ac + 1;
  return u;
}

int32_t defense_of(const UnitPlan& plan, uint32_t unit_seed) {
  if (!plan.armor) return -1;
  Rng rng(unit_seed);
  rng.roll(plan.durability_n);
  return plan.min_ac + static_cast<int32_t>(rng.roll(plan.defense_n));
}

bool find_unit_seed(const UnitPlan& plan, uint32_t item_seed, bool perfect, uint32_t start, uint32_t tries,
                    uint32_t* out) {
  uint32_t seed = start;
  bool any = false;
  for (uint32_t i = 0; i < tries; ++i, seed += 0x9E3779B1u) {
    if (!in_a_row(seed, item_seed)) continue;
    if (!any) *out = seed;
    any = true;
    if (!perfect || !plan.armor || plan.defense_n <= 1 ||
        defense_of(plan, seed) == plan.min_ac + plan.defense_n - 1) {
      *out = seed;
      return true;
    }
  }
  return false;
}

}  // namespace d2rcc::itemgen
