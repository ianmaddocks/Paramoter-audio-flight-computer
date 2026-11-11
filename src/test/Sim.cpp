#include "test/Sim.h"

#if PV_TEST

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
float baroAltStdM() { return s.altM - s.baroOffsetM; }
GpsMode gpsMode() { return s.gps; }
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
  char buf[320];
  static const char* kGps[] = {"real", "fix", "nofix", "silent"};
  snprintf(buf, sizeof(buf),
           "{\"baro\":%d,\"alt\":%.2f,\"vz\":%.2f,\"gps\":\"%s\",\"lat\":%.6f,\"lon\":%.6f,\"speed\":%.2f,"
           "\"track\":%.1f,\"sats\":%d,\"hdop\":%.2f,\"batt\":%d,\"battV\":%.3f,\"noise\":%d,\"noiseDb\":%.1f}",
           s.baro, s.altM, s.vzMs, kGps[(int)s.gps], s.lat, s.lon, s.speedMs, s.trackDeg, s.sats, s.hdop,
           s.batt, s.battV, s.noise, s.noiseDb);
  json = buf;
}

}  // namespace sim

#endif  // PV_TEST
