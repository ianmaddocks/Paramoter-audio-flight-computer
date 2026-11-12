#include "test/Sim.h"

#if PV_TEST

#include "Hardware.h"
#include "Log.h"
#include "nav/Geo.h"

namespace sim {
namespace {

struct State {
  bool baro = false;
  uint32_t baroEpoch = 1;
  float altM = 0;          // true (MSL) altitude of the simulated aircraft
  float vzMs = 0;          // climb rate
  bool haveTarget = false;
  float targetM = 0;       // climb/descend stops here
  float baroOffsetM = 60;  // MSL minus standard-pressure altitude (a high-pressure day)

  GpsMode gps = GpsMode::Real;
  double lat = 51.5, lon = 0.5;
  float speedMs = 0;
  float trackDeg = 0;
  float gpsAltErrM = 0;    // GPS altitude error added to altM
  bool gpsAltAbs = false;  // report gpsAltM as-is instead of altM + error
  float gpsAltM = 0;
  int sats = 10;
  float hdop = 0.9f;
  float gpsHz = 5;
  uint32_t lastNmeaMs = 0;

  bool batt = false;
  float battV = 4.0f;
  bool noise = false;
  float noiseDb = -60;

  bool hasGps = true;  // false: behave as a vario with no GPS module
  bool hasMic = true;  // false: behave as a vario with no microphone
  bool gpsForget = false;

  // A simulated aircraft: flies at airMs along hdg, drifting with the wind; speed and track (what the GPS
  // reports) then follow from it. fly=1 switches the model on; it turns at turnDegS (signed, + right), or
  // towards turnTo at turnRate until it is on that heading.
  bool fly = false;
  float hdg = 0;
  float airMs = 10;
  float windFromDeg = 0, windMs = 0;
  float turnDegS = 0;
  float turnRate = 15;
  bool haveTurnTo = false;
  float turnTo = 0;

  BaroFault baroFault = BaroFault::None;
  float baroNoiseM = 0;
  float baroSpikeM = 0;      // added to the next sample, once
  float baroHeldM = 0;       // the frozen value of a stuck sensor

  MicMode mic = MicMode::Real;
  float micHz = 0;       // engine firing frequency; 0 = no engine
  float micAmp = 150;    // engine signal amplitude, ADC counts
  float micNoise = 0;    // white noise amplitude, ADC counts

