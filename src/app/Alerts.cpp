#include "Alerts.h"

#include "Hardware.h"
#include "Settings.h"
#include "audio/Speech.h"

using audio::Priority;

namespace alerts {
namespace {

constexpr uint32_t kGpsLostCallMs = 5000;
constexpr uint32_t kBattLowHoldMs = 10000;  // below the threshold this long before the first call
constexpr uint32_t kBattLowRepeatMs = 5 * 60 * 1000;

bool hadFix = false;
uint32_t fixLostMs = 0;
bool fixLostCalled = false;
uint32_t battLowSinceMs = 0;
uint32_t lastBattCallMs = 0;

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

void checkBattery(uint32_t now, float volts) {
  if (volts < 2.5f || volts >= g_cfg.battLowV) {  // < 2.5 V: running on USB / not connected
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
  checkBattery(nowMs, batteryVolts);
}

void reset() {
  hadFix = false;
  fixLostMs = 0;
  fixLostCalled = false;
  battLowSinceMs = 0;
  lastBattCallMs = 0;
}

}  // namespace alerts
