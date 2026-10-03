#pragma once

#include <windows.h>

#include <cstdint>

// What the plugin's own work costs, measured in the game: the pieces that run every frame, every server frame
// or at every UI message are timed while a sample runs (the console's `cabbycodes perf [seconds]`), and the
// watchdog writes the outcome to the log when it ends. Outside a sample a timer is one read of a flag.
//
// A log line takes the loader some 5 ms, far more than any of the work timed here, so the time a piece spent
// writing log lines is kept apart from the time it worked.
namespace d2rcc::perf {

enum Scope : int {
  kUiPump = 0,     // the UI callback, once a frame: everything below it
  kHealthBars,     // ... the walk of the client's monsters
  kBarMonsters,    //     (a count, not a time: the monsters a walk met
  kBarsShown,      //     ... and the bars it made of them)
  kMapReveal,      // ... the map reveal, and the frames it looked at the map at all
  kMapRevealLook,
  kMapMarksRoom,   //     ... of it the landmarks and spawn spots of a room (each)
  kRemember,       // ... the automap and Show Items, and the frames they were looked at
  kRememberLook,
  kUiRequeue,      // ... and asking the loader for the next frame's callback
  kUiMessage,      // the UI message listener, per message
  kLevelName,      // the level-name hook, per name asked
  kManaCost,       // the mana-cost hook, per cost asked
  kTick,           // the server tick, 25 times a second
  kRoomFill,       // ... of it the rooms of an area built ahead for the game to fill, and what it made read
  kPresent,        // the present hook, once a frame: everything below it
  kOverlayFrame,   // ... the frame drawn over the game's (the health bars, the panel)
  kMapMarksDraw,   // the named enemies' part of the automap's draw of one unit (each unit the automap draws)
  kScopeCount
};

bool sampling();          // any thread: a sample is running
void start(int seconds);  // any thread: a new sample (1 to 120 s), in place of one that runs
// Any thread: one run of a piece took this long (in performance-counter ticks), log_us of it writing log lines.
void add(Scope scope, int64_t ticks, int64_t log_us);
// Any thread: one run of a piece met this many (for the scopes that count).
void count(Scope scope, int64_t amount);
// The watchdog: when the sample's time is up, the outcome goes to the log. True once, when it wrote it.
bool finish(ULONGLONG now);

// The time from here to the end of the block, while a sample runs.
class Timer {
 public:
  explicit Timer(Scope scope);
  ~Timer();
  Timer(const Timer&) = delete;
  Timer& operator=(const Timer&) = delete;

 private:
  Scope scope_;
  bool on_;
  int64_t began_ = 0;
  long long log_began_ = 0;
};

}  // namespace d2rcc::perf
