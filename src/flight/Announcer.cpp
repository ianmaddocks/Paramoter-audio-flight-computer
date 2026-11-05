#include "Announcer.h"

#include <Arduino.h>

#include "Log.h"
#include "Settings.h"
#include "audio/Speech.h"

void Announcer::Boundaries::parseIfChanged(const char* src) {
  if (strcmp(parsedFrom, src) == 0) return;
  strlcpy(parsedFrom, src, sizeof(parsedFrom));
  count = 0;
  const char* p = src;
  while (*p && count < kMaxBoundaries) {
    char* end;
    float f = strtof(p, &end);
    if (end == p) {
      ++p;
      continue;
    }
    list[count++] = f;
    p = end;
  }
}

float Announcer::Boundaries::at(int i) const {
  if (i < count) return list[i];
  if (g_cfg.boundaryStepM <= 0) return INFINITY;
  float last = count ? list[count - 1] : 0.0f;
  return last + (i - count + 1) * g_cfg.boundaryStepM;
}

int Announcer::Boundaries::bandFor(float h) const {
  int b = 0;
  while (b < 500 && at(b) <= h) ++b;
  return b;
}

void Announcer::onTakeoff(const NavState& nav) {
  up_.parseIfChanged(g_cfg.boundariesUp);
  down_.parseIfChanged(g_cfg.boundaries);
  up_.band = up_.bandFor(nav.aglM);
  down_.band = down_.bandFor(nav.aglM);
  lastHeightMs_ = nav.ms;
  nextFlightTimeS_ = g_cfg.flightTimeIntervalMin * 60;
}

void Announcer::update(const NavState& nav, bool guidanceActive) {
  if (!nav.flying) return;
  up_.parseIfChanged(g_cfg.boundariesUp);
  down_.parseIfChanged(g_cfg.boundaries);
  const float h = nav.aglM;
  const float hyst = g_cfg.boundaryHystM;

  // Each list tracks its own band; a band changes only once a boundary is passed by the
  // hysteresis margin. The climbing list speaks on the way up and moves back silently on the
  // way down (so it can speak again on the next climb); the descending list is the mirror image.
  float crossedUp = NAN, crossedDown = NAN;
  while (up_.at(up_.band) + hyst <= h) crossedUp = up_.at(up_.band++);
  while (up_.band > 0 && h <= up_.at(up_.band - 1) - hyst) --up_.band;
  while (down_.at(down_.band) + hyst <= h) ++down_.band;
  while (down_.band > 0 && h <= down_.at(down_.band - 1) - hyst) crossedDown = down_.at(--down_.band);

  if (!isnan(crossedUp)) LOGD("ANN", "climbed through boundary %.1f m (agl %.1f m, band %d)", crossedUp, h, up_.band);
  if (!isnan(crossedDown)) LOGD("ANN", "descended through boundary %.1f m (agl %.1f m, band %d)", crossedDown, h, down_.band);

  float crossed = NAN;
  if (!isnan(crossedUp) && g_cfg.boundaryClimbing) crossed = crossedUp;
  else if (!isnan(crossedDown) && g_cfg.boundaryDescending) crossed = crossedDown;
  if (!isnan(crossed)) {
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
