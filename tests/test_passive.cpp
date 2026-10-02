// Passive mercenary and minions (src/passive.h): which monsters are kept passive, and the thread's mark, which is up
// for the length of a kept pet's AI tick. While it is up an enemy test answers "not an enemy" and the target kept on a
// monster reads as none; an AI tick inside another has a mark of its own and puts the outer one back after it.
#include <cstdio>

#include "passive.h"

using namespace d2rcc;

static int g_failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
      ++g_failures;                                                     \
    }                                                                   \
  } while (0)

int main() {
  // Which monsters: only one the local player owns; the mercenary by its switch, any other by the minions' switch.
  for (int merc_on = 0; merc_on < 2; ++merc_on)
    for (int minions_on = 0; minions_on < 2; ++minions_on) {
      CHECK(!passive::keeps(false, false, merc_on, minions_on));  // a hostile monster, a town NPC, someone else's pet
      CHECK(!passive::keeps(false, true, merc_on, minions_on));   // ... another player's mercenary
      CHECK(passive::keeps(true, true, merc_on, minions_on) == static_cast<bool>(merc_on));
      CHECK(passive::keeps(true, false, merc_on, minions_on) == static_cast<bool>(minions_on));
    }

  // Outside any tick: the game's answers.
  passive::Mark mark;
  CHECK(passive::answer(mark, 1) == 1 && passive::answer(mark, 0) == 0);
  CHECK(passive::kept_kind(mark, 2) == 2 && passive::kept_kind(mark, 0) == 0);
  CHECK(mark.refused == 0 && mark.kept == 0);

  // A kept pet's tick: "not an enemy" where the game says enemy (counted), "not an enemy" stays; no kept target.
  const passive::Mark tick = passive::marked(mark, true, [&] {
    CHECK(mark.up);
    CHECK(passive::answer(mark, 1) == 0);
    CHECK(passive::answer(mark, 8) == 0);  // the party test's answer for two players (flag 8 of their relation)
    CHECK(passive::answer(mark, 0) == 0);
    CHECK(passive::answer(mark, 0x100000000ull) == 0x100000000ull);  // what the callers read is eax: zero is "no"
    CHECK(passive::kept_kind(mark, 2) == 0);
    CHECK(passive::kept_kind(mark, 0) == 0);
  });
  CHECK(tick.up && tick.refused == 2 && tick.kept == 1 && tick.kept_kind == 2);
  CHECK(!mark.up && mark.refused == 0 && mark.kept == 0);  // put back as it was
  CHECK(passive::answer(mark, 1) == 1 && passive::kept_kind(mark, 4) == 4);

  // Any other monster's tick: the game's answers, nothing counted.
  const passive::Mark other = passive::marked(mark, false, [&] {
    CHECK(!mark.up);
    CHECK(passive::answer(mark, 1) == 1);
    CHECK(passive::kept_kind(mark, 1) == 1);
  });
  CHECK(!other.up && other.refused == 0 && other.kept == 0);

  // A tick inside a kept pet's (should the game ever run one): its own mark, and the pet's back after it, its counts
  // kept apart.
  const passive::Mark outer = passive::marked(mark, true, [&] {
    CHECK(passive::answer(mark, 1) == 0);
    const passive::Mark inner = passive::marked(mark, false, [&] {
      CHECK(!mark.up);
      CHECK(passive::answer(mark, 1) == 1);
    });
    CHECK(!inner.up && inner.refused == 0);
    CHECK(mark.up && mark.refused == 1);  // the pet's mark again, with what it had counted
    const passive::Mark pet = passive::marked(mark, true, [&] { CHECK(passive::answer(mark, 1) == 0); });
    CHECK(pet.up && pet.refused == 1);
    CHECK(mark.up && mark.refused == 1);
    CHECK(passive::kept_kind(mark, 3) == 0);
  });
  CHECK(outer.up && outer.refused == 1 && outer.kept == 1 && outer.kept_kind == 3);
  CHECK(!mark.up && mark.refused == 0 && mark.kept == 0);

  if (g_failures) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("test_passive: ok\n");
  return 0;
}
