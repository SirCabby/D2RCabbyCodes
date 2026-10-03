#include "perf.h"

#include <cstdio>

#include "log.h"

namespace d2rcc::perf {
namespace {

constexpr int kMinSeconds = 1;
constexpr int kMaxSeconds = 120;

struct Cell {
  volatile LONG64 runs;
  volatile LONG64 ticks;    // without the log lines written inside
  volatile LONG64 longest;  // ... of one run
  volatile LONG64 log_us;
};

const char* const kNames[kScopeCount] = {
    "UI callback (a frame)",    "  health bars",          "    monsters met",         "    bars made",
    "  map reveal",             "    of them looking",    "      a room's landmarks", "  automap and Show Items",
    "    of them looking",      "  asking for the next",  "UI message (each)",        "level name asked (each)",
    "mana cost asked (each)",   "server tick",            "present (a frame)",        "  frame drawn over it",
    "automap unit (each)",
};

bool counts(int scope) { return scope == kBarMonsters || scope == kBarsShown; }

Cell g_cells[kScopeCount];
volatile LONG g_on = 0;
volatile LONG64 g_until = 0;  // the tick count the sample ends at
volatile LONG64 g_began = 0;
int64_t g_frequency = 0;

int64_t frequency() {
  if (!g_frequency) {
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    g_frequency = f.QuadPart > 0 ? f.QuadPart : 1;
  }
  return g_frequency;
}

int64_t ticks_now() {
  LARGE_INTEGER t{};
  QueryPerformanceCounter(&t);
  return t.QuadPart;
}

double to_us(int64_t ticks) { return static_cast<double>(ticks) * 1000000.0 / static_cast<double>(frequency()); }

}  // namespace

bool sampling() { return g_on != 0; }

void start(int seconds) {
  if (seconds < kMinSeconds) seconds = kMinSeconds;
  if (seconds > kMaxSeconds) seconds = kMaxSeconds;
  frequency();
  InterlockedExchange(&g_on, 0);
  for (Cell& c : g_cells) {
    InterlockedExchange64(&c.runs, 0);
    InterlockedExchange64(&c.ticks, 0);
    InterlockedExchange64(&c.longest, 0);
    InterlockedExchange64(&c.log_us, 0);
  }
  const ULONGLONG now = GetTickCount64();
  InterlockedExchange64(&g_began, static_cast<LONG64>(now));
  InterlockedExchange64(&g_until, static_cast<LONG64>(now + static_cast<ULONGLONG>(seconds) * 1000));
  InterlockedExchange(&g_on, 1);
}

void count(Scope scope, int64_t amount) {
  if (sampling()) add(scope, amount, 0);
}

void add(Scope scope, int64_t ticks, int64_t log_us) {
  if (scope < 0 || scope >= kScopeCount) return;
  Cell& c = g_cells[scope];
  if (ticks < 0) ticks = 0;
  InterlockedIncrement64(&c.runs);
  InterlockedExchangeAdd64(&c.ticks, ticks);
  if (log_us > 0) InterlockedExchangeAdd64(&c.log_us, log_us);
  for (LONG64 seen = c.longest; ticks > seen;) {
    const LONG64 was = InterlockedCompareExchange64(&c.longest, ticks, seen);
    if (was == seen) break;
    seen = was;
  }
}

bool finish(ULONGLONG now) {
  if (!g_on || now < static_cast<ULONGLONG>(g_until)) return false;
  InterlockedExchange(&g_on, 0);
  const LogAlways always;  // asked for by name: written with the log switched off too
  const double seconds = static_cast<double>(now - static_cast<ULONGLONG>(g_began)) / 1000.0;
  logf("perf: %.1f s sampled; the times are the plugin's own work, without the log lines written inside it", seconds);
  for (int s = 0; s < kScopeCount; ++s) {
    const Cell& c = g_cells[s];
    const LONG64 runs = c.runs;
    if (!runs) {
      if (!counts(s)) logf("perf: %-26s never ran", kNames[s]);
      continue;
    }
    if (counts(s)) {
      logf("perf: %-26s %8.1f a walk, the most %lld", kNames[s], static_cast<double>(c.ticks) / static_cast<double>(runs),
           static_cast<long long>(c.longest));
      continue;
    }
    char log_note[64] = "";
    if (c.log_us > 0)
      std::snprintf(log_note, sizeof(log_note), "; %.1f ms more writing log lines",
                    static_cast<double>(c.log_us) / 1000.0);
    logf("perf: %-26s %8lld runs (%7.1f a second), %8.2f us each, the longest %9.1f us, %9.2f ms in all%s", kNames[s],
         static_cast<long long>(runs), seconds > 0.0 ? static_cast<double>(runs) / seconds : 0.0,
         to_us(c.ticks) / static_cast<double>(runs), to_us(c.longest), to_us(c.ticks) / 1000.0, log_note);
  }
  // The pieces that share the game's frames: the UI callback, the UI messages, the two name and cost hooks and the
  // present hook. (Where the game presents from its UI thread, as it does under Proton, they add up on one thread.)
  const Scope frame_scopes[] = {kUiPump, kUiRequeue, kUiMessage, kLevelName, kManaCost, kPresent};
  double frame_us = 0.0, frame_log_us = 0.0;
  for (const Scope s : frame_scopes) {
    frame_us += to_us(g_cells[s].ticks);
    frame_log_us += static_cast<double>(g_cells[s].log_us);
  }
  const double frames = static_cast<double>(g_cells[kPresent].runs ? g_cells[kPresent].runs : g_cells[kUiPump].runs);
  logf("perf: in the game's frames %.2f ms of %.0f ms (%.3f %%), %.2f us a frame over %.0f frames; on the server "
       "thread's ticks %.2f ms; writing log lines in either %.1f ms",
       frame_us / 1000.0, seconds * 1000.0, seconds > 0.0 ? frame_us / (seconds * 10000.0) : 0.0,
       frames > 0.0 ? frame_us / frames : 0.0, frames, to_us(g_cells[kTick].ticks) / 1000.0,
       (frame_log_us + static_cast<double>(g_cells[kTick].log_us)) / 1000.0);
  return true;
}

Timer::Timer(Scope scope) : scope_(scope), on_(sampling()) {
  if (!on_) return;
  log_began_ = log_spent_us();
  began_ = ticks_now();
}

Timer::~Timer() {
  if (!on_) return;
  const int64_t took = ticks_now() - began_;
  const long long log_us = log_spent_us() - log_began_;
  const double log_ticks = static_cast<double>(log_us) * static_cast<double>(frequency()) / 1000000.0;
  add(scope_, took - static_cast<int64_t>(log_ticks), log_us);
}

}  // namespace d2rcc::perf
