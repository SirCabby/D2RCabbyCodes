#include "autoid.h"

#include <windows.h>

#include "cheats.h"
#include "game.h"
#include "log.h"
#include "sites.h"

namespace d2rcc::autoid {
namespace {

constexpr uint32_t kCubePage = 3;  // an item's page in the Horadric Cube (0 the inventory grid)

const char* g_why = "the pick-up and identify routines are not read yet";
uintptr_t g_pickup_notice = 0;  // where the pick-up routine's notice of the item's new place returns to
uint32_t g_identified = 0;      // the item flag the identify routine tests and sets
volatile LONG g_count = 0;
volatile LONG g_cursor = 0;
volatile LONG g_logged = 0;
volatile LONG g_left_warned = 0;

const char* where(uint32_t mode, uint32_t page) {
  return mode == kCursor  ? "onto the cursor"
         : mode == kBelt  ? "into the belt"
         : mode == kWorn  ? "onto the body"
         : page == kCubePage ? "into the Horadric Cube"
                             : "into the inventory";
}

// The game's identify, then what it did: counted, the first few logged; an item it left unidentified is a warning.
void identify(void* game, Unit* player, Unit* item, uint32_t mode, uint32_t page) {
  game::identify_item(game, player, item);
  if (!(game::item_flags(item) & g_identified)) {
    if (log_once(&g_left_warned, LogLevel::kWarning))
      log_warn("identify on pickup: the game's identify left item class %u (quality %d) unidentified",
               game::unit_class(item), game::item_quality(item));
    return;
  }
  InterlockedIncrement(&g_count);
  if (mode == kCursor) InterlockedIncrement(&g_cursor);
  if (log_first(&g_logged, 4))
    logf("identify on pickup: item class %u (quality %d) identified as it was picked up %s", game::unit_class(item),
         game::item_quality(item), where(mode, page));
}

}  // namespace

const char* bind() {
  const sites::IdentifyFacts& f = sites::identify_facts();
  g_pickup_notice = f.known ? f.pickup_notice : 0;
  g_identified = f.known ? f.identified : 0;
  g_why = !f.known                 ? "the pick-up and identify routines not found"
          : !game::has_identify() ? "the game's identify routine not found"
                                  : nullptr;
  if (g_why)
    logf("identify on pickup: %s - items picked up stay as the game has them", g_why);
  else
    logf("identify on pickup: bound (the pick-up's notice returns to 0x%llX; identified is item flag 0x%X)",
         static_cast<unsigned long long>(g_pickup_notice - sites::exe_base()), g_identified);
  return g_why;
}

void notice(NoticeFn original, void* game, Unit* player, Unit* item, uint32_t flags, uint32_t command, int32_t mode,
            uint32_t page, uint32_t body, uint32_t position, uintptr_t from) {
  if (!original) return;
  When w = When::kNever;
  uint32_t at = 0;
  if (from == g_pickup_notice && g_pickup_notice && !g_why && game && cheats::enabled(cheats::kAutoIdentify) &&
      !cheats::why_not(cheats::kAutoIdentify) && game::is_local_player(player) &&
      game::unit_type(item) == game::kItem) {
    at = game::unit_mode(item);
    w = when(game::item_flags(item), g_identified, at);
  }
  if (w == When::kBefore) identify(game, player, item, at, page);
  original(game, player, item, flags, command, mode, page, body, position);
  if (w == When::kAfter) identify(game, player, item, at, page);
}

Stats stats() {
  Stats st;
  st.identified = static_cast<unsigned>(g_count);
  st.cursor = static_cast<unsigned>(g_cursor);
  return st;
}

}  // namespace d2rcc::autoid
