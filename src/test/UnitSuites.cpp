// Software-level unit tests of production maths, run on the vario by the test build (see UnitTest.h):
//   geo      - angle wrapping, distance, bearings, local coordinates, clock positions
//   wind     - the circling wind fit (accuracy, turn direction, 0/360, rejections, averaging) and the glide
//              ratio over the ground for head, tail and cross winds
//   circuit  - the landing circuit state machine driven by scripted positions (leg order, turn points,
//              no early or skipped legs, overshoot, wrong way)
//   battery  - ADC millivolts to volts, smoothing, the percentage curve, the low-battery call
// They use their own inputs. Settings that the code under test reads from g_cfg are changed for the
// duration of a suite and put back afterwards.
#include "test/UnitTest.h"

#if PV_TEST

#include <math.h>

#include "Settings.h"
#include "app/Alerts.h"
#include "audio/Audio.h"
#include "flight/Approach.h"
#include "flight/Wind.h"
#include "nav/Geo.h"
#include "nav/Nav.h"
#include "sensors/Battery.h"

namespace {

struct CfgGuard {
  Settings saved;
  CfgGuard() : saved(g_cfg) {}
  ~CfgGuard() { g_cfg = saved; }
};

float wrapDiff(float a, float b) { return fabsf(geo::wrap180(a - b)); }

}  // namespace

// ---------------------------------------------------------------------------------------------- geo
void geoUnitSuite(UnitReport& r) {
  using namespace geo;
  r.near(wrap360(0), 0, 1e-4f, "wrap360(0)");
  r.near(wrap360(360), 0, 1e-3f, "wrap360(360)");
  r.near(wrap360(-1), 359, 1e-3f, "wrap360(-1)");
  r.near(wrap360(725), 5, 1e-3f, "wrap360(725)");
  r.near(wrap360(-725), 355, 1e-3f, "wrap360(-725)");
  r.near(wrap180(190), -170, 1e-3f, "wrap180(190)");
  r.near(wrap180(-190), 170, 1e-3f, "wrap180(-190)");
  r.near(wrap180(180), 180, 1e-3f, "wrap180(180)");
  r.near(wrap180(360), 0, 1e-3f, "wrap180(360)");
  r.near(wrap180(-720.5f), -0.5f, 1e-3f, "wrap180(-720.5)");

  // distance: one degree of latitude, one degree of longitude at the equator and at 60 N, symmetry, zero
  r.near((float)distanceM(51, 0, 52, 0), 111195.0f, 120.0f, "1 deg of latitude");
  r.near((float)distanceM(0, 10, 0, 11), 111195.0f, 120.0f, "1 deg of longitude at the equator");
  r.near((float)distanceM(60, 10, 60, 11), 55597.0f, 100.0f, "1 deg of longitude at 60 N");
  r.near((float)distanceM(51.5, 0.5, 51.6, 0.7), (float)distanceM(51.6, 0.7, 51.5, 0.5), 0.01f, "distance symmetric");
  r.near((float)distanceM(51.5, 0.5, 51.5, 0.5), 0, 0.001f, "distance to itself");
  r.near((float)distanceM(0, 179.5, 0, -179.5), 111195.0f, 120.0f, "across the date line");

  // bearings
  r.near(bearingDeg(51, 0, 52, 0), 0, 0.01f, "bearing north");
  r.near(bearingDeg(0, 0, 0, 1), 90, 0.01f, "bearing east");
  r.near(bearingDeg(52, 0, 51, 0), 180, 0.01f, "bearing south");
  r.near(bearingDeg(0, 1, 0, 0), 270, 0.01f, "bearing west");
  r.near(bearingDeg(0, 0, 1, 1), 45, 0.1f, "bearing north-east");
  r.near(bearingDeg(0, 0, 1e-5, -1e-9) , 359.9999f, 0.01f, "bearing just west of north (wraps to 360)");

  // flat-earth offsets agree with the distance for short ranges, with the right signs
  EN p = toLocal(51.5, 0.5, 51.501, 0.502);
  r.check(p.e > 0 && p.n > 0, "toLocal signs: e %.1f n %.1f", p.e, p.n);
  r.near(hypotf(p.e, p.n), (float)distanceM(51.5, 0.5, 51.501, 0.502), 0.3f, "toLocal vs distance");
  p = toLocal(51.5, 0.5, 51.499, 0.498);
  r.check(p.e < 0 && p.n < 0, "toLocal signs south-west: e %.1f n %.1f", p.e, p.n);

  // bearing of a vector: the four quadrants and the wrap
  r.near(vecBearing(0, 1), 0, 1e-3f, "vecBearing north");
  r.near(vecBearing(1, 0), 90, 1e-3f, "vecBearing east");
  r.near(vecBearing(0, -1), 180, 1e-3f, "vecBearing south");
  r.near(vecBearing(-1, 0), 270, 1e-3f, "vecBearing west");
  r.near(vecBearing(-1e-4f, 1), 359.99f, 0.01f, "vecBearing just west of north");
  r.near(vecBearing(1, 1), 45, 1e-3f, "vecBearing north-east");
  r.near(vecBearing(-1, -1), 225, 1e-3f, "vecBearing south-west");

  // clock positions: ahead is 12, right is 3, behind 6, left 9, and the wrap at the nose
  r.check(clockFace(0) == 12, "clock 0 -> %d", clockFace(0));
  r.check(clockFace(90) == 3, "clock 90 -> %d", clockFace(90));
  r.check(clockFace(180) == 6, "clock 180 -> %d", clockFace(180));
  r.check(clockFace(270) == 9, "clock 270 -> %d", clockFace(270));
  r.check(clockFace(-90) == 9, "clock -90 -> %d", clockFace(-90));
  r.check(clockFace(14.9f) == 12 && clockFace(15.1f) == 1, "clock boundary at 15 deg: %d %d", clockFace(14.9f), clockFace(15.1f));
  r.check(clockFace(359) == 12, "clock 359 -> %d", clockFace(359));
}

