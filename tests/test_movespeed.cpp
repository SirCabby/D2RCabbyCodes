// Movement speed (src/movespeed.h): the bonus in the player's base velocitypercent. The tick writes the game's own
// base plus the bonus, and takes a base it did not write for the game's own: each game's character starts at 100, on
// a new unit whose id is 1 again offline. The case that went wrong (2026-10-02): a bonus of 100 in one game, Save and
// Exit, the next game's character at 100 without it; the bonus lowered to 0 then wrote 0, slower than the game's own.
#include <cstdio>
#include <random>
#include <vector>

#include "movespeed.h"

using namespace d2rcc;
using movespeed::Found;

static int g_failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
      ++g_failures;                                                     \
    }                                                                   \
  } while (0)

// A player's unit as the game keeps it: its base velocitypercent, 100 as a game makes it.
struct Unit {
  int32_t base = 100;
};

// One tick, as the plugin's does it: the base read, written when the rule says so (SetUnitStat; `clamp` stands for a
// game that would hold the value at a top), and read back. Whether it wrote.
static bool tick(movespeed::State& s, Unit& u, int bonus, int32_t clamp = 1 << 30) {
  const int32_t base = u.base;
  const int32_t to = movespeed::target(s, &u, base, bonus);
  if (to == base) return false;
  u.base = to < clamp ? to : clamp;
  movespeed::wrote(s, u.base);
  return true;
}

int main() {
  // One game: off, on, from tick to tick nothing, up, off again; the game's own 100 under it throughout.
  {
    movespeed::State s;
    Unit a;
    CHECK(!tick(s, a, 0) && a.base == 100);
    CHECK(s.found == Found::kNewUnit);  // the session's first character
    CHECK(tick(s, a, 100) && a.base == 200);
    CHECK(s.found == Found::kWritten);
    CHECK(!tick(s, a, 100) && a.base == 200);
    CHECK(tick(s, a, 300) && a.base == 400);
    CHECK(tick(s, a, 25) && a.base == 125);
    CHECK(tick(s, a, 0) && a.base == 100);
    CHECK(!tick(s, a, 0) && a.base == 100);
  }

  // The case of 2026-10-02: 100 in one game; Save and Exit; the next game's character (another unit, at the game's
  // own 100 again) gets the bonus back at its first tick, and lowered to 0 it is at 100, not 0.
  {
    movespeed::State s;
    Unit first;
    CHECK(tick(s, first, 100) && first.base == 200);
    Unit next;
    CHECK(tick(s, next, 100) && next.base == 200);
    CHECK(s.found == Found::kNewUnit);
    CHECK(!tick(s, next, 100) && next.base == 200);
    CHECK(tick(s, next, 0) && next.base == 100);
    CHECK(!tick(s, next, 0) && next.base == 100);
  }

  // The next game's character in the memory of the last one (an allocator may hand it the same place): its base is
  // not the one written, which is enough.
  {
    movespeed::State s;
    Unit u;
    CHECK(tick(s, u, 100) && u.base == 200);
    u = Unit{};
    CHECK(tick(s, u, 100) && u.base == 200);
    CHECK(s.found == Found::kChanged);
    CHECK(tick(s, u, 0) && u.base == 100);
  }

  // The bonus changed in the menus, where no tick runs: the next game's character gets the new one; switched off
  // there, the character keeps the game's own and nothing is written.
  {
    movespeed::State s;
    Unit first;
    CHECK(tick(s, first, 100) && first.base == 200);
    Unit next;
    CHECK(tick(s, next, 50) && next.base == 150);
    Unit third;
    CHECK(!tick(s, third, 0) && third.base == 100);
    CHECK(!tick(s, third, 0) && third.base == 100);
  }

  // A base the game keeps at another value (a mod's): the bonus goes on top of it and comes off to it.
  {
    movespeed::State s;
    Unit m{150};
    CHECK(tick(s, m, 100) && m.base == 250);
    CHECK(tick(s, m, 0) && m.base == 150);
  }

  // A write held at a top: no write again from tick to tick, and the bonus comes off to the game's own base.
  {
    movespeed::State s;
    Unit c;
    CHECK(tick(s, c, 300, 255) && c.base == 255);
    CHECK(!tick(s, c, 300, 255) && c.base == 255);
    CHECK(tick(s, c, 0, 255) && c.base == 100);
  }

  // Games, bonuses and units in any order: after every tick the base is the game's own plus the bonus, never less
  // than the game's own (a base of 0 or less was the bug).
  {
    std::mt19937 rng(20261002);
    std::vector<Unit> memory(3);  // the places a game's character may be made in
    for (int run = 0; run < 2000; ++run) {
      movespeed::State s;
      size_t at = rng() % memory.size();
      memory[at] = Unit{};
      int bonus = 0;
      for (int step = 0; step < 60; ++step) {
        switch (rng() % 6) {
          case 0:  // Save and Exit, a new game: a character at the game's own 100, maybe in the last one's memory
            at = rng() % memory.size();
            memory[at] = Unit{};
            break;
          case 1:  // the bonus off
            bonus = 0;
            break;
          default:  // the bonus changed in the panel or the console, 0 .. 300 by 5
            bonus = static_cast<int>(rng() % 61) * 5;
            break;
        }
        tick(s, memory[at], bonus);
        CHECK(memory[at].base == 100 + bonus);
        CHECK(!tick(s, memory[at], bonus));  // and nothing more to do at the next tick
      }
    }
  }

  if (g_failures) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("test_movespeed: ok\n");
  return 0;
}
