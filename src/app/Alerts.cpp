#include "Alerts.h"

#include <math.h>

#include "Hardware.h"
#include "Settings.h"
#include "audio/Speech.h"

using audio::Priority;

namespace alerts {
namespace {

constexpr uint32_t kGpsLostCallMs = 5000;
constexpr uint32_t kBattLowHoldMs = 10000;  // below the threshold this long before the first call
constexpr uint32_t kBattLowRepeatMs = 5 * 60 * 1000;
constexpr float kBattPresentV = 2.5f;  // a reading under this is no battery measurement (USB power, nothing connected)
constexpr uint32_t kBattUrgentHoldMs = 5000;   // critical, or the reading lost in flight: this long before the call
constexpr uint32_t kBattUrgentRepeatMs = 60 * 1000;

bool hadFix = false;
uint32_t fixLostMs = 0;
bool fixLostCalled = false;
uint32_t battLowSinceMs = 0;
uint32_t lastBattCallMs = 0;
bool battSeen = false;  // a real battery reading (>= kBattPresentV) has been seen since power-on
uint32_t battUrgentSinceMs = 0;
uint32_t lastBattUrgentMs = 0;

void checkGps(uint32_t now, const NavState& nav) {
  if (nav.fix) {
    if (!hadFix) speech::say(Priority::Normal, "gps_fix");
    hadFix = true;
    fixLostMs = 0;
    fixLostCalled = false;
    return;
  }
  if (!hadFix || !nav.flying) return;
  if (!fixLostMs) fixLostMs = now;
  if (!fixLostCalled && now - fixLostMs >= kGpsLostCallMs) {
    fixLostCalled = true;
    speech::say(Priority::High, "no_gps");
  }
}

// Low: under the warning voltage for 10 s, one call, repeated every 5 minutes. Critical (F5): under the critical
// voltage for 5 s, an urgent call every minute. A reading under 2.5 V is not a measurement: with no battery reading
// seen since power-on it is USB power or nothing connected and stays silent, but once a real reading has been
// seen, losing it IN FLIGHT (the voltage collapsing, a wire off the divider) is a fault and is called, urgently,
// every minute, because the vario cannot say how much battery is left.
void checkBattery(uint32_t now, float volts, bool flying) {
  const bool present = volts >= kBattPresentV;
  if (present) battSeen = true;
  const float critV = fminf(g_cfg.battCriticalV, g_cfg.battLowV);
  const bool urgent = present ? volts < critV : (battSeen && flying);
  if (urgent) {
    battLowSinceMs = 0;
    if (!battUrgentSinceMs) battUrgentSinceMs = now;
    if (now - battUrgentSinceMs >= kBattUrgentHoldMs && (!lastBattUrgentMs || now - lastBattUrgentMs >= kBattUrgentRepeatMs)) {
      lastBattUrgentMs = now;
      speech::say(Priority::Urgent, "battery_low");
    }
    return;
  }
  battUrgentSinceMs = 0;
  lastBattUrgentMs = 0;
  if (!present || volts >= g_cfg.battLowV) {
    battLowSinceMs = 0;
    return;
  }
  if (!battLowSinceMs) battLowSinceMs = now;
  if (now - battLowSinceMs >= kBattLowHoldMs && (!lastBattCallMs || now - lastBattCallMs >= kBattLowRepeatMs)) {
    lastBattCallMs = now;
    speech::say(Priority::Normal, "battery_low");
  }
}

}  // namespace

void update(uint32_t nowMs, const NavState& nav, float batteryVolts) {
  if (hw::hasGps()) checkGps(nowMs, nav);
  checkBattery(nowMs, batteryVolts, nav.flying);
}

void reset() {
  hadFix = false;
  fixLostMs = 0;
  fixLostCalled = false;
  battLowSinceMs = 0;
  lastBattCallMs = 0;
  battSeen = false;
  battUrgentSinceMs = 0;
  lastBattUrgentMs = 0;
}

}  // namespace alerts
