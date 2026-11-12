#include "Wind.h"
#include "Log.h"

#include <Arduino.h>
#include <string.h>

#include "Settings.h"
#include "nav/Geo.h"

namespace {
constexpr uint32_t kSampleIntervalMs = 500;
constexpr uint32_t kWindowMs = 60000;
constexpr uint32_t kFitIntervalMs = 5000;
constexpr int kMinBins = 9;  // of 12 x 30 degree track bins, i.e. >= 270 degrees of turn
constexpr float kMinSampleSpeed = 3.0f;
constexpr float kOutlierMinMs = 1.2f;    // samples this far (or 2 x rms) off the circle are not part of it
constexpr float kFitAgreeMs = 1.5f;      // two fits this close are the same wind
constexpr uint32_t kFitMemoryMs = 150000;
constexpr int kHeadStep = 4;             // samples (2 s) between the candidate starts of the fit window
}  // namespace

void Wind::onTakeoff(bool courseValid, float courseDeg) {
  sampleCount_ = sampleHead_ = 0;
  pendValid_ = false;
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
//
// Two protections against out-of-date data (F2):
//   * Samples from before a wind change (the straight flight before the circle) are the oldest in the window and
//     lie off the circle: the fit is also tried without the oldest seconds and the clearly better one is used. Any
//     other sample further from the circle than max(1.2 m/s, 2 x rms) is dropped and the circle fitted again; if
//     more than a quarter are off the data is not one circle (the wind changed during it) and there is no fit.
//   * The previous estimate is averaged in (50/50) so one odd fit cannot swing it, but when the new fit and the
//     one before it agree within kFitAgreeMs they are two independent measurements of the same wind, and the
//     estimate is their mean: after a real change of wind it is followed from the second fit on, not by halves.
bool Wind::solveCircle(const Sample* const* s, const bool* use, int m, float* cx, float* cy, float* r, int* nUsed,
                       int* binsFilled) {
  bool bins[12] = {};
  double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0, sxz = 0, syz = 0, sz = 0;
  int n = 0;
  for (int i = 0; i < m; ++i) {
    if (!use[i]) continue;
    const Sample& p = *s[i];
    bins[(int)(geo::vecBearing(p.e, p.n) / 30.0f) % 12] = true;
    double z = p.e * p.e + p.n * p.n;
    sx += p.e;
    sy += p.n;
    sxx += p.e * p.e;
    syy += p.n * p.n;
    sxy += p.e * p.n;
    sxz += p.e * z;
    syz += p.n * z;
    sz += z;
    ++n;
  }
  int filled = 0;
  for (bool b : bins) filled += b;
  *nUsed = n;
  *binsFilled = filled;
  if (n < 20 || filled < kMinBins) return false;

  // Solve [sxx sxy sx; sxy syy sy; sx sy n] [D E F]' = -[sxz syz sz]'
  double a[3][4] = {{sxx, sxy, sx, -sxz}, {sxy, syy, sy, -syz}, {sx, sy, (double)n, -sz}};
  for (int c = 0; c < 3; ++c) {
    int piv = c;
    for (int q = c + 1; q < 3; ++q)
      if (fabs(a[q][c]) > fabs(a[piv][c])) piv = q;
    if (fabs(a[piv][c]) < 1e-9) return false;
    if (piv != c)
      for (int k = 0; k < 4; ++k) std::swap(a[c][k], a[piv][k]);
    for (int q = 0; q < 3; ++q) {
      if (q == c) continue;
      double f = a[q][c] / a[c][c];
      for (int k = c; k < 4; ++k) a[q][k] -= f * a[c][k];
    }
  }
  double D = a[0][3] / a[0][0], E = a[1][3] / a[1][1], F = a[2][3] / a[2][2];
  *cx = -D / 2;
  *cy = -E / 2;
  float r2 = *cx * *cx + *cy * *cy - F;
  if (r2 <= 0) return false;
  *r = sqrtf(r2);
  return true;
}

void Wind::tryFit(uint32_t now) {
  const Sample* sm[kMaxSamples];  // oldest first
  bool use[kMaxSamples];
  int m = 0;
  for (int i = 0; i < sampleCount_; ++i) {
    const Sample& s = samples_[(sampleHead_ + i) % kMaxSamples];
    if (now - s.ms > kWindowMs) continue;
    sm[m++] = &s;
  }
  auto rmsOf = [&](float cx, float cy, float r, int count) {
    double err = 0;
    for (int i = 0; i < m; ++i) {
      if (!use[i]) continue;
      float d = hypotf(sm[i]->e - cx, sm[i]->n - cy) - r;
      err += d * d;
    }
    return sqrtf(err / count);
  };

  // 1. Stale head: samples from before a wind change (straight flight before the circle) are the OLDEST in the
  //    window and sit off the circle. Try the fit without the oldest 0, 2, 4 ... seconds and take the best one,
  //    but only if it is clearly better (rms under 70 % of the full fit's): data that is all one circle is
  //    never thinned. A candidate must itself have 20 samples and 9 direction bins.
  float cx = 0, cy = 0, r = 0, rms = 0;
  int n = 0, filled = 0, start = 0;
  bool have = false;
  for (int skip = 0; m - skip >= 20; skip += kHeadStep) {
    for (int i = 0; i < m; ++i) use[i] = i >= skip;
    float x, y, rr;
    int nn, ff;
    if (!solveCircle(sm, use, m, &x, &y, &rr, &nn, &ff)) break;  // too little of the circle left: stop looking
    float e = rmsOf(x, y, rr, nn);
    if (!have || e < 0.7f * rms) {
      have = true;
      cx = x; cy = y; r = rr; n = nn; filled = ff; rms = e; start = skip;
    }
  }
  if (!have) return;
  for (int i = 0; i < m; ++i) use[i] = i >= start;

  // 2. Stray samples anywhere else: drop those further from the circle than max(1.2 m/s, 2 x rms) and fit again,
  //    tightening as the rms falls. If more than a quarter are off the first time the data is not one circle (the
  //    wind changed during it): no fit.
  int off = 0;
  for (int pass = 0; pass < 4; ++pass) {
    const float cut = fmaxf(kOutlierMinMs, 2.0f * rms);
    int now_off = 0;
    bool keep[kMaxSamples];
    for (int i = 0; i < m; ++i) {
      keep[i] = use[i] && fabsf(hypotf(sm[i]->e - cx, sm[i]->n - cy) - r) <= cut;
      now_off += use[i] && !keep[i];
    }
    if (now_off == 0) break;
    if (pass == 0 && now_off * 4 > n) {
      LOGD("WIND", "fit rejected: %d of %d samples are off the circle (wind changed during it?)", now_off, n);
      return;
    }
    bool saved[kMaxSamples];
    memcpy(saved, use, sizeof(use));
    memcpy(use, keep, sizeof(keep));
    if (!solveCircle(sm, use, m, &cx, &cy, &r, &n, &filled)) {
      LOGD("WIND", "fit rejected: too little of the circle left after dropping %d stray samples", off + now_off);
      memcpy(use, saved, sizeof(use));
      return;
    }
    off += now_off;
    rms = rmsOf(cx, cy, r, n);
  }
  off += start;  // (the stale head counts as dropped too, for the log line)
  float wspd = hypotf(cx, cy);
  if (rms > 2.0f || r < 5.0f || r > 25.0f || wspd > 15.0f) {
    LOGD("WIND", "fit rejected: n=%d bins=%d airspeed %.1f wind %.1f rms %.2f", n, filled, r, wspd, rms);
    return;
  }

  // Wind vector (cx, cy) points where the air is going; "from" is the opposite bearing.
  float from = geo::vecBearing(-cx, -cy);
  const bool agreesWithLast = pendValid_ && now - pendMs_ <= kFitMemoryMs && hypotf(pendE_ - cx, pendN_ - cy) <= kFitAgreeMs;
  if (src_ == WindSource::Circling) {
    float pe, pn;  // the estimate to average with: the previous fit if this one agrees with it, else the old estimate
    if (agreesWithLast) {
      pe = -pendE_;
      pn = -pendN_;
    } else {
      pe = speedMs_ * sinf(fromDeg_ * geo::kDegToRadF);
      pn = speedMs_ * cosf(fromDeg_ * geo::kDegToRadF);
    }
    float ne = (pe - cx) / 2, nn = (pn - cy) / 2;
    fromDeg_ = geo::vecBearing(ne, nn);
    speedMs_ = hypotf(ne, nn);
    airspeedMs_ = (airspeedMs_ + r) / 2;
  } else {
    fromDeg_ = from;
    speedMs_ = wspd;
    airspeedMs_ = r;
  }
  pendValid_ = true;
  pendE_ = cx;
  pendN_ = cy;
  pendMs_ = now;
  ++fits_;
  src_ = WindSource::Circling;
  sampleCount_ = sampleHead_ = 0;
  LOGI("WIND", "Wind fit: from %.0f deg %.1f m/s, airspeed %.1f m/s (rms %.2f%s%s)", fromDeg_, speedMs_,
        airspeedMs_, rms, off ? ", stray samples dropped" : "", agreesWithLast ? ", agrees with the last fit" : "");
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
