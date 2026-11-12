#include "Mic.h"

#if PV_HAS_MIC

#include <Arduino.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>
#include <esp_timer.h>

#include <algorithm>
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "Log.h"
#include "Pins.h"
#include "Settings.h"
#include "audio/Audio.h"
#include "sensors/RpmTrack.h"
#include "test/Sim.h"

namespace mic {
namespace {

// ADC: mic and battery alternate, so each gets half of kAdcRateHz. The mic stream is then
// low-passed and decimated by 2 (~2 kHz), which covers the first harmonics of a paramotor
// (single-cylinder two-stroke fires 25..170 Hz) with ~2 Hz FFT bins.
constexpr uint32_t kAdcRateHz = 8000;
constexpr int kChunkBytes = 256;  // 64 conversions per DMA interrupt
constexpr int kDecim = 2;
constexpr int kN = 1024;          // FFT size: 0.5 s window at 2 kHz
constexpr int kHop = kN / 2;      // new result every 0.25 s
constexpr float kNominalFs = (float)kAdcRateHz / 2 / kDecim;

constexpr uint32_t kSpeechHoldoffMs = 250;  // I2S DMA + window edge margin after speech ends
constexpr float kNoiseMinHz = 20.0f;        // ignore DC wander / mic handling below this
constexpr float kAttackS = 1.0f;            // noise level rise time constant
constexpr int kHarmonics = 6;
constexpr float kHarmWeight = 0.85f;        // later harmonics count a little less (avoids octave-up errors)
constexpr float kMaxAnalysisHz = 900.0f;    // decimation filter rolls off above this
constexpr int kNotchBins = 4;               // +/- bins removed around the speaker tone
constexpr uint32_t kRpmHoldMs = 1500;       // keep the last RPM through brief dropouts
constexpr int kMaxCandidates = 512;

adc_channel_t micCh;
adc_channel_t battCh;
esp_adc_cal_characteristics_t calChars;
bool gRunning = false;

portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;
// Shared with the loop task (under gMux).
uint32_t gBattSum = 0, gBattCount = 0;
float gNoiseDb = NAN;
float gRpm = 0, gRpmRaw = 0, gRpmConf = 0;
int32_t gBattMv = -1;  // loop task only
#if PV_HAS_SPECTRUM
// Spectrum snapshot for the web UI (under gMux). Only filled while gSpecWantUntilMs is in the future.
uint8_t gSpec[kSpectrumBins];
uint32_t gSpecSeq = 0;
float gSpecBinHz = 0, gSpecToneHz = 0;
volatile uint32_t gSpecWantUntilMs = 0;
#endif

// ---------- mic task state ----------
float cosTab[kN / 2], sinTab[kN / 2];
float re[kN], im[kN];
float scores[kMaxCandidates];
float scoreSort[kMaxCandidates];
int16_t frame[kN];
int frameFill = 0;
int32_t hist[4] = {2048, 2048, 2048, 2048};
uint8_t decimPhase = 0;

uint32_t lastSpeechMs = 0;
bool everSpoke = false;
float lastToneHz = 0;
float fsDec = kNominalFs;
uint32_t rateCount = 0;
int64_t rateStartUs = 0;
float noiseSmooth = NAN;
RpmTracker rpmTracker;  // what is shown; see RpmTrack.h
uint32_t overflows = 0;

void fft() {
  for (int i = 1, j = 0; i < kN; ++i) {
    int bit = kN >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      std::swap(re[i], re[j]);
      std::swap(im[i], im[j]);
    }
  }
  for (int len = 2; len <= kN; len <<= 1) {
    const int half = len >> 1;
    const int step = kN / len;
    for (int i = 0; i < kN; i += len) {
      for (int k = 0; k < half; ++k) {
        const float wr = cosTab[k * step], wi = -sinTab[k * step];
        const int a = i + k, b = a + half;
        const float tr = re[b] * wr - im[b] * wi;
        const float ti = re[b] * wi + im[b] * wr;
        re[b] = re[a] - tr;
        im[b] = im[a] - ti;
        re[a] += tr;
        im[a] += ti;
      }
    }
  }
}

// Periodic Hann window from the twiddle table.
inline float hann(int i) {
  float c = i < kN / 2 ? cosTab[i] : i == kN / 2 ? -1.0f : cosTab[kN - i];
  return 0.5f - 0.5f * c;
}

// Zeroes the bins around f and its 2nd/3rd harmonics (speaker distortion), folded into the
// decimated band where they would alias.
void notch(float* pw, float f, float binHz) {
  if (f <= 0) return;
  for (int h = 1; h <= 3; ++h) {
    float a = fmodf(f * h, fsDec);
    if (a > fsDec / 2) a = fsDec - a;
    int c = (int)lroundf(a / binHz);
    for (int k = std::max(0, c - kNotchBins); k <= std::min(kN / 2 - 1, c + kNotchBins); ++k) pw[k] = 0;
  }
}

// Largest magnitude in mag[lo..hi], returns its bin through *at.
float peakIn(const float* mag, int lo, int hi, int* at) {
  float best = -1;
  for (int k = lo; k <= hi; ++k) {
    if (mag[k] > best) {
      best = mag[k];
      *at = k;
    }
  }
  return best;
}

// Weighted harmonic sum for a fundamental at fractional bin c. Harmonic h is searched within
// +/- h/4 bins (candidates are spaced 1/2 bin apart).
float harmonicScore(const float* mag, float c, int kLimit) {
  float s = 0, w = 1;
  for (int h = 1; h <= kHarmonics; ++h, w *= kHarmWeight) {
    float p = c * h;
    int lo = (int)lroundf(p - 0.25f * h), hi = (int)lroundf(p + 0.25f * h);
    if (hi >= kLimit) break;
    int at;
    s += w * peakIn(mag, std::max(1, lo), hi, &at);
  }
  return s;
}

// Fundamental (Hz) from the chosen candidate: each harmonic's peak is refined by parabolic
// interpolation and divided by h; the results are averaged, weighted by peak magnitude.
float refineHz(const float* mag, float c, int kLimit, float binHz) {
  float sum = 0, wsum = 0;
  for (int h = 1; h <= kHarmonics; ++h) {
    float p = c * h;
    int lo = (int)lroundf(p - 0.25f * h), hi = (int)lroundf(p + 0.25f * h);
    if (hi >= kLimit - 1) break;
    int at = lo;
    float m = peakIn(mag, std::max(1, lo), hi, &at);
    if (m <= 0) continue;
    float a = mag[at - 1], b = mag[at], d = mag[at + 1];
    float den = a - 2 * b + d;
    float off = den < 0 ? 0.5f * (a - d) / den : 0;
    sum += m * (at + off) / h;
    wsum += m;
  }
  return wsum > 0 ? sum / wsum * binHz : c * binHz;
}

}  // namespace