// ---------------------------------------------------------------------------------------------- wind
namespace {

// Ground velocity of an aircraft at constant airspeed turning at turnDegS in a wind from windFrom,
// sampled every 500 ms (the estimator's rate), fed into the estimator.
struct WindRig {
  Wind w;
  uint32_t ms = 10000;
  float hdg = 0;
  uint32_t seed = 99;
  void fly(float seconds, float air, float windFrom, float windSpd, float turnDegS, float noise = 0, bool flying = true,
           bool fix = true) {
    const float wt = (windFrom + 180.0f) * geo::kDegToRadF;
    const int steps = (int)(seconds / 0.25f);
    for (int i = 0; i < steps; ++i) {  // called every 250 ms; the estimator samples every 500 ms
      hdg = geo::wrap360(hdg + turnDegS * 0.25f);
      float h = hdg * geo::kDegToRadF;
      float e = air * sinf(h) + windSpd * sinf(wt), n = air * cosf(h) + windSpd * cosf(wt);
      if (noise > 0) {
        seed = seed * 1664525u + 1013904223u;
        e += ((seed >> 8) / 8388608.0f - 1.0f) * noise;
        seed = seed * 1664525u + 1013904223u;
        n += ((seed >> 8) / 8388608.0f - 1.0f) * noise;
      }
      NavState nav;
      nav.ms = ms += 250;
      nav.fix = fix;
      nav.flying = flying;
      nav.speedMs = hypotf(e, n);
      nav.trackDeg = geo::vecBearing(e, n);
      w.update(nav);
    }
  }
};

}  // namespace

