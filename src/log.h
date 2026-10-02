#pragma once

// Logging goes to D2RLoader's per-plugin log (d2rloader/logs/cabbycodes.log)
// through the plugin context; logf() is the printf-style front for it, usable
// from any thread once the plugin has loaded.
//
// A line takes the loader some 5 ms to write (measured: two lines are never less than 4 ms apart, 8 on
// average), which is a frame or more of the game. So the game's own threads - the UI thread that also draws
// its frames, the server thread - do not write: they hand their lines to a writer thread of the plugin's,
// which writes them in the order they were made (a line written a quarter of a second or more after it was
// made says by how much). Written at once, by the thread that made them, are:
//   - the lines of the plugin's own threads (the worker, the watchdog), which keep nobody waiting;
//   - warnings and errors, after everything that waits (they are rare, and the last thing before a crash);
//   - every line with trace = true in the settings: the log is then complete up to the instant of a crash,
//     and a frame waits for each line as it did.
//
// What is written is chosen in the panel's Diagnostics section (and in the settings, and in the console): the
// log can be switched off (`logging`, `cabbycodes log on|off`), and while it is on the least severe kind of
// line to write is picked (`log_level`, `cabbycodes loglevel`): information, which is everything; warnings,
// which leaves the information out; or errors alone. A line that is not written is not even put together.
// Written whatever is chosen is what was asked for by name (the console's perf and dropcheck, a DEV command),
// which is made inside a LogAlways.
namespace d2rcc {

// The kinds of line, the least severe first (the loader's [INFO], [WARN] and [ERROR]).
enum class LogLevel : int { kInfo = 0, kWarning = 1, kError = 2 };

void logf(const char* fmt, ...) __attribute__((format(gnu_printf, 1, 2)));       // information
void log_warn(const char* fmt, ...) __attribute__((format(gnu_printf, 1, 2)));   // a warning
void log_error(const char* fmt, ...) __attribute__((format(gnu_printf, 1, 2)));  // an error

void log_enable(bool on);            // any thread: the switch
bool log_is_on();
void log_set_level(LogLevel least);  // any thread: the least severe kind that is written (the more severe are too)
LogLevel log_level();
// A line of this kind, made by this thread now, would be written: the log is on and the kind is severe enough,
// or a LogAlways is open.
bool log_wanted(LogLevel level);
inline bool log_enabled() { return log_wanted(LogLevel::kInfo); }  // a logf() would be written

// For a line that is written only the first `limit` times: whether this is one of them. The count stands still
// while the line would not be written, so the first ones are there once the log takes them.
// (`long` is the LONG of the Interlocked calls.)
bool log_first(volatile long* count, long limit, LogLevel level = LogLevel::kInfo);
inline bool log_once(volatile long* said, LogLevel level = LogLevel::kInfo) { return log_first(said, 1, level); }

// While one is open, this thread's lines are written whatever was chosen.
class LogAlways {
 public:
  LogAlways();
  ~LogAlways();
  LogAlways(const LogAlways&) = delete;
  LogAlways& operator=(const LogAlways&) = delete;
};

// The time this thread has spent writing log lines so far, in microseconds: the loader's log takes some
// milliseconds a line, which is not the work of whoever is timing itself. (A line handed to the writer
// takes its maker no time to speak of, and counts nothing.)
long long log_spent_us();

// The writer thread: started once the plugin has its context and its settings (at_once: every line is
// written by the thread that made it, as before it is started), stopped at unload, after the lines that
// still wait.
void log_start(bool at_once);
void log_stop();
// A thread of the plugin's own says so as it starts: its lines are written at once.
void log_own_thread();

}  // namespace d2rcc