// RPM candidates: fundamental bins for the configured RPM range, 1/2 bin apart; the one whose harmonic
// series scores best wins, its confidence is its score over the median candidate's.
void rpmFromSpectrum(const float* mag, float binHz, float ppr, float rpmMin, float rpmMax, float* rpm,
                     float* confidence) {
  const int kLimit = std::min(kN / 2, (int)(kMaxAnalysisHz / binHz));
  const float cMin = std::max(2.0f, rpmMin * ppr / 60.0f / binHz);
  const float cMax = std::min((float)kLimit - 1, rpmMax * ppr / 60.0f / binHz);
  // The score buffers are shared: the mic task and (test build) the unit tests both call this
  static StaticSemaphore_t lockBuf;
  static SemaphoreHandle_t lock = xSemaphoreCreateMutexStatic(&lockBuf);
  xSemaphoreTake(lock, portMAX_DELAY);
  int n = 0;
  float bestScore = -1, bestC = 0;
  for (float c = cMin; c <= cMax && n < kMaxCandidates; c += 0.5f, ++n) {
    scores[n] = harmonicScore(mag, c, kLimit);
    if (scores[n] > bestScore) {
      bestScore = scores[n];
      bestC = c;
    }
  }
  float raw = 0, conf = 0;
  if (n >= 3 && bestScore > 0) {
    std::copy(scores, scores + n, scoreSort);
    std::nth_element(scoreSort, scoreSort + n / 2, scoreSort + n);
    float median = scoreSort[n / 2];
    conf = median > 0 ? bestScore / median : 0;
    raw = refineHz(mag, bestC, kLimit, binHz) * 60.0f / ppr;
  }
  xSemaphoreGive(lock);
  *rpm = raw;
  *confidence = conf;
}

