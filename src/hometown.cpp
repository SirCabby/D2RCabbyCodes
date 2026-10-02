#include "hometown.h"

#include <windows.h>

#include <cstdio>
#include <vector>

#include "cheats.h"
#include "game.h"
#include "log.h"
#include "mem.h"
#include "sites.h"

namespace d2rcc::hometown {
namespace {

constexpr int kBanks = 4;             // by the unit's data-table bank, 1 classic .. 3 rotw
constexpr uint8_t kClassicBank = 1;   // its games end with the fourth act
constexpr unsigned kCollectTicks = 125;  // the tables are tried again every five seconds until they are read
constexpr unsigned kReachTicks = 25;     // the character's waypoints are looked at once a second

const char* const kTownNames[kActs] = {"the Rogue Encampment", "Lut Gholein", "the Kurast Docks",
                                       "the Pandemonium Fortress", "Harrogath"};

const D2RL::PluginContext* g_ctx = nullptr;
const D2RL::DataTableService* g_tables = nullptr;
Towns g_towns[kBanks];
volatile LONG g_ready[kBanks] = {};
bool g_bound = false;
volatile LONG g_wake_hooked = 0;
thread_local Unit* t_cast = nullptr;  // the player whose cast this thread is inside

volatile LONG g_in_game = 0, g_known = 0, g_acts = 0, g_here = -1, g_reach = 0;
volatile LONG g_made = 0, g_trips = 0, g_wakes = 0, g_asked = 0;
volatile LONG g_made_logged = 0, g_trip_logged = 0, g_wake_logged = 0, g_unreached_logged = 0,
              g_wake_unreached_logged = 0, g_far_logged = 0, g_still_dead_logged = 0, g_asked_logged = 0,
              g_row_warned = 0;

bool on(cheats::Kind k) { return cheats::enabled(k) && !cheats::why_not(k); }

bool ready(uint8_t bank) { return bank >= 1 && bank < kBanks && g_ready[bank] != 0; }

// A copy of a table's rows (game thread), empty when the table is not there or its rows have another size.
std::vector<uint8_t> rows_of(D2RL::DataTables::Bank bank, D2RL::DataTables::TableId table, uint32_t row_size,
                             uint32_t most, uint32_t* count) {
  using namespace D2RL::DataTables;
  std::vector<uint8_t> out;
  *count = 0;
  TableView view{};
  view.structSize = TableViewSize;
  if (g_tables->getTable(g_ctx, bank, table, &view) != Result::Success || !view.rows || !view.rowCount) return out;
  if (view.rowSize != row_size || view.rowCount > most) {
    if (log_once(&g_row_warned, LogLevel::kWarning))
      log_warn("home town: table %u has %u rows of %u bytes, not rows of %u - town portals and wakes stay the "
               "game's own",
               static_cast<unsigned>(table), view.rowCount, view.rowSize, row_size);
    return out;
  }
  out.resize(static_cast<size_t>(view.rowCount) * row_size);
  if (!mem::copy_from(out.data(), reinterpret_cast<uintptr_t>(view.rows), out.size())) out.clear();
  else *count = view.rowCount;
  return out;
}

void collect() {
  using namespace D2RL::DataTables;
  if (!g_ctx || !has_tables()) return;
  for (int b = 1; b < kBanks; ++b) {
    if (g_ready[b]) continue;
    uint32_t acts = 0, levels = 0;
    const std::vector<uint8_t> actinfo =
        rows_of(static_cast<Bank>(b), TableId::ActInfo, kActInfoRowSize, static_cast<uint32_t>(kActs), &acts);
    const std::vector<uint8_t> level_rows =
        rows_of(static_cast<Bank>(b), TableId::Levels, kLevelsRowSize, static_cast<uint32_t>(kMaxLevels), &levels);
    if (actinfo.empty() || level_rows.empty()) continue;
    Towns towns;
    if (!towns_of(actinfo.data(), acts, level_rows.data(), levels, &towns)) {
      if (log_once(&g_row_warned, LogLevel::kWarning))
        log_warn("home town: the ActInfo and Levels rows of bank %d do not name a town with a waypoint for every "
                 "act - town portals and wakes stay the game's own",
                 b);
      continue;
    }
    if (b == kClassicBank && towns.acts > kActs - 1) towns.acts = kActs - 1;
    g_towns[b] = towns;
    InterlockedExchange(&g_ready[b], 1);
    char list[160] = {};
    size_t at = 0;
    for (int a = 0; a < towns.acts && at < sizeof(list) - 24; ++a)
      at += static_cast<size_t>(
          std::snprintf(list + at, sizeof(list) - at, "%s%d (waypoint %d)", a ? ", " : "", towns.level[a], towns.waypoint[a]));
    logf("home town: bank %d: %d acts, their towns levels %s; %d levels", b, towns.acts, list, towns.levels);
  }
}

// Bit a: the character has the waypoint of act a's town.
unsigned reach_of(void* game, Unit* player, const Towns& towns) {
  unsigned reach = 0;
  for (int a = 0; a < towns.acts; ++a)
    if (game::has_waypoint(game, player, towns.waypoint[a])) reach |= 1u << a;
  return reach;
}

}  // namespace

void set_services(const D2RL::PluginContext* ctx, const D2RL::DataTableService* tables) {
  g_ctx = ctx;
  g_tables = tables;
}

bool has_tables() { return g_tables && g_tables->getTable; }

const char* bind() {
  const sites::PortalFacts& facts = sites::portal_facts();
  g_bound = false;
  if (!game::has_town_travel() || !game::has_state_test() || !facts.portal_class || !facts.town_spot ||
      !facts.used_state || !facts.class_offset || !facts.other_level_offset)
    return "town portal routines not found";
  if (!has_tables()) return "the game's act table is unavailable";
  g_bound = true;
  const sites::WakeFacts& wake = sites::wake_facts();
  if (wake.known)
    logf("home town: bound (portals of object class %d, a town's portal spot %d; a death wakes a player in mode %d at "
         "a town's spot %d)",
         facts.portal_class, facts.town_spot, wake.dead_mode, wake.town_spot);
  else
    logf("home town: bound (portals of object class %d, a town's portal spot %d); a death's wake stays the game's "
         "own (its handler is not as expected)",
         facts.portal_class, facts.town_spot);
  return nullptr;
}

void on_tick(void* game, Unit* player, unsigned tick) {
  if (!g_bound || !game || !player) return;
  InterlockedExchange(&g_in_game, 1);
  const uint8_t bank = game::unit_table_bank(player);
  if (!ready(bank)) {
    if ((tick % kCollectTicks) == 1) collect();
    if (!ready(bank)) return;
  }
  if ((tick % kReachTicks) != 1 && g_known) return;
  const Towns& towns = g_towns[bank];
  InterlockedExchange(&g_acts, towns.acts);
  InterlockedExchange(&g_here, towns.act(game::unit_level_id(player)));
  InterlockedExchange(&g_reach, static_cast<LONG>(reach_of(game, player, towns)));
  InterlockedExchange(&g_known, 1);
}

void game_left() {
  InterlockedExchange(&g_in_game, 0);
  InterlockedExchange(&g_known, 0);
  InterlockedExchange(&g_here, -1);
  InterlockedExchange(&g_reach, 0);
}

Cast::Cast(Unit* player) : before_(t_cast) { t_cast = player; }
Cast::~Cast() { t_cast = before_; }

int town_end_level(void* game, Unit* player, Unit* portal, int destination) {
  if (!g_bound || !game || !player || player != t_cast || !on(cheats::kHomeTown) ||
      !game::is_local_player(player))
    return destination;
  const sites::PortalFacts& facts = sites::portal_facts();
  if (game::unit_type(portal) != game::kObject ||
      game::unit_class(portal) != static_cast<uint32_t>(facts.portal_class))
    return destination;
  const uint8_t bank = game::unit_table_bank(player);
  if (!ready(bank)) return destination;
  const Towns& towns = g_towns[bank];
  // A cast's portal leads to the town of the act it is read in.
  const int here = towns.act(destination);
  if (here < 0 || here >= towns.acts || towns.level[here] != destination) return destination;
  const int choice = cheats::home_town();
  const int act = chosen_act(choice, here, reach_of(game, player, towns), towns.acts);
  if (act == here) {
    if (choice >= 1 && choice <= kActs && choice - 1 != here && log_first(&g_unreached_logged, 2))
      logf("home town: the character has not the waypoint of %s - the portal leads to %s, the game's own",
           town_name(choice - 1), town_name(here));
    return destination;
  }
  const bool had = game::act_loaded(game, act);
  if (!game::load_act(game, act)) {
    log_warn("home town: act %d could not be made for this game - the portal leads to %s, the game's own", act + 1,
             town_name(here));
    return destination;
  }
  if (!had) logf("home town: act %d made for this game (no player had been there yet)", act + 1);
  return towns.level[act];
}

void town_end_made(Unit* player, Unit* portal, Unit* made, int level, int destination) {
  if (!made) {
    // The game takes the portal beside the player away when it cannot make the other.
    log_warn("home town: the portal in level %d (instead of %d) could not be made - the cast made no portal",
             level, destination);
    return;
  }
  // The game may put the portal somewhere of its own choosing: a town portal read in Tal Rasha's Chamber once
  // Duriel is dead stands at a spot of its own in Lut Gholein, whatever destination its maker is handed. Such a
  // pair is the game's own, both ends in one act, and is left as the game made it.
  const int stands = game::unit_level_id(made);
  if (stands != level) {
    logf("home town: the game put the portal in level %d itself, not in level %d - it leads there, as the game "
         "has it",
         stands, level);
    return;
  }
  game::set_object_destination(portal, level);
  InterlockedIncrement(&g_made);
  const uint8_t bank = game::unit_table_bank(player);
  const int act = ready(bank) ? g_towns[bank].act(level) : -1;
  if (log_first(&g_made_logged, 4))
    logf("home town: the portal leads to %s (level %d, not the game's own %d); the portal there leads back",
         town_name(act), level, destination);
  cheats::note("Home town: the portal leads to %s", town_name(act));
}

Used use(void** context) {
  if (!g_bound || !context) return Used::kGameOwn;
  const sites::PortalFacts& facts = sites::portal_facts();
  const uintptr_t ctx = reinterpret_cast<uintptr_t>(context);
  void* game = reinterpret_cast<void*>(mem::read_ptr(ctx));
  Unit* object = reinterpret_cast<Unit*>(mem::read_ptr(ctx + sizeof(uintptr_t)));
  Unit* player = reinterpret_cast<Unit*>(mem::read_ptr(ctx + 2 * sizeof(uintptr_t)));
  int32_t object_class = -1;
  if (!game || !object || !player || !mem::read_safe(ctx + static_cast<uintptr_t>(facts.class_offset), &object_class) ||
      object_class != facts.portal_class || game::unit_type(object) != game::kObject ||
      game::unit_class(object) != static_cast<uint32_t>(object_class) || game::unit_type(player) != game::kPlayer)
    return Used::kGameOwn;
  const uint8_t bank = game::unit_table_bank(player);
  if (!ready(bank)) return Used::kGameOwn;
  const Towns& towns = g_towns[bank];
  // The act the portal stands in (its user stands beside it) and the one it leads into.
  const int destination = game::object_destination(object);
  const int here = towns.act(game::unit_level_id(object));
  const int there = towns.act(destination);
  const Use what = use_of(here, there, towns.is_town(destination));
  if (what == Use::kGameOwn) return Used::kGameOwn;
  // A portal into another act: the game's use would move the player as inside one act, so it never runs here.
  // What it refuses is refused: a portal that is not open yet, a user just through a portal, a dead one.
  if (!game::is_local_player(player) || game::unit_mode(object) != game::kObjectModeOpen ||
      game::unit_is_dead(player) || game::state_on(player, facts.used_state))
    return Used::kRefused;
  if (there >= towns.acts || !game::load_act(game, there)) {
    log_warn("home town: act %d is not there for a portal that leads into it - the use is refused", there + 1);
    return Used::kRefused;
  }
  game::warp_to_level(game, player, towns.level[there], facts.town_spot);
  const int now = game::unit_level_id(player);
  if (towns.act(now) != there) {
    log_warn("home town: the move into act %d did not arrive (the character stands in level %d) - the use is "
             "refused",
             there + 1, now);
    return Used::kRefused;
  }
  InterlockedIncrement(&g_trips);
  if (log_first(&g_trip_logged, 4))
    logf("home town: a portal from act %d into act %d used: the character taken to %s%s", here + 1, there + 1,
         town_name(there),
         what == Use::kToTown ? ", where the portal leads" : ", the game's own use takes it on to the portal");
  return what == Use::kToTown ? Used::kDone : Used::kGameOwn;
}

bool dying(Unit* player) {
  const sites::WakeFacts& wake = sites::wake_facts();
  return g_bound && wake.known && player && on(cheats::kHomeTown) && game::is_local_player(player) &&
         game::unit_type(player) == game::kPlayer &&
         game::unit_mode(player) == static_cast<uint32_t>(wake.dead_mode);
}

void woke(void* game, Unit* player) {
  const sites::WakeFacts& wake = sites::wake_facts();
  if (!g_bound || !wake.known || !game || !player || !on(cheats::kHomeTown)) return;
  if (game::unit_is_dead(player)) {
    // The game did not wake it (a hardcore character stays dead): nothing to take anywhere.
    if (log_first(&g_still_dead_logged, 1))
      logf("home town: the game left the character dead at a wake request - nothing is done");
    return;
  }
  const uint8_t bank = game::unit_table_bank(player);
  if (!ready(bank)) return;
  const Towns& towns = g_towns[bank];
  // The game woke it in the town of the act it died in.
  const int stands = game::unit_level_id(player);
  const int here = towns.act(stands);
  if (here < 0 || here >= towns.acts || towns.level[here] != stands) {
    if (log_first(&g_far_logged, 2))
      logf("home town: the game woke the character in level %d, which is no act's town - it stays there", stands);
    return;
  }
  const int choice = cheats::home_town();
  const int act = chosen_act(choice, here, reach_of(game, player, towns), towns.acts);
  if (act == here) {
    if (choice >= 1 && choice <= kActs && choice - 1 != here && log_first(&g_wake_unreached_logged, 2))
      logf("home town: the character has not the waypoint of %s - it woke in %s, the game's own", town_name(choice - 1),
           town_name(here));
    return;
  }
  const bool had = game::act_loaded(game, act);
  if (!game::load_act(game, act)) {
    log_warn("home town: act %d could not be made for this game - the character woke in %s, the game's own", act + 1,
             town_name(here));
    return;
  }
  if (!had) logf("home town: act %d made for this game (no player had been there yet)", act + 1);
  game::warp_to_level(game, player, towns.level[act], wake.town_spot);
  const int now = game::unit_level_id(player);
  if (now != towns.level[act]) {
    log_warn("home town: the move to %s after a death did not arrive (the character stands in level %d)",
             town_name(act), now);
    return;
  }
  InterlockedIncrement(&g_wakes);
  if (log_first(&g_wake_logged, 4))
    logf("home town: a death in act %d: the character woke in %s (level %d), taken on from %s, the game's own",
         here + 1, town_name(act), towns.level[act], town_name(here));
  cheats::note("Home town: you woke in %s", town_name(act));
}

bool wakes_at_home() { return g_bound && g_wake_hooked != 0 && sites::wake_facts().known; }
void wake_hooked(bool hooked) { InterlockedExchange(&g_wake_hooked, hooked ? 1 : 0); }

bool leads_across_acts(Unit* player, Unit* portal) {
  const sites::PortalFacts& facts = sites::portal_facts();
  if (!g_bound || !portal || facts.other_level_offset <= 0 || game::unit_type(portal) != game::kObject ||
      game::unit_class(portal) != static_cast<uint32_t>(facts.portal_class))
    return false;
  // The level the portal's other end stands in, as the server told the client; none (0): the client makes no trip.
  uint16_t other = 0;
  if (!mem::read_safe(reinterpret_cast<uintptr_t>(portal) + static_cast<uintptr_t>(facts.other_level_offset),
                      &other) ||
      !other)
    return false;
  // The towns of the portal's tables, else of its user's: no portal into another act was made before they were
  // read for the character.
  uint8_t bank = game::unit_table_bank(portal);
  if (!ready(bank)) bank = game::unit_table_bank(player);
  if (!ready(bank)) return false;
  const Towns& towns = g_towns[bank];
  // The act the portal stands in; its user stands beside it.
  int stands = game::unit_level_id(portal);
  if (towns.act(stands) < 0) stands = game::unit_level_id(player);
  const int here = towns.act(stands);
  const int there = towns.act(other);
  if (here < 0 || there < 0 || here == there) return false;
  InterlockedIncrement(&g_asked);
  if (log_first(&g_asked_logged, 4))
    logf("home town: the portal in level %d leads into act %d (level %d): the client makes no trip of its own and "
         "asks the server",
         stands, there + 1, static_cast<int>(other));
  return true;
}

Stats stats() {
  Stats s;
  s.in_game = g_in_game != 0;
  s.known = g_known != 0;
  s.acts = static_cast<int>(g_acts);
  s.here = static_cast<int>(g_here);
  s.reach = static_cast<unsigned>(g_reach);
  s.made = static_cast<unsigned>(g_made);
  s.trips = static_cast<unsigned>(g_trips);
  s.wakes = static_cast<unsigned>(g_wakes);
  s.asked = static_cast<unsigned>(g_asked);
  return s;
}

const char* town_name(int act) { return act >= 0 && act < kActs ? kTownNames[act] : "a town"; }

}  // namespace d2rcc::hometown
