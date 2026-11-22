#include "SafeArea.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

#include <new>

#include "Log.h"
#include "SafeAreaGeo.h"
#include "Settings.h"
#include "audio/Speech.h"
#include "nav/Geo.h"

using audio::Priority;
using audio::Utterance;
using safearea::Pt;

namespace {
constexpr const char* kFile = "/areas.json";
constexpr uint32_t kEvalMs = 500;       // how often the position is judged
constexpr float kCloseM = 25.0f;        // this close to a restricted area, moving, is a warning whichever way the track points
constexpr float kMinSpeedMs = 3.0f;     // below this there is no direction of travel to turn from
constexpr float kMaxExtentM = 100000.0f;
constexpr float kMinAreaM2 = 100.0f;
constexpr size_t kMaxFileBytes = 16384;  // 25 areas of 24 corners
constexpr float kFarM = 100.0f;          // an area more than this beyond its warning distance is not looked at every time
constexpr float kMaxSpeedMs = 40.0f;     // the fastest the pilot could close in on it (so the look is never later than the earliest arrival)
constexpr uint32_t kMaxLookMs = 60000;   // ...and never longer than a minute apart
constexpr uint8_t kConfirm = 2;  // evaluations (half a second each) on the same side in a row before a change is believed: GPS noise on an edge

// A stored corner in the local frame around (lat0, lon0).
inline Pt local(int32_t latE6, int32_t lonE6, double lat0, double lon0, double cosLat0) {
  const double kDegToRad = geo::kDegToRad;
  return {(float)((lonE6 * 1e-6 - lon0) * kDegToRad * geo::kEarthR * cosLat0), (float)((latE6 * 1e-6 - lat0) * kDegToRad * geo::kEarthR)};
}
}  // namespace

void SafeArea::begin() {
  File f = LittleFS.open(kFile, "r");
  if (!f) return;
  const size_t len = f.size();
  if (len == 0 || len > kMaxFileBytes) {
    f.close();
    return;
  }
  String s = f.readString();
  f.close();
  Area* tmp = nullptr;
  int n = 0;
  const char* err = nullptr;
  if (!parse(s.c_str(), tmp, n, &err)) {
    LOGW("SAF", "Stored safe areas not used: %s", err ? err : "?");
    return;
  }
  adopt(tmp, n);
  LOGI("SAF", "%d safe and %d restricted area(s) loaded", safeCount(), restrictedCount());
}

bool SafeArea::parse(const char* json, Area*& result, int& count, const char** err) {
  result = nullptr;
  count = 0;
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok || !doc["a"].is<JsonArray>()) {
    *err = "not valid JSON";
    return false;
  }
  JsonArray arr = doc["a"].as<JsonArray>();
  if (arr.size() > (size_t)kMaxAreas) {
    *err = "too many areas";
    return false;
  }
  Area* out = arr.size() ? new (std::nothrow) Area[arr.size()] : nullptr;
  if (arr.size() && !out) {
    *err = "out of memory";
    return false;
  }
  int na = 0;
  struct Free {  // the areas are not handed on unless all of them are good
    Area* p;
    bool keep;
    explicit Free(Area* a) : p(a), keep(false) {}
    ~Free() {
      if (!keep) delete[] p;
    }
  };
  Free guard(out);
  for (JsonObject a : arr) {
    Area& ar = out[na];
    strlcpy(ar.name, a["n"] | "", sizeof(ar.name));
    for (char* c = ar.name; *c; ++c)
      if (*c == '"' || *c == '\\' || (uint8_t)*c < 32) *c = ' ';
    ar.on = a["on"] | true;
    ar.kind = strcmp(a["k"] | "safe", "restricted") == 0 ? Restricted : Safe;
    ar.warnM = (uint16_t)constrain((int)(a["w"] | (ar.kind == Restricted ? 500 : 0)), 0, kMaxWarnM);  // when to be warned: see SafeArea.h
    JsonArray pts = a["p"].as<JsonArray>();
    if (pts.isNull() || pts.size() < 3 || pts.size() > (size_t)kMaxPoints) {
      *err = "an area needs 3 to 24 corners";
      return false;
    }
    int n = 0;
    for (JsonArray pt : pts) {
      if (pt.size() < 2) {
        *err = "a corner is not [latitude, longitude]";
        return false;
      }
      const double la = pt[0].as<double>(), lo = pt[1].as<double>();
      if (!(la >= -90 && la <= 90 && lo >= -180 && lo <= 180)) {
        *err = "a corner is not a latitude and longitude";
        return false;
      }
      ar.lat[n] = (int32_t)lround(la * 1e6);
      ar.lon[n] = (int32_t)lround(lo * 1e6);
      ++n;
    }
    ar.n = (uint8_t)n;
    // Sanity: one place on Earth, and a real area.
    const double lat0 = ar.lat[0] * 1e-6, lon0 = ar.lon[0] * 1e-6, c0 = cos(lat0 * geo::kDegToRad);
    Pt buf[kMaxPoints];
    float far = 0;
    for (int i = 0; i < n; ++i) {
      buf[i] = local(ar.lat[i], ar.lon[i], lat0, lon0, c0);
      far = fmaxf(far, fmaxf(fabsf(buf[i].e), fabsf(buf[i].n)));
    }
    if (far > kMaxExtentM) {
      *err = "an area is more than 100 km across";
      return false;
    }
    if (safearea::areaM2(buf, n) < kMinAreaM2) {
      *err = "an area is too small";
      return false;
    }
    ++na;
  }
  guard.keep = true;
  result = out;
  count = na;
  return true;
}

