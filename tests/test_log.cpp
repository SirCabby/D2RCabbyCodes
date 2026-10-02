// The log (src/log.cpp) with a loader stood in for: one that takes 5 ms a line, as the real one does. The
// game's threads must not wait for it, and the lines must come out in the order they were made.
#include <D2RLPlugin/api.h>

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "context.h"
#include "log.h"

using namespace d2rcc;

static int g_failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
      ++g_failures;                                                     \
    }                                                                   \
  } while (0)

namespace {

struct Written {
  DWORD thread;
  char level;
  std::string text;
};
CRITICAL_SECTION g_cs;
std::vector<Written> g_written;
volatile LONG g_inside = 0;   // threads inside the loader's log at once
volatile LONG g_overlaps = 0;

void take(char level, const char* message) {
  if (InterlockedIncrement(&g_inside) > 1) InterlockedIncrement(&g_overlaps);
  Sleep(5);
  EnterCriticalSection(&g_cs);
  g_written.push_back(Written{GetCurrentThreadId(), level, message});
  LeaveCriticalSection(&g_cs);
  InterlockedDecrement(&g_inside);
}
void __cdecl log_info(const D2RL::PluginContext*, const char* m) noexcept { take('I', m); }
void __cdecl log_warn_fn(const D2RL::PluginContext*, const char* m) noexcept { take('W', m); }
void __cdecl log_error_fn(const D2RL::PluginContext*, const char* m) noexcept { take('E', m); }

std::vector<Written> written() {
  EnterCriticalSection(&g_cs);
  std::vector<Written> copy = g_written;
  LeaveCriticalSection(&g_cs);
  return copy;
}
void forget() {
  EnterCriticalSection(&g_cs);
  g_written.clear();
  LeaveCriticalSection(&g_cs);
}
bool wait_for(size_t lines, DWORD ms) {
  const ULONGLONG until = GetTickCount64() + ms;
  while (written().size() < lines && GetTickCount64() < until) Sleep(2);
  return written().size() >= lines;
}

double ms_of(LARGE_INTEGER a, LARGE_INTEGER b) {
  LARGE_INTEGER f;
  QueryPerformanceFrequency(&f);
  return static_cast<double>(b.QuadPart - a.QuadPart) * 1000.0 / static_cast<double>(f.QuadPart);
}

// A thread of the plugin's own: its lines are written as they are made, by itself.
DWORD WINAPI own_thread(void* result) {
  log_own_thread();
  const DWORD me = GetCurrentThreadId();
  LARGE_INTEGER t0, t1;
  QueryPerformanceCounter(&t0);
  for (int i = 0; i < 10; ++i) logf("own %d", i);
  QueryPerformanceCounter(&t1);
  bool ok = ms_of(t0, t1) >= 45.0;  // ten lines of 5 ms
  int mine = 0;
  for (const Written& w : written())
    if (w.text.rfind("own ", 0) == 0) {
      ok = ok && w.thread == me;
      ++mine;
    }
  *static_cast<bool*>(result) = ok && mine == 10;
  return 0;
}

// Two threads of the game's at once: the lines of each keep their order.
DWORD WINAPI game_thread(void* name) {
  for (int i = 0; i < 200; ++i) logf("%s %d", static_cast<const char*>(name), i);
  return 0;
}

}  // namespace

