// Software-level unit tests of production maths, run on the vario by the test build (see UnitTest.h):
//   geo      - angle wrapping, distance, bearings, local coordinates, clock positions
//   wind     - the circling wind fit (accuracy, turn direction, 0/360, rejections, averaging) and the glide
//              ratio over the ground for head, tail and cross winds
//   circuit  - the landing circuit state machine driven by scripted positions (leg order, turn points,
//              no early or skipped legs, overshoot, wrong way)
//   baro     - the barometer plausibility checks (impossible pressure, lone outliers, steps, frozen value)
//   rpmtrack - the RPM reading logic: jump confirmation, hold through speech and dropouts
//   flight   - the landing decision (what counts as standing still on the ground)
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
#include "flight/Flight.h"
#include "flight/Wind.h"
#include "nav/Geo.h"
#include "nav/Nav.h"
#include "sensors/Battery.h"
#include "sensors/BaroGuard.h"
#include "sensors/GpsData.h"
#include "sensors/RpmTrack.h"

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

  // F2: samples from before a wind change must not bias the fit, and a real change of wind is followed from
  // the second fit on. Straight flight in the old wind, then circling in the new one: the straight samples lie
  // off the circle and are dropped.
  for (float straightS : {1.0f, 3.0f, 6.0f}) {
    WindRig rig;
    rig.fly(straightS, 10, 270, 5, 0);  // straight (heading 0) in a wind from 270
    rig.fly(60, 10, 90, 5, 18);          // then circling in a wind from 90
    snprintf(what, sizeof what, "%.0f s of old wind, then circling", straightS);
    r.check(rig.w.source() == WindSource::Circling, "%s: no fit", what);
    r.near(rig.w.speedMs(), 5, 0.3f, what);
    r.check(wrapDiff(rig.w.fromDeg(), 90) < 4.0f, "%s: from %.1f, wind from 90", what, rig.w.fromDeg());
  }
  {  // a long stretch of old wind (almost half the window): still a clean circle after it, so the old head is dropped
    WindRig rig;
    rig.fly(14, 10, 270, 5, 0);  // 28 straight samples
    rig.fly(24, 10, 90, 5, 18);  // then 48 circling
    r.check(rig.w.source() == WindSource::Circling, "40 %% old head: no fit");
    r.near(rig.w.speedMs(), 5, 0.3f, "40 % old head: speed");
    r.check(wrapDiff(rig.w.fromDeg(), 90) < 4.0f, "40 %% old head: from %.1f, wind from 90", rig.w.fromDeg());
  }
  {  // old wind at the START of a circle that is itself in the new wind, noise on the velocity: the same
    WindRig rig;
    rig.fly(5, 10, 270, 5, 0, 0.3f);
    rig.fly(50, 10, 90, 5, 18, 0.3f);
    r.check(rig.w.source() == WindSource::Circling, "noisy old head: no fit");
    r.near(rig.w.speedMs(), 5, 0.5f, "noisy old head: speed");
    r.check(wrapDiff(rig.w.fromDeg(), 90) < 6.0f, "noisy old head: from %.1f", rig.w.fromDeg());
  }
  {  // a change of wind in the middle of a circle: nothing believable, so no new estimate
    WindRig rig;
    rig.fly(30, 10, 270, 5, 18);
    r.near(rig.w.speedMs(), 5, 0.1f, "before the change");
    const uint32_t f = rig.w.fitCount();
    rig.fly(7, 10, 90, 5, 18);  // wind flips, 7 s later (a third of a circle)
    r.check(rig.w.fitCount() == f && wrapDiff(rig.w.fromDeg(), 270) < 1.0f && fabsf(rig.w.speedMs() - 5) < 0.1f,
            "a fit within 7 s of a wind flip: now %.1f m/s from %.0f (%u fits, was %u)", rig.w.speedMs(), rig.w.fromDeg(),
            (unsigned)rig.w.fitCount(), (unsigned)f);
  }
  {  // convergence: A, then B. The first fit in B is averaged with A, the second (agreeing with the first) is B.
    auto flyUntilFits = [](WindRig& rig, uint32_t want, float from, float spd, float turn) {
      for (int i = 0; i < 400 && rig.w.fitCount() < want; ++i) rig.fly(0.5f, 10, from, spd, turn);
    };
    WindRig rig;
    flyUntilFits(rig, 1, 270, 5, 18);
    r.check(rig.w.fitCount() == 1, "first fit not made");
    r.near(rig.w.speedMs(), 5, 0.1f, "estimate A");
    flyUntilFits(rig, 2, 90, 5, 18);
    r.check(rig.w.fitCount() == 2, "second fit not made");
    r.check(rig.w.speedMs() < 0.4f, "first fit after the change is averaged with the old estimate: %.2f m/s from %.0f (want about 0)", rig.w.speedMs(), rig.w.fromDeg());
    flyUntilFits(rig, 3, 90, 5, 18);
    r.near(rig.w.speedMs(), 5, 0.3f, "estimate B after the second fit in the new wind");
    r.check(wrapDiff(rig.w.fromDeg(), 90) < 4.0f, "estimate B from %.1f, wind from 90", rig.w.fromDeg());
    flyUntilFits(rig, 4, 90, 5, 18);
    r.near(rig.w.speedMs(), 5, 0.3f, "estimate B stays after a third fit");
  }
  {  // fits that keep disagreeing (A, B, A) are averaged as before: (A + B) / 2, then with A again
    auto flyUntilFits = [](WindRig& rig, uint32_t want, float from, float spd, float turn) {
      for (int i = 0; i < 400 && rig.w.fitCount() < want; ++i) rig.fly(0.5f, 10, from, spd, turn);
    };
    WindRig rig;
    flyUntilFits(rig, 1, 270, 5, 18);
    flyUntilFits(rig, 2, 90, 5, 18);
    flyUntilFits(rig, 3, 270, 5, 18);
    r.near(rig.w.speedMs(), 2.5f, 0.4f, "A, B, A: wind speed");
    r.check(wrapDiff(rig.w.fromDeg(), 270) < 6.0f, "A, B, A: from %.1f, expected 270", rig.w.fromDeg());
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

  // ---- steering calls: only when the pilot is NOT already making the turn that was just called (F6).
  // One sample a second; after the crosswind turn call the track is scripted (relative to upwind) at the
  // crosswind point, where the leg heading is 90 degrees.
  {
    g_cfg.windManual = true;
    g_cfg.windSpeedMs = 0;
    g_cfg.windFromDeg = 0;
    g_cfg.circuitSide = CS_RIGHT;
    // returns the 1-based number of the first sample that was followed by a steering call, or 0
    auto steerAfterTurnCall = [&](const float* rel, int n, int* calls) {
      CircuitRig c(0);
      c.ap.start(c.nav(xStart - 50, 0, 0, 150), c.wind);
      c.step(xStart, 0, 0, 150);
      for (float x = xStart; x < xCross + 20 && c.ap.phase() != ApPhase::Crosswind; x += 10) c.step(x, 0, 0, 150);
      audio::testCapture(true);  // from here: what is said after the turn call
      int first = 0;
      for (int i = 0; i < n; ++i) {
        c.step(xCross, 0, rel[i], 150);
        if (!first && audio::testSaid() > 0) first = i + 1;
      }
      *calls = 0;
      for (int i = 0; i < audio::testSaid(); ++i) *calls += strstr(audio::testSaidText(i), "steer_") != nullptr;
      return first;
    };
    int calls = 0, first = 0;
    {
      const float turn[] = {0, 0, 0, 0, 22, 45, 67, 90, 90, 90, 90, 90, 90};
      first = steerAfterTurnCall(turn, 13, &calls);
      r.check(calls == 0, "slow reaction then a normal turn: %d steering calls, first after sample %d (want none)", calls, first);
    }
    {
      const float turn[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
      first = steerAfterTurnCall(turn, 16, &calls);
      r.check(calls >= 1 && calls <= 2, "turn call ignored for 16 s: %d steering calls (want 1 or 2)", calls);
      r.check(first >= 6 && first <= 12, "turn call ignored: first steering call after sample %d (want 6..12)", first);
    }
    {
      const float turn[] = {0, -15, -30, -45, -60, -75, -90, -90, -90, -90, -90, -90, -90};
      first = steerAfterTurnCall(turn, 13, &calls);
      r.check(first >= 6 && first <= 12, "turned the wrong way: first steering call after sample %d (want 6..12)", first);
    }
    {
      const float turn[] = {0, 0, 10, 25, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40};
      first = steerAfterTurnCall(turn, 15, &calls);
      r.check(first >= 8 && first <= 14, "turn stalled at 40 degrees: first steering call after sample %d (want 8..14)", first);
    }
    {
      const float turn[] = {0, 15, -15, 20, -20, 25, -25, 15, -15, 10, -10, 5, 0};  // hand-flown wobble round the heading... of 90?
      // (the desired heading here is 90 degrees: a track near 0 is 90 off and never converges: this is the
      //  "ignored turn", so only check the opposite: a track within the tolerance of 90 never calls)
      float near90[13];
      for (int i = 0; i < 13; ++i) near90[i] = 90.0f + turn[i];
      first = steerAfterTurnCall(near90, 13, &calls);
      r.check(calls == 0, "track within 30 degrees of the leg heading: %d steering calls", calls);
    }
    {  // a turn that is fast (4 s) is not called either, and one that has gone past 90 and is coming back is a turn the right way
      const float turn[] = {0, 40, 80, 120, 100, 92, 90, 90, 90, 90};
      first = steerAfterTurnCall(turn, 10, &calls);
      r.check(calls == 0, "fast turn with an overshoot: %d steering calls", calls);
    }
    {  // unexpected heading change on a leg, no turn call involved: 60 degrees off for 12 s is called
      CircuitRig c(0);
      c.ap.start(c.nav(xStart - 50, 0, 0, 150), c.wind);
      c.step(xStart, 0, 0, 150);
      audio::testCapture(true);
      for (int i = 0; i < 8; ++i) c.step(xStart + 5, 0, 0, 150);
      r.check(audio::testSaid() == 0, "steered on a straight leg (%s)", audio::testSaidText(0));
      int at = 0;
      for (int i = 0; i < 12; ++i) {
        c.step(xStart + 5, 0, -60, 150);
        if (!at && audio::testSaid() > 0) at = i + 1;
      }
      r.check(at >= 1 && at <= 8 && strstr(audio::testSaidText(0), "steer_") != nullptr,
              "track 60 degrees off for 12 s: first call after sample %d (want 1..8)", at);
    }
    audio::testCapture(false);
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

  // ---- critical battery and a reading lost in flight (F5)
  {
    audio::testCapture(true);  // counts from zero again: only the first 16 utterances keep their text
    g_cfg.battLowV = 3.5f;
    g_cfg.battCriticalV = 3.3f;
    NavState fly, ground;
    fly.flying = true;
    uint32_t tt = 5000000;
    auto runf = [&](float volts, uint32_t seconds, const NavState& nv) {
      for (uint32_t s = 0; s < seconds; ++s) alerts::update(tt += 1000, nv, volts);
    };
    auto said = [&](int from, int i, const char* what) { return strstr(audio::testSaidText(from + i), what) != nullptr; };

    alerts::reset();
    int b = audio::testSaid();
    runf(3.20f, 5, ground);
    r.check(audio::testSaid() == b, "critical battery called before the 5 s hold");
    runf(3.20f, 1, ground);
    r.check(audio::testSaid() == b + 1 && said(b, 0, "[urgent] battery_low"), "critical battery: %d calls, first '%s'", audio::testSaid() - b, audio::testSaidText(b));
    runf(3.20f, 58, ground);
    r.check(audio::testSaid() == b + 1, "critical battery repeated within a minute (%d)", audio::testSaid() - b);
    runf(3.20f, 2, ground);
    r.check(audio::testSaid() == b + 2 && said(b, 1, "[urgent]"), "critical battery not repeated after a minute (%d)", audio::testSaid() - b);
    runf(3.20f, 130, ground);
    r.check(audio::testSaid() == b + 4, "critical battery: %d calls in 3.5 minutes, expected 4", audio::testSaid() - b);

    alerts::reset();  // exactly at the critical voltage is low, not critical: the ordinary call
    b = audio::testSaid();
    runf(3.30f, 30, ground);
    r.check(audio::testSaid() == b + 1 && said(b, 0, "[normal] battery_low"), "3.30 V (at the critical voltage): %d calls, '%s'", audio::testSaid() - b, audio::testSaidText(b));

    alerts::reset();  // a critical voltage set above the warning voltage cannot silence the warning
    g_cfg.battCriticalV = 3.8f;
    b = audio::testSaid();
    runf(3.40f, 7, ground);
    r.check(audio::testSaid() == b + 1 && said(b, 0, "[urgent]"), "critical set above the low voltage: %d calls", audio::testSaid() - b);
    g_cfg.battCriticalV = 3.3f;

    alerts::reset();  // 2.6 V is a measurement (a very flat battery), not USB
    b = audio::testSaid();
    runf(2.6f, 7, ground);
    r.check(audio::testSaid() == b + 1 && said(b, 0, "[urgent]"), "2.6 V on the ground: %d calls, expected 1 urgent", audio::testSaid() - b);

    alerts::reset();  // a real reading, then 0 V on the ground: USB / unplugged, silent
    b = audio::testSaid();
    runf(3.9f, 5, ground);
    runf(0.0f, 120, ground);
    r.check(audio::testSaid() == b, "reading lost on the ground called (%d)", audio::testSaid() - b);

    alerts::reset();  // never a real reading, in the air: no battery fitted, silent
    b = audio::testSaid();
    runf(0.0f, 120, fly);
    r.check(audio::testSaid() == b, "no battery reading ever, in flight, called (%d)", audio::testSaid() - b);

    alerts::reset();  // a real reading, then lost in flight: urgent after 5 s and every minute
    b = audio::testSaid();
    runf(3.9f, 10, fly);
    r.check(audio::testSaid() == b, "a good battery in flight called");
    runf(0.0f, 5, fly);
    r.check(audio::testSaid() == b, "reading lost in flight called before the 5 s hold");
    runf(0.0f, 1, fly);
    r.check(audio::testSaid() == b + 1 && said(b, 0, "[urgent] battery_low"), "reading lost in flight: %d calls, '%s'", audio::testSaid() - b, audio::testSaidText(b));
    runf(0.0f, 60, fly);
    r.check(audio::testSaid() == b + 2, "reading lost in flight not repeated after a minute (%d)", audio::testSaid() - b);
    runf(3.9f, 20, fly);  // back: quiet, and the clocks start again
    r.check(audio::testSaid() == b + 2, "called after the reading returned");
    runf(0.0f, 6, fly);
    r.check(audio::testSaid() == b + 3, "lost again: %d calls, expected 3", audio::testSaid() - b);
  }
  alerts::reset();
  audio::testCapture(false);
}

// ---------------------------------------------------------------------------------------------- flight
// Flight::looksStill(speedOk, speedMs, landingSpeedMs, baroOk, varioMs, aglM, maxAglM)
void flightUnitSuite(UnitReport& r) {
  const float kLand = 1.4f, kMaxAgl = 15.0f;
  auto still = [&](bool speedOk, float gs, bool baroOk, float vz, float agl) {
    return Flight::looksStill(speedOk, gs, kLand, baroOk, vz, agl, kMaxAgl);
  };
  // trusted ground speed decides alone
  r.check(still(true, 0.5f, true, 0, 3), "trusted speed 0.5 m/s on the ground is not still");
  r.check(still(true, 0.5f, true, 0, 300), "trusted speed 0.5 m/s at 300 m (hanging in a headwind) is not still: the speed rule is unchanged");
  r.check(still(true, 0.5f, false, 0, 300), "trusted speed with the barometer lost is not still");
  r.check(!still(true, 3.0f, true, 0, 0), "trusted speed 3 m/s on the ground is still");
  r.check(still(true, 1.39f, true, 5, 0), "speed just under the landing speed is not still");
  r.check(!still(true, 1.4f, true, 0, 0), "speed equal to the landing speed counts as still");
  // F8: a weak fix reads zero speed; it must not mean 'standing still'
  r.check(!still(false, 0.0f, true, 0.0f, 100), "weak fix (speed 0, untrusted) at 100 m level is taken for a landing");
  r.check(!still(false, 0.0f, true, 0.0f, 60), "no GPS at 60 m, steady barometer, is taken for a landing (F1)");
  r.check(!still(false, 0.0f, true, 0.0f, 15.0f), "height exactly at the limit is under it");
  r.check(still(false, 0.0f, true, 0.0f, 14.9f), "no GPS, steady, 14.9 m not still");
  r.check(still(false, 0.0f, true, 0.0f, 0.0f), "no GPS, steady, on the ground not still");
  r.check(still(false, 0.0f, true, 0.0f, -30.0f), "landing lower than the reference (-30 m) not still");
  // vertical speed limit without a speed
  r.check(still(false, 0, true, 0.29f, 2), "0.29 m/s climb on the ground without GPS not still");
  r.check(!still(false, 0, true, 0.3f, 2), "0.3 m/s climb counts as still");
  r.check(!still(false, 0, true, -0.31f, 2), "0.31 m/s sink counts as still");
  // no evidence at all
  r.check(!still(false, 0, false, 0.0f, 0), "no GPS speed and no barometer is taken for a landing");
  r.check(!still(false, 0, false, 0.0f, 300), "no GPS speed and no barometer at height is taken for a landing");
  // the limit follows the setting
  r.check(Flight::looksStill(false, 0, kLand, true, 0, 40.0f, 50.0f), "limit 50 m: 40 m not still");
  r.check(!Flight::looksStill(false, 0, kLand, true, 0, 60.0f, 50.0f), "limit 50 m: 60 m is still");

  // GPS coordinates that cannot be a position are no fix (F5)
  r.check(gpsCoordsValid(51.5, 0.5), "51.5, 0.5 rejected");
  r.check(gpsCoordsValid(-33.9, 151.2) && gpsCoordsValid(40.7, -74.0), "southern / western positions rejected");
  r.check(gpsCoordsValid(90.0, 180.0) && gpsCoordsValid(-90.0, -180.0), "the limits themselves rejected");
  r.check(!gpsCoordsValid(90.0001, 0.5), "latitude 90.0001 accepted");
  r.check(!gpsCoordsValid(-90.0001, 0.5), "latitude -90.0001 accepted");
  r.check(!gpsCoordsValid(95.0, 0.5), "latitude 95 accepted");
  r.check(!gpsCoordsValid(51.5, 180.0001), "longitude 180.0001 accepted");
  r.check(!gpsCoordsValid(51.5, -200.0), "longitude -200 accepted");
  r.check(!gpsCoordsValid(NAN, 0.5) && !gpsCoordsValid(51.5, NAN), "NaN coordinate accepted");
  r.check(!gpsCoordsValid(INFINITY, 0.5) && !gpsCoordsValid(51.5, -INFINITY), "infinite coordinate accepted");
  r.check(!gpsCoordsValid(0.0, 0.0), "0, 0 accepted");
  r.check(gpsCoordsValid(0.0, 0.000001) && gpsCoordsValid(0.000001, 0.0), "a point on the equator or the meridian rejected");
}

// ---------------------------------------------------------------------------------------------- baro
void baroUnitSuite(UnitReport& r) {
  using V = BaroGuard::Verdict;
  r.check(BaroGuard::pressureValid(101325.0f), "sea level pressure invalid");
  r.check(BaroGuard::pressureValid(20000.0f) && BaroGuard::pressureValid(110000.0f), "limits themselves invalid");
  r.check(!BaroGuard::pressureValid(19999.0f), "19999 Pa valid");
  r.check(!BaroGuard::pressureValid(110001.0f), "110001 Pa valid");
  r.check(!BaroGuard::pressureValid(0.0f), "0 Pa valid");
  r.check(!BaroGuard::pressureValid(NAN), "NaN pressure valid");
  r.check(!BaroGuard::pressureValid(INFINITY), "infinite pressure valid");

  const uint32_t kStuck = 5000;
  const float kPa = 101000.0f;
  // wobble: a real sensor's last digits (about +-2 cm), never the same twice running
  auto wob = [](int i) { return 0.02f * sinf(i * 1.7f) + 0.01f * cosf(i * 0.9f); };

  {  // an ordinary stream is all accepted
    BaroGuard g;
    uint32_t now = 1000;
    int acc = 0;
    for (int i = 0; i < 300; ++i) {
      now += 31;
      acc += g.check(kPa, 120.0f + wob(i), 0.031f, now, kStuck) == V::Accept;
    }
    r.check(acc == 300, "ordinary samples: %d of 300 accepted", acc);
  }
  {  // a climb at 10 m/s is accepted throughout
    BaroGuard g;
    uint32_t now = 1000;
    int acc = 0;
    for (int i = 0; i < 300; ++i) {
      now += 31;
      acc += g.check(kPa, 100.0f + i * 0.31f + wob(i), 0.031f, now, kStuck) == V::Accept;
    }
    r.check(acc == 300, "10 m/s climb: %d of 300 accepted", acc);
  }
  {  // a single 30 m sample is held back and leaves no trace; the next normal sample is accepted
    BaroGuard g;
    uint32_t now = 1000;
    for (int i = 0; i < 50; ++i) g.check(kPa, 120.0f + wob(i), 0.031f, now += 31, kStuck);
    V a = g.check(kPa, 150.0f, 0.031f, now += 31, kStuck);
    V b = g.check(kPa, 120.0f + wob(51), 0.031f, now += 31, kStuck);
    r.check(a == V::Reject, "lone +30 m sample not held back (%d)", (int)a);
    r.check(b == V::Accept, "sample after a lone spike not accepted as normal (%d)", (int)b);
    V c = g.check(kPa, 90.0f, 0.031f, now += 31, kStuck);
    V d = g.check(kPa, 120.0f + wob(52), 0.031f, now += 31, kStuck);
    r.check(c == V::Reject && d == V::Accept, "lone -30 m sample: %d then %d", (int)c, (int)d);
  }
  {  // two samples that agree are a step: accepted as a Jump on the second
    BaroGuard g;
    uint32_t now = 1000;
    for (int i = 0; i < 50; ++i) g.check(kPa, 120.0f + wob(i), 0.031f, now += 31, kStuck);
    V a = g.check(kPa, 150.0f, 0.031f, now += 31, kStuck);
    V b = g.check(kPa, 150.5f, 0.031f, now += 31, kStuck);
    V c = g.check(kPa, 150.4f, 0.031f, now += 31, kStuck);
    r.check(a == V::Reject && b == V::Jump && c == V::Accept, "30 m step: %d %d %d (want Reject Jump Accept)", (int)a,
            (int)b, (int)c);
  }
  {  // two outliers that do not agree with each other are both held back
    BaroGuard g;
    uint32_t now = 1000;
    g.check(kPa, 120.0f, 0.031f, now += 31, kStuck);
    V a = g.check(kPa, 150.0f, 0.031f, now += 31, kStuck);
    V b = g.check(kPa, 90.0f, 0.031f, now += 31, kStuck);
    r.check(a == V::Reject && b == V::Reject, "disagreeing outliers: %d %d", (int)a, (int)b);
  }
  {  // the rate limit: 40 m/s plus 3 m between samples
    BaroGuard g;
    uint32_t now = 1000;
    g.check(kPa, 120.0f, 0.03f, now, kStuck);
    V a = g.check(kPa, 124.0f, 0.03f, now += 30, kStuck);  // 4.0 m in 30 ms (limit 4.2)
    r.check(a == V::Accept, "4 m in 30 ms rejected (%d)", (int)a);
    V b = g.check(kPa, 128.6f, 0.03f, now += 30, kStuck);  // 4.6 m
    r.check(b == V::Reject, "4.6 m in 30 ms accepted (%d)", (int)b);
  }
  {  // after a gap the allowed change grows with the time (2 s: 83 m)
    BaroGuard g;
    uint32_t now = 1000;
    g.check(kPa, 120.0f, 0.03f, now, kStuck);
    V a = g.check(kPa, 180.0f, 2.0f, now += 2000, kStuck);
    r.check(a == V::Accept, "60 m after a 2 s gap rejected (%d)", (int)a);
  }
  {  // impossible pressures and NaN are rejected and change nothing
    BaroGuard g;
    uint32_t now = 1000;
    g.check(kPa, 120.0f, 0.03f, now, kStuck);
    r.check(g.check(5000.0f, 120.0f, 0.03f, now += 30, kStuck) == V::Reject, "5000 Pa accepted");
    r.check(g.check(NAN, 120.0f, 0.03f, now += 30, kStuck) == V::Reject, "NaN pressure accepted");
    r.check(g.check(kPa, NAN, 0.03f, now += 30, kStuck) == V::Reject, "NaN altitude accepted");
    r.check(g.check(kPa, 120.01f, 0.03f, now += 30, kStuck) == V::Accept, "good sample after bad ones not accepted");
  }
  {  // frozen value: reported at the limit, not before; recovered on the first different value
    BaroGuard g;
    uint32_t now = 1000;
    g.check(kPa, 120.0f, 0.03f, now, kStuck);
    V last = V::Accept;
    int firstStuck = -1;
    for (int i = 1; i <= 200; ++i) {  // 31 ms steps, 6.2 s
      last = g.check(kPa, 120.0f, 0.031f, now += 31, kStuck);
      if (last == V::Stuck && firstStuck < 0) firstStuck = i;
    }
    r.check(firstStuck > 0, "5 s of identical readings never reported");
    r.near(firstStuck * 0.031f, 5.0f, 0.1f, "frozen reading detected after (s)");
    r.check(last == V::Stuck && g.stuck(), "still frozen but not reported at 6.2 s");
    V rec = g.check(kPa, 120.02f, 0.031f, now += 31, kStuck);
    V next = g.check(kPa, 120.01f, 0.031f, now += 31, kStuck);
    r.check(rec == V::Recovered && !g.stuck(), "first changed reading after a freeze: %d", (int)rec);
    r.check(next == V::Accept, "reading after the recovery: %d", (int)next);
  }
  {  // a freeze shorter than the limit is not reported; two alternating values are not frozen
    BaroGuard g;
    uint32_t now = 1000;
    int bad = 0;
    for (int i = 0; i < 150; ++i) bad += g.check(kPa, 120.0f, 0.031f, now += 31, kStuck) == V::Stuck;  // 4.65 s
    r.check(bad == 0, "4.65 s of identical readings reported as frozen");
    bad = 0;
    for (int i = 0; i < 400; ++i) bad += g.check(kPa, (i & 1) ? 120.0f : 120.01f, 0.031f, now += 31, kStuck) == V::Stuck;
    r.check(bad == 0, "alternating readings reported as frozen");
  }
  {  // limit 0: not checked
    BaroGuard g;
    uint32_t now = 1000;
    int bad = 0;
    for (int i = 0; i < 400; ++i) bad += g.check(kPa, 120.0f, 0.031f, now += 31, 0) == V::Stuck;
    r.check(bad == 0, "frozen reading reported with the check switched off");
  }
}

// ---------------------------------------------------------------------------------------------- rpmtrack
void rpmTrackUnitSuite(UnitReport& r) {
  struct Rig {
    RpmTracker t;
    uint32_t now = 100000;
    float feed(float raw, float conf = 9.0f, bool clear = true) { return t.update(raw, conf, 3.0f, clear, now += 250, 1500); }
    void settle(float rpm) { feed(rpm); feed(rpm); feed(rpm); }
  };

  {  // acquisition needs two agreeing windows
    Rig g;
    g.feed(2000);
    r.near(g.t.rpm(), 0, 0.01f, "one window is not a reading");
    g.feed(2010);
    r.near(g.t.rpm(), 2010, 0.01f, "two agreeing windows are");
  }
  {  // two windows that disagree: not yet; a third that agrees with the second is
    Rig g;
    g.feed(2000);
    g.feed(3000);
    r.near(g.t.rpm(), 0, 0.01f, "disagreeing windows gave a reading");
    g.feed(3050);
    r.near(g.t.rpm(), 3050, 0.01f, "third window agreeing with the second");
  }
  {  // smoothing: a change inside 20 % moves half way
    Rig g;
    g.settle(2000);
    r.near(g.t.rpm(), 2000, 0.5f, "settled at 2000");
    g.feed(2100);
    r.near(g.t.rpm(), 2050, 0.5f, "5 %% step moves half way");
    g.feed(2300);  // 12 % above 2050
    r.near(g.t.rpm(), 2175, 0.5f, "12 %% step moves half way");
  }
  {  // a lone bad window never shows (F4): up, down, octave
    for (float bad : {4000.0f, 1000.0f, 500.0f, 2600.0f}) {
      Rig g;
      g.settle(2000);
      g.feed(bad);
      r.near(g.t.rpm(), 2000, 0.5f, "reading while one bad window is pending");
      g.feed(2000);
      r.near(g.t.rpm(), 2000, 0.5f, "reading after the next window is normal");
      g.feed(bad);
      g.feed(2000);
      g.feed(bad);
      g.feed(2000);
      r.near(g.t.rpm(), 2000, 0.5f, "reading after bad windows between good ones");
    }
  }
  {  // two agreeing windows far away are a real change
    Rig g;
    g.settle(2000);
    g.feed(4000);
    g.feed(4100);
    r.near(g.t.rpm(), 4100, 0.5f, "real change to 4000");
  }
  {  // two bad windows that disagree with each other: nothing moves; a third that agrees with the second does
    Rig g;
    g.settle(2000);
    g.feed(4000);
    g.feed(3000);
    r.near(g.t.rpm(), 2000, 0.5f, "disagreeing bad windows");
    g.feed(3050);
    r.near(g.t.rpm(), 3050, 0.5f, "pending 3000 confirmed by 3050");
  }
  {  // a window of low confidence cancels a pending jump
    Rig g;
    g.settle(2000);
    g.feed(4000);
    g.feed(4000, 1.0f);
    g.feed(4000);
    r.near(g.t.rpm(), 2000, 0.5f, "jump confirmed across a low-confidence window");
  }
  {  // no usable window: the reading goes after the hold time, not before
    Rig g;
    g.settle(2000);
    for (int i = 0; i < 6; ++i) g.feed(2000, 1.0f);  // 1.5 s
    r.near(g.t.rpm(), 2000, 0.5f, "reading at exactly the hold time");
    g.feed(2000, 1.0f);
    r.near(g.t.rpm(), 0, 0.01f, "reading just after the hold time");
    g.feed(2000);
    r.near(g.t.rpm(), 0, 0.01f, "one window after a dropout is not a reading again");
    g.feed(2010);
    r.near(g.t.rpm(), 2010, 0.01f, "two windows are");
  }
  {  // speech (F3): the reading is held for the speech and for the hold time after it
    Rig g;
    g.settle(2000);
    for (int i = 0; i < 40; ++i) g.feed(0, 0, false);  // 10 s of speech
    r.near(g.t.rpm(), 2000, 0.5f, "reading after 10 s of speech");
    for (int i = 0; i < 6; ++i) g.feed(0, 0.5f);  // 1.5 s after it, nothing usable
    r.near(g.t.rpm(), 2000, 0.5f, "reading 1.5 s after the speech");
    g.feed(0, 0.5f);
    r.near(g.t.rpm(), 0, 0.01f, "reading 1.75 s after the speech with no usable window");
  }
  {  // speech, then the engine has changed
    Rig g;
    g.settle(2000);
    for (int i = 0; i < 12; ++i) g.feed(0, 0, false);
    g.feed(3000);
    r.near(g.t.rpm(), 2000, 0.5f, "first window after speech at a new speed");
    g.feed(3010);
    r.near(g.t.rpm(), 3010, 0.5f, "second window");
  }
  {  // speech with the engine already at 0: nothing to hold
    Rig g;
    for (int i = 0; i < 20; ++i) g.feed(0, 0, false);
    r.near(g.t.rpm(), 0, 0.01f, "speech with no engine gave a reading");
  }
  {  // a speech flag that never clears cannot hold the reading for ever: 15 s at most
    Rig g;
    g.settle(2000);
    for (int i = 0; i < 56; ++i) g.feed(0, 0, false);  // 14 s
    r.near(g.t.rpm(), 2000, 0.5f, "reading after 14 s of speech");
    for (int i = 0; i < 12; ++i) g.feed(0, 0, false);  // to 17 s
    r.near(g.t.rpm(), 0, 0.01f, "reading after 17 s of 'speech'");
  }
}

#endif
