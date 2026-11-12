#pragma once
#include <math.h>
#include <stdint.h>

// Turns the stream of per-window RPM estimates (one every 0.25 s, from mic::rpmFromSpectrum) into the
// reading that is shown. Pure logic, no hardware, so it can be tested directly.
//
//   * Smoothing: an estimate within 20 % of the reading moves it half way.
//   * Jumps (F4): an estimate further than 20 % away, or any estimate when there is no reading yet, is not
//     taken at once. It is held, and only accepted when the NEXT estimate agrees with it within 10 %; a lone
//     bad window (a cough, a gust in the microphone, an octave error) never reaches the display.
//   * Dropouts: with no usable estimate for holdMs the reading goes to 0.
//   * Speech (F3): while the speaker is sounding (and for a moment after) the spectrum is polluted, so no
//     estimates are taken, but nothing says the engine stopped either: the reading is held for the length
//     of the speech (at most kMaxSpeechHoldMs, so a stuck 'speaking' flag cannot hold it for ever) and
//     the dropout clock starts when the speech ends.
class RpmTracker {
 public:
  static constexpr float kJumpFraction = 0.20f;
  static constexpr float kAgreeFraction = 0.10f;
  static constexpr uint32_t kMaxSpeechHoldMs = 15000;

  // raw/conf: this window's estimate and its confidence; minConf: the confidence setting; speechClear:
  // false while the window overlaps speech; holdMs: how long a reading survives without estimates.
  float update(float raw, float conf, float minConf, bool speechClear, uint32_t nowMs, uint32_t holdMs) {
    if (speechClear) {
      speechSinceMs_ = 0;
      if (conf >= minConf && raw > 0) {
        if (smooth_ > 0 && fabsf(raw - smooth_) < kJumpFraction * smooth_) {
          smooth_ += (raw - smooth_) * 0.5f;
          lastOkMs_ = nowMs;
          pending_ = 0;
        } else if (pending_ > 0 && fabsf(raw - pending_) < kAgreeFraction * pending_) {
          smooth_ = raw;  // two windows in a row agree: it is real
          lastOkMs_ = nowMs;
          pending_ = 0;
        } else {
          pending_ = raw;  // wait for a second opinion
        }
      } else {
        pending_ = 0;
      }
    } else if (smooth_ > 0) {
      if (!speechSinceMs_) speechSinceMs_ = nowMs ? nowMs : 1;
      if (nowMs - speechSinceMs_ < kMaxSpeechHoldMs) lastOkMs_ = nowMs;
    }
    if (smooth_ > 0 && nowMs - lastOkMs_ > holdMs) {
      smooth_ = 0;
      pending_ = 0;
    }
    return smooth_;
  }

  float rpm() const { return smooth_; }
  void reset() { *this = RpmTracker(); }

 private:
  float smooth_ = 0, pending_ = 0;
  uint32_t lastOkMs_ = 0, speechSinceMs_ = 0;
};
