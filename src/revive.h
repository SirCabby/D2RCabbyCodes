#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

namespace d2rcc::game {
struct Unit;
}

// Permanent revives: the Necromancer's revived monsters stay until they die. What the game does (3.3): Revive gives the
// monster it raises the killself monster mod, and a game event at the end of its time (the skill's calc2: 4500 frames,
// 180 s). The event runs the mod's timer on the server thread: a monster with the uninterruptable state has the event
// again 3 frames on; a pet whose owner is a player is taken out of its owner's pet list and killed; any other monster
// is put in its death mode. The same timer ends the Amazon's Decoy, the monsters an item's Reanimate As raises (Tomb
// Reaver, Faith) and some monsters the game spawns for a while.
// Under the switch the timer's hook does not run it for a revive of the local player's: a monster it owns that is in
// its pet list of Revive's pet type (the byte of Revive's Skills row the game registers its revives under), asked of
// the game's own pet lookup, the first question of the timer's pet removal. The event is used up, so the revive has no
// timer left: it stays until it dies, the game ends, or the game's own limit of revives (one a skill level) makes room
// for a new one (the oldest falls). Nothing is written. Switched off, revives raised from then on time out again; the
// ones it kept stay until they die.
namespace d2rcc::revive {

using game::Unit;

constexpr int kReviveSkill = 95;            // Revive's Skills row (its id)
constexpr uint32_t kSkillsRowSize = 0x2EC;  // a compiled Skills row
constexpr size_t kSkillsPetType = 0x112;    // ... the pet type its pets are kept under (a byte; Revive reads it there)
constexpr int kMaxPetTypes = 64;            // 3.3 has 22

// Whether the timer is left alone: under the switch, for a monster the local player owns that is in its pet list of
// Revive's pet type (known: a pet type above 0).
constexpr bool keeps(bool on, bool owned, int pet_type, int revive_type) {
  return on && owned && revive_type > 0 && revive_type < kMaxPetTypes && pet_type == revive_type;
}

// The killself mod's timer (game, monster, the mod, the monster's unique flag).
using TimerFn = void(__fastcall*)(void* game, Unit* monster, int32_t mod, int32_t unique) noexcept;

void set_services(const D2RL::PluginContext* ctx, const D2RL::DataTableService* tables);
bool has_tables();

// After sites::derive() and game::bind(): null when the switch can work, else why not.
const char* bind();

// Game thread (the tick): Revive's pet type, from its Skills row in each table bank. Once is enough; a bank that could
// not be read is tried again at the next call.
void collect();
bool ready(uint8_t bank);           // the bank (a unit's +0x1BD, 1 classic .. 3 rotw) is read
int revive_pet_type(uint8_t bank);  // -1 when not known

// Server thread, the hook: the timer of a monster's killself mod has come.
void timer(TimerFn original, void* game, Unit* monster, int32_t mod, int32_t unique);

// For the console (any thread).
struct Stats {
  unsigned kept = 0;  // this session: revives of yours whose time came and that stayed
};
Stats stats();

}  // namespace d2rcc::revive
