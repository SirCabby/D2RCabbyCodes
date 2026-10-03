#include "requirements.h"

#include <windows.h>

#include <cstdio>

#include "cheats.h"
#include "game.h"
#include "log.h"
#include "mem.h"
#include "sites.h"

namespace d2rcc::requirements {
namespace {

constexpr int kBanks = 4;                // the data tables' array: banks 0 .. 3 (a unit's +0x1BD, 1 classic .. 3 rotw)
constexpr uintptr_t kBankStride = 16;    // ... 16 bytes a bank, its pointer first
constexpr uint64_t kMaxRows = 1u << 16;  // more rows than that is no table (3.3: Items 1,000 or so, Hireling 120)
constexpr int kMaxPets = 256;            // nodes of the client's pet list walked at most
constexpr uint32_t kNone = 0xFFFFFFFFu;

const char* g_why_player = "the game's routines are not read yet";
const char* g_why_mercenary = "the game's routines are not read yet";
volatile LONG g_player_yes = 0;
volatile LONG g_mercenary_yes = 0;
volatile LONG g_refreshes = 0;
volatile LONG g_player_seen = 0;     // the first answers turned, logged
volatile LONG g_mercenary_seen = 0;
volatile LONG g_new_game = 1;        // set by game_left (UI thread), taken by the tick
// The tick's own (server thread): the game it last saw, and the switches as that game's items were worked out with.
void* g_game = nullptr;
bool g_player_on = false;
bool g_mercenary_on = false;

bool on(cheats::Kind k) { return cheats::enabled(k) && !cheats::why_not(k); }

// The row `index` of a table of a bank's data tables ({rows, u64 count} at `at`), 0 when there is none. Read where the
// game's own getters read it.
uintptr_t row_of(uint8_t bank, int at, int row_size, int64_t index) {
  const sites::RequirementFacts& f = sites::requirement_facts();
  if (bank >= kBanks || index < 0 || row_size <= 0) return 0;
  const uintptr_t tables = mem::read_ptr(f.tables + bank * kBankStride);
  const uintptr_t rows = tables ? mem::read_ptr(tables + static_cast<uintptr_t>(at)) : 0;
  uint64_t count = 0;
  if (!rows || !mem::read_safe(tables + static_cast<uintptr_t>(at) + 8, &count) || count > kMaxRows ||
      static_cast<uint64_t>(index) >= count)
    return 0;
  return rows + static_cast<uintptr_t>(index) * static_cast<uintptr_t>(row_size);
}

// A monster's Hireling row, as the game's lookup finds it: the first of the unit's bank whose monster class is the
// unit's and whose version is the bank's (100 an expansion bank's, 0 the classic one's). 0 when none.
uintptr_t hireling_row(Unit* unit) {
  const sites::RequirementFacts& f = sites::requirement_facts();
  const uint8_t bank = game::unit_table_bank(unit);
  if (bank >= kBanks || f.hireling_row <= 0) return 0;
  const uintptr_t tables = mem::read_ptr(f.tables + bank * kBankStride);
  const uintptr_t rows = tables ? mem::read_ptr(tables + static_cast<uintptr_t>(f.hirelings_at)) : 0;
  uint64_t count = 0;
  if (!rows || !mem::read_safe(tables + static_cast<uintptr_t>(f.hirelings_at) + 8, &count) || count > kMaxRows)
    return 0;
  const uint32_t monster = game::unit_class(unit);
  const uint16_t version = bank != 1 ? static_cast<uint16_t>(f.hireling_version) : 0;
  for (uint64_t i = 0; i < count; ++i) {
    const uintptr_t row = rows + static_cast<uintptr_t>(i) * static_cast<uintptr_t>(f.hireling_row);
    uint32_t cls = 0;
    uint16_t v = 0;
    if (!mem::read_safe(row + static_cast<uintptr_t>(f.hireling_monster_at), &cls) || !mem::read_safe(row, &v)) return 0;
    if (cls == monster && v == version) return row;
  }
  return 0;
}

// The local player's mercenary as the client knows it: the id of its pet of the mercenary's type in the client's pet
// list (a gone one skipped, as the client's own lookup skips it), kNone when there is none.
uint32_t client_mercenary_id() {
  const sites::RequirementFacts& f = sites::requirement_facts();
  const uint32_t me = game::local_player_id();
  if (!f.client_pets || me == kNone) return kNone;
  uintptr_t node = mem::read_ptr(f.client_pets);
  for (int n = 0; node && n < kMaxPets; ++n) {
    int32_t type = 0, gone = 0;
    uint32_t owner = 0, id = 0;
    if (!mem::read_safe(node + static_cast<uintptr_t>(f.pet_type_at), &type) ||
        !mem::read_safe(node + static_cast<uintptr_t>(f.pet_owner_at), &owner) ||
        !mem::read_safe(node + static_cast<uintptr_t>(f.pet_gone_at), &gone) ||
        !mem::read_safe(node + static_cast<uintptr_t>(f.pet_id_at), &id))
      return kNone;
    if (type == f.mercenary_pet && owner == me && gone == 0) return id;
    node = mem::read_ptr(node + static_cast<uintptr_t>(f.pet_next_at));
  }
  return kNone;
}

// The unit flag the class test asks a hireling for (the unit's flags at +0x124, as read from the test's code).
bool mercenary_flag(Unit* unit) {
  uint32_t flags = 0;
  return unit && mem::read_safe(reinterpret_cast<uintptr_t>(unit) + 0x124, &flags) &&
         (flags & sites::requirement_facts().mercenary_flag) != 0;
}

// Whose unit the test is asked about, of the two the switches are for (the server's units and the client's copies).
enum class Whose : int { kNobody, kPlayer, kMercenary };
Whose whose(Unit* unit, bool player_on, bool mercenary_on) {
  if (player_on && game::is_local_player(unit)) return Whose::kPlayer;
  if (mercenary_on && game::unit_type(unit) == game::kMonster && mercenary_flag(unit) &&
      (game::owned_by_local_player(unit) || client_mercenary_id() == game::unit_id(unit)))
    return Whose::kMercenary;
  return Whose::kNobody;
}

// The tests after level, strength and dexterity, in the routine's order (a later one is not asked once one has failed:
// the routine stops there too). The item's Items row, of its bank, is item_row.
Later later_tests(Unit* item, uint8_t bank, uintptr_t item_row, Unit* unit, int32_t body_location) {
  const sites::RequirementFacts& f = sites::requirement_facts();
  Later l;
  l.identified = (game::item_flags(item) & f.identified) != 0;
  if (!l.identified) return l;
  int16_t type = -1;
  mem::read_safe(item_row + static_cast<uintptr_t>(f.item_type_at), &type);
  l.empty_tome = type == f.tome_type && game::get_stat(item, f.quantity_stat) < 1;
  if (l.empty_tome) return l;
  const uintptr_t type_row = row_of(bank, f.types_at, f.type_row, type);
  uint8_t first = 0, second = 0, type_class = static_cast<uint8_t>(f.class_none);
  if (type_row) {
    mem::read_safe(type_row + static_cast<uintptr_t>(f.body1_at), &first);
    mem::read_safe(type_row + static_cast<uintptr_t>(f.body2_at), &second);
    mem::read_safe(type_row + static_cast<uintptr_t>(f.class_at), &type_class);
  }
  if (body_location) {
    l.restricted = restricted(true, game::restricted_socket(unit, item, body_location), false, false);
  } else {
    const bool at_first = game::restricted_socket(unit, item, first);
    l.restricted = restricted(false, false, at_first, at_first && game::restricted_socket(unit, item, second));
  }
  if (l.restricted) return l;
  const uint32_t unit_type = game::unit_type(unit);
  const Who who = unit_type == game::kPlayer ? Who::kPlayer : unit_type == game::kMonster ? Who::kMonster : Who::kOther;
  bool hireling = false, flag = false;
  int hireling_class = -1;
  if (who == Who::kMonster && type_class < f.class_none) {
    const uintptr_t row = hireling_row(unit);
    int32_t kind = 0;
    int8_t cls = -1;
    hireling = row && mem::read_safe(row + static_cast<uintptr_t>(f.hireling_kind_at), &kind) && kind != 0;
    if (hireling && mem::read_safe(row + static_cast<uintptr_t>(f.hireling_class_at), &cls)) hireling_class = cls;
    flag = hireling && mercenary_flag(unit);
  }
  l.class_ok = class_ok(type_class, f.class_none, who, static_cast<int>(game::unit_class(unit)), hireling, flag,
                        hireling_class);
  return l;
}

}  // namespace

void bind() {
  const sites::RequirementFacts& f = sites::requirement_facts();
  g_why_player = !sites::found(sites::kItemRequirements) ? "the game's requirement test not found"
                 : !f.known                             ? "the game's requirement test is not made the way expected"
                 : !game::has_restricted_test()         ? "the game's restricted-socket test not found"
                                                        : nullptr;
  g_why_mercenary = g_why_player             ? g_why_player
                    : !f.client_pets          ? "the mercenary's client copy is not found the way expected"
                    : !game::has_pet_of_type() ? "the game's lookup of a player's mercenary not found"
                                               : nullptr;
  if (g_why_player) {
    logf("ignore item requirements: %s - items keep their requirements", g_why_player);
    return;
  }
  logf("ignore item requirements: bound (the requirement test 0x%llX, its restricted-socket test 0x%llX; your gear "
       "worked out again at a switch: %s)",
       static_cast<unsigned long long>(sites::result(sites::kItemRequirements).rva),
       static_cast<unsigned long long>(f.restricted - sites::exe_base()),
       game::has_items_refresh() ? "yes" : "no, at the next change of it");
  if (g_why_mercenary)
    logf("ignore item requirements: %s - your mercenary's items keep their requirements", g_why_mercenary);
  else
    logf("ignore item requirements: your mercenary too (the server's lookup 0x%llX, the client's pet list 0x%llX, pet "
         "type %d)",
         static_cast<unsigned long long>(sites::result(sites::kPetOfType).rva),
         static_cast<unsigned long long>(f.client_pets - sites::exe_base()), f.mercenary_pet);
}

const char* why_not_player() { return g_why_player; }
const char* why_not_mercenary() { return g_why_mercenary; }

int32_t test(TestFn original, Unit* item, Unit* unit, int32_t equipping, int32_t* strength, int32_t* dexterity,
             int32_t* level, int32_t body_location) {
  if (!original) return 0;
  const bool player_on = !g_why_player && on(cheats::kIgnoreRequirements);
  const bool mercenary_on = !g_why_mercenary && on(cheats::kIgnoreRequirementsMerc);
  if (!player_on && !mercenary_on)
    return original(item, unit, equipping, strength, dexterity, level, body_location);
  int32_t s = 0, d = 0, l = 0;
  const int32_t said = original(item, unit, equipping, &s, &d, &l, body_location);
  // The unit's switch, and an item the test reads: an item with its Items row.
  const Whose who = whose(unit, player_on, mercenary_on);
  const uint8_t bank = who != Whose::kNobody && game::unit_type(item) == game::kItem ? game::unit_table_bank(item) : 0;
  const uintptr_t item_row =
      bank ? row_of(bank, sites::requirement_facts().items_at, sites::requirement_facts().item_row,
                    game::unit_class(item))
           : 0;
  const bool ignoring = item_row != 0;
  if (strength) *strength = ignoring ? 1 : s;
  if (dexterity) *dexterity = ignoring ? 1 : d;
  if (level) *level = ignoring ? 1 : l;
  if (!ignoring || said || (s && d && l)) return said;
  const Later later = later_tests(item, bank, item_row, unit, body_location);
  if (!answer(true, false, s != 0, d != 0, l != 0, later)) return said;
  const bool mercenary = who == Whose::kMercenary;
  InterlockedIncrement(mercenary ? &g_mercenary_yes : &g_player_yes);
  if (log_first(mercenary ? &g_mercenary_seen : &g_player_seen, 4)) {
    char unmet[40] = {};
    std::snprintf(unmet, sizeof(unmet), "%s%s%s%s%s", !l ? "level" : "", !l && (!s || !d) ? ", " : "",
                  !s ? "strength" : "", !s && !d ? ", " : "", !d ? "dexterity" : "");
    const int count = !l + !s + !d;
    logf("ignore item requirements: item class %u (item level %d) is usable by your %s, though its %s requirement%s "
         "not met",
         game::unit_class(item), game::item_level(item), mercenary ? "mercenary" : "character", unmet,
         count > 1 ? "s are" : " is");
  }
  return 1;
}

void on_tick(void* game, Unit* player) {
  const bool player_on = !g_why_player && on(cheats::kIgnoreRequirements);
  const bool mercenary_on = !g_why_mercenary && on(cheats::kIgnoreRequirementsMerc);
  if ((g_new_game && InterlockedExchange(&g_new_game, 0)) || game != g_game) {
    // A game's load has worked its items out with the switches as they were.
    g_game = game;
    g_player_on = player_on;
    g_mercenary_on = mercenary_on;
    return;
  }
  if (player_on != g_player_on && !game::unit_is_dead(player)) {
    g_player_on = player_on;
    if (game::has_items_refresh()) {
      game::refresh_unit_items(game, player);
      InterlockedIncrement(&g_refreshes);
      logf("ignore item requirements: %s for your character - what it wears and carries worked out again",
           player_on ? "on" : "off");
      cheats::note("Ignore item requirements: your gear %s", player_on ? "counts whatever its requirements"
                                                                       : "counts by its requirements again");
    }
  }
  if (mercenary_on != g_mercenary_on) {
    g_mercenary_on = mercenary_on;
    Unit* const mercenary = game::pet_of_type(game, player, sites::requirement_facts().mercenary_pet);
    if (mercenary && game::has_items_refresh()) {
      game::refresh_unit_items(game, mercenary);
      InterlockedIncrement(&g_refreshes);
      logf("ignore item requirements: %s for your mercenary (monster class %u) - what it wears worked out again",
           mercenary_on ? "on" : "off", game::unit_class(mercenary));
      cheats::note("Ignore item requirements: your mercenary's gear %s",
                   mercenary_on ? "counts whatever its requirements" : "counts by its requirements again");
    } else if (!mercenary) {
      logf("ignore item requirements: %s for your mercenary - none is with you now (its gear is worked out as it "
           "comes)",
           mercenary_on ? "on" : "off");
    }
  }
}

void game_left() { InterlockedExchange(&g_new_game, 1); }

Stats stats() {
  Stats st;
  st.player_yes = static_cast<unsigned>(g_player_yes);
  st.mercenary_yes = static_cast<unsigned>(g_mercenary_yes);
  st.refreshes = static_cast<unsigned>(g_refreshes);
  return st;
}

}  // namespace d2rcc::requirements
