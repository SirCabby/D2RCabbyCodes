#pragma once

#include <cstddef>
#include <cstdint>

namespace d2rcc::game {
struct Unit;
}

// Ignore item requirements: a level 1 character with no attribute points spent can wear and use any item its class can,
// and, under its own switch, the mercenary any item it can. What the game does (3.3): every test of whether a unit can
// use an item goes through one routine, on both sides of the game. The client asks it before it puts an item on (the
// character's or the mercenary's), to draw an item's background red and the tooltip's requirement lines red; the server
// before it lets an item be put on, and when it works out what the worn items and the charms carried give (an item whose
// requirements are not met gives nothing); D2RCore's tooltip, charm and slot checks call it too. It asks level, strength
// and dexterity first (the item's own, raised by what is in its sockets and by a runeword, an ethereal item's 10 less),
// then, in this order, whether the item is identified, a tome's quantity, 3.3's restricted sockets (two worn items with
// a Colossal Jewel each) and the item type's class (orbs, claws, Paladin shields and the like: one class's own).
// The hook (any thread) runs the game's test, and for a unit whose switch is on - the local player (the server's unit or
// the client's copy), or its mercenary (the server's, whose AI record names the local player as its owner, or the
// client's, the local player's pet of the mercenary's type in the client's pet list) - takes level, strength and
// dexterity as met: the three flags the test hands back say so, and when the game's answer was no and one of the three
// was not met, the answer is that of the tests after them, asked the way the routine asks them: the identified flag,
// the tome's quantity, the restricted-socket test itself (the game's), the class against the item type's row (a
// player's own; a hireling's by its Hireling row). Nothing is written. When a switch changes in a game, the tick has the
// game work that unit's items out again (its own routine, the cube's refresh), so what is worn counts, or stops
// counting, at once; a game's load works them out by itself.
namespace d2rcc::requirements {

using game::Unit;

// The tests after level, strength and dexterity, as the hook asks them again.
struct Later {
  bool identified = false;   // the item's identified flag
  bool empty_tome = false;   // a tome with a quantity below 1
  bool restricted = false;   // the restricted-socket test refuses
  bool class_ok = true;      // the item type's class lets the unit use the item
};

// What the restricted-socket test comes to, as the routine takes it: at the body location it was asked about, or, asked
// about none, at both of the item type's (one that does not refuse is enough).
constexpr bool restricted(bool asked, bool at_asked, bool at_first, bool at_second) {
  return asked ? at_asked : at_first && at_second;
}

// The class test, as the routine makes it: a type whose class is below `none` (or a type with no row: no class) is for
// that class alone. A player must be of it. A monster passes when it is no hireling (no Hireling row for its class, or
// one whose kind is 0); a hireling must have the mercenary flag and the class its row lets it use (below 0: none).
// Any other unit fails.
enum class Who : int { kPlayer, kMonster, kOther };
constexpr bool class_ok(int type_class, int none, Who who, int unit_class, bool hireling, bool mercenary_flag,
                        int hireling_class) {
  if (type_class < 0 || type_class >= none) return true;
  if (who == Who::kPlayer) return unit_class == type_class;
  if (who != Who::kMonster) return false;
  if (!hireling) return true;
  return (mercenary_flag && hireling_class >= 0 ? hireling_class : none) == type_class;
}

// What the hook answers: the game's answer, unless the unit's switch is on and the item is one the test reads. Then a
// yes stays a yes; a no with level, strength and dexterity all met was a later test's and stays; any other no is what
// the later tests say together.
constexpr bool answer(bool ignoring, bool game_said, bool strength, bool dexterity, bool level, const Later& later) {
  if (!ignoring || game_said) return game_said;
  if (strength && dexterity && level) return false;
  return later.identified && !later.empty_tome && !later.restricted && later.class_ok;
}

// The game's requirement test (item, unit, equipping: the item's own strength and dexterity do not count toward its
// requirements, three flags it hands back - strength, dexterity, level met - and the body location asked about, 0 none)
// -> nonzero when the unit can use the item.
using TestFn = int32_t(__fastcall*)(Unit* item, Unit* unit, int32_t equipping, int32_t* strength, int32_t* dexterity,
                                    int32_t* level, int32_t body_location) noexcept;

// After sites::derive() and game::bind(): why each switch cannot work, null when it can (the mercenary's needs what
// the character's does, and its client copy found).
void bind();
const char* why_not_player();
const char* why_not_mercenary();

// Any thread, the hook: the game's requirement test.
int32_t test(TestFn original, Unit* item, Unit* unit, int32_t equipping, int32_t* strength, int32_t* dexterity,
             int32_t* level, int32_t body_location);

// Server thread, the tick: a switch changed in this game -> that unit's items worked out again (a dead character's once
// it is alive; a mercenary only while there is one, found by the game's own lookup).
void on_tick(void* game, Unit* player);
void game_left();  // any thread: the next game's load works its items out with the switches as they are then

// For the console (any thread).
struct Stats {
  unsigned player_yes = 0;     // the test's no turned into a yes for your character (each time it was asked)
  unsigned mercenary_yes = 0;  // ... for your mercenary
  unsigned refreshes = 0;      // the items worked out again after a switch changed
};
Stats stats();

}  // namespace d2rcc::requirements
