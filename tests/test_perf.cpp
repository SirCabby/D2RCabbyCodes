// The sample of the plugin's own work (src/perf.cpp): timers inside and outside a sample, the time spent writing
// log lines kept apart, and the outcome as it goes to the log (the log is this test's own).
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "log.h"
#include "perf.h"

static int g_failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
      ++g_failures;                                                     \
    }                                                                   \
  } while (0)

// The plugin's log, stood in for: the lines are kept, and "writing" one takes the time this test says.
static std::vector<std::string> g_lines;
static long long g_log_us = 0;

namespace d2rcc {
void logf(const char* fmt, ...) {
  char line[1024];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  g_lines.emplace_back(line);
}
void log_warn(const char*, ...) {}
void log_error(const char*, ...) {}
long long log_spent_us() { return g_log_us; }
LogAlways::LogAlways() {}
LogAlways::~LogAlways() {}
}  // namespace d2rcc

using namespace d2rcc;

static void spin_us(long long us) {
  LARGE_INTEGER f, t0, t;
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&t0);
  do QueryPerformanceCounter(&t);
  while ((t.QuadPart - t0.QuadPart) * 1000000 / f.QuadPart < us);
}

static const std::string* line_of(const char* what) {
  for (const std::string& l : g_lines)
    if (l.find(what) != std::string::npos) return &l;
  return nullptr;
}

int main() {
  // Outside a sample nothing is kept.
  CHECK(!perf::sampling());
  { perf::Timer timer(perf::kUiPump); }
  CHECK(!perf::finish(GetTickCount64() + 1000000));
  CHECK(g_lines.empty());

  perf::start(5);
  CHECK(perf::sampling());
  CHECK(!perf::finish(GetTickCount64()));  // its time is not up
  for (int frame = 0; frame < 100; ++frame) {
    perf::Timer pump(perf::kUiPump);
    {
      perf::Timer bars(perf::kHealthBars);
      spin_us(200);
      perf::count(perf::kBarMonsters, 40);
      perf::count(perf::kBarsShown, frame < 50 ? 10 : 30);
    }
    {
      perf::Timer map(perf::kMapReveal);
      if (frame == 7) {
        perf::Timer look(perf::kMapRevealLook);
        spin_us(1000);
        g_log_us += 5000;  // a log line written inside: 5 ms that are not the piece's work
        spin_us(5000);
      }
    }
  }
  for (int tick = 0; tick < 25; ++tick) {
    perf::Timer timer(perf::kTick);
    spin_us(20);
  }
  CHECK(perf::finish(GetTickCount64() + 6000));
  CHECK(!perf::sampling());
  CHECK(!perf::finish(GetTickCount64() + 6000));  // written once
  for (const std::string& l : g_lines) std::printf("%s\n", l.c_str());

  const std::string* pump = line_of("UI callback (a frame)");
  const std::string* bars = line_of("  health bars");
  const std::string* met = line_of("monsters met");
  const std::string* made = line_of("bars made");
  const std::string* look = line_of("    of them looking");
  const std::string* tick = line_of("server tick");
  const std::string* never = line_of("present (a frame)");
  CHECK(pump && pump->find("     100 runs") != std::string::npos);
  CHECK(bars && bars->find("     100 runs") != std::string::npos);
  CHECK(met && met->find("40.0 a walk, the most 40") != std::string::npos);
  CHECK(made && made->find("20.0 a walk, the most 30") != std::string::npos);
  CHECK(look && look->find("       1 runs") != std::string::npos);
  // The look took 6 ms, 5 of them a log line's: about 1 ms of work is its own.
  CHECK(look && look->find("5.0 ms more writing log lines") != std::string::npos);
  double each = 0.0;
  if (look) {
    const size_t at = look->find("a second),");
    CHECK(at != std::string::npos);
    if (at != std::string::npos) each = std::atof(look->c_str() + at + 10);
  }
  CHECK(each > 800.0 && each < 1600.0);
  CHECK(tick && tick->find("      25 runs") != std::string::npos);
  CHECK(never && never->find("never ran") != std::string::npos);
  CHECK(line_of("in the game's frames") != nullptr);

  // A new sample starts from nothing.
  g_lines.clear();
  perf::start(1);
  { perf::Timer timer(perf::kTick); }
  CHECK(perf::finish(GetTickCount64() + 2000));
  tick = line_of("server tick");
  CHECK(tick && tick->find("       1 runs") != std::string::npos);
  CHECK(line_of("UI callback (a frame)") && line_of("UI callback (a frame)")->find("never ran") != std::string::npos);

  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("test_perf: ok\n");
  return 0;
}
