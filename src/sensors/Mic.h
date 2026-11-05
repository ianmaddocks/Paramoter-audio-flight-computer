#pragma once
#include <stdint.h>

// Microphone on D0, sampled with ADC1 continuous (DMA) mode in a background task.
//
// The ADC DMA controller owns ADC1, and one-shot reads (analogRead) cannot be mixed with
// it, so the battery divider (D1) is sampled in the same DMA pattern and read back here.
//
// Every analysis block (~0.5 s window, updated ~4x per second) produces:
//  - background noise level in dBFS (RMS relative to ADC full scale), excluding the
//    speaker's own vario tone; blocks that overlap speech are skipped so the vario does not
//    hear itself and ratchet the volume up;
//  - engine RPM from the strongest harmonic series in the spectrum.
namespace mic {

void begin();
bool running();

// Smoothed background noise in dBFS (about -90 = silent, 0 = clipping). NAN until measured.
float noiseDb();

// Engine RPM; 0 when no engine is detected (confidence below the setting, or no data).
float rpm();
// Raw estimate and its confidence (peak / median harmonic score), for tuning in the web UI.
float rpmRaw();
float rpmConfidence();

// Latest FFT spectrum for the web UI's Spectrum tab. 512 bins, 0..Nyquist, each a byte:
// dBFS = byte / 2.5 - 100 (0 = below -100 dBFS, 250 = a full-scale sine). Computed before the
// speaker-tone notch, so it shows everything the mic hears. The spectrum is only computed while
// something keeps calling this (it stops ~3 s after the last call), so flight use pays nothing.
// Returns a sequence number that increments with every new spectrum (0 = none yet).
constexpr int kSpectrumBins = 512;
uint32_t spectrum(uint8_t* out, float* binHz, float* toneHz);

// Battery divider input in mV (before the divider ratio). -1 if the DMA sampler is not running.
int32_t batteryMilliVolts();

// Speaker volume (0..100) after background-noise compensation.
uint8_t compensatedVolume(uint8_t basePct);

}  // namespace mic
