#pragma once
#include <stdint.h>

enum class ButtonEvent : uint8_t { None, Single, Double, Long };

// Active-low push button with debounce and single / double / long press detection.
class Button {
 public:
  void begin(int pin);
  ButtonEvent update(uint32_t nowMs);

 private:
  static constexpr uint32_t kDebounceMs = 25;
  static constexpr uint32_t kLongMs = 800;
  static constexpr uint32_t kDoubleGapMs = 400;

  int pin_ = -1;
  bool rawPressed_ = false;
  bool pressed_ = false;
  bool longFired_ = false;
  uint8_t clicks_ = 0;
  uint32_t rawChangeMs_ = 0;
  uint32_t pressMs_ = 0;
  uint32_t releaseMs_ = 0;
};