  uint32_t lastMs = 0;
} s;

int finish(char* buf, int n, size_t size) {
  uint8_t cs = 0;
  for (int i = 1; i < n; ++i) cs ^= (uint8_t)buf[i];
  return n + snprintf(buf + n, size - n, "*%02X\r\n", cs);
}

void coord(double deg, int degDigits, char pos, char neg, char* out, size_t size, char* hemi) {
  *hemi = deg < 0 ? neg : pos;
  deg = fabs(deg);
  int d = (int)deg;
  double m = (deg - d) * 60.0;
  snprintf(out, size, "%0*d%08.5f", degDigits, d, m);
}

}  // namespace

bool baroOn() { return s.baro; }
uint32_t baroEpoch() { return s.baroEpoch; }
float baroAltStdM() {
  static uint32_t rng = 777;
  float v = s.altM - s.baroOffsetM;
  if (s.baroFault == BaroFault::Stuck) return s.baroHeldM;
  s.baroHeldM = v;
  if (s.baroFault == BaroFault::Noise) {
    rng = rng * 1664525u + 1013904223u;
    v += ((rng >> 8) / 8388608.0f - 1.0f) * s.baroNoiseM;
  }
  v += s.baroSpikeM;
  s.baroSpikeM = 0;
  return v;
}
bool baroAvail() { return s.baroFault != BaroFault::Dead; }
float baroPressureOverride() { return s.baroFault == BaroFault::Range ? 5000.0f : 0.0f; }
GpsMode gpsMode() { return s.gps; }
bool gpsFitted() { return s.hasGps; }
bool takeGpsForget() {
  const bool f = s.gpsForget;
  s.gpsForget = false;
  return f;
}
MicMode micMode() { return s.mic; }

// One ADC sample of the synthetic microphone: the engine's firing pulses as a six-harmonic series at
// micHz (sample rate 4 kHz, the ADC's rate for the microphone) plus white noise. The waveform comes
// from a table so that this costs a few microseconds per sample.
int32_t micSample() {
  static int16_t wave[1024];
  static bool built = false;
  static uint32_t phase = 0, rng = 12345;
  if (!built) {
    for (int i = 0; i < 1024; ++i) {
      float x = 0;
      for (int h = 1; h <= 6; ++h) x += sinf(h * 2.0f * (float)M_PI * i / 1024.0f) / h;
      wave[i] = (int16_t)lroundf(x * 1000.0f / 1.8f);  // peak about +-1000
    }
    built = true;
  }
  if (s.mic == MicMode::Dead) return 2048;
  float x = 0;
  if (s.micHz > 0.0f) {
    phase += (uint32_t)(s.micHz / 4000.0f * 4294967296.0f);
    x = wave[phase >> 22] * s.micAmp / 1000.0f;
  }
  rng = rng * 1664525u + 1013904223u;
  x += ((rng >> 8) / 8388608.0f - 1.0f) * s.micNoise;
  return constrain((int32_t)lroundf(2048.0f + x), 0, 4095);
}

bool battOn() { return s.batt; }
float battV() { return s.battV; }
bool noiseOn() { return s.noise; }
float noiseDb() { return s.noiseDb; }

void update(uint32_t nowMs) {
  float dt = s.lastMs ? (nowMs - s.lastMs) / 1000.0f : 0.0f;
  s.lastMs = nowMs;
  if (dt <= 0 || dt > 1.0f) return;
  if (s.vzMs != 0) {
    s.altM += s.vzMs * dt;
    if (s.haveTarget && ((s.vzMs > 0 && s.altM >= s.targetM) || (s.vzMs < 0 && s.altM <= s.targetM))) {
      s.altM = s.targetM;
      s.vzMs = 0;
      s.haveTarget = false;
      LOGI("SIM", "reached target altitude %.1f m", s.altM);
    }
  }
  if (s.fly) {
    if (s.haveTurnTo) {
      float diff = fmodf(s.turnTo - s.hdg + 540.0f, 360.0f) - 180.0f;  // -180..180, + is to the right
      float step = s.turnRate * dt;
      if (fabsf(diff) <= step) {
        s.hdg = s.turnTo;
        s.haveTurnTo = false;
        s.turnDegS = 0;
      } else {
        s.hdg += diff > 0 ? step : -step;
      }
    } else {
      s.hdg += s.turnDegS * dt;
    }
    s.hdg = fmodf(s.hdg + 720.0f, 360.0f);
    const float h = s.hdg * geo::kDegToRadF, w = (s.windFromDeg + 180.0f) * geo::kDegToRadF;
    const float ve = s.airMs * sinf(h) + s.windMs * sinf(w), vn = s.airMs * cosf(h) + s.windMs * cosf(w);
    s.speedMs = hypotf(ve, vn);
    s.trackDeg = geo::vecBearing(ve, vn);
  }
  if (s.speedMs > 0) {
    float d = s.speedMs * dt;
    float t = s.trackDeg * geo::kDegToRadF;
    s.lat += (double)(d * cosf(t)) / 111320.0;
    s.lon += (double)(d * sinf(t)) / (111320.0 * cos(s.lat * 0.017453292519943295));
  }
}

size_t gpsNmea(uint32_t nowMs, char* out, size_t size) {
  if (s.gps != GpsMode::Fix && s.gps != GpsMode::NoFix) return 0;
  uint32_t period = (uint32_t)(1000.0f / constrain(s.gpsHz, 1.0f, 10.0f));
  if (nowMs - s.lastNmeaMs < period) return 0;
  s.lastNmeaMs = nowMs;

  // Simulated clock: 2021-01-01 12:00:00 UTC at power-on.
  uint32_t t = 12 * 3600 + nowMs / 1000;
  char timeStr[12];
  snprintf(timeStr, sizeof(timeStr), "%02lu%02lu%02lu.%02lu", (unsigned long)(t / 3600 % 24),
           (unsigned long)(t / 60 % 60), (unsigned long)(t % 60), (unsigned long)(nowMs % 1000 / 10));
  char lat[16], lon[16], latH, lonH;
  coord(s.lat, 2, 'N', 'S', lat, sizeof(lat), &latH);
  coord(s.lon, 3, 'E', 'W', lon, sizeof(lon), &lonH);
  bool fix = s.gps == GpsMode::Fix;
  float alt = s.gpsAltAbs ? s.gpsAltM : s.altM + s.gpsAltErrM;

  int n = snprintf(out, size, "$GPGGA,%s,%s,%c,%s,%c,%d,%02d,%.2f,%.1f,M,0.0,M,,", timeStr, lat, latH, lon,
                   lonH, fix ? 1 : 0, s.sats, s.hdop, alt);
  n = finish(out, n, size);
  int m = snprintf(out + n, size - n, "$GPRMC,%s,%c,%s,%c,%s,%c,%.2f,%.1f,010121,,,%c", timeStr,
                   fix ? 'A' : 'V', lat, latH, lon, lonH, s.speedMs / 0.514444f, s.trackDeg, fix ? 'A' : 'N');
  return n + finish(out + n, m, size - n);
}

bool set(const String& key, const String& value) {
  float f = value.toFloat();
  bool on = value == "1" || value == "on" || value == "true";
  if (key == "baro") {
    s.baro = on;
    ++s.baroEpoch;
  } else if (key == "alt") {
    s.altM = f;
    s.haveTarget = false;
    ++s.baroEpoch;
  } else if (key == "vz") {
    s.vzMs = f;
  } else if (key == "to") {
    s.targetM = f;
    s.haveTarget = true;
  } else if (key == "baroOffset") {
    s.baroOffsetM = f;
    ++s.baroEpoch;
  } else if (key == "fly") {
    s.fly = on;
  } else if (key == "hdg") {
    s.hdg = f;
    s.haveTurnTo = false;
  } else if (key == "air") {
    s.airMs = f;
  } else if (key == "windFrom") {
    s.windFromDeg = f;
  } else if (key == "windSpd") {
    s.windMs = f;
  } else if (key == "turn") {
    s.turnDegS = f;
    s.haveTurnTo = false;
  } else if (key == "turnRate") {
    s.turnRate = f;
  } else if (key == "turnTo") {
    s.turnTo = fmodf(f + 720.0f, 360.0f);
    s.haveTurnTo = true;
  } else if (key == "gps") {
    s.gps = value == "fix" ? GpsMode::Fix : value == "nofix" ? GpsMode::NoFix
            : value == "silent" ? GpsMode::Silent : GpsMode::Real;
  } else if (key == "lat") {
    s.lat = value.toDouble();
  } else if (key == "lon") {
    s.lon = value.toDouble();
  } else if (key == "speed") {
    s.speedMs = f;
  } else if (key == "track") {
    s.trackDeg = f;
  } else if (key == "gpsAltErr") {
    s.gpsAltErrM = f;
    s.gpsAltAbs = false;
  } else if (key == "gpsAlt") {
    s.gpsAltM = f;
    s.gpsAltAbs = true;
  } else if (key == "sats") {
    s.sats = (int)f;
  } else if (key == "hdop") {
    s.hdop = f;
  } else if (key == "batt") {
    s.batt = value != "off" && value != "0";
    if (s.batt) s.battV = f;
  } else if (key == "noise") {
    s.noise = value != "off";
    if (s.noise) s.noiseDb = f;
  } else if (key == "hasGps") {
    s.hasGps = on;
  } else if (key == "gpsForget") {
    s.gpsForget = on;
  } else if (key == "baroFault") {
    s.baroFault = value == "dead" ? BaroFault::Dead : value == "range" ? BaroFault::Range
                  : value == "stuck" ? BaroFault::Stuck : value == "noise" ? BaroFault::Noise : BaroFault::None;
  } else if (key == "baroNoise") {
    s.baroNoiseM = f;
  } else if (key == "baroSpike") {
    s.baroSpikeM = f;
  } else if (key == "micMode") {
    s.mic = value == "synth" ? MicMode::Synth : value == "dead" ? MicMode::Dead : MicMode::Real;
  } else if (key == "micHz") {
    s.micHz = f;
  } else if (key == "micAmp") {
    s.micAmp = f;
  } else if (key == "micNoise") {
    s.micNoise = f;
  } else if (key == "hasMic") {
    s.hasMic = on;
  } else if (key == "off") {
    uint32_t epoch = s.baroEpoch + 1;  // always a new value, so the barometer filter restarts
    s = State();
    s.baroEpoch = epoch;
  } else {
    return false;
  }
  LOGI("SIM", "%s=%s", key.c_str(), value.c_str());
  return true;
}

void describe(String& json) {
  char buf[400];
  static const char* kGps[] = {"real", "fix", "nofix", "silent"};
  snprintf(buf, sizeof(buf),
           "{\"baro\":%d,\"alt\":%.2f,\"vz\":%.2f,\"gps\":\"%s\",\"lat\":%.6f,\"lon\":%.6f,\"speed\":%.2f,"
           "\"track\":%.1f,\"sats\":%d,\"hdop\":%.2f,\"batt\":%d,\"battV\":%.3f,\"noise\":%d,\"noiseDb\":%.1f,\"hdg\":%.1f}",
           s.baro, s.altM, s.vzMs, kGps[(int)s.gps], s.lat, s.lon, s.speedMs, s.trackDeg, s.sats, s.hdop,
           s.batt, s.battV, s.noise, s.noiseDb, s.hdg);
  json = buf;
}

}  // namespace sim

// Hardware.h: in the test build the microphone can be switched "off" to test a vario without one
// (the GPS switch is sim::gpsFitted(), used by hw::gpsState() in sensors/Gps.cpp).
namespace hw {
bool hasMic() { return PV_HAS_MIC && sim::s.hasMic; }
}  // namespace hw

#endif  // PV_TEST