void windUnitSuite(UnitReport& r) {
  CfgGuard guard;
  g_cfg.windManual = false;
  char what[96];

  // accuracy, either direction of turn, across the compass, including the 0/360 wrap
  struct C {
    float from, spd, turn;
  };
  const C cases[] = {{250, 5, 18}, {250, 5, -18}, {0.5f, 6, 18}, {359.5f, 6, -18}, {90, 8, 15}, {180, 3, -20}, {45, 0.5f, 18}};
  for (const C& c : cases) {
    WindRig rig;
    rig.fly(40, 10, c.from, c.spd, c.turn);
    snprintf(what, sizeof what, "wind %.1f from %.1f, turn %.0f", c.spd, c.from, c.turn);
    r.check(rig.w.source() == WindSource::Circling, "%s: no fit (source %s)", what, rig.w.sourceName());
    r.near(rig.w.speedMs(), c.spd, 0.05f, what);
    if (c.spd >= 1.0f) r.check(wrapDiff(rig.w.fromDeg(), c.from) < 0.5f, "%s: from %.1f", what, rig.w.fromDeg());
    r.near(rig.w.airspeedMs(), 10.0f, 0.05f, "airspeed from the circle's radius");
  }
  { // no wind at all
    WindRig rig;
    rig.fly(40, 10, 123, 0, 18);
    r.check(rig.w.source() == WindSource::Circling && rig.w.speedMs() < 0.05f, "zero wind: %.2f m/s", rig.w.speedMs());
  }
  { // noisy GPS velocity (+-0.3 m/s on each axis)
    WindRig rig;
    rig.fly(60, 10, 250, 5, 18, 0.3f);
    r.check(rig.w.source() == WindSource::Circling, "noisy: no fit");
    r.near(rig.w.speedMs(), 5, 0.4f, "noisy wind speed");
    r.check(wrapDiff(rig.w.fromDeg(), 250) < 6.0f, "noisy wind from %.1f", rig.w.fromDeg());
  }

  // not enough data: nothing is estimated, and the takeoff estimate is kept
  const char* none[] = {"straight 60 s", "half circle", "10 s of circle", "slow turn (3 deg/s, 60 s)", "not flying", "no fix",
                        "ground speed under 3 m/s"};
  for (int k = 0; k < 7; ++k) {
    WindRig rig;
    rig.w.onTakeoff(true, 77);
    switch (k) {
      case 0: rig.fly(60, 10, 250, 5, 0); break;
      case 1: rig.fly(10, 10, 250, 5, 18); rig.fly(30, 10, 250, 5, 0); break;
      case 2: rig.fly(10, 10, 250, 5, 18); break;
      case 3: rig.fly(60, 10, 250, 5, 3); break;
      case 4: rig.fly(40, 10, 250, 5, 18, 0, false); break;
      case 5: rig.fly(40, 10, 250, 5, 18, 0, true, false); break;
      case 6: rig.fly(40, 2.5f, 250, 0, 18); break;
    }
    r.check(rig.w.source() == WindSource::Takeoff && wrapDiff(rig.w.fromDeg(), 77) < 0.01f, "%s: estimate changed to %s %.1f", none[k],
            rig.w.sourceName(), rig.w.fromDeg());
  }

  // data that is refused: airspeed under 5 m/s, a wind over 15 m/s
  { WindRig rig; rig.fly(40, 4, 250, 0, 18); r.check(rig.w.source() == WindSource::None, "airspeed 4 m/s accepted"); }
  { WindRig rig; rig.fly(40, 20, 250, 16, 18); r.check(rig.w.source() == WindSource::None, "16 m/s of wind accepted"); }
  { WindRig rig; rig.fly(40, 10, 250, 12, 18); r.check(rig.w.source() == WindSource::None, "wind over the airspeed (no circle round the origin) accepted"); }

  // each new fit is averaged with the estimate before it
  {
    WindRig rig;
    rig.fly(25, 10, 270, 5, 18);
    r.near(rig.w.speedMs(), 5, 0.05f, "first fit");
    rig.w.onTakeoff(false, 0);  // drop the samples gathered since the fit (they are still the old wind: F2)
    const float before = rig.w.speedMs();
    float after = before;
    for (int i = 0; i < 60 && fabsf(after - before) < 0.01f && wrapDiff(rig.w.fromDeg(), 270) < 0.01f; ++i) {
      rig.fly(1, 10, 90, 5, 18);  // the opposite wind, until the next fit
      after = rig.w.speedMs();
    }
    r.check(rig.w.speedMs() < 0.3f, "first fit in the opposite wind: %.2f m/s from %.0f, expected the mean of the two (about 0)",
            rig.w.speedMs(), rig.w.fromDeg());
  }

  // takeoff run: the course is the direction the wind comes from, with no speed
  {
    Wind w;
    r.check(!w.valid(), "a new estimator is valid");
    w.onTakeoff(false, 10);
    r.check(!w.valid(), "takeoff without a course made the estimate valid");
    w.onTakeoff(true, 123);
    r.check(w.valid() && w.source() == WindSource::Takeoff && wrapDiff(w.fromDeg(), 123) < 0.01f && w.speedMs() == 0, "takeoff estimate");
  }

  // glide over the ground: head, tail, cross, and the floor when the wind beats the airspeed
  g_cfg.windManual = true;
  g_cfg.trimAirspeedMs = 10.0f;
  g_cfg.glideRatio = 5.0f;
  g_cfg.windFromDeg = 0;
  g_cfg.windSpeedMs = 5;
  {
    Wind w;
    r.near(w.groundGlide(0), 2.5f, 0.001f, "glide into a 5 m/s wind (half the ground speed)");
    r.near(w.groundGlide(180), 7.5f, 0.001f, "glide with the wind");
    r.near(w.groundGlide(90), 5.0f, 0.001f, "glide across the wind from the left");
    r.near(w.groundGlide(270), 5.0f, 0.001f, "glide across the wind from the right");
    r.near(w.groundGlide(360), 2.5f, 0.001f, "glide at track 360 = 0");
    r.check(w.source() == WindSource::Manual && w.valid(), "manual wind source");
    g_cfg.windSpeedMs = 20;
    r.near(w.groundGlide(0), 0.5f, 0.001f, "glide against a wind over the airspeed (floor of 1 m/s over the ground)");
    g_cfg.windFromDeg = 90;
    g_cfg.windSpeedMs = 5;
    r.near(w.groundGlide(90), 2.5f, 0.001f, "head wind from 90");
    r.near(w.groundGlide(270), 7.5f, 0.001f, "tail wind from 90");
    r.near(w.fromDeg(), 90, 0.001f, "manual from");
  }
}

