#include "Button.h"

#include <Arduino.h>

void Button::begin(int pin) {
  pin_ = pin;
  pinMode(pin_, INPUT_PULLUP);
}

ButtonEvent Button::update(uint32_t now) {
  bool raw = digitalRead(pin_) == LOW;
  if (raw != rawPressed_) {
    rawPressed_ = raw;
    rawChangeMs_ = now;
  }
  if (raw != pressed_ && now - rawChangeMs_ >= kDebounceMs) {
    pressed_ = raw;
    if (pressed_) {
      pressMs_ = now;
      longFired_ = false;
    } else if (!longFired_) {
      ++clicks_;
      releaseMs_ = now;
      if (clicks_ >= 2) {
        clicks_ = 0;
        return ButtonEvent::Double;
      }
    }
  }
  if (pressed_ && !longFired_ && now - pressMs_ >= kLongMs) {
    longFired_ = true;
    clicks_ = 0;
    return ButtonEvent::Long;
  }
  if (!pressed_ && clicks_ == 1 && now - releaseMs_ >= kDoubleGapMs) {
    clicks_ = 0;
    return ButtonEvent::Single;
  }
  return ButtonEvent::None;
}
