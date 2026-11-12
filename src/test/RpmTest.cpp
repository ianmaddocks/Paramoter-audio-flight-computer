#include "sensors/Mic.h"
#include "test/UnitTest.h"

#if PV_TEST && PV_HAS_MIC

#include <math.h>

namespace {

constexpr int kBins = 512;
constexpr float kBinHz = 2000.0f / 1024.0f;  // the mic task's FFT: 2 kHz decimated rate, 1024 points
float gMag[kBins];

// A Hann-windowed line: main bin plus half-height neighbours, at fractional position pos.
void line(float pos, float amp) {
  int c = (int)lroundf(pos);
  for (int d = -1; d <= 1; ++d) {
    int k = c + d;
    if (k < 1 || k >= kBins) continue;
    float dist = fabsf(k - pos);
    float a = dist < 1.0f ? amp * (1.0f - 0.5f * dist) : 0.0f;
    if (a > gMag[k]) gMag[k] = a;
  }
}

// An engine at rpm: harmonics of the firing frequency 1/h, plus a noise floor of the given level.
void engine(float rpm, float ppr, float floorLevel, float amp = 100.0f, unsigned seed = 1) {
  for (int k = 0; k < kBins; ++k) {
    seed = seed * 1664525u + 1013904223u;
    gMag[k] = floorLevel * (0.5f + (seed >> 16) / 65536.0f);
  }
  const float f0 = rpm * ppr / 60.0f;
  for (int h = 1; h <= 8; ++h) line(f0 * h / kBinHz, amp / h);
}

}  // namespace

void rpmUnitSuite(UnitReport& r) {
  float rpm, conf;
  char what[64];

  // Known engine speeds across the range come back within 1.5 % (bin width is ~2 Hz, i.e. a few per cent
  // of the lowest speeds, so the interpolation matters), with a high confidence
  const float speeds[] = {1500, 2000, 2800, 3500, 4500, 6000, 8000};
  for (float s : speeds) {
    engine(s, 1.0f, 1.0f);
    mic::rpmFromSpectrum(gMag, kBinHz, 1.0f, 1200, 10000, &rpm, &conf);
    snprintf(what, sizeof what, "%.0f rpm", s);
    r.near(rpm, s, s * 0.015f, what);
    r.check(conf >= 3.0f, "%.0f rpm: confidence %.1f below the default gate of 3", s, conf);
  }

  // Pulses per revolution scale the answer: a 4-stroke twin fires twice per turn
  engine(3000, 2.0f, 1.0f);
  mic::rpmFromSpectrum(gMag, kBinHz, 2.0f, 1200, 10000, &rpm, &conf);
  r.near(rpm, 3000, 45, "3000 rpm with 2 pulses per revolution");
  engine(3000, 0.5f, 1.0f);
  mic::rpmFromSpectrum(gMag, kBinHz, 0.5f, 1200, 10000, &rpm, &conf);
  r.near(rpm, 3000, 45, "3000 rpm with 0.5 pulses per revolution");

  // Harmonic content: the fundamental can be weak (only harmonics 2..6 strong) and still be found at the
  // right octave
  engine(2500, 1.0f, 1.0f, 100.0f);
  gMag[(int)lroundf(2500 / 60.0f / kBinHz)] *= 0.2f;
  mic::rpmFromSpectrum(gMag, kBinHz, 1.0f, 1200, 10000, &rpm, &conf);
  r.near(rpm, 2500, 40, "weak fundamental at 2500 rpm (octave not mistaken)");

  // No engine: a flat noise floor has a confidence near 1 (below the gate), many different noise patterns
  float worst = 0;
  for (unsigned seed = 1; seed <= 40; ++seed) {
    engine(3000, 1.0f, 1.0f, 0.0f, seed);
    mic::rpmFromSpectrum(gMag, kBinHz, 1.0f, 1200, 10000, &rpm, &conf);
    if (conf > worst) worst = conf;
  }
  r.check(worst < 3.0f, "noise only: worst confidence %.2f over 40 patterns reaches the gate of 3", worst);

  // Nothing at all (silent input) gives zero, not a number out of thin air
  for (int k = 0; k < kBins; ++k) gMag[k] = 0;
  mic::rpmFromSpectrum(gMag, kBinHz, 1.0f, 1200, 10000, &rpm, &conf);
  r.check(rpm == 0.0f && conf == 0.0f, "silent spectrum: rpm %.1f conf %.2f, expected 0 / 0", rpm, conf);

  // A single strong tone with no harmonics (a whistle, the speaker) is weaker evidence than a real series
  for (int k = 0; k < kBins; ++k) gMag[k] = 1.0f;
  line(1000.0f / kBinHz, 100.0f);
  mic::rpmFromSpectrum(gMag, kBinHz, 1.0f, 1200, 10000, &rpm, &conf);
  float tone = conf;
  engine(3000, 1.0f, 1.0f);
  mic::rpmFromSpectrum(gMag, kBinHz, 1.0f, 1200, 10000, &rpm, &conf);
  r.check(conf > tone, "a harmonic series (%.1f) should score above a lone tone (%.1f)", conf, tone);

  // The configured range limits the search: an engine outside it is not reported as in range
  engine(900, 1.0f, 1.0f);
  mic::rpmFromSpectrum(gMag, kBinHz, 1.0f, 1200, 10000, &rpm, &conf);
  r.check(rpm == 0.0f || rpm >= 1150, "900 rpm with a 1200 minimum: reported %.0f", rpm);
  engine(7000, 1.0f, 1.0f);
  mic::rpmFromSpectrum(gMag, kBinHz, 1.0f, 1200, 5000, &rpm, &conf);
  r.check(rpm <= 5050, "7000 rpm with a 5000 maximum: reported %.0f", rpm);
}

#endif
