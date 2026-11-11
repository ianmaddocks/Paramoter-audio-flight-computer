#include "Volume.h"

#include <Arduino.h>

#include "Hardware.h"
#include "Settings.h"
#include "audio/Audio.h"
#include "sensors/Mic.h"

namespace volume {
namespace {

float gSpeech = -1;  // applied volume in %, -1 until the first update
float gVario = -1;
uint32_t gLastMs = 0;

// Moves `now` towards `want` by at most `step`.
float approach(float now, float want, float step) {
  if (want > now) return fminf(want, now + step);
  return fmaxf(want, now - step);
}

}  // namespace

uint8_t wantedFor(uint8_t basePct) {
  if (!hw::hasMic() || !g_cfg.autoVolume || g_cfg.noiseLoudDb <= g_cfg.noiseQuietDb) return basePct;
  const float n = mic::noiseDb();
  if (isnan(n)) return basePct;  // nothing measured yet
  const float t = constrain((n - g_cfg.noiseQuietDb) / (g_cfg.noiseLoudDb - g_cfg.noiseQuietDb), 0.0f, 1.0f);
  return (uint8_t)lroundf(basePct + (100 - basePct) * t);
}

void update(uint32_t nowMs) {
  const float wantSpeech = wantedFor((uint8_t)g_cfg.speechVolume);
  const float wantVario = wantedFor((uint8_t)g_cfg.varioVolume);
  if (gSpeech < 0) {  // power-on: start at the wanted volume
    gSpeech = wantSpeech;
    gVario = wantVario;
  } else {
    const float dt = fminf((nowMs - gLastMs) / 1000.0f, 0.5f);
    const float step = (float)g_cfg.volumeRampPctS * dt;
    gSpeech = approach(gSpeech, wantSpeech, step);
    gVario = approach(gVario, wantVario, step);
  }
  gLastMs = nowMs;
  audio::setVolumes(speechPct(), varioPct());
}

uint8_t speechPct() { return (uint8_t)lroundf(fmaxf(gSpeech, 0)); }
uint8_t varioPct() { return (uint8_t)lroundf(fmaxf(gVario, 0)); }

}  // namespace volume
