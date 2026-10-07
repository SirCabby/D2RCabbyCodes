#include "enemies.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

#include "cheats.h"
#include "game.h"
#include "log.h"
#include "mem.h"
#include "sites.h"

namespace d2rcc::enemies {
namespace {

constexpr int kBanks = 4;        // by the unit's data-table bank, 1 classic .. 3 rotw
constexpr uint32_t kMonster = 1;
constexpr size_t kContextDefender = 0x18;    // the resistance step's context: the defender (its signature reads it)
constexpr size_t kContextMonster = 0x24;     // ... nonzero for a defender that is a monster and no hireling
constexpr size_t kDamageTypeStat = 0x08;     // a damage type: its resistance stat (-1 none)

const D2RL::PluginContext* g_ctx = nullptr;
const D2RL::DataTableService* g_tables = nullptr;
const char* g_why_immunities = "the game's routines are not read yet";
const char* g_why_affixes = "the game's routines are not read yet";
const char* g_why_display = "the game's routines are not read yet";
volatile LONG g_shown = 0, g_shown_seen = 0;
constexpr uint32_t kNoId = 0xFFFFFFFFu;
constexpr int kMaxPets = 256;  // nodes of the client's pet list walked at most
volatile LONG g_ready[kBanks] = {};       // the bank's MonUMod rows are read
volatile LONG64 g_affix[kBanks] = {};     // ... a bit a mod: an affix
volatile LONG64 g_retry_at[kBanks] = {};  // a bank that could not be read is tried again from then on (ms)
volatile LONG g_immunities = 0, g_affixes = 0, g_refused = 0;
volatile LONG g_immunity_seen = 0, g_affix_seen = 0, g_refused_seen = 0, g_stripped_seen = 0, g_row_warned = 0;
constexpr size_t kUnitData = 0x10;  // a unit's data (a monster's: its mods at sites::monster_mods_at())
volatile LONG g_placeholder_ok = 0;  // the placeholder's server maker is checked to be none
constexpr uint16_t kNamedKinds = 0x20A;  // a monster's kind bits: super unique 2, unique 8, Herald 0x200
volatile LONG g_placeholders = 0, g_placeholder_seen = 0;
// The monsters a roller was refused an affix for on this thread, until their mods' makers run (a few at once at most: a
// maker rolls one monster's mods and runs them before it makes the next).
constexpr int kRefusedRing = 8;
thread_local Unit* t_refused[kRefusedRing] = {};
thread_local int t_refused_next = 0;

// True when the monster was not noted yet (a roll offers the test every affix row, one after another).
bool note_refused(Unit* monster) {
  for (Unit* u : t_refused)
    if (u == monster) return false;
  t_refused[t_refused_next] = monster;
  t_refused_next = (t_refused_next + 1) % kRefusedRing;
  return true;
}

bool take_refused(Unit* monster) {
  for (Unit*& u : t_refused)
    if (u == monster) {
      u = nullptr;
      return true;
    }
  return false;
}

bool on(cheats::Kind k) { return cheats::enabled(k) && !cheats::why_not(k); }

// A monster that is not the local player's (its mercenary, its summons, its revives).
bool enemy(Unit* u) { return u && game::unit_type(u) == kMonster && !game::owned_by_local_player(u); }

// Client: whether the client's pet list has the unit as a pet of the local player's (any kind, not gone); the list's
// layout is the one ignore item requirements reads (sites::requirement_facts()).
bool client_pet_of_mine(Unit* u) {
  const sites::RequirementFacts& f = sites::requirement_facts();
  const uint32_t me = game::local_player_id(), id = game::unit_id(u);
  if (!f.client_pets || me == kNoId) return false;
  uintptr_t node = mem::read_ptr(f.client_pets);
  for (int n = 0; node && n < kMaxPets; ++n) {
    int32_t gone = 0;
    uint32_t owner = 0, pet = 0;
    if (!mem::read_safe(node + static_cast<uintptr_t>(f.pet_owner_at), &owner) ||
        !mem::read_safe(node + static_cast<uintptr_t>(f.pet_gone_at), &gone) ||
        !mem::read_safe(node + static_cast<uintptr_t>(f.pet_id_at), &pet))
      return false;
    if (pet == id && owner == me && gone == 0) return true;
    node = mem::read_ptr(node + static_cast<uintptr_t>(f.pet_next_at));
  }
  return false;
}

// Game thread: which mods of a bank are affixes, from its MonUMod rows.
bool read_bank(int b) {
  using namespace D2RL::DataTables;
  if (!g_ctx || !has_tables()) return false;
  TableView view{};
  view.structSize = TableViewSize;
  if (g_tables->getTable(g_ctx, static_cast<Bank>(b), TableId::MonUMod, &view) != Result::Success || !view.rows ||
      !view.rowCount)
    return false;
  uint8_t row[kMonUModRowSize];
  const uint32_t n = view.rowCount < static_cast<uint32_t>(kMaxMods) ? view.rowCount : kMaxMods;
  LONG64 bits = 0;
  int count = 0;
  bool laid_out = view.rowSize == kMonUModRowSize;
  char list[192] = {};
  size_t at = 0;
  for (uint32_t i = 0; laid_out && i < n; ++i) {
    uint32_t id = 0;
    if (!mem::copy_from(row, reinterpret_cast<uintptr_t>(view.rows) + static_cast<uintptr_t>(i) * kMonUModRowSize,
                        sizeof(row))) {
      laid_out = false;
      break;
    }
    std::memcpy(&id, row, sizeof(id));
    if (id != i) laid_out = false;  // a row is its mod's id
    if (laid_out && i == kPlaceholderMod && row_is_affix(row)) laid_out = false;  // the placeholder must not be one
    if (laid_out && row_is_affix(row)) {
      bits |= LONG64{1} << i;
      ++count;
      if (at < sizeof(list) - 8)
        at += static_cast<size_t>(std::snprintf(list + at, sizeof(list) - at, "%s%u", count > 1 ? " " : "", i));
    }
  }
  if (!laid_out || count == 0) {
    if (log_once(&g_row_warned, LogLevel::kWarning))
      log_warn("enemy affixes: the MonUMod rows (%u of %u bytes) are not laid out as expected - monsters keep their "
               "affixes",
               view.rowCount, view.rowSize);
    return false;
  }
  InterlockedExchange64(&g_affix[b], bits);
  InterlockedExchange(&g_ready[b], 1);
  logf("enemy affixes: bank %d MonUMod: %u mods; %d affixes the game rolls for uniques (mods %s)", b, n, count, list);
  return true;
}

// The affixes of the monster's bank (a bit a mod), 0 while unknown; the bank is read the first time it is asked (game
// thread), again a second later when it could not be.
uint64_t affixes_of(Unit* monster) {
  const uint8_t b = game::unit_table_bank(monster);
  if (b < 1 || b >= kBanks) return 0;
  if (!g_ready[b]) {
    const LONG64 now = static_cast<LONG64>(GetTickCount64());
    if (now < g_retry_at[b] || !read_bank(b)) {
      if (now >= g_retry_at[b]) InterlockedExchange64(&g_retry_at[b], now + 1000);
      return 0;
    }
  }
  return static_cast<uint64_t>(g_affix[b]);
}

bool is_affix(Unit* monster, int32_t mod) {
  return mod >= 0 && mod < kMaxMods && ((affixes_of(monster) >> mod) & 1);
}

}  // namespace

void set_services(const D2RL::PluginContext* ctx, const D2RL::DataTableService* tables) {
  g_ctx = ctx;
  g_tables = tables;
}

bool has_tables() { return g_tables && g_tables->getTable; }

const char* bind_immunities() {
  g_why_immunities = !sites::resist_adjust_checked() ? "the game's resistance step not found as expected" : nullptr;
  if (g_why_immunities)
    logf("enemy immunities: %s - enemies keep their immunities", g_why_immunities);
  else
    logf("enemy immunities: bound (a hit's resistance step 0x%llX hands the defender's resistance to 0x%llX)",
         static_cast<unsigned long long>(sites::result(sites::kResistComponent).rva),
         static_cast<unsigned long long>(sites::result(sites::kResistAdjust).rva));
  return g_why_immunities;
}

const char* bind_affixes() {
  g_why_affixes = !sites::found(sites::kMonsterModAdd) || !sites::found(sites::kMonsterModTest) ||
                          !sites::found(sites::kMonsterModsRun)
                      ? "the game's monster mod routines not found"
                  : !sites::monster_mods_at() ? "a monster's mods not found where expected"
                  : !has_tables()             ? "the game's monster mod table is unavailable"
                                              : nullptr;
  // The placeholder kept in a list that would be empty must have no maker on the server (the client's side, its event
  // routines and its text are the record's: CLAUDE.md, monster mods).
  // The client's handler table, when its mod loop is found, must have none for it either.
  uintptr_t maker = 1, handler = 0;
  const uintptr_t makers = sites::monster_mod_makers(), handlers = sites::client_mod_handlers();
  InterlockedExchange(&g_placeholder_ok,
                      makers && mem::read_safe(makers + kPlaceholderMod * sizeof(uintptr_t), &maker) && maker == 0 &&
                          (!handlers || (mem::read_safe(handlers + kPlaceholderMod * sizeof(uintptr_t), &handler) &&
                                         handler == 0)));
  if (!g_why_affixes && !g_placeholder_ok)
    log_warn("enemy affixes: mod %u has a maker of its own - a unique whose affixes are all taken out reaches the "
             "client as a plain monster",
             kPlaceholderMod);
  if (g_why_affixes)
    logf("enemy affixes: %s - monsters keep their affixes", g_why_affixes);
  else
    logf("enemy affixes: bound (a mod added at 0x%llX, the rollers' test 0x%llX, a monster's mods' makers run at 0x%llX; "
         "its 9 mods at its data +0x%X)",
         static_cast<unsigned long long>(sites::result(sites::kMonsterModAdd).rva),
         static_cast<unsigned long long>(sites::result(sites::kMonsterModTest).rva),
         static_cast<unsigned long long>(sites::result(sites::kMonsterModsRun).rva), sites::monster_mods_at());
  return g_why_affixes;
}

const char* bind_display() {
  g_why_display = !sites::found(sites::kClientModLoop) ? "the client's mod loop not found"
                  : !game::has_base_stat()             ? "the game's base stat reader not found"
                                                       : nullptr;
  if (g_why_display)
    logf("enemy immunities: %s - the hover keeps listing immunities (hits still go through them)", g_why_display);
  else
    logf("enemy immunities: the hover lists none for enemies (the client's copy lowered after its mod loop 0x%llX)",
         static_cast<unsigned long long>(sites::result(sites::kClientModLoop).rva));
  return g_why_display;
}

int32_t adjust(AdjustFn original, void* context, void* damage_type, int32_t resistance) {
  const int32_t answer = original ? original(context, damage_type, resistance) : resistance;
  if (!on(cheats::kNoImmunities) || g_why_immunities || !context || !damage_type) return answer;
  const uintptr_t ctx = reinterpret_cast<uintptr_t>(context);
  int32_t monster = 0, stat = -1;
  if (!mem::read_safe(ctx + kContextMonster, &monster) || !monster ||
      !mem::read_safe(reinterpret_cast<uintptr_t>(damage_type) + kDamageTypeStat, &stat) || stat < 0)
    return answer;
  Unit* const defender = reinterpret_cast<Unit*>(mem::read_ptr(ctx + kContextDefender));
  if (!enemy(defender)) return answer;
  // The base only when the total does not say immune already (a curse may have brought an immunity below 100).
  const int32_t base = resistance >= kImmune ? resistance : game::get_base_stat(defender, stat);
  const int32_t taken = enemies::resistance(true, true, resistance, base, answer);
  if (taken != answer) {
    InterlockedIncrement(&g_immunities);
    if (log_first(&g_immunity_seen, 4))
      logf("enemy immunities: monster class %u's resistance %d (stat %d, base %d) counted as %d, not %d",
           game::unit_class(defender), resistance, stat, base, taken, answer);
    cheats::note("No immunities: a hit went through an immunity");
  }
  return taken;
}

void mod_add(ModAddFn original, void* game_ptr, Unit* monster, int32_t mod, int32_t unique) {
  if (on(cheats::kNoAffixes) && !g_why_affixes && enemy(monster) && skips(true, true, is_affix(monster, mod))) {
    InterlockedIncrement(&g_affixes);
    if (log_first(&g_affix_seen, 4))
      logf("enemy affixes: monster class %u did not get its affix (mod %d)", game::unit_class(monster), mod);
    cheats::note("No affixes: a monster's affix was left out");
    return;
  }
  if (original) original(game_ptr, monster, mod, unique);
}

int32_t mod_test(ModTestFn original, Unit* monster, const uint8_t* row, int32_t expansion) {
  const int32_t answer = original ? original(monster, row, expansion) : 0;
  if (!answer || !on(cheats::kNoAffixes) || g_why_affixes || !row || !enemy(monster)) return answer;
  uint8_t copy[kMonUModRowSize];
  if (!mem::copy_from(copy, reinterpret_cast<uintptr_t>(row), sizeof(copy)) || !row_is_affix(copy)) return answer;
  uint32_t mod = 0;
  std::memcpy(&mod, copy, sizeof(mod));
  if (note_refused(monster)) {
    InterlockedIncrement(&g_refused);
    if (log_first(&g_refused_seen, 4))
      logf("enemy affixes: monster class %u may take no affix (a roller asked about affix %u first)",
           game::unit_class(monster), mod);
  }
  return 0;
}

void mods_run(ModsRunFn original, Unit* monster) {
  const bool refused = take_refused(monster);
  if (on(cheats::kNoAffixes) && !g_why_affixes && enemy(monster)) {
    const uintptr_t data = mem::read_ptr(reinterpret_cast<uintptr_t>(monster) + kUnitData);
    const uintptr_t at = data ? data + static_cast<uintptr_t>(sites::monster_mods_at()) : 0;
    uint8_t mods[9] = {}, before[9] = {};
    const uint64_t bits = affixes_of(monster);
    if (at && bits && mem::copy_from(mods, at, sizeof(mods))) {
      std::memcpy(before, mods, sizeof(before));
      const int taken = strip_affixes(mods, bits);
      // Only a unique, a super unique or a Herald (kind 8, 2, 0x200) is told to the client by its mods.
      const bool named = (game::monster_type_flags(monster) & kNamedKinds) != 0;
      const bool placeholder = g_placeholder_ok && named && keep_placeholder(mods, taken, refused);
      if ((taken > 0 || placeholder) && mem::copy_to(at, mods, sizeof(mods))) {
        InterlockedExchangeAdd(&g_affixes, taken);
        if (placeholder) InterlockedIncrement(&g_placeholders);
        if (taken > 0 && log_first(&g_stripped_seen, 4))
          logf("enemy affixes: monster class %u (kind 0x%X) had %d affixes written in (mods %u %u %u %u %u %u %u %u "
               "%u) - taken out before their makers run%s",
               game::unit_class(monster), game::monster_type_flags(monster), taken, before[0], before[1], before[2],
               before[3], before[4], before[5], before[6], before[7], before[8],
               placeholder ? "; mod 13, which does nothing, kept in their place so the client still sees what it is"
                           : "");
        else if (taken == 0 && placeholder && log_first(&g_placeholder_seen, 4))
          logf("enemy affixes: monster class %u (kind 0x%X) was rolled no affix - mod 13, which does nothing, kept in "
               "its list so the client still sees what it is",
               game::unit_class(monster), game::monster_type_flags(monster));
        cheats::note("No affixes: a monster's affixes were taken out");
      }
    }
  }
  if (original) original(monster);
}

void client_mods_run(ClientModsFn original, Unit* monster) {
  if (original) original(monster);
  if (!on(cheats::kNoImmunities) || g_why_display || g_why_immunities || !monster ||
      game::unit_type(monster) != kMonster || game::is_mercenary(monster) || client_pet_of_mine(monster))
    return;
  int lowered = 0;
  int32_t was[6] = {};
  for (int i = 0; i < 6; ++i) {
    was[i] = game::get_base_stat(monster, kResistStats[i]);
    const int32_t now = displayed(true, true, was[i]);
    if (now != was[i]) {
      game::set_stat(monster, kResistStats[i], now);
      ++lowered;
    }
  }
  if (!lowered) return;
  InterlockedIncrement(&g_shown);
  if (log_first(&g_shown_seen, 4))
    logf("enemy immunities: monster class %u's immunities taken off the client's copy (damage %d, magic %d, fire %d, "
         "lightning %d, cold %d, poison %d; each of 100 or more now 0) - the hover lists none",
         game::unit_class(monster), was[0], was[1], was[2], was[3], was[4], was[5]);
}

Stats stats() {
  Stats st;
  st.immunities = static_cast<unsigned>(g_immunities);
  st.affixes = static_cast<unsigned>(g_affixes);
  st.refused = static_cast<unsigned>(g_refused);
  st.placeholders = static_cast<unsigned>(g_placeholders);
  st.shown = static_cast<unsigned>(g_shown);
  return st;
}

}  // namespace d2rcc::enemies