int main() {
  InitializeCriticalSection(&g_cs);
  static D2RL::PluginApi api{};
  api.apiSize = D2RL::PluginApiSize;
  api.logInfo = log_info;
  api.logWarn = log_warn_fn;
  api.logError = log_error_fn;
  static D2RL::PluginContext ctx{};
  ctx.contextSize = static_cast<uint32_t>(sizeof(D2RL::PluginContext));
  ctx.api = &api;
  set_context(&ctx);
  const DWORD me = GetCurrentThreadId();
  LARGE_INTEGER t0, t1;

  // Before the writer is there a line is written by the thread that made it.
  QueryPerformanceCounter(&t0);
  logf("first %d", 1);
  QueryPerformanceCounter(&t1);
  CHECK(written().size() == 1 && written()[0].thread == me && written()[0].text == "first 1");
  CHECK(ms_of(t0, t1) >= 4.5);
  CHECK(log_spent_us() >= 4500);

  // With it, a game thread's lines take it no time, and come out in their order on the writer's thread.
  forget();
  log_start(false);
  const long long spent = log_spent_us();
  QueryPerformanceCounter(&t0);
  for (int i = 0; i < 100; ++i) logf("line %d", i);
  QueryPerformanceCounter(&t1);
  std::printf("100 lines handed over in %.3f ms (written one by one they take 500)\n", ms_of(t0, t1));
  CHECK(ms_of(t0, t1) < 20.0);
  CHECK(log_spent_us() == spent);
  CHECK(wait_for(100, 5000));
  {
    const std::vector<Written> w = written();
    CHECK(w.size() == 100);
    bool ordered = true, elsewhere = true, late = false;
    for (size_t i = 0; i < w.size(); ++i) {
      char want[32];
      std::snprintf(want, sizeof(want), "line %zu", i);
      // A line written a quarter of a second or more after it was made says so.
      ordered = ordered && w[i].text.rfind(want, 0) == 0 &&
                (w[i].text == want || w[i].text.find("  (made ") == std::strlen(want));
      elsewhere = elsewhere && w[i].thread != me;
      late = late || w[i].text.find(" ms earlier)") != std::string::npos;
    }
    CHECK(ordered);
    CHECK(elsewhere);
    CHECK(late);                  // the last of them waited for 99 others
    CHECK(w[0].text == "line 0");  // the first did not
  }

  // A warning is written at once by its own thread, after everything that waits.
  forget();
  for (int i = 0; i < 5; ++i) logf("before %d", i);
  log_warn("warning %d", 1);
  {
    const std::vector<Written> w = written();
    CHECK(w.size() == 6);
    if (w.size() == 6) {
      for (size_t i = 0; i < 5; ++i) CHECK(w[i].text == "before " + std::to_string(i) && w[i].level == 'I');
      CHECK(w[5].text == "warning 1" && w[5].level == 'W' && w[5].thread == me);
    }
  }
  forget();
  log_error("error %d", 2);
  CHECK(written().size() == 1 && written()[0].level == 'E' && written()[0].thread == me);

  // The plugin's own threads write their own lines.
  forget();
  bool own_ok = false;
  HANDLE own = CreateThread(nullptr, 0, own_thread, &own_ok, 0, nullptr);
  WaitForSingleObject(own, 10000);
  CloseHandle(own);
  CHECK(own_ok);

  // Two game threads at once: every line is there, each thread's in its order, never two in the loader at once.
  forget();
  static char a[] = "a", b[] = "b";
  HANDLE threads[2] = {CreateThread(nullptr, 0, game_thread, a, 0, nullptr),
                       CreateThread(nullptr, 0, game_thread, b, 0, nullptr)};
  WaitForMultipleObjects(2, threads, TRUE, 10000);
  CloseHandle(threads[0]);
  CloseHandle(threads[1]);
  CHECK(wait_for(400, 10000));
  {
    const std::vector<Written> w = written();
    CHECK(w.size() == 400);
    int next_a = 0, next_b = 0;
    bool ordered = true;
    for (const Written& line : w) {
      int n = -1;
      if (std::sscanf(line.text.c_str(), "a %d", &n) == 1) ordered = ordered && n == next_a++;
      else if (std::sscanf(line.text.c_str(), "b %d", &n) == 1) ordered = ordered && n == next_b++;
      else ordered = false;
    }
    CHECK(ordered && next_a == 200 && next_b == 200);
  }
  CHECK(g_overlaps == 0);

  // Stopped with lines still waiting: they are written before it returns, and later ones by their own thread.
  forget();
  for (int i = 0; i < 20; ++i) logf("last %d", i);
  log_stop();
  CHECK(written().size() == 20);
  logf("after the writer");
  CHECK(written().size() == 21 && written().back().thread == me && written().back().text == "after the writer");

  // trace = true: no writer, every line by the thread that made it.
  forget();
  log_start(true);
  QueryPerformanceCounter(&t0);
  for (int i = 0; i < 4; ++i) logf("traced %d", i);
  QueryPerformanceCounter(&t1);
  CHECK(ms_of(t0, t1) >= 18.0);
  CHECK(written().size() == 4 && written()[3].thread == me);
  log_stop();

  // The least severe kind of line to write: the more severe are written too, the less severe are not.
  forget();
  log_start(false);
  CHECK(log_level() == LogLevel::kInfo);
  CHECK(log_wanted(LogLevel::kInfo) && log_wanted(LogLevel::kWarning) && log_wanted(LogLevel::kError));
  log_set_level(LogLevel::kWarning);
  CHECK(log_level() == LogLevel::kWarning && !log_enabled());
  CHECK(!log_wanted(LogLevel::kInfo) && log_wanted(LogLevel::kWarning) && log_wanted(LogLevel::kError));
  logf("information, left out");
  log_warn("a warning, written");
  log_error("an error, written");
  log_set_level(LogLevel::kError);
  CHECK(!log_wanted(LogLevel::kInfo) && !log_wanted(LogLevel::kWarning) && log_wanted(LogLevel::kError));
  logf("information, left out");
  log_warn("a warning, left out");
  log_error("the one error");
  Sleep(50);
  {
    const std::vector<Written> w = written();
    CHECK(w.size() == 3);
    if (w.size() == 3) {
      CHECK(w[0].level == 'W' && w[0].text == "a warning, written");
      CHECK(w[1].level == 'E' && w[1].text == "an error, written");
      CHECK(w[2].level == 'E' && w[2].text == "the one error");
    }
  }
  // A warning that is written the first few times waits for the log to take warnings.
  {
    volatile long warned = 0;
    CHECK(!log_once(&warned, LogLevel::kWarning) && warned == 0);
    CHECK(log_once(&warned, LogLevel::kError) && warned == 1);
    warned = 0;
    log_set_level(LogLevel::kWarning);
    CHECK(log_once(&warned, LogLevel::kWarning) && !log_once(&warned, LogLevel::kWarning));
    volatile long said = 0;
    CHECK(!log_once(&said) && said == 0);  // information is still left out
  }
  // A level that is none is taken for the nearest.
  log_set_level(static_cast<LogLevel>(7));
  CHECK(log_level() == LogLevel::kError);
  log_set_level(static_cast<LogLevel>(-3));
  CHECK(log_level() == LogLevel::kInfo);

  // The switch. Off: nothing is written, of any kind, and not by the writer either.
  forget();
  CHECK(log_is_on() && log_enabled());
  log_enable(false);
  CHECK(!log_is_on() && !log_enabled());
  CHECK(!log_wanted(LogLevel::kWarning) && !log_wanted(LogLevel::kError));
  QueryPerformanceCounter(&t0);
  for (int i = 0; i < 1000; ++i) logf("unheard %d", i);
  QueryPerformanceCounter(&t1);
  std::printf("1000 lines with the log off: %.3f ms\n", ms_of(t0, t1));
  CHECK(ms_of(t0, t1) < 5.0);
  log_warn("a warning with the log off");
  log_error("an error with the log off");
  Sleep(50);
  CHECK(written().empty());
  // What is asked for by name is written all the same, in its place among the rest.
  forget();
  logf("unheard");
  {
    const LogAlways always;
    CHECK(log_enabled() && !log_is_on());
    logf("asked for %d", 1);
    {
      const LogAlways inner;
      logf("asked for %d", 2);
    }
    logf("asked for %d", 3);
  }
  CHECK(!log_enabled());
  logf("unheard again");
  CHECK(wait_for(3, 2000));
  Sleep(30);
  {
    const std::vector<Written> w = written();
    CHECK(w.size() == 3);
    for (size_t i = 0; i < w.size() && i < 3; ++i) CHECK(w[i].text == "asked for " + std::to_string(i + 1));
  }
  // ... and whatever the level is: with errors alone taken, information that was asked for is written.
  forget();
  log_enable(true);
  log_set_level(LogLevel::kError);
  logf("unheard");
  {
    const LogAlways always;
    CHECK(log_wanted(LogLevel::kInfo) && log_wanted(LogLevel::kWarning));
    logf("asked for, as information");
    log_warn("asked for, as a warning");
  }
  CHECK(wait_for(2, 2000));
  Sleep(30);
  {
    const std::vector<Written> w = written();
    CHECK(w.size() == 2);
    if (w.size() == 2) CHECK(w[0].text == "asked for, as information" && w[1].text == "asked for, as a warning");
  }
  log_set_level(LogLevel::kInfo);
  log_enable(false);
  // A line that is written the first few times: its count stands still while the log is off.
  {
    volatile long count = 0, said = 0;
    for (int i = 0; i < 10; ++i) CHECK(!log_first(&count, 4));
    CHECK(!log_once(&said));
    CHECK(count == 0 && said == 0);
    log_enable(true);
    int firsts = 0, onces = 0;
    for (int i = 0; i < 10; ++i) {
      if (log_first(&count, 4)) ++firsts;
      if (log_once(&said)) ++onces;
    }
    CHECK(firsts == 4 && onces == 1);
    CHECK(count == 4 && said == 1);  // and no further once the last was written
  }
  // Switched on again: lines as before.
  forget();
  CHECK(log_is_on());
  logf("heard again");
  CHECK(wait_for(1, 2000));
  CHECK(written().size() == 1 && written()[0].text == "heard again");
  log_stop();

  // No loader at all (the context gone): nothing happens.
  set_context(nullptr);
  logf("nobody hears this");
  log_warn("nor this");

  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("test_log: ok\n");
  return 0;
}
