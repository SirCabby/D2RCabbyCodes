#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

namespace d2rcc::game {
struct Unit;
}

// Enemies without immunities, and without affixes (server thread, both).
//
// Immunities. What the game does (3.3): a hit's damage goes through one resistance step per damage type (0x4523E0,
// which D2RCore's wrapper calls first): the defender's resistance to that type (the total: base, items, states,
// curses), then the routine hooked here (0x44F6D0), whose answer the attacker's pierce is taken from (only below 100
// for a monster: an immunity cannot be pierced), then the cap (none for a monster) and the damage taken off: none at
// all at 100 or more. Chill, freeze, poison and burn lengths are damage types of their own with the element's
// resistance, so an immune monster is never chilled or poisoned either.
// Under the switch, for a monster that is not yours (nor your mercenary), a resistance that is an immunity (the
// total at 100 or more, or the monster's own base at 100 or more, which a curse may have brought below) counts as
// none: the answer is 0, or what a curse or Conviction took it below 0 to. The pierce then applies as on any monster.
// Nothing of the server's is written. The hover's "Immune to ..." is the client's own: it works out its copy of a
// monster's resistances itself (its monster init and class change set them from the MonStats row of the difficulty,
// its mod handlers add the resistance affixes'), then its mod loop runs (0x1DF980, also at packet 0x57). That loop is
// hooked on the client (UI thread): after it, for a monster that is not yours (not the mercenary, not in the client's
// pet list as the local player's), under the switch, a resistance of 100 or more in the client's copy becomes 0, as the
// server now takes it (the one write: those six stats of the client's copy, which the server never reads). A monster
// the client already has keeps its hover line until the client makes it again.
//
// Affixes. The affixes are the mods the game rolls for uniques: the MonUMod rows with a unique pick weight (3.3: Extra
// Strong, Extra Fast, Cursed, Magic Resistant, Fire, Lightning and Cold Enchanted, Mana Burn, Teleportation, Spectral
// Hit, Stone Skin, Multiple Shots, Aura Enchanted). A monster gets them three ways:
// - rolled: D2RCore rolls a random unique's into its spawn plan as it populates a room, the game rolls a super unique's
//   extra ones in Nightmare and Hell and a terror zone's extra one; every roller asks one test of each candidate
//   (0x4A1E50, "may the monster take the mod"), and picks nothing when none is left. A pack maker copies the plan into
//   the leader, the minion maker into each minion;
// - written in: a super unique's own (its SuperUniques row's three), by its maker, with no test; every maker that
//   writes the 9 mods itself then runs their makers through one routine (0x49E860);
// - added one at a time (0x4995E0): the quest monsters', the skills' (killself, a summon's own), D2RCore's.
// Under the switch, for a monster that is not yours: the test says no to an affix, the routine that runs the makers
// first takes the affixes out of the monster's list (the one write: that list, the rest kept in their order), and the
// add is not run for one. So no affix is in the monster's list, its maker never runs, the client never names it. A
// champion's type stays. Monsters made before the switch went on keep theirs.
// A list is never left empty when it had affixes, or a roller was refused one for the monster: the game tells the
// client what kind a monster is (unique, super unique, minion, Herald) only when its first mod is not 0 (0x49AAE0, the
// one test of the monster packet), and the client names a unique only when its list has a mod (its mod loop 0x1DF980
// returns at once for an empty one). Every unique the game makes has one; without it a unique reached the client as
// a plain monster (reported 2026-10-06: "some uniques were just removed. The map cheat shows them there but I didn't
// see them appear"). Such a list keeps one mod that does nothing: MonUMod 13 "rage", which has no server maker, no
// client handler, no event routine and no affix text, and which 3.3's data gives to no monster (its row holds the
// unique attack rating constant, read by row).
namespace d2rcc::enemies {

using game::Unit;

constexpr int32_t kImmune = 100;  // a monster at this resistance takes nothing of the type

// The resistance a hit goes by: the game's answer, or, for an immunity under the switch, at most 0.
constexpr int32_t resistance(bool on, bool enemy, int32_t total, int32_t base, int32_t answer) {
  return on && enemy && (total >= kImmune || base >= kImmune) && answer > 0 ? 0 : answer;
}

// The resistances the client keeps of a monster and its hover reads: damage (physical), magic, fire, lightning,
// cold, poison.
constexpr int kResistStats[6] = {36, 37, 39, 41, 43, 45};

// What the client's copy of an enemy's resistance becomes under the switch (an immunity counts as none).
constexpr int32_t displayed(bool on, bool enemy, int32_t base) { return on && enemy && base >= kImmune ? 0 : base; }

// A MonUMod row (the game's, 32 bytes): +0x00 its id, +0x06 enabled, +0x14 / +0x16 / +0x18 its pick weight for a
// unique in Normal, Nightmare, Hell (u16).
constexpr uint32_t kMonUModRowSize = 32;
constexpr size_t kMonUModEnabled = 0x06;
constexpr size_t kMonUModUniquePick = 0x14;
constexpr int kMaxMods = 64;  // 3.3 has 45

// An affix: an enabled mod the game picks for uniques in some difficulty.
inline bool row_is_affix(const uint8_t* row) {
  if (!row || !row[kMonUModEnabled]) return false;
  for (int d = 0; d < 3; ++d) {
    const size_t at = kMonUModUniquePick + 2 * static_cast<size_t>(d);
    if ((row[at] | row[at + 1]) != 0) return true;
  }
  return false;
}

constexpr uint8_t kPlaceholderMod = 13;  // MonUMod "rage": does nothing on either side (above)

// After the strip: a list left empty keeps the placeholder when it had affixes or a roller was refused one for the
// monster (a monster the game itself makes with no mods, such as the Ancients in Normal, stays as the game made it).
inline bool keep_placeholder(uint8_t mods[9], int taken, bool refused) {
  if (mods[0] != 0 || (taken <= 0 && !refused)) return false;
  mods[0] = kPlaceholderMod;
  return true;
}

// Whether the mod is not added: under the switch, an affix, to a monster that is not yours.
constexpr bool skips(bool on, bool enemy, bool affix) { return on && enemy && affix; }

// A monster's mods (9 bytes, a 0 ends them) with the affixes taken out, the others kept in their order; how many were.
inline int strip_affixes(uint8_t mods[9], uint64_t affix_bits) {
  uint8_t kept[9] = {};
  int n = 0, taken = 0;
  for (int i = 0; i < 9 && mods[i]; ++i) {
    if (mods[i] < kMaxMods && ((affix_bits >> mods[i]) & 1)) ++taken;
    else kept[n++] = mods[i];
  }
  for (int i = 0; i < 9; ++i) mods[i] = kept[i];
  return taken;
}

// The resistance routine (context, damage type, the defender's resistance) -> the resistance the hit goes by.
using AdjustFn = int32_t(__fastcall*)(void* context, void* damage_type, int32_t resistance) noexcept;
// The mod add (game, monster, mod, counted as unique).
using ModAddFn = void(__fastcall*)(void* game, Unit* monster, int32_t mod, int32_t unique) noexcept;
// The test (monster, MonUMod row, expansion) -> nonzero when the monster may take the mod.
using ModTestFn = int32_t(__fastcall*)(Unit* monster, const uint8_t* row, int32_t expansion) noexcept;
// The makers of a monster's mods run (monster).
using ModsRunFn = void(__fastcall*)(Unit* monster) noexcept;
// The client's handlers of a monster's mods run (monster).
using ClientModsFn = void(__fastcall*)(Unit* monster) noexcept;

void set_services(const D2RL::PluginContext* ctx, const D2RL::DataTableService* tables);
bool has_tables();

// After sites::derive(): null when the switch can work, else why not.
const char* bind_immunities();
const char* bind_affixes();
const char* bind_display();  // the hover: null when the client's copy can be lowered, else why not

// Server thread, the hooks.
int32_t adjust(AdjustFn original, void* context, void* damage_type, int32_t resistance);
void mod_add(ModAddFn original, void* game, Unit* monster, int32_t mod, int32_t unique);
int32_t mod_test(ModTestFn original, Unit* monster, const uint8_t* row, int32_t expansion);
void mods_run(ModsRunFn original, Unit* monster);

// Client, UI thread, the hook: a monster's mods' handlers have run; its resistances are what the client keeps.
void client_mods_run(ClientModsFn original, Unit* monster);

// For the console (any thread).
struct Stats {
  unsigned immunities = 0;  // this session: hits on an immune monster that went through
  unsigned refused = 0;     // ... monsters a roller was refused affixes for
  unsigned affixes = 0;     // ... affixes taken out of a list or not added
  unsigned placeholders = 0;  // ... monsters that kept the placeholder in a list left empty
  unsigned shown = 0;       // ... monsters whose client copy had an immunity lowered (the hover lists none)
};
Stats stats();

}  // namespace d2rcc::enemies
