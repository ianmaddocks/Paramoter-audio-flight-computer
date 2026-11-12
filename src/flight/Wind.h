#pragma once
#include "flight/FlightStore.h"
#include "nav/Nav.h"

enum class WindSource : uint8_t { None, Takeoff, Circling, Manual };

// Wind estimate: takeoff run direction first (assumed into wind), refined by fitting a circle
// to GPS ground-velocity vectors while turning. Manual wind from settings overrides both.
class Wind {
 public:
  void onTakeoff(bool courseValid, float courseDeg);
  void update(const NavState& nav);
  void exportState(FlightRecord& rec) const;
  void importState(const FlightRecord& rec);

  bool valid() const;
  float fromDeg() const;
  float speedMs() const;
  float airspeedMs() const;
  WindSource source() const;
  uint32_t fitCount() const { return fits_; }  // circling fits accepted so far (for the tests)
  const char* sourceName() const;

  // Ground glide ratio flying engine-off along the given track, allowing for wind.
  float groundGlide(float trackDeg) const;

 private:
  struct Sample {
    uint32_t ms;
    float e, n;
  };
  static constexpr int kMaxSamples = 120;

  void tryFit(uint32_t now);
  static bool solveCircle(const Sample* const* s, const bool* use, int m, float* cx, float* cy, float* r, int* nUsed,
                          int* binsFilled);

  Sample samples_[kMaxSamples];
  int sampleCount_ = 0;
  int sampleHead_ = 0;
  uint32_t lastSampleMs_ = 0;
  uint32_t lastFitMs_ = 0;

  bool pendValid_ = false;  // the last accepted fit (wind vector, where the air is going)
  float pendE_ = 0, pendN_ = 0;
  uint32_t pendMs_ = 0;
  uint32_t fits_ = 0;

  WindSource src_ = WindSource::None;
  float fromDeg_ = 0;
  float speedMs_ = 0;
  float airspeedMs_ = 0;
};