namespace {

void process(uint32_t nowMs) {
  const float binHz = fsDec / kN;
  const float windowMs = kN * 1000.0f / fsDec;
  const bool speechClear = !everSpoke || nowMs - lastSpeechMs > windowMs + kSpeechHoldoffMs;

  int32_t sum = 0;
  for (int i = 0; i < kN; ++i) sum += frame[i];
  const float mean = (float)sum / kN;
  for (int i = 0; i < kN; ++i) {
    re[i] = (frame[i] - mean) * hann(i);
    im[i] = 0;
  }
  fft();

  float* pw = re;  // power spectrum, bins 0..N/2-1
  for (int k = 0; k < kN / 2; ++k) pw[k] = re[k] * re[k] + im[k] * im[k];
  const float toneHz = audio::toneHz();

#if PV_HAS_SPECTRUM
  if ((int32_t)(gSpecWantUntilMs - nowMs) > 0) {
    // Single-sided amplitude of a Hann-windowed bin is 2*|X|/sum(w) = |X|/256 ADC counts;
    // 2048 counts is full scale. Quantised to 0.4 dB steps over -100..0 dBFS.
    constexpr float kNorm = 1.0f / (256.0f * 256.0f * 2048.0f * 2048.0f);
    static uint8_t q[kSpectrumBins];
    for (int k = 0; k < kSpectrumBins; ++k) {
      float d = 10.0f * log10f(pw[k] * kNorm + 1e-12f);
      q[k] = (uint8_t)constrain((int)lroundf((d + 100.0f) * 2.5f), 0, 250);
    }
    portENTER_CRITICAL(&gMux);
    memcpy(gSpec, q, sizeof(q));
    gSpecBinHz = binHz;
    gSpecToneHz = toneHz;
    ++gSpecSeq;
    portEXIT_CRITICAL(&gMux);
  }
#endif  // PV_HAS_SPECTRUM

  notch(pw, toneHz, binHz);
  notch(pw, lastToneHz, binHz);  // the window also spans the previous tone
  lastToneHz = toneHz;

  // Noise: Parseval over the band. mean square = 2 * sum|X|^2 / (N * sum w^2), sum w^2 = 3N/8.
  float band = 0;
  for (int k = std::max(1, (int)(kNoiseMinHz / binHz)); k < kN / 2; ++k) band += pw[k];
  const float meanSq = 2.0f * band / ((float)kN * (3.0f * kN / 8.0f));
  const float db = 10.0f * log10f(meanSq / (2048.0f * 2048.0f) + 1e-12f);

  float* mag = im;
  for (int k = 0; k < kN / 2; ++k) mag[k] = sqrtf(pw[k]);

  float raw = 0, conf = 0;
  rpmFromSpectrum(mag, binHz, g_cfg.rpmPulsesPerRev, (float)g_cfg.rpmMin, (float)g_cfg.rpmMax, &raw, &conf);
#if PV_TEST
  if (sim::takeRpmGlitch(&raw)) conf = 99.0f;  // one bad window, as far as everything after this can tell
#endif

  const float dt = kHop / fsDec;
  if (speechClear) {
    if (isnan(noiseSmooth)) noiseSmooth = db;
    float tau = db > noiseSmooth ? kAttackS : (float)g_cfg.noiseReleaseS;
    noiseSmooth += (db - noiseSmooth) * dt / (tau + dt);
  }
  const float rpmSmooth = rpmTracker.update(raw, conf, g_cfg.rpmMinConfidence, speechClear, nowMs, kRpmHoldMs);

  portENTER_CRITICAL(&gMux);
  gNoiseDb = noiseSmooth;
  gRpm = rpmSmooth;
  if (speechClear) {
    gRpmRaw = raw;
    gRpmConf = conf;
  }
  portEXIT_CRITICAL(&gMux);

#if PV_DEBUG
  static uint32_t lastLogMs = 0;
  if (nowMs - lastLogMs >= 2000) {
    lastLogMs = nowMs;
    LOGD("MIC", "noise=%.1f (blk %.1f) dBFS rpm=%.0f raw=%.0f conf=%.1f fs=%.0f tone=%.0f clear=%d ovf=%lu", noiseSmooth,
         db, rpmSmooth, raw, conf, fsDec, toneHz, speechClear, (unsigned long)overflows);
  }
#endif
}

void onMicSample(int32_t s) {
  hist[0] = hist[1];
  hist[1] = hist[2];
  hist[2] = hist[3];
  hist[3] = s;
  if (++decimPhase < kDecim) return;
  decimPhase = 0;
  frame[frameFill++] = (int16_t)((hist[0] + 3 * hist[1] + 3 * hist[2] + hist[3]) >> 3);
  if (frameFill < kN) return;
  process(millis());
  memmove(frame, frame + kHop, (kN - kHop) * sizeof(frame[0]));
  frameFill = kN - kHop;
}

void micTask(void*) {
  static uint8_t buf[kChunkBytes];
  rateStartUs = esp_timer_get_time();
  for (;;) {
    uint32_t len = 0;
    esp_err_t err = adc_digi_read_bytes(buf, sizeof(buf), &len, 200);
    if (err == ESP_ERR_INVALID_STATE) ++overflows;  // data still returned; the task fell behind
    else if (err != ESP_OK) continue;

    if (audio::speaking()) {
      lastSpeechMs = millis();
      everSpoke = true;
    }
    uint32_t battSum = 0, battN = 0;
    for (uint32_t i = 0; i + 4 <= len; i += 4) {
      adc_digi_output_data_t d;
      memcpy(&d.val, buf + i, 4);
      if (d.type2.unit != 0) continue;
      if (d.type2.channel == micCh) {
        ++rateCount;
        // Test build: a synthetic engine / noise / dead input replaces the ADC samples
        onMicSample(sim::micMode() != sim::MicMode::Real ? sim::micSample() : d.type2.data);
      } else if (d.type2.channel == battCh) {
        battSum += d.type2.data;
        ++battN;
      }
    }
    portENTER_CRITICAL(&gMux);
    gBattSum += battSum;
    gBattCount += battN;
    portEXIT_CRITICAL(&gMux);

    // Measured sample rate, so RPM does not depend on the ADC clock divider being exact.
    int64_t elapsed = esp_timer_get_time() - rateStartUs;
    if (elapsed >= 4000000) {
      float fs = rateCount * 1e6f / elapsed / kDecim;
      if (fs > kNominalFs * 0.5f && fs < kNominalFs * 4) fsDec = fs;
      rateCount = 0;
      rateStartUs += elapsed;
    }
  }
}

}  // namespace

