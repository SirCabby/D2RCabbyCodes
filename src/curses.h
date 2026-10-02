#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace d2rcc::game {
struct Unit;
}

// Cannot be cursed. A curse is a state with its stats, put on a unit for a time by one routine of the game's
// that D2RCore replaced at its entry, so it is reached through its callers: the two steps that put a monster's
// curse on one unit (hooks_game.cpp hooks them and asks here) - a curse skill's step (the necromancer's curses,
// Defense Curse, Blood Mana, the Oblivion Knights' random curse) and the Cursed monster modifier's Amplify Damage.
// A curse that is on the player all the same (it was there when the switch went on, or came another way) is
// ended by the tick the way an antidote potion ends a poison.
//
// Which states are curses comes from the game's States table (through the loader, on the game thread). Its
// "curse" flag alone will not do: the game gives it to every state that takes another's place, a shrine's bonus
// among them (a curse ends a shrine's bonus and a shrine ends a curse). A curse to keep off is a state with that
// flag that can also be cured: in 3.3 Amplify Damage, Weaken, Dim Vision, Taunt, Iron Maiden, Terror, Attract,
// Life Tap, Confuse, Decrepify, Lower Resist, Defense Curse and Blood Mana.
namespace d2rcc::curses {

using game::Unit;

// The compiled rows read here. A States row: its flags, a bit per column of states.txt (the game's own order).
constexpr uint32_t kStatesRowSize = 0x44;
constexpr size_t kStatesFlags = 0x10;
constexpr uint32_t kFlagCurse = 1u << 12;    // takes the place of the others that have it: curses, shrine bonuses
constexpr uint32_t kFlagCurable = 1u << 13;  // can be cured
constexpr uint32_t kSkillsRowSize = 0x2EC;
constexpr int kMaxStates = 512;   // 3.3 has 232
constexpr int kMaxCurses = 64;    // ... 13 of them curses

constexpr bool keeps_off(uint32_t flags) { return (flags & kFlagCurse) != 0 && (flags & kFlagCurable) != 0; }

// The curses of a States table, in the order of their ids.
struct Curses {
  int states[kMaxCurses] = {};
  int count = 0;
  bool has(int state) const {
    for (int i = 0; i < count; ++i)
      if (states[i] == state) return true;
    return false;
  }
};

inline uint32_t state_flags(const uint8_t* rows, uint32_t row) {
  uint32_t flags = 0;
  std::memcpy(&flags, rows + static_cast<size_t>(row) * kStatesRowSize + kStatesFlags, sizeof(flags));
  return flags;
}

// The flags are where they are taken to be: three states every game has say so - freeze (1) and poison (2) can
// be cured and are no curses, Amplify Damage (9) is a curse that can be.
inline bool rows_as_expected(const uint8_t* rows, uint32_t count) {
  if (!rows || count < 10) return false;
  const uint32_t freeze = state_flags(rows, 1), poison = state_flags(rows, 2), amplify = state_flags(rows, 9);
  return (freeze & kFlagCurable) && !(freeze & kFlagCurse) && (poison & kFlagCurable) && !(poison & kFlagCurse) &&
         keeps_off(amplify);
}

// From a copy of the rows. False when the rows are not what they are taken to be, or hold more curses than fit.
inline bool curses_of(const uint8_t* rows, uint32_t count, Curses* out) {
  *out = Curses{};
  if (!rows_as_expected(rows, count)) return false;
  for (uint32_t i = 0; i < count && i < static_cast<uint32_t>(kMaxStates); ++i) {
    if (!keeps_off(state_flags(rows, i))) continue;
    if (out->count == kMaxCurses) return false;
    out->states[out->count++] = static_cast<int>(i);
  }
  return out->count > 0;
}

void set_services(const D2RL::PluginContext* ctx, const D2RL::DataTableService* tables);
bool has_tables();

// Game thread (the tick): the States rows of the three table banks, and in each the state the Cursed monster
// modifier's skill puts on. Once is enough; a bank that could not be read is tried again at the next call.
void collect();
bool ready(uint8_t bank);  // the bank (a unit's +0x1BD, 1 classic .. 3 rotw) is read

bool is_curse(uint8_t bank, int state);   // any thread
int cursed_modifier_state(uint8_t bank);  // the state of the Cursed modifier's skill, -1 when not known

// Server thread, the curse hooks: the curse is not put on (the unit is the local player, under the switch).
bool keep_off(Unit* target, int state);
// Server thread, the tick: every curse on the player ended. How many were on.
int end_curses(void* game, Unit* player);

unsigned kept_off();  // this session (the counters the panel and the console show)
unsigned ended();

}  // namespace d2rcc::curses
