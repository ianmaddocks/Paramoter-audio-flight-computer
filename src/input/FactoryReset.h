#pragma once
#include <stdint.h>

#include "Button.h"

// The button's way to a factory reset, with no hardware in it so it can be tested with scripted presses (the
// "factoryreset" unit suite). Two steps, so a fumble or a bag full of presses can never wipe the settings:
//
//   1. five quick clicks (the decoder's Five event), not flying: the vario says how to go on ("To restore factory
//      settings, press the button 5 times, then hold it for 2 seconds") and waits, with every other button action off
//   2. five quick clicks and a sixth press held for kHoldMs: the reset is done at that 2 s point
//
// Anything else ends it, saying so: a press held while still counting, the sixth press let go early, a seventh press,
// 8 s without a press, or 30 s after the prompt with nothing done.
class FactoryResetGesture {
 public:
  static constexpr uint32_t kArmWindowMs = 30000;  // from the prompt to the first press
  static constexpr uint32_t kPressGapMs = 8000;    // between presses
  static constexpr uint32_t kHoldMs = 2000;        // the sixth press, held
  static constexpr uint8_t kPresses = 6;

  enum class Action : uint8_t { None, Prompt, Cancelled, Reset };

  // Call every loop with the decoder's event (None most of the time) and whether the button is down. canArm: false
  // while flying (the prompt is not given, and the five clicks do what they always did: nothing).
  Action update(ButtonEvent ev, bool pressed, bool canArm, uint32_t now) {
    if (!armed_) {
      if (ev == ButtonEvent::Five && canArm) {
        armed_ = true;
        count_ = 0;
        holding_ = false;
        deadlineMs_ = now + kArmWindowMs;
        return Action::Prompt;
      }
      return Action::None;
    }
    if (!canArm) return end(Action::Cancelled);  // took off meanwhile
    if (ev == ButtonEvent::Press) {
      if (++count_ > kPresses) return end(Action::Cancelled);
      deadlineMs_ = now + kPressGapMs;
      if (count_ == kPresses) {
        holding_ = true;
        holdStartMs_ = now;
      }
      return Action::None;
    }
    if (ev == ButtonEvent::HoldCue && count_ < kPresses) return end(Action::Cancelled);  // held while still counting
    if (holding_) {
      if (!pressed) return end(Action::Cancelled);  // let go before the 2 s
      if (now - holdStartMs_ >= kHoldMs) {
        armed_ = false;
        return Action::Reset;
      }
      return Action::None;
    }
    if ((int32_t)(now - deadlineMs_) >= 0) return end(Action::Cancelled);
    return Action::None;
  }

  // True from the prompt until it is over: the button's usual actions are not to be taken meanwhile. Read it before
  // calling update() for an event, so the event that ends it is also not taken.
  bool armed() const { return armed_; }
  uint8_t presses() const { return count_; }

 private:
  Action end(Action a) {
    armed_ = false;
    holding_ = false;
    return a;
  }
  bool armed_ = false, holding_ = false;
  uint8_t count_ = 0;
  uint32_t deadlineMs_ = 0, holdStartMs_ = 0;
};
