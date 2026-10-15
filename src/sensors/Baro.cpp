#include "Baro.h"
#include "Log.h"

#include "Pins.h"
#include "Settings.h"

namespace {
constexpr float kMeasVar = 0.25f * 0.25f;
// The DPS310's first readings after start-up jump while its filtering settles: skip ~1 s.
constexpr uint16_t kWarmupSamples = 32;
// Settled once the vario has stayed within this for varioSettleS, or after 3x that regardless
// (e.g. restarted in flight, where it may never be still).
constexpr float kSettleQuietMs = 0.3f;
constexpr uint32_t kSettleMaxFactor = 3;

float pressureToAltStd(float pa) { return 44330.8f * (1.0f - powf(pa / 101325.0f, 0.190263f)); }
}  // namespace

void AltKalman::update(float z, float dt, float q, float r) {
  if (!init) {
    h = z;
    v = 0;
    init = true;
    return;
  }
  h += v * dt;
  float dt2 = dt * dt, dt3 = dt2 * dt, dt4 = dt3 * dt;
  p00 += dt * (p01 + p10) + dt2 * p11 + q * dt4 / 4.0f;
  p01 += dt * p11 + q * dt3 / 2.0f;
  p10 = p01;
  p11 += q * dt2;

  float s = p00 + r;
  float k0 = p00 / s, k1 = p10 / s;
  float y = z - h;
  h += k0 * y;
  v += k1 * y;
  float a00 = p00, a01 = p01;
  p00 -= k0 * a00;
  p01 -= k0 * a01;
  p10 -= k1 * a00;
  p11 -= k1 * a01;
}

bool Baro::begin(TwoWire& wire) {
  wire_ = &wire;
  present_ = configure();
  if (!present_) LOGE("BARO", "DPS310 not found at 0x%02X", DPS310_ADDR);
  return present_;
}

bool Baro::configure() {
  if (!dps_.begin_I2C(DPS310_ADDR, wire_)) return false;
  dps_.configurePressure(DPS310_32HZ, DPS310_8SAMPLES);
  dps_.configureTemperature(DPS310_2HZ, DPS310_2SAMPLES);
  dps_.setMode(DPS310_CONT_PRESTEMP);
  lastSampleUs_ = 0;
  kf_ = AltKalman();
  warmupLeft_ = kWarmupSamples;
  settled_ = false;
  settleStartMs_ = 0;
  return true;
}

void Baro::update(uint32_t nowMs) {
  if (!present_) {
    if (nowMs - lastRetryMs_ > 5000) {
      lastRetryMs_ = nowMs;
      present_ = configure();
    }
    ok_ = false;
    return;
  }
  if (!dps_.pressureAvailable()) {
    if (ok_ && nowMs - lastGoodMs_ > 2000) {
      ok_ = false;
      present_ = false;
      LOGW("BARO", "DPS310 stopped responding");
    }
    return;
  }
  sensors_event_t t, p;
  if (!dps_.getEvents(&t, &p)) return;
  float pa = p.pressure * 100.0f;
  if (pa < 20000.0f || pa > 110000.0f) return;

  uint32_t us = micros();
  float dt = lastSampleUs_ ? (us - lastSampleUs_) * 1e-6f : 0.03f;
  lastSampleUs_ = us;
  if (dt <= 0.0f || dt > 1.0f) dt = 0.03f;

  pressurePa_ = pa;
  tempC_ = t.temperature;
  lastGoodMs_ = nowMs;
  if (warmupLeft_) {
    --warmupLeft_;
    return;
  }
  kf_.update(pressureToAltStd(pa), dt, g_cfg.varioAccelNoise, kMeasVar);
  ok_ = true;
  updateSettled(nowMs);
}

void Baro::updateSettled(uint32_t nowMs) {
  if (settled_) return;
  if (!settleStartMs_) settleStartMs_ = quietSinceMs_ = nowMs;
  if (fabsf(kf_.v) > kSettleQuietMs) quietSinceMs_ = nowMs;
  uint32_t needMs = (uint32_t)g_cfg.varioSettleS * 1000;
  bool timedOut = nowMs - settleStartMs_ >= needMs * kSettleMaxFactor;
  if (nowMs - quietSinceMs_ < needMs && !timedOut) return;
  settled_ = true;
  LOGI("BARO", "Vario settled after %.1f s%s", (nowMs - settleStartMs_) / 1000.0f,
       timedOut ? " (timed out)" : "");
}
