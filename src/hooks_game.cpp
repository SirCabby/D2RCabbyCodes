#include "hooks_game.h"

#include <D2RLPlugin/api.h>

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "cheats.h"
#include "chronicle.h"
#include "context.h"
#include "game.h"
#include "healthbars.h"
#include "log.h"
#include "mem.h"
#include "perf.h"
#include "sites.h"
#include "terror.h"
#include "arealevel.h"
#include "autoid.h"
#include "character.h"
#include "consumables.h"
#include "cube.h"
#include "curses.h"
#include "itemlevel.h"
#include "itemseed.h"
#include "loot.h"
#include "hometown.h"
#include "npcservice.h"
#include "passive.h"
#include "watchdog.h"

namespace d2rcc::hooks {
namespace {

using Unit = game::Unit;
// ExecuteEvents takes the damage record fifth, on the stack; the fourth argument (r9d) says whether
// it adds up the hit's totals itself (1, most callers: missiles, skills) or they are already there
// (0: the melee record, which the total routine filled when the swing was queued).
using ExecuteEventsFn = void(__fastcall*)(void* game, Unit* attacker, Unit* defender, int32_t compute_totals,
                                          uint8_t* damage) noexcept;
using FinalizeDamageFn = void(__fastcall*)(void* game, Unit* attacker, Unit* defender, uint8_t* damage) noexcept;
using StatRegenFn = void(__fastcall*)(void* game, Unit* unit, int32_t a3, int32_t a4) noexcept;
using DeathHandlerFn = uint64_t(__fastcall*)(void* game, Unit* player, Unit* killer) noexcept;
// D2Common_SKILLMANA_GetManaCost: (data-table context byte, skill id, skill level) -> cost in 256ths.
using ManaCostFn = int32_t(__fastcall*)(uint32_t dataCtx, int32_t skillId, int32_t level) noexcept;
// PLAYER_AddExperience: (game, player, current level, amount). Caps at the level-99
// total, sets the stat, and levels the character up when the total crosses a level.
using AddExperienceFn = void(__fastcall*)(void* game, Unit* unit, int32_t level, int32_t amount) noexcept;
// UI: the client's level-name getter and two routines that ask it for names.
using LevelNameFn = const char*(__fastcall*)(uint32_t dataCtx, int32_t levelId) noexcept;
using PassthroughFn = uint64_t(__fastcall*)(void* a, void* b, void* c, void* d) noexcept;
// Loot: the item generator's quality step (item, request) -> quality, and the property roll (unit, a, b)
// -> a value in [min, max]. Item creation is itemseed.h's.
using QualityStepFn = int32_t(__fastcall*)(Unit* item, uint8_t* request) noexcept;
using PropertyRollFn = int32_t(__fastcall*)(Unit* unit, int32_t a, int32_t b) noexcept;
// ... a class item's skill lines (game, item, item level, the class's first skill, its skill count, the roll bonus;
// the bonus is the sixth argument, on the stack). The magic, rare and crafted affix steps are loot.h's.
using ClassSkillsFn = void(__fastcall*)(void* game, Unit* item, int32_t item_level, int32_t first_skill,
                                        int32_t skill_count, int32_t bonus) noexcept;
// Consumables: whether a used item is removed (the answer is in al), and a tome's quantity update
// (game, player, item, delta), which also keeps the book's skill in step.
using ShouldRemoveFn = bool(__fastcall*)(Unit* item) noexcept;
using SyncQuantityFn = void(__fastcall*)(void* game, Unit* player, Unit* item, int32_t delta) noexcept;
// Keys: the key use (game, player) -> 1 once it took a key from the inventory, 0 when there is none.
using UseKeyFn = int32_t(__fastcall*)(void* game, Unit* player) noexcept;
// Durability: a hit's wear on one item (game, owner, item), and Impale's wear on its weapon (game, player,
// weapon, chance in percent, amount; the amount is the fifth argument, on the stack).
using ItemWearFn = void(__fastcall*)(void* game, Unit* owner, Unit* item) noexcept;
using ImpaleWearFn = void(__fastcall*)(void* game, Unit* player, Unit* weapon, int32_t chance, int32_t amount) noexcept;
// The inventory's item name: fills name (0x400 bytes, the bound of its own strcat_s calls).
using ItemNameFn = void(__fastcall*)(Unit* item, char* name) noexcept;
constexpr size_t kItemNameSize = 0x400;
// Curses: a curse skill's step for one unit of its range (the unit, the step's context) -> 1 when it cursed it, and
// the Cursed monster modifier's step (game, the monster, the unit, the curse's level).
using CurseStepFn = uint64_t(__fastcall*)(Unit* target, uint8_t* context) noexcept;
using CursedModFn = void(__fastcall*)(void* game, Unit* monster, Unit* target, int32_t level) noexcept;
// Gold: a vendor's payment (game, player, amount) -> 1 when paid, 0 when the player has not that much.
using VendorPayFn = int32_t(__fastcall*)(void* game, Unit* player, int32_t amount) noexcept;
// Town portals: a cast (game, player, the scroll or tome; the answer is in al), the portal made at a pair's
// destination (game, player, the portal beside the player, the destination's level, the level it is made from;
// the last on the stack), and a portal's use (the operate function's context) -> 1 when it was used.
using PortalStartFn = uint64_t(__fastcall*)(void* game, Unit* player, void* item) noexcept;
using PortalTownEndFn = Unit*(__fastcall*)(void* game, Unit* player, Unit* portal, int32_t destination,
                                           int32_t source) noexcept;
using PortalOperateFn = uint64_t(__fastcall*)(void** context) noexcept;
// A death's wake: the server's handler of the request a dead player's client sends (game, player, the packet, its
// size) -> 0.
using WakeFn = uint64_t(__fastcall*)(void* game, Unit* player, void* packet, int32_t size) noexcept;
// The client's trip through a town portal ahead of the server (the client's player, the client's portal) -> al:
// whether it is made.
using PortalTripFn = uint64_t(__fastcall*)(Unit* player, Unit* portal) noexcept;
// The client's NPC menu as it opens (the NPC, whether its speech came first): the entries its quests add, then
// the menu.
using NpcMenuFn = uint64_t(__fastcall*)(Unit* npc, int32_t after_speech) noexcept;
// God mode: whether a stat of a kind of unit may go down (the stat, the unit type) -> nonzero when it may not.
using ProtectedStatFn = bool(__fastcall*)(int32_t stat, int32_t unit_type) noexcept;

ExecuteEventsFn g_orig_execute = nullptr;
FinalizeDamageFn g_orig_finalize = nullptr;
StatRegenFn g_orig_regen = nullptr;
DeathHandlerFn g_orig_death = nullptr;
ManaCostFn g_orig_mana_cost = nullptr;
AddExperienceFn g_orig_add_exp = nullptr;
LevelNameFn g_orig_level_name = nullptr;
PassthroughFn g_orig_automap_update = nullptr;
PassthroughFn g_orig_automap_info = nullptr;
PassthroughFn g_orig_wp_populate = nullptr;
loot::TcDropCoreFn g_orig_tc_drop = nullptr;
QualityStepFn g_orig_quality_step = nullptr;
PropertyRollFn g_orig_property_roll = nullptr;
itemseed::CreateItemFn g_orig_create_item = nullptr;
ClassSkillsFn g_orig_class_skills = nullptr;
loot::SuperiorFitsFn g_orig_superior_fits = nullptr;
loot::MagicAffixesFn g_orig_magic_affixes = nullptr;
loot::AffixesFn g_orig_rare_affixes = nullptr;
loot::AffixesFn g_orig_crafted_affixes = nullptr;
loot::PickAffixFn g_orig_pick_affix = nullptr;
loot::AutoAffixFn g_orig_auto_affix = nullptr;
ShouldRemoveFn g_orig_should_remove = nullptr;
SyncQuantityFn g_orig_sync_quantity = nullptr;
UseKeyFn g_orig_use_key = nullptr;
ItemWearFn g_orig_item_wear = nullptr;
ImpaleWearFn g_orig_impale_wear = nullptr;
ItemNameFn g_orig_item_name = nullptr;
CurseStepFn g_orig_curse_step = nullptr;
CursedModFn g_orig_cursed_mod = nullptr;
VendorPayFn g_orig_vendor_pay = nullptr;
PortalStartFn g_orig_portal_start = nullptr;
PortalTownEndFn g_orig_portal_town_end = nullptr;
PortalOperateFn g_orig_portal_operate = nullptr;
WakeFn g_orig_wake = nullptr;
PortalTripFn g_orig_portal_trip = nullptr;
NpcMenuFn g_orig_npc_menu = nullptr;
ProtectedStatFn g_orig_protected_stat = nullptr;
// Infinite cube ingredients: the cube's product routine, the item free and the socket contents' free (cube.h).
cube::ProductsFn g_orig_cube_products = nullptr;
cube::FreeFn g_orig_item_free = nullptr;
cube::FreeFn g_orig_socketed_free = nullptr;
// Identify on pickup: the item notice (autoid.h).
autoid::NoticeFn g_orig_item_notice = nullptr;
// Passive mercenary and minions: the AI dispatcher, the enemy test and the kept target's getter (passive.h).
passive::DispatchFn g_orig_ai_dispatch = nullptr;
passive::EnemyTestFn g_orig_enemy_test = nullptr;
passive::KeptTargetFn g_orig_kept_target = nullptr;
volatile LONG g_pay_seen = 0;          // the first payments not taken are logged
volatile LONG g_hit_wear_seen = 0;     // the first skipped wear of each kind is logged
volatile LONG g_impale_wear_seen = 0;
uintptr_t g_book_debit_return = 0;  // where the item-use effect's tome charge call returns to
thread_local int t_in_automap = 0;    // inside the automap panel's update
thread_local int t_in_waypoints = 0;  // inside the waypoint panel's fill
bool g_levels_collected = false;
bool g_monsters_collected = false;  // the health bars' copy of the MonStats flags, once per game
const char* g_items_why = nullptr;  // why the infinite items cannot work at all (install: hooks, table service)
#ifdef D2RCC_DEV
volatile LONG g_mana_cost_seen = 0;   // calls that met the local player and were logged (diagnostics)
#endif

// The D2Damage record (0x180 bytes). The game's own debug damage command fills the same fields,
// one per damage type; the total routine applies resistances to them in place and adds the direct
// ones up into the life total. Amounts are 256ths of a point (per frame for poison and burning),
// lengths are frames.
constexpr size_t kDmgFlags = 0x04;       // uint16: bit 1 killed, bit 10 always kills
constexpr size_t kDmgPhysical = 0x18;
constexpr size_t kDmgFire = 0x20;
constexpr size_t kDmgBurn = 0x24;        // burning: damage per frame ...
constexpr size_t kDmgBurnLen = 0x28;     // ... and its length
constexpr size_t kDmgLightning = 0x2C;
constexpr size_t kDmgMagic = 0x30;
constexpr size_t kDmgCold = 0x34;
constexpr size_t kDmgPoison = 0x38;      // poison per frame (one frame's worth is also in the total)
constexpr size_t kDmgPoisonLen = 0x3C;
constexpr size_t kDmgPoisonList = 0x40;  // stacked poison sources, 12 bytes each: {source, per frame, length}
constexpr size_t kDmgPoisonCount = 0x48; // uint64
constexpr size_t kDmgColdLen = 0x118;    // chill
constexpr size_t kDmgFreezeLen = 0x11C;
constexpr size_t kDmgLeech = 0x120;      // life leech; a monster attacker's is added to the life total
constexpr size_t kDmgMana = 0x124;       // int32, 256ths
constexpr size_t kDmgStamina = 0x128;    // int32, 256ths
constexpr size_t kDmgStunLen = 0x12C;
constexpr size_t kDmgLife = 0x134;       // int32, 256ths
constexpr size_t kPoisonEntry = 12;
constexpr uint64_t kMaxPoisonEntries = 256;
// The game adds the damage types up in 32 bits after resistances (up to double at -100 %), so a
// scaled type stays below 1/16 of the range: still far more than any monster's life (524k points).
constexpr int32_t kMaxScaledPart = 0x7FFFFFFF / 16;
constexpr int32_t kMaxScaledTotal = 0x7FFFFFFF;
constexpr uint16_t kKilled = 0x0002;
constexpr uint16_t kAlwaysKills = 0x0400;
constexpr int32_t kOneLife = 0x100;
constexpr size_t kGameDifficulty = 0x104;  // Game: 0 normal, 1 nightmare, 2 hell (SUNITDMG reads it here)

bool g_life_guarded = false;       // god mode: the protected-stat test is hooked (else hits on you are emptied)
volatile LONG g_life_kept = 0;      // god mode: a player's life losses the stat writers were refused
volatile LONG g_life_kept_seen = 0;  // the first are logged
volatile LONG g_life_topped_seen = 0;  // ... and the first times a life above its top went down to it
thread_local int t_life_to_top = 0;  // inside god mode's own lowering of a life to its top
int g_speed_applied = 0;       // the velocitypercent bonus set on the player's base stat
bool g_exit_armed = false;     // exit before death fires on a fall to the trigger, never on a life already there
uint32_t g_speed_player = 0xFFFFFFFFu;

volatile LONG g_exit = 0;
char g_exit_reason[96] = {};
cheats::Status g_status;  // the server thread's working copy

void request_exit(const char* reason) {
  if (InterlockedCompareExchange(&g_exit, 1, 0) == 0) {
    std::snprintf(g_exit_reason, sizeof(g_exit_reason), "%s", reason);
    watchdog::wake();  // it leaves the game: at once, before the line below is written
    logf("exit before death: %s - leaving the game", reason);
    cheats::note("Exit before death: %s", reason);
  }
}

template <typename T>
T dmg_read(uint8_t* d, size_t off) {
  T v{};
  mem::read_safe(reinterpret_cast<uintptr_t>(d) + off, &v);
  return v;
}
template <typename T>
void dmg_write(uint8_t* d, size_t off, T v) {
  mem::store(reinterpret_cast<uintptr_t>(d) + off, v);
}

bool on(cheats::Kind k) { return cheats::enabled(k) && !cheats::why_not(k); }

void set_items_why(const char* why) {
  cheats::set_why_not(cheats::kInfiniteTownPortal, why);
  cheats::set_why_not(cheats::kInfiniteIdentify, why);
  cheats::set_why_not(cheats::kInfinitePotions, why);
}

int32_t scaled(int64_t v, double m, int32_t cap) {
  const double r = static_cast<double>(v) * m + 0.5;
  return r >= static_cast<double>(cap) ? cap : static_cast<int32_t>(r);
}

void scale_field(uint8_t* d, size_t off, double m, int32_t cap) {
  const int32_t v = dmg_read<int32_t>(d, off);
  if (v > 0) dmg_write<int32_t>(d, off, scaled(v, m, cap));
}

// The stacked poison sources (a list the record points to), one entry at a time. False when the
// list is there but cannot be walked.
template <typename Fn>
bool each_poison_source(uint8_t* d, Fn fn) {
  const uintptr_t list = mem::read_ptr(reinterpret_cast<uintptr_t>(d) + kDmgPoisonList);
  uint64_t count = 0;
  if (!list || !mem::read_safe(reinterpret_cast<uintptr_t>(d) + kDmgPoisonCount, &count) || !count) return true;
  if (count > kMaxPoisonEntries) return false;
  for (uint64_t i = 0; i < count; ++i) fn(list + i * kPoisonEntry);
  return true;
}

// Poison m times as strong. The game poisons with each stacked source, then with what the
// record's own amount has beyond their sum: that remainder is scaled on its own so it stays whole.
void scale_poison(uint8_t* d, double m) {
  int64_t sources = 0, scaled_sources = 0;
  const bool walked = each_poison_source(d, [&](uintptr_t e) {
    int32_t v = 0;
    if (!mem::read_safe(e + 4, &v) || v <= 0) return;
    const int32_t s = scaled(v, m, kMaxScaledPart);
    sources += v;
    scaled_sources += s;
    mem::store(e + 4, s);
  });
  const int32_t per_frame = dmg_read<int32_t>(d, kDmgPoison);
  if (per_frame <= 0 || !walked) return;
  const int64_t own = per_frame > sources ? per_frame - sources : 0;
  const int64_t total = scaled(own, m, kMaxScaledPart) + scaled_sources;  // a part of the life total too
  dmg_write<int32_t>(d, kDmgPoison, total > kMaxScaledPart ? kMaxScaledPart : static_cast<int32_t>(total));
}

// A hit by you or a minion of yours, m times as strong. When ExecuteEvents still has to add up
// the totals, the damage types are scaled (resistances then apply to the bigger amounts, as they
// would to any bigger hit); when the totals are in, the life total is. Poison and burning run on
// after the hit from their per-frame amounts, which are scaled either way.
void scale_hit(uint8_t* d, int32_t compute_totals, double m) {
  if (compute_totals) {
    for (const size_t off : {kDmgPhysical, kDmgFire, kDmgLightning, kDmgMagic, kDmgCold})
      scale_field(d, off, m, kMaxScaledPart);
  } else {
    scale_field(d, kDmgLife, m, kMaxScaledTotal);
  }
  scale_poison(d, m);
  scale_field(d, kDmgBurn, m, kMaxScaledPart);
}

// What a hit does to its target besides the damage, taken away: what runs on after it (poison, burning,
// chill, freeze, stun), the life leeched and the mana and stamina drained, and "always kills".
void strip_hit(uint8_t* d) {
  for (const size_t off : {kDmgBurn, kDmgBurnLen, kDmgPoison, kDmgPoisonLen, kDmgColdLen, kDmgFreezeLen, kDmgLeech,
                           kDmgMana, kDmgStamina, kDmgStunLen})
    dmg_write<int32_t>(d, off, 0);
  each_poison_source(d, [](uintptr_t e) {
    mem::store(e + 4, int32_t{0});
    mem::store(e + 8, int32_t{0});
  });
  dmg_write<uint16_t>(d, kDmgFlags, dmg_read<uint16_t>(d, kDmgFlags) & static_cast<uint16_t>(~kAlwaysKills));
}

// Everything a hit does to its target, taken away: the damage of every type and all the rest.
void zero_hit(uint8_t* d) {
  for (const size_t off : {kDmgPhysical, kDmgFire, kDmgLightning, kDmgMagic, kDmgCold, kDmgLife})
    dmg_write<int32_t>(d, off, 0);
  strip_hit(d);
}

// Cannot be frozen: the hit's chill and freeze lengths go (the game's own "cannot be frozen" stat
// does the same inside the total routine). Cold damage itself still counts.
bool block_freeze(uint8_t* d) {
  const bool any = dmg_read<int32_t>(d, kDmgColdLen) > 0 || dmg_read<int32_t>(d, kDmgFreezeLen) > 0;
  if (any) {
    dmg_write<int32_t>(d, kDmgColdLen, 0);
    dmg_write<int32_t>(d, kDmgFreezeLen, 0);
  }
  return any;
}

// Cannot be poisoned: no poison runs on after the hit. With the totals already in, the frame of
// poison the life total holds comes out of it too.
bool block_poison(uint8_t* d, int32_t compute_totals) {
  const int32_t per_frame = dmg_read<int32_t>(d, kDmgPoison);
  bool any = per_frame > 0 || dmg_read<int32_t>(d, kDmgPoisonLen) > 0;
  if (!compute_totals && per_frame > 0) {
    const int32_t life = dmg_read<int32_t>(d, kDmgLife);
    dmg_write<int32_t>(d, kDmgLife, life > per_frame ? life - per_frame : 0);
  }
  dmg_write<int32_t>(d, kDmgPoison, 0);
  dmg_write<int32_t>(d, kDmgPoisonLen, 0);
  each_poison_source(d, [&any](uintptr_t e) {
    int32_t v = 0;
    if (mem::read_safe(e + 4, &v) && v > 0) any = true;
    mem::store(e + 4, int32_t{0});
    mem::store(e + 8, int32_t{0});
  });
  return any;
}

// A monster of the local player's that its switch keeps from harm: the mercenary, or a summon.
bool protected_pet(Unit* u) {
  const bool merc_on = on(cheats::kInvincibleMerc);
  const bool pets_on = on(cheats::kInvinciblePets);
  if ((!merc_on && !pets_on) || !game::owned_by_local_player(u)) return false;
  return game::is_mercenary(u) ? merc_on : pets_on;
}

bool mine(Unit* u) { return u && (game::is_local_player(u) || game::owned_by_local_player(u)); }

// --- the tick ------------------------------------------------------------------------------
void tick(void* game, Unit* player) {
  cheats::Status& st = g_status;
  st.player_found = true;
  st.player_id = game::unit_id(player);
  st.hp = game::get_stat(player, game::kHitpoints) >> 8;
  st.max_hp = game::get_stat(player, game::kMaxHp) >> 8;
  st.mana = game::get_stat(player, game::kMana) >> 8;
  st.max_mana = game::get_stat(player, game::kMaxMana) >> 8;
  st.level = game::get_stat(player, game::kLevel);
  st.experience = game::get_stat(player, game::kExperience);
  st.velocity = game::get_stat(player, game::kVelocityPercent);
  ++st.ticks;
  if (!g_levels_collected && (cheats::enabled(cheats::kAreaLevelAutomap) || cheats::enabled(cheats::kAreaLevelWaypoints))) {
    g_levels_collected = true;  // one attempt per game; the log says how it went
    arealevel::collect();
  }
  if (!g_monsters_collected && (cheats::enabled(cheats::kMonsterBars) || cheats::enabled(cheats::kBossBar))) {
    g_monsters_collected = true;  // the loader's tables are read on this thread only
    healthbars::collect_tables();
  }
  if (const uint8_t bank = game::unit_table_bank(player); !consumables::ready(bank) && (st.ticks % 125) == 1) {
    // Which items are scrolls, tomes and potions (the hooks ask on any thread): read on this
    // thread, tried again every few seconds until the loader has this game's table.
    consumables::collect();
    if (!g_items_why) set_items_why(consumables::ready(bank) ? nullptr : "the game's item table is not read yet");
  }
  if (const uint8_t bank = game::unit_table_bank(player);
      !cheats::why_not(cheats::kItemLevel) && !itemlevel::ready(bank) && (st.ticks % 125) == 1)
    itemlevel::collect();  // which items show their level (the name builder asks on the UI thread)
  if (const uint8_t bank = game::unit_table_bank(player);
      !cheats::why_not(cheats::kCannotBeCursed) && !curses::ready(bank) && (st.ticks % 125) == 1)
    curses::collect();  // which states are curses (the curse hooks ask)
  hometown::on_tick(game, player, st.ticks);  // the acts' towns, and the ones the character can reach
  npcservice::on_tick();  // an NPC's service switched on: no quest asks for it in the game's NPC table
  terror::on_tick(game, st.ticks);  // all areas terrorized: every act's zone applied as a shard applies it
  // The Chronicle, looked at every 5 s for the panel's counts while its loot switch is on (a drop reads it anyway).
  if (on(cheats::kChronicleOnly) && (st.ticks % 125) == 1) chronicle::read();
  if (character::on_tick(game, player) && g_exit_armed) {
    // A reset takes the life that vitality gave: exit before death arms again once life is above
    // its trigger, instead of taking the drop for a fall.
    g_exit_armed = false;
    logf("exit before death: disarmed by the character reset");
  }
  st.char_class = static_cast<int>(game::unit_class(player));
  st.stat_points = game::get_stat(player, game::kStatPoints);
  st.skill_points = game::get_stat(player, game::kSkillPoints);

  // God mode has nothing to do here: its hooks refuse what would lower your life when the game tries it.
  const bool god = cheats::enabled(cheats::kGodMode);
  // Cannot be poisoned keeps poison off every hit (the damage hook); a poison already on you - one
  // there when the switch went on - ends here, the way an antidote potion ends it.
  if (on(cheats::kCannotBePoisoned) && !game::unit_is_dead(player)) {
    const game::Cured cured = game::cure_state(game, player, game::kStatePoison);
    if (cured.lists || cured.cleared) {
      ++st.poisons_ended;
      logf("cannot be poisoned: the poison on you ended (%d stat list%s taken off%s)", cured.lists,
           cured.lists == 1 ? "" : "s", cured.cleared ? ", the state turned off" : "");
      cheats::note("Cannot be poisoned: the poison on you was removed");
    }
  }
  // Cannot be cursed keeps a monster's curse off (the curse hooks); a curse on you all the same - one there when
  // the switch went on - ends here, the same way.
  if (on(cheats::kCannotBeCursed) && !game::unit_is_dead(player)) curses::end_curses(game, player);
  if (cheats::enabled(cheats::kInfiniteMana)) {
    const int32_t mana = game::get_stat(player, game::kMana);
    const int32_t max = game::get_stat(player, game::kMaxMana);
    if (mana < max) game::set_stat(player, game::kMana, max);
  }
  // Stamina drains as the character runs, on the server and in the client's
  // own prediction (which decides when it stops running), so both are refilled.
  if (cheats::enabled(cheats::kInfiniteStamina)) {
    const int32_t max = game::get_stat(player, game::kMaxStamina);
    if (game::get_stat(player, game::kStamina) < max) game::set_stat(player, game::kStamina, max);
    if (Unit* client = game::local_client_player(); client && game::get_stat(client, game::kStamina) < max)
      game::set_stat(client, game::kStamina, max);
  }
  // Movement: the bonus lives in the player's base velocitypercent, so the
  // game's own additions (skills, states) stack on top and come and go as
  // usual. The client keeps a mirror of the unit; its base is kept equal so
  // the movement it predicts matches what the game decides.
  const int bonus = cheats::move_speed_bonus();
  if (st.player_id != g_speed_player) {
    g_speed_player = st.player_id;
    g_speed_applied = 0;  // a new character starts without our bonus
  }
  if (bonus != g_speed_applied && game::has_base_stat()) {
    const int32_t base = game::get_base_stat(player, game::kVelocityPercent);
    game::set_stat(player, game::kVelocityPercent, base - g_speed_applied + bonus);
    logf("movement speed: base velocitypercent %d -> %d (bonus %d%%)", base, base - g_speed_applied + bonus, bonus);
    g_speed_applied = bonus;
  }
  if (Unit* client = game::local_client_player(); client && game::has_base_stat()) {
    const int32_t sbase = game::get_base_stat(player, game::kVelocityPercent);
    const int32_t cbase = game::get_base_stat(client, game::kVelocityPercent);
    if (cbase != sbase) game::set_stat(client, game::kVelocityPercent, sbase);
  }
  // Exit before death watches for a fall to the trigger (1 life, or the
  // threshold). A life that is already there when the switch goes on - a
  // character loaded at 1 life, say - does not count until it has been above
  // the trigger once; a hit that would kill is handled by the damage hooks.
  if (cheats::enabled(cheats::kExitBeforeDeath) && !god) {
    const int32_t hp = game::get_stat(player, game::kHitpoints);
    const int32_t max = game::get_stat(player, game::kMaxHp);
    const int pct = cheats::exit_below_percent();
    int32_t trigger = kOneLife;
    if (pct > 0 && max > 0 && max * pct / 100 > trigger) trigger = max * pct / 100;
    if (hp > trigger) {
      if (!g_exit_armed) {
        g_exit_armed = true;
        logf("exit before death: armed (life %d, trigger %d)", hp >> 8, trigger >> 8);
      }
    } else if (g_exit_armed) {
      g_exit_armed = false;
      request_exit(hp <= kOneLife ? "life is down to 1" : "life fell below the threshold");
    } else if ((st.ticks % 250) == 1) {
      cheats::note("Exit before death arms once life is above %d", trigger >> 8);
    }
  } else {
    g_exit_armed = false;
  }
  st.exit_armed = g_exit_armed;
  cheats::publish(st);
}

// --- the hooks -----------------------------------------------------------------------------
void __fastcall hk_regen(void* game, Unit* unit, int32_t a3, int32_t a4) noexcept {
  if (unit && game::is_local_player(unit)) {
    perf::Timer timer(perf::kTick);
    game::note_local_server_player(unit);
    uint8_t difficulty = 0;
    if (game && mem::read_safe(reinterpret_cast<uintptr_t>(game) + kGameDifficulty, &difficulty) && difficulty <= 2)
      game::set_difficulty(difficulty);
    tick(game, unit);
  }
  if (g_orig_regen) g_orig_regen(game, unit, a3, a4);
}

void __fastcall hk_execute_events(void* game, Unit* attacker, Unit* defender, int32_t compute_totals,
                                  uint8_t* dmg) noexcept {
  if (dmg && defender && game::is_local_player(defender)) {
    const bool god = cheats::enabled(cheats::kGodMode);
    const bool exit_on = cheats::enabled(cheats::kExitBeforeDeath);
    if (cheats::enabled(cheats::kInfiniteStamina)) dmg_write<int32_t>(dmg, kDmgStamina, 0);
    if (on(cheats::kCannotBeFrozen) && block_freeze(dmg)) ++g_status.effects_blocked;
    if (on(cheats::kCannotBePoisoned) && block_poison(dmg, compute_totals)) ++g_status.effects_blocked;
    if (god) {
      // The hit does nothing else to you, and what it would take from your life the protected-stat hook refuses; its
      // damage stays for the hit's own events (thorns, damage taken to mana). Without that hook the hit is emptied.
      if (g_life_guarded) strip_hit(dmg);
      else zero_hit(dmg);
      ++g_status.hits_absorbed;
    } else if (exit_on) {
      // The life total is only in the record when the caller added it up (a melee swing); a hit
      // whose totals come later that turns out lethal is undone by FinalizeDamage instead.
      const int32_t hp = game::get_stat(defender, game::kHitpoints);
      const int32_t life_dmg = dmg_read<int32_t>(dmg, kDmgLife);
      const uint16_t flags = dmg_read<uint16_t>(dmg, kDmgFlags);
      const int32_t after = hp - (life_dmg > 0 ? life_dmg : 0);
      if ((life_dmg > 0 && after < kOneLife) || (flags & kAlwaysKills)) {
        // Stop the hit at 1 life, and take away any "always kills" the hit carries.
        dmg_write<int32_t>(dmg, kDmgLife, hp > kOneLife ? hp - kOneLife : 0);
        dmg_write<uint16_t>(dmg, kDmgFlags, flags & static_cast<uint16_t>(~kAlwaysKills));
        ++g_status.lethal_hits;
        request_exit("a lethal hit was stopped at 1 life");
      } else {
        const int pct = cheats::exit_below_percent();
        const int32_t max = game::get_stat(defender, game::kMaxHp);
        if (pct > 0 && max > 0 && after < max * pct / 100) request_exit("a hit took life below the threshold");
      }
    }
  } else if (dmg && defender && game::unit_type(defender) == game::kMonster) {
    if (protected_pet(defender)) {
      zero_hit(dmg);
      ++g_status.pet_hits;
    } else if (cheats::enabled(cheats::kDamageMultiplier) && !cheats::why_not(cheats::kDamageMultiplier) &&
               mine(attacker) && !game::owned_by_local_player(defender)) {
      scale_hit(dmg, compute_totals, static_cast<double>(cheats::damage_multiplier()));
      ++g_status.hits_scaled;
    }
  }
  if (g_orig_execute) g_orig_execute(game, attacker, defender, compute_totals, dmg);
}

void __fastcall hk_finalize_damage(void* game, Unit* attacker, Unit* defender, uint8_t* dmg) noexcept {
  if (dmg && defender && game::is_local_player(defender) &&
      (cheats::enabled(cheats::kExitBeforeDeath) || cheats::enabled(cheats::kGodMode))) {
    const uint16_t flags = dmg_read<uint16_t>(dmg, kDmgFlags);
    if (flags & kKilled) {
      dmg_write<uint16_t>(dmg, kDmgFlags, flags & static_cast<uint16_t>(~kKilled));
      if (game::get_stat(defender, game::kHitpoints) < kOneLife) game::set_stat(defender, game::kHitpoints, kOneLife);
      log_warn("a kill reached FinalizeDamage despite the guards - undone");
      if (cheats::enabled(cheats::kExitBeforeDeath)) request_exit("a kill was undone at the last step");
    }
  } else if (dmg && defender && game::unit_type(defender) == game::kMonster && protected_pet(defender)) {
    // A protected pet's hits are emptied in ExecuteEvents; a kill that still gets here is undone.
    const uint16_t flags = dmg_read<uint16_t>(dmg, kDmgFlags);
    if (flags & kKilled) {
      dmg_write<uint16_t>(dmg, kDmgFlags, flags & static_cast<uint16_t>(~kKilled));
      if (game::get_stat(defender, game::kHitpoints) < kOneLife) game::set_stat(defender, game::kHitpoints, kOneLife);
      log_warn("a kill of your %s reached FinalizeDamage - undone",
               game::is_mercenary(defender) ? "mercenary" : "minion");
    }
  }
  if (g_orig_finalize) g_orig_finalize(game, attacker, defender, dmg);
}

uint64_t __fastcall hk_death(void* game, Unit* player, Unit* killer) noexcept {
  if (player && game::is_local_player(player)) {
    log_warn("the death penalties ran for the local player (%s)",
             cheats::enabled(cheats::kExitBeforeDeath) ? "exit before death is ON" : "exit before death is off");
    if (cheats::enabled(cheats::kExitBeforeDeath)) request_exit("the character died");
  }
  return g_orig_death ? g_orig_death(game, player, killer) : 0;
}

// A call site's target, from its rel32 (0 when the site is unknown).
uint64_t call_target_rva(sites::Id site) {
  const uintptr_t at = sites::address(site);
  int32_t rel = 0;
  if (!at || !mem::read_safe(at + 1, &rel)) return 0;
  return sites::result(site).rva + 5 + static_cast<int64_t>(rel);
}

// Every experience gain of the local player passes through here (kills and
// quest rewards alike); the multiplier scales the amount before the game adds
// it, so the level-up check and the client's update see the scaled gain.
void __fastcall hk_add_exp(void* game, Unit* unit, int32_t level, int32_t amount) noexcept {
  if (unit && amount > 0 && cheats::enabled(cheats::kExpMultiplier) && game::is_local_player(unit)) {
    const float m = cheats::exp_multiplier();
    const double scaled = static_cast<double>(amount) * static_cast<double>(m) + 0.5;
    // The game keeps experience as a 32-bit value and caps the total at the
    // level-99 figure itself; the gain stays a positive int32.
    const int32_t out = scaled >= 2147483647.0 ? 0x7FFFFFFF : static_cast<int32_t>(scaled);
    ++g_status.exp_awards;
    cheats::note("Experience: %d x %.2f = %d", amount, static_cast<double>(m), out);
    amount = out;
  }
  if (g_orig_add_exp) g_orig_add_exp(game, unit, level, amount);
}

// The area level after the name: only while the automap or the waypoint panel
// is asking, so every other use of a level's name is untouched.
const char* __fastcall hk_level_name(uint32_t data_ctx, int32_t level_id) noexcept {
  const char* name = g_orig_level_name ? g_orig_level_name(data_ctx, level_id) : "";
  perf::Timer timer(perf::kLevelName);
  if ((t_in_automap && cheats::enabled(cheats::kAreaLevelAutomap)) ||
      (t_in_waypoints && cheats::enabled(cheats::kAreaLevelWaypoints)))
    return arealevel::with_level(level_id, name);
  return name;
}

uint64_t __fastcall hk_automap_update(void* a, void* b, void* c, void* d) noexcept {
  ++t_in_automap;
  const uint64_t r = g_orig_automap_update ? g_orig_automap_update(a, b, c, d) : 0;
  --t_in_automap;
  return r;
}

uint64_t __fastcall hk_automap_info(void* a, void* b, void* c, void* d) noexcept {
  ++t_in_automap;
  const uint64_t r = g_orig_automap_info ? g_orig_automap_info(a, b, c, d) : 0;
  --t_in_automap;
  return r;
}

uint64_t __fastcall hk_wp_populate(void* a, void* b, void* c, void* d) noexcept {
  ++t_in_waypoints;
  const uint64_t r = g_orig_wp_populate ? g_orig_wp_populate(a, b, c, d) : 0;
  --t_in_waypoints;
  return r;
}

// A monster's or an object's drop: the loot module runs it (the filter, every pick, extra drops).
void __fastcall hk_tc_drop(void* game, Unit* source, Unit* killer, const void* tc, int32_t forced_quality,
                           int32_t item_level, int32_t skip_nodrop, Unit** out, int32_t* out_count,
                           int32_t out_max) noexcept {
  if (g_orig_tc_drop)
    loot::drop(g_orig_tc_drop, game, source, killer, tc, forced_quality, item_level, skip_nodrop, out, out_count,
               out_max);
}

// Every freshly made item's quality is decided here first: superior, ethereal and socketed where the game allows.
int32_t __fastcall hk_quality_step(Unit* item, uint8_t* request) noexcept {
  const int32_t quality = g_orig_quality_step ? g_orig_quality_step(item, request) : 0;
  return loot::quality_step(item, request, quality);
}

// Every property value an item gets. The game still rolls (the item's seed advances as it would);
// perfect rolls answer with the top of the range, except at the sites that pick an id.
int32_t __fastcall hk_property_roll(Unit* unit, int32_t a, int32_t b) noexcept {
  const uintptr_t from = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
  const int32_t rolled = g_orig_property_roll ? g_orig_property_roll(unit, a, b) : (a < b ? a : b);
  return loot::property_roll(unit, a, b, rolled, from);
}

// Every item the game makes. Under perfect rolls a new armor is made from a unit seed that rolls its top base
// defense: the seed a save keeps with the item, handed to the game's own creation routine (itemseed.cpp). Under
// the loot filter switch a drop is made only in a form the filter shows (loot.cpp says which through the request,
// which gets its own back after), and one the filter hides in every form is not made: the drop core gets no item,
// as it does from the game's item maker when there is no room for one.
Unit* __fastcall hk_create_item(void* game, uint8_t* request, int32_t use_seeds) noexcept {
  if (loot::refuse(request)) return nullptr;
  Unit* const item = itemseed::create(g_orig_create_item, game, request, use_seeds);
  loot::made(request, item);
  return item;
}

// A class item's skill lines: how many and how high are the step's own rolls, both raised by its bonus argument
// (perfect rolls give the bonus that puts them at their tops; which skills is still the step's pick).
void __fastcall hk_class_skills(void* game, Unit* item, int32_t item_level, int32_t first_skill, int32_t skill_count,
                                int32_t bonus) noexcept {
  if (g_orig_class_skills)
    g_orig_class_skills(game, item, item_level, first_skill, skill_count, loot::class_skill_bonus(item, bonus));
}

// Whether a superior kind (a QualityItems row) goes on an item: the superior step asks it of each kind its roll
// comes to and takes the first that does. Under perfect rolls a fresh item's step is told no for every kind but the
// best of those that go on it (loot.cpp).
int32_t __fastcall hk_superior_fits(Unit* item, const uint8_t* row) noexcept {
  return loot::superior_fits(g_orig_superior_fits, item, row);
}

// The item generator's affix steps: each runs as the game made it, then max affixes fills the item up (loot.cpp).
int32_t __fastcall hk_magic_affixes(Unit** item, uint8_t* request) noexcept {
  return loot::magic_affixes(g_orig_magic_affixes, item, request);
}
int32_t __fastcall hk_rare_affixes(Unit* item, uint8_t* request) noexcept {
  return loot::rare_affixes(g_orig_rare_affixes, item, request);
}
int32_t __fastcall hk_crafted_affixes(Unit* item, uint8_t* request) noexcept {
  return loot::crafted_affixes(g_orig_crafted_affixes, item, request);
}

// Every affix the magic and crafted steps pick (and, with the patch below, the rare step's, and max affixes'
// additions): best affixes keeps the best version of the affix the game picked for the item it is making (loot.cpp).
int32_t __fastcall hk_pick_affix(Unit* item, int32_t spawnable, int32_t must, int32_t apply, int32_t prefix,
                                 int32_t forced) noexcept {
  return loot::pick_affix(g_orig_pick_affix, item, spawnable, must, apply, prefix, forced);
}

// The automatic affix, a base's own (its Items row's AutoMagic group, the seventh argument): the generator's last pick,
// whose row it writes and applies itself. Best affixes keeps the best version of it (loot.cpp).
int32_t __fastcall hk_auto_affix(Unit* item, int32_t spawnable, int32_t must, int32_t apply, int32_t prefix,
                                 int32_t forced, int32_t group) noexcept {
  return loot::pick_auto_affix(g_orig_auto_affix, item, spawnable, must, apply, prefix, forced, group);
}

// Every use of an item asks whether the item goes: the client as it builds its request (the
// request carries the answer), the server as it checks that request. A scroll or potion under its
// switch stays, and both sides agree because both ask here.
bool __fastcall hk_should_remove(Unit* item) noexcept {
  const bool remove = g_orig_should_remove ? g_orig_should_remove(item) : false;
  return remove && consumables::keep_after_use(item) ? false : remove;
}

// A tome's quantity changes here. The item-use effect takes a charge with -1 from one call site;
// that call is skipped under the switch. Merging two tomes and buying scrolls come from other
// sites and move charges as usual.
void __fastcall hk_sync_quantity(void* game, Unit* player, Unit* item, int32_t delta) noexcept {
  const uintptr_t from = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
  if (delta < 0 && from == g_book_debit_return && consumables::keep_tome_charge(player, item)) return;
  if (g_orig_sync_quantity) g_orig_sync_quantity(game, player, item, delta);
}

// A locked chest or door asks for a key here (an Assassin's chest does not ask). The key use takes one
// from the inventory and says so; under the switch the local player's key opens the lock and stays.
int32_t __fastcall hk_use_key(void* game, Unit* player) noexcept {
  if (player && consumables::keep_key(player)) return 1;
  return g_orig_use_key ? g_orig_use_key(game, player) : 0;
}

// No durability loss: the local player's gear keeps its durability, so the wear is not rolled at all.
bool keep_durability(Unit* owner, volatile LONG* seen, const char* what) {
  if (!owner || !on(cheats::kNoDurabilityLoss) || !game::is_local_player(owner)) return false;
  ++g_status.wear_skipped;
  if (log_once(seen)) logf("no durability loss: %s skipped", what);
  return true;
}

// A melee hit that lands wears one item: the attacker's weapon when the attacker is a player, one armor piece of
// a player who is hit (a roll first: 10 % armor, 10 % a throwing weapon, 4 % any other weapon).
void __fastcall hk_item_wear(void* game, Unit* owner, Unit* item) noexcept {
  if (keep_durability(owner, &g_hit_wear_seen, "a hit's wear on your weapon or armor")) return;
  if (g_orig_item_wear) g_orig_item_wear(game, owner, item);
}

// Impale wears the weapon it was used with, by the skill's own chance and amount (a thrown weapon loses one of
// its stack instead).
void __fastcall hk_impale_wear(void* game, Unit* player, Unit* weapon, int32_t chance, int32_t amount) noexcept {
  if (keep_durability(player, &g_impale_wear_seen, "Impale's wear on your weapon")) return;
  if (g_orig_impale_wear) g_orig_impale_wear(game, player, weapon, chance, amount);
}

// An item's name, on the UI thread (D2RCore's tooltip code calls the game's builder). The game adds the level
// itself for an Items row with ShowLevel; under the switch the other weapons, armor, rings, amulets, charms and
// jewels get it the same way (itemlevel.cpp).
void __fastcall hk_item_name(Unit* item, char* name) noexcept {
  if (g_orig_item_name) g_orig_item_name(item, name);
  itemlevel::append(item, name, kItemNameSize);
}

// A monster's curse, for each unit in its range: a curse skill's step, and the Cursed monster modifier's Amplify
// Damage. Under the switch the step is not made for the local player: nothing is put on, as when the unit is one
// the curse does not take.
uint64_t __fastcall hk_curse_step(Unit* target, uint8_t* context) noexcept {
  const int at = sites::curse_facts().state_offset;
  int32_t state = -1;
  if (target && context && at > 0 && on(cheats::kCannotBeCursed) && game::is_local_player(target) &&
      mem::read_safe(reinterpret_cast<uintptr_t>(context) + static_cast<uintptr_t>(at), &state) &&
      curses::keep_off(target, state))
    return 0;
  return g_orig_curse_step ? g_orig_curse_step(target, context) : 0;
}

void __fastcall hk_cursed_mod(void* game, Unit* monster, Unit* target, int32_t level) noexcept {
  if (target && on(cheats::kCannotBeCursed) && game::is_local_player(target) &&
      curses::keep_off(target, curses::cursed_modifier_state(game::unit_table_bank(target))))
    return;
  if (g_orig_cursed_mod) g_orig_cursed_mod(game, monster, target, level);
}

// What a vendor takes its price with: buying, gambling, repairs, Cain's identifying, hiring and reviving the
// mercenary. Each of them has made sure the player holds the price before it comes here (and asserts that the
// payment then succeeds), so under the switch the answer is "paid" and nothing is taken.
int32_t __fastcall hk_vendor_pay(void* game, Unit* player, int32_t amount) noexcept {
  if (player && amount > 0 && on(cheats::kInfiniteGold) && game::is_local_player(player)) {
    ++g_status.payments_free;
    g_status.gold_kept += amount;
    if (log_first(&g_pay_seen, 4)) logf("infinite gold: a price of %d gold was not taken", amount);
    cheats::note("Infinite gold: %d gold not taken", amount);
    return 1;
  }
  return g_orig_vendor_pay ? g_orig_vendor_pay(game, player, amount) : 0;
}

// The home town (hometown.cpp). A cast marks the portals made inside it as the player's own town portal; the
// portal at the destination is made in the home town; a portal that leads into another act takes its user there
// by the game's own move to a level.
uint64_t __fastcall hk_portal_start(void* game, Unit* player, void* item) noexcept {
  const hometown::Cast cast(player);
  return g_orig_portal_start ? g_orig_portal_start(game, player, item) : 0;
}

Unit* __fastcall hk_portal_town_end(void* game, Unit* player, Unit* portal, int32_t destination,
                                    int32_t source) noexcept {
  const int32_t level = hometown::town_end_level(game, player, portal, destination);
  Unit* const made = g_orig_portal_town_end ? g_orig_portal_town_end(game, player, portal, level, source) : nullptr;
  if (level != destination) hometown::town_end_made(player, portal, made, level, destination);
  return made;
}

uint64_t __fastcall hk_portal_operate(void** context) noexcept {
  switch (hometown::use(context)) {
    case hometown::Used::kDone: return 1;
    case hometown::Used::kRefused: return 0;
    case hometown::Used::kGameOwn: break;
  }
  return g_orig_portal_operate ? g_orig_portal_operate(context) : 0;
}

// The client's trip through a town portal (UI thread). In a town the client builds the rooms at the portal's
// other end in the act it is in and moves its player there before it asks the server; for a portal whose other end
// stands in another act that cannot be (the act has no such level). The answer "no" has the client send the
// ordinary request and wait for the server, as it does outside a town.
uint64_t __fastcall hk_portal_trip(Unit* player, Unit* portal) noexcept {
  if (hometown::leads_across_acts(player, portal)) return 0;
  return g_orig_portal_trip ? g_orig_portal_trip(player, portal) : 0;
}

// An NPC's menu as it opens (UI thread). The game puts an item service in the menu of the NPC that does it when
// the quest has a reward to be had; under the service's switch the game's own routine for that entry is called
// before the opener walks its rows, so the menu it builds has the entry whatever the quest says (npcservice.cpp).
uint64_t __fastcall hk_npc_menu(Unit* npc, int32_t after_speech) noexcept {
  npcservice::menu_opens(npc);
  return g_orig_npc_menu ? g_orig_npc_menu(npc, after_speech) : 0;
}

// A cube recipe's products (the transmute's second pass, after the characters were saved). Under the switch the
// ingredients the routine uses up, and what Clear Sockets takes out of the sockets, are kept by the two frees below
// and handed back to the player once the routine is through (cube.cpp).
void __fastcall hk_cube_products(void* game, Unit* player, void* recipe, void* inputs) noexcept {
  cube::products(g_orig_cube_products, g_orig_item_free, game, player, recipe, inputs);
}

// The free of an item that lies on no inventory and not on the ground: every sale, every item used up, every item of
// a unit that goes. Only the product routine's own free of an ingredient, inside a transmute the switch is on for
// (its return address says which call it is), is not made.
void __fastcall hk_item_free(void* game, Unit* item) noexcept {
  const uintptr_t from = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
  if (cube::keep_ingredient(item, from)) return;
  if (g_orig_item_free) g_orig_item_free(game, item);
}

// The free of what is in an item's sockets. For Clear Sockets inside such a transmute the socket contents are taken
// out and kept first; the game's own free then finds the sockets empty.
void __fastcall hk_socketed_free(void* game, Unit* item) noexcept {
  cube::keep_socketed(game, item, reinterpret_cast<uintptr_t>(__builtin_return_address(0)), g_orig_item_free);
  if (g_orig_socketed_free) g_orig_socketed_free(game, item);
}

// The clients told where an item went: every move of an item comes here (124 callers). Only the pick-up routine's
// notice of the item it has just put in the inventory, the belt or the cube, or on the cursor, is taken (its return
// address says which call it is): under the switch the local player's item is identified around it (autoid.cpp).
void __fastcall hk_item_notice(void* game, Unit* player, Unit* item, uint32_t flags, uint32_t command, int32_t mode,
                               uint32_t page, uint32_t body, uint32_t position) noexcept {
  autoid::notice(g_orig_item_notice, game, player, item, flags, command, mode, page, body, position,
                 reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
}

// A monster's AI tick: every monster's, the mercenary's and every summon's included, comes here. For a pet of the
// local player's that its switch keeps passive the thread is marked for the length of the tick (passive.cpp); any
// other's is the game's.
void __fastcall hk_ai_dispatch(void* game, Unit* unit, uint64_t a3, uint64_t a4, uint64_t a5) noexcept {
  passive::think(g_orig_ai_dispatch, game, unit, a3, a4, a5);
}

// Whether a unit is another's enemy: every way an AI finds a target asks it, and so do skills, curses and auras.
// While a passive pet's tick is marked the answer is "not an enemy"; any other question gets the game's answer.
uint64_t __fastcall hk_enemy_test(void* game, Unit* unit, Unit* other, uint64_t flag) noexcept {
  return passive::enemy(g_orig_enemy_test, game, unit, other, flag);
}

// The kind of the target kept on a monster (on a pet the Warlock demons' own, or one a summoning skill set; Attract
// and Confuse set one on hostile monsters only): none while a passive pet's tick is marked.
uint64_t __fastcall hk_kept_target(Unit* monster) noexcept {
  return passive::kept_target(g_orig_kept_target, monster);
}

// A death's wake: the game's own first, which makes the player whole and wakes it in the town of the act it died
// in; then, for the local player that was dead when the request came, the move on to the home town.
uint64_t __fastcall hk_wake_in_town(void* game, Unit* player, void* packet, int32_t size) noexcept {
  const bool dying = hometown::dying(player);
  const uint64_t answer = g_orig_wake ? g_orig_wake(game, player, packet, size) : 0;
  if (dying) hometown::woke(game, player);
  return answer;
}

// The item writer's network form (what the server sends a client) holds the real item level only for an Items
// row with ShowLevel, a placeholder 1 for the rest. Its `jne` to the real-level branch becomes a `jmp`, written by
// the loader (which puts the bytes back when the plugin unloads). Seven bits either way, so the client reads the
// same stream; a save writes the real level on its own branch and never comes here.
bool send_real_item_levels() {
  const D2RL::PluginContext* ctx = context();
  const uintptr_t at = sites::address(sites::kItemLevelSend);
  static const uint8_t kPlaceholder[5] = {0xBA, 0x01, 0x00, 0x00, 0x00};  // mov edx, 1: the level written otherwise
  uint8_t jne[6] = {};
  uint8_t next[5] = {};
  if (!ctx || !at || !mem::copy_from(jne, at, sizeof(jne)) || !mem::copy_from(next, at + sizeof(jne), sizeof(next)) ||
      jne[0] != 0x0F || jne[1] != 0x85 || std::memcmp(next, kPlaceholder, sizeof(next)) != 0) {
    log_warn("hooks: the item writer's ShowLevel test not found as expected - the item level stays off");
    return false;
  }
  int32_t rel = 0;
  std::memcpy(&rel, jne + 2, sizeof(rel));
  const uint64_t rva = sites::result(sites::kItemLevelSend).rva;
  const uint64_t target = rva + sizeof(jne) + static_cast<int64_t>(rel);
  if (!ctx->PatchJmpRel32(rva, jne, sizeof(jne), target, sizeof(jne))) {
    log_error("hooks: the loader refused the item writer patch at 0x%llX - another plugin may own it",
              static_cast<unsigned long long>(rva));
    return false;
  }
  logf("hooks: the item writer sends every item's real level to the client (0x%llX: jne -> jmp 0x%llX)",
       static_cast<unsigned long long>(rva), static_cast<unsigned long long>(target));
  return true;
}

// The rare step calls D2RCore's affix picker straight, not through the game's wrapper the other steps call, so its
// two picks would never reach the picker hook. Each call is pointed at the wrapper instead, written by the loader
// (which puts the bytes back when the plugin unloads). The arguments are the same: the wrapper hands an expansion
// item to that same picker with the same group 0 the rare step passes, and the rare step makes only expansion items.
int route_rare_picks() {
  const D2RL::PluginContext* ctx = context();
  const uintptr_t base = sites::exe_base();
  const uintptr_t wrapper = sites::address(sites::kAffixPicker);
  const uintptr_t calls[2] = {sites::derived(sites::dRarePickSuffix), sites::derived(sites::dRarePickPrefix)};
  const char* const sides[2] = {"suffix", "prefix"};
  if (!ctx || !wrapper || !calls[0] || !calls[1]) {
    log_warn("hooks: the rare affix step's picks not found - best affixes leaves rare items as the game picks them");
    return 0;
  }
  bool routed[2] = {};
  for (int i = 0; i < 2; ++i) {
    uint8_t call[5] = {};
    if (!mem::copy_from(call, calls[i], sizeof(call)) || call[0] != 0xE8) {
      log_warn("hooks: the rare affix step's %s pick at 0x%llX is not a call", sides[i],
               static_cast<unsigned long long>(calls[i] - base));
      continue;
    }
    routed[i] = ctx->PatchCallRel32(calls[i] - base, call, sizeof(call), wrapper - base, sizeof(call));
    if (!routed[i])
      log_error("hooks: the loader refused to point the rare affix step's %s pick (0x%llX) at the affix picker",
                sides[i], static_cast<unsigned long long>(calls[i] - base));
  }
  logf("hooks: the rare affix step's picks go through the affix picker: suffix %s, prefix %s (0x%llX, 0x%llX -> 0x%llX)",
       routed[0] ? "yes" : "NO", routed[1] ? "yes" : "NO", static_cast<unsigned long long>(calls[0] - base),
       static_cast<unsigned long long>(calls[1] - base), static_cast<unsigned long long>(wrapper - base));
  return (routed[0] ? 1 : 0) + (routed[1] ? 1 : 0);
}

// The property roll's eight choice sites must be there and call the roll, or perfect rolls stay off:
// a pick answered with its top would always give the last skill, class, stat, group entry or skill tab
// (Wraithstep's "+1 to <tab> Skills" came out Chaos, the Warlock's last tab, every time).
bool collect_choice_sites() {
  const uintptr_t roll = sites::address(sites::kPropertyRoll);
  const sites::Id ids[] = {sites::kRollPickSkill,       sites::kRollPickClass,       sites::kRollPickStat,
                           sites::kRollPickGroupA,      sites::kRollPickGroupB,      sites::kRollPickGroupParam,
                           sites::kRollPickGroupParamA, sites::kRollPickGroupParamB};
  constexpr int kSites = static_cast<int>(sizeof(ids) / sizeof(ids[0]));
  static_assert(kSites <= loot::kMaxChoiceSites, "loot keeps at most kMaxChoiceSites choice sites");
  uintptr_t returns[kSites] = {};
  for (int i = 0; i < kSites; ++i) {
    const uintptr_t at = sites::address(ids[i]);
    if (!roll || !at || sites::call_target(ids[i]) != roll) {
      log_warn("hooks: %s is not a call to the property roll - perfect rolls stay off", sites::spec(ids[i]).name);
      return false;
    }
    returns[i] = at + 5;
  }
  const mem::Range text = mem::section(sites::exe_base(), ".text");
  if (text.empty()) return false;
  loot::set_choice_sites(returns, kSites, text.begin, text.end);
  logf("hooks: the property roll's %d choice sites call it as expected (skill, class, stat, two group picks, a group "
       "entry's param in each of three pick modes)",
       kSites);
  return true;
}

template <typename Fn>
bool install_one(sites::Id id, Fn detour, Fn* original, const char* what) {
  const D2RL::PluginContext* ctx = context();
  uint8_t expected[128];  // the longest signature of a hooked routine has 96 bytes (the kept target's getter)
  const size_t n = sites::entry_bytes(id, expected, sizeof(expected));
  if (!ctx || !n) {
    log_warn("hooks: %s not resolved - not hooked", what);
    return false;
  }
  const uint64_t rva = sites::result(id).rva;
  if (!ctx->InstallInlineHook(rva, expected, static_cast<uint32_t>(n), detour, original)) {
    log_error("hooks: the loader refused the hook on %s (0x%llX) - another plugin may own it", what,
              static_cast<unsigned long long>(rva));
    return false;
  }
  logf("hooks: %s hooked at 0x%llX", what, static_cast<unsigned long long>(rva));
  return true;
}

}  // namespace
}  // namespace d2rcc::hooks

// --- the mana-cost hook -------------------------------------------------------------------
// GetManaCost never sees the unit, but both of its callers keep the unit in a
// callee-saved register when they call it: the server's SKILLMANA_Consume in
// rsi, the client's own mana check in rbx. A naked thunk hands both registers
// (and the return address, for the log) to the C++ side.
extern "C" int32_t d2rcc_mana_cost_impl(uint32_t data_ctx, int32_t skill, int32_t level, uint64_t rsi, uint64_t rbx,
                                        uint64_t return_address) noexcept {
  using namespace d2rcc;
  using namespace d2rcc::hooks;
  (void)return_address;  // logged in developer builds only
  bool costs_nothing = false;
  {
    perf::Timer timer(perf::kManaCost);
    if (cheats::enabled(cheats::kInfiniteMana)) {
      Unit* a = reinterpret_cast<Unit*>(rsi);
      Unit* b = reinterpret_cast<Unit*>(rbx);
      if (game::is_local_player(a) || game::is_local_player(b)) {
#ifdef D2RCC_DEV
        // The first few only: the client asks at every frame a skill's cost is on screen.
        if (log_first(&g_mana_cost_seen, 4))
          logf("mana cost: zeroed for the local player (skill %d level %d, %s unit, return address rva 0x%llX)", skill,
               level, game::is_local_player(a) ? "server" : "client",
               static_cast<unsigned long long>(return_address - sites::exe_base()));
#endif
        costs_nothing = true;
      }
    }
  }
  if (costs_nothing) return 0;
  return g_orig_mana_cost ? g_orig_mana_cost(data_ctx, skill, level) : 0;
}

extern "C" __attribute__((naked)) void d2rcc_mana_cost_thunk() noexcept {
  asm("pushq %rbp\n\t"
      "movq %rsp, %rbp\n\t"
      "andq $-16, %rsp\n\t"
      "subq $0x30, %rsp\n\t"
      "movq %rsi, %r9\n\t"          // 4th argument: rsi
      "movq %rbx, 0x20(%rsp)\n\t"   // 5th: rbx
      "movq 8(%rbp), %rax\n\t"      // 6th: the return address into the game
      "movq %rax, 0x28(%rsp)\n\t"
      "call d2rcc_mana_cost_impl\n\t"
      "movq %rbp, %rsp\n\t"
      "popq %rbp\n\t"
      "ret");
}

// --- the protected-stat hook (god mode) ---------------------------------------------------
// A player's life may not go down, but for a life above its top (Battle Orders ended, an item that gave life taken
// off), which goes down to the top and no further. D2RCore's stat writers (set and add, of a unit and of a stat list)
// ask the game's protected-stat test before they lower any value, whatever lowers it: a hit, the damage returned to
// an attacker, crushing blow, poison and burning as they run, Blood Mana, a skill's cost. The test is told only the
// stat and the unit's type (so every player in the game is kept; offline, that is you); the two unit writers keep
// the unit in rdi, callee-saved, which a thunk hands on. The list writers keep other things there, which do not read
// as a player of the local player's id: for them life is only kept. Any thread that writes stats calls it, each
// about its own units (the server's player, or the client's copy of it).
extern "C" bool d2rcc_protected_stat_impl(int32_t stat, int32_t unit_type, uint64_t rdi) noexcept {
  using namespace d2rcc;
  using namespace d2rcc::hooks;
  if (stat != game::kHitpoints || unit_type != static_cast<int32_t>(game::kPlayer) ||
      !cheats::enabled(cheats::kGodMode))
    return g_orig_protected_stat ? g_orig_protected_stat(stat, unit_type) : false;
  if (t_life_to_top) return false;  // god mode's own lowering to the top, below
  Unit* u = reinterpret_cast<Unit*>(rdi);
  const bool known = mem::plausible(rdi) && game::is_local_player(u);
  if (known) {
    const int32_t life = game::get_stat(u, game::kHitpoints);
    const int32_t top = game::get_stat(u, game::kMaxHp);
    if (top > 0 && life > top) {
      if (log_first(&g_life_topped_seen, 4))
        logf("god mode: your life was above its top (%d > %d) and goes down to it", life >> 8, top >> 8);
      // To the top exactly, whatever the write that asked would have made of it (a hit in the same frame, a poison
      // tick): the game's setter makes it here, on the thread and the unit the writer was at, and that write is
      // not made.
      ++t_life_to_top;
      game::set_stat(u, game::kHitpoints, top);
      --t_life_to_top;
      return true;
    }
  }
  InterlockedIncrement(&g_life_kept);
  if (log_first(&g_life_kept_seen, 4)) {
    const uintptr_t from = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    const uintptr_t core = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"D2RCore.dll"));
    if (core && from > core && from - core < 0x10000000)
      logf("god mode: a player's life was not lowered (asked by the stat writer at D2RCore.dll+0x%llX, %s)",
           static_cast<unsigned long long>(from - core), known ? "your unit" : "no unit in rdi");
    else
      logf("god mode: a player's life was not lowered (asked from 0x%llX)",
           static_cast<unsigned long long>(from - sites::exe_base()));
  }
  return true;
}

