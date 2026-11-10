#include "BatteryLearn.h"

#include <Arduino.h>
#include <Preferences.h>

#include "Log.h"
#include "Settings.h"
#include "sensors/Battery.h"

namespace {
constexpr uint32_t kNone = 0xFFFFFFFFu;
constexpr uint32_t kTableMagic = 0x42544C31;    // "BTL1"
constexpr uint32_t kSessionMagic = 0x42535331;  // "BSS1"
constexpr uint32_t kSaveEveryS = 300;
constexpr float kRechargedRiseV = 0.06f;   // voltage this far above the deepest edge = recharged
constexpr uint32_t kDiedBelowMv = 3600;    // last voltage seen at or below this = battery ran down
constexpr uint32_t kDeathLagS = 150;       // the last save is on average this long before the end
constexpr float kBlend = 0.3f;             // weight of a new measurement

struct TableBlob {
  uint32_t magic;
  uint32_t mask;
  float minutes[BatteryLearner::kEdges];
};
struct SessionBlob {
  uint32_t magic;
  uint32_t sessionS;
  uint32_t lastMv;
  uint32_t cross[BatteryLearner::kEdges];
};
}  // namespace

void BatteryLearner::begin() {
  for (auto& c : cross_) c = kNone;
  fillDefaults();
  loadTable();
  loadSession();
}

// Typical LiPo curve scaled to the configured full-battery runtime: used for edges with no
// measurement yet.
void BatteryLearner::fillDefaults() {
  for (int i = 0; i < kEdges; ++i) {
    if (learnedMask_ & (1u << i)) continue;
    table_[i] = batteryCurvePct(edgeV(i)) / 100.0f * (float)g_cfg.battRuntimeMin;
  }
  enforceMonotonic();
}

void BatteryLearner::enforceMonotonic() {
  for (int i = 1; i < kEdges; ++i) table_[i] = min(table_[i], table_[i - 1]);
}

void BatteryLearner::loadTable() {
  Preferences p;
  if (!p.begin("battlrn", true)) return;
  TableBlob b;
  if (p.getBytesLength("tbl") == sizeof(b) && p.getBytes("tbl", &b, sizeof(b)) == sizeof(b) &&
      b.magic == kTableMagic) {
    learnedMask_ = b.mask;
    for (int i = 0; i < kEdges; ++i) {
      if (learnedMask_ & (1u << i)) table_[i] = b.minutes[i];
    }
    fillDefaults();
    LOGD("BATL", "loaded learned table, %d edges measured", __builtin_popcount(learnedMask_));
  }
  p.end();
}

void BatteryLearner::saveTable() {
  Preferences p;
  if (!p.begin("battlrn", false)) return;
  TableBlob b;
  b.magic = kTableMagic;
  b.mask = learnedMask_;
  for (int i = 0; i < kEdges; ++i) b.minutes[i] = table_[i];
  p.putBytes("tbl", &b, sizeof(b));
  p.end();
}

void BatteryLearner::loadSession() {
  Preferences p;
  if (!p.begin("battlrn", true)) return;
  SessionBlob b;
  if (p.getBytesLength("ses") == sizeof(b) && p.getBytes("ses", &b, sizeof(b)) == sizeof(b) &&
      b.magic == kSessionMagic) {
    sessionS_ = b.sessionS;
    lastMv_ = b.lastMv;
    for (int i = 0; i < kEdges; ++i) cross_[i] = b.cross[i];
    lastSaveS_ = sessionS_;
  }
  p.end();
}

void BatteryLearner::saveSession() {
  Preferences p;
  if (!p.begin("battlrn", false)) return;
  SessionBlob b;
  b.magic = kSessionMagic;
  b.sessionS = sessionS_;
  b.lastMv = lastMv_;
  for (int i = 0; i < kEdges; ++i) b.cross[i] = cross_[i];
  p.putBytes("ses", &b, sizeof(b));
  p.end();
  lastSaveS_ = sessionS_;
}

void BatteryLearner::resetSession() {
  for (auto& c : cross_) c = kNone;
  sessionS_ = 0;
  saveSession();
}

int BatteryLearner::lowestEdge() const {
  for (int i = kEdges - 1; i >= 0; --i) {
    if (cross_[i] != kNone) return i;
  }
  return -1;
}

