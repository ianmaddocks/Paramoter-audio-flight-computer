#include "TestAudio.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <driver/i2s.h>

#include "../../Paramoter audio flight computer/src/Pins.h"


namespace testaudio {
namespace {

constexpr i2s_port_t kPort = I2S_NUM_0;
constexpr uint32_t kRate = 16000;
constexpr int kFrames = 256;
constexpr uint32_t kRamp = kRate * 5 / 1000;

enum class Mode : uint8_t { Idle, Tone, Sweep, Vario, Clip };

struct Cmd {
  Mode mode = Mode::Idle;
  float f0 = 0, f1 = 0;
  uint32_t samples = 0;
  Wave wave = Wave::Sine;
  float climb = 0;
  char clip[32] = "";
  uint32_t gen = 0;
};

portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;
Cmd gCmd;
volatile Mode gPlaying = Mode::Idle;
volatile uint8_t gVolume = 70;
bool gOk = false;
char gError[64] = "";
int16_t gSine[256];

const int8_t kIndexTable[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
const int16_t kStepTable[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,   253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

void setCmd(const Cmd& c) {
  portENTER_CRITICAL(&gMux);
  uint32_t gen = gCmd.gen + 1;
  gCmd = c;
  gCmd.gen = gen;
  gPlaying = c.mode;  // so busy() is true before the audio task picks the command up
  portEXIT_CRITICAL(&gMux);
}

void setError(const char* msg) {
  strlcpy(gError, msg, sizeof(gError));
  Serial.printf("[audio] %s\n", msg);
}

void audioTask(void*) {
  static int16_t buf[kFrames * 2];
  Cmd cmd;
  uint32_t gen = UINT32_MAX;
  uint32_t t = 0;  // samples since command start
  uint32_t phase = 0;
  int32_t env = 0;
  File file;
  uint32_t clipLeft = 0;
  int32_t pred = 0;
  int index = 0;
  uint8_t fileBuf[128];
  int fileLen = 0, filePos = 0;
  bool highNibble = false;

  for (;;) {
    portENTER_CRITICAL(&gMux);
    bool changed = gCmd.gen != gen;
    if (changed) cmd = gCmd;
    portEXIT_CRITICAL(&gMux);

    if (changed) {
      gen = cmd.gen;
      t = 0;
      if (file) file.close();
      if (cmd.mode == Mode::Clip) {
        char path[48];
        snprintf(path, sizeof(path), "/v/%s.adp", cmd.clip);
        file = LittleFS.open(path, "r");
        uint8_t hdr[12];
        if (!file || file.read(hdr, 12) != 12 || memcmp(hdr, "IMA1", 4) != 0) {
          setError(file ? "voice clip has a bad header" : "voice clip not found (run uploadfs)");
          if (file) file.close();
          cmd.mode = Mode::Idle;
        } else {
          clipLeft = hdr[8] | (hdr[9] << 8) | (hdr[10] << 16) | ((uint32_t)hdr[11] << 24);
          pred = 0;
          index = 0;
          fileLen = filePos = 0;
          highNibble = false;
        }
      }
      gPlaying = cmd.mode;
    }

    const int32_t gain = gVolume * 256 / 100;
    for (int i = 0; i < kFrames; ++i) {
      int32_t s = 0;
      float hz = 0;
      bool gate = false;
      switch (cmd.mode) {
        case Mode::Tone:
          hz = cmd.f0;
          gate = t < cmd.samples;
          break;
        case Mode::Sweep:
          gate = t < cmd.samples;
          hz = cmd.f0 * powf(cmd.f1 / cmd.f0, (float)t / cmd.samples);
          break;
        case Mode::Vario: {
          float v = cmd.climb;
          if (v >= 0.3f) {
            hz = constrain(700.0f + 100.0f * v, 200.0f, 2500.0f);
            uint32_t period = (uint32_t)constrain(500.0f - 70.0f * v, 150.0f, 500.0f) * kRate / 1000;
            gate = (t % period) < period / 2;
          } else if (v <= -2.0f) {
            hz = constrain(350.0f + 20.0f * v, 150.0f, 1000.0f);
            gate = true;
          }
          break;
        }
        case Mode::Clip: {
          if (clipLeft == 0) break;
          if (!highNibble && filePos >= fileLen) {
            fileLen = file.read(fileBuf, sizeof(fileBuf));
            filePos = 0;
            if (fileLen <= 0) {
              clipLeft = 0;
              break;
            }
          }
          uint8_t code = highNibble ? fileBuf[filePos++] >> 4 : fileBuf[filePos] & 0x0F;
          highNibble = !highNibble;
          int step = kStepTable[index];
          int diff = step >> 3;
          if (code & 4) diff += step;
          if (code & 2) diff += step >> 1;
          if (code & 1) diff += step >> 2;
          pred = constrain(pred + ((code & 8) ? -diff : diff), -32768, 32767);
          index = constrain(index + kIndexTable[code], 0, 88);
          --clipLeft;
          s = pred * gain / 256;
          break;
        }
        default: break;
      }
      if (cmd.mode != Mode::Clip) {
        if (gate && env < (int32_t)kRamp) ++env;
        if (!gate && env > 0) --env;
        if (env > 0) {
          phase += (uint32_t)(hz * 4294967296.0f / kRate);
          int32_t w = cmd.wave == Wave::Square ? ((phase >> 31) ? -24000 : 24000) : gSine[phase >> 24];
          s = w * gain / 256 * env / (int32_t)kRamp;
        }
      }
      buf[2 * i] = buf[2 * i + 1] = (int16_t)s;
      ++t;
    }

    bool finished = (cmd.mode == Mode::Tone || cmd.mode == Mode::Sweep) ? (t >= cmd.samples && env == 0)
                    : cmd.mode == Mode::Clip                            ? clipLeft == 0
                                                                        : false;
    if (finished) {
      if (file) file.close();
      cmd.mode = Mode::Idle;
      gPlaying = Mode::Idle;
    }

    size_t written;
    i2s_write(kPort, buf, sizeof(buf), &written, portMAX_DELAY);
  }
}

}  // namespace

bool begin() {
  for (int i = 0; i < 256; ++i) gSine[i] = (int16_t)(32000.0f * sinf(2.0f * PI * i / 256.0f));
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = kRate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = kFrames;
  cfg.tx_desc_auto_clear = true;
  if (i2s_driver_install(kPort, &cfg, 0, nullptr) != ESP_OK) {
    setError("I2S driver install failed");
    return false;
  }
  i2s_pin_config_t pinCfg = {};
  pinCfg.mck_io_num = I2S_PIN_NO_CHANGE;
  pinCfg.bck_io_num = pins::I2S_BCLK;
  pinCfg.ws_io_num = pins::I2S_LRC;
  pinCfg.data_out_num = pins::I2S_DOUT;
  pinCfg.data_in_num = I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(kPort, &pinCfg) != ESP_OK) {
    setError("I2S pin setup failed");
    return false;
  }
  i2s_zero_dma_buffer(kPort);
  xTaskCreate(audioTask, "audio", 4096, nullptr, 5, nullptr);
  gOk = true;
  return true;
}

bool ok() { return gOk; }

void tone(float hz, uint32_t ms, Wave wave) {
  Cmd c;
  c.mode = Mode::Tone;
  c.f0 = constrain(hz, 50.0f, 7000.0f);
  c.samples = constrain(ms, 20u, 10000u) * kRate / 1000;
  c.wave = wave;
  setCmd(c);
}

void sweep(float fromHz, float toHz, uint32_t ms) {
  Cmd c;
  c.mode = Mode::Sweep;
  c.f0 = constrain(fromHz, 50.0f, 7000.0f);
  c.f1 = constrain(toHz, 50.0f, 7000.0f);
  c.samples = constrain(ms, 100u, 20000u) * kRate / 1000;
  setCmd(c);
}

void vario(float climbMs) {
  Cmd c;
  c.mode = Mode::Vario;
  c.climb = constrain(climbMs, -10.0f, 10.0f);
  setCmd(c);
}

void clip(const char* name) {
  Cmd c;
  c.mode = Mode::Clip;
  strlcpy(c.clip, name, sizeof(c.clip));
  gError[0] = 0;
  setCmd(c);
}

void stop() { setCmd(Cmd()); }

void setVolume(uint8_t pct) { gVolume = pct > 100 ? 100 : pct; }
uint8_t volume() { return gVolume; }
bool busy() { return gPlaying != Mode::Idle; }

const char* mode() {
  switch (gPlaying) {
    case Mode::Tone: return "tone";
    case Mode::Sweep: return "sweep";
    case Mode::Vario: return "vario";
    case Mode::Clip: return "clip";
    default: return "idle";
  }
}

const char* lastError() { return gError; }

}  // namespace testaudio
