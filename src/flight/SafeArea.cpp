#include "SafeArea.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

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
constexpr float kCloseM = 25.0f;        // this close to an edge is a warning whichever way the track points
constexpr float kMinSpeedMs = 3.0f;     // below this there is no direction of travel to turn from
constexpr float kMaxExtentM = 100000.0f;
constexpr float kMinAreaM2 = 100.0f;
constexpr uint8_t kOutsideConfirm = 2;  // evaluations outside in a row before it is believed (GPS noise on the edge)

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
  if (len == 0 || len > 4096) {
    f.close();
    return;
  }
  String s = f.readString();
  f.close();
  Area tmp[kMaxAreas];
  const char* err = nullptr;
  if (!parse(s.c_str(), tmp, &err)) {
    LOGW("SAF", "Stored safe areas not used: %s", err ? err : "?");
    return;
  }
  for (int i = 0; i < kMaxAreas; ++i) areas_[i] = tmp[i];
  LOGI("SAF", "%d safe area(s) loaded", activeCount());
}

bool SafeArea::parse(const char* json, Area* out, const char** err) {
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
  int na = 0;
  for (JsonObject a : arr) {
    Area& ar = out[na];
    ar = Area();
    strlcpy(ar.name, a["n"] | "", sizeof(ar.name));
    for (char* c = ar.name; *c; ++c)
      if (*c == '"' || *c == '\\' || (uint8_t)*c < 32) *c = ' ';
    ar.on = a["on"] | true;
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
  for (int i = na; i < kMaxAreas; ++i) out[i] = Area();
  return true;
}

bool SafeArea::setFromJson(const char* json, const char** err) {
  static const char* none = "";
  if (!err) err = &none;
  Area tmp[kMaxAreas];
  if (!parse(json, tmp, err)) return false;
  Area keep[kMaxAreas];
  for (int i = 0; i < kMaxAreas; ++i) {
    keep[i] = areas_[i];
    areas_[i] = tmp[i];
  }
  if (!save()) {
    for (int i = 0; i < kMaxAreas; ++i) areas_[i] = keep[i];
    *err = "could not store the areas";
    return false;
  }
  reset();
  LOGI("SAF", "%d safe area(s) stored", activeCount());
  return true;
}

void SafeArea::toJson(String& out) const {
  out = "{\"a\":[";
  bool firstArea = true;
  for (const Area& a : areas_) {
    if (a.n == 0) continue;
    if (!firstArea) out += ',';
    firstArea = false;
    out += "{\"n\":\"";
    out += a.name;
    out += "\",\"on\":";
    out += a.on ? "true" : "false";
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

int SafeArea::activeCount() const {
  int c = 0;
  for (const Area& a : areas_)
    if (a.n >= 3 && a.on) ++c;
  return c;
}

void SafeArea::reset() {
  state_ = State::None;
  edgeM_ = -1;
  outsideCount_ = 0;
  wasOutside_ = false;
  lastSpokeMs_ = 0;
}

const char* SafeArea::stateName() const {
  switch (state_) {
    case State::Inside: return "inside";
    case State::Edge: return "edge";
    case State::Outside: return "outside";
    default: return "none";
  }
}

void SafeArea::update(const NavState& nav, bool guidanceActive) {
  if (!nav.fix || activeCount() == 0) {
    state_ = State::None;
    edgeM_ = -1;
    return;
  }
  if (nav.ms - lastEvalMs_ < kEvalMs) return;
  lastEvalMs_ = nav.ms;

  const double c0 = cos(nav.lat * geo::kDegToRad);
  const bool moving = nav.speedOk && nav.speedMs >= kMinSpeedMs;
  const float lead = fmaxf(g_cfg.safeAreaWarnM, nav.speedMs * (float)g_cfg.safeAreaWarnS);
  const Pt dir = safearea::unit(nav.trackDeg);

  // The area the pilot is best placed in (the one the track stays inside longest), or else the nearest one.
  bool inAny = false;
  float bestRay = -2, bestNear = 0;   // bestRay: distance to the exit along the track, +1e9 when there is none
  Pt bufIn[kMaxPoints];               // the polygon chosen for the way out
  int nIn = 0;
  float outDist = 1e30f;
  Pt outAt = {0, 0};
  for (const Area& a : areas_) {
    if (a.n < 3 || !a.on) continue;
    Pt buf[kMaxPoints];
    for (int i = 0; i < a.n; ++i) buf[i] = local(a.lat[i], a.lon[i], nav.lat, nav.lon, c0);
    const safearea::Near nr = safearea::nearest(buf, a.n, 0, 0);
    if (safearea::contains(buf, a.n, 0, 0)) {
      float ray = 1e9f;
      if (moving) {
        const float r = safearea::rayHit(buf, a.n, 0, 0, dir.e, dir.n, 1e7f);
        if (r >= 0) ray = r;
      }
      if (!inAny || ray > bestRay || (ray == bestRay && nr.dist > bestNear)) {
        inAny = true;
        bestRay = ray;
        bestNear = nr.dist;
        nIn = a.n;
        for (int i = 0; i < a.n; ++i) bufIn[i] = buf[i];
      }
    } else if (nr.dist < outDist) {
      outDist = nr.dist;
      outAt = nr.at;
    }
  }

  const bool speak = g_cfg.safeAreaOn && nav.flying && nav.gpsGood && !guidanceActive;
  const uint32_t repeatMs = (uint32_t)g_cfg.safeAreaRepeatS * 1000u;

  if (inAny) {
    outsideCount_ = 0;
    edgeM_ = bestNear;
    const bool leaving = bestRay <= lead;                    // the track leaves the area within the warning distance
    const bool close = bestNear < kCloseM && nav.speedMs > 1.0f;  // right on an edge
    if (leaving || close) {
      const State before = state_;
      state_ = State::Edge;
      if (speak && (before != State::Edge || nav.ms - lastSpokeMs_ >= repeatMs)) {
        lastSpokeMs_ = nav.ms | 1;
        Utterance u(Priority::High);
        u.add("safe_edge|~700");
        speech::addDistance(u, leaving ? bestRay : bestNear);
        if (moving) {
          const float target = safearea::escapeHeading(bufIn, nIn, nav.trackDeg, lead);
          const float rel = safearea::wrap180(target - nav.trackDeg);
          u.add(rel > 0 ? "turn_right|turn_right_now" : "turn_left|turn_left_now");
          speech::addClock(u, rel);
        }
        audio::say(u);
        LOGI("SAF", "edge: %.0f m ahead, track %.0f", leaving ? bestRay : bestNear, nav.trackDeg);
      }
    } else {
      state_ = State::Inside;
      if (wasOutside_ && speak) speech::say(Priority::High, "back_inside|~900");
      wasOutside_ = false;
    }
    return;
  }

  // Outside every area.
  edgeM_ = outDist;
  if (outsideCount_ < 255) ++outsideCount_;
  if (outsideCount_ < kOutsideConfirm) return;
  const State before = state_;
  state_ = State::Outside;
  wasOutside_ = true;
  if (speak && (before != State::Outside || nav.ms - lastSpokeMs_ >= repeatMs)) {
    lastSpokeMs_ = nav.ms | 1;
    Utterance u(Priority::High);
    u.add("outside_area|~400");
    speech::addDistance(u, outDist);
    if (moving) {
      const float rel = safearea::wrap180(safearea::bearingOf(outAt.e, outAt.n) - nav.trackDeg);
      u.add(rel > 0 ? "turn_right|turn_right_now" : "turn_left|turn_left_now");
      speech::addClock(u, rel);
    }
    audio::say(u);
    LOGI("SAF", "outside: %.0f m from the nearest area", outDist);
  }
}