int BatteryLearner::firstEdge() const {
  for (int i = 0; i < kEdges; ++i) {
    if (cross_[i] != kNone) return i;
  }
  return -1;
}

// The previous discharge is over (battery recharged). If it ran the battery down, every edge it
// crossed tells how long the battery lasted from that voltage.
void BatteryLearner::finishSession(bool died) {
  if (!died) return;
  uint32_t endS = sessionS_ + kDeathLagS;
  int used = 0;
  for (int i = 0; i < kEdges; ++i) {
    if (cross_[i] == kNone || cross_[i] > endS) continue;
    float minutes = (endS - cross_[i]) / 60.0f;
    if (learnedMask_ & (1u << i)) table_[i] += (minutes - table_[i]) * kBlend;
    else table_[i] = minutes;
    learnedMask_ |= 1u << i;
    ++used;
  }
  if (!used) return;
  enforceMonotonic();
  saveTable();
  LOGI("BATL", "discharge learned: %d edges, ran %lu min, from 4.10 V table now %.0f min", used,
       (unsigned long)(endS / 60), table_[0]);
}

void BatteryLearner::update(float volts, uint32_t nowMs) {
  if (volts < 2.5f) return;  // no battery (USB powered)
  if (!lastMs_) {
    lastMs_ = nowMs;
    prevV_ = refV_ = volts;
    refMs_ = nowMs;
    return;
  }
  uint32_t dt = (nowMs - lastMs_ + 500) / 1000;
  lastMs_ = nowMs;
  if (dt > 5) dt = 5;

  if (!checked_) {
    checked_ = true;
    int lo = lowestEdge();
    if (lo >= 0 && volts > edgeV(lo) + kRechargedRiseV) {
      LOGI("BATL", "battery recharged since last discharge (%.2f V)", volts);
      finishSession(lastMv_ > 0 && lastMv_ <= kDiedBelowMv);
      resetSession();
    }
  }

  sessionS_ += dt;
  lastMv_ = (uint32_t)lroundf(volts * 1000.0f);

  // A voltage rising over a minute means it is being charged: nothing learned meanwhile.
  if (nowMs - refMs_ >= 60000) {
    if (volts - refV_ >= 0.005f) chargingUntilMs_ = nowMs + 180000;
    refV_ = volts;
    refMs_ = nowMs;
  }
  charging_ = (int32_t)(chargingUntilMs_ - nowMs) > 0;
  if (charging_) {
    if (lowestEdge() >= 0) resetSession();
    prevV_ = volts;
    return;
  }

  bool changed = false;
  for (int i = 0; i < kEdges; ++i) {
    float thr = edgeV(i) - 0.005f;
    if (cross_[i] == kNone && prevV_ > thr && volts <= thr) {
      cross_[i] = sessionS_;
      changed = true;
      LOGD("BATL", "crossed %.2f V after %lu s", edgeV(i), (unsigned long)sessionS_);
    }
  }
  prevV_ = volts;
  if (changed || sessionS_ - lastSaveS_ >= kSaveEveryS) saveSession();
}

float BatteryLearner::minutesAt(float volts) const {
  if (volts >= edgeV(0)) return table_[0];
  for (int i = 1; i < kEdges; ++i) {
    if (volts >= edgeV(i)) {
      float t = (volts - edgeV(i)) / (edgeV(i - 1) - edgeV(i));
      return table_[i] + t * (table_[i - 1] - table_[i]);
    }
  }
  return table_[kEdges - 1];
}

int BatteryLearner::minutesLeft(float volts) const {
  if (volts < 2.5f || charging_ || !checked_) return -1;
  float base = minutesAt(volts);
  // This discharge's own pace (load, temperature, cell age) against what the table predicts for
  // the same voltage span.
  int a = firstEdge(), b = lowestEdge();
  float scale = 1.0f;
  if (a >= 0 && b > a) {
    float predicted = table_[a] - table_[b];
    float measured = (cross_[b] - cross_[a]) / 60.0f;
    if (predicted >= 8.0f && measured >= 5.0f) scale = constrain(measured / predicted, 0.5f, 2.0f);
  }
  return (int)lroundf(base * scale);
}
