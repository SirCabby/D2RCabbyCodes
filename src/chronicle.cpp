#include "chronicle.h"

#include <windows.h>

#include "cheats.h"
#include "game.h"
#include "log.h"
#include "mem.h"
#include "sites.h"

namespace d2rcc::chronicle {
namespace {

// A unit's data (the accessor's `mov rax, [rbx+10h]`, which its signature holds).
constexpr uintptr_t kUnitData = 0x10;

// The Chronicle's entries (the game's lists), and the lists they were read from.
dropodds::Rows g_entry_uniques, g_entry_sets;
uint64_t g_list_uniques = 0, g_listed_uniques = 0, g_list_sets = 0, g_listed_sets = 0;
bool g_entries_read = false;
// What the local player's Chronicle has, the rows kept as drops in this game, and what it misses less those.
dropodds::Rows g_found_uniques, g_found_sets;
dropodds::Rows g_dropped_uniques, g_dropped_sets;
dropodds::Rows g_missing_uniques, g_missing_sets;
bool g_found_read = false;  // in this game

volatile LONG g_forget = 0;  // a game was left: the game thread forgets what it read of it
volatile LONG g_stat_read = 0, g_stat_uniques = 0, g_stat_sets = 0, g_stat_uniques_missing = 0,
              g_stat_sets_missing = 0, g_stat_dropped = 0;
volatile LONG g_read_logged = 0;     // this game's first read
volatile LONG g_change_logged = 0;   // ... and what changed since, a few times a game
volatile LONG g_wait_logged = 0;     // the Chronicle could not be read yet (no player), once a game
volatile LONG g_layout_warned = 0;   // memory that does not read as the game keeps it, once a session
volatile LONG g_totals_warned = 0;

bool word(uintptr_t at, uint64_t* out) { return mem::read_safe(at, out); }

// What the game keeps does not read as it was read from its code: the switch is off for the session, and says why.
constexpr const char* kUnreadable = "the game's Chronicle does not read as expected (see the log)";

void forget_game() {
  g_found_uniques.clear();
  g_found_sets.clear();
  g_dropped_uniques.clear();
  g_dropped_sets.clear();
  g_missing_uniques.clear();
  g_missing_sets.clear();
  g_found_read = false;
  InterlockedExchange(&g_read_logged, 0);
  InterlockedExchange(&g_change_logged, 0);
  InterlockedExchange(&g_wait_logged, 0);
}

// The game's lists of the entries there are, read again when they are other ones (a table load makes them anew).
bool read_entries(const sites::ChronicleFacts& f) {
  uint64_t uniques = 0, listed_uniques = 0, sets = 0, listed_sets = 0, unique_total = 0, set_total = 0;
  if (!mem::read_safe(f.unique_list, &uniques) || !mem::read_safe(f.unique_count, &listed_uniques) ||
      !mem::read_safe(f.set_list, &sets) || !mem::read_safe(f.set_count, &listed_sets) ||
      !mem::read_safe(f.unique_total, &unique_total) || !mem::read_safe(f.set_total, &set_total))
    return false;
  if (g_entries_read && uniques == g_list_uniques && listed_uniques == g_listed_uniques && sets == g_list_sets &&
      listed_sets == g_listed_sets)
    return true;
  dropodds::Rows entry_uniques, entry_sets;
  const int u = list_ids(static_cast<uintptr_t>(uniques), listed_uniques, 8, -1, word, &entry_uniques);
  const int s = list_ids(static_cast<uintptr_t>(sets), listed_sets, f.set_entry, 8, word, &entry_sets);
  if (u < 0 || s < 0) {
    if (log_once(&g_layout_warned, LogLevel::kWarning))
      log_warn("chronicle: the game's list of its %s does not read as expected - drops do not go by it",
               u < 0 ? "uniques" : "set items");
    cheats::set_why_not(cheats::kChronicleOnly, kUnreadable);
    return false;
  }
  // What the game counts its shares by: every unique of its list, the set items of the other.
  if (static_cast<uint64_t>(u) != unique_total || static_cast<uint64_t>(s) != set_total ||
      entry_uniques.count() != u || entry_sets.count() != s) {
    if (log_once(&g_totals_warned, LogLevel::kWarning))
      log_warn("chronicle: its lists have %d uniques and %d set items (%d and %d rows), the game counts %llu and %llu "
               "- drops do not go by it",
               u, s, entry_uniques.count(), entry_sets.count(), static_cast<unsigned long long>(unique_total),
               static_cast<unsigned long long>(set_total));
    cheats::set_why_not(cheats::kChronicleOnly, kUnreadable);
    return false;
  }
  g_entry_uniques = entry_uniques;
  g_entry_sets = entry_sets;
  g_list_uniques = uniques;
  g_listed_uniques = listed_uniques;
  g_list_sets = sets;
  g_listed_sets = listed_sets;
  g_entries_read = true;
  return true;
}

// The entries of a kind the Chronicle has not.
int lacking(const dropodds::Rows& entries, const dropodds::Rows& found) {
  dropodds::Rows lack = entries;
  lack.without(found);
  return lack.count();
}

}  // namespace

const char* bind() {
  return sites::chronicle_facts().known ? nullptr : "the game's Chronicle not found";
}

bool read() {
  if (InterlockedExchange(&g_forget, 0)) forget_game();
  const sites::ChronicleFacts& f = sites::chronicle_facts();
  if (!f.known) return false;
  Unit* const player = game::local_server_player();
  const uintptr_t data = player ? mem::read_ptr(reinterpret_cast<uintptr_t>(player) + kUnitData) : 0;
  if (!data) {
    if (log_once(&g_wait_logged))
      logf("chronicle: no player to read it of yet - drops do not go by it until there is");
    return false;
  }
  if (!read_entries(f)) return false;
  const uintptr_t chronicle = data + static_cast<uintptr_t>(f.of_player);
  MapLayout at;
  at.count_at = f.count_at;
  at.buckets_at = f.buckets_at;
  at.next_at = f.next_at;
  at.key_at = f.key_at;
  dropodds::Rows found_uniques, found_sets;
  const int u = ids_of(chronicle + static_cast<uintptr_t>(f.uniques_at), at, word, &found_uniques);
  const int s = ids_of(chronicle + static_cast<uintptr_t>(f.sets_at), at, word, &found_sets);
  if (u < 0 || s < 0) {
    if (log_once(&g_layout_warned, LogLevel::kWarning))
      log_warn("chronicle: the player's map of %s does not read as the game keeps it - drops do not go by it",
               u < 0 ? "uniques" : "set items");
    cheats::set_why_not(cheats::kChronicleOnly, kUnreadable);
    return false;
  }
  const bool changed = g_found_read && !(found_uniques == g_found_uniques && found_sets == g_found_sets);
  g_found_uniques = found_uniques;
  g_found_sets = found_sets;
  g_missing_uniques = g_entry_uniques;
  g_missing_uniques.without(g_found_uniques);
  g_missing_uniques.without(g_dropped_uniques);
  g_missing_sets = g_entry_sets;
  g_missing_sets.without(g_found_sets);
  g_missing_sets.without(g_dropped_sets);
  const int uniques = g_entry_uniques.count(), sets = g_entry_sets.count();
  const int uniques_missing = lacking(g_entry_uniques, g_found_uniques);
  const int sets_missing = lacking(g_entry_sets, g_found_sets);
  const bool first = !g_found_read;
  g_found_read = true;
  InterlockedExchange(&g_stat_uniques, uniques);
  InterlockedExchange(&g_stat_sets, sets);
  InterlockedExchange(&g_stat_uniques_missing, uniques_missing);
  InterlockedExchange(&g_stat_sets_missing, sets_missing);
  InterlockedExchange(&g_stat_read, 1);
  if (first && log_once(&g_read_logged))
    logf("chronicle: %d of its %d uniques and %d of its %d set items found (%d entries in the map of uniques, %d in "
         "the set items'); %d and %d missing",
         uniques - uniques_missing, uniques, sets - sets_missing, sets, u, s, uniques_missing, sets_missing);
  else if (changed && log_first(&g_change_logged, 8))
    logf("chronicle: now %d uniques and %d set items missing", uniques_missing, sets_missing);
  return true;
}

const dropodds::Rows& missing_uniques() { return g_missing_uniques; }
const dropodds::Rows& missing_sets() { return g_missing_sets; }

bool missing(Unit* item) {
  const int32_t row = item ? game::item_file_index(item) : -1;
  if (row < 0) return false;
  const int32_t quality = game::item_quality(item);
  if (quality == game::kQualityUnique) return g_missing_uniques.has(static_cast<uint32_t>(row));
  if (quality == game::kQualitySet) return g_missing_sets.has(static_cast<uint32_t>(row));
  return false;
}

void dropped(Unit* item) {
  const int32_t row = item ? game::item_file_index(item) : -1;
  if (row < 0) return;
  const int32_t quality = game::item_quality(item);
  dropodds::Rows* const dropped = quality == game::kQualityUnique ? &g_dropped_uniques
                                  : quality == game::kQualitySet  ? &g_dropped_sets
                                                                  : nullptr;
  dropodds::Rows* const missing = quality == game::kQualityUnique ? &g_missing_uniques : &g_missing_sets;
  if (!dropped) return;
  dropped->set(static_cast<uint32_t>(row));
  if (!missing->has(static_cast<uint32_t>(row))) return;
  missing->set(static_cast<uint32_t>(row), false);
  InterlockedIncrement(&g_stat_dropped);
}

void game_left() {
  InterlockedExchange(&g_forget, 1);
  InterlockedExchange(&g_stat_read, 0);
  InterlockedExchange(&g_stat_dropped, 0);
}

Stats stats() {
  Stats s;
  s.read = g_stat_read != 0;
  s.uniques = static_cast<int>(g_stat_uniques);
  s.sets = static_cast<int>(g_stat_sets);
  s.uniques_missing = static_cast<int>(g_stat_uniques_missing);
  s.sets_missing = static_cast<int>(g_stat_sets_missing);
  s.dropped = static_cast<int>(g_stat_dropped);
  return s;
}

}  // namespace d2rcc::chronicle
