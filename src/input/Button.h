#pragma once
#include <stdint.h>

// What the button did. Press is the instant a press is registered (for the click cue); HoldCue is 0.8 s into a hold
// (the "keep holding" cue); the others are decided once the pilot has finished: a single, double, triple or five-fold
// click (the clicks are counted until kGapMs passes with no further press), or a hold of 1.5 s. Five is the first step of
// the factory reset (see FactoryReset.h); four clicks, or more than five, are nothing.
enum class ButtonEvent : uint8_t { None, Press, HoldCue, Single, Double, Triple, Hold, Five };

// The decoding, with no hardware in it, so it can be tested with scripted presses (the "button" unit suite).
//
//   click   a press shorter than kHoldCueMs; clicks are counted, and when kGapMs passes with no further press the
//           count is the event (1 Single, 2 Double, 3 Triple, 5 Five)
//   hold    held for kHoldMs: Hold (with a HoldCue at kHoldCueMs). A press held longer than a click but released before
//           kHoldMs is a hold that was abandoned: it does nothing at all, so a fumble never triggers anything.
class ButtonDecoder {
 public:
  static constexpr uint32_t kDebounceMs = 25;
  static constexpr uint32_t kHoldCueMs = 800;
  static constexpr uint32_t kHoldMs = 1500;
  static constexpr uint32_t kGapMs = 600;  // between the clicks of a double or triple (gloves: not too quick)

  ButtonEvent feed(bool rawPressed, uint32_t nowMs);
  bool pressed() const { return pressed_; }  // the debounced state of the button now

 private:
  bool rawPressed_ = false;
  bool pressed_ = false;
  bool holdCued_ = false;
  bool holdFired_ = false;
  uint8_t clicks_ = 0;
  uint32_t rawChangeMs_ = 0;
  uint32_t pressMs_ = 0;
  uint32_t releaseMs_ = 0;
};

// Active-low push button on a pin.
class Button {
 public:
  void begin(int pin);
  ButtonEvent update(uint32_t nowMs);
  bool pressed() const { return dec_.pressed(); }

 private:
  int pin_ = -1;
  ButtonDecoder dec_;
};