bool SafeArea::setFromJson(const char* json, const char** err) {
  static const char* none = "";
  if (!err) err = &none;
  Area* tmp = nullptr;
  int n = 0;
  if (!parse(json, tmp, n, err)) return false;
  Area* keep = areas_;  // put back if the file cannot be written
  const int keepN = nAreas_;
  areas_ = tmp;
  nAreas_ = n;
  if (!save()) {
    areas_ = keep;
    nAreas_ = keepN;
    delete[] tmp;
    *err = "could not store the areas";
    return false;
  }
  delete[] keep;
  reset();
  LOGI("SAF", "%d safe and %d restricted area(s) stored", safeCount(), restrictedCount());
  return true;
}

void SafeArea::toJson(String& out) const {
  out = "{\"a\":[";
  bool firstArea = true;
  for (int ai = 0; ai < nAreas_; ++ai) {
    const Area& a = areas_[ai];
    if (a.n == 0) continue;
    if (!firstArea) out += ',';
    firstArea = false;
    out += "{\"n\":\"";
    out += a.name;
    out += "\",\"on\":";
    out += a.on ? "true" : "false";
    out += a.kind == Restricted ? ",\"k\":\"restricted\"" : ",\"k\":\"safe\"";
    out += ",\"w\":";
    out += (unsigned)a.warnM;
    out += ",\"p\":[";
    for (int i = 0; i < a.n; ++i) {
      char b[40];
      snprintf(b, sizeof(b), "%s[%.6f,%.6f]", i ? "," : "", a.lat[i] * 1e-6, a.lon[i] * 1e-6);
      out += b;
    }
    out += "]}";
  }
  out += "]}";
}

bool SafeArea::save() const {
  String s;
  toJson(s);
  File f = LittleFS.open(kFile, "w");
  if (!f) return false;
  const size_t w = f.print(s);
  f.close();
  return w == s.length();
}

int SafeArea::count(Kind k) const {
  int c = 0;
  for (int i = 0; i < nAreas_; ++i)
    if (areas_[i].n >= 3 && areas_[i].on && areas_[i].kind == k) ++c;
  return c;
}

void SafeArea::reset() {
  state_ = State::None;
  rstate_ = RState::Clear;
  edgeM_ = rDistM_ = -1;
  outsideCount_ = insideCount_ = rInsideCount_ = 0;
  lastSpokeMs_ = lastRSpokeMs_ = 0;
  lookAgain();
}

const char* SafeArea::restrictedStateName() const {
  switch (rstate_) {
    case RState::Near: return "near";
    case RState::Inside: return "inside";
    default: return "clear";
  }
}

const char* SafeArea::stateName() const {
  switch (state_) {
    case State::Inside: return "inside";
    case State::Edge: return "edge";
    case State::Outside: return "outside";
    default: return "none";
  }
}

