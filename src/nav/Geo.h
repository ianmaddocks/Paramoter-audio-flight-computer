#pragma once
#include <math.h>

namespace geo {

constexpr double kEarthR = 6371000.0;
constexpr double kDegToRad = M_PI / 180.0;
constexpr float kRadToDegF = 180.0f / (float)M_PI;
constexpr float kDegToRadF = (float)M_PI / 180.0f;

inline float wrap360(float d) {
  d = fmodf(d, 360.0f);
  return d < 0.0f ? d + 360.0f : d;
}

inline float wrap180(float d) {
  d = wrap360(d);
  return d > 180.0f ? d - 360.0f : d;
}

inline double distanceM(double lat1, double lon1, double lat2, double lon2) {
  double dLat = (lat2 - lat1) * kDegToRad;
  double dLon = (lon2 - lon1) * kDegToRad;
  double a = sin(dLat / 2) * sin(dLat / 2) +
             cos(lat1 * kDegToRad) * cos(lat2 * kDegToRad) * sin(dLon / 2) * sin(dLon / 2);
  return 2.0 * kEarthR * atan2(sqrt(a), sqrt(1.0 - a));
}

inline float bearingDeg(double lat1, double lon1, double lat2, double lon2) {
  double p1 = lat1 * kDegToRad, p2 = lat2 * kDegToRad;
  double dLon = (lon2 - lon1) * kDegToRad;
  double y = sin(dLon) * cos(p2);
  double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dLon);
  return wrap360((float)(atan2(y, x) / kDegToRad));
}

// Flat-earth east/north offset in metres of (lat, lon) from origin; fine within a few km.
struct EN {
  float e;
  float n;
};

inline EN toLocal(double lat0, double lon0, double lat, double lon) {
  return {(float)((lon - lon0) * kDegToRad * kEarthR * cos(lat0 * kDegToRad)),
          (float)((lat - lat0) * kDegToRad * kEarthR)};
}

// Compass bearing of an east/north vector.
inline float vecBearing(float e, float n) { return wrap360(atan2f(e, n) * kRadToDegF); }

// Relative angle (0 = dead ahead, clockwise) to clock position 1..12.
inline int clockFace(float relDeg) {
  int c = (int)lroundf(wrap360(relDeg) / 30.0f) % 12;
  return c == 0 ? 12 : c;
}

}  // namespace geo
