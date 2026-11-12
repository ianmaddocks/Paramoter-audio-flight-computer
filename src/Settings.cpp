#include "Settings.h"

#include "Hardware.h"
#include "Log.h"

#include <Preferences.h>

#include <algorithm>
#include <vector>

Settings g_cfg;

namespace {

enum class FT : uint8_t { Bool, Int, Float, Double, Str, Enum, List };

// unit: how the web UI converts/labels the value ("height", "dist", "speed" are user-unit
// conversions; anything else is a literal suffix).
struct Field {
  const char* key;
  const char* group;
  const char* label;
  FT type;
  const char* unit;
  void* ptr;
  float min;
  float max;
  uint16_t size;
  const char* opts;
  bool restart;
};

#define B(k, g, l) {#k, g, l, FT::Bool, "", &g_cfg.k, 0, 1, 0, nullptr, false}
#define BR(k, g, l) {#k, g, l, FT::Bool, "", &g_cfg.k, 0, 1, 0, nullptr, true}
#define I(k, g, l, u, mn, mx) {#k, g, l, FT::Int, u, &g_cfg.k, mn, mx, 0, nullptr, false}
#define IR(k, g, l, u, mn, mx) {#k, g, l, FT::Int, u, &g_cfg.k, mn, mx, 0, nullptr, true}
#define FL(k, g, l, u, mn, mx) {#k, g, l, FT::Float, u, &g_cfg.k, mn, mx, 0, nullptr, false}
#define D(k, g, l, mn, mx) {#k, g, l, FT::Double, "", &g_cfg.k, mn, mx, 0, nullptr, false}
#define S(k, g, l) {#k, g, l, FT::Str, "", &g_cfg.k, 0, 0, sizeof(g_cfg.k), nullptr, false}
#define SR(k, g, l) {#k, g, l, FT::Str, "", &g_cfg.k, 0, 0, sizeof(g_cfg.k), nullptr, true}
#define E(k, g, l, o) {#k, g, l, FT::Enum, "", &g_cfg.k, 0, 0, 0, o, false}

const Field kFields[] = {
    E(heightUnit, "Units", "Height", "Feet|Metres"),
    E(distUnit, "Units", "Distance", "Metres / km|Feet / miles"),
    E(speedUnit, "Units", "Speed", "km/h|mph|knots"),

    I(speechVolume, "Audio", "Speech volume", "%", 0, 100),
    I(varioVolume, "Audio", "Vario volume", "%", 0, 100),
    I(volumeRampPctS, "Audio", "Fastest volume change (lower = gentler)", "%/s", 5, 500),
    B(autoVolume, "Audio", "Raise volume with background noise"),
    FL(noiseQuietDb, "Audio", "Noise for set volume (see Background noise tile)", "dBFS", -90.0f, 0.0f),
    FL(noiseLoudDb, "Audio", "Noise for full volume", "dBFS", -90.0f, 0.0f),
    I(noiseReleaseS, "Audio", "Volume fall time after noise drops", "s", 1, 60),

    FL(rpmPulsesPerRev, "Engine RPM", "Sound pulses per revolution (2-stroke = 1, 4-stroke = 0.5)", "", 0.25f, 8.0f),
    I(rpmMin, "Engine RPM", "Lowest RPM", "rpm", 300, 20000),
    I(rpmMax, "Engine RPM", "Highest RPM", "rpm", 1000, 30000),
    FL(rpmMinConfidence, "Engine RPM", "Minimum confidence (see Mic tile)", "", 1.0f, 20.0f),
    B(spectrumEnabled, "Engine RPM", "Show the Spectrum tab"),

    B(varioEnabled, "Vario", "Vario beeps enabled"),
    B(varioOnlyInFlight, "Vario", "Only when flying"),
    B(varioClimbBeeps, "Vario", "Climb beeps"),
    B(varioSinkAlarm, "Vario", "Sink alarm"),
    FL(climbThreshold, "Vario", "Climb threshold", "m/s", 0.0f, 5.0f),
    FL(sinkThreshold, "Vario", "Sink alarm threshold", "m/s", -10.0f, 0.0f),
    FL(varioBaseHz, "Vario", "Base tone", "Hz", 200.0f, 2000.0f),
    FL(varioHzPerMs, "Vario", "Tone rise per m/s", "Hz", 0.0f, 400.0f),
    FL(varioAccelNoise, "Vario", "Responsiveness (higher = faster, noisier)", "", 0.05f, 10.0f),
    I(varioSettleS, "Vario", "Settle time at power-on", "s", 0, 30),

    // Each "Announce" switch sits above its list; the web page greys the list out while it is off
    // (gate table in WebPage.h).
    B(boundaryClimbing, "Height calls", "Announce when climbing"),
    {"boundariesUp", "Height calls", "Boundaries when climbing", FT::List, "height", &g_cfg.boundariesUp, 0, 0,
     sizeof(g_cfg.boundariesUp), nullptr, false},
    B(boundaryDescending, "Height calls", "Announce when descending"),
    {"boundaries", "Height calls", "Boundaries when descending", FT::List, "height", &g_cfg.boundaries, 0, 0,
     sizeof(g_cfg.boundaries), nullptr, false},
    FL(boundaryStepM, "Height calls", "Repeat every (above last boundary)", "height", 0.0f, 2000.0f),
    FL(boundaryHystM, "Height calls", "Hysteresis", "height", 0.0f, 50.0f),
    I(heightIntervalS, "Height calls", "Periodic height every (0 = off)", "s", 0, 3600),
    I(flightTimeIntervalMin, "Height calls", "Flight time every (0 = off)", "min", 0, 120),

    FL(takeoffSpeedMs, "Takeoff & landing", "Takeoff ground speed", "speed", 1.0f, 30.0f),
    I(takeoffTimeS, "Takeoff & landing", "Takeoff speed held for", "s", 1, 60),
    FL(takeoffGainM, "Takeoff & landing", "Takeoff height gain", "height", 1.0f, 100.0f),
    FL(baroTakeoffGainM, "Takeoff & landing", "Takeoff height gain without GPS", "height", 3.0f, 200.0f),
    FL(groundHoldVarioMs, "Takeoff & landing", "Ground level held when climbing or sinking faster than", "m/s",
       0.1f, 3.0f),
    FL(landingSpeedMs, "Takeoff & landing", "Landed by ground speed", "speed", 0.2f, 10.0f),
    I(landingTimeS, "Takeoff & landing", "Landed after", "s", 5, 300),

    I(glideShortS, "Glide", "Glide ratio tile: average over", "s", 1, 10),
    I(glideLongS, "Glide", "Glide ratio tile: second average over", "s", 10, 120),
    FL(glideRatio, "Glide", "Glide ratio (engine off)", ":1", 2.0f, 12.0f),
    FL(trimAirspeedMs, "Glide", "Trim airspeed (engine off)", "speed", 5.0f, 25.0f),

    E(lzMode, "Landing zone", "Landing zone", "Takeoff point|Manual"),
    D(lzLat, "Landing zone", "Latitude", -90.0f, 90.0f),
    D(lzLon, "Landing zone", "Longitude", -180.0f, 180.0f),
    FL(lzElevM, "Landing zone", "Elevation (MSL)", "height", -500.0f, 6000.0f),

    E(circuitSide, "Landing approach", "Circuit direction", "Pilot's choice|Left hand|Right hand"),
    FL(circuitHeightM, "Landing approach", "Circuit height", "height", 30.0f, 500.0f),
    FL(upwindLegM, "Landing approach", "Upwind leg", "dist", 50.0f, 2000.0f),
    FL(crosswindLegM, "Landing approach", "Crosswind leg", "dist", 50.0f, 2000.0f),
    FL(downwindLegM, "Landing approach", "Downwind leg (target)", "dist", 50.0f, 2000.0f),
    FL(finalHeightM, "Landing approach", "Start final at", "height", 10.0f, 200.0f),
    FL(minFinalM, "Landing approach", "Minimum final length", "dist", 30.0f, 1000.0f),
    FL(arrivalRadiusM, "Landing approach", "Circuit start capture radius", "dist", 20.0f, 500.0f),
    FL(turnLeadS, "Landing approach", "Call turns early by", "s", 0.0f, 15.0f),
    FL(offHeadingDeg, "Landing approach", "Off-heading warning", "°", 10.0f, 90.0f),
    I(guidanceUpdateS, "Landing approach", "Guidance update every", "s", 5, 120),

    I(emergencyUpdateS, "Emergency", "Distance update every", "s", 5, 300),
    FL(onCourseDeg, "Emergency", "On-course tolerance", "°", 5.0f, 45.0f),

    B(windManual, "Wind", "Use manual wind (overrides estimate)"),
    FL(windFromDeg, "Wind", "Wind from", "°", 0.0f, 359.9f),
    FL(windSpeedMs, "Wind", "Wind speed", "speed", 0.0f, 25.0f),

    E(gpsMode, "GPS", "GPS module", "Auto-detect|Always present|Not fitted"),
    IR(gpsRateHz, "GPS", "Update rate", "Hz", 1, 10),
    BR(gpsLed, "GPS", "GPS module LED (time-pulse output)"),
    I(readyMinSats, "GPS", "Ready to fly: minimum satellites", "", 4, 20),
    FL(readyMaxHdop, "GPS", "Ready to fly: maximum HDOP", "", 0.5f, 10.0f),

    SR(bleName, "Bluetooth", "Device name (ParaVario = unit code added, e.g. ParaVario-A1B2)"),
    I(bleRateHz, "Bluetooth", "Vario sentences ($LK8EX1, $LXWP0) rate", "Hz", 1, 10),
    B(bleGps, "Bluetooth", "Send pseudo-GPS ($GPGGA, $GPRMC)"),
    I(bleGpsRateHz, "Bluetooth", "Pseudo-GPS rate", "Hz", 1, 10),

    SR(wifiSsid, "Wi-Fi", "Network name (ParaVario = unit code added, e.g. ParaVario-A1B2)"),
    SR(wifiPass, "Wi-Fi", "Password (8+ chars, blank = open)"),
    SR(serverSsid, "Wi-Fi", "Server network name (blank = off)"),
    SR(serverPass, "Wi-Fi", "Server network password"),
    I(serverGiveUpS, "Wi-Fi", "Stop searching if server network not found after (0 = keep trying)", "s", 0, 600),

    FL(battDivider, "Battery", "Divider ratio", "", 1.0f, 10.0f),
    FL(battCal, "Battery", "Calibration factor", "", 0.8f, 1.2f),
    FL(battLowV, "Battery", "Low battery warning", "V", 3.0f, 4.0f),
    FL(battFullV, "Battery", "Voltage shown as 100%", "V", 4.02f, 4.25f),
    B(battLearn, "Battery", "Learn discharge to estimate minutes left and refine %"),
    I(battRuntimeMin, "Battery", "Runtime on a full battery, used until learned", "min", 30, 1440),

    B(igcEnabled, "Logging", "Record IGC flight logs"),
    B(igcQnhAlt, "Logging", "IGC pressure altitude referenced to GPS (off = true 1013 hPa, can be negative)"),
    I(igcPreS, "Logging", "Keep this much track before takeoff (RAM only, 0 = off)", "s", 0, 120),
    S(pilotName, "Logging", "Pilot name"),
    S(gliderType, "Logging", "Wing / motor"),
};

#undef B
#undef I
#undef IR
#undef FL
#undef D
#undef S
#undef SR
#undef E

// Settings for hardware this vario does not have are left out of the settings page (they keep
// their stored values, and nothing reads them).
bool listed(const char* const* list, size_t n, const char* name) {
  for (size_t i = 0; i < n; ++i) {
    if (strcmp(list[i], name) == 0) return true;
  }
  return false;
}

bool available(const Field& f) {
  if (strcmp(f.key, "gpsMode") == 0) return true;  // the one GPS setting to show even with no GPS
  static const char* const kGpsGroups[] = {"Glide", "Landing zone", "Landing approach", "Emergency",
                                           "Wind",  "GPS",          "Logging"};
  static const char* const kGpsKeys[] = {"distUnit",       "speedUnit", "takeoffSpeedMs", "takeoffTimeS",
                                         "takeoffGainM",   "landingSpeedMs", "bleGps",    "bleGpsRateHz"};
  static const char* const kMicGroups[] = {"Engine RPM"};
  static const char* const kMicKeys[] = {"autoVolume", "noiseQuietDb", "noiseLoudDb", "noiseReleaseS"};
  if (!hw::hasGps() && (listed(kGpsGroups, sizeof(kGpsGroups) / sizeof(kGpsGroups[0]), f.group) ||
                        listed(kGpsKeys, sizeof(kGpsKeys) / sizeof(kGpsKeys[0]), f.key))) {
    return false;
  }
  if (!hw::hasMic() && (listed(kMicGroups, sizeof(kMicGroups) / sizeof(kMicGroups[0]), f.group) ||
                        listed(kMicKeys, sizeof(kMicKeys) / sizeof(kMicKeys[0]), f.key))) {
    return false;
  }
  return true;
}

int enumCount(const char* opts) {
  int n = 1;
  for (const char* p = opts; *p; ++p) n += (*p == '|');
  return n;
}

// Parses a comma list, drops invalid entries, sorts ascending, rewrites in place.
void normaliseList(char* buf, size_t size) {
  std::vector<float> v;
  const char* p = buf;
  while (*p) {
    char* end;
    float f = strtof(p, &end);
    if (end == p) {
      ++p;
      continue;
    }
    if (f > 0.0f && f < 10000.0f) v.push_back(f);
    p = end;
  }
  std::sort(v.begin(), v.end());
  v.erase(std::unique(v.begin(), v.end()), v.end());
  String out;
  for (size_t i = 0; i < v.size(); ++i) {
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%s%.2f", i ? "," : "", v[i]);
    if (out.length() + strlen(tmp) >= size) break;
    out += tmp;
  }
  strlcpy(buf, out.c_str(), size);
}

}  // namespace

