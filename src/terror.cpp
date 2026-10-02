#include "terror.h"

#include <windows.h>

#include <time.h>

#include <cstdio>

#include "cheats.h"
#include "log.h"
#include "mem.h"
#include "sites.h"

namespace d2rcc::terror {
namespace {

using EnabledFn = bool(__fastcall*)(void* game) noexcept;
using ApplyFn = void(__fastcall*)(void* game, uintptr_t zone, uint8_t kind) noexcept;
using ClientUpdateFn = void(__fastcall*)(void* game) noexcept;
using RemoveKindFn = void(__fastcall*)(void* game, uint8_t kind) noexcept;
using CurrentZoneFn = uintptr_t(__fastcall*)(uint8_t* changed) noexcept;
using ConfigForTimeFn = uintptr_t(__fastcall*)(int64_t utc_seconds, uint8_t* changed) noexcept;

constexpr uint8_t kRotationKind = 1;  // the half-hour rotation's zone
constexpr uint8_t kManualKind = 2;    // a Worldstone Shard's

// The config record (DesecrateGetConfigForTime): the rotation's zones, and the manual zone groups, one per act
// ({int32 group id, zones, zone count}, 0x20 bytes each).
constexpr size_t kCfgZones = 0x250;
constexpr size_t kCfgZoneCount = 0x258;
constexpr size_t kCfgGroups = 0x268;
constexpr size_t kCfgGroupCount = 0x270;
constexpr size_t kGroupSize = 0x20;
constexpr size_t kGroupZones = 0x08;
constexpr size_t kGroupZoneCount = 0x10;
// A zone record: an FNV-1a hash of its id, the id, and its levels (0x38 bytes each, the level id first).
constexpr size_t kZoneSize = 1000;
constexpr size_t kZoneId = 0x04;
constexpr size_t kZoneIdLen = 0x80;
constexpr size_t kZoneLevels = 0x88;
constexpr size_t kZoneLevelCount = 0x90;
constexpr size_t kZoneLevelSize = 0x38;
// The game record: its terrorized levels ({entries, count}, 0x90 bytes each) and the ones waiting behind a level's
// other kind of terror (0x790 bytes each), both with the level id first and the kind at +0x8C, and its level runtimes
// by level id (the game skips a level without one). sites.cpp checks these in the code that walks them.
constexpr size_t kGameTerrorized = 0x5820;
constexpr size_t kGameWaiting = 0x7728;
constexpr size_t kTerrorizedSize = 0x90;
constexpr size_t kWaitingSize = 0x790;
constexpr size_t kEntryKind = 0x8C;
constexpr size_t kGameLevelRuntimes = 0x1D0;

constexpr int kMaxZones = 16;               // the zones the switch applies (3.3: five acts and the Moo Moo Farm)
constexpr int kMaxZoneLevels = 64;          // one zone's levels (3.3: 37 at most)
constexpr uint64_t kMaxRotationZones = 64;  // 3.3: 34
constexpr uint64_t kMaxEntries = 1024;      // terrorized or waiting levels in a game (a sanity bound)
constexpr int32_t kMaxLevelId = 1024;
constexpr unsigned kSettleTicks = 25;       // a new game's first second: its join applies the rotation's zone
constexpr unsigned kCheckTicks = 125;       // then a look every five seconds
constexpr int kMaxApplies = 4;              // a zone applied this often in one game and still short is left alone

EnabledFn g_enabled = nullptr;
ApplyFn g_apply = nullptr;
ClientUpdateFn g_update = nullptr;
RemoveKindFn g_remove = nullptr;
CurrentZoneFn g_current = nullptr;
ConfigForTimeFn g_config = nullptr;

volatile LONG g_generation = 0;  // game_left() moves it on

// The game the tick works on (server thread only).
struct Applied {
  uintptr_t zone;
  int times;
};
struct GameState {
  void* game = nullptr;
  LONG generation = -1;
  unsigned settled = 0;     // the tick its first second ends
  unsigned next = 0;        // the tick of the next look
  bool wanted = false;      // the switch at the last tick
  bool terrorized = false;  // the switch applied a zone in this game (switching it off takes the terror away)
  bool warned = false;      // the one warning a game gets
  Applied applied[kMaxZones] = {};
  int applied_count = 0;
};
GameState g_state;

// The panel's copy (any thread).
volatile LONG g_in_game = 0, g_checked = 0, g_has_zones = 0, g_areas = 0, g_zones = 0;

struct Zone {
  uintptr_t record = 0;
  char id[64] = {};
  int32_t levels[kMaxZoneLevels] = {};
  int count = 0;
};

bool read_zone(uintptr_t record, Zone* z) {
  char id[kZoneIdLen];
  uint64_t levels = 0, count = 0;
  if (!mem::copy_from(id, record + kZoneId, sizeof(id)) || !mem::read_safe(record + kZoneLevels, &levels) ||
      !mem::read_safe(record + kZoneLevelCount, &count) || !levels || !count ||
      count > static_cast<uint64_t>(kMaxZoneLevels))
    return false;
  id[sizeof(id) - 1] = 0;
  z->record = record;
  std::snprintf(z->id, sizeof(z->id), "%s", id);
  z->count = 0;
  for (uint64_t i = 0; i < count; ++i) {
    int32_t level = 0;
    if (!mem::read_safe(levels + i * kZoneLevelSize, &level) || level <= 0 || level >= kMaxLevelId) return false;
    z->levels[z->count++] = level;
  }
  return z->id[0] != 0;
}

bool covered(const Zone* zones, int n, int32_t level) {
  for (int k = 0; k < n; ++k)
    for (int j = 0; j < zones[k].count; ++j)
      if (zones[k].levels[j] == level) return true;
  return false;
}

// What the switch applies: every act's manual zone, then each rotation zone with a level none of those covers.
int collect(uintptr_t cfg, Zone* out) {
  int n = 0;
  uint64_t groups = 0, group_count = 0;
  if (mem::read_safe(cfg + kCfgGroups, &groups) && mem::read_safe(cfg + kCfgGroupCount, &group_count) && groups &&
      group_count <= static_cast<uint64_t>(kMaxZones)) {
    for (uint64_t g = 0; g < group_count; ++g) {
      const uintptr_t group = groups + g * kGroupSize;
      uint64_t zones = 0, zone_count = 0;
      if (!mem::read_safe(group + kGroupZones, &zones) || !mem::read_safe(group + kGroupZoneCount, &zone_count) ||
          !zones || zone_count > static_cast<uint64_t>(kMaxZones))
        continue;
      for (uint64_t z = 0; z < zone_count && n < kMaxZones; ++z)
        if (read_zone(zones + z * kZoneSize, &out[n])) ++n;
    }
  }
  uint64_t zones = 0, zone_count = 0;
  if (!mem::read_safe(cfg + kCfgZones, &zones) || !mem::read_safe(cfg + kCfgZoneCount, &zone_count) || !zones ||
      zone_count > kMaxRotationZones)
    return n;
  for (uint64_t z = 0; z < zone_count && n < kMaxZones; ++z) {
    if (!read_zone(zones + z * kZoneSize, &out[n])) continue;
    for (int i = 0; i < out[n].count; ++i)
      if (!covered(out, n, out[n].levels[i])) {
        ++n;
        break;
      }
  }
  return n;
}

// The game's terror lists and its level runtimes.
struct Lists {
  uintptr_t terrorized = 0, waiting = 0, runtimes = 0;
  uint64_t terrorized_count = 0, waiting_count = 0;
};

bool read_lists(void* game, Lists* l) {
  const uintptr_t g = reinterpret_cast<uintptr_t>(game);
  return mem::read_safe(g + kGameTerrorized, &l->terrorized) &&
         mem::read_safe(g + kGameTerrorized + 8, &l->terrorized_count) &&
         mem::read_safe(g + kGameWaiting, &l->waiting) && mem::read_safe(g + kGameWaiting + 8, &l->waiting_count) &&
         mem::read_safe(g + kGameLevelRuntimes, &l->runtimes) && l->terrorized_count <= kMaxEntries &&
         l->waiting_count <= kMaxEntries && (l->terrorized || !l->terrorized_count) &&
         (l->waiting || !l->waiting_count);
}

bool listed(uintptr_t entries, uint64_t count, size_t size, int32_t level, uint8_t kind) {
  for (uint64_t i = 0; i < count; ++i) {
    const uintptr_t e = entries + i * size;
    int32_t id = 0;
    uint8_t k = 0;
    if (mem::read_safe(e, &id) && id == level && mem::read_safe(e + kEntryKind, &k) && k == kind) return true;
  }
  return false;
}

// The zone's levels that could have manual terror in this game and have none, terrorized or waiting.
int short_of(const Lists& l, const Zone& z) {
  int n = 0;
  for (int i = 0; i < z.count; ++i) {
    uintptr_t runtime = 0;
    if (!l.runtimes || !mem::read_safe(l.runtimes + static_cast<uintptr_t>(z.levels[i]) * 8, &runtime) || !runtime)
      continue;
    if (!listed(l.terrorized, l.terrorized_count, kTerrorizedSize, z.levels[i], kManualKind) &&
        !listed(l.waiting, l.waiting_count, kWaitingSize, z.levels[i], kManualKind))
      ++n;
  }
  return n;
}

Applied* applied_entry(uintptr_t zone) {
  for (int i = 0; i < g_state.applied_count; ++i)
    if (g_state.applied[i].zone == zone) return &g_state.applied[i];
  if (g_state.applied_count == kMaxZones) return nullptr;
  g_state.applied[g_state.applied_count] = Applied{zone, 0};
  return &g_state.applied[g_state.applied_count++];
}

// A look while the switch is on: each zone short of manual terror is applied the way a shard applies its act's,
// the clients told after each (as after each shard).
void look(void* game) {
  const bool has = g_enabled(game);
  InterlockedExchange(&g_checked, 1);
  InterlockedExchange(&g_has_zones, has ? 1 : 0);
  if (!has) {
    if (!g_state.warned) logf("all areas terrorized: this game has no terror zones - nothing applied");
    g_state.warned = true;
    return;
  }
  const uintptr_t cfg = g_config(static_cast<int64_t>(_time64(nullptr)), nullptr);
  Zone zones[kMaxZones];
  const int n = cfg ? collect(cfg, zones) : 0;
  Lists lists;
  if (!n || !read_lists(game, &lists)) {
    if (!g_state.warned)
      log_warn("all areas terrorized: %s", n ? "the game's terror lists cannot be read" : "the game's terror zone "
                                                                                          "config has no zones");
    g_state.warned = true;
    return;
  }
  int applied = 0;
  for (int i = 0; i < n; ++i) {
    const int missing = short_of(lists, zones[i]);
    Applied* a = missing ? applied_entry(zones[i].record) : nullptr;
    if (!a || a->times > kMaxApplies) continue;
    if (a->times == kMaxApplies) {
      log_warn("all areas terrorized: %s still has %d area(s) without manual terror after %d applies - left alone in "
               "this game", zones[i].id, missing, kMaxApplies);
      ++a->times;
      continue;
    }
    g_apply(game, zones[i].record, kManualKind);
    g_update(game);
    g_state.terrorized = true;
    ++applied;
    if (++a->times == 1)
      logf("all areas terrorized: %s applied as manual terror (%d area%s)", zones[i].id, zones[i].count,
           zones[i].count == 1 ? "" : "s");
    else
      logf("all areas terrorized: %s applied again - %d of its areas had no manual terror", zones[i].id, missing);
    if (!read_lists(game, &lists)) break;
  }
  int zones_applied = 0;
  for (int i = 0; i < g_state.applied_count; ++i)
    if (g_state.applied[i].times > 0) ++zones_applied;
  InterlockedExchange(&g_zones, zones_applied);
  InterlockedExchange(&g_areas, static_cast<LONG>(lists.terrorized_count));
  if (applied) {
    logf("all areas terrorized: %d areas terrorized in this game now", static_cast<int>(lists.terrorized_count));
    cheats::note("All areas terrorized: %d areas in this game", static_cast<int>(lists.terrorized_count));
  }
}

// The switch went off in a game it terrorized: the manual terror comes off (a rotation level waiting behind it takes
// over), then the rotation's zone is applied again and the clients are told - what a rotation does with its own kind,
// which also puts D2RCore's Herald bookkeeping (rebuilt on an apply) in step.
void take_off(void* game) {
  g_remove(game, kManualKind);
  const uintptr_t zone = g_enabled(game) ? g_current(nullptr) : 0;
  if (zone) g_apply(game, zone, kRotationKind);
  g_update(game);
  Lists lists;
  if (read_lists(game, &lists)) InterlockedExchange(&g_areas, static_cast<LONG>(lists.terrorized_count));
  InterlockedExchange(&g_zones, 0);
  logf("all areas terrorized: off - the manual terror is taken off this game%s",
       zone ? ", the rotation's zone applied again" : "");
  cheats::note("All areas terrorized: off in this game");
}

}  // namespace

const char* bind() {
  g_enabled = reinterpret_cast<EnabledFn>(sites::derived(sites::dTerrorEnabled));
  g_apply = reinterpret_cast<ApplyFn>(sites::derived(sites::dTerrorApply));
  g_update = reinterpret_cast<ClientUpdateFn>(sites::derived(sites::dTerrorClientUpdate));
  g_remove = reinterpret_cast<RemoveKindFn>(sites::derived(sites::dTerrorRemoveKind));
  g_current = reinterpret_cast<CurrentZoneFn>(sites::address(sites::kDesecrateCurrentZone));
  g_config = reinterpret_cast<ConfigForTimeFn>(sites::address(sites::kDesecrateConfigForTime));
  if (!g_enabled || !g_apply || !g_update || !g_remove || !g_current || !g_config) {
    g_apply = nullptr;  // the tick does nothing
    return "terror zone routines not found";
  }
  const uintptr_t base = sites::exe_base();
  logf("terror: all areas terrorized bound (a shard's zone apply 0x%llX, the rotation's removal 0x%llX)",
       static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_apply) - base),
       static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_remove) - base));
  return nullptr;
}

