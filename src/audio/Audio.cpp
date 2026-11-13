#include "Audio.h"
#include "Log.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <driver/i2s.h>
#include <freertos/semphr.h>

#include "Pins.h"

namespace audio {
namespace {

constexpr i2s_port_t kPort = I2S_NUM_0;
// Output buffering: 4 blocks of 256 samples = 64 ms at 16 kHz. This is the delay between a
// change (new vario pitch, start of speech) and hearing it, and it is deliberate: writing the
// flight log can stop every task for tens of milliseconds while flash is erased, and a shorter
// buffer would run dry and click.
constexpr int kFrames = 256;
constexpr int kQueueLen = 6;
constexpr uint32_t kClipGapSamples = kSampleRate * 40 / 1000;
constexpr uint32_t kUtteranceGapSamples = kSampleRate * 250 / 1000;
constexpr uint32_t kRampSamples = kSampleRate * 5 / 1000;  // tone fade in/out: 5 ms
// Volume changes are spread over time sample by sample (0 to 100 % in 100 ms), so a change of
// volume never produces a step in the waveform.
constexpr int32_t kGainFull = 1 << 16;
constexpr int32_t kGainStep = kGainFull / (kSampleRate / 10);

// ---------- IMA ADPCM ----------
const int8_t kIndexTable[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
const int16_t kStepTable[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,   253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

struct Adpcm {
  int32_t pred = 0;
  int index = 0;

  int16_t decode(uint8_t code) {
    int step = kStepTable[index];
    int diff = step >> 3;
    if (code & 4) diff += step;
    if (code & 2) diff += step >> 1;
    if (code & 1) diff += step >> 2;
    pred += (code & 8) ? -diff : diff;
    pred = constrain(pred, -32768, 32767);
    index = constrain(index + kIndexTable[code], 0, 88);
    return (int16_t)pred;
  }
};

// ---------- speech queue (shared with loop task) ----------
SemaphoreHandle_t gLock;
Utterance gQueue[kQueueLen];
int gHead = 0, gCount = 0;
volatile bool gAbort = false;
volatile bool gBusy = false;

// ---------- tone params (written by loop task) ----------
portMUX_TYPE gToneMux = portMUX_INITIALIZER_UNLOCKED;
struct ToneParams {
  float hz = 0;
  uint16_t onMs = 0;
  uint16_t offMs = 0;
} gTone;
volatile uint8_t gSpeechVol = 90;
volatile uint8_t gToneVol = 60;

int16_t gSine[256];

// ---------- audio-task-only state ----------
enum class SpState : uint8_t { Idle, Clip, Gap };
SpState spState = SpState::Idle;
Utterance spCur;
uint8_t spClip = 0;
File spFile;
Adpcm spDec;
uint32_t spSamplesLeft = 0;
uint32_t spGapLeft = 0;
uint8_t spBuf[128];
int spBufLen = 0, spBufPos = 0;
bool spHighNibble = false;

// A tone made in place for a '~hz' clip: spSynth is true while it plays, spSamplesLeft counts it down.
bool spSynth = false;
uint32_t spSynthPhase = 0, spSynthInc = 0, spSynthTotal = 0;

// The one-shot cue (audio::cue), written by the loop task, read by the audio task.
volatile uint32_t gCueInc = 0;
volatile int32_t gCueLeft = 0;
volatile int32_t gCueTotal = 0;
uint32_t cuePhase = 0;

uint32_t tonePhase = 0;
uint32_t toneCyclePos = 0;
int32_t toneEnv = 0;  // 0..kRampSamples
int32_t speechGain = -1;  // gains in use now, 0..kGainFull; -1 until the first block
int32_t toneGain = -1;

inline int32_t stepTowards(int32_t now, int32_t want) {
  if (now < want) return now + kGainStep < want ? now + kGainStep : want;
  return now - kGainStep > want ? now - kGainStep : want;
}

bool openClip(const char* name) {
  spSynth = false;
  if (const char* bar = strchr(name, '|')) {  // "a|b": the first clip that exists (a new clip, then an old one or a tone)
    char first[32];
    size_t n = bar - name < (int)sizeof(first) - 1 ? bar - name : sizeof(first) - 1;
    memcpy(first, name, n);
    first[n] = 0;
    return openClip(first) || openClip(bar + 1);
  }
  if (name[0] == '~') {  // a tone: "~900" = 900 Hz for 120 ms
    float hz = (float)atoi(name + 1);
    if (hz < 50.0f || hz > 4000.0f) return false;
    spSynth = true;
    spSynthPhase = 0;
    spSynthInc = (uint32_t)(hz * 4294967296.0f / kSampleRate);
    spSynthTotal = spSamplesLeft = kSampleRate * 120 / 1000;
    spFile = File();  // nothing to read
    return true;
  }
  char path[40];
  snprintf(path, sizeof(path), "/v/%s.adp", name);
  spFile = LittleFS.open(path, "r");
  if (!spFile) {
    LOGW("AUD", "Missing voice clip %s", path);
    return false;
  }
  uint8_t hdr[12];
  if (spFile.read(hdr, sizeof(hdr)) != sizeof(hdr) || memcmp(hdr, "IMA1", 4) != 0) {
    LOGW("AUD", "Bad voice clip %s", path);
    spFile.close();
    return false;
  }
  spSamplesLeft = hdr[8] | (hdr[9] << 8) | (hdr[10] << 16) | ((uint32_t)hdr[11] << 24);
  spDec = Adpcm();
  spBufLen = spBufPos = 0;
  spHighNibble = false;
  return true;
}

// Opens the next playable clip of the current utterance; false when the utterance is done.
bool startNextClip() {
  while (spClip < spCur.count) {
    if (openClip(spCur.clips[spClip++])) return true;
  }
  return false;
}

void stopSpeech() {
  if (spFile) spFile.close();
  spSynth = false;
  spState = SpState::Idle;
  spSamplesLeft = 0;
}

int16_t nextSpeechSample() {
  if (spState == SpState::Gap) {
    if (--spGapLeft == 0) {
      if (spClip < spCur.count && startNextClip()) {
        spState = SpState::Clip;
      } else {
        spState = SpState::Idle;
      }
    }
    return 0;
  }
  if (spSamplesLeft == 0) {
    if (!spSynth) spFile.close();
    spSynth = false;
    bool more = spClip < spCur.count;
    spState = SpState::Gap;
    spGapLeft = more ? kClipGapSamples : kUtteranceGapSamples;
    return 0;
  }
  if (spSynth) {  // a made tone with 5 ms fades at both ends
    --spSamplesLeft;
    spSynthPhase += spSynthInc;
    int32_t env = spSynthTotal - spSamplesLeft < kRampSamples ? (int32_t)(spSynthTotal - spSamplesLeft)
                  : spSamplesLeft < kRampSamples                ? (int32_t)spSamplesLeft
                                                                : (int32_t)kRampSamples;
    return (int16_t)(((int32_t)gSine[spSynthPhase >> 24] * 10000 / 32000) * env / (int32_t)kRampSamples);
  }
  if (!spHighNibble && spBufPos >= spBufLen) {
    spBufLen = spFile.read(spBuf, sizeof(spBuf));
    spBufPos = 0;
    if (spBufLen <= 0) {
      spBufLen = 0;
      spSamplesLeft = 0;
      return 0;
    }
  }
  uint8_t byte = spBuf[spBufPos];
  uint8_t code;
  if (!spHighNibble) {
    code = byte & 0x0F;
    spHighNibble = true;
  } else {
    code = byte >> 4;
    spHighNibble = false;
    ++spBufPos;
  }
  --spSamplesLeft;
  return spDec.decode(code);
}

void render(int16_t* out, int frames) {
  // Abort and pop are checked under one lock so an urgent utterance queued together with
  // the abort flag is never itself aborted.
  bool popped = false;
  xSemaphoreTake(gLock, portMAX_DELAY);
  if (gAbort) {
    gAbort = false;
    stopSpeech();
  }
  if (spState == SpState::Idle && gCount > 0) {
    spCur = gQueue[gHead];
    gHead = (gHead + 1) % kQueueLen;
    --gCount;
    popped = true;
  }
  xSemaphoreGive(gLock);
  if (popped) {
    spClip = 0;
    if (startNextClip()) spState = SpState::Clip;
  }
  gBusy = spState != SpState::Idle;

  ToneParams tp;
  portENTER_CRITICAL(&gToneMux);
  tp = gTone;
  portEXIT_CRITICAL(&gToneMux);

  const int32_t speechWant = gSpeechVol * kGainFull / 100;
  const int32_t toneWant = gToneVol * kGainFull / 100;
  if (speechGain < 0) {  // first block: nothing to glide from
    speechGain = speechWant;
    toneGain = toneWant;
  }
  const uint32_t phaseInc = (uint32_t)(tp.hz * 4294967296.0f / kSampleRate);
  const uint32_t onSamples = tp.onMs * kSampleRate / 1000;
  const uint32_t cycleSamples = (tp.onMs + tp.offMs) * kSampleRate / 1000;
  const bool speechWaiting = spState != SpState::Idle;

  for (int i = 0; i < frames; ++i) {
    int32_t s = 0;
    speechGain = stepTowards(speechGain, speechWant);
    toneGain = stepTowards(toneGain, toneWant);
    if (spState != SpState::Idle && toneEnv == 0) {
      s = ((int32_t)nextSpeechSample() * (speechGain >> 4)) >> 12;
    } else {
      // Speech takes over from the tone, but only once the tone has faded out (5 ms): cutting
      // it off mid-wave is heard as a click before every announcement.
      bool gate = tp.hz > 0 && !speechWaiting && spState == SpState::Idle;
      if (gate && tp.offMs > 0) {
        if (toneCyclePos >= cycleSamples) toneCyclePos = 0;
        gate = toneCyclePos < onSamples;
        ++toneCyclePos;
      }
      if (gate && toneEnv < (int32_t)kRampSamples) ++toneEnv;
      if (!gate && toneEnv > 0) --toneEnv;
      if (toneEnv > 0) {
        tonePhase += phaseInc;
        s = (((int32_t)gSine[tonePhase >> 24] * (toneGain >> 4)) >> 12) * toneEnv / (int32_t)kRampSamples;
      }
    }
    if (gCueLeft > 0) {  // the cue: on top of everything, 4 ms fades, at the speech volume
      cuePhase += gCueInc;
      const int32_t played = gCueTotal - gCueLeft;
      const int32_t ramp = kSampleRate * 4 / 1000;
      int32_t env = played < ramp ? played : gCueLeft < ramp ? gCueLeft : ramp;
      s += ((((int32_t)gSine[cuePhase >> 24] * 9000) / 32000) * env / ramp) * (speechGain >> 4) >> 12;
      --gCueLeft;
      s = constrain(s, -32768, 32767);
    }
    out[2 * i] = out[2 * i + 1] = (int16_t)s;
  }
}

void audioTask(void*) {
  static int16_t buf[kFrames * 2];
  for (;;) {
    render(buf, kFrames);
    size_t written;
    i2s_write(kPort, buf, sizeof(buf), &written, portMAX_DELAY);
  }
}

}  // namespace

void begin() {
  gLock = xSemaphoreCreateMutex();
  for (int i = 0; i < 256; ++i) gSine[i] = (int16_t)(32000.0f * sinf(2.0f * PI * i / 256.0f));

  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = kSampleRate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = 0;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = kFrames;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  if (i2s_driver_install(kPort, &cfg, 0, nullptr) != ESP_OK) {
    LOGE("AUD", "I2S install failed");
    return;
  }
  i2s_pin_config_t pinCfg = {};
  pinCfg.mck_io_num = I2S_PIN_NO_CHANGE;
  pinCfg.bck_io_num = pins::I2S_BCLK;
  pinCfg.ws_io_num = pins::I2S_LRC;
  pinCfg.data_out_num = pins::I2S_DOUT;
  pinCfg.data_in_num = I2S_PIN_NO_CHANGE;
  i2s_set_pin(kPort, &pinCfg);
  i2s_zero_dma_buffer(kPort);

  xTaskCreate(audioTask, "audio", 4096, nullptr, 5, nullptr);
}

#if PV_TEST
namespace {
bool gCapture = false;
int gSaid = 0;
char gSaidText[16][200];
}  // namespace

void testCapture(bool on) {
  gCapture = on;
  gSaid = 0;
}
int testSaid() { return gSaid; }
const char* testSaidText(int i) { return i >= 0 && i < gSaid && i < 16 ? gSaidText[i] : ""; }
#endif

void say(const Utterance& u) {
  if (u.count == 0) return;
#if PV_TEST
  if (gCapture) {
    static const char* kP[] = {"low", "normal", "high", "urgent"};
    char* t = gSaidText[gSaid < 16 ? gSaid : 15];
    size_t len = snprintf(t, 200, "[%s]", kP[(int)u.prio]);
    for (int i = 0; i < u.count && len < 190; ++i) len += snprintf(t + len, 200 - len, " %s", u.clips[i]);
    ++gSaid;
    return;
  }
#endif
#if PV_DEBUG
  char text[100];
  size_t len = 0;
  text[0] = 0;
  for (int i = 0; i < u.count && len < sizeof(text) - 1; ++i) {
    len += snprintf(text + len, sizeof(text) - len, "%s%s", i ? " " : "", u.clips[i]);
  }
  static const char* kPrio[] = {"low", "normal", "high", "urgent"};
  LOGD("SAY", "[%s] %s", kPrio[(int)u.prio], text);
#endif
  const Utterance q = withSignal(u);
  xSemaphoreTake(gLock, portMAX_DELAY);
  switch (u.prio) {
    case Priority::Urgent:
      gHead = gCount = 0;
      gAbort = true;
      [[fallthrough]];
    case Priority::High:
      if (gCount == kQueueLen) --gCount;
      gHead = (gHead + kQueueLen - 1) % kQueueLen;
      gQueue[gHead] = q;
      ++gCount;
      break;
    case Priority::Low:
      if (gCount > 0) break;
      [[fallthrough]];
    case Priority::Normal:
      if (gCount < kQueueLen) {
        gQueue[(gHead + gCount) % kQueueLen] = q;
        ++gCount;
      }
      break;
  }
  xSemaphoreGive(gLock);
}

bool speaking() { return gBusy || gCount > 0; }

void cue(float hz, uint16_t ms) {
#if PV_TEST
  if (gCapture) {  // recorded like speech, as "[cue] ~<hz>"
    char* t = gSaidText[gSaid < 16 ? gSaid : 15];
    snprintf(t, 200, "[cue] ~%d", (int)hz);
    ++gSaid;
    return;
  }
#endif
  gCueInc = (uint32_t)(hz * 4294967296.0f / kSampleRate);
  gCueTotal = gCueLeft = (int32_t)(kSampleRate * ms / 1000);
}

void setTone(float hz, uint16_t onMs, uint16_t offMs) {
  portENTER_CRITICAL(&gToneMux);
  gTone.hz = hz;
  gTone.onMs = onMs;
  gTone.offMs = offMs;
  portEXIT_CRITICAL(&gToneMux);
}

float toneHz() {
  portENTER_CRITICAL(&gToneMux);
  float hz = gTone.hz;
  portEXIT_CRITICAL(&gToneMux);
  return hz;
}

void setVolumes(uint8_t speechPct, uint8_t tonePct) {
  gSpeechVol = speechPct;
  gToneVol = tonePct;
}

}  // namespace audio
