// The unit seed (src/itemseed.h): the game's random stream, what item init rolls from an armor's unit seed, a seed
// that rolls the top base defense, and the rule the two seeds of an item come in a row by. Over the game's tables
// when they are at hand (D2RCC_EXCEL = the loader's compiled excel folder), every armor row against the item seed
// model (tools/seedmodel), which was checked against the game itself.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "itemgen.h"
#include "itemseed.h"
#include "tables.h"

using namespace d2rcc;
using itemseed::Armor;
using itemseed::Rng;

static int g_failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
      ++g_failures;                                                     \
    }                                                                   \
  } while (0)

// The rows the plugin reads, from the compiled tables.
constexpr size_t kItemsMinAc = 0xD4, kItemsMaxAc = 0xD8, kItemsDurability = 0x121, kItemsType = 0x12E,
                 kItemsType2 = 0x130, kTypesEquiv1 = 0x04, kTypesEquiv2 = 0x06;
template <typename T>
static T at(const uint8_t* row, size_t offset) {
  T v{};
  std::memcpy(&v, row + offset, sizeof(T));
  return v;
}

static uint32_t xorshift(uint32_t* s) {
  *s ^= *s << 13;
  *s ^= *s >> 17;
  *s ^= *s << 5;
  return *s;
}

int main() {
  // The stream: multiply with carry, the carry seeded 666 (values from an independent implementation).
  {
    Rng r(0);
    CHECK(r.step() == 0x29Au && r.step() == 0xC894A082u && r.step() == 0xED34A51Fu && r.step() == 0x06079FB2u);
    CHECK(r.hi == 0x62EFC93Bu);
    Rng one(1);
    CHECK(one.step() == 0x6AC6935Fu && one.step() == 0x2F2ED81Bu && one.step() == 0x280687C4u);
    Rng big(0xFFFFFFFFu);
    CHECK(big.step() == 0x953971D5u && big.step() == 0xCCC0F9ADu);
    Rng seen(0x5B2A6F7Du);
    CHECK(seen.step() == 0x030C1DCBu && seen.step() == 0x681D65C1u && seen.hi != 666);
  }
  // The roll: nothing moves for a range that is not above 0; a power of two is a mask, anything else a remainder.
  {
    Rng r(0x12345678u);
    CHECK(r.roll(0) == 0 && r.roll(-5) == 0 && r.lo == 0x12345678u && r.hi == 666);
    CHECK(r.roll(16) == (0x03BA0CF2u & 15));
    CHECK(r.roll(100) == 0xC437E0CEu % 100);
    CHECK(r.roll(1) == 0 && r.lo == 0x9A55CBE3u);  // a range of one still takes a step
  }
  // Item init on an armor: the durability roll, then the base defense.
  {
    const Armor sacred{60, 487, 600}, cap{24, 8, 11}, none{0, 5, 9}, one{1, 5, 9};
    const uint32_t seeds[5] = {0, 1, 2, 0xDEADBEEFu, 0x80000000u};
    const int32_t of_sacred[5] = {549, 490, 513, 523, 581}, of_cap[5] = {10, 11, 8, 10, 8};
    const int32_t of_none[5] = {6, 6, 6, 8, 9};
    for (int i = 0; i < 5; ++i) {
      CHECK(itemseed::defense(sacred, seeds[i]) == of_sacred[i]);
      CHECK(itemseed::defense(cap, seeds[i]) == of_cap[i]);
      // A base without durability, or with 1, has no durability roll: the defense is the first roll.
      CHECK(itemseed::defense(none, seeds[i]) == of_none[i]);
      CHECK(itemseed::defense(one, seeds[i]) == of_none[i]);
    }
    uint32_t seed = 0, tried = 0;
    CHECK(itemseed::top_defense_seed(sacred, 0, 1u << 16, &seed, &tried) && seed == 0x5B89F564u && tried == 165);
    CHECK(itemseed::defense(sacred, seed) == 600);
    CHECK(itemseed::top_defense_seed(sacred, 0xCAFEBABEu, 1u << 16, &seed, &tried) && seed == 0x8707E7E8u &&
          tried == 75);
    CHECK(itemseed::top_defense_seed(cap, 0, 1u << 16, &seed, &tried) && seed == 0x9E3779B1u && tried == 2);
    CHECK(itemseed::top_defense_seed(none, 0, 1u << 16, &seed, &tried) && seed == 0xB54CDA26u && tried == 7);
    // Too few tries: none found, and the count says how many were looked at.
    CHECK(!itemseed::top_defense_seed(sacred, 0, 100, &seed, &tried) && tried == 100);
    const Armor flat{10, 5, 5}, empty{};
    CHECK(sacred.ranged() && !flat.ranged() && !empty.ranged());
    // One seed in (maxac - minac + 1) rolls the top: about that many tries from wherever (the walk is in even
    // steps, so rather fewer than blind tries would take).
    uint32_t x = 0x2545F491u;
    uint64_t total = 0;
    uint32_t most = 0;
    const auto began = std::chrono::steady_clock::now();
    for (int i = 0; i < 20000; ++i) {
      CHECK(itemseed::top_defense_seed(sacred, xorshift(&x), 1u << 16, &seed, &tried));
      CHECK(itemseed::defense(sacred, seed) == 600);
      total += tried;
      if (tried > most) most = tried;
    }
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - began).count();
    const double mean = static_cast<double>(total) / 20000;
    CHECK(mean > 114 * 0.5 && mean < 114 * 1.5 && most < 4000);
    std::printf("a range of 114 (487..600): %.1f tries a seed on average, %u at most, %.2f microseconds a seed\n",
                mean, most, us / 20000);
  }
  // The two seeds come in a row: from any state of the game's stream, two steps give a unit seed and an item seed
  // the rule takes, and an item seed made for a unit seed is one the stream can give after it.
  {
    uint32_t x = 0x9E3779B9u;
    for (int i = 0; i < 20000; ++i) {
      Rng game(xorshift(&x));
      game.hi = xorshift(&x) % itemseed::kMultiplier;  // the carry is always below the multiplier
      const uint32_t unit = game.step();
      const uint32_t carry = game.hi;
      const uint32_t item = game.step();
      CHECK(itemseed::in_a_row(unit, item));
      CHECK(itemseed::item_seed_after(unit, carry) == item);
      const uint32_t any = xorshift(&x);
      CHECK(itemseed::in_a_row(unit, itemseed::item_seed_after(unit, any)));
    }
    // Past the carry's range the pair is not one the stream gives.
    CHECK(itemseed::in_a_row(7, 7 * itemseed::kMultiplier + itemseed::kMultiplier - 1));
    CHECK(!itemseed::in_a_row(7, 7 * itemseed::kMultiplier + itemseed::kMultiplier));
    CHECK(!itemseed::in_a_row(7, 7 * itemseed::kMultiplier - 1));
  }
  // What the generator makes of a top base defense.
  {
    const Armor plate{60, 487, 600};
    CHECK(itemseed::top_defense_kept(plate, 2, false, 600));
    CHECK(itemseed::top_defense_kept(plate, 4, false, 601));   // enhanced defense: the top plus one
    CHECK(itemseed::top_defense_kept(plate, 7, true, 900));    // ethereal: half as much again
    CHECK(itemseed::top_defense_kept(plate, 6, true, 901));    // ... of the top plus one (601 * 3 / 2)
    CHECK(itemseed::top_defense_kept(plate, 1, false, 450));   // low quality: three quarters
    CHECK(!itemseed::top_defense_kept(plate, 1, false, 600));
    CHECK(!itemseed::top_defense_kept(plate, 2, false, 599) && !itemseed::top_defense_kept(plate, 2, false, 602));
    CHECK(!itemseed::top_defense_kept(plate, 2, true, 600) && !itemseed::top_defense_kept(plate, 2, false, 900));
    CHECK(itemseed::top_defense_kept(Armor{10, 0, 1}, 1, false, 1));  // never below 1
  }

  // Over the game's tables: every item row, against the seed model.
  const char* dir = std::getenv("D2RCC_EXCEL");
  itemgen::Tables t;
  if (!dir || !*dir || !tables::load(dir, &t)) {
    std::printf("the game's tables are not at hand (D2RCC_EXCEL) - the armor rows were not checked\n");
  } else {
    const auto equivs = [&t](uint32_t type, uint16_t* e1, uint16_t* e2) {
      if (type >= t.types_count) return false;
      const uint8_t* row = t.types.data() + static_cast<size_t>(type) * itemgen::kTypesRow;
      *e1 = at<uint16_t>(row, kTypesEquiv1);
      *e2 = at<uint16_t>(row, kTypesEquiv2);
      return true;
    };
    int armors = 0, ranged = 0, widest = 0;
    uint32_t most_tries = 0;
    uint32_t x = 0x1234ABCDu;
    for (uint32_t c = 0; c < t.items_count; ++c) {
      const uint8_t* row = t.items.data() + static_cast<size_t>(c) * itemgen::kItemsRow;
      const int16_t type = at<int16_t>(row, kItemsType), type2 = at<int16_t>(row, kItemsType2);
      const bool armor = (type > 0 && itemseed::counts_as_armor(static_cast<uint32_t>(type), equivs)) ||
                         (type2 > 0 && itemseed::counts_as_armor(static_cast<uint32_t>(type2), equivs));
      const itemgen::UnitPlan plan = itemgen::make_unit_plan(t, c);
      CHECK(armor == plan.armor);
      if (!armor) continue;
      ++armors;
      const Armor base{at<uint8_t>(row, kItemsDurability), at<int32_t>(row, kItemsMinAc),
                       at<int32_t>(row, kItemsMaxAc)};
      for (int i = 0; i < 64; ++i) {
        const uint32_t s = xorshift(&x);
        CHECK(itemseed::defense(base, s) == itemgen::defense_of(plan, s));
      }
      if (!base.ranged()) continue;
      ++ranged;
      if (base.max_ac - base.min_ac + 1 > widest) widest = base.max_ac - base.min_ac + 1;
      uint32_t seed = 0, tried = 0;
      CHECK(itemseed::top_defense_seed(base, xorshift(&x), 1u << 16, &seed, &tried));
      CHECK(itemseed::defense(base, seed) == base.max_ac && itemgen::defense_of(plan, seed) == base.max_ac);
      if (tried > most_tries) most_tries = tried;
    }
    CHECK(armors > 150 && ranged > 150);
    std::printf("%u item rows: %d armor, %d with a range of base defense (the widest %d); a top seed for each, the "
                "most tries %u\n",
                t.items_count, armors, ranged, widest, most_tries);
  }

  if (g_failures) {
    std::printf("%d failure%s\n", g_failures, g_failures == 1 ? "" : "s");
    return 1;
  }
  std::printf("ok\n");
  return 0;
}