void on_tick(void* game, unsigned tick) {
  if (!g_apply || !game) return;
  const LONG generation = g_generation;
  if (game != g_state.game || generation != g_state.generation) {
    g_state = GameState{};
    g_state.game = game;
    g_state.generation = generation;
    g_state.settled = tick + kSettleTicks;
    g_state.next = g_state.settled;
    InterlockedExchange(&g_checked, 0);
    InterlockedExchange(&g_zones, 0);
  }
  InterlockedExchange(&g_in_game, 1);
  const bool want = cheats::enabled(cheats::kTerrorAll) && !cheats::why_not(cheats::kTerrorAll);
  if (want != g_state.wanted) {
    g_state.wanted = want;
    if (want) {
      // Switched on: a look at once, but not in a new game's first second.
      g_state.next = static_cast<int>(tick - g_state.settled) < 0 ? g_state.settled : tick;
      g_state.warned = false;
    } else if (g_state.terrorized) {
      take_off(game);
      g_state.terrorized = false;
      g_state.applied_count = 0;
    }
  }
  if (static_cast<int>(tick - g_state.next) < 0) return;
  g_state.next = tick + kCheckTicks;
  if (want) {
    look(game);
  } else {
    Lists lists;
    if (read_lists(game, &lists)) InterlockedExchange(&g_areas, static_cast<LONG>(lists.terrorized_count));
  }
}

void game_left() {
  InterlockedIncrement(&g_generation);
  InterlockedExchange(&g_in_game, 0);
  InterlockedExchange(&g_checked, 0);
  InterlockedExchange(&g_has_zones, 0);
  InterlockedExchange(&g_areas, 0);
  InterlockedExchange(&g_zones, 0);
}

Stats stats() {
  Stats s;
  s.in_game = g_in_game != 0;
  s.checked = g_checked != 0;
  s.has_zones = g_has_zones != 0;
  s.areas = static_cast<int>(g_areas);
  s.zones = static_cast<int>(g_zones);
  return s;
}

}  // namespace d2rcc::terror
