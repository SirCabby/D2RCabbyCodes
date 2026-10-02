// When a piece of the UI callback works (src/gate.h): frames are played at the pace of a game, things happen,
// and the looks taken are counted.
#include <cstdint>
#include <cstdio>
#include <vector>

#include "gate.h"

using namespace d2rcc::gate;

static int g_failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
      ++g_failures;                                                     \
    }                                                                   \
  } while (0)

namespace {

constexpr uint64_t kFrameMs = 7;  // 144 frames a second, about
constexpr uint64_t kStart = 5'000'000;

// The frames of `ms` milliseconds from `from` on; at each, `happens` says whether something happened before it.
template <typename Happens>
std::vector<uint64_t> looks_taken(Looks& looks, uint64_t from, uint64_t ms, Happens happens) {
  std::vector<uint64_t> taken;
  for (uint64_t now = from; now < from + ms; now += kFrameMs) {
    if (happens(now)) looks.happened(now);
    if (looks.due(now)) taken.push_back(now);
  }
  return taken;
}

void test_looks() {
  // Nothing happens: the first frame looks (nothing is known yet), then one look a second.
  {
    Looks looks(2, 150, 1000);
    const std::vector<uint64_t> taken = looks_taken(looks, kStart, 10'000, [](uint64_t) { return false; });
    CHECK(!taken.empty() && taken.front() == kStart);
    CHECK(taken.size() == 10 || taken.size() == 11);
    for (size_t i = 1; i < taken.size(); ++i) {
      CHECK(taken[i] - taken[i - 1] >= 1000);
      CHECK(taken[i] - taken[i - 1] < 1000 + kFrameMs);
    }
  }
  // A key: the frame it came in looks, and one some 150 ms later; then the safety pace again.
  {
    Looks looks(2, 150, 1000);
    looks_taken(looks, kStart, 500, [](uint64_t) { return false; });
    const uint64_t key = kStart + 500 + 3 * kFrameMs;
    const std::vector<uint64_t> taken =
        looks_taken(looks, kStart + 500, 900, [key](uint64_t now) { return now == key; });
    CHECK(taken.size() == 2);
    if (taken.size() == 2) {
      CHECK(taken[0] == key);
      CHECK(taken[1] >= key + 150 && taken[1] < key + 150 + kFrameMs);
    }
    // ... and the safety look counts from the last look taken.
    const std::vector<uint64_t> later = looks_taken(looks, kStart + 1400, 2000, [](uint64_t) { return false; });
    CHECK(!later.empty() && !taken.empty() && later.front() - taken.back() >= 1000);
    CHECK(!later.empty() && !taken.empty() && later.front() - taken.back() < 1000 + kFrameMs);
  }
  // A key held down (it repeats at every frame): every frame looks, as each used to; no more than that.
  {
    Looks looks(2, 150, 1000);
    const std::vector<uint64_t> taken = looks_taken(looks, kStart, 700, [](uint64_t) { return true; });
    CHECK(taken.size() == 100);
  }
  // Keys now and then, as in a fight (four a second): two looks each, against 144 a second.
  {
    Looks looks(2, 150, 1000);
    const std::vector<uint64_t> taken =
        looks_taken(looks, kStart, 10'003, [](uint64_t now) { return (now - kStart) % (36 * kFrameMs) == 0; });
    CHECK(taken.size() >= 78 && taken.size() <= 82);
  }
  // A second look that comes due after another key is that key's first: never more than two are owed.
  {
    Looks looks(2, 150, 1000);
    looks.happened(kStart);
    CHECK(looks.due(kStart));
    looks.happened(kStart + 100);
    CHECK(looks.due(kStart + 100));
    CHECK(!looks.due(kStart + 150));
    CHECK(looks.due(kStart + 250));
    CHECK(!looks.due(kStart + 400));
    CHECK(looks.due(kStart + 1250));
  }
  // The clock at its start (a machine just booted): the first frame looks.
  {
    Looks looks(2, 150, 1000);
    CHECK(looks.due(0));
    CHECK(!looks.due(999));
    CHECK(looks.due(1000));
  }
}

void test_work() {
  // Nothing started it: a look a second.
  {
    Work work(10'000, 1000);
    int looks = 0;
    for (uint64_t now = kStart; now < kStart + 10'000; now += kFrameMs)
      if (work.due(now)) {
        ++looks;
        work.went(false);
      }
    CHECK(looks == 10 || looks == 11);
  }
  // An area entered whose map takes 14 frames: those 14 frames and no other until the next safety look.
  {
    Work work(10'000, 1000);
    CHECK(work.due(kStart));
    work.went(false);
    const uint64_t entered = kStart + 300;
    int frames = 0;
    uint64_t last = 0;
    for (uint64_t now = entered; now < entered + 900; now += kFrameMs) {
      if (now == entered) work.start(now);
      if (!work.due(now)) continue;
      ++frames;
      last = now;
      work.went(frames < 14);
    }
    CHECK(frames == 14);
    CHECK(last == entered + 13 * kFrameMs);
    // The safety look counts from the last frame that worked.
    uint64_t next = 0;
    for (uint64_t now = entered + 900; now < entered + 3000 && !next; now += kFrameMs)
      if (work.due(now)) next = now;
    CHECK(next >= last + 1000 && next < last + 1000 + kFrameMs);
  }
  // Work that cannot begin (no character yet) waits from frame to frame, ten seconds at the most, and is
  // then looked at once a second.
  {
    Work work(10'000, 1000);
    work.start(kStart);
    int waiting = 0, after = 0;
    for (uint64_t now = kStart; now < kStart + 20'000; now += kFrameMs) {
      if (!work.due(now)) continue;
      const bool may = work.may_wait(now);
      (may ? waiting : after) += 1;
      work.went(may);
    }
    CHECK(waiting >= 1428 && waiting <= 1430);  // every frame of the ten seconds
    CHECK(after >= 10 && after <= 11);         // then the safety look
  }
  // Left in the middle of it: nothing goes on, nothing may wait.
  {
    Work work(10'000, 1000);
    work.start(kStart);
    CHECK(work.due(kStart));
    work.went(true);
    CHECK(work.due(kStart + kFrameMs));
    work.stop();
    CHECK(!work.may_wait(kStart + 2 * kFrameMs));
    CHECK(!work.due(kStart + 2 * kFrameMs));
    CHECK(work.due(kStart + kFrameMs + 1000));
  }
  // Started again while it goes on: it goes on, and may wait from the later start.
  {
    Work work(10'000, 1000);
    work.start(kStart);
    CHECK(work.due(kStart));
    work.went(true);
    work.start(kStart + 5000);
    CHECK(work.due(kStart + 5000));
    CHECK(work.may_wait(kStart + 14'999));
    CHECK(!work.may_wait(kStart + 15'000));
  }
}

}  // namespace

int main() {
  test_looks();
  test_work();
  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("gate: all checks passed\n");
  return 0;
}
