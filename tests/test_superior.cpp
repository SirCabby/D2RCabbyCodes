// A superior item's kind under perfect rolls (src/superior.h): which of the kinds that go on an item is the best.
// The rule over kinds made here, and, when the game's tables are at hand (D2RCC_EXCEL = the loader's compiled excel
// folder), over the game's own QualityItems and Properties rows for every weapon and armor base: the game's test of
// a kind (0x3D40D0) and the step's count of the kinds (0x58B9E0) are written a second time here.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "superior.h"

using namespace d2rcc;

static int g_failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
      ++g_failures;                                                     \
    }                                                                   \
  } while (0)

template <typename T>
static T at(const uint8_t* d, size_t offset) {
  T v{};
  std::memcpy(&v, d + offset, sizeof(T));
  return v;
}

// The rows of a compiled table (a D2RLBIN file has one or more tables: the one with rows of this size).
static bool rows_of(const std::string& path, uint32_t row_size, std::vector<uint8_t>* rows, uint32_t* count) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  const std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (d.size() < 0x60 || std::memcmp(d.data(), "D2RLBIN", 7) != 0) return false;
  const uint32_t tables = at<uint32_t>(d.data(), 0x3C), size = at<uint32_t>(d.data(), 0x40);
  for (uint32_t t = tables; t + 0x50 <= tables + size && t + 0x50 <= d.size(); t += 0x50) {
    const uint32_t n = at<uint32_t>(d.data(), t + 12), each = at<uint32_t>(d.data(), t + 16);
    const uint32_t where = at<uint32_t>(d.data(), t + 36);
    if (each != row_size || static_cast<uint64_t>(where) + static_cast<uint64_t>(n) * each > d.size()) continue;
    rows->assign(d.begin() + where, d.begin() + where + static_cast<size_t>(n) * each);
    *count = n;
    return true;
  }
  return false;
}

// 3.3's kinds, as bonuses: 0 attack rating, 1 enhanced damage, 2 enhanced defense, 3 attack rating with enhanced
// damage, 4 durability, 5 attack rating with durability, 6 enhanced damage with durability, 7 enhanced defense with
// durability.
constexpr uint32_t kDurability = 75;
static superior::Kind kind_of(std::initializer_list<superior::Bonus> bonuses) {
  superior::Kind k;
  for (const superior::Bonus& b : bonuses) k.bonus[k.count++] = b;
  return k;
}
static void game_kinds(superior::Kind (&kinds)[superior::kMaxKinds]) {
  const superior::Bonus att{superior::kAttackRating, 1, 3}, dmg{superior::kEnhancedDamage, 5, 15};
  const superior::Bonus ac{superior::kEnhancedDefense, 5, 15}, dur{kDurability, 10, 15};
  kinds[0] = kind_of({att});
  kinds[1] = kind_of({dmg});
  kinds[2] = kind_of({ac});
  kinds[3] = kind_of({att, dmg});
  kinds[4] = kind_of({dur});
  kinds[5] = kind_of({att, dur});
  kinds[6] = kind_of({dmg, dur});
  kinds[7] = kind_of({ac, dur});
}
static uint32_t best_of(uint32_t fit, int among) {
  superior::Kind kinds[superior::kMaxKinds];
  game_kinds(kinds);
  for (int k = 0; k < superior::kMaxKinds; ++k) kinds[k].fits = k < among && (fit & (1u << k)) != 0;
  return superior::best(kinds, among);
}

// The game's tables: the compiled Items rows (Weapons, Armor), ItemTypes, QualityItems and Properties.
constexpr uint32_t kItemsRow = 0x1C0, kTypesRow = 0xE8;
constexpr size_t kItemsNoDurability = 0x122, kItemsType = 0x12E, kItemsType2 = 0x130;
constexpr size_t kTypesEquiv1 = 0x04, kTypesEquiv2 = 0x06, kTypesThrown = 0x10;
constexpr int kTypeWeapon = 0x2D, kTypeArmor = 0x32;

struct Game {
  std::vector<uint8_t> types, kinds, properties;
  uint32_t type_count = 0, kind_count = 0, property_count = 0;

