#include "log.h"

#include <D2RLPlugin/api.h>

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <deque>
#include <string>

#include "context.h"

namespace d2rcc {
namespace {

constexpr size_t kMaxWaiting = 8192;  // lines that wait for the writer; more are counted and left out
constexpr ULONGLONG kLateMs = 250;    // a line written this long after it was made says so
constexpr DWORD kWriterIdleMs = 1000;
constexpr DWORD kStopWaitMs = 2000;   // the lines that wait at unload get this long

struct Line {
  LogLevel level;
  ULONGLONG made;
  std::string text;
};

CRITICAL_SECTION g_waiting_cs;  // the lines that wait
CRITICAL_SECTION g_write_cs;    // one thread writes at a time, so the lines keep their order
bool g_ready = false;           // the two locks are
std::deque<Line> g_waiting;
HANDLE g_writer = nullptr;
HANDLE g_wake = nullptr;
volatile LONG g_handed = 0;     // the writer takes the game threads' lines
volatile LONG g_stop = 0;
volatile LONG g_left_out = 0;   // lines that found the queue full
volatile LONG g_off = 0;        // the log is switched off: only what was asked for by name is written
volatile LONG g_least = 0;      // the least severe kind of line that is written (a LogLevel)

thread_local long long t_spent_us = 0;
thread_local bool t_own = false;  // a thread of the plugin's own
thread_local int t_always = 0;    // LogAlways open on this thread

long long now_us() {
  static LARGE_INTEGER frequency{};
  if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
  LARGE_INTEGER t{};
  QueryPerformanceCounter(&t);
  return frequency.QuadPart > 0 ? t.QuadPart * 1000000 / frequency.QuadPart : 0;
}

void write(LogLevel level, const char* message) {
  const D2RL::PluginContext* ctx = context();
  const D2RL::PluginApi* api = D2RL::GetApi(ctx);
  const D2RL::LogFn fn = level == LogLevel::kError     ? D2RL::ApiLogError(api)
                         : level == LogLevel::kWarning ? D2RL::ApiLogWarn(api)
                                                       : D2RL::ApiLogInfo(api);
  if (!ctx || !fn) return;
  const long long began = now_us();
  fn(ctx, message);
  t_spent_us += now_us() - began;
}

// Under g_write_cs: the lines that wait, in their order.
void write_waiting() {
  for (;;) {
    Line line;
    EnterCriticalSection(&g_waiting_cs);
    const bool any = !g_waiting.empty();
    if (any) {
      line = std::move(g_waiting.front());
      g_waiting.pop_front();
    }
    LeaveCriticalSection(&g_waiting_cs);
    if (!any) break;
    const ULONGLONG late = GetTickCount64() - line.made;
    if (late >= kLateMs) {
      char note[48];
      std::snprintf(note, sizeof(note), "  (made %llu ms earlier)", static_cast<unsigned long long>(late));
      line.text += note;
    }
    write(line.level, line.text.c_str());
  }
  if (const LONG left_out = InterlockedExchange(&g_left_out, 0)) {
    char message[96];
    std::snprintf(message, sizeof(message), "log: %ld lines were left out - more than %zu waited to be written",
                  left_out, kMaxWaiting);
    if (log_wanted(LogLevel::kWarning)) write(LogLevel::kWarning, message);
  }
}

DWORD WINAPI writer(void*) {
  t_own = true;
  while (!g_stop) {
    WaitForSingleObject(g_wake, kWriterIdleMs);
    EnterCriticalSection(&g_write_cs);
    write_waiting();
    LeaveCriticalSection(&g_write_cs);
  }
  return 0;
}

void emit(LogLevel level, const char* fmt, va_list args) {
  if (!log_wanted(level)) return;
  char message[1024];
  std::vsnprintf(message, sizeof(message), fmt, args);
  if (g_handed && level == LogLevel::kInfo && !t_own) {
    EnterCriticalSection(&g_waiting_cs);
    const bool room = g_waiting.size() < kMaxWaiting;
    if (room) g_waiting.push_back(Line{level, GetTickCount64(), message});
    LeaveCriticalSection(&g_waiting_cs);
    if (!room) InterlockedIncrement(&g_left_out);
    SetEvent(g_wake);
    return;
  }
  if (!g_ready) {
    write(level, message);
    return;
  }
  EnterCriticalSection(&g_write_cs);
  write_waiting();
  write(level, message);
  LeaveCriticalSection(&g_write_cs);
}

}  // namespace

long long log_spent_us() { return t_spent_us; }

void log_enable(bool on) { InterlockedExchange(&g_off, on ? 0 : 1); }
bool log_is_on() { return !g_off; }

void log_set_level(LogLevel least) {
  LONG n = static_cast<LONG>(least);
  if (n < static_cast<LONG>(LogLevel::kInfo)) n = static_cast<LONG>(LogLevel::kInfo);
  if (n > static_cast<LONG>(LogLevel::kError)) n = static_cast<LONG>(LogLevel::kError);
  InterlockedExchange(&g_least, n);
}
LogLevel log_level() { return static_cast<LogLevel>(g_least); }

bool log_wanted(LogLevel level) { return t_always > 0 || (!g_off && static_cast<LONG>(level) >= g_least); }

bool log_first(volatile long* count, long limit, LogLevel level) {
  if (*count >= limit || !log_wanted(level)) return false;
  return InterlockedIncrement(count) <= limit;
}

LogAlways::LogAlways() { ++t_always; }
LogAlways::~LogAlways() { --t_always; }

void log_start(bool at_once) {
  if (!g_ready) {
    InitializeCriticalSection(&g_waiting_cs);
    InitializeCriticalSection(&g_write_cs);
    g_ready = true;
  }
  if (at_once || g_writer) return;
  InterlockedExchange(&g_stop, 0);
  // The event is kept for good: a thread may still be about to set it when the writer is stopped.
  if (!g_wake) g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  g_writer = g_wake ? CreateThread(nullptr, 0, writer, nullptr, 0, nullptr) : nullptr;
  // Without a writer every line is written by the thread that made it, as with trace = true.
  if (g_writer) InterlockedExchange(&g_handed, 1);
}

void log_stop() {
  if (!g_writer) return;
  InterlockedExchange(&g_handed, 0);  // from here on every line is written by its own thread
  InterlockedExchange(&g_stop, 1);
  SetEvent(g_wake);
  const bool ended = WaitForSingleObject(g_writer, kStopWaitMs) == WAIT_OBJECT_0;
  CloseHandle(g_writer);
  g_writer = nullptr;
  if (!ended) return;  // it is still writing: the lock is its own
  EnterCriticalSection(&g_write_cs);
  write_waiting();  // a line handed over as the writer ended
  LeaveCriticalSection(&g_write_cs);
}

void log_own_thread() { t_own = true; }

void logf(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  emit(LogLevel::kInfo, fmt, args);
  va_end(args);
}

void log_warn(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  emit(LogLevel::kWarning, fmt, args);
  va_end(args);
}

void log_error(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  emit(LogLevel::kError, fmt, args);
  va_end(args);
}

}  // namespace d2rcc
