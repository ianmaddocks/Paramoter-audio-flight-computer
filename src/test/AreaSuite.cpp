// Unit suite "areas": the safe and restricted areas (flight/SafeArea.cpp) driven by scripted positions, as the flight logic drives them
// every 100 ms in flight: a pilot flying a straight track at 10 m/s, one evaluation per half second, past and through the areas. What it
// checks is what is said (captured instead of played), when, and what is left out. The areas are loaded with testSet(), which stores
// nothing: the pilot's own areas on the vario are never touched.
#include "test/UnitTest.h"

#if PV_TEST

#include <math.h>
#include <string>

#include "Settings.h"
#include "audio/Audio.h"
#include "flight/SafeArea.h"
#include "nav/Geo.h"
#include "nav/Nav.h"

namespace {

constexpr double kMperDeg = geo::kEarthR * geo::kDegToRad;
constexpr double kLat0 = 51.5, kLon0 = 0.5;  // a made-up place

struct CfgGuard {
  Settings saved;
  CfgGuard() : saved(g_cfg) {}
  ~CfgGuard() { g_cfg = saved; }
};

// One rectangular area, corners (e0,n0)-(e1,n1) in metres east and north of the made-up origin.
String rect(const char* kind, int warn, float e0, float n0, float e1, float n1, bool on = true) {
  const float c[4][2] = {{e0, n0}, {e0, n1}, {e1, n1}, {e1, n0}};
  String s = String("{\"n\":\"T\",\"on\":") + (on ? "true" : "false") + ",\"k\":\"" + kind + "\",\"w\":" + warn + ",\"p\":[";
  for (int i = 0; i < 4; ++i) {
    char b[56];
    snprintf(b, sizeof b, "%s[%.6f,%.6f]", i ? "," : "", kLat0 + c[i][1] / kMperDeg, kLon0 + c[i][0] / (kMperDeg * cos(kLat0 * geo::kDegToRad)));
    s += b;
  }
  return s + "]}";
}

String areas(const String& a, const String& b = "") { return "{\"a\":[" + a + (b.length() ? "," + b : String("")) + "]}"; }

struct Rig {
  SafeArea sa;
  float e = 0, n = 0, track = 0, speed = 10;
  uint32_t ms = 1000;
  bool flying = true, guidance = false;

  bool load(const String& json) { return sa.testSet(json.c_str()); }
  void place(float e_, float n_, float trk) {
    e = e_;
    n = n_;
    track = trk;
    sa.reset();
    audio::testCapture(true);
  }
  void step() {  // half a second of flight at the speed and track
    ms += 500;
    e += sinf(track * geo::kDegToRadF) * speed * 0.5f;
    n += cosf(track * geo::kDegToRadF) * speed * 0.5f;
    NavState nav;
    nav.ms = ms;
    nav.fix = nav.gpsGood = nav.speedOk = true;
    nav.speedMs = speed;
    nav.trackDeg = track;
    nav.flying = flying;
    nav.lat = kLat0 + n / kMperDeg;
    nav.lon = kLon0 + e / (kMperDeg * cos(kLat0 * geo::kDegToRad));
    sa.update(nav, guidance);
  }
  // Steps until at least `calls` utterances have been said (at most `max` steps); returns the steps taken, -1 if it did not happen.
  int until(int calls, int max = 400) {
    for (int i = 1; i <= max; ++i) {
      step();
      if (audio::testSaid() >= calls) return i;
    }
    return -1;
  }
  void steps(int k) {
    for (int i = 0; i < k; ++i) step();
  }
};

std::string said(int i) { return std::string(audio::testSaidText(i)); }
bool has(int i, const char* w) { return said(i).find(w) != std::string::npos; }

Rig rig;  // static: the polygons are bigger than the stack of the web server task is happy with

}  // namespace

