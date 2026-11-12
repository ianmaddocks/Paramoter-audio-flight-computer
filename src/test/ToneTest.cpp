#include "test/ToneTest.h"

#if PV_TEST

#include <math.h>

#include "Log.h"
#include "Settings.h"
#include "app/VarioSound.h"
#include "nav/Nav.h"

void varioToneSelfTest() {
  using variosound::ToneCommand;
  using Mode = ToneCommand::Mode;
  int pass = 0, fail = 0;

  Settings s;  // a clean set of settings, whatever the vario has stored
  s.varioEnabled = true;
  s.varioOnlyInFlight = false;
  s.varioClimbBeeps = true;
  s.varioSinkAlarm = true;
  s.climbThreshold = 0.3f;
  s.sinkThreshold = -2.0f;
  s.varioBaseHz = 700.0f;
  s.varioHzPerMs = 100.0f;
  NavState n;
  n.baroOk = true;
  n.flying = false;
  bool muted = false;

  auto name = [](Mode m) { return m == Mode::Climb ? "climb" : m == Mode::Sink ? "sink" : "off"; };
  // wantHz < 0: pitch not checked; wantOn < 0: beep timing not checked (on and off are equal)
  auto check = [&](const char* what, float v, Mode want, float wantHz = -1.0f, int wantOn = -1) {
    n.varioMs = v;
    const ToneCommand c = variosound::toneFor(n, muted, s);
    bool ok = c.mode == want;
    if (ok && wantHz >= 0.0f && fabsf(c.hz - wantHz) > 0.01f) ok = false;
    if (ok && wantOn >= 0 && (c.onMs != wantOn || c.offMs != wantOn)) ok = false;
    if (ok && want == Mode::Off && (c.hz != 0.0f || c.onMs || c.offMs)) ok = false;
    if (ok && want == Mode::Sink && (c.onMs || c.offMs)) ok = false;  // the sink tone is continuous
    if (ok) {
      ++pass;
    } else {
      ++fail;
      LOGE("TONET", "FAIL %s: vario %.7f gave %s %.2f Hz on/off %u/%u, wanted %s %.2f Hz on %d", what, v, name(c.mode),
           c.hz, (unsigned)c.onMs, (unsigned)c.offMs, name(want), wantHz, wantOn);
    }
  };

  const float up = INFINITY, down = -INFINITY;

  // ---- climb threshold: the threshold itself counts (>=)
  check("climb: at the threshold", 0.3f, Mode::Climb);
  check("climb: just below the threshold", nextafterf(0.3f, down), Mode::Off);
  check("climb: just above the threshold", nextafterf(0.3f, up), Mode::Climb);
  check("climb: 0.25 is below", 0.25f, Mode::Off);
  check("climb: 0.35 is above", 0.35f, Mode::Climb, 735.0f, 237);  // 700 + 100 x 0.35; period 475 -> 237 ms
  check("climb: level flight", 0.0f, Mode::Off);
  check("climb: gentle sink", -1.0f, Mode::Off);

  // ---- sink threshold: the threshold itself counts (<=)
  check("sink: at the threshold", -2.0f, Mode::Sink);
  check("sink: just above the threshold", nextafterf(-2.0f, up), Mode::Off);
  check("sink: just below the threshold", nextafterf(-2.0f, down), Mode::Sink);
  check("sink: -1.8 is above", -1.8f, Mode::Off);
  check("sink: -2.2 is below", -2.2f, Mode::Sink, 306.0f);  // 350 + 20 x -2.2

  // ---- pitch and beep rate formulas, and their limits
  check("climb pitch at 1 m/s", 1.0f, Mode::Climb, 800.0f, 215);        // period 430 -> 215 ms
  check("climb pitch at 2 m/s", 2.0f, Mode::Climb, 900.0f, 180);        // period 360 -> 180 ms
  check("climb: fastest beeps (period limit 150 ms)", 6.0f, Mode::Climb, 1300.0f, 75);
  check("climb: beep rate limit holds above it", 30.0f, Mode::Climb, 2500.0f, 75);  // pitch limit 2500 Hz too
  check("sink pitch at 3 m/s", -3.0f, Mode::Sink, 290.0f);
  check("sink: pitch limit 150 Hz is reached at -10", -10.0f, Mode::Sink, 150.0f);
  check("sink: pitch limit holds below it", -50.0f, Mode::Sink, 150.0f);
  s.varioBaseHz = 3000.0f;
  check("climb: pitch limit 2500 Hz from a high base", 0.35f, Mode::Climb, 2500.0f);
  check("sink: pitch limit 1000 Hz from a high base", -2.2f, Mode::Sink, 1000.0f);
  s.varioBaseHz = 100.0f;
  check("climb: pitch limit 200 Hz from a low base", 0.35f, Mode::Climb, 200.0f);
  s.varioBaseHz = 700.0f;
  s.varioHzPerMs = 50.0f;
  check("climb: pitch follows the rise per m/s", 1.0f, Mode::Climb, 750.0f);
  s.varioHzPerMs = 100.0f;

  // ---- other threshold values: the same comparisons wherever the thresholds are set
  s.climbThreshold = 1.0f;
  check("climb threshold 1.0: just below", 0.99f, Mode::Off);
  check("climb threshold 1.0: at", 1.0f, Mode::Climb);
  s.climbThreshold = 0.0f;
  check("climb threshold 0: at zero", 0.0f, Mode::Climb);
  check("climb threshold 0: just below zero", nextafterf(0.0f, down), Mode::Off);
  s.climbThreshold = 0.3f;
  s.sinkThreshold = -0.5f;
  check("sink threshold -0.5: just above", -0.4f, Mode::Off);
  check("sink threshold -0.5: at", -0.5f, Mode::Sink);
  s.sinkThreshold = -2.0f;

  // ---- everything that silences the tone
  muted = true;
  check("muted: climb", 1.0f, Mode::Off);
  check("muted: sink", -3.0f, Mode::Off);
  muted = false;
  s.varioEnabled = false;
  check("vario disabled: climb", 1.0f, Mode::Off);
  check("vario disabled: sink", -3.0f, Mode::Off);
  s.varioEnabled = true;
  n.baroOk = false;
  check("no barometer: climb", 1.0f, Mode::Off);
  check("no barometer: sink", -3.0f, Mode::Off);
  n.baroOk = true;
  s.varioOnlyInFlight = true;
  check("only when flying, on the ground: climb", 1.0f, Mode::Off);
  check("only when flying, on the ground: sink", -3.0f, Mode::Off);
  n.flying = true;
  check("only when flying, flying: climb", 1.0f, Mode::Climb);
  check("only when flying, flying: sink", -3.0f, Mode::Sink);
  n.flying = false;
  s.varioOnlyInFlight = false;
  s.varioClimbBeeps = false;
  check("climb beeps off: climb", 1.0f, Mode::Off);
  check("climb beeps off: the sink alarm still works", -3.0f, Mode::Sink);
  s.varioClimbBeeps = true;
  s.varioSinkAlarm = false;
  check("sink alarm off: sink", -3.0f, Mode::Off);
  check("sink alarm off: the climb beeps still work", 1.0f, Mode::Climb);
  s.varioSinkAlarm = true;

  LOGI("TONET", "done pass=%d fail=%d", pass, fail);
}

#endif  // PV_TEST
