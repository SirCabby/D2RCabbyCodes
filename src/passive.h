#pragma once

#include <cstdint>

namespace d2rcc::game {
struct Unit;
}

// Passive mercenary and minions: your pets follow you and fight nothing. What the game does (3.3): every monster's AI,
// the mercenary's and every summon's included, runs through one dispatcher once per AI tick, on the server thread. For
// the AIs that take one it finds a target first; then it runs the AI's own routine (the mercenary's; the golems',
// skeletons', mages' and Valkyrie's; the Druid's, the Assassin's, the hydra's; the Warlock's demons'), which looks for
// targets itself. Every way a pet finds a target asks one routine whether a unit is an enemy: the unit searches
// through their filters (the AI's own two enemy checks end in a jump to it), "what my owner fights", the Warlock
// demons' scoring of the units near them. The one other way is a target kept on the monster, read through one getter:
// Attract and Confuse set it on hostile monsters only; on a pet, the Warlock's demons keep their chosen target there
// from one tick to the next, and a summoning skill or an AI mode's start can set one.
// Under a switch (the mercenary's, or the minions' for every other monster the local player owns), the dispatcher's
// hook marks the thread for the length of that pet's AI tick, and while the mark is up the enemy test answers "not an
// enemy" and the kept target reads as none. The pet's own AI then finds nothing to fight and does what it does
// without an enemy: it follows you, buffs, eats corpses. Nothing is written, and no routine of the game's is called but
// the hooked ones' originals. Hostile monsters' AI ticks, and everything outside an AI tick, are the game's: your own
// skills and curses, a pet's auras (Holy Freeze still chills, a Fire Golem's Holy Fire still burns), the hits it takes
// (it does not hit back), an attack it had begun.
namespace d2rcc::passive {

using game::Unit;

// Which monsters are kept passive: one the local player owns, the mercenary under its switch, any other (a summon, a
// revive, a trap, a hydra, a Warlock's demon) under the minions' switch.
constexpr bool keeps(bool owned, bool mercenary, bool merc_on, bool minions_on) {
  return owned && (mercenary ? merc_on : minions_on);
}

// The thread's mark: up for the length of a kept pet's AI tick, with what its questions were answered.
struct Mark {
  bool up = false;
  unsigned refused = 0;     // enemy tests the game answered "an enemy", answered "not an enemy"
  unsigned kept = 0;        // kept targets left aside
  uint32_t kept_kind = 0;   // ... the last one's kind (1 a player, 2 a monster, 3 a unit near (Confuse), 4 a missile)
};

// Runs tick with the mark up (a kept pet's AI tick) or down (any other monster's), and the mark as it was put back
// after: an AI tick inside another, should the game run one, has its own. Answers what was counted during tick.
template <typename Tick>
Mark marked(Mark& mark, bool up, Tick&& tick) {
  const Mark before = mark;
  mark = Mark{};
  mark.up = up;
  tick();
  const Mark during = mark;
  mark = before;
  return during;
}

// An enemy test asked under the mark: the game's answer (a callers' eax: nonzero when the other unit is an enemy), but
// "not an enemy" while the mark is up.
inline uint64_t answer(Mark& mark, uint64_t game_answer) {
  if (!mark.up || static_cast<uint32_t>(game_answer) == 0) return game_answer;
  ++mark.refused;
  return 0;
}

// ... and the kind of the target kept on the monster: the game's, but none while the mark is up.
inline uint64_t kept_kind(Mark& mark, uint64_t game_kind) {
  if (!mark.up || static_cast<uint32_t>(game_kind) == 0) return game_kind;
  ++mark.kept;
  mark.kept_kind = static_cast<uint32_t>(game_kind);
  return 0;
}

// The routines hooked: the AI dispatcher (game, monster; its callers pass three zeros after them, handed on as they
// came), the enemy test (game, unit, other unit, the flag its first entry stub sets in r9b), and the getter of the
// kept target's kind (monster).
using DispatchFn = void(__fastcall*)(void* game, Unit* unit, uint64_t a3, uint64_t a4, uint64_t a5) noexcept;
using EnemyTestFn = uint64_t(__fastcall*)(void* game, Unit* unit, Unit* other, uint64_t flag) noexcept;
using KeptTargetFn = uint64_t(__fastcall*)(Unit* monster) noexcept;

// After sites::derive(): null when the switches can work, else why not.
const char* bind();

// Server thread, the hooks: a monster's AI tick (marked for a pet kept passive), and the two questions answered under
// the mark.
void think(DispatchFn original, void* game, Unit* unit, uint64_t a3, uint64_t a4, uint64_t a5);
uint64_t enemy(EnemyTestFn original, void* game, Unit* unit, Unit* other, uint64_t flag);
uint64_t kept_target(KeptTargetFn original, Unit* monster);

// For the console (any thread).
struct Stats {
  unsigned ticks = 0;    // this session: AI ticks of pets kept passive
  unsigned held = 0;     // ... in which the pet had an enemy to take on and left it
  unsigned refused = 0;  // enemy tests answered "not an enemy"
  unsigned kept = 0;     // kept targets left aside
};
Stats stats();

}  // namespace d2rcc::passive
