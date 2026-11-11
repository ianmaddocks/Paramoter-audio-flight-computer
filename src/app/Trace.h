#pragma once
#include <stdint.h>

// Periodic lines in the debug log (debug and test builds only; these compile to nothing in a
// release build). They are what tools/hil_test.py reads to check the vario's behaviour:
//
//   NAV / BARO  once a second: position, altitudes, vario, flight and guidance state
//   BAT         every 2 s: battery voltage, percentage, minutes left
//   VOL         every 5 s: background noise and the volumes being applied
//   RDY         whenever the "ready to fly" state or its reason changes
//   BLE         when a Bluetooth client connects or disconnects
//   MEM         whenever free memory reaches a new low, naming what had just run
namespace trace {

// Call once per flight-logic tick.
void update(uint32_t nowMs);

// Call after a part of the main loop has run, naming it.
void memCheck(const char* after);

// Test build: report the ready state afresh.
void reset();

}  // namespace trace
