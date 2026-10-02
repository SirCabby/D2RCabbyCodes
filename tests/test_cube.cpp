// Infinite cube ingredients (src/cube.h): what one transmute keeps and the order it is given back in. The product
// routine frees what it uses up in the cube's order, after Clear Sockets has freed what was in the sockets; the
// ingredients go back first, then the socket contents, each kind in the order the game gave them up. Nothing is kept
// twice, and nothing past the list's bound (the game then frees it as it would have).
#include <cstdio>

#include "cube.h"

using namespace d2rcc;
using cube::Kept;
using game::Unit;

static int g_failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
      ++g_failures;                                                     \
    }                                                                   \
  } while (0)

// Stand-ins for units: only their addresses are compared.
static unsigned char g_units[64];
static Unit* unit(int i) { return reinterpret_cast<Unit*>(&g_units[i]); }

int main() {
  {
    // Clear Sockets: Hel, a Scroll of Town Portal and a Shako with an Ist in it. The Ist is freed while the products
    // are made, the Hel and the scroll after (the Shako is the product: changed in place, never freed).
    Kept k;
    CHECK(k.size() == 0);
    CHECK(k.add(unit(10), cube::kSocketed));
    CHECK(k.add(unit(1), cube::kIngredient));
    CHECK(k.add(unit(2), cube::kIngredient));
    CHECK(k.size() == 3);
    CHECK(k.count(cube::kIngredient) == 2 && k.count(cube::kSocketed) == 1);
    Unit* order[cube::kMaxKept] = {};
    cube::Kind kinds[cube::kMaxKept] = {};
    CHECK(k.order(order, kinds, cube::kMaxKept) == 3);
    CHECK(order[0] == unit(1) && order[1] == unit(2) && order[2] == unit(10));
    CHECK(kinds[0] == cube::kIngredient && kinds[1] == cube::kIngredient && kinds[2] == cube::kSocketed);
    // A smaller list takes the first ones of that order; the kinds may be left out.
    CHECK(k.order(order, nullptr, 2) == 2 && order[0] == unit(1) && order[1] == unit(2));
    CHECK(k.order(order, nullptr, 0) == 0);
  }
  {
    // A reroll: three skulls and a magic ring, all used up, kept in the cube's order.
    Kept k;
    for (int i = 0; i < 4; ++i) CHECK(k.add(unit(20 + i), cube::kIngredient));
    Unit* order[cube::kMaxKept] = {};
    CHECK(k.order(order, nullptr, cube::kMaxKept) == 4);
    for (int i = 0; i < 4; ++i) CHECK(order[i] == unit(20 + i));
    CHECK(k.count(cube::kSocketed) == 0);
  }
  {
    // Nothing twice, nothing that is not a unit, no kind the list does not know.
    Kept k;
    CHECK(k.add(unit(5), cube::kIngredient));
    CHECK(!k.add(unit(5), cube::kIngredient));
    CHECK(!k.add(unit(5), cube::kSocketed));
    CHECK(!k.add(nullptr, cube::kIngredient));
    CHECK(!k.add(unit(6), cube::kKinds));
    CHECK(k.size() == 1);
    k.clear();
    CHECK(k.size() == 0 && k.add(unit(5), cube::kSocketed));
  }
  {
    // The bound: a full cube and a full set of sockets fit (12 + 6), and the list holds kMaxKept; one more is the
    // game's to free.
    static_assert(cube::kMaxKept >= 12 + 6, "a full cube and a socketed item's contents must fit");
    Kept k;
    for (int i = 0; i < cube::kMaxKept; ++i) CHECK(k.add(unit(i), i % 3 ? cube::kIngredient : cube::kSocketed));
    CHECK(k.full() && k.size() == cube::kMaxKept);
    CHECK(!k.add(unit(cube::kMaxKept), cube::kIngredient));
    Unit* order[cube::kMaxKept] = {};
    cube::Kind kinds[cube::kMaxKept] = {};
    CHECK(k.order(order, kinds, cube::kMaxKept) == cube::kMaxKept);
    // Every ingredient before every socket content, each kind in the order kept.
    const int ingredients = k.count(cube::kIngredient);
    CHECK(ingredients + k.count(cube::kSocketed) == cube::kMaxKept);
    int last = -1;
    for (int i = 0; i < ingredients; ++i) {
      CHECK(kinds[i] == cube::kIngredient);
      const int at = static_cast<int>(reinterpret_cast<unsigned char*>(order[i]) - g_units);
      CHECK(at > last && at % 3 != 0);
      last = at;
    }
    last = -1;
    for (int i = ingredients; i < cube::kMaxKept; ++i) {
      CHECK(kinds[i] == cube::kSocketed);
      const int at = static_cast<int>(reinterpret_cast<unsigned char*>(order[i]) - g_units);
      CHECK(at > last && at % 3 == 0);
      last = at;
    }
  }

  if (g_failures) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("test_cube: ok\n");
  return 0;
}
