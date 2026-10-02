#include "passive.h"

#include <windows.h>

#include "cheats.h"
#include "game.h"
#include "log.h"
#include "sites.h"

namespace d2rcc::passive {
namespace {

const char* g_why = "the AI routines are not read yet";
thread_local Mark t_mark;
volatile LONG g_up = 0;  // threads with the mark up (the server thread, while a kept pet thinks): a question asked
                         // while none is goes straight to the game, without a look at the thread's mark
volatile LONG g_ticks = 0;
volatile LONG g_held = 0;
volatile LONG g_refused = 0;
volatile LONG g_kept = 0;
volatile LONG g_held_seen = 0;
volatile LONG g_kept_seen = 0;

bool on(cheats::Kind k) { return cheats::enabled(k) && !cheats::why_not(k); }

// What a kept pet's AI tick came to: counted, the first few that left an enemy alone logged.
void report(Unit* unit, bool mercenary, const Mark& tick) {
  InterlockedIncrement(&g_ticks);
  if (!tick.refused && !tick.kept) return;
  InterlockedIncrement(&g_held);
  InterlockedExchangeAdd(&g_refused, static_cast<LONG>(tick.refused));
  InterlockedExchangeAdd(&g_kept, static_cast<LONG>(tick.kept));
  const char* const which = mercenary ? "mercenary" : "minions";
  const char* const who = mercenary ? "mercenary" : "minion";
  if (tick.kept && log_first(&g_kept_seen, 4))
    logf("passive %s: your %s (monster class %u) left aside the target kept on it (kind %u)", which, who,
         game::unit_class(unit), tick.kept_kind);
  else if (tick.refused && log_first(&g_held_seen, 4))
    logf("passive %s: your %s (monster class %u) had an enemy to take on and left it (%u enemy test%s answered no in "
         "its AI tick)",
         which, who, game::unit_class(unit), tick.refused, tick.refused == 1 ? "" : "s");
}

}  // namespace

const char* bind() {
  g_why = !sites::enemy_test_checked() ? "the game's enemy test not found as expected" : nullptr;
  if (g_why)
    logf("passive pets: %s - your mercenary and minions fight as the game has them", g_why);
  else
    logf("passive pets: bound (the AI dispatcher 0x%llX, the enemy test 0x%llX, the kept target's getter 0x%llX)",
         static_cast<unsigned long long>(sites::result(sites::kAiDispatch).rva),
         static_cast<unsigned long long>(sites::result(sites::kEnemyTest).rva),
         static_cast<unsigned long long>(sites::result(sites::kKeptTarget).rva));
  return g_why;
}

void think(DispatchFn original, void* game, Unit* unit, uint64_t a3, uint64_t a4, uint64_t a5) {
  if (!original) return;
  const bool merc_on = on(cheats::kPassiveMerc), minions_on = on(cheats::kPassivePets);
  bool kept = false, mercenary = false;
  if ((merc_on || minions_on) && !g_why) {
    const bool owned = game::owned_by_local_player(unit);
    mercenary = owned && game::is_mercenary(unit);
    kept = keeps(owned, mercenary, merc_on, minions_on);
  }
  // Any other monster's tick, with no kept pet's tick around it on this thread: the game's, nothing marked.
  if (!kept && (!g_up || !t_mark.up)) {
    original(game, unit, a3, a4, a5);
    return;
  }
  if (kept) InterlockedIncrement(&g_up);
  const Mark tick = marked(t_mark, kept, [&] { original(game, unit, a3, a4, a5); });
  if (kept) {
    InterlockedDecrement(&g_up);
    report(unit, mercenary, tick);
  }
}

uint64_t enemy(EnemyTestFn original, void* game, Unit* unit, Unit* other, uint64_t flag) {
  const uint64_t game_answer = original ? original(game, unit, other, flag) : 0;
  return g_up ? answer(t_mark, game_answer) : game_answer;
}

uint64_t kept_target(KeptTargetFn original, Unit* monster) {
  const uint64_t kind = original ? original(monster) : 0;
  return g_up ? kept_kind(t_mark, kind) : kind;
}

Stats stats() {
  Stats st;
  st.ticks = static_cast<unsigned>(g_ticks);
  st.held = static_cast<unsigned>(g_held);
  st.refused = static_cast<unsigned>(g_refused);
  st.kept = static_cast<unsigned>(g_kept);
  return st;
}

}  // namespace d2rcc::passive
