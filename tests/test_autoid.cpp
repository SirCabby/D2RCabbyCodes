// Identify on pickup (src/autoid.h): when the game's identify routine runs for an item the pick-up routine has just
// moved, around the pick-up's notice to the clients. The routine's own update of the item reaches the client for an
// item that is stored, worn or in the belt, and not for one on the cursor: so an item on the cursor is identified
// before the pick-up's notice (which then carries it identified), any other after it. An item that has the flag
// already, or that lies anywhere a pick-up never puts one, is left alone.
#include <cstdio>

#include "autoid.h"

using namespace d2rcc;
using autoid::When;

static int g_failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
      ++g_failures;                                                     \
    }                                                                   \
  } while (0)

int main() {
  constexpr uint32_t kIdentified = 0x10;  // the flag the game's identify routine tests and sets (3.3)
  constexpr uint32_t kEthereal = 0x400000, kSocketed = 0x800;

  // Not identified yet: before the notice on the cursor, after it in the inventory grid or the cube, worn, the belt.
  CHECK(autoid::when(0, kIdentified, autoid::kCursor) == When::kBefore);
  CHECK(autoid::when(0, kIdentified, autoid::kStored) == When::kAfter);
  CHECK(autoid::when(0, kIdentified, autoid::kWorn) == When::kAfter);
  CHECK(autoid::when(0, kIdentified, autoid::kBelt) == When::kAfter);
  // Other flags change nothing.
  CHECK(autoid::when(kEthereal | kSocketed, kIdentified, autoid::kCursor) == When::kBefore);
  CHECK(autoid::when(kEthereal, kIdentified, autoid::kStored) == When::kAfter);

  // Identified already (white items, potions, anything a player identified and dropped): never.
  for (uint32_t mode = 0; mode < 8; ++mode) {
    CHECK(autoid::when(kIdentified, kIdentified, mode) == When::kNever);
    CHECK(autoid::when(kIdentified | kEthereal, kIdentified, mode) == When::kNever);
  }

  // Still on the ground, or a mode a pick-up never leaves an item in (dropping, in a socket): never.
  CHECK(autoid::when(0, kIdentified, autoid::kGround) == When::kNever);
  CHECK(autoid::when(0, kIdentified, 5) == When::kNever);
  CHECK(autoid::when(0, kIdentified, 6) == When::kNever);
  CHECK(autoid::when(0, kIdentified, 0xFFFFFFFFu) == When::kNever);

  // Without the flag read from the routine's code nothing is taken for granted.
  for (uint32_t mode = 0; mode < 8; ++mode) CHECK(autoid::when(0, 0, mode) == When::kNever);

  if (g_failures) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("test_autoid: ok\n");
  return 0;
}
