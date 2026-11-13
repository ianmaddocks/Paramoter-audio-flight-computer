#include "Alerts.h"

#include <math.h>

#include "Hardware.h"
#include "Settings.h"
#include "audio/Speech.h"
#include "nav/Geo.h"

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
uint32_t baroBadSinceMs = 0, baroGoodSinceMs = 0, lastBaroCallMs = 0;
bool baroCalled = false;
constexpr uint32_t kBaroCallHoldMs = 3000;
constexpr uint32_t kBaroRepeatMs = 60 * 1000;
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
// The barometer failing in flight is as bad as it is quiet: the vario tones simply stop, which sounds like "no lift".
// Said once after 3 s, repeated every minute, and "Barometer back" once it has read again for 3 s.
void checkBaro(uint32_t now, const NavState& nav) {
  if (!nav.flying) {
    baroBadSinceMs = baroGoodSinceMs = 0;
    baroCalled = false;
    return;
  }
  if (!nav.baroOk) {
    baroGoodSinceMs = 0;
    if (!baroBadSinceMs) baroBadSinceMs = now;
    if (now - baroBadSinceMs >= kBaroCallHoldMs && (!baroCalled || now - lastBaroCallMs >= kBaroRepeatMs)) {
      baroCalled = true;
      lastBaroCallMs = now;
      speech::say(Priority::Urgent, "baro_failed|~400");
    }
    return;
  }
  baroBadSinceMs = 0;
  if (!baroCalled) return;
  if (!baroGoodSinceMs) baroGoodSinceMs = now;
  if (now - baroGoodSinceMs >= kBaroCallHoldMs) {
    baroCalled = false;
    baroGoodSinceMs = 0;
    speech::say(Priority::Normal, "baro_back|~800");
  }
}

// What a low or critical battery call says: the battery, the minutes left when they are known, how far the landing zone
// is, and (with a GPS and a landing zone, which the vario keeps if it is switched off and on again) advice to switch
// off until help is needed. Every part that is not known is left out.
void callBattery(const char* lead, Priority p, const NavState& nav, int minutes) {
  audio::Utterance u(p);
  u.add(lead);
  if (minutes > 0) {  // (0 minutes left says nothing useful: left out)
    u.add("approximately");
    speech::addDuration(u, (uint32_t)minutes * 60);
    u.add("remaining");
  }
  if (hw::hasGps() && nav.lzValid) {
    if (nav.fix) {
      u.add("landing_zone");
      speech::addDistance(u, geo::distanceM(nav.lat, nav.lon, nav.lzLat, nav.lzLon));
    }
    u.add("turn_me_off");
  }
  audio::say(u);
}

void checkBattery(uint32_t now, float volts, const NavState& nav, int minutes) {
  const bool flying = nav.flying;
  const bool present = volts >= kBattPresentV;
  if (present) battSeen = true;
  const float critV = fminf(g_cfg.battCriticalV, g_cfg.battLowV);
  const bool urgent = present ? volts < critV : (battSeen && flying);
  if (urgent) {
    battLowSinceMs = 0;
    if (!battUrgentSinceMs) battUrgentSinceMs = now;
    if (now - battUrgentSinceMs >= kBattUrgentHoldMs && (!lastBattUrgentMs || now - lastBattUrgentMs >= kBattUrgentRepeatMs)) {
      lastBattUrgentMs = now;
      if (present) callBattery("battery_critical|battery_low", Priority::Urgent, nav, minutes);
      else speech::say(Priority::Urgent, "battery_lost|battery_low");  // the reading vanished: nothing to say about minutes
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
    callBattery("battery_low", Priority::Normal, nav, minutes);
  }
}

}  // namespace

void update(uint32_t nowMs, const NavState& nav, float batteryVolts, int batteryMinutes) {
  if (hw::hasGps()) checkGps(nowMs, nav);
  checkBaro(nowMs, nav);
  checkBattery(nowMs, batteryVolts, nav, batteryMinutes);
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
  baroBadSinceMs = baroGoodSinceMs = lastBaroCallMs = 0;
  baroCalled = false;
}

}  // namespace alerts