void begin() {
  for (int i = 0; i < kN / 2; ++i) {
    cosTab[i] = cosf(2.0f * PI * i / kN);
    sinTab[i] = sinf(2.0f * PI * i / kN);
  }
  int mc = digitalPinToAnalogChannel(pins::MIC_ADC);
  int bc = digitalPinToAnalogChannel(pins::BATT_ADC);
  if (mc < 0 || bc < 0 || mc >= SOC_ADC_CHANNEL_NUM(0) || bc >= SOC_ADC_CHANNEL_NUM(0)) {
    LOGE("MIC", "Mic/battery pins are not ADC1 channels");
    return;
  }
  micCh = (adc_channel_t)mc;
  battCh = (adc_channel_t)bc;
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 1100, &calChars);

  adc_digi_init_config_t init = {};
  init.max_store_buf_size = 4096;  // ~125 ms of samples while an FFT runs
  init.conv_num_each_intr = kChunkBytes;
  init.adc1_chan_mask = BIT(micCh) | BIT(battCh);
  init.adc2_chan_mask = 0;
  if (adc_digi_initialize(&init) != ESP_OK) {
    LOGE("MIC", "ADC DMA init failed");
    return;
  }
  adc_digi_pattern_config_t pat[2] = {};
  pat[0].atten = ADC_ATTEN_DB_12;
  pat[0].channel = micCh;
  pat[0].unit = 0;
  pat[0].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
  pat[1] = pat[0];
  pat[1].channel = battCh;
  adc_digi_configuration_t dig = {};
  dig.conv_limit_en = false;
  dig.conv_limit_num = 250;
  dig.pattern_num = 2;
  dig.adc_pattern = pat;
  dig.sample_freq_hz = kAdcRateHz;
  dig.conv_mode = ADC_CONV_SINGLE_UNIT_1;
  dig.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
  if (adc_digi_controller_configure(&dig) != ESP_OK || adc_digi_start() != ESP_OK) {
    LOGE("MIC", "ADC DMA start failed");
    adc_digi_deinitialize();
    return;
  }
  // Below the audio task (5) so speech never stutters; above the loop task (1).
  xTaskCreate(micTask, "mic", 4096, nullptr, 3, nullptr);
  gRunning = true;
  LOGI("MIC", "Mic sampler running (ADC1 ch%d mic, ch%d battery)", (int)micCh, (int)battCh);
}

