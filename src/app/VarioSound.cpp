#include "VarioSound.h"

#include <Arduino.h>

#include "Log.h"
#include "Settings.h"
#include "audio/Audio.h"
#include "audio/Speech.h"

namespace variosound {
namespace {

constexpr uint32_t kStartBeepMs = 250;
constexpr uint32_t kStartWaitMs = 1000;
constexpr float kStartBeepClimbMs = 2.0f;  // beep as if climbing at this rate

enum class Startup : uint8_t { Beep, Wait, Done };
Startup startup = Startup::Beep;
uint32_t startupMs = 0;

const char* lastMode = "";
float lastHz = 0;

// Debug log of what the tone generator was last told (mode change, or pitch moved 10 Hz), so the
// test runner can follow the vario sound from the log.
void logTone(const char* mode, float hz) {
#if PV_DEBUG
  if (mode == lastMode && fabsf(hz - lastHz) < 10.0f) return;
  lastMode = mode;
  lastHz = hz;
  LOGD("TONE", "%s %.0f Hz", mode, hz);
#endif
}

float climbHz(float varioMs) {
  return constrain(g_cfg.varioBaseHz + g_cfg.varioHzPerMs * varioMs, 200.0f, 2500.0f);
}

void updateStartup(uint32_t now) {
  if (startup == Startup::Beep) {
    if (!startupMs) {
      startupMs = now ? now : 1;
      const float hz = climbHz(kStartBeepClimbMs);
      audio::setTone(hz, 0, 0);  // continuous until stopped below
      logTone("startup", hz);
    } else if (now - startupMs >= kStartBeepMs) {
      audio::setTone(0, 0, 0);
      logTone("off", 0);
      startup = Startup::Wait;
      startupMs = now;
    }
  } else if (now - startupMs >= kStartWaitMs) {
    speech::say(audio::Priority::Normal, "ready");
    startup = Startup::Done;
  }
}

void updateTone(const NavState& n, bool muted) {
  const bool allowed = g_cfg.varioEnabled && !muted && n.baroOk && (!g_cfg.varioOnlyInFlight || n.flying);
  const float v = n.varioMs;
  if (allowed && g_cfg.varioClimbBeeps && v >= g_cfg.climbThreshold) {
    const float hz = climbHz(v);
    const uint16_t period = (uint16_t)constrain(500.0f - 70.0f * v, 150.0f, 500.0f);
    audio::setTone(hz, period / 2, period / 2);
    logTone("climb", hz);
  } else if (allowed && g_cfg.varioSinkAlarm && v <= g_cfg.sinkThreshold) {
    const float hz = constrain(g_cfg.varioBaseHz * 0.5f + 20.0f * v, 150.0f, 1000.0f);
    audio::setTone(hz, 0, 0);
    logTone("sink", hz);
  } else {
    audio::setTone(0, 0, 0);
    logTone("off", 0);
  }
}

}  // namespace

void update(uint32_t nowMs, const NavState& nav, bool muted) {
  if (startup != Startup::Done) updateStartup(nowMs);
  else updateTone(nav, muted);
}

void resetLog() {
  lastMode = "";
  lastHz = 0;
}

}  // namespace variosound
