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

#if PV_TEST
uint32_t gBeepStartMs = 0, gBeepEndMs = 0, gReadyMs = 0;
float gBeepHz = 0;
#endif

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

float climbHz(const Settings& cfg, float varioMs) {
  return constrain(cfg.varioBaseHz + cfg.varioHzPerMs * varioMs, 200.0f, 2500.0f);
}

void updateStartup(uint32_t now) {
  if (startup == Startup::Beep) {
    if (!startupMs) {
      startupMs = now ? now : 1;
      const float hz = climbHz(g_cfg, kStartBeepClimbMs);
      audio::setTone(hz, 0, 0);  // continuous until stopped below
#if PV_TEST
      gBeepStartMs = startupMs;
      gBeepHz = hz;
#endif
      logTone("startup", hz);
    } else if (now - startupMs >= kStartBeepMs) {
      audio::setTone(0, 0, 0);
      logTone("off", 0);
      startup = Startup::Wait;
      startupMs = now;
#if PV_TEST
      gBeepEndMs = now;
#endif
    }
  } else if (now - startupMs >= kStartWaitMs) {
    speech::say(audio::Priority::Normal, "ready");
    startup = Startup::Done;
#if PV_TEST
    gReadyMs = now;
#endif
  }
}

void updateTone(const NavState& n, bool muted) {
  const ToneCommand c = toneFor(n, muted, g_cfg);
  switch (c.mode) {
    case ToneCommand::Mode::Climb:
      audio::setTone(c.hz, c.onMs, c.offMs);
      logTone("climb", c.hz);
      break;
    case ToneCommand::Mode::Sink:
      audio::setTone(c.hz, 0, 0);
      logTone("sink", c.hz);
      break;
    default:
      audio::setTone(0, 0, 0);
      logTone("off", 0);
      break;
  }
}

}  // namespace

ToneCommand toneFor(const NavState& n, bool muted, const Settings& cfg) {
  const bool allowed = cfg.varioEnabled && !muted && n.baroOk && (!cfg.varioOnlyInFlight || n.flying);
  const float v = n.varioMs;
  ToneCommand c = {ToneCommand::Mode::Off, 0.0f, 0, 0};
  if (allowed && cfg.varioClimbBeeps && v >= cfg.climbThreshold) {
    c.mode = ToneCommand::Mode::Climb;
    c.hz = climbHz(cfg, v);
    const uint16_t period = (uint16_t)constrain(500.0f - 70.0f * v, 150.0f, 500.0f);
    c.onMs = c.offMs = period / 2;
  } else if (allowed && cfg.varioSinkAlarm && v <= cfg.sinkThreshold) {
    c.mode = ToneCommand::Mode::Sink;
    c.hz = constrain(cfg.varioBaseHz * 0.5f + 20.0f * v, 150.0f, 1000.0f);
  }
  return c;
}

void update(uint32_t nowMs, const NavState& nav, bool muted) {
  if (startup != Startup::Done) updateStartup(nowMs);
  else updateTone(nav, muted);
}

#if PV_TEST
BootInfo bootInfo() {
  BootInfo b = {};
  b.done = startup == Startup::Done;
  b.beepHz = gBeepHz;
  b.beepMs = gBeepEndMs - gBeepStartMs;
  b.readyGapMs = gReadyMs ? gReadyMs - gBeepEndMs : 0;
  return b;
}
#endif

void resetLog() {
  lastMode = "";
  lastHz = 0;
}

}  // namespace variosound
