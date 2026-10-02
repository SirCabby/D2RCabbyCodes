#pragma once

#include <cstddef>
#include <cstdint>

#include "dropodds.h"

namespace d2rcc::game {
struct Unit;
}

// The Chronicle (3.3): the game's record of the unique items, set items and runewords a player has found. The game
// keeps it in the player's data, a map a kind (0 set items, 1 uniques, 2 runewords) whose entries are row ids (the
// item's file index: its UniqueItems or SetItems row); the save keeps it in the shared stash, a section of its own.
// An item that drops carries a "found" flag, and identifying it adds its row when the Chronicle does not have it
// yet. Which rows are entries at all is the game's own list of each kind, made when its tables are read: a row that
// is not disabled, not kept out of the Chronicle (its disableChronicle column) and that has a base (a unique's not a
// quest item).
//
// The loot switch "only drop what the Chronicle misses" asks here which unique and set rows the local player's
// Chronicle misses; dropodds.h then keeps a drop that comes out as one of them (loot.cpp). Nothing is written and
// nothing of the game's is called: its data is read where its own routines read it (sites.cpp takes the offsets and
// the lists from their code). Server thread, but for stats().
namespace d2rcc::chronicle {

using game::Unit;

// --- how the game keeps it (no game here: tests/test_chronicle.cpp) ---------------------------------------------

// A map of the Chronicle: a count of buckets (a power of two) and the buckets, each a chain of entries that begin
// with the next one and have their id (u32) further on. `read(at, &word)` reads 8 bytes of the game's memory (false
// when it cannot). The ids go into `out`; the answer is how many entries there are, or -1 when the map does not read
// as one (a count that is no power of two or too large, an id past the rows, a chain that does not end).
struct MapLayout {
  int count_at = 0, buckets_at = 8;  // of the map
  int next_at = 0, key_at = 8;       // of an entry
};
constexpr uint64_t kMostBuckets = 1u << 16;
constexpr int kMostEntries = 1 << 14;

template <typename Read>
int ids_of(uintptr_t map, const MapLayout& at, const Read& read, dropodds::Rows* out) {
  uint64_t buckets = 0, array = 0;
  if (!read(map + static_cast<uintptr_t>(at.count_at), &buckets) ||
      !read(map + static_cast<uintptr_t>(at.buckets_at), &array))
    return -1;
  if (!array) return buckets <= kMostBuckets ? 0 : -1;  // a map nothing was put in has no buckets yet
  if (!buckets || buckets > kMostBuckets || (buckets & (buckets - 1)) != 0) return -1;
  int entries = 0;
  for (uint64_t b = 0; b < buckets; ++b) {
    uint64_t node = 0;
    if (!read(static_cast<uintptr_t>(array + b * 8), &node)) return -1;
    while (node) {
      uint64_t key = 0, next = 0;
      if (++entries > kMostEntries || !read(static_cast<uintptr_t>(node) + static_cast<uintptr_t>(at.key_at), &key) ||
          !read(static_cast<uintptr_t>(node) + static_cast<uintptr_t>(at.next_at), &next) ||
          !out->set(static_cast<uint32_t>(key)))
        return -1;
      node = next;
    }
  }
  return entries;
}

// A list of the game's: `count` entries of `stride` bytes from `data`, each beginning with a pointer to a row of the
// table, whose id is its first 16 bits. With `tag_at` >= 0 an entry is taken only when the 8 bytes there are 0 (the
// set items' list has the sets too, tagged 1). The ids go into `out`; the answer is how many were taken, or -1.
constexpr uint64_t kMostListed = dropodds::Rows::kMost;

template <typename Read>
int list_ids(uintptr_t data, uint64_t count, int stride, int tag_at, const Read& read, dropodds::Rows* out) {
  if (!count) return 0;
  if (!data || count > 2 * kMostListed || stride < 8) return -1;
  int taken = 0;
  for (uint64_t i = 0; i < count; ++i) {
    const uintptr_t entry = static_cast<uintptr_t>(data + i * static_cast<uint64_t>(stride));
    uint64_t row = 0, id = 0;
    if (tag_at >= 0) {
      uint64_t tag = 0;
      if (!read(entry + static_cast<uintptr_t>(tag_at), &tag)) return -1;
      if (tag != 0) continue;
    }
    if (!read(entry, &row) || !row || !read(static_cast<uintptr_t>(row), &id) ||
        !out->set(static_cast<uint32_t>(id & 0xFFFF)))
      return -1;
    ++taken;
  }
  return taken;
}

// --- the game ---------------------------------------------------------------------------------------------------

// After sites::derive: why the Chronicle cannot be read (null when it can).
const char* bind();

// The local player's Chronicle read again (server thread): the unique and set rows it has entries for and does not
// have, less the ones that dropped in this game (dropped()). False when it cannot be read now: no player yet, or
// memory that does not read as the game keeps it (the log says which).
bool read();
const dropodds::Rows& missing_uniques();
const dropodds::Rows& missing_sets();
// A real item (made): a unique or set item of a row the Chronicle misses, that has not dropped in this game before.
bool missing(Unit* item);
// A drop that stays on the ground: its row is not missing again in this game (a unique drops once a game by the
// game's own rule anyway; a set item would not).
void dropped(Unit* item);
void game_left();

struct Stats {
  bool read = false;          // the Chronicle was read in this game
  int uniques = 0, sets = 0;  // the entries there are
  int uniques_missing = 0, sets_missing = 0;  // ... and the ones the Chronicle does not have
  int dropped = 0;            // drops kept in this game as items it misses
};
Stats stats();  // any thread

}  // namespace d2rcc::chronicle