void areasUnitSuite(UnitReport& r) {
  CfgGuard guard;
  g_cfg.distUnit = 0;  // metres
  g_cfg.safeAreaOn = g_cfg.restrictedAreaOn = true;
  g_cfg.safeAreaRepeatS = 20;
  g_cfg.restrictedRepeatS = 10;
  audio::testCapture(true);
  Rig& g = rig;
  g.flying = true;
  g.guidance = false;

  // ---------------------------------------------------------------- data: what is accepted, the defaults
  {
    SafeArea s;
    const char* bad[] = {
        "not json",
        "{\"a\":[{\"n\":\"x\",\"p\":[[51.5,0.5],[51.6,0.5]]}]}",                                           // two corners
        "{\"a\":[{\"n\":\"x\",\"p\":[[91,0.5],[51.6,0.5],[51.6,0.6]]}]}",                                  // latitude 91
        "{\"a\":[{\"n\":\"x\",\"p\":[[51.5,0.5],[51.5,0.50003],[51.50003,0.50003]]}]}",                   // a few metres across
        "{\"a\":[{\"n\":\"x\",\"p\":[[51.5,0.5],[51.6,0.5],[90,170]]}]}",                                 // over 100 km across
    };
    for (const char* b : bad) r.check(!s.testSet(b), "data not refused: %.40s", b);
    {  // 25 areas are accepted, 26 are not
      const char* one = "{\"p\":[[51.5,0.5],[51.5,0.51],[51.51,0.51]]}";
      String many = "{\"a\":[";
      for (int i = 0; i < 26; ++i) {
        if (i) many += ',';
        many += one;
        if (i == 24) {
          String ok = many + "]}";
          r.check(s.testSet(ok.c_str()) && s.safeCount() == 25, "25 areas not accepted");
        }
      }
      many += "]}";
      r.check(!s.testSet(many.c_str()), "26 areas accepted");
      String big = "{\"a\":[{\"n\":\"";  // more than 5 kB of data is refused whatever it holds
      for (int i = 0; i < 5300; ++i) big += 'x';
      big += "\",\"p\":[[51.5,0.5],[51.5,0.51],[51.51,0.51]]}]}";
      r.check(!s.testSet(big.c_str()), "over 5 kB accepted");
    }
    r.check(s.testSet("{\"a\":[]}"), "an empty list is valid");
    String two = areas(rect("safe", 0, -100, -100, 100, 100), rect("restricted", 500, 300, 300, 400, 400));
    r.check(s.testSet(two.c_str()), "two areas not accepted");
    r.check(s.safeCount() == 1 && s.restrictedCount() == 1, "counts %d %d", s.safeCount(), s.restrictedCount());
    String out;
    s.toJson(out);
    r.check(out.indexOf("\"k\":\"safe\",\"w\":0") > 0 && out.indexOf("\"k\":\"restricted\",\"w\":500") > 0, "kinds and warning distances not written: %.80s", out.c_str());
    // no "k" and no "w": safe, 0; a restricted one with no "w": 500
    String d1 = "{\"a\":[{\"n\":\"a\",\"p\":[[51.5,0.5],[51.5,0.51],[51.51,0.51]]},{\"n\":\"b\",\"k\":\"restricted\",\"p\":[[51.5,0.5],[51.5,0.51],[51.51,0.51]]},"
                "{\"n\":\"c\",\"k\":\"banana\",\"p\":[[51.5,0.5],[51.5,0.51],[51.51,0.51]]}]}";
    r.check(s.testSet(d1.c_str()), "defaults data refused");
    s.toJson(out);
    r.check(out.indexOf("\"n\":\"a\",\"on\":true,\"k\":\"safe\",\"w\":0") >= 0, "safe default is 0: %.90s", out.c_str());
    r.check(out.indexOf("\"n\":\"b\",\"on\":true,\"k\":\"restricted\",\"w\":500") >= 0, "restricted default is 500");
    r.check(out.indexOf("\"n\":\"c\",\"on\":true,\"k\":\"safe\",\"w\":0") >= 0, "an unknown kind is safe");
  }

  // ---------------------------------------------------------------- a safe area, warning distance 0: told only on leaving
  g.load(areas(rect("safe", 0, -500, -500, 500, 500)));
  g.place(0, 0, 0);
  g.steps(99);  // to 495 m north: still 5 m inside
  r.check(audio::testSaid() == 0, "safe: said something inside the area: %s", said(0).c_str());
  r.check(g.sa.state() == SafeArea::State::Inside && g.sa.edgeM() < 10, "safe: inside, edge %.0f", g.sa.edgeM());
  int k = g.until(1, 12);
  r.check(k > 0 && g.n > 500 && g.n < 525, "safe: left it at %.0f m (steps %d)", g.n, k);
  r.check(has(0, "[high] left_area|outside_area|~400"), "safe: leaving said %s", said(0).c_str());
  r.check(!has(0, "turn") && !has(0, "oclock") && !has(0, "metres"), "safe: leaving must have no directions or distances: %s", said(0).c_str());
  r.check(g.sa.state() == SafeArea::State::Outside, "safe: state outside");
  g.steps(36);  // 18 s outside
  r.check(audio::testSaid() == 1, "safe: repeated too soon (%d calls)", audio::testSaid());
  k = g.until(2, 10);
  r.check(k > 0 && k <= 7 && has(1, "[high] outside_area|~400") && !has(1, "left_area"), "safe: repeat after about 20 s: %d steps, %s", k, said(1).c_str());
  g.track = 180;  // back the way it came
  for (int i = 0; i < 90 && g.sa.state() != SafeArea::State::Inside; ++i) g.step();
  r.check(g.sa.state() == SafeArea::State::Inside, "safe: back inside");
  r.check(has(audio::testSaid() - 1, "back_inside"), "safe: return said %s", said(audio::testSaid() - 1).c_str());
  const int before = audio::testSaid();
  g.steps(20);
  r.check(audio::testSaid() == before, "safe: spoke again while inside");

  // ---- on the ground, under guidance, or switched off: nothing is said, but the state is kept
  g.flying = false;
  g.place(0, 480, 0);
  g.steps(20);
  r.check(audio::testSaid() == 0 && g.sa.state() == SafeArea::State::Outside, "ground: said %d, state %s", audio::testSaid(), g.sa.stateName());
  g.flying = true;
  g.guidance = true;
  g.place(0, 600, 0);
  g.steps(10);
  r.check(audio::testSaid() == 0, "guidance: a landing approach or emergency glide must come first");
  g.guidance = false;
  g.steps(2);
  r.check(audio::testSaid() == 1 && has(0, "outside_area"), "outside when the guidance ends: %s", said(0).c_str());
  g.place(0, 600, 0);
  g.steps(8);
  const int calls = audio::testSaid();
  g_cfg.safeAreaOn = false;
  g.place(0, 600, 0);
  g.steps(60);
  r.check(audio::testSaid() == 0 && calls >= 1, "switched off: still said %d", audio::testSaid());
  g_cfg.safeAreaOn = true;

  // ---------------------------------------------------------------- a safe area with a warning distance: also told near the edge
  g.load(areas(rect("safe", 200, -500, -500, 500, 500)));
  g.place(0, 0, 0);
  k = g.until(1, 200);
  r.check(k > 0 && g.n > 295 && g.n < 325, "safe edge: said at %.0f m, expected about 300", g.n);
  r.check(has(0, "[high] safe_edge|~700") && has(0, "metres") && !has(0, "turn") && !has(0, "oclock"), "safe edge: %s", said(0).c_str());
  r.check(g.sa.state() == SafeArea::State::Edge, "safe edge: state");

  // ---------------------------------------------------------------- restricted area 1000 m north, warning distance 500
  g.load(areas(rect("restricted", 500, -250, 1000, 250, 1500)));
  g.place(0, 0, 0);
  k = g.until(1, 300);
  r.check(k > 0 && g.n > 490 && g.n < 520, "restricted: first warning at %.0f m, expected about 500", g.n);
  r.check(has(0, "[high] restricted_area|~600") && has(0, "metres") && !has(0, "in_restricted"), "restricted: ahead said %s", said(0).c_str());
  r.check(has(0, "turn_right|turn_right_now") && has(0, "n1 oclock"), "restricted: the turn should be right to 1 o'clock (30 degrees): %s", said(0).c_str());
  r.check(g.sa.restrictedState() == SafeArea::RState::Near && g.sa.restrictedM() > 400 && g.sa.restrictedM() <= 500, "restricted: near, %.0f m",
          g.sa.restrictedM());
  g.steps(18);
  r.check(audio::testSaid() == 1, "restricted: repeated within 9 s (%d)", audio::testSaid());
  k = g.until(2, 6);
  r.check(k > 0 && k <= 4 && has(1, "restricted_area"), "restricted: repeat after 10 s: %d steps", k);
  // on through it
  for (int i = 0; i < 200 && g.n < 1030; ++i) g.step();
  int inside = -1;
  for (int i = 0; i < audio::testSaid() && i < 16; ++i)
    if (has(i, "in_restricted")) inside = i;
  r.check(inside >= 0, "restricted: nothing said on entering (n %.0f)", g.n);
  if (inside >= 0) {
    r.check(has(inside, "[high] in_restricted|restricted_area|~600") && has(inside, "turn_right|turn_right_now") && has(inside, "n6 oclock"),
            "restricted: inside said %s", said(inside).c_str());
  }
  r.check(g.sa.restrictedState() == SafeArea::RState::Inside, "restricted: state inside, n %.0f", g.n);
  for (int i = 0; i < 200 && g.n < 1560; ++i) g.step();
  r.check(g.sa.restrictedState() == SafeArea::RState::Clear, "restricted: clear after leaving, n %.0f", g.n);

  // ---- passing beside it, 150 m off the edge: no warning
  g.place(400, 0, 0);
  g.steps(330);
  r.check(audio::testSaid() == 0, "beside: said %d: %s", audio::testSaid(), said(0).c_str());

  // ---- warning distance 0: only when inside
  g.load(areas(rect("restricted", 0, -250, 1000, 250, 1500)));
  g.place(0, 0, 0);
  g.steps(190);
  r.check(audio::testSaid() == 0, "restricted 0: said before entering: %s", said(0).c_str());
  k = g.until(1, 30);
  r.check(k > 0 && has(0, "in_restricted") && g.n > 1000 && g.n < 1020, "restricted 0: entering said %s at %.0f", said(0).c_str(), g.n);

  // ---- switched off, ground, setting off
  g.load(areas(rect("restricted", 500, -250, 1000, 250, 1500, false)));
  g.place(0, 0, 0);
  g.steps(300);
  r.check(audio::testSaid() == 0 && g.sa.restrictedCount() == 0, "restricted area switched off: said %d", audio::testSaid());
  g.load(areas(rect("restricted", 500, -250, 1000, 250, 1500)));
  g_cfg.restrictedAreaOn = false;
  g.place(0, 0, 0);
  g.steps(300);
  r.check(audio::testSaid() == 0, "restricted warnings off in the settings: said %d", audio::testSaid());
  g_cfg.restrictedAreaOn = true;
  g.flying = false;
  g.place(0, 0, 0);
  g.steps(300);
  r.check(audio::testSaid() == 0 && g.sa.restrictedState() == SafeArea::RState::Inside, "ground: said %d, state %s", audio::testSaid(),
          g.sa.restrictedStateName());
  g.flying = true;
  g.guidance = true;
  g.place(0, 0, 0);
  g.steps(300);
  r.check(audio::testSaid() == 0, "guidance: restricted warnings must wait for the landing guidance: %d", audio::testSaid());
  g.guidance = false;

  // ---------------------------------------------------------------- both: a big safe area with a restricted one inside it
  g.load(areas(rect("safe", 0, -3000, -3000, 3000, 3000), rect("restricted", 500, -250, 1000, 250, 1500)));
  g.place(0, 0, 0);
  g.steps(200);
  bool leftSaid = false;
  for (int i = 0; i < audio::testSaid() && i < 16; ++i)
    if (has(i, "left_area")) leftSaid = true;
  r.check(audio::testSaid() >= 1 && !leftSaid, "both: %d calls, safe-area call %d", audio::testSaid(), leftSaid);
  r.check(g.sa.state() == SafeArea::State::Inside, "both: inside the safe area");

  // ---------------------------------------------------------------- a track away from a restricted area, and slowly: nothing
  g.load(areas(rect("restricted", 500, -250, 1000, 250, 1500)));
  g.place(0, 0, 180);  // flying away from it
  g.steps(200);
  r.check(audio::testSaid() == 0, "flying away: said %s", said(0).c_str());
  g.place(0, 900, 0);
  g.speed = 2;  // too slow for a direction of travel: the distance only is not enough to warn of an area ahead
  g.steps(40);
  r.check(audio::testSaid() == 0, "creeping at 2 m/s: said %s", said(0).c_str());
  g.speed = 10;

  // ---------------------------------------------------------------- 50 mph over the ground (22.35 m/s, a strong tailwind), 25 areas
  // 24 far-off restricted areas are looked at only now and then; the one ahead must still be warned of at its warning distance, and the
  // pilot must not get into it before being told.
  {
    String many = "{\"a\":[";
    for (int i = 0; i < 24; ++i) many += rect("restricted", 500, 20000 + 700 * i, 3000, 20400 + 700 * i, 3400) + ",";
    many += rect("restricted", 500, -250, 1000, 250, 1500) + "]}";
    g.load(many);
    r.check(g.sa.restrictedCount() == 25, "25 areas: %d loaded", g.sa.restrictedCount());
    g.speed = 22.35f;
    g.place(0, -4000, 0);  // 5 km short of it
    k = g.until(1, 600);
    r.check(k > 0 && g.n > 420 && g.n < 512, "50 mph, 25 areas: first warning at n %.0f, expected 500 m short of the area (n 500)", g.n);
    r.check(k > 0 && has(0, "[high] restricted_area|~600") && !has(0, "in_restricted"), "50 mph: said %s", said(0).c_str());
    // the same from a few starting points, so that the far-away look falls at different moments
    for (int off = 0; off < 6; ++off) {
      g.place(0, -2300.0f - 37.0f * off, 0);
      k = g.until(1, 400);
      r.check(k > 0 && g.n > 420 && g.n < 512, "50 mph, start %d: first warning at n %.0f", off, g.n);
    }
    // a safe area at that speed: told promptly on leaving it (the area edge is 250 m ahead)
    g.load(areas(rect("safe", 0, -300, -400, 300, 250), rect("restricted", 500, 30000, 30000, 30400, 30400)));
    g.place(0, 0, 0);
    k = g.until(1, 200);
    r.check(k > 0 && g.n > 250 && g.n < 250 + 22.35f * 3.0f, "50 mph, leaving the safe area: said at n %.0f, expected within 3 s of the edge", g.n);
    g.speed = 10;
  }

  audio::testCapture(false);
}

#endif