  bool is_a(int type, int want, int depth = 0) const {
    if (type <= 0 || static_cast<uint32_t>(type) >= type_count || depth > 10) return false;
    if (type == want) return true;
    const uint8_t* row = types.data() + static_cast<size_t>(type) * kTypesRow;
    return is_a(at<uint16_t>(row, kTypesEquiv1), want, depth + 1) ||
           is_a(at<uint16_t>(row, kTypesEquiv2), want, depth + 1);
  }
  // The game's item-type test of an item (0x373890): its type or its second type counts as the one asked.
  bool item_is(const uint8_t* item, int want) const {
    return is_a(at<int16_t>(item, kItemsType), want) || is_a(at<int16_t>(item, kItemsType2), want);
  }
  // The game's test of a kind (0x3D40D0): a weapon takes the kinds marked for weapons unless its type is one of
  // those with a mark of their own (scepter, wand, staff, bow, crossbow), an armor the ones marked for armor unless
  // its type is (shield, boots, gloves, belt); else the mark of the item's own type.
  bool fits(const uint8_t* item, const uint8_t* kind) const {
    const int type = at<int16_t>(item, kItemsType);
    const bool own_weapon = (type >= 0x18 && type <= 0x1B) || type == 0x23;
    const bool own_armor = type >= 0 && type < 0x14 && ((0x98004u >> type) & 1);
    if (kind[1] && item_is(item, kTypeWeapon) && !own_weapon) return true;
    if (kind[0] && item_is(item, kTypeArmor) && !own_armor) return true;
    static const struct {
      int mark, type;
    } kOwn[9] = {{2, 2}, {3, 0x18}, {4, 0x19}, {5, 0x1A}, {6, 0x1B}, {6, 0x23}, {7, 0x0F}, {8, 0x10}, {9, 0x13}};
    for (const auto& own : kOwn)
      if (type == own.type && kind[own.mark]) return true;
    return false;
  }
};

