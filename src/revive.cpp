#include "revive.h"

#include <windows.h>

#include "cheats.h"
#include "game.h"
#include "log.h"
#include "mem.h"
#include "sites.h"

namespace d2rcc::revive {
namespace {

constexpr int kBanks = 4;  // by the unit's data-table bank, 1 classic .. 3 rotw

const D2RL::PluginContext* g_ctx = nullptr;
const D2RL::DataTableService* g_tables = nullptr;
const char* g_why = "the game's routines are not read yet";
volatile LONG g_type[kBanks] = {};  // Revive's pet type in each bank, 0 until read
volatile LONG g_kept = 0;
volatile LONG g_kept_seen = 0;
volatile LONG g_row_warned = 0;

bool on(cheats::Kind k) { return cheats::enabled(k) && !cheats::why_not(k); }

// Revive's pet type from its Skills row of a bank (where the game's Revive reads it), 0 when it cannot be read.
int read_pet_type(D2RL::DataTables::Bank bank) {
  using namespace D2RL::DataTables;
  RowView row{};
  row.structSize = RowViewSize;
  const Result got = g_tables->findRowById
                         ? g_tables->findRowById(g_ctx, bank, TableId::Skills, kReviveSkill, &row)
                         : g_tables->getRow(g_ctx, bank, TableId::Skills, kReviveSkill, &row);
  if (got != Result::Success || !row.row) return 0;
  const uintptr_t at = reinterpret_cast<uintptr_t>(row.row);
  uint16_t id = 0;
  uint8_t type = 0;
  if (row.rowSize != kSkillsRowSize || !mem::read_safe(at, &id) || id != kReviveSkill ||
      !mem::read_safe(at + kSkillsPetType, &type) || type == 0 || type >= kMaxPetTypes) {
    if (log_once(&g_row_warned, LogLevel::kWarning))
      log_warn("permanent revives: Revive's Skills row (%u bytes, id %u, pet type %u) is not laid out as expected - "
               "revives time out as the game has them",
               row.rowSize, id, type);
    return 0;
  }
  return type;
}

}  // namespace

void set_services(const D2RL::PluginContext* ctx, const D2RL::DataTableService* tables) {
  g_ctx = ctx;
  g_tables = tables;
}

bool has_tables() { return g_tables && (g_tables->findRowById || g_tables->getRow); }

const char* bind() {
  g_why = !sites::revive_facts().known ? "the game's revive timer not found as expected"
          : !game::has_pet_lookup()    ? "the game's pet lookup not found"
                                       : nullptr;
  if (g_why)
    logf("permanent revives: %s - revives time out as the game has them", g_why);
  else
    logf("permanent revives: bound (the killself timer 0x%llX, its pet removal 0x%llX, the pet lookup 0x%llX)",
         static_cast<unsigned long long>(sites::result(sites::kKillSelfTimer).rva),
         static_cast<unsigned long long>(sites::revive_facts().pet_removal - sites::exe_base()),
         static_cast<unsigned long long>(sites::result(sites::kPetTypeOf).rva));
  return g_why;
}

void collect() {
  if (!g_ctx || !has_tables()) return;
  for (int b = 1; b < kBanks; ++b) {
    if (g_type[b]) continue;
    const int type = read_pet_type(static_cast<D2RL::DataTables::Bank>(b));
    if (!type) continue;
    InterlockedExchange(&g_type[b], type);
    logf("permanent revives: bank %d: Revive (skill %d) keeps its revives as pet type %d", b, kReviveSkill, type);
  }
}

bool ready(uint8_t bank) { return bank >= 1 && bank < kBanks && g_type[bank] != 0; }

int revive_pet_type(uint8_t bank) { return ready(bank) ? static_cast<int>(g_type[bank]) : -1; }

void timer(TimerFn original, void* game, Unit* monster, int32_t mod, int32_t unique) {
  if (on(cheats::kPermanentRevives) && !g_why && monster && !game::unit_is_dead(monster) &&
      game::owned_by_local_player(monster)) {
    Unit* const player = game::local_server_player();
    const int revive = player ? revive_pet_type(game::unit_table_bank(player)) : -1;
    const int type = revive > 0 ? game::pet_type(player, game::unit_id(monster)) : 0;
    if (keeps(true, true, type, revive)) {
      // The event that brought the timer is used up: with the timer not run, nothing ends the revive any more.
      InterlockedIncrement(&g_kept);
      if (log_first(&g_kept_seen, 4))
        logf("permanent revives: your revive (monster class %u) came to the end of its time and stays until it dies",
             game::unit_class(monster));
      cheats::note("Permanent revives: a revive stays past its time");
      return;
    }
  }
  if (original) original(game, monster, mod, unique);
}

Stats stats() {
  Stats st;
  st.kept = static_cast<unsigned>(g_kept);
  return st;
}

}  // namespace d2rcc::revive