// ---------------------------------------------------------------------------------------------- circuit
namespace {

constexpr double kLzLat = 51.5, kLzLon = 0.5;

// Position in the circuit frame (origin at the LZ, x into wind, y to the right) to latitude/longitude.
void frameToLatLon(float x, float y, float upDeg, double* lat, double* lon) {
  const float u = upDeg * geo::kDegToRadF;
  const float e = x * sinf(u) + y * cosf(u), n = x * cosf(u) - y * sinf(u);
  *lat = kLzLat + (n / geo::kEarthR) / geo::kDegToRad;
  *lon = kLzLon + (e / (geo::kEarthR * cos(kLzLat * geo::kDegToRad))) / geo::kDegToRad;
}

struct CircuitRig {
  Approach ap;
  Wind wind;
  uint32_t ms = 100000;
  float up;
  CircuitRig(float upDeg) : up(upDeg) {}
  NavState nav(float x, float y, float relTrack, float agl, float gs = 10.0f) {
    NavState n;
    n.ms = ms += 1000;
    n.fix = n.gpsGood = n.speedOk = true;
    n.sats = 10;
    n.hdop = 0.9f;
    frameToLatLon(x, y, up, &n.lat, &n.lon);
    n.speedMs = gs;
    n.trackDeg = geo::wrap360(up + relTrack);
    n.aglM = agl;
    n.lzValid = true;
    n.lzLat = kLzLat;
    n.lzLon = kLzLon;
    n.flying = true;
    n.baroOk = true;
    return n;
  }
  ApPhase step(float x, float y, float relTrack, float agl) {
    NavState n = nav(x, y, relTrack, agl);
    ap.update(n, wind);
    return ap.phase();
  }
};

}  // namespace

