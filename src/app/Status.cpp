#include "Status.h"

#include <math.h>

#include "audio/Speech.h"
#include "nav/Geo.h"

using audio::Priority;
using audio::Utterance;

namespace status {

void tell(const NavState& nav, const Wind& wind, int batteryPct, int batteryMinutes, bool hasGps) {
  const bool moving = nav.fix && nav.speedOk && nav.speedMs >= 3.0f;
  Utterance a(Priority::High);
  speech::addHeight(a, nav.aglM);
  if (hasGps && nav.fix && nav.lzValid) {
    const float dist = geo::distanceM(nav.lat, nav.lon, nav.lzLat, nav.lzLon);
    const float brg = geo::bearingDeg(nav.lat, nav.lon, nav.lzLat, nav.lzLon);
    a.add("landing_zone");
    if (moving) speech::addClock(a, brg - nav.trackDeg);
    speech::addDistance(a, dist);
  } else if (hasGps && !nav.lzValid) {
    a.add("no_landing_zone");
  }
  audio::say(a);

  Utterance b(Priority::High);
  if (hasGps && wind.valid()) {
    b.add("wind");
    if (moving && wind.speedMs() >= 1.0f) speech::addClock(b, wind.fromDeg() - nav.trackDeg);
    speech::addSpeed(b, wind.speedMs());
  }
  b.add("battery");
  if (batteryMinutes >= 0) {
    speech::addDuration(b, (uint32_t)batteryMinutes * 60);
  } else {
    speech::addNumber(b, batteryPct);
    b.add("percent");
  }
  audio::say(b);
}

}  // namespace status
