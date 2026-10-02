#include "cube.h"

#include <windows.h>

#include "cheats.h"
#include "game.h"
#include "log.h"
#include "sites.h"

namespace d2rcc::cube {
namespace {

// One transmute of the local player's under the switch, on the thread that runs it (the server's).
struct Mark {
  bool on = false;
  Kept kept;
};
thread_local Mark t_mark;

const char* g_why = "the cube's routines are not read yet";
uintptr_t g_ingredient_free = 0;  // where the product routine's free of an ingredient returns to
uintptr_t g_sockets_free = 0;     // ... and its free of Clear Sockets' socket contents
volatile LONG g_sockets_hooked = 0;
volatile LONG g_transmutes = 0;
volatile LONG g_kept[kKinds] = {};
volatile LONG g_inventory = 0;
volatile LONG g_ground = 0;
volatile LONG g_lost = 0;
volatile LONG g_logged = 0;
volatile LONG g_lost_warned = 0;
volatile LONG g_full_warned = 0;

}  // namespace

const char* bind() {
  const sites::CubeFacts& f = sites::cube_facts();
  g_ingredient_free = f.known ? f.ingredient_free : 0;
  g_sockets_free = f.known && game::has_socket_takeout() ? f.sockets_free : 0;
  g_why = !f.known                     ? "the cube's routines not found"
          : !game::has_item_handover() ? "the game's item handover not found"
                                       : nullptr;
  if (g_why)
    logf("cube: %s - a recipe uses up its ingredients as the game has it", g_why);
  else
    logf("cube: bound (an ingredient's free returns to 0x%llX; Clear Sockets' socket contents %s)",
         static_cast<unsigned long long>(g_ingredient_free - sites::exe_base()),
         g_sockets_free ? "kept too" : "destroyed as the game has it (not found)");
  return g_why;
}

void hooked(bool sockets) { InterlockedExchange(&g_sockets_hooked, sockets && g_sockets_free ? 1 : 0); }
bool sockets_kept() { return g_sockets_hooked != 0; }

void products(ProductsFn original, FreeFn free_item, void* game, Unit* player, void* recipe, void* inputs) {
  if (!original) return;
  const bool mark = free_item && !g_why && !t_mark.on && game && cheats::enabled(cheats::kInfiniteCubeIngredients) &&
                    !cheats::why_not(cheats::kInfiniteCubeIngredients) && game::is_local_player(player);
  if (!mark) {
    original(game, player, recipe, inputs);
    return;
  }
  t_mark.kept.clear();
  t_mark.on = true;
  original(game, player, recipe, inputs);
  t_mark.on = false;
  Unit* order[kMaxKept] = {};
  Kind kinds[kMaxKept] = {};
  const int n = t_mark.kept.order(order, kinds, kMaxKept);
  const int ingredients = t_mark.kept.count(kIngredient), socketed = t_mark.kept.count(kSocketed);
  t_mark.kept.clear();
  if (n <= 0) return;  // nothing used up: no product was made, or the recipe changed its only ingredient
  int in_inventory = 0, at_feet = 0, lost = 0;
  for (int i = 0; i < n; ++i) {
    if (game::put_in_inventory(game, player, order[i])) {
      ++in_inventory;
    } else if (game::put_at_feet(game, player, order[i])) {
      ++at_feet;
    } else {
      // No room in the inventory and no spot on the ground near the player: freed, as the game would have.
      free_item(game, order[i]);
      ++lost;
    }
  }
  InterlockedIncrement(&g_transmutes);
  InterlockedExchangeAdd(&g_kept[kIngredient], ingredients);
  InterlockedExchangeAdd(&g_kept[kSocketed], socketed);
  InterlockedExchangeAdd(&g_inventory, in_inventory);
  InterlockedExchangeAdd(&g_ground, at_feet);
  InterlockedExchangeAdd(&g_lost, lost);
  if (log_first(&g_logged, 4))
    logf("cube: a transmute kept %d ingredient%s and %d socket content%s: %d into the inventory, %d at your feet, %d "
         "with no place anywhere (freed)",
         ingredients, ingredients == 1 ? "" : "s", socketed, socketed == 1 ? "" : "s", in_inventory, at_feet, lost);
  if (lost && log_first(&g_lost_warned, 4, LogLevel::kWarning))
    log_warn("cube: %d of the items a transmute kept had no room in the inventory and no spot on the ground - freed "
             "as the game would have",
             lost);
  cheats::note("Cube: %d item%s back%s", n - lost, n - lost == 1 ? "" : "s", at_feet ? " (some at your feet)" : "");
}

bool keep_ingredient(Unit* item, uintptr_t from) {
  if (!t_mark.on || !item || from != g_ingredient_free || game::unit_type(item) != game::kItem) return false;
  if (t_mark.kept.add(item, kIngredient)) return true;
  if (t_mark.kept.full() && log_once(&g_full_warned, LogLevel::kWarning))
    log_warn("cube: a transmute used up more than %d items - the rest are used up as the game has it", kMaxKept);
  return false;
}

void keep_socketed(void* game, Unit* item, uintptr_t from, FreeFn free_item) {
  if (!t_mark.on || !g_sockets_hooked || !item || from != g_sockets_free) return;
  const int room = kMaxKept - t_mark.kept.size();
  if (room <= 0) return;  // the game's own free takes them, as it would have
  Unit* taken[kMaxKept] = {};
  const int n = game::take_out_socketed(item, taken, room);
  for (int i = 0; i < n; ++i)
    if (!t_mark.kept.add(taken[i], kSocketed) && free_item && game) free_item(game, taken[i]);
}

Stats stats() {
  Stats st;
  st.transmutes = static_cast<unsigned>(g_transmutes);
  for (int k = 0; k < kKinds; ++k) st.kept[k] = static_cast<unsigned>(g_kept[k]);
  st.inventory = static_cast<unsigned>(g_inventory);
  st.ground = static_cast<unsigned>(g_ground);
  st.lost = static_cast<unsigned>(g_lost);
  return st;
}

}  // namespace d2rcc::cube
