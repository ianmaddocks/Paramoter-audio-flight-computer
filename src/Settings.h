#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// All values are stored in SI units (m, m/s, s); the web UI converts for display.

enum HeightUnit : int32_t { HU_FEET = 0, HU_METRES = 1 };
enum DistUnit : int32_t { DU_METRES = 0, DU_FEET = 1 };
enum SpeedUnit : int32_t { SU_KMH = 0, SU_MPH = 1, SU_KNOTS = 2 };
enum CircuitSide : int32_t { CS_PILOT = 0, CS_LEFT = 1, CS_RIGHT = 2 };
enum LzMode : int32_t { LZ_TAKEOFF = 0, LZ_MANUAL = 1 };
enum GpsModeSetting : int32_t { GPS_AUTO = 0, GPS_ALWAYS = 1, GPS_NOT_FITTED = 2 };

struct Settings {
  int32_t heightUnit = HU_FEET;
  int32_t distUnit = DU_METRES;
  int32_t speedUnit = SU_KMH;

  int32_t speechVolume = 5;
  int32_t varioVolume = 5;
  int32_t volumeRampPctS = 40;  // fastest change of the applied volume, % per second
  bool autoVolume = true;
  float noiseQuietDb = -52.0f;  // dBFS at the mic: at or below this the set volumes are used
  float noiseLoudDb = -30.0f;   // at or above this both volumes go to 100 %
  int32_t noiseReleaseS = 1;

  float rpmPulsesPerRev = 1.0f;  // single-cylinder two-stroke fires once per revolution
  int32_t rpmMin = 1200;
  int32_t rpmMax = 10000;
  float rpmMinConfidence = 3.0f;
  bool spectrumEnabled = false;  // show the Spectrum tab (needs a build with the spectrum feature)

  bool varioEnabled = true;
  bool varioOnlyInFlight = true;
  bool varioClimbBeeps = true;
  bool varioSinkAlarm = true;
  float climbThreshold = 0.3f;
  float sinkThreshold = -2.0f;
  float varioBaseHz = 700.0f;
  float varioHzPerMs = 100.0f;
  float varioAccelNoise = 1.0f;
  int32_t baroStuckS = 5;    // an identical barometer reading for this long means a frozen sensor (0 = not checked)
  int32_t varioSettleS = 5;  // vario held at 0 after power-on until this long without movement

  // Climbing: 50, 100, 250, 500, 750, 1000 ft.
  char boundariesUp[128] = "15.24,30.48,76.2,152.4,228.6,304.8";
  // Descending (key kept as "boundaries" so values saved before the split carry over).
  char boundaries[128] = "15.24,30.48,45.72,76.2,106.68,152.4,228.6,304.8";
  float boundaryStepM = 152.4f;
  float boundaryHystM = 3.0f;
  bool boundaryClimbing = true;
  bool boundaryDescending = true;
  int32_t heightIntervalS = 60;
  int32_t flightTimeIntervalMin = 5;

  float takeoffSpeedMs = 15.0f / 3.6f;
  int32_t takeoffTimeS = 5;
  float takeoffGainM = 3.048f;  // 10 ft
  float baroTakeoffGainM = 15.0f;   // height gain that counts as takeoff with no usable GPS
  float groundHoldVarioMs = 0.5f;   // ground level stops following the baro above this rate
  float landingSpeedMs = 5.0f / 3.6f;
  int32_t landingTimeS = 30;
  float landingNoSpeedAglM = 15.0f;  // without a trusted ground speed, landed only this close to the ground

  int32_t glideShortS = 1;   // glide ratio tile: main value averaged over this many seconds
  int32_t glideLongS = 60;   // ... and the smaller value over this many
  float glideRatio = 5.0f;
  float trimAirspeedMs = 38.0f / 3.6f;

  int32_t lzMode = LZ_TAKEOFF;
  double lzLat = 0.0;
  double lzLon = 0.0;
  float lzElevM = 0.0f;

  int32_t circuitSide = CS_PILOT;
  float circuitHeightM = 121.92f;
  float upwindLegM = 400.0f;
  float crosswindLegM = 400.0f;
  float downwindLegM = 400.0f;
  float finalHeightM = 50.0f;
  float minFinalM = 100.0f;
  float arrivalRadiusM = 80.0f;
  float turnLeadS = 3.0f;
  float offHeadingDeg = 30.0f;
  int32_t guidanceUpdateS = 20;

  int32_t emergencyUpdateS = 30;
  float onCourseDeg = 15.0f;

  bool windManual = false;
  float windFromDeg = 0.0f;
  float windSpeedMs = 0.0f;

  int32_t gpsMode = GPS_AUTO;  // how the vario decides whether a GPS module is fitted (see Hardware.h)
  int32_t gpsRateHz = 5;
  // u-blox time-pulse output, which drives the positioning LED on most M10 breakouts.
  bool gpsLed = false;
  int32_t readyMinSats = 6;
  float readyMaxHdop = 2.0f;

  char bleName[24] = "ParaVario";
  int32_t bleRateHz = 5;
  bool bleGps = true;
  int32_t bleGpsRateHz = 1;

  char wifiSsid[33] = "ParaVario";
  char wifiPass[65] = "";
  char serverSsid[33] = "";
  char serverPass[65] = "";
  // While the vario searches for the server network its own network is unreliable (one radio,
  // channel hopping), so give up after this long without a connection. 0 = keep trying.
  int32_t serverGiveUpS = 30;

  float battDivider = 2.0f;
  float battCal = 0.999f;
  float battLowV = 3.5f;
  float battCriticalV = 3.3f;  // under this: an urgent call every minute
  float battFullV = 4.09f;
  bool battLearn = true;
  int32_t battRuntimeMin = 360;

  bool igcEnabled = true;
  int32_t igcPreS = 30;
  bool igcQnhAlt = true;
  char pilotName[32] = "";
  char gliderType[32] = "Paramotor";
};

extern Settings g_cfg;

namespace settings {
void load();
bool save();
void resetDefaults();
void toJson(JsonObject obj);
// All settings as JSON text in out (the working copy is freed before this returns).
void toJsonString(String& out);
// Applies known keys, clamped to their limits. Returns true if a changed key needs a restart.
bool fromJson(JsonObjectConst obj);
// Names this unit advertises. A name left at the default "ParaVario" gets "-" and the last two
// bytes of the unit's MAC address added (for example "ParaVario-A1B2"), so several units near
// each other are told apart. A name the pilot has changed is used exactly as typed.
const char* bleName();
const char* apSsid();
// The settings table for the web page, one entry at a time so it can be sent in small pieces.
int schemaCount();
// Fills `o` with the description of setting `index` for the settings page. Returns false (and
// leaves `o` empty) for a setting that does not apply to the hardware fitted.
bool schemaEntry(int index, JsonObject o);
}  // namespace settings
