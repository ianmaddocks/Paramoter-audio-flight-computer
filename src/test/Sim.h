#pragma once
#include <Arduino.h>

// Simulated sensor inputs for the test build (PV_TEST=1, the "test" PlatformIO environment).
// Controlled over HTTP (/api/sim?key=value...) by tools/hil_test.py. Each sensor is simulated
// only while its switch is on; with everything off the test build behaves like the debug build.
// In every other build these are constant-false stubs, so the hooks compile away.
namespace sim {

enum class GpsMode : uint8_t { Real, Fix, NoFix, Silent };

#if PV_TEST
bool baroOn();
uint32_t baroEpoch();     // changes whenever the simulated altitude jumps (filter must restart)
float baroAltStdM();      // simulated standard-pressure altitude

GpsMode gpsMode();
// Writes one epoch of NMEA (GGA + RMC) for the simulated fix. Returns the length, 0 if none due.
size_t gpsNmea(uint32_t nowMs, char* out, size_t size);

bool battOn();
float battV();

bool noiseOn();
float noiseDb();

void update(uint32_t nowMs);                       // integrates the simulated flight
bool set(const String& key, const String& value);  // false = unknown key
void describe(String& json);                       // current state as a JSON object
#else
inline bool baroOn() { return false; }
inline uint32_t baroEpoch() { return 0; }
inline float baroAltStdM() { return 0; }
inline GpsMode gpsMode() { return GpsMode::Real; }
inline size_t gpsNmea(uint32_t, char*, size_t) { return 0; }
inline bool battOn() { return false; }
inline float battV() { return 0; }
inline bool noiseOn() { return false; }
inline float noiseDb() { return 0; }
inline void update(uint32_t) {}
#endif

}  // namespace sim
