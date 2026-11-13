// Software-level unit tests of production maths, run on the vario by the test build (see UnitTest.h):
//   geo      - angle wrapping, distance, bearings, local coordinates, clock positions
//   wind     - the circling wind fit (accuracy, turn direction, 0/360, rejections, averaging) and the glide
//              ratio over the ground for head, tail and cross winds
//   circuit  - the landing circuit state machine driven by scripted positions (leg order, turn points,
//              no early or skipped legs, overshoot, wrong way)
//   baro     - the barometer plausibility checks (impossible pressure, lone outliers, steps, frozen value)
//   button   - the button decoding (click counts, hold, abandoned hold, bounce)
//   audio    - what is queued for an utterance (the signal before urgent calls)
//   resume   - when a flight saved in the air is believed again at power-up
//   alerts   - what the battery and barometer warnings say
//   status   - the spoken "tell me" status
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
#include "app/Status.h"
#include "flight/Resume.h"
#include "input/Button.h"
#include <string>

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

  // ---- the pilot's turn rate, learned from the first two turns, and the later turns called to suit (right-hand circuit,
  // no wind, final 250 m, height 150 m on downwind: the base turn corner is at x = -350). A pilot who takes `react` seconds
  // to start and then turns at `rate` deg/s should be called early by react + 57.3 / rate seconds (a 90 degree turn at rate w
  // ends 57.3 / w seconds' worth of flight past where it began): written out here, not taken from the code under test.
  {
    g_cfg.windManual = true;
    g_cfg.windSpeedMs = 0;
    g_cfg.windFromDeg = 0;
    g_cfg.circuitSide = CS_RIGHT;
    g_cfg.turnLeadS = 3.0f;
    const float corner = -350.0f;
    // a scripted turn, one sample a second from the call: hold, then turn at rate to the new leg, then hold 3 s
    auto scriptTurn = [&](CircuitRig& c, float fromRel, float toRel, float react, float rate, float x, float y) {
      const float span = fabsf(toRel - fromRel);
      const int n = (int)(react + span / rate + 3.0f);
      for (int k = 1; k <= n; ++k) {
        float done = k <= react ? 0.0f : fminf(rate * (k - react), span);
        c.step(x, y, fromRel + (toRel > fromRel ? done : -done), 150);
      }
    };
    // flies a circuit to the base turn with the given first two turns; returns the x where the base turn was called
    auto circuitTo = [&](CircuitRig& c, float react1, float rate1, float react2, float rate2, bool wrongWayFirst) {
      c.ap.start(c.nav(xStart - 50, 0, 0, 150), c.wind);
      c.step(xStart, 0, 0, 150);
      float x = xStart;
      for (; x < xCross + 20 && c.ap.phase() != ApPhase::Crosswind; x += 10) c.step(x, 0, 0, 150);
      if (wrongWayFirst) {  // turns away from the leg first (left), then comes back right: not a sample
        for (int k = 1; k <= 4; ++k) c.step(x, 0, -20.0f * k, 150);
        for (int k = 1; k <= 4; ++k) c.step(x, 0, -80.0f + 40.0f * k, 150);
      } else {
        scriptTurn(c, 0.0f, 90.0f, react1, rate1, x, 0);
      }
      float y = 0;
      for (; y < 420 && c.ap.phase() != ApPhase::Downwind; y += 10) c.step(xCross, y, 90, 150);
      scriptTurn(c, 90.0f, 180.0f, react2, rate2, xCross, y);
      float bx = xCross;
      for (; bx > -800 && c.ap.phase() != ApPhase::Base; bx -= 10) c.step(bx, 400, 180, 150);
      return bx + 10;  // the x of the step that made the call
    };
    { // 2 s reaction, 10 deg/s: lead 2 + 5.73 = 7.73 s; at 10 m/s the base turn comes 77 m before the corner
      CircuitRig c(0);
      float bx = circuitTo(c, 2, 10, 2, 10, false);
      r.check(c.ap.turnsLearned() == 2, "10 deg/s pilot: %d turns learned, expected 2", c.ap.turnsLearned());
      r.near(c.ap.leadSeconds(), 7.73f, 0.4f, "lead after two turns at 10 deg/s, 2 s reaction");
      r.near(bx, corner + 77.3f, 10.5f, "base turn point for a slow pilot (10 deg/s)");
    }
    { // 3 s reaction, 4.5 deg/s (20 s for a quarter circle): lead 3 + 12.7 = 15.7 s
      CircuitRig c(0);
      float bx = circuitTo(c, 3, 4.5f, 3, 4.5f, false);
      r.near(c.ap.leadSeconds(), 15.7f, 1.0f, "lead after two 20 s turns");
      r.near(bx, corner + 157.0f, 12.0f, "base turn point for a 20 s turner");
    }
    { // a quick pilot, 20 deg/s with 1 s reaction: about 1 + 2.9 s
      CircuitRig c(0);
      circuitTo(c, 1, 20, 1, 20, false);
      r.check(c.ap.turnsLearned() == 2, "20 deg/s pilot: %d turns learned", c.ap.turnsLearned());
      r.near(c.ap.leadSeconds(), 4.2f, 1.0f, "lead after two quick turns");
    }
    { // the lead is learned, not guessed: the first two turns still use the setting
      CircuitRig c(0);
      c.ap.start(c.nav(xStart - 50, 0, 0, 150), c.wind);
      r.near(c.ap.leadSeconds(), 3.0f, 0.001f, "lead before any turn");
      r.check(c.ap.turnsLearned() == 0, "turns learned before any turn: %d", c.ap.turnsLearned());
    }
    { // a turn that cannot be trusted is not a sample: the wrong way first, then one good turn: only one sample, so the setting stays
      CircuitRig c(0);
      float bx = circuitTo(c, 2, 10, 2, 10, true);
      r.check(c.ap.turnsLearned() == 1, "wrong way first: %d turns learned, expected 1", c.ap.turnsLearned());
      r.near(c.ap.leadSeconds(), 3.0f, 0.001f, "one sample is not enough: lead stays at the setting");
      r.near(bx, corner + 30.0f, 10.5f, "base turn point with the setting's lead after a bad first turn");
    }
    { // switched off: the setting is used however the pilot turns
      g_cfg.turnLearn = false;
      CircuitRig c(0);
      float bx = circuitTo(c, 3, 4.5f, 3, 4.5f, false);
      r.check(c.ap.turnsLearned() == 0, "learning switched off: %d turns learned", c.ap.turnsLearned());
      r.near(bx, corner + 30.0f, 10.5f, "base turn point with learning off");
      g_cfg.turnLearn = true;
    }
    { // the track jumping to the new leg at once (a GPS that only reports the course now and then) is not a measurement
      CircuitRig c(0);
      float bx = circuitTo(c, 0, 1000, 0, 1000, false);
      r.check(c.ap.turnsLearned() == 0, "track jumps: %d turns learned, expected none", c.ap.turnsLearned());
      r.near(bx, corner + 30.0f, 10.5f, "base turn point when nothing could be measured");
    }
    { // a new approach starts again from the setting
      CircuitRig c(0);
      circuitTo(c, 2, 10, 2, 10, false);
      c.ap.cancel(false);
      c.ap.start(c.nav(xStart - 50, 0, 0, 150), c.wind);
      r.check(c.ap.turnsLearned() == 0 && c.ap.leadSeconds() == 3.0f, "a new approach kept what the last one learned");
    }
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
    fly.baroOk = true;  // (a failed barometer in flight is announced too)
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
    r.check(audio::testSaid() == b + 1 && said(b, 0, "[urgent] battery_critical|battery_low"), "critical battery: %d calls, first '%s'", audio::testSaid() - b, audio::testSaidText(b));
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
    r.check(audio::testSaid() == b + 1 && said(b, 0, "[urgent] battery_lost|battery_low"), "reading lost in flight: %d calls, '%s'", audio::testSaid() - b, audio::testSaidText(b));
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

  // F10: how long it must look still. Near the ground the landing time; higher up with a trusted speed, highMult times it
  // (a paramotor hanging in a headwind is level and almost stopped); no trusted speed, twice the landing time near the
  // ground only. Expected values written out from the rules.
  const uint32_t T = 30000;
  auto hold = [&](bool speedOk, float gs, bool baroOk, float vz, float agl, int mult = 5) {
    return Flight::landingHoldMs(speedOk, gs, kLand, baroOk, vz, agl, kMaxAgl, T, mult);
  };
  r.check(hold(true, 0.5f, true, 0, 3) == T, "trusted speed near the ground: %lu ms, expected 30000", (unsigned long)hold(true, 0.5f, true, 0, 3));
  r.check(hold(true, 0.5f, true, 0, 14.9f) == T, "just under the near-ground height: %lu", (unsigned long)hold(true, 0.5f, true, 0, 14.9f));
  r.check(hold(true, 0.5f, true, 0, 15.0f) == 5 * T, "at the near-ground height counts as higher: %lu", (unsigned long)hold(true, 0.5f, true, 0, 15.0f));
  r.check(hold(true, 0.5f, true, 0, 60) == 5 * T, "trusted speed at 60 m: %lu, expected 150000", (unsigned long)hold(true, 0.5f, true, 0, 60));
  r.check(hold(true, 0.5f, true, 0, -40) == T, "landed lower than the reference: %lu", (unsigned long)hold(true, 0.5f, true, 0, -40));
  r.check(hold(true, 3.0f, true, 0, 0) == 0, "moving at 3 m/s on the ground is still");
  r.check(hold(true, 1.4f, true, 0, 0) == 0, "speed equal to the landing speed counts as moving");
  r.check(hold(true, 0.5f, true, 0, 60, 1) == T, "multiplier 1 is the old rule at height: %lu", (unsigned long)hold(true, 0.5f, true, 0, 60, 1));
  r.check(hold(true, 0.5f, true, 0, 60, 0) == T, "multiplier 0 is taken as 1: %lu", (unsigned long)hold(true, 0.5f, true, 0, 60, 0));
  r.check(hold(true, 0.5f, true, 0, 60, 20) == 20 * T, "multiplier 20: %lu", (unsigned long)hold(true, 0.5f, true, 0, 60, 20));
  r.check(hold(true, 0.5f, false, 0, 60) == 5 * T, "the trusted speed rule does not need the barometer: %lu", (unsigned long)hold(true, 0.5f, false, 0, 60));
  r.check(hold(false, 0, true, 0.0f, 3) == 2 * T, "no trusted speed, near the ground, steady: %lu, expected 60000", (unsigned long)hold(false, 0, true, 0.0f, 3));
  r.check(hold(false, 0, true, 0.0f, 60) == 0, "no trusted speed at 60 m is never still");
  r.check(hold(false, 0, false, 0.0f, 3) == 0, "no speed and no barometer is never still");
  r.check(hold(false, 0, true, 0.5f, 3) == 0, "no trusted speed and a 0.5 m/s climb is not still");

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

// ---------------------------------------------------------------------------------------------- button
namespace {

// Scripted presses fed to the decoder every 5 ms; records every event with the time it came.
struct ButtonScript {
  ButtonDecoder d;
  uint32_t t = 1000;
  struct Ev {
    ButtonEvent e;
    uint32_t at;
  };
  Ev ev[16];
  int n = 0;
  void run(bool pressed, uint32_t ms) {
    for (uint32_t i = 0; i < ms; i += 5) {
      t += 5;
      ButtonEvent e = d.feed(pressed, t);
      if (e != ButtonEvent::None && n < 16) ev[n++] = {e, t};
    }
  }
  void click(uint32_t on = 100) { run(true, on); }
  void idle(uint32_t ms) { run(false, ms); }
  // the events as letters: P press, C hold cue, 1 single, 2 double, 3 triple, H hold
  void letters(char* out) const {
    static const char k[] = ".PC123H";
    for (int i = 0; i < n; ++i) out[i] = k[(int)ev[i].e];
    out[n] = 0;
  }
};

}  // namespace

void buttonUnitSuite(UnitReport& r) {
  char s[24];
  const uint32_t kGap = ButtonDecoder::kGapMs;
  {  // one click: the press, then Single once the gap has passed with no second press
    ButtonScript b;
    b.click();
    b.idle(kGap + 200);
    b.letters(s);
    r.check(strcmp(s, "P1") == 0, "one click gave '%s', expected 'P1'", s);
    const uint32_t release = 1000 + 100 + ButtonDecoder::kDebounceMs;  // about when the release was registered
    r.check(b.ev[1].at >= release + kGap - 10 && b.ev[1].at <= release + kGap + 40,
            "Single came at +%lu ms after the click, expected about %lu", (unsigned long)(b.ev[1].at - 1100),
            (unsigned long)kGap);
  }
  {  // two clicks
    ButtonScript b;
    b.click();
    b.idle(150);
    b.click();
    b.idle(kGap + 200);
    b.letters(s);
    r.check(strcmp(s, "PP2") == 0, "two clicks gave '%s', expected 'PP2'", s);
  }
  {  // three clicks: Triple at once on the third release, no waiting
    ButtonScript b;
    b.click();
    b.idle(150);
    b.click();
    b.idle(150);
    b.click();
    b.idle(40);
    b.letters(s);
    r.check(strcmp(s, "PPP3") == 0, "three clicks gave '%s', expected 'PPP3' (Triple on the third release)", s);
    b.idle(kGap + 300);
    b.letters(s);
    r.check(strcmp(s, "PPP3") == 0, "after a Triple more events came: '%s'", s);
  }
  {  // clicks further apart than the gap are separate singles
    ButtonScript b;
    b.click();
    b.idle(kGap + 100);
    b.click();
    b.idle(kGap + 100);
    b.letters(s);
    r.check(strcmp(s, "P1P1") == 0, "two slow clicks gave '%s', expected 'P1P1'", s);
  }
  {  // a gloved double click with 500 ms between the clicks is still a double (the gap is 600 ms)
    ButtonScript b;
    b.click(120);
    b.idle(500);
    b.click(120);
    b.idle(kGap + 100);
    b.letters(s);
    r.check(strcmp(s, "PP2") == 0, "double click with 500 ms between gave '%s', expected 'PP2'", s);
  }
  {  // a hold: cue at 0.8 s, Hold at 1.5 s, nothing more on release
    ButtonScript b;
    b.run(true, 1700);
    b.idle(kGap + 300);
    b.letters(s);
    r.check(strcmp(s, "PCH") == 0, "a 1.7 s hold gave '%s', expected 'PCH'", s);
    r.check(b.ev[1].at - 1000 >= 800 && b.ev[1].at - 1000 <= 860, "cue at +%lu ms, expected 800",
            (unsigned long)(b.ev[1].at - 1000));
    r.check(b.ev[2].at - 1000 >= 1500 && b.ev[2].at - 1000 <= 1560, "Hold at +%lu ms, expected 1500",
            (unsigned long)(b.ev[2].at - 1000));
  }
  {  // a hold released before 1.5 s is abandoned: the cue was heard, nothing is triggered
    ButtonScript b;
    b.run(true, 1100);
    b.idle(kGap + 500);
    b.letters(s);
    r.check(strcmp(s, "PC") == 0, "a 1.1 s press gave '%s', expected 'PC' (abandoned hold)", s);
  }
  {  // a slow click (700 ms, under the cue time) is still a click
    ButtonScript b;
    b.run(true, 700);
    b.idle(kGap + 200);
    b.letters(s);
    r.check(strcmp(s, "P1") == 0, "a 700 ms press gave '%s', expected 'P1'", s);
  }
  {  // a click and then a hold: the click is dropped, only the hold counts
    ButtonScript b;
    b.click();
    b.idle(200);
    b.run(true, 1700);
    b.idle(kGap + 300);
    b.letters(s);
    r.check(strcmp(s, "PPCH") == 0, "click then hold gave '%s', expected 'PPCH'", s);
  }
  {  // contact bounce shorter than the debounce time does nothing
    ButtonScript b;
    b.run(true, 10);
    b.idle(500);
    b.letters(s);
    r.check(strcmp(s, "") == 0, "a 10 ms glitch gave '%s'", s);
  }
  {  // a held button that never releases fires Hold once, not repeatedly
    ButtonScript b;
    b.run(true, 6000);
    b.letters(s);
    r.check(strcmp(s, "PCH") == 0, "a 6 s hold gave '%s', expected 'PCH'", s);
  }
}

// ---------------------------------------------------------------------------------------------- status ("tell me")
void statusUnitSuite(UnitReport& r) {
  CfgGuard guard;
  audio::testCapture(true);
  g_cfg.windManual = true;
  g_cfg.windFromDeg = 90;
  g_cfg.windSpeedMs = 5;
  g_cfg.speedUnit = SU_KMH;
  Wind wind;
  NavState n;
  n.fix = n.speedOk = true;
  n.lat = 51.5;
  n.lon = 0.5;
  n.speedMs = 10;
  n.trackDeg = 0;
  n.aglM = 150;
  n.lzValid = true;
  n.lzLat = 51.5 + 500.0 / geo::kEarthR / geo::kDegToRad;  // 500 m due north
  n.lzLon = 0.5;
  auto said = [&](int i) { return std::string(audio::testSaidText(i)); };
  auto has = [&](int i, const char* w) { return said(i).find(w) != std::string::npos; };

  status::tell(n, wind, 80, 25, true);
  r.check(audio::testSaid() == 2, "tell me made %d calls, expected 2", audio::testSaid());
  r.check(said(0).rfind("[high]", 0) == 0 && said(1).rfind("[high]", 0) == 0, "calls are not high priority: %s | %s",
          said(0).c_str(), said(1).c_str());
  r.check(has(0, "landing_zone") && has(0, "oclock") && (has(0, "metres") || has(0, "feet")), "first call: %s",
          said(0).c_str());
  r.check(said(0).find("landing_zone") < said(0).find("oclock"), "clock position must follow 'landing zone': %s",
          said(0).c_str());
  r.check(has(0, "n12 oclock"), "LZ dead ahead should be 12 o'clock: %s", said(0).c_str());
  r.check(has(1, "wind") && has(1, "kmh") && has(1, "battery") && has(1, "minutes"), "second call: %s", said(1).c_str());
  r.check(said(1).find("wind") < said(1).find("battery"), "wind comes before the battery: %s", said(1).c_str());
  r.check(has(1, "n3 oclock"), "wind from 090 relative to track 000 should be 3 o'clock: %s", said(1).c_str());

  audio::testCapture(true);  // units follow the setting
  g_cfg.speedUnit = SU_KNOTS;
  status::tell(n, wind, 80, 25, true);
  r.check(has(1, "knots"), "speed unit knots: %s", said(1).c_str());
  audio::testCapture(true);
  g_cfg.speedUnit = SU_MPH;
  status::tell(n, wind, 80, 25, true);
  r.check(has(1, "mph"), "speed unit mph: %s", said(1).c_str());

  audio::testCapture(true);  // battery minutes unknown: the percentage
  status::tell(n, wind, 62, -1, true);
  r.check(has(1, "percent") && !has(1, "minutes"), "unknown minutes should say percent: %s", said(1).c_str());

  audio::testCapture(true);  // standing still: no clock positions (there is no direction of travel)
  n.speedMs = 0.5f;
  status::tell(n, wind, 80, 25, true);
  r.check(!has(0, "oclock") && has(0, "landing_zone"), "standing still, first call: %s", said(0).c_str());
  r.check(!has(1, "oclock"), "standing still, wind clock: %s", said(1).c_str());
  n.speedMs = 10;

  audio::testCapture(true);  // no landing zone
  n.lzValid = false;
  status::tell(n, wind, 80, 25, true);
  r.check(has(0, "no_landing_zone"), "no landing zone: %s", said(0).c_str());
  n.lzValid = true;

  audio::testCapture(true);  // no fix: height and wind, no landing zone
  n.fix = false;
  status::tell(n, wind, 80, 25, true);
  r.check(!has(0, "landing_zone") && !has(0, "oclock"), "no fix: %s", said(0).c_str());
  n.fix = true;

  audio::testCapture(true);  // wind unknown: no wind words
  g_cfg.windManual = false;
  status::tell(n, wind, 80, 25, true);
  r.check(!has(1, "wind") && has(1, "battery"), "wind unknown: %s", said(1).c_str());
  g_cfg.windManual = true;

  audio::testCapture(true);  // a vario with no GPS: height and battery only
  status::tell(n, wind, 80, 25, false);
  r.check(!has(0, "landing_zone") && !has(0, "no_landing_zone") && !has(1, "wind") && has(1, "battery"),
          "no GPS: %s | %s", said(0).c_str(), said(1).c_str());
  audio::testCapture(false);
}

// ---------------------------------------------------------------------------------------------- alerts
// What the battery and barometer warnings say, and when. (When the battery calls are made is in the battery suite.)
void alertsUnitSuite(UnitReport& r) {
  CfgGuard guard;
  g_cfg.gpsMode = GPS_ALWAYS;  // the messages depend on there being a GPS
  audio::testCapture(true);
  g_cfg.battLowV = 3.5f;
  g_cfg.battCriticalV = 3.3f;
  g_cfg.distUnit = DU_METRES;
  auto said = [&](int i) { return std::string(audio::testSaidText(i)); };
  auto has = [&](int i, const char* w) { return said(i).find(w) != std::string::npos; };
  auto before = [&](int i, const char* a, const char* b) {
    size_t pa = said(i).find(a), pb = said(i).find(b);
    return pa != std::string::npos && pb != std::string::npos && pa < pb;
  };
  uint32_t t = 7000000;
  auto run = [&](const NavState& nav, float volts, int minutes, uint32_t seconds) {
    for (uint32_t s = 0; s < seconds; ++s) alerts::update(t += 1000, nav, volts, minutes);
  };
  auto fresh = [&](const NavState& n0) {  // reset; the first fix is announced once: let that pass, count from zero
    alerts::reset();
    alerts::update(t += 1000, n0, 3.9f, -1);
    audio::testCapture(true);
  };
  NavState nav;
  nav.fix = nav.speedOk = true;
  nav.lat = 51.5;
  nav.lon = 0.5;
  nav.lzValid = true;
  nav.lzLat = 51.5 + 1200.0 / geo::kEarthR / geo::kDegToRad;  // 1200 m north
  nav.lzLon = 0.5;
  nav.flying = true;
  nav.baroOk = true;

  // low battery with everything known: the battery, the minutes, the landing zone and its distance, the advice
  fresh(nav);
  run(nav, 3.40f, 25, 11);
  r.check(audio::testSaid() == 1, "low battery: %d calls, expected 1", audio::testSaid());
  r.check(said(0).rfind("[normal] battery_low", 0) == 0, "low battery lead: %s", said(0).c_str());
  r.check(before(0, "battery_low", "approximately") && before(0, "approximately", "minutes") &&
              before(0, "minutes", "remaining") && before(0, "remaining", "landing_zone") && before(0, "landing_zone", "kilometres") &&
              before(0, "kilometres", "turn_me_off"),
          "low battery message in the wrong order or incomplete: %s", said(0).c_str());
  r.check(has(0, "n20 n5 minutes remaining"), "25 minutes remaining: %s", said(0).c_str());
  r.check(has(0, "n1 point n2 kilometres"), "1.2 km to the landing zone: %s", said(0).c_str());

  // minutes unknown: no 'approximately', no minutes
  fresh(nav);
  run(nav, 3.40f, -1, 11);
  r.check(!has(0, "approximately") && !has(0, "minutes") && !has(0, "remaining") && has(0, "landing_zone") && has(0, "turn_me_off"),
          "minutes unknown: %s", said(0).c_str());

  // zero minutes left says nothing useful and is left out
  fresh(nav);
  run(nav, 3.40f, 0, 11);
  r.check(!has(0, "approximately") && !has(0, "remaining") && has(0, "landing_zone"), "0 minutes: %s", said(0).c_str());

  // no fix: the distance cannot be said; the advice still is (the vario keeps its landing zone)
  NavState nofix = nav;
  nofix.fix = false;
  nofix.flying = false;  // (in flight the lost GPS is announced first)
  fresh(nav);
  run(nofix, 3.40f, 25, 11);
  r.check(!has(0, "landing_zone") && has(0, "turn_me_off") && has(0, "approximately"), "no fix: %s", said(0).c_str());

  // no landing zone known: no distance and no advice to switch off (it would be forgotten)
  NavState nolz = nav;
  nolz.lzValid = false;
  fresh(nolz);
  run(nolz, 3.40f, 25, 11);
  r.check(!has(0, "landing_zone") && !has(0, "turn_me_off") && has(0, "approximately"), "no landing zone: %s", said(0).c_str());

  // critical: its own lead (falling back to 'battery low' when the clip is not there), urgent, with the details
  fresh(nav);
  run(nav, 3.20f, 8, 6);
  r.check(said(0).rfind("[urgent] battery_critical|battery_low", 0) == 0, "critical lead: %s", said(0).c_str());
  r.check(has(0, "approximately") && has(0, "n8 minutes remaining") && has(0, "landing_zone") && has(0, "turn_me_off"),
          "critical details: %s", said(0).c_str());

  // reading lost in flight: its own lead, nothing else to say
  fresh(nav);
  run(nav, 3.9f, 25, 5);
  run(nav, 0.0f, 25, 6);
  r.check(audio::testSaid() == 1 && said(0) == "[urgent] battery_lost|battery_low", "reading lost: %s", said(0).c_str());

  // a vario with no landing-zone advice is not told to switch off: a lone battery call has no 'turn_me_off' without
  // the landing zone (covered above); and the vario tone stays out of it
  // ---- the barometer failing in flight
  NavState bad = nav;
  bad.baroOk = false;
  fresh(nav);
  run(bad, 3.9f, 25, 2);
  r.check(audio::testSaid() == 0, "barometer blip of 2 s called");
  run(nav, 3.9f, 25, 2);  // it came back before 3 s
  run(bad, 3.9f, 25, 3);
  r.check(audio::testSaid() == 0, "two short blips added up to a call");
  run(bad, 3.9f, 25, 2);
  r.check(audio::testSaid() == 1 && said(0) == "[urgent] baro_failed|~400", "barometer failed call: %d, '%s'", audio::testSaid(), said(0).c_str());
  run(bad, 3.9f, 25, 55);
  r.check(audio::testSaid() == 1, "barometer failed repeated within a minute (%d)", audio::testSaid());
  run(bad, 3.9f, 25, 10);
  r.check(audio::testSaid() == 2, "barometer failed not repeated after a minute (%d)", audio::testSaid());
  run(nav, 3.9f, 25, 2);
  r.check(audio::testSaid() == 2, "'back' said before 3 s");
  run(nav, 3.9f, 25, 2);
  r.check(audio::testSaid() == 3 && said(2) == "[normal] baro_back|~800", "barometer back: %d, '%s'", audio::testSaid(), said(2).c_str());
  run(nav, 3.9f, 25, 30);
  r.check(audio::testSaid() == 3, "'back' repeated");

  // on the ground the barometer is the ready check's business, not a spoken alert
  NavState ground = bad;
  ground.flying = false;
  fresh(ground);
  run(ground, 3.9f, 25, 30);
  r.check(audio::testSaid() == 0, "barometer fault on the ground called: %s", said(0).c_str());

  alerts::reset();
  audio::testCapture(false);
}

// ---------------------------------------------------------------------------------------------- resume
// resumeDecide(): when a flight saved in the air is believed again at power-up. Expectations are written out from the
// rules, with distances computed here and not by the code under test.
void resumeUnitSuite(UnitReport& r) {
  using V = ResumeVerdict;
  ResumeParams p;
  p.maxAgeS = 180 * 60;
  p.minSpeedMs = 5.0f;
  p.saveIntervalS = 60.0f;
  p.maxSpeedMs = 40.0f;
  FlightRecord rec = {};
  rec.flying = true;
  rec.savedUtc = 1000000;
  rec.posValid = true;
  rec.lastLat = 51.5;
  rec.lastLon = 0.5;
  const double kMperDegLat = geo::kEarthR * geo::kDegToRad;  // about 111.2 km
  auto at = [&](double metresNorth) { return 51.5 + metresNorth / kMperDegLat; };
  uint32_t age = 0;
  auto verdict = [&](uint32_t nowUtc, double lat, double lon, float speed) {
    return resumeDecide(rec, nowUtc, lat, lon, speed, p, &age);
  };

  r.check(verdict(1000300, at(2000), 0.5, 12) == V::Resume, "5 minutes, 2 km, 12 m/s: not resumed");
  r.check(age == 300, "age %lu, expected 300", (unsigned long)age);
  r.check(verdict(1000300, 51.5, 0.5, 5.0f) == V::Resume, "speed at the minimum (5 m/s) not resumed");
  r.check(verdict(1000300, 51.5, 0.5, 4.9f) == V::OnGround, "4.9 m/s resumed");
  r.check(verdict(1000300, 51.5, 0.5, 0.0f) == V::OnGround, "standing still resumed (a vario switched on at the launch)");

  rec.flying = false;
  r.check(verdict(1000300, 51.5, 0.5, 12) == V::NotFlying, "a record that was not flying resumed");
  rec.flying = true;

  // the time
  r.check(verdict(0, 51.5, 0.5, 12) == V::NoTime, "no GPS time resumed");
  rec.savedUtc = 0;
  r.check(verdict(1000300, 51.5, 0.5, 12) == V::NoTime, "a record with no time resumed");
  rec.savedUtc = 1000000;
  r.check(verdict(999000, 51.5, 0.5, 12) == V::NoTime, "a clock that went backwards resumed");
  r.check(verdict(1000000 + 180 * 60, 51.5, 0.5, 12) == V::Resume, "exactly at the age limit refused");
  r.check(verdict(1000000 + 180 * 60 + 1, 51.5, 0.5, 12) == V::Stale, "one second over the age limit resumed");
  r.check(verdict(1000000 + 24 * 3600, 51.5, 0.5, 12) == V::Stale, "a record a day old resumed");
  p.maxAgeS = 30 * 60;
  r.check(verdict(1000000 + 1801, 51.5, 0.5, 12) == V::Stale, "the age limit setting is not followed");
  r.check(verdict(1000000 + 1799, 51.5, 0.5, 12) == V::Resume, "inside a 30 minute limit refused");
  p.maxAgeS = 180 * 60;

  // the position: 40 m/s for (age + 60 s), plus 1 km
  r.check(verdict(1000300, at(15000), 0.5, 12) == V::Resume, "15 km after 5 minutes (limit 15.4 km) refused");
  r.check(verdict(1000300, at(16000), 0.5, 12) == V::Implausible, "16 km after 5 minutes (limit 15.4 km) resumed");
  r.check(verdict(1000010, at(4500), 0.5, 12) == V::Implausible, "4.5 km after 10 s (limit 3.8 km) resumed");
  r.check(verdict(1000010, at(3500), 0.5, 12) == V::Resume, "3.5 km after 10 s refused");
  r.check(verdict(1000300, 51.5, 5.5, 12) == V::Implausible, "300 km east after 5 minutes resumed");
  r.check(verdict(1000300, -33.9, 151.2, 12) == V::Implausible, "the other side of the world resumed");
  rec.posValid = false;
  r.check(verdict(1000300, 51.5, 0.5, 12) == V::Implausible, "a record with no position resumed");
  rec.posValid = true;

  // the order of the checks: no time is reported before the position, and age before position
  r.check(verdict(1000000 + 24 * 3600, at(900000), 0.5, 0) == V::Stale, "stale record, far away and still: not 'stale'");
}

// ---------------------------------------------------------------------------------------------- audio
// What is queued for an utterance: the signal tone before urgent calls.
void audioUnitSuite(UnitReport& r) {
  using audio::Priority;
  using audio::Utterance;
  {
    Utterance u(Priority::Urgent);
    u.add("no_gps");
    Utterance q = audio::withSignal(u);
    r.check(q.count == 2 && strcmp(q.clips[0], "~1000") == 0 && strcmp(q.clips[1], "no_gps") == 0, "urgent call: %d clips, first '%s'", q.count, q.clips[0]);
    r.check(u.count == 1, "the original utterance was changed");
  }
  for (Priority p : {Priority::Low, Priority::Normal, Priority::High}) {
    Utterance u(p);
    u.add("landed");
    Utterance q = audio::withSignal(u);
    r.check(q.count == 1 && strcmp(q.clips[0], "landed") == 0, "priority %d got a signal", (int)p);
  }
  {
    Utterance u(Priority::Urgent);
    u.add("~400").add("baro_failed");
    Utterance q = audio::withSignal(u);
    r.check(q.count == 2 && strcmp(q.clips[0], "~400") == 0, "an utterance that starts with a tone got another");
  }
  {
    Utterance u(Priority::Urgent);
    for (int i = 0; i < Utterance::kMaxClips; ++i) u.add("n1");
    Utterance q = audio::withSignal(u);
    r.check(q.count == Utterance::kMaxClips && strcmp(q.clips[0], "n1") == 0, "a full utterance was changed (%d clips)", q.count);
  }
  {
    Utterance u(Priority::Urgent);
    Utterance q = audio::withSignal(u);
    r.check(q.count == 0, "an empty urgent utterance got a signal");
  }
  {  // the cue is recorded, not played, while capturing
    audio::testCapture(true);
    audio::cue(1800.0f, 15);
    r.check(audio::testSaid() == 1 && strcmp(audio::testSaidText(0), "[cue] ~1800") == 0, "cue not recorded: '%s'", audio::testSaidText(0));
    audio::testCapture(false);
  }
}

#endif
