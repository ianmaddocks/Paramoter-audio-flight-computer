#pragma once
// The geometry of the safe flying areas: plain functions on polygons in a local flat frame (metres east and north, the pilot at the
// origin), with no dependence on the rest of the firmware so they can be tested on a PC (test/host/safearea_geo_test.cpp).
#include <math.h>

namespace safearea {

struct Pt {
  float e, n;
};

constexpr float kPi = 3.14159265f;

// Even-odd rule: is (x, y) inside the polygon?
inline bool contains(const Pt* p, int cnt, float x, float y) {
  bool in = false;
  for (int i = 0, j = cnt - 1; i < cnt; j = i++) {
    if (((p[i].n > y) != (p[j].n > y)) && (x < (p[j].e - p[i].e) * (y - p[i].n) / (p[j].n - p[i].n) + p[i].e)) in = !in;
  }
  return in;
}

struct Near {
  float dist = 1e30f;  // to the closest point on the boundary
  Pt at = {0, 0};      // that point
  int edge = -1;       // the edge it is on (edge i runs from point i to point i + 1)
};

inline Near nearest(const Pt* p, int cnt, float x, float y) {
  Near best;
  for (int i = 0; i < cnt; ++i) {
    const Pt a = p[i], b = p[(i + 1) % cnt];
    const float ex = b.e - a.e, ey = b.n - a.n, l2 = ex * ex + ey * ey;
    float t = l2 > 0 ? ((x - a.e) * ex + (y - a.n) * ey) / l2 : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    const float cx = a.e + t * ex, cy = a.n + t * ey;
    const float d = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy));
    if (d < best.dist) {
      best.dist = d;
      best.at = {cx, cy};
      best.edge = i;
    }
  }
  return best;
}

// How far along a straight line from (x, y) in the direction of the unit vector (de, dn) the boundary is first crossed, up to
// maxM. -1 when it is not crossed within that distance.
inline float rayHit(const Pt* p, int cnt, float x, float y, float de, float dn, float maxM, int* edge = nullptr) {
  float best = -1;
  for (int i = 0; i < cnt; ++i) {
    const Pt a = p[i], b = p[(i + 1) % cnt];
    const float ex = b.e - a.e, ey = b.n - a.n;
    const float denom = de * ey - dn * ex;
    if (fabsf(denom) < 1e-6f) continue;  // parallel
    const float ae = a.e - x, an = a.n - y;
    const float t = (ae * ey - an * ex) / denom;
    const float u = (ae * dn - an * de) / denom;
    if (t >= 0 && u >= 0 && u <= 1 && t <= maxM && (best < 0 || t < best)) {
      best = t;
      if (edge) *edge = i;
    }
  }
  return best;
}

// Unit vector (east, north) of a compass bearing.
inline Pt unit(float bearingDeg) {
  const float r = bearingDeg * kPi / 180.0f;
  return {sinf(r), cosf(r)};
}

inline float bearingOf(float e, float n) {
  float d = atan2f(e, n) * 180.0f / kPi;
  return d < 0 ? d + 360.0f : d;
}

inline float wrap180(float d) {
  while (d > 180.0f) d -= 360.0f;
  while (d < -180.0f) d += 360.0f;
  return d;
}

// The heading to turn to, from track `trackDeg`, for a pilot outside the polygon (at the origin) whose track runs into it: the smallest
// turn, either way, to a heading from which a straight line never enters the polygon, with 3 degrees to spare either side so the track
// passes clear of the corner and not along it. The turn is a whole number of clock positions (30 degrees, 60, ... 180): that is how it is
// spoken ("turn right to 2 o'clock"), so the heading the pilot takes is the one that was tested. A tie turns right.
inline float avoidHeading(const Pt* p, int cnt, float trackDeg) {
  auto clear = [&](float h) {
    const Pt d = unit(h);
    return rayHit(p, cnt, 0, 0, d.e, d.n, 1e5f) < 0;
  };
  for (int step = 1; step <= 6; ++step) {  // clock positions out to a U-turn
    const float turn = 30.0f * step;
    const bool right = clear(trackDeg + turn - 3.0f) && clear(trackDeg + turn) && clear(trackDeg + turn + 3.0f);
    const bool left = clear(trackDeg - turn - 3.0f) && clear(trackDeg - turn) && clear(trackDeg - turn + 3.0f);
    if (right || left) return right ? trackDeg + turn : trackDeg - turn;
  }
  return trackDeg + 180.0f;
}

// Area of a polygon, m^2 (always positive).
inline float areaM2(const Pt* p, int cnt) {
  float s = 0;
  for (int i = 0; i < cnt; ++i) {
    const Pt a = p[i], b = p[(i + 1) % cnt];
    s += a.e * b.n - b.e * a.n;
  }
  return fabsf(s) * 0.5f;
}

}  // namespace safearea
