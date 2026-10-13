#pragma once
#include <stdint.h>

class Battery {
 public:
  void begin();
  void update(uint32_t nowMs);
  float volts() const { return volts_; }
  int percent() const;

 private:
  float volts_ = 0;
  uint32_t lastMs_ = 0;
};
