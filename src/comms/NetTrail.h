#pragma once
#include <Arduino.h>

// A short trail of what happened to the network links and to free memory, kept in the part of memory that a crash or a restart
// does not clear (only switching off does). The debug log is lost in a crash, which is exactly when the question "why did it
// leave the router, and what did it do about it" comes up: GET /api/nettrail answers it after the vario is back.
namespace nettrail {

enum Code : uint8_t {
  Boot = 1,   // arg: the reset reason (esp_reset_reason)
  StaUp,      // joined the maintenance network
  StaDown,    // the link to it dropped; arg: the driver's reason code
  GaveUp,     // stopped looking for it
  Retry,      // looking for it again
  ApJoin,     // a phone joined the vario's own network; arg: how many are joined now
  ApLeave,
  LowMem,     // a web request was refused for lack of memory; arg: the largest free block
  Deaf,       // joined to the maintenance network but the router no longer answers: joining again
};

void begin(int resetReason);
void add(Code code, int arg = 0);  // safe to call from the Wi-Fi event callback
void print(Print& out);

}  // namespace nettrail
