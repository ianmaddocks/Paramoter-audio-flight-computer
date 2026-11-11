#pragma once
#include <stdint.h>

// Learns how long the battery lasts from how it has actually discharged, so the vario can say
// how many minutes are left and have its percentage follow the real curve.
//
// The battery voltage is divided into "edges" every 50 mV (4.10 V down to 3.30 V). While the
// vario runs on the battery it notes how many powered-on seconds pass as each edge is crossed
// going down. When a discharge ends in the vario dying (the next start-up finds the battery
// recharged, and the last voltage seen was low) every crossing becomes a measurement: "from this
// voltage the battery lasted N more minutes". Measurements are blended into a table stored in
// NVS (survives firmware and filesystem updates). Until a full discharge has been seen, a typical
// curve scaled by the "runtime when full" setting is used.
//
// Flash writes are kept low: the in-progress discharge is saved on each edge crossing and every
// five minutes, the learned table only when a discharge finishes.
class BatteryLearner {
 public:
  static constexpr int kEdges = 17;

  // persist = false keeps everything in RAM (used by the test build's self-test).
  void begin(bool persist = true);
  void update(float volts, uint32_t nowMs);  // call about once a second

  // True once at least one measured discharge has been blended into the table.
  bool learned() const { return learnedMask_ != 0; }
  // Minutes of battery left at this voltage by the table alone.
  float minutesAt(float volts) const;
  // Minutes left now: the table, scaled by how fast this discharge is going. -1 = unknown
  // (charging, or not on battery).
  int minutesLeft(float volts) const;
#if PV_TEST
  void rebootForTest();  // forget run-time state as a power cycle would, keeping the saved discharge
#endif

 private:
  static float edgeV(int i) { return 4.10f - 0.05f * i; }
  void fillDefaults();
  void enforceMonotonic();
  void loadTable();
  void saveTable();
  void loadSession();
  void saveSession();
  void resetSession();
  void finishSession(bool died);
  int lowestEdge() const;   // deepest edge crossed this session, -1 = none
  int firstEdge() const;    // first edge crossed this session, -1 = none

  float table_[kEdges] = {};
  uint32_t learnedMask_ = 0;  // bit i set: table_[i] holds a measurement

  uint32_t cross_[kEdges];     // powered-on seconds at which edge i was crossed (kNone = not yet)
  uint32_t sessionS_ = 0;      // powered-on seconds in this discharge
  uint32_t lastMv_ = 0;        // last battery voltage seen, mV
  uint32_t lastSaveS_ = 0;

  bool persist_ = true;
  bool checked_ = false;       // start-up check of the saved discharge done
  bool charging_ = false;
  uint32_t lastMs_ = 0;
  float prevV_ = 0;
  float refV_ = 0;
  uint32_t refMs_ = 0;
  uint32_t chargingUntilMs_ = 0;
};

#if PV_TEST
// Runs two simulated discharges through a RAM-only learner and logs the results (BATL selftest).
void batteryLearnSelfTest();
#endif