void circuitUnitSuite(UnitReport& r) {
  CfgGuard guard;
  audio::testCapture(true);
  g_cfg.windManual = true;
  g_cfg.windSpeedMs = 0;  // no wind: the glide is 5 everywhere, so the expected numbers are exact
  g_cfg.trimAirspeedMs = 10.0f;
  g_cfg.glideRatio = 5.0f;
  g_cfg.finalHeightM = 50.0f;
  g_cfg.minFinalM = 100.0f;
  g_cfg.upwindLegM = 400.0f;
  g_cfg.crosswindLegM = 400.0f;
  g_cfg.downwindLegM = 400.0f;
  g_cfg.arrivalRadiusM = 80.0f;
  g_cfg.turnLeadS = 3.0f;
  g_cfg.offHeadingDeg = 30.0f;
  const float S = 400.0f, lead = 30.0f;  // lead: 10 m/s x 3 s
  const float xCross = 150.0f, xStart = -250.0f;  // final 250 m (50 m x glide 5), downwind leg 400 m, upwind 400 m

  for (int side = -1; side <= 1; side += 2) {
    for (float up : {0.0f, 250.0f}) {
      g_cfg.windFromDeg = up;
      g_cfg.circuitSide = side > 0 ? CS_RIGHT : CS_LEFT;
      CircuitRig c(up);
      char tag[48];
      snprintf(tag, sizeof tag, "%s up %.0f", side > 0 ? "right" : "left", up);

      NavState start = c.nav(xStart - 50, 0, 0, 150);
      r.check(c.ap.start(start, c.wind), "%s: start refused", tag);
      r.check(c.ap.phase() == ApPhase::ToStart, "%s: not in 'to start' after start", tag);

      // fly the legs, one position a second at 10 m/s; the first sample of each phase marks the turn point
      float firstX[7] = {};
      float firstY[7] = {};
      bool seen[7] = {};
      auto note = [&](ApPhase p, float x, float y) {
        if (!seen[(int)p]) { seen[(int)p] = true; firstX[(int)p] = x; firstY[(int)p] = y; }
      };
      ApPhase order[8];
      int nOrder = 0;
      ApPhase last = c.ap.phase();
      auto fly = [&](float x0, float y0, float x1, float y1, float rel, float agl) {
        float len = hypotf(x1 - x0, y1 - y0);
        int n = (int)(len / 10.0f);
        for (int i = 1; i <= n; ++i) {
          float x = x0 + (x1 - x0) * i / n, y = y0 + (y1 - y0) * i / n;
          ApPhase p = c.step(x, y, rel, agl);
          if (p != last) {
            if (nOrder < 8) order[nOrder++] = p;
            note(p, x, y);
            last = p;
          }
        }
      };
      fly(xStart - 50, 0, xStart, 0, 0, 150);          // arrives at the start point: upwind begins
      fly(xStart, 0, xCross + 20, 0, 0, 150);           // upwind leg (and a little past the crosswind point)
      fly(xCross + 20, 0, xCross + 20, side * (S + 20), 90.0f * side, 150);  // crosswind
      fly(xCross + 20, side * (S + 20), -330, side * S, 180, 150);            // downwind, 150 m up
      fly(-330, side * S, -330, 0, -90.0f * side, 140);                      // base (high enough to fly it all)
      fly(-330, 0, 0, 0, 0, 60);                                             // final to the field

      const ApPhase want[] = {ApPhase::Upwind, ApPhase::Crosswind, ApPhase::Downwind, ApPhase::Base, ApPhase::Final};
      bool okOrder = nOrder >= 5;
      for (int i = 0; i < 5 && okOrder; ++i) okOrder = order[i] == want[i];
      r.check(okOrder, "%s: legs in order upwind, crosswind, downwind, base, final (got %d changes)", tag, nOrder);
      // turn points, from the spec: crosswind at xCross less the lead, downwind at S less the lead,
      // base from the height (150 m: 150 - 400/5 = 70 m left at the end of base, 350 m of final, base turn at x = -350 + lead),
      // final at y within the lead of the line
      r.near(firstX[(int)ApPhase::Crosswind], xCross - lead, 10.5f, "crosswind turn x");
      r.near(side * firstY[(int)ApPhase::Downwind], S - lead, 10.5f, "downwind turn y");
      r.near(firstX[(int)ApPhase::Base], -350.0f + lead, 10.5f, "base turn x");
      r.check(side * firstY[(int)ApPhase::Final] <= lead + 0.5f && side * firstY[(int)ApPhase::Final] >= lead - 10.5f,
              "%s: final turn at y %.0f, expected about %.0f", tag, firstY[(int)ApPhase::Final], side * lead);
      // the upwind leg only began at the start point
      r.near(firstX[(int)ApPhase::Upwind], xStart - 50 + 10.0f * (int)((xStart - (xStart - 50)) / 10.0f), 80.0f, "upwind begins near the start");
    }
  }

  // the base turn follows the height: lower means a shorter final, clamped between the minimum final and the longest
  for (float agl : {110.0f, 90.0f, 60.0f, 200.0f, 300.0f}) {
    g_cfg.windFromDeg = 0;
    g_cfg.circuitSide = CS_RIGHT;
    CircuitRig c(0);
    c.ap.start(c.nav(xStart - 50, 0, 0, agl), c.wind);
    ApPhase p = ApPhase::ToStart;
    float turnX = 0;
    c.step(xStart, 0, 0, agl);  // arrive
    for (float x = xStart; x <= xCross + 20; x += 10) c.step(x, 0, 0, agl);
    for (float y = 0; y <= S + 20; y += 10) c.step(xCross + 20, y, 90, agl);
    for (float x = xCross + 20; x >= -700 && p != ApPhase::Base; x -= 10) {
      p = c.step(x, S, 180, agl);
      turnX = x;
    }
    float hFinal = agl - S / 5.0f;
    float xb = fminf(fmaxf(hFinal * 5.0f, 100.0f), 250.0f + 400.0f);
    char what[48];
    snprintf(what, sizeof what, "base turn x at %.0f m up", agl);
    r.check(p == ApPhase::Base, "%s: never turned onto base", what);
    r.near(turnX, -xb + lead, 10.5f, what);
  }

  // overshoot: carried on past the crosswind point the circuit does not move on; it waits for the crosswind leg
  {
    g_cfg.windFromDeg = 0;
    g_cfg.circuitSide = CS_RIGHT;
    CircuitRig c(0);
    c.ap.start(c.nav(xStart, 0, 0, 150), c.wind);
    ApPhase p = ApPhase::ToStart;
    for (float x = xStart; x <= 600; x += 10) p = c.step(x, 0, 0, 150);
    r.check(p == ApPhase::Crosswind, "overshoot: phase %s after flying 450 m past the turn", c.ap.phaseName());
    for (int i = 0; i < 20; ++i) p = c.step(600 + i * 10, 0, 0, 150);
    r.check(p == ApPhase::Crosswind, "overshoot continued: phase %s (must not skip to downwind with y = 0)", c.ap.phaseName());
  }

  // wrong way: flying away from the start point does not start the first leg
  {
    g_cfg.windFromDeg = 0;
    CircuitRig c(0);
    c.ap.start(c.nav(500, 600, 0, 150), c.wind);
    ApPhase p = ApPhase::ToStart;
    for (int i = 0; i < 40; ++i) p = c.step(500 + i * 10, 600 + i * 10, 45, 150);
    r.check(p == ApPhase::ToStart, "wrong way: phase %s while flying away from the start", c.ap.phaseName());
  }

  // refusals: no fix, no landing zone, no wind
  {
    CircuitRig c(0);
    NavState n = c.nav(0, 0, 0, 100);
    n.fix = false;
    r.check(!c.ap.start(n, c.wind), "approach started without a fix");
    n = c.nav(0, 0, 0, 100);
    n.lzValid = false;
    r.check(!c.ap.start(n, c.wind), "approach started without a landing zone");
    g_cfg.windManual = false;
    n = c.nav(0, 0, 0, 100);
    r.check(!c.ap.start(n, c.wind), "approach started without a wind");
  }
  audio::testCapture(false);
}

