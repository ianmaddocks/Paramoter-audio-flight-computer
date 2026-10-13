#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// All values are stored in SI units (m, m/s, s); the web UI converts for display.

enum HeightUnit : int32_t { HU_FEET = 0, HU_METRES = 1 };
enum DistUnit : int32_t { DU_METRES = 0, DU_FEET = 1 };
enum SpeedUnit : int32_t { SU_KMH = 0, SU_MPH = 1, SU_KNOTS = 2 };
enum CircuitSide : int32_t { CS_PILOT = 0, CS_LEFT = 1, CS_RIGHT = 2 };
enum LzMode : int32_t { LZ_TAKEOFF = 0, LZ_MANUAL = 1 };

struct Settings {
  int32_t heightUnit = HU_FEET;
  int32_t distUnit = DU_METRES;
  int32_t speedUnit = SU_KMH;

  int32_t speechVolume = 90;
  int32_t varioVolume = 60;

  bool varioEnabled = true;
  bool varioOnlyInFlight = true;
  bool varioClimbBeeps = true;
  bool varioSinkAlarm = true;
  float climbThreshold = 0.3f;
  float sinkThreshold = -2.0f;
  float varioBaseHz = 700.0f;
  float varioHzPerMs = 100.0f;
  float varioAccelNoise = 1.0f;

  bool boundaryEnabled = true;
  char boundaries[128] = "15.24,30.48,45.72,76.2,106.68,152.4,228.6,304.8";
  float boundaryStepM = 152.4f;
  float boundaryHystM = 3.0f;
  bool boundaryClimbing = true;
  bool boundaryDescending = true;
  int32_t heightIntervalS = 60;
  int32_t flightTimeIntervalMin = 5;

  float takeoffSpeedMs = 15.0f / 3.6f;
  int32_t takeoffTimeS = 5;
  float takeoffGainM = 10.0f;
  float landingSpeedMs = 5.0f / 3.6f;
  int32_t landingTimeS = 30;
  float landingMaxAglM = 30.0f;

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

  int32_t gpsRateHz = 5;
  int32_t readyMinSats = 6;
  float readyMaxHdop = 2.0f;

  char bleName[24] = "ParaVario";
  int32_t bleRateHz = 5;
  bool bleGps = true;
  int32_t bleGpsRateHz = 1;

  char wifiSsid[33] = "ParaVario";
  char wifiPass[65] = "paramotor";

  float battDivider = 2.0f;
  float battCal = 1.0f;
  float battLowV = 3.5f;

  bool igcEnabled = true;
  char pilotName[32] = "";
  char gliderType[32] = "Paramotor";
};

extern Settings g_cfg;

namespace settings {
void load();
bool save();
void resetDefaults();
void toJson(JsonObject obj);
// Applies known keys, clamped to their limits. Returns true if a changed key needs a restart.
bool fromJson(JsonObjectConst obj);
void schemaJson(JsonArray arr);
}  // namespace settings
