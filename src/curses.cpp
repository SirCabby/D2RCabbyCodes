#include "curses.h"

#include <windows.h>

#include <cstdio>

#include "cheats.h"
#include "game.h"
#include "log.h"
#include "mem.h"
#include "sites.h"

namespace d2rcc::curses {
namespace {

constexpr int kBanks = 4;  // by the unit's data-table bank, 1 classic .. 3 rotw

const D2RL::PluginContext* g_ctx = nullptr;
const D2RL::DataTableService* g_tables = nullptr;
Curses g_curses[kBanks];
int g_modifier_state[kBanks] = {-1, -1, -1, -1};
volatile LONG g_ready[kBanks] = {};
volatile LONG g_kept_off = 0;
volatile LONG g_ended = 0;
volatile LONG g_kept_logged = 0;
volatile LONG g_ended_logged = 0;
volatile LONG g_row_warned = 0;

bool on(cheats::Kind k) { return cheats::enabled(k) && !cheats::why_not(k); }

// The state a skill puts on its targets, from the skill's row (where the Cursed modifier's step reads it).
int skill_state(D2RL::DataTables::Bank bank, int skill, int offset) {
  using namespace D2RL::DataTables;
  if (!g_tables->getRow || skill <= 0 || offset <= 0 || offset + 2 > static_cast<int>(kSkillsRowSize)) return -1;
  RowView row{};
  row.structSize = RowViewSize;
  if (g_tables->getRow(g_ctx, bank, TableId::Skills, static_cast<uint32_t>(skill), &row) != Result::Success ||
      !row.row || row.rowSize != kSkillsRowSize)
    return -1;
  int16_t state = -1;
  return mem::read_safe(reinterpret_cast<uintptr_t>(row.row) + static_cast<uintptr_t>(offset), &state) ? state : -1;
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
  static uint8_t rows[static_cast<size_t>(kMaxStates) * kStatesRowSize];
  for (int b = 1; b < kBanks; ++b) {
    if (g_ready[b]) continue;
    TableView view{};
    view.structSize = TableViewSize;
    if (g_tables->getTable(g_ctx, static_cast<Bank>(b), TableId::States, &view) != Result::Success || !view.rows ||
        !view.rowCount)
      continue;
    const uint32_t n = view.rowCount < static_cast<uint32_t>(kMaxStates) ? view.rowCount : kMaxStates;
    Curses found;
    if (view.rowSize != kStatesRowSize ||
        !mem::copy_from(rows, reinterpret_cast<uintptr_t>(view.rows), static_cast<size_t>(n) * kStatesRowSize) ||
        !curses_of(rows, n, &found)) {
      if (log_once(&g_row_warned, LogLevel::kWarning))
        log_warn("curses: the States rows (%u of %u bytes) are not laid out as expected - no state is taken for a "
                 "curse, and none is kept off",
                 view.rowCount, view.rowSize);
      continue;
    }
    g_curses[b] = found;
    const sites::CurseFacts& facts = sites::curse_facts();
    g_modifier_state[b] = skill_state(static_cast<Bank>(b), facts.amplify_skill, facts.skill_state);
    InterlockedExchange(&g_ready[b], 1);
    char list[256] = {};
    size_t at = 0;
    for (int i = 0; i < found.count && at < sizeof(list) - 8; ++i)
      at += static_cast<size_t>(std::snprintf(list + at, sizeof(list) - at, "%s%d", i ? " " : "", found.states[i]));
    logf("curses: bank %d States: %u rows; %d curses that can be cured (states %s); the Cursed monster modifier "
         "puts on state %d%s",
         b, n, found.count, list, g_modifier_state[b],
         is_curse(static_cast<uint8_t>(b), g_modifier_state[b]) ? "" : " - not one of them: it is not kept off");
  }
}

bool ready(uint8_t bank) { return bank >= 1 && bank < kBanks && g_ready[bank] != 0; }

bool is_curse(uint8_t bank, int state) { return ready(bank) && state >= 0 && g_curses[bank].has(state); }

int cursed_modifier_state(uint8_t bank) { return ready(bank) ? g_modifier_state[bank] : -1; }

bool keep_off(Unit* target, int state) {
  if (!target || !on(cheats::kCannotBeCursed) || !game::is_local_player(target)) return false;
  if (!is_curse(game::unit_table_bank(target), state)) return false;
  InterlockedIncrement(&g_kept_off);
  if (log_first(&g_kept_logged, 4)) logf("cannot be cursed: a curse (state %d) was not put on you", state);
  cheats::note("Cannot be cursed: a curse was kept off you");
  return true;
}

int end_curses(void* game, Unit* player) {
  if (!game || !player || !game::has_cure()) return 0;
  const uint8_t bank = game::unit_table_bank(player);
  if (!ready(bank)) return 0;
  int n = 0;
  const Curses& curses = g_curses[bank];
  for (int i = 0; i < curses.count; ++i) {
    const int state = curses.states[i];
    if (!game::state_on(player, state)) continue;
    const game::Cured cured = game::cure_state(game, player, state);
    if (!cured.lists && !cured.cleared) continue;
    ++n;
    InterlockedIncrement(&g_ended);
    if (log_first(&g_ended_logged, 4))
      logf("cannot be cursed: the curse on you ended (state %d: %d stat list%s taken off%s)", state, cured.lists,
           cured.lists == 1 ? "" : "s", cured.cleared ? ", the state turned off" : "");
  }
  if (n) cheats::note("Cannot be cursed: the curse on you was removed");
  return n;
}

unsigned kept_off() { return static_cast<unsigned>(g_kept_off); }
unsigned ended() { return static_cast<unsigned>(g_ended); }

}  // namespace d2rcc::curses
