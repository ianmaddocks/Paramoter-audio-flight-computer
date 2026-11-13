#include "Button.h"

#include <Arduino.h>

ButtonEvent ButtonDecoder::feed(bool raw, uint32_t now) {
  if (raw != rawPressed_) {
    rawPressed_ = raw;
    rawChangeMs_ = now;
  }
  if (raw != pressed_ && now - rawChangeMs_ >= kDebounceMs) {
    pressed_ = raw;
    if (pressed_) {
      pressMs_ = now;
      holdCued_ = holdFired_ = false;
      return ButtonEvent::Press;
    }
    // released
    const bool wasHold = holdFired_ || holdCued_;
    holdCued_ = holdFired_ = false;
    if (wasHold) {  // a hold that fired is done; one that did not reach kHoldMs is abandoned
      clicks_ = 0;
      return ButtonEvent::None;
    }
    releaseMs_ = now;
    if (++clicks_ >= 3) {
      clicks_ = 0;
      return ButtonEvent::Triple;
    }
    return ButtonEvent::None;
  }
  if (pressed_) {
    if (!holdCued_ && now - pressMs_ >= kHoldCueMs) {
      holdCued_ = true;
      clicks_ = 0;  // this press is a hold: clicks before it are dropped
      return ButtonEvent::HoldCue;
    }
    if (holdCued_ && !holdFired_ && now - pressMs_ >= kHoldMs) {
      holdFired_ = true;
      return ButtonEvent::Hold;
    }
    return ButtonEvent::None;
  }
  if (clicks_ > 0 && now - releaseMs_ >= kGapMs) {
    const ButtonEvent ev = clicks_ == 1 ? ButtonEvent::Single : ButtonEvent::Double;
    clicks_ = 0;
    return ev;
  }
  return ButtonEvent::None;
}

void Button::begin(int pin) {
  pin_ = pin;
  pinMode(pin_, INPUT_PULLUP);
}

ButtonEvent Button::update(uint32_t now) { return dec_.feed(digitalRead(pin_) == LOW, now); }