namespace {
// "Turn right to 2 o'clock": the turn to the heading `target`, from the track.
void adviseTurn(Utterance& u, float target, float track) {
  const float rel = safearea::wrap180(target - track);
  if (fabsf(rel) < 10.0f) return;  // already going that way: nothing to turn
  u.add(rel > 0 ? "turn_right|turn_right_now" : "turn_left|turn_left_now");
  speech::addClock(u, rel);
}
}  // namespace

void SafeArea::update(const NavState& nav, bool guidanceActive) {
  if (!nav.fix || (safeCount() == 0 && restrictedCount() == 0)) {
    state_ = State::None;
    rstate_ = RState::Clear;
    edgeM_ = rDistM_ = -1;
    lookAgain();  // the position will have moved on by the time there is a fix again
    return;
  }
  if (nav.ms - lastEvalMs_ < kEvalMs) return;
  lastEvalMs_ = nav.ms;

  const double c0 = cos(nav.lat * geo::kDegToRad);
  const bool moving = nav.speedOk && nav.speedMs >= kMinSpeedMs;
  const Pt dir = safearea::unit(nav.trackDeg);

  // ---- look at every area from where the pilot is
  bool inAny = false;  // inside a safe area
  float inDist = 0, inMargin = -1e30f;
  uint16_t inWarn = 0;
  float outDist = 1e30f;  // outside every safe area: the distance to the nearest edge
  bool rInside = false;   // inside a restricted area
  float rExitDist = 1e30f;
  Pt rExitAt = {0, 0};  // its nearest edge
  float rAhead = -1;    // the nearest restricted area the track runs into within its warning distance
  Pt rBuf[kMaxPoints];
  int rN = 0;
  for (int ai = 0; ai < nAreas_; ++ai) {
    Area& a = areas_[ai];
    if (a.n < 3 || !a.on) continue;
    Pt buf[kMaxPoints];
    safearea::Near nr;
    bool in;
    const bool fresh = nav.ms >= a.nextMs;
    if (fresh) {
      for (int i = 0; i < a.n; ++i) buf[i] = local(a.lat[i], a.lon[i], nav.lat, nav.lon, c0);
      nr = safearea::nearest(buf, a.n, 0, 0);
      in = safearea::contains(buf, a.n, 0, 0);
      // An area well clear of the pilot (and of its warning distance) cannot matter until the pilot could have flown there: look again then
      const float slack = nr.dist - (float)a.warnM;
      a.nextMs = 0;
      if (slack > kFarM && !(in && a.kind == Restricted)) {
        const float s = fminf(slack / kMaxSpeedMs * 1000.0f, (float)kMaxLookMs);
        a.nextMs = nav.ms + (uint32_t)s;
        if (a.nextMs == 0) a.nextMs = 1;
      }
      a.in = in;
      a.dist = nr.dist;
      a.atE = nr.at.e;
      a.atN = nr.at.n;
    } else {  // the last look still stands
      in = a.in;
      nr.dist = a.dist;
      nr.at = {a.atE, a.atN};
    }
    if (a.kind == Safe) {
      if (in) {
        const float margin = nr.dist - a.warnM;  // how far inside the warning distance: the area the pilot is safest in counts
        if (!inAny || margin > inMargin) {
          inMargin = margin;
          inDist = nr.dist;
          inWarn = a.warnM;
        }
        inAny = true;
      } else if (nr.dist < outDist) {
        outDist = nr.dist;
      }
    } else if (in) {
      rInside = true;
      if (nr.dist < rExitDist) {
        rExitDist = nr.dist;
        rExitAt = nr.at;
      }
    } else if (a.warnM > 0) {
      float ahead = -1;
      if (moving && fresh) ahead = safearea::rayHit(buf, a.n, 0, 0, dir.e, dir.n, (float)a.warnM);
      // right beside it, and not moving away from it
      if (ahead < 0 && fresh && nr.dist < kCloseM && nav.speedMs > 1.0f && dir.e * nr.at.e + dir.n * nr.at.n > 0) ahead = nr.dist;
      if (ahead >= 0 && (rAhead < 0 || ahead < rAhead)) {
        rAhead = ahead;
        rN = a.n;
        for (int i = 0; i < a.n; ++i) rBuf[i] = buf[i];
      }
    }
  }

  // ---- safe areas: "You have left the safe area", and "Safe area edge" when a warning distance is set
  const bool speak = g_cfg.safeAreaOn && nav.flying && nav.gpsGood && !guidanceActive;
  const uint32_t repeatMs = (uint32_t)g_cfg.safeAreaRepeatS * 1000u;
  if (safeCount() == 0) {
    state_ = State::None;
    edgeM_ = -1;
  } else if (inAny) {
    outsideCount_ = 0;
    if (insideCount_ < 255) ++insideCount_;
    edgeM_ = inDist;
    if (!(state_ == State::Outside && insideCount_ < kConfirm)) {  // (not yet believed to be back)
      const State before = state_;
      const bool nearEdge = inWarn > 0 && inMargin < 0;
      state_ = nearEdge ? State::Edge : State::Inside;
      if (before == State::Outside) {
        if (speak) speech::say(Priority::High, "back_inside|~900");
      } else if (nearEdge && speak && (before != State::Edge || nav.ms - lastSpokeMs_ >= repeatMs)) {
        lastSpokeMs_ = nav.ms | 1;
        Utterance u(Priority::High);
        u.add("safe_edge|~700");
        speech::addDistance(u, inDist);
        audio::say(u);
      }
    }
  } else {
    insideCount_ = 0;
    if (outsideCount_ < 255) ++outsideCount_;
    edgeM_ = outDist;
    if (outsideCount_ >= kConfirm) {
      const State before = state_;
      state_ = State::Outside;
      if (speak) {
        if (before != State::Outside) {  // the moment of leaving
          lastSpokeMs_ = nav.ms | 1;
          speech::say(Priority::High, "left_area|outside_area|~400");
          LOGI("SAF", "left the safe area, %.0f m from the nearest edge", outDist);
        } else if (nav.ms - lastSpokeMs_ >= repeatMs) {  // still outside: repeated
          lastSpokeMs_ = nav.ms | 1;
          speech::say(Priority::High, "outside_area|~400");
        }
      }
    }
  }

  // ---- restricted areas: the way to turn, ahead of one and inside one
  if (restrictedCount() == 0) {
    rstate_ = RState::Clear;
    rDistM_ = -1;
    return;
  }
  const bool rSpeak = g_cfg.restrictedAreaOn && nav.flying && nav.gpsGood && !guidanceActive;
  const uint32_t rRepeatMs = (uint32_t)g_cfg.restrictedRepeatS * 1000u;
  if (rInside) {
    if (rInsideCount_ < 255) ++rInsideCount_;
  } else {
    rInsideCount_ = 0;
  }
  RState want = RState::Clear;
  bool settling = false;  // inside for only one look so far: not said yet
  if (rInside) {
    want = RState::Inside;
    if (rInsideCount_ < kConfirm && rstate_ != RState::Inside) {
      want = RState::Near;
      settling = true;
    }
  } else if (rAhead >= 0) {
    want = RState::Near;
  }
  rDistM_ = rInside ? 0 : rAhead;
  const RState before = rstate_;
  rstate_ = want;
  if (want == RState::Clear || settling || !rSpeak) return;
  if (before == want && nav.ms - lastRSpokeMs_ < rRepeatMs) return;
  lastRSpokeMs_ = nav.ms | 1;
  Utterance u(Priority::High);
  if (want == RState::Inside) {
    u.add("in_restricted|restricted_area|~600");
    if (moving) adviseTurn(u, safearea::bearingOf(rExitAt.e, rExitAt.n), nav.trackDeg);  // out by the nearest edge
    LOGI("SAF", "inside a restricted area, %.0f m from its edge", rExitDist);
  } else {
    u.add("restricted_area|~600");
    speech::addDistance(u, rAhead);
    if (moving) adviseTurn(u, safearea::avoidHeading(rBuf, rN, nav.trackDeg), nav.trackDeg);
    LOGI("SAF", "restricted area ahead: %.0f m, track %.0f", rAhead, nav.trackDeg);
  }
  audio::say(u);
}

#if PV_TEST
bool SafeArea::testSet(const char* json) {
  Area* tmp = nullptr;
  int n = 0;
  const char* err = nullptr;
  if (!parse(json, tmp, n, &err)) return false;
  adopt(tmp, n);
  reset();
  return true;
}
#endif
