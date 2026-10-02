#pragma once

#include <cstddef>
#include <cstdint>

namespace d2rcc::game {
struct Unit;
}

// Identify on pickup: an item picked up from the ground is identified at once, as if the player had read a Scroll of
// Identify on it. What the game does with a pick-up (a click on an item on the ground, the request 0x16; the pick-up
// straight into the Horadric Cube, 0x5F; Telekinesis): the pick-up routine finds the item beside the player, takes it
// off the ground, puts it in the inventory grid, the belt or the cube, or on the cursor when the client asks for that
// (no room in the inventory), with the inventory's own move; its last call tells the clients where the item went (the
// item notice, a packet with the whole item, which holds its properties only once it is identified). Gold and a stack
// that merges into another never come to that call.
// Under the switch, for the local player's item that is not identified, the game's identify routine (an Identify
// scroll's and Deckard Cain's) is called around that notice; the item notice is hooked and acts only on the pick-up
// routine's call. The identify routine sets the item's flag, counts a charm's stats where they count, tells the
// clients, and records a unique or set item that dropped in the Chronicle. Its own update of the item is sent for an
// item that is stored, worn or in the belt, never for one on the cursor. So an item put on the cursor is identified
// before the pick-up's notice, which then carries it identified; any other after it, the way the quest reward giver
// identifies an item it has just told the client of. No game data is written.
namespace d2rcc::autoid {

using game::Unit;

// An item's mode (unit +0x0C): where it is.
enum Mode : uint32_t { kStored = 0, kWorn = 1, kBelt = 2, kGround = 3, kCursor = 4 };

// When the identify routine runs for an item the pick-up routine has moved: never for one that has the identified
// flag (the routine would do nothing), before the notice for one on the cursor, after it for one stored (in the
// inventory grid or the cube), worn or in the belt, and never for one anywhere else (a pick-up puts it nowhere else).
enum class When : uint8_t { kNever = 0, kBefore, kAfter };
constexpr When when(uint32_t item_flags, uint32_t identified, uint32_t mode) {
  if (!identified || (item_flags & identified)) return When::kNever;
  if (mode == kCursor) return When::kBefore;
  return mode == kStored || mode == kWorn || mode == kBelt ? When::kAfter : When::kNever;
}

// The routine hooked: the item notice (game, player, item, flags, command, mode, page, body location, x | y << 16).
using NoticeFn = void(__fastcall*)(void* game, Unit* player, Unit* item, uint32_t flags, uint32_t command,
                                   int32_t mode, uint32_t page, uint32_t body, uint32_t position) noexcept;

// After sites::derive() and game::bind(): null when the switch can work, else why not.
const char* bind();

// Server thread, the hook on the item notice: the original, and for the pick-up routine's call (`from` is the call's
// return address) the item identified before or after it.
void notice(NoticeFn original, void* game, Unit* player, Unit* item, uint32_t flags, uint32_t command, int32_t mode,
            uint32_t page, uint32_t body, uint32_t position, uintptr_t from);

// For the console (any thread).
struct Stats {
  unsigned identified = 0;  // this session: items identified as they were picked up
  unsigned cursor = 0;      // ... of them onto the cursor
};
Stats stats();

}  // namespace d2rcc::autoid