// The writer's rdi as a third argument; the impl answers the writer itself.
extern "C" __attribute__((naked)) void d2rcc_protected_stat_thunk() noexcept {
  asm("movq %rdi, %r8\n\t"
      "jmp d2rcc_protected_stat_impl");
}

namespace d2rcc::hooks {

bool install() {
  if (!game::bound()) {
    for (int k = 0; k < cheats::kCount; ++k) cheats::set_why_not(static_cast<cheats::Kind>(k), "stat helpers not found");
    return false;
  }
  const bool regen = install_one(sites::kPlayerStatRegen, &hk_regen, &g_orig_regen, "D2GAME_PLAYER_ApplyStatRegen");
  const bool execute = install_one(sites::kExecuteEvents, &hk_execute_events, &g_orig_execute, "SUNITDMG_ExecuteEvents");
  const bool finalize = install_one(sites::kFinalizeDamage, &hk_finalize_damage, &g_orig_finalize, "SUNITDMG_FinalizeDamage");
  install_one(sites::kDeathHandler, &hk_death, &g_orig_death, "death penalties");
  // God mode: the test D2RCore's stat writers ask before they lower a value (checked to read the no-damage switches).
  g_life_guarded = sites::protected_stat_checked() &&
                   install_one(sites::kProtectedStat, reinterpret_cast<ProtectedStatFn>(&d2rcc_protected_stat_thunk),
                               &g_orig_protected_stat, "protected-stat test");
  const bool mana_cost = install_one(sites::kGetManaCost, reinterpret_cast<ManaCostFn>(&d2rcc_mana_cost_thunk),
                                     &g_orig_mana_cost, "D2Common_SKILLMANA_GetManaCost");
  if (mana_cost) {
    const uint64_t routine = sites::result(sites::kGetManaCost).rva;
    const sites::Id calls[2] = {sites::kManaCostServerCall, sites::kManaCostClientCall};
    const char* names[2] = {"the server's SKILLMANA_Consume", "the client's mana check"};
    for (int i = 0; i < 2; ++i) {
      const uint64_t target = call_target_rva(calls[i]);
      if (target == routine)
        logf("hooks: %s calls the mana-cost routine at 0x%llX as expected", names[i],
             static_cast<unsigned long long>(sites::result(calls[i]).rva));
      else
        log_warn("hooks: %s (0x%llX) calls 0x%llX, not the mana-cost routine - the register capture may miss it",
                 names[i], static_cast<unsigned long long>(sites::result(calls[i]).rva),
                 static_cast<unsigned long long>(target));
    }
  }
  const bool exp = install_one(sites::kPlayerAddExperience, &hk_add_exp, &g_orig_add_exp, "PLAYER_AddExperience");
  const bool names = install_one(sites::kClientGetLevelName, &hk_level_name, &g_orig_level_name, "CLIENT_GetLevelName");
  const bool automap = names && install_one(sites::kAutomapInfoUpdate, &hk_automap_info, &g_orig_automap_info, "AutomapInfo_Update");
  if (names) install_one(sites::kAutomapUpdate, &hk_automap_update, &g_orig_automap_update, "AutomapPanel_Update");
  const bool wp = names && install_one(sites::kWaypointPopulate, &hk_wp_populate, &g_orig_wp_populate, "WaypointPanel_Populate");
  // Loot.
  const bool tc_drop = install_one(sites::kTcDropCore, &hk_tc_drop, &g_orig_tc_drop, "TreasureClassDropCore");
  const bool quality =
      install_one(sites::kItemQualityStep, &hk_quality_step, &g_orig_quality_step, "item generation quality step");
  const bool choices = collect_choice_sites();
  const bool roll = choices && install_one(sites::kPropertyRoll, &hk_property_roll, &g_orig_property_roll, "property roll");
  // An armor's base defense is rolled by the unit's seed, the one a save keeps: the item is made from a seed that
  // rolls the top, by the game's own item init.
  if (roll && (!itemseed::has_tables() ||
               !install_one(sites::kCreateItem, &hk_create_item, &g_orig_create_item, "item creation")))
    logf("hooks: perfect rolls leave armor base defense as rolled (item creation is not hooked)");
  // A class item's skill lines are rolls of their own; the step's bonus argument raises them (its top from sites.cpp).
  if (roll && (!sites::affix_facts().skill_bonus ||
               !install_one(sites::kClassSkillBonus, &hk_class_skills, &g_orig_class_skills, "class item skill bonuses")))
    logf("hooks: perfect rolls leave class items' skill lines as rolled (their routine is not hooked)");
  // A superior item's kind is a pick of the superior step's, rolled inline: the step's test of a kind is hooked, and
  // the quality step's hook says whose step it is.
  if (roll && (!quality || !sites::superior_facts().known ||
               !install_one(sites::kSuperiorFits, &hk_superior_fits, &g_orig_superior_fits, "superior kind test")))
    logf("hooks: perfect rolls leave a superior item's kind as rolled (its test is not hooked)");
  // Max affixes and best affixes: the three affix steps. Each runs as the game made it; max affixes then tops the item
  // up with the game's own picker (where sites.cpp found the step's top count), and best affixes tells the picker
  // hook below which item the step is making.
  const bool picker = game::has_affix_picker();
  const bool magic_affixes = picker && install_one(sites::kMagicAffixes, &hk_magic_affixes, &g_orig_magic_affixes,
                                                   "item generation: magic affixes");
  const bool rare_affixes = picker && install_one(sites::kRareAffixes, &hk_rare_affixes, &g_orig_rare_affixes,
                                                  "item generation: rare affixes");
  const bool crafted_affixes = picker && install_one(sites::kCraftedAffixes, &hk_crafted_affixes,
                                                     &g_orig_crafted_affixes, "item generation: crafted affixes");
  const bool rare_max = rare_affixes && sites::affix_facts().rare_max;
  const bool crafted_max = crafted_affixes && sites::affix_facts().crafted_max;
  if (magic_affixes || rare_max || crafted_max)
    logf("hooks: max affixes covers magic %s, rare %s, crafted %s", magic_affixes ? "yes" : "NO",
         rare_max ? "yes" : "NO", crafted_max ? "yes" : "NO");
  // Best affixes: the picker the magic and crafted steps call (so do max affixes' additions), with the rare step's
  // two picks pointed at it too.
  const bool affix_rows = game::has_affix_rows();
  const bool pick_hook = picker && affix_rows && (magic_affixes || rare_affixes || crafted_affixes) &&
                         install_one(sites::kAffixPicker, &hk_pick_affix, &g_orig_pick_affix, "affix picker");
  const bool rare_best = pick_hook && rare_affixes && route_rare_picks() == 2;
  // ... and the automatic affix, a base's own, which the generator picks last through the picker's twin (checked to
  // call the same two pickers): the quality step's hook says whose item it is.
  const bool auto_hook = affix_rows && quality && sites::affix_facts().auto_same &&
                         install_one(sites::kAutoAffixPicker, &hk_auto_affix, &g_orig_auto_affix,
                                     "automatic affix picker");
  if (pick_hook || auto_hook)
    logf("hooks: best affixes covers magic %s, rare %s, crafted %s, automatic %s",
         pick_hook && magic_affixes ? "yes" : "NO", rare_best ? "yes" : "NO", pick_hook && crafted_affixes ? "yes" : "NO",
         auto_hook ? "yes" : "NO");
  const char* removal = game::has_item_removal() ? nullptr : "the game's item removal not found";
  // Consumables: the use predicate covers scrolls and potions; tomes need the quantity update and
  // the one call of it that takes a charge (its return address is how the hook knows the call).
  const bool should_remove =
      install_one(sites::kShouldRemoveOnUse, &hk_should_remove, &g_orig_should_remove, "ITEMS_ShouldRemoveOnUse");
  bool tomes = false;
  const uintptr_t sync = sites::address(sites::kSyncItemQuantity);
  if (sync && sites::call_target(sites::kItemUseBookDebit) == sync) {
    g_book_debit_return = sites::address(sites::kItemUseBookDebit) + 5;
    tomes = install_one(sites::kSyncItemQuantity, &hk_sync_quantity, &g_orig_sync_quantity, "item quantity update");
    if (tomes) logf("hooks: the item-use effect takes a tome's charge at 0x%llX as expected",
                    static_cast<unsigned long long>(sites::result(sites::kItemUseBookDebit).rva));
  } else {
    log_warn("hooks: the item-use effect's tome charge call was not found - tomes are used up as usual");
  }
  g_items_why = !should_remove              ? "item use routine not hooked"
                : !consumables::has_tables() ? "the game's item table is unavailable"
                                             : nullptr;
  if (should_remove && !tomes && !g_items_why) logf("hooks: infinite town portal and identify keep scrolls only");
  // Keys: the hook needs the key use's own inventory walk to know the player carries a key.
  const bool keys = game::has_key_check() && install_one(sites::kUseKey, &hk_use_key, &g_orig_use_key, "key use");
  // Durability: a hit's wear covers the weapon and the armor; Impale's wear is its own routine.
  const bool wear = install_one(sites::kItemWear, &hk_item_wear, &g_orig_item_wear, "item durability loss");
  if (wear && !install_one(sites::kImpaleWear, &hk_impale_wear, &g_orig_impale_wear, "Impale's weapon wear"))
    logf("hooks: no durability loss leaves Impale's wear on the weapon as it is (its routine is not hooked)");
  // Item level: the name builder first (it does nothing until the switch), then the writer patch, so the client
  // is sent real levels only when something can show them.
  const bool item_names =
      install_one(sites::kItemNameBuild, &hk_item_name, &g_orig_item_name, "inventory item name");
  const bool levels_sent = item_names && send_real_item_levels();
  // Cannot be cursed: the two steps that put a monster's curse on a unit. What they hand the routine that puts it
  // on was checked by sites.cpp.
  const bool curse_step = sites::curse_facts().state_offset &&
                          install_one(sites::kCurseSkillStep, &hk_curse_step, &g_orig_curse_step, "curse skill step");
  if (curse_step && (!sites::curse_facts().amplify_skill ||
                     !install_one(sites::kCursedModStep, &hk_cursed_mod, &g_orig_cursed_mod,
                                  "Cursed monster modifier step")))
    logf("hooks: cannot be cursed leaves the Cursed monster modifier's curse to the tick, which ends it (its step "
         "is not hooked)");
  // Infinite gold: what every vendor takes its price with.
  const bool pay = install_one(sites::kVendorPay, &hk_vendor_pay, &g_orig_vendor_pay, "vendor payment");
  // The home town. The client's trip first: a portal into another act must never be one the client goes through
  // ahead of the server. Then a portal's use: it is what such a portal needs on the server, and without the other
  // two none is made. The cast last: without it the town end maker changes nothing. Then a death's wake, which
  // only adds to them: without it a death wakes the player where the game does.
  const char* portal_why = hometown::bind();
  const bool portals = !portal_why &&
                       install_one(sites::kClientPortalTrip, &hk_portal_trip, &g_orig_portal_trip,
                                   "client's portal trip") &&
                       install_one(sites::kPortalOperate, &hk_portal_operate, &g_orig_portal_operate, "portal use") &&
                       install_one(sites::kPortalTownEnd, &hk_portal_town_end, &g_orig_portal_town_end,
                                   "portal town end") &&
                       install_one(sites::kTownPortalStart, &hk_portal_start, &g_orig_portal_start,
                                   "town portal cast");
  const bool wake = portals && sites::wake_facts().known &&
                    install_one(sites::kWakeInTown, &hk_wake_in_town, &g_orig_wake, "wake in town");
  hometown::wake_hooked(wake);
  if (portals && !wake)
    logf("hooks: the home town leaves a death's wake to the game (its handler is not hooked): you wake in the town "
         "of the act you died in");
  // NPC item services without their quests: the client's menu opener, where a service is put in its NPC's menu.
  // The server needs no hook: the tick takes the quest out of the NPC's record (npcservice.cpp).
  const char* npc_why = npcservice::bind();
  const bool npc_menu =
      !npc_why && install_one(sites::kNpcMenuOpen, &hk_npc_menu, &g_orig_npc_menu, "client's NPC menu");
  // Infinite cube ingredients: the two frees first (they keep nothing until a transmute is marked), then the product
  // routine, which marks one. Without the socket contents' free Clear Sockets destroys them as the game does.
  const char* cube_why = cube::bind();
  const bool item_free = !cube_why && install_one(sites::kItemFree, &hk_item_free, &g_orig_item_free, "item free");
  const bool socketed_free = item_free && sites::cube_facts().sockets_free && game::has_socket_takeout() &&
                             install_one(sites::kSocketedFree, &hk_socketed_free, &g_orig_socketed_free,
                                         "socket contents free");
  const bool cube_products =
      item_free && install_one(sites::kCubeProducts, &hk_cube_products, &g_orig_cube_products, "cube products");
  cube::hooked(socketed_free);
  if (cube_products && !socketed_free)
    logf("hooks: infinite cube ingredients leaves what Clear Sockets takes out of the sockets to the game: destroyed");
  // Identify on pickup: the item notice, which acts only on the pick-up routine's call (its return address, read by
  // sites.cpp); inside it the game's identify routine.
  const char* autoid_why = autoid::bind();
  const bool item_notice =
      !autoid_why && install_one(sites::kItemNotice, &hk_item_notice, &g_orig_item_notice, "item notice");
  // Passive mercenary and minions: the enemy test and the kept target's getter first (they answer as the game does
  // while no tick is marked), then the AI dispatcher, which marks a passive pet's.
  const char* passive_why = passive::bind();
  const bool enemy_test =
      !passive_why && install_one(sites::kEnemyTest, &hk_enemy_test, &g_orig_enemy_test, "enemy test");
  const bool kept_target =
      enemy_test && install_one(sites::kKeptTarget, &hk_kept_target, &g_orig_kept_target, "kept target");
  const bool ai_dispatch =
      kept_target && install_one(sites::kAiDispatch, &hk_ai_dispatch, &g_orig_ai_dispatch, "AI dispatcher");

  cheats::set_why_not(cheats::kGodMode, g_life_guarded || execute ? nullptr : "damage routines not hooked");
  cheats::set_why_not(cheats::kInfiniteMana, regen ? nullptr : "stat regeneration not hooked");
  cheats::set_why_not(cheats::kInfiniteStamina, regen ? nullptr : "stat regeneration not hooked");
  if (regen && !mana_cost) logf("hooks: infinite mana refills on the tick only (mana-cost routine not hooked)");
  cheats::set_why_not(cheats::kMoveSpeed, regen && game::has_base_stat() ? nullptr : "stat regeneration not hooked");
  cheats::set_why_not(cheats::kExitBeforeDeath, execute && finalize && regen ? nullptr : "damage routines not hooked");
  cheats::set_why_not(cheats::kExpMultiplier, exp ? nullptr : "experience routine not hooked");
  // All areas terrorized hooks nothing of its own: the tick applies the zones (terror.cpp).
  const char* terror_why = terror::bind();
  cheats::set_why_not(cheats::kTerrorAll, !regen ? "stat regeneration not hooked" : terror_why);
  cheats::set_why_not(cheats::kAreaLevelAutomap, automap ? nullptr : "automap routine not hooked");
  cheats::set_why_not(cheats::kAreaLevelWaypoints, wp ? nullptr : "waypoint routine not hooked");
  cheats::set_why_not(cheats::kLootFilterOnly, !tc_drop                   ? "drop routine not hooked"
                                               : removal                  ? removal
                                               : !game::has_loot_filter() ? "the game's loot filter not found"
                                                                          : nullptr);
  cheats::set_why_not(cheats::kChronicleOnly, !tc_drop  ? "drop routine not hooked"
                                              : removal ? removal
                                                        : chronicle::bind());
  cheats::set_why_not(cheats::kExtraDrops, !tc_drop ? "drop routine not hooked" : removal);
  cheats::set_why_not(cheats::kExtraDropsElite, !tc_drop ? "drop routine not hooked" : removal);
  cheats::set_why_not(cheats::kPerfectRolls, !choices ? "the property roll's picks not found"
                                             : !roll  ? "property roll not hooked"
                                                      : nullptr);
  cheats::set_why_not(cheats::kAllSuperior, quality ? nullptr : "item generator not hooked");
  cheats::set_why_not(cheats::kAllEthereal, quality ? nullptr : "item generator not hooked");
  // The flag it sets means "socketed" only where the socket roll tests it that way.
  cheats::set_why_not(cheats::kAllSocketed, !quality                                   ? "item generator not hooked"
                                            : !sites::found(sites::kSocketRollFlags) ? "the game's socket roll not found"
                                                                                      : nullptr);
  cheats::set_why_not(cheats::kMaxAffixes, !picker ? "the game's affix picker not found"
                                           : !magic_affixes && !rare_max && !crafted_max
                                               ? "affix routines not hooked"
                                               : nullptr);
  cheats::set_why_not(cheats::kBestAffixes, pick_hook || auto_hook ? nullptr
                                            : !picker              ? "the game's affix picker not found"
                                            : !affix_rows          ? "the game's affix rows not found"
                                                                   : "affix picker not hooked");
  const char* damage_why = execute ? nullptr : "damage routine not hooked";
  cheats::set_why_not(cheats::kDamageMultiplier, damage_why);
  cheats::set_why_not(cheats::kInvincibleMerc, damage_why);
  cheats::set_why_not(cheats::kInvinciblePets, damage_why);
  const char* const passive_reason = passive_why    ? passive_why
                                     : !enemy_test  ? "enemy test not hooked"
                                     : !kept_target ? "kept target routine not hooked"
                                     : !ai_dispatch ? "AI routine not hooked"
                                                    : nullptr;
  cheats::set_why_not(cheats::kPassiveMerc, passive_reason);
  cheats::set_why_not(cheats::kPassivePets, passive_reason);
  cheats::set_why_not(cheats::kCannotBeFrozen, damage_why);
  cheats::set_why_not(cheats::kCannotBePoisoned, damage_why);
  if (execute && regen && !game::has_cure())
    logf("hooks: cannot be poisoned leaves a poison already on you to run its course (the antidote's cure not found)");
  set_items_why(g_items_why);
  cheats::set_why_not(cheats::kInfiniteKeys, !game::has_key_check() ? "the game's key search not found"
                                             : !keys                  ? "key use not hooked"
                                                                      : nullptr);
  cheats::set_why_not(cheats::kNoDurabilityLoss, wear ? nullptr : "durability routine not hooked");
  cheats::set_why_not(cheats::kCannotBeCursed, !regen                  ? "stat regeneration not hooked"
                                               : !curse_step           ? "curse routine not hooked"
                                               : !curses::has_tables() ? "the game's state table is unavailable"
                                                                       : nullptr);
  if (curse_step && regen && !game::has_cure())
    logf("hooks: cannot be cursed leaves a curse already on you to run its course (the antidote's cure not found)");
  cheats::set_why_not(cheats::kInfiniteGold, pay ? nullptr : "payment routine not hooked");
  {
    const cheats::Kind kinds[npcservice::kServices] = {cheats::kInfiniteImbue, cheats::kInfiniteSockets,
                                                       cheats::kInfinitePersonalize};
    for (int s = 0; s < npcservice::kServices; ++s)
      cheats::set_why_not(kinds[s], !regen      ? "stat regeneration not hooked"
                                    : npc_why   ? npc_why
                                    : !npc_menu ? "NPC menu routine not hooked"
                                                : npcservice::why_not(static_cast<npcservice::Service>(s)));
  }
  cheats::set_why_not(cheats::kInfiniteCubeIngredients, cube_why        ? cube_why
                                                         : !item_free     ? "item free not hooked"
                                                         : !cube_products ? "cube routine not hooked"
                                                                          : nullptr);
  cheats::set_why_not(cheats::kAutoIdentify, autoid_why    ? autoid_why
                                             : !item_notice ? "item notice not hooked"
                                                            : nullptr);
  cheats::set_why_not(cheats::kHomeTown, !regen       ? "stat regeneration not hooked"
                                         : portal_why ? portal_why
                                         : !portals   ? "portal routines not hooked"
                                                      : nullptr);
  cheats::set_why_not(cheats::kItemLevel, !item_names                ? "item name routine not hooked"
                                          : !levels_sent             ? "the item writer not found"
                                          : !itemlevel::has_tables() ? "the game's item table is unavailable"
                                                                     : nullptr);
  const char* respec_why = !regen                ? "stat regeneration not hooked"
                           : !game::has_respec() ? "the game's reset routine not found"
                                                 : nullptr;
  cheats::set_why_not(cheats::kRespec, respec_why);
  cheats::set_why_not(cheats::kPresets, respec_why                ? respec_why
                                        : !game::has_spenders()   ? "the game's point handlers not found"
                                        : !game::has_base_stat()  ? "the base stat getter not found"
                                                                  : nullptr);
  if (g_life_guarded)
    logf("hooks: god mode refuses every lowering of a player's life (the protected-stat test)%s",
         execute ? ", and takes the rest of a hit on you away"
                 : "; a hit's freeze, poison and stun still land (the damage routine is not hooked)");
  else if (execute)
    logf("hooks: god mode empties the hits on you, but life lost outside a hit (poison already on you, Blood Mana, "
         "crushing blow) is not refused: the protected-stat test is not hooked");
  return regen;
}

void uninstall() {
  npcservice::restore();  // every NPC's record as the game has it
  g_orig_regen = nullptr;
  g_orig_execute = nullptr;
  g_orig_finalize = nullptr;
  g_orig_death = nullptr;
  g_orig_mana_cost = nullptr;
  g_orig_add_exp = nullptr;
  g_orig_level_name = nullptr;
  g_orig_automap_update = nullptr;
  g_orig_automap_info = nullptr;
  g_orig_wp_populate = nullptr;
  g_orig_tc_drop = nullptr;
  g_orig_quality_step = nullptr;
  g_orig_property_roll = nullptr;
  g_orig_create_item = nullptr;
  g_orig_class_skills = nullptr;
  g_orig_superior_fits = nullptr;
  g_orig_magic_affixes = nullptr;
  g_orig_rare_affixes = nullptr;
  g_orig_crafted_affixes = nullptr;
  g_orig_pick_affix = nullptr;
  g_orig_auto_affix = nullptr;
  g_orig_should_remove = nullptr;
  g_orig_sync_quantity = nullptr;
  g_orig_use_key = nullptr;
  g_orig_item_wear = nullptr;
  g_orig_impale_wear = nullptr;
  g_orig_item_name = nullptr;
  g_orig_curse_step = nullptr;
  g_orig_cursed_mod = nullptr;
  g_orig_vendor_pay = nullptr;
  g_orig_portal_start = nullptr;
  g_orig_portal_town_end = nullptr;
  g_orig_portal_operate = nullptr;
  g_orig_wake = nullptr;
  g_orig_portal_trip = nullptr;
  g_orig_npc_menu = nullptr;
  g_orig_protected_stat = nullptr;
  g_life_guarded = false;
  g_orig_cube_products = nullptr;
  g_orig_item_free = nullptr;
  g_orig_socketed_free = nullptr;
  g_orig_item_notice = nullptr;
  g_orig_ai_dispatch = nullptr;
  g_orig_enemy_test = nullptr;
  g_orig_kept_target = nullptr;
  cube::hooked(false);
  hometown::wake_hooked(false);
  g_book_debit_return = 0;
  g_items_why = nullptr;
}

void game_left() {
  g_levels_collected = false;
  g_monsters_collected = false;
  arealevel::reset();
  itemseed::game_left();
}

unsigned life_losses_kept() { return static_cast<unsigned>(g_life_kept); }

bool exit_requested() { return g_exit != 0; }
const char* exit_reason() { return g_exit_reason; }
void clear_exit_request() { InterlockedExchange(&g_exit, 0); }

}  // namespace d2rcc::hooks
