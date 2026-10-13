#include "Wind.h"
#include "Log.h"

#include <Arduino.h>

#include "Settings.h"
#include "nav/Geo.h"

namespace {
constexpr uint32_t kSampleIntervalMs = 500;
constexpr uint32_t kWindowMs = 60000;
constexpr uint32_t kFitIntervalMs = 5000;
constexpr int kMinBins = 9;  // of 12 x 30 degree track bins, i.e. >= 270 degrees of turn
constexpr float kMinSampleSpeed = 3.0f;
}  // namespace

void Wind::onTakeoff(bool courseValid, float courseDeg) {
  sampleCount_ = sampleHead_ = 0;
  if (courseValid) {
    LOGI("WIND", "from takeoff run: %.0f deg", courseDeg);
    src_ = WindSource::Takeoff;
    fromDeg_ = courseDeg;
    speedMs_ = 0;
    airspeedMs_ = 0;
  }
}

void Wind::update(const NavState& nav) {
  if (!nav.flying || !nav.fix || nav.speedMs < kMinSampleSpeed) return;
  if (nav.ms - lastSampleMs_ < kSampleIntervalMs) return;
  lastSampleMs_ = nav.ms;

  float t = nav.trackDeg * geo::kDegToRadF;
  int idx = (sampleHead_ + sampleCount_) % kMaxSamples;
  if (sampleCount_ == kMaxSamples) sampleHead_ = (sampleHead_ + 1) % kMaxSamples;
  else ++sampleCount_;
  samples_[idx] = {nav.ms, nav.speedMs * sinf(t), nav.speedMs * cosf(t)};

  if (nav.ms - lastFitMs_ >= kFitIntervalMs) {
    lastFitMs_ = nav.ms;
    tryFit(nav.ms);
  }
}

// Ground velocity = air velocity + wind, so with constant airspeed the ground-velocity
// vectors lie on a circle centred on the wind vector (Kasa least-squares circle fit).
void Wind::tryFit(uint32_t now) {
  bool bins[12] = {};
  double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0, sxz = 0, syz = 0, sz = 0;
  int n = 0;
  for (int i = 0; i < sampleCount_; ++i) {
    const Sample& s = samples_[(sampleHead_ + i) % kMaxSamples];
    if (now - s.ms > kWindowMs) continue;
    bins[(int)(geo::vecBearing(s.e, s.n) / 30.0f) % 12] = true;
    double z = s.e * s.e + s.n * s.n;
    sx += s.e;
    sy += s.n;
    sxx += s.e * s.e;
    syy += s.n * s.n;
    sxy += s.e * s.n;
    sxz += s.e * z;
    syz += s.n * z;
    sz += z;
    ++n;
  }
  int filled = 0;
  for (bool b : bins) filled += b;
  if (n < 20 || filled < kMinBins) return;

  // Solve [sxx sxy sx; sxy syy sy; sx sy n] [D E F]' = -[sxz syz sz]'
  double a[3][4] = {{sxx, sxy, sx, -sxz}, {sxy, syy, sy, -syz}, {sx, sy, (double)n, -sz}};
  for (int c = 0; c < 3; ++c) {
    int piv = c;
    for (int r = c + 1; r < 3; ++r)
      if (fabs(a[r][c]) > fabs(a[piv][c])) piv = r;
    if (fabs(a[piv][c]) < 1e-9) return;
    if (piv != c)
      for (int k = 0; k < 4; ++k) std::swap(a[c][k], a[piv][k]);
    for (int r = 0; r < 3; ++r) {
      if (r == c) continue;
      double f = a[r][c] / a[c][c];
      for (int k = c; k < 4; ++k) a[r][k] -= f * a[c][k];
    }
  }
  double D = a[0][3] / a[0][0], E = a[1][3] / a[1][1], F = a[2][3] / a[2][2];
  float cx = -D / 2, cy = -E / 2;
  float r2 = cx * cx + cy * cy - F;
  if (r2 <= 0) return;
  float r = sqrtf(r2);

  double err = 0;
  for (int i = 0; i < sampleCount_; ++i) {
    const Sample& s = samples_[(sampleHead_ + i) % kMaxSamples];
    if (now - s.ms > kWindowMs) continue;
    float d = hypotf(s.e - cx, s.n - cy) - r;
    err += d * d;
  }
  float rms = sqrtf(err / n);
  float wspd = hypotf(cx, cy);
  if (rms > 2.0f || r < 5.0f || r > 25.0f || wspd > 15.0f) {
    LOGD("WIND", "fit rejected: n=%d bins=%d airspeed %.1f wind %.1f rms %.2f", n, filled, r, wspd, rms);
    return;
  }

  // Wind vector (cx, cy) points where the air is going; "from" is the opposite bearing.
  float from = geo::vecBearing(-cx, -cy);
  if (src_ == WindSource::Circling) {
    float pe = speedMs_ * sinf(fromDeg_ * geo::kDegToRadF);
    float pn = speedMs_ * cosf(fromDeg_ * geo::kDegToRadF);
    float ne = (pe - cx) / 2, nn = (pn - cy) / 2;
    fromDeg_ = geo::vecBearing(ne, nn);
    speedMs_ = hypotf(ne, nn);
    airspeedMs_ = (airspeedMs_ + r) / 2;
  } else {
    fromDeg_ = from;
    speedMs_ = wspd;
    airspeedMs_ = r;
  }
  src_ = WindSource::Circling;
  sampleCount_ = sampleHead_ = 0;
  LOGI("WIND", "Wind fit: from %.0f deg %.1f m/s, airspeed %.1f m/s (rms %.2f)", fromDeg_, speedMs_,
        airspeedMs_, rms);
}

void Wind::exportState(FlightRecord& r) const {
  r.windSource = (uint8_t)src_;
  r.windFromDeg = fromDeg_;
  r.windSpeedMs = speedMs_;
  r.windAirspeedMs = airspeedMs_;
}

void Wind::importState(const FlightRecord& r) {
  if (r.windSource > (uint8_t)WindSource::Circling) return;
  src_ = (WindSource)r.windSource;
  fromDeg_ = r.windFromDeg;
  speedMs_ = r.windSpeedMs;
  airspeedMs_ = r.windAirspeedMs;
}

bool Wind::valid() const { return g_cfg.windManual || src_ != WindSource::None; }

float Wind::fromDeg() const { return g_cfg.windManual ? g_cfg.windFromDeg : fromDeg_; }

float Wind::speedMs() const { return g_cfg.windManual ? g_cfg.windSpeedMs : speedMs_; }

float Wind::airspeedMs() const {
  return (!g_cfg.windManual && airspeedMs_ > 0) ? airspeedMs_ : g_cfg.trimAirspeedMs;
}

WindSource Wind::source() const { return g_cfg.windManual ? WindSource::Manual : src_; }

const char* Wind::sourceName() const {
  switch (source()) {
    case WindSource::Takeoff: return "takeoff";
    case WindSource::Circling: return "circling";
    case WindSource::Manual: return "manual";
    default: return "none";
  }
}

float Wind::groundGlide(float trackDeg) const {
  float va = airspeedMs();
  float along = -speedMs() * cosf((trackDeg - fromDeg()) * geo::kDegToRadF);
  float gs = fmaxf(va + along, 1.0f);
  return g_cfg.glideRatio * gs / va;
}
