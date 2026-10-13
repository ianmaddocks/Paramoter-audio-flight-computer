#include "Announcer.h"

#include <Arduino.h>

#include "Log.h"
#include "Settings.h"
#include "audio/Speech.h"

void Announcer::parseIfChanged() {
  if (strcmp(parsedFrom_, g_cfg.boundaries) == 0) return;
  strlcpy(parsedFrom_, g_cfg.boundaries, sizeof(parsedFrom_));
  count_ = 0;
  const char* p = g_cfg.boundaries;
  while (*p && count_ < kMaxBoundaries) {
    char* end;
    float f = strtof(p, &end);
    if (end == p) {
      ++p;
      continue;
    }
    list_[count_++] = f;
    p = end;
  }
}

// i-th boundary in ascending order; above the list, repeats every boundaryStepM.
float Announcer::boundary(int i) const {
  if (i < count_) return list_[i];
  if (g_cfg.boundaryStepM <= 0) return INFINITY;
  float last = count_ ? list_[count_ - 1] : 0.0f;
  return last + (i - count_ + 1) * g_cfg.boundaryStepM;
}

int Announcer::bandFor(float h) const {
  int b = 0;
  while (b < 500 && boundary(b) <= h) ++b;
  return b;
}

void Announcer::onTakeoff(const NavState& nav) {
  parseIfChanged();
  band_ = bandFor(nav.aglM);
  lastHeightMs_ = nav.ms;
  nextFlightTimeS_ = g_cfg.flightTimeIntervalMin * 60;
}

void Announcer::update(const NavState& nav, bool guidanceActive) {
  if (!nav.flying) return;
  parseIfChanged();
  const float h = nav.aglM;
  const float hyst = g_cfg.boundaryHystM;

  // A band changes only once the boundary is passed by the hysteresis margin.
  float crossed = NAN;
  bool climbing = false;
  while (boundary(band_) + hyst <= h) {
    crossed = boundary(band_++);
    climbing = true;
  }
  while (band_ > 0 && h <= boundary(band_ - 1) - hyst) {
    crossed = boundary(--band_);
    climbing = false;
  }
  if (!isnan(crossed)) {
    LOGD("ANN", "%s boundary %.1f m (agl %.1f m, band %d)", climbing ? "climbed through" : "descended through",
         crossed, h, band_);
  }
  if (!isnan(crossed) && g_cfg.boundaryEnabled &&
      (climbing ? g_cfg.boundaryClimbing : g_cfg.boundaryDescending)) {
    audio::Utterance u(audio::Priority::Normal);
    speech::addHeight(u, crossed);
    audio::say(u);
    lastHeightMs_ = nav.ms;
  }

  if (g_cfg.heightIntervalS > 0 && nav.ms - lastHeightMs_ >= (uint32_t)g_cfg.heightIntervalS * 1000) {
    lastHeightMs_ = nav.ms;
    audio::Utterance u(audio::Priority::Low);
    speech::addHeight(u, h);
    audio::say(u);
  }

  if (g_cfg.flightTimeIntervalMin > 0 && nav.flightTimeS >= nextFlightTimeS_) {
    bool due = nextFlightTimeS_ > 0;
    nextFlightTimeS_ = (nav.flightTimeS / 60 / g_cfg.flightTimeIntervalMin + 1) *
                       g_cfg.flightTimeIntervalMin * 60;
    if (due && !guidanceActive) {
      audio::Utterance u(audio::Priority::Low);
      u.add("flight_time");
      speech::addDuration(u, nav.flightTimeS);
      audio::say(u);
    }
  }
}
