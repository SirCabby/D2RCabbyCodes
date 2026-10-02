#pragma once

#include <cstdint>

// Movement speed: the bonus is put in the player's base velocitypercent (stat 67), so what the game adds itself (a
// skill's or a state's velocitypercent, gear's faster run/walk) stacks on top and comes and goes as usual. The game
// sets that base to 100 as it makes a character's unit for a game, nothing else of the game's sets a player's: a new
// character's stats (0x52D770), a saved character's (the .d2s reader 0x420280, and 0x5311F0 on the load D2RCore
// runs). So in every game the player's unit starts at 100, without the bonus, and offline it has the id 1 in every
// game.
// The tick keeps what it wrote last: a base that is not that (another unit, or the same unit's base set anew) is the
// game's own, without the bonus, and the bonus goes on top of it again. Lowered, the bonus comes off the game's own
// base, never off what the tick only believes it wrote. (Until 2026-10-02 the player's id said whether the unit had
// the bonus: in the next game of a session it was not put back, and a bonus lowered then took off what was not there,
// down to a base of 0, slower than the game's own speed.)
namespace d2rcc::movespeed {

// What target() found the base to be.
enum class Found : uint8_t {
  kWritten = 0,  // the base the tick left there
  kNewUnit,      // another unit than the one written to: a game's character (the session's first one too)
  kChanged,      // the same unit with a base the tick did not write: set anew by the game, or by someone else
};

struct State {
  const void* unit = nullptr;  // the player's unit the tick wrote to last (compared, never read)
  int32_t written = 0;         // its base as it read after that write
  int32_t own = 0;             // the game's own base under the bonus
  int bonus = 0;               // the bonus in the base
  Found found = Found::kWritten;
};

// The base the player's unit is to have, from its base as it reads now and the bonus asked for: `base` itself when
// nothing is to be written. The state is brought up to date; after a write the tick hands back the base as it then
// reads (wrote), so that a write the game held back or clamped is not taken for one the game undid.
constexpr int32_t target(State& s, const void* unit, int32_t base, int bonus) {
  s.found = Found::kWritten;
  if (unit != s.unit || base != s.written) {
    s.found = unit != s.unit ? Found::kNewUnit : Found::kChanged;
    s.unit = unit;
    s.written = s.own = base;
    s.bonus = 0;
  }
  if (bonus == s.bonus) return base;
  s.bonus = bonus;
  return s.own + bonus;
}

constexpr void wrote(State& s, int32_t base) { s.written = base; }

}  // namespace d2rcc::movespeed