int main() {
  using superior::kAttackRating;
  using superior::kEnhancedDamage;
  using superior::kEnhancedDefense;

  // What a bonus counts: enhanced damage and defense the most, attack rating before the rest.
  CHECK(superior::weight(kEnhancedDamage) == superior::weight(kEnhancedDefense));
  CHECK(superior::weight(kEnhancedDamage) > superior::weight(kAttackRating));
  CHECK(superior::weight(kAttackRating) > superior::weight(kDurability));
  CHECK(superior::weight(kDurability) == superior::weight(12345));
  CHECK(superior::weight(superior::kMaxDamagePercent) == superior::weight(kEnhancedDamage));
  // ... so that damage with attack rating is before damage with durability, and either before damage alone, and
  // that before any pair without it.
  CHECK(superior::weight(kEnhancedDamage) > superior::weight(kAttackRating) + superior::weight(kDurability));

  // How many kinds the step picks among.
  CHECK(superior::picked_among(8, 4, false, false) == 8);
  CHECK(superior::picked_among(8, 4, true, false) == 4);
  CHECK(superior::picked_among(8, 4, false, true) == 4);
  CHECK(superior::picked_among(8, 4, true, true) == 4);
  CHECK(superior::picked_among(3, 4, true, false) == 3);
  CHECK(superior::picked_among(10, 4, false, false) == 10);

  // An armor (the kinds 2, 4 and 7 go on it): enhanced defense with durability.
  CHECK(best_of(0x94, 8) == 0x80);
  // A weapon (0, 1, 3, 4, 5, 6): enhanced damage with attack rating.
  CHECK(best_of(0x7B, 8) == 0x08);
  // A bow or a throwing weapon picks among the first four: the same kind.
  CHECK(best_of(0x7B, 4) == 0x08);
  // An armor that picked among the first four would have enhanced defense alone.
  CHECK(best_of(0x94, 4) == 0x04);
  // Whatever goes on the item is the best when nothing else does; nothing, when nothing does.
  CHECK(best_of(0x10, 8) == 0x10);
  CHECK(best_of(0x01, 8) == 0x01);
  CHECK(best_of(0, 8) == 0);
  CHECK(best_of(0x80, 4) == 0);
  // Without the pair with attack rating: damage with durability; without any damage: attack rating with durability.
  CHECK(best_of(0x73, 8) == 0x40);
  CHECK(best_of(0x31, 8) == 0x20);
  // Damage alone is before attack rating with durability.
  CHECK(best_of(0x32, 8) == 0x02);

  // The most of a bonus counts: of two kinds with the same bonuses the one that gives more, by the tops first,
  // then by the bottoms; two that give the same are both taken (the step's roll decides).
  {
    superior::Kind kinds[superior::kMaxKinds];
    kinds[0] = kind_of({{kEnhancedDefense, 5, 15}, {kDurability, 10, 15}});
    kinds[1] = kind_of({{kEnhancedDefense, 5, 20}, {kDurability, 10, 15}});
    kinds[2] = kind_of({{kEnhancedDefense, 10, 20}, {kDurability, 10, 15}});
    kinds[3] = kind_of({{kEnhancedDefense, 10, 20}, {kDurability, 10, 15}});
    for (int k = 0; k < 4; ++k) kinds[k].fits = true;
    CHECK(superior::best(kinds, 2) == 0x02);
    CHECK(superior::best(kinds, 3) == 0x04);
    CHECK(superior::best(kinds, 4) == 0x0C);
    kinds[3].fits = false;
    CHECK(superior::best(kinds, 4) == 0x04);
    // A kind that does not go on the item does not set what the most of a bonus is.
    kinds[4] = kind_of({{kDurability, 10, 200}});
    CHECK(superior::best(kinds, 5) == 0x04);
    // A bonus that lowers something counts by how far.
    kinds[5] = kind_of({{kEnhancedDefense, -30, -10}, {kDurability, 10, 15}});
    kinds[5].bonus[0].lo = superior::magnitude(-10);
    kinds[5].bonus[0].hi = superior::magnitude(-30);
    kinds[5].fits = true;
    CHECK(superior::best(kinds, 6) == 0x20);
    // A kind without a bonus is taken only when nothing else goes on the item.
    superior::Kind empty[2];
    empty[0].fits = empty[1].fits = true;
    CHECK(superior::best(empty, 2) == 0x03);
    empty[1] = kind_of({{kDurability, 10, 15}});
    empty[1].fits = true;
    CHECK(superior::best(empty, 2) == 0x02);
  }

  // A row's bonuses: the properties of its two mods, each by its stat, or by its function when it names none.
  {
    uint8_t properties[3 * superior::kPropertyRowSize] = {};
    const auto property = [&properties](int id, uint8_t func, uint16_t stat) {
      uint8_t* p = properties + static_cast<size_t>(id) * superior::kPropertyRowSize;
      std::memset(p + superior::kPropertyStat, 0xFF, superior::kPropertySlots * sizeof(uint16_t));
      p[superior::kPropertyFunc] = func;
      std::memcpy(p + superior::kPropertyStat, &stat, sizeof(stat));
    };
    property(0, 1, 19);
    property(1, 7, superior::kNoStat);
    property(2, 13, 75);
    const auto row_of = [&properties](int32_t id) -> const uint8_t* {
      return id >= 0 && id < 3 ? properties + static_cast<size_t>(id) * superior::kPropertyRowSize : nullptr;
    };
    uint8_t row[superior::kRowSize] = {};
    const int32_t mods[8] = {0, 0, 1, 3, 1, 0, 15, 5};
    std::memcpy(row + superior::kRowMods, mods, sizeof(mods));
    superior::Kind k;
    CHECK(superior::bonuses_of(row, row_of, &k));
    CHECK(k.count == 2 && k.bonus[0].what == kAttackRating && k.bonus[0].lo == 1 && k.bonus[0].hi == 3);
    CHECK(k.bonus[1].what == kEnhancedDamage && k.bonus[1].lo == 5 && k.bonus[1].hi == 15);
    const int32_t one[8] = {2, 0, 10, 15, -1, 0, 0, 0};
    std::memcpy(row + superior::kRowMods, one, sizeof(one));
    CHECK(superior::bonuses_of(row, row_of, &k));
    CHECK(k.count == 1 && k.bonus[0].what == kDurability && k.bonus[0].lo == 10 && k.bonus[0].hi == 15);
    const int32_t none[8] = {-1, 0, 0, 0, -1, 0, 0, 0};
    std::memcpy(row + superior::kRowMods, none, sizeof(none));
    CHECK(superior::bonuses_of(row, row_of, &k) && k.count == 0);
    const int32_t unknown[8] = {2, 0, 10, 15, 7, 0, 1, 2};
    std::memcpy(row + superior::kRowMods, unknown, sizeof(unknown));
    CHECK(!superior::bonuses_of(row, row_of, &k));
  }

  // The game's own rows.
  const char* dir = std::getenv("D2RCC_EXCEL");
  Game g;
  std::vector<uint8_t> weapons, armor;
  uint32_t weapon_count = 0, armor_count = 0;
  if (!dir || !rows_of(std::string(dir) + "/qualityitems.bin", superior::kRowSize, &g.kinds, &g.kind_count) ||
      !rows_of(std::string(dir) + "/properties.bin", superior::kPropertyRowSize, &g.properties, &g.property_count) ||
      !rows_of(std::string(dir) + "/itemtypes.bin", kTypesRow, &g.types, &g.type_count) ||
      !rows_of(std::string(dir) + "/weapons.bin", kItemsRow, &weapons, &weapon_count) ||
      !rows_of(std::string(dir) + "/armor.bin", kItemsRow, &armor, &armor_count)) {
    std::printf("the game's tables are not at hand (D2RCC_EXCEL) - the game's kinds were not checked\n");
  } else {
    CHECK(g.kind_count >= 8 && g.kind_count <= static_cast<uint32_t>(superior::kMaxKinds));
    const auto property_row = [&g](int32_t id) -> const uint8_t* {
      return id >= 0 && static_cast<uint32_t>(id) < g.property_count
                 ? g.properties.data() + static_cast<size_t>(id) * superior::kPropertyRowSize
                 : nullptr;
    };
    // The kinds are the ones written above.
    superior::Kind as_written[superior::kMaxKinds];
    game_kinds(as_written);
    for (uint32_t k = 0; k < 8 && k < g.kind_count; ++k) {
      superior::Kind kind;
      CHECK(superior::bonuses_of(g.kinds.data() + static_cast<size_t>(k) * superior::kRowSize, property_row, &kind));
      CHECK(kind.count == as_written[k].count);
      for (int i = 0; i < kind.count && i < as_written[k].count; ++i)
        CHECK(kind.bonus[i].what == as_written[k].bonus[i].what && kind.bonus[i].lo == as_written[k].bonus[i].lo &&
              kind.bonus[i].hi == as_written[k].bonus[i].hi);
    }
    // Every weapon and armor base: one kind is the best, it has two bonuses, a weapon's enhanced damage with attack
    // rating and an armor's enhanced defense with durability, and the game's own roll can come to it.
    int bases[2] = {}, few[2] = {}, none = 0;
    const auto over = [&](const std::vector<uint8_t>& rows, uint32_t count, bool weapon) {
      for (uint32_t c = 0; c < count; ++c) {
        const uint8_t* item = rows.data() + static_cast<size_t>(c) * kItemsRow;
        const int type = at<int16_t>(item, kItemsType);
        if (type <= 0 || static_cast<uint32_t>(type) >= g.type_count) continue;
        if (!g.item_is(item, weapon ? kTypeWeapon : kTypeArmor)) continue;
        const bool thrown = at<uint8_t>(g.types.data() + static_cast<size_t>(type) * kTypesRow, kTypesThrown) != 0;
        const bool no_durability = at<uint8_t>(item, kItemsNoDurability) != 0;
        const int among = superior::picked_among(static_cast<int>(g.kind_count), 4, thrown, no_durability);
        superior::Kind kinds[superior::kMaxKinds];
        uint32_t fit = 0;
        for (int k = 0; k < among; ++k) {
          const uint8_t* row = g.kinds.data() + static_cast<size_t>(k) * superior::kRowSize;
          kinds[k].fits = g.fits(item, row);
          if (!kinds[k].fits) continue;
          CHECK(superior::bonuses_of(row, property_row, &kinds[k]));
          fit |= 1u << k;
        }
        const uint32_t take = superior::best(kinds, among);
        if (!fit) {
          // Nothing goes on it: the game's step makes no superior item of the base either.
          CHECK(take == 0);
          ++none;
          continue;
        }
        ++bases[weapon ? 0 : 1];
        if (among < static_cast<int>(g.kind_count)) ++few[weapon ? 0 : 1];
        CHECK(take != 0 && (take & (take - 1)) == 0);
        CHECK((take & fit) == take);
        const uint32_t want = weapon ? 0x08u : among > 7 ? 0x80u : 0x04u;
        if (take != want) {
          std::printf("FAIL: %s row %u (type %d, %s%s): the kinds %#x go on it, %#x taken, %#x expected\n",
                      weapon ? "weapon" : "armor", c, type, thrown ? "thrown " : "",
                      no_durability ? "no durability" : "", fit, take, want);
          ++g_failures;
        }
      }
    };
    over(weapons, weapon_count, true);
    over(armor, armor_count, false);
    CHECK(bases[0] > 200 && bases[1] > 150);
    // Bows, crossbows, throwing weapons and the Phase Blade pick among the first four; every armor has durability.
    CHECK(few[0] > 30 && few[1] == 0);
    std::printf("%u kinds; %d weapon bases (%d pick among the first four) take enhanced damage with attack rating, "
                "%d armor bases enhanced defense with durability; %d bases take no kind\n",
                g.kind_count, bases[0], few[0], bases[1], none);
  }

  if (g_failures) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("superior: ok\n");
  return 0;
}