// ---------------------------------------------------------------------------------------------- battery
void batteryUnitSuite(UnitReport& r) {
  CfgGuard guard;
  audio::testCapture(true);
  g_cfg.battDivider = 2.0f;
  g_cfg.battCal = 1.0f;
  r.near(batteryVoltsFromMv(2000), 4.0f, 1e-4f, "2000 mV through a divider of 2");
  g_cfg.battCal = 0.999f;
  r.near(batteryVoltsFromMv(2050), 4.0959f, 1e-3f, "calibration 0.999");
  g_cfg.battDivider = 3.0f;
  r.near(batteryVoltsFromMv(1000), 2.997f, 1e-3f, "divider 3");
  r.near(batteryVoltsFromMv(0), 0, 1e-6f, "0 mV");
  g_cfg.battDivider = 2.0f;

  r.near(batteryFilter(0, 4.0f), 4.0f, 1e-6f, "first reading taken as it is");
  r.near(batteryFilter(4.0f, 3.0f), 3.8f, 1e-5f, "smoothing step of 0.2");
  float x = 4.0f;
  for (int i = 0; i < 40; ++i) x = batteryFilter(x, 3.0f);
  r.near(x, 3.0f, 0.001f, "smoothing converges");

  // percentage curve: anchor points, never above 100, never below 0, rising with the voltage
  g_cfg.battFullV = 4.09f;
  const float pts[][2] = {{4.20f, 100}, {4.09f, 100}, {4.00f, 80}, {3.90f, 65}, {3.80f, 50}, {3.70f, 30}, {3.60f, 12}, {3.50f, 5}, {3.30f, 0}, {3.00f, 0}, {0.0f, 0}};
  for (auto& p : pts) {
    char what[40];
    snprintf(what, sizeof what, "curve at %.2f V", p[0]);
    r.near(batteryCurvePct(p[0]), p[1], 0.6f, what);
  }
  float prev = -1;
  bool mono = true;
  for (float v = 3.0f; v <= 4.3f; v += 0.01f) {
    float pc = batteryCurvePct(v);
    if (pc < prev - 1e-3f || pc < 0 || pc > 100.0001f) mono = false;
    prev = pc;
  }
  r.check(mono, "the curve is not monotonic or leaves 0..100");
  g_cfg.battFullV = 4.15f;
  r.near(batteryCurvePct(4.15f), 100, 0.6f, "full voltage setting moves the 100 % point up");
  r.near(batteryCurvePct(4.10f), 93.3f, 0.6f, "and the curve between it and 4.00 V follows");
  g_cfg.battFullV = 3.95f;  // below 4.01 V the 100 % point stays at 4.01 V
  r.near(batteryCurvePct(4.01f), 100, 0.6f, "full voltage setting under 4.01 V is taken as 4.01 V");
  r.check(batteryCurvePct(3.99f) < 100, "full voltage setting under 4.01 V made 3.99 V read 100 %%");
  g_cfg.battFullV = 4.09f;

  // the low-battery call: after 10 s under the threshold, once, again after 5 minutes; none from USB (< 2.5 V),
  // none at or above the threshold; reset by recovering
  g_cfg.battLowV = 3.5f;
  NavState nav;
  alerts::reset();
  uint32_t t = 1000;
  auto run = [&](float volts, uint32_t seconds) {
    for (uint32_t s = 0; s < seconds; ++s) alerts::update(t += 1000, nav, volts);
  };
  int base = audio::testSaid();
  run(3.40f, 9);
  r.check(audio::testSaid() == base, "low battery called before 10 s");
  run(3.40f, 2);
  r.check(audio::testSaid() == base + 1, "low battery not called after 10 s (%d)", audio::testSaid() - base);
  r.check(strstr(audio::testSaidText(base), "battery_low") != nullptr, "call is not battery_low: %s", audio::testSaidText(base));
  run(3.40f, 280);
  r.check(audio::testSaid() == base + 1, "low battery repeated within 5 minutes (%d)", audio::testSaid() - base);
  run(3.40f, 25);
  r.check(audio::testSaid() == base + 2, "low battery not repeated after 5 minutes (%d)", audio::testSaid() - base);
  alerts::reset();
  base = audio::testSaid();
  run(3.50f, 30);
  r.check(audio::testSaid() == base, "called at exactly the threshold (3.50 V, not below)");
  run(3.499f, 12);
  r.check(audio::testSaid() == base + 1, "not called just under the threshold (%d)", audio::testSaid() - base);
  alerts::reset();
  base = audio::testSaid();
  run(2.4f, 60);
  r.check(audio::testSaid() == base, "called for a reading under 2.5 V (USB power)");
  run(0.0f, 60);
  r.check(audio::testSaid() == base, "called for 0 V");
  run(3.40f, 5);
  run(3.60f, 1);  // a recovery restarts the 10 s hold
  run(3.40f, 8);
  r.check(audio::testSaid() == base, "hold not restarted by a reading back over the threshold");
  alerts::reset();
  audio::testCapture(false);
}

#endif
