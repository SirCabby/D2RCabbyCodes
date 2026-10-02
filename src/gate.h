#pragma once

#include <cstdint>

// When a piece of the UI callback has work. The callback runs at every frame, and most of what it carries has
// nothing to do at most of them: the automap is opened by a key, an area is entered once. Such a piece waits for
// something to happen - the loader's events and the game's UI messages say when - and until then a frame costs
// it a comparison. What nothing announces is found by a look at a slow, steady pace (the safety look).
// Pure arithmetic on the frame's time in milliseconds, so it is tested without the game (tests/test_gate.cpp).
namespace d2rcc::gate {

// A state that something may have changed: looked at in the first frame after it happened, again a little
// later (what a key does may take a frame or two to show), and at the safety pace besides.
class Looks {
 public:
  constexpr Looks(int per_event, uint64_t again_ms, uint64_t safety_ms)
      : per_event_(per_event), again_ms_(again_ms), safety_ms_(safety_ms) {}

  // Something happened that may have changed the state.
  void happened(uint64_t now) {
    left_ = per_event_;
    next_ = now;
  }

  // Whether this frame looks; a frame that does has taken the look.
  bool due(uint64_t now) {
    const bool asked = left_ > 0 && now >= next_;
    if (!asked && looked_ && now - looked_at_ < safety_ms_) return false;
    if (asked) {
      --left_;
      next_ = now + again_ms_;
    }
    looked_ = true;
    looked_at_ = now;
    return true;
  }

 private:
  int per_event_;
  uint64_t again_ms_, safety_ms_;
  int left_ = 0;         // looks still to take for what happened last
  uint64_t next_ = 0;    // ... the next of them
  bool looked_ = false;
  uint64_t looked_at_ = 0;
};

// Work that something starts and that then goes on, frame after frame, until it says it is done. While it
// cannot begin yet (what it waits for is not there) it may ask for the next frame too, but only for so long
// after what started it; from then on it is looked at at the safety pace, as it is when nothing started it.
class Work {
 public:
  constexpr Work(uint64_t wait_ms, uint64_t safety_ms) : wait_ms_(wait_ms), safety_ms_(safety_ms) {}

  // Something happened that may have given it work.
  void start(uint64_t now) {
    going_ = true;
    wait_until_ = now + wait_ms_;
  }

  // Nothing is going on any more (the game was left).
  void stop() {
    going_ = false;
    wait_until_ = 0;
  }

  // Whether this frame works.
  bool due(uint64_t now) {
    if (!going_ && looked_ && now - looked_at_ < safety_ms_) return false;
    looked_ = true;
    looked_at_ = now;
    return true;
  }

  // What the frame's work said: whether the next frame has to go on with it.
  void went(bool more) { going_ = more; }

  // Work that cannot begin may still wait from frame to frame.
  bool may_wait(uint64_t now) const { return now < wait_until_; }

 private:
  uint64_t wait_ms_, safety_ms_;
  bool going_ = false;
  uint64_t wait_until_ = 0;
  bool looked_ = false;
  uint64_t looked_at_ = 0;
};

}  // namespace d2rcc::gate