namespace hw {
bool hasSpectrum() { return PV_HAS_SPECTRUM && g_cfg.spectrumEnabled && hasMic(); }
}  // namespace hw

namespace settings {

void toJson(JsonObject obj) {
  for (const Field& f : kFields) {
    switch (f.type) {
      case FT::Bool: obj[f.key] = *static_cast<bool*>(f.ptr); break;
      case FT::Int:
      case FT::Enum: obj[f.key] = *static_cast<int32_t*>(f.ptr); break;
      case FT::Float: obj[f.key] = *static_cast<float*>(f.ptr); break;
      case FT::Double: obj[f.key] = *static_cast<double*>(f.ptr); break;
      case FT::Str:
      case FT::List: obj[f.key] = static_cast<const char*>(f.ptr); break;
    }
  }
}

bool fromJson(JsonObjectConst obj) {
  bool restart = false;
  for (const Field& f : kFields) {
    JsonVariantConst v = obj[f.key];
    if (v.isNull()) continue;
    bool changed = false;
    switch (f.type) {
      case FT::Bool: {
        bool* p = static_cast<bool*>(f.ptr);
        bool nv = v.as<bool>();
        changed = nv != *p;
        *p = nv;
        break;
      }
      case FT::Int:
      case FT::Enum: {
        int32_t* p = static_cast<int32_t*>(f.ptr);
        int32_t nv = v.as<int32_t>();
        int32_t lo = f.type == FT::Enum ? 0 : (int32_t)f.min;
        int32_t hi = f.type == FT::Enum ? enumCount(f.opts) - 1 : (int32_t)f.max;
        nv = constrain(nv, lo, hi);
        changed = nv != *p;
        *p = nv;
        break;
      }
      case FT::Float: {
        float* p = static_cast<float*>(f.ptr);
        float nv = v.as<float>();
        if (isnan(nv)) break;
        nv = constrain(nv, f.min, f.max);
        changed = nv != *p;
        *p = nv;
        break;
      }
      case FT::Double: {
        double* p = static_cast<double*>(f.ptr);
        double nv = v.as<double>();
        if (isnan(nv)) break;
        nv = constrain(nv, (double)f.min, (double)f.max);
        changed = nv != *p;
        *p = nv;
        break;
      }
      case FT::Str:
      case FT::List: {
        char* p = static_cast<char*>(f.ptr);
        const char* nv = v.as<const char*>();
        if (!nv) break;
        char tmp[128];
        strlcpy(tmp, nv, std::min<size_t>(sizeof(tmp), f.size));
        if (f.type == FT::List) normaliseList(tmp, f.size);
        changed = strcmp(tmp, p) != 0;
        strlcpy(p, tmp, f.size);
        break;
      }
    }
    if (changed && f.restart) restart = true;
  }
  return restart;
}

namespace {
constexpr char kDefaultName[] = "ParaVario";

// Last two bytes of the factory MAC address as four hex digits, fixed for the life of the unit.
const char* unitSuffix() {
  static char suffix[5] = "";
  if (!suffix[0]) {
    const uint64_t mac = ESP.getEfuseMac();  // byte 0 of the address in the low bits
    snprintf(suffix, sizeof(suffix), "%02X%02X", (unsigned)((mac >> 32) & 0xFF), (unsigned)((mac >> 40) & 0xFF));
  }
  return suffix;
}

// `configured` as is, unless it is the default name (or empty): then the default plus the suffix.
const char* uniqueName(const char* configured, char* out, size_t size) {
  if (configured[0] && strcmp(configured, kDefaultName) != 0) {
    strlcpy(out, configured, size);
  } else {
    snprintf(out, size, "%s-%s", kDefaultName, unitSuffix());
  }
  return out;
}
}  // namespace

const char* bleName() {
  static char name[sizeof(g_cfg.bleName)];
  return uniqueName(g_cfg.bleName, name, sizeof(name));
}

const char* apSsid() {
  static char name[sizeof(g_cfg.wifiSsid)];
  return uniqueName(g_cfg.wifiSsid, name, sizeof(name));
}

int schemaCount() { return sizeof(kFields) / sizeof(kFields[0]); }

bool schemaEntry(int index, JsonObject o) {
  static const char* kTypeNames[] = {"bool", "int", "float", "double", "str", "enum", "list"};
  const Field& f = kFields[index];
  if (!available(f)) return false;
  {
    o["k"] = f.key;
    o["g"] = f.group;
    o["l"] = f.label;
    o["t"] = kTypeNames[(int)f.type];
    if (f.unit && *f.unit) o["u"] = f.unit;
    if (f.type == FT::Int || f.type == FT::Float || f.type == FT::Double) {
      o["min"] = f.min;
      o["max"] = f.max;
    }
    if (f.opts) o["o"] = f.opts;
    if (f.size) o["n"] = f.size - 1;
    if (f.restart) o["r"] = true;
  }
  return true;
}

void load() {
  Preferences prefs;
  // Read-write open creates the namespace on a fresh device instead of logging NOT_FOUND.
  if (!prefs.begin("paravario", false)) return;
  String s = prefs.isKey("cfg") ? prefs.getString("cfg", "") : String();
  prefs.end();
  if (s.isEmpty()) return;
  JsonDocument doc;
  if (deserializeJson(doc, s) != DeserializationError::Ok) {
    LOGW("CFG", "Stored settings unreadable, using defaults");
    return;
  }
  JsonObject root = doc.as<JsonObject>();
  if (root["serverSsid"].isNull() && !root["homeSsid"].isNull()) {
    root["serverSsid"] = root["homeSsid"];
  }
  if (root["serverPass"].isNull() && !root["homePass"].isNull()) {
    root["serverPass"] = root["homePass"];
  }
  fromJson(doc.as<JsonObjectConst>());
}

void toJsonString(String& out) {
  JsonDocument doc;
  toJson(doc.to<JsonObject>());
  out = "";
  out.reserve(measureJson(doc) + 1);  // one allocation of the right size
  serializeJson(doc, out);
}

bool save() {
  String s;
  toJsonString(s);
  Preferences prefs;
  if (!prefs.begin("paravario", false)) return false;
  bool ok = prefs.putString("cfg", s) == s.length();
  prefs.end();
  return ok;
}

void resetDefaults() {
  g_cfg = Settings();
  save();
}

}  // namespace settings