bool running() { return gRunning; }

float noiseDb() {
  if (!hw::hasMic()) return NAN;              // test build only: microphone "not fitted"
  if (sim::noiseOn()) return sim::noiseDb();  // test build only
  portENTER_CRITICAL(&gMux);
  float v = gNoiseDb;
  portEXIT_CRITICAL(&gMux);
  return v;
}

float rpm() {
  portENTER_CRITICAL(&gMux);
  float v = gRpm;
  portEXIT_CRITICAL(&gMux);
  return v;
}

float rpmRaw() {
  portENTER_CRITICAL(&gMux);
  float v = gRpmRaw;
  portEXIT_CRITICAL(&gMux);
  return v;
}

float rpmConfidence() {
  portENTER_CRITICAL(&gMux);
  float v = gRpmConf;
  portEXIT_CRITICAL(&gMux);
  return v;
}

#if PV_HAS_SPECTRUM
uint32_t spectrum(uint8_t* out, float* binHz, float* toneHz) {
  gSpecWantUntilMs = millis() + 3000;
  portENTER_CRITICAL(&gMux);
  uint32_t seq = gSpecSeq;
  if (seq) {
    memcpy(out, gSpec, sizeof(gSpec));
    *binHz = gSpecBinHz;
    *toneHz = gSpecToneHz;
  }
  portEXIT_CRITICAL(&gMux);
  return seq;
}
#endif  // PV_HAS_SPECTRUM

int32_t batteryMilliVolts() {
  if (!gRunning) return -1;
  portENTER_CRITICAL(&gMux);
  uint32_t s = gBattSum, n = gBattCount;
  gBattSum = gBattCount = 0;
  portEXIT_CRITICAL(&gMux);
  if (n) gBattMv = (int32_t)esp_adc_cal_raw_to_voltage(s / n, &calChars);
  return gBattMv;
}

}  // namespace mic

#endif  // PV_HAS_MIC
