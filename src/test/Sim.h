#pragma once
#include <Arduino.h>

// Simulated sensor inputs for the test build (PV_TEST=1, the "test" PlatformIO environment).
// Controlled over HTTP (/api/sim?key=value...) by tools/hil_test.py. Each sensor is simulated
// only while its switch is on; with everything off the test build behaves like the debug build.
// In every other build these are constant-false stubs, so the hooks compile away.
//
// Keys: baro, alt, vz, to, baroOffset, gps (fix|nofix|silent|real), lat, lon, speed, track,
// gpsAltErr, gpsAlt, sats, hdop, batt, noise, hasGps, hasMic (0 = behave as a vario with no GPS /
// microphone), gpsForget (forget that a GPS was ever seen and start detecting again), micMode
// (real|synth|dead), micHz (engine firing frequency in Hz: rpm x pulses per rev / 60), micAmp (engine
// signal amplitude, ADC counts), micNoise (white noise amplitude, ADC counts), button (single|double|triple|five|hold|press|holdcue: a button event as if the button had decoded it), buttonHeld (1|0: the button is held down, for the factory reset's 2 s hold), gpsBoot (silent|real: kept across restarts: the simulated GPS starts silent after every power-up, so a real GPS module on the bench cannot get a fix before a test has set up its flight), utcBase (seconds added to the simulated GPS clock, which starts at 12:00:00 at power-on and runs with the uptime), rpmGlitch (one analysis window reports this RPM with a high confidence), baroFault (none|dead|range|stuck|noise: no samples /
// impossible pressure / frozen value / random error of +-baroNoise m). The simulated sensor always has its
// own small noise (about 3 cm), as a real one does, baroNoise (metres), baroSpike (adds
// that many metres to the next baroSpikeN samples, default 1), baroSpikeN, baroDropMs (no samples for that many ms, timed by the vario), baroStep (metres added to every
// sample from now on: a persistent step), baroFault nan (pressure is not a number), off (everything back
// to real).
namespace sim {

enum class GpsMode : uint8_t { Real, Fix, NoFix, Silent };
// Real: the ADC; Synth: an engine at a given firing frequency plus noise; Dead: a flat input (no signal at all)
enum class BaroFault : uint8_t { None, Dead, Range, Stuck, Noise, Nan };
enum class MicMode : uint8_t { Real, Synth, Dead };

#if PV_TEST
bool baroOn();
uint32_t baroEpoch();     // changes whenever the simulated altitude jumps (filter must restart)
float baroAltStdM();      // simulated standard-pressure altitude

GpsMode gpsMode();
bool gpsFitted();       // false: behave as a vario with no GPS module
bool takeGpsForget();   // true once after "gpsForget"
// Writes one epoch of NMEA (GGA + RMC) for the simulated fix. Returns the length, 0 if none due.
size_t gpsNmea(uint32_t nowMs, char* out, size_t size);

bool baroAvail();        // false: the sensor gives no samples at all
float baroPressureOverride();  // > 0: this pressure (Pa) is what the sensor reports, whatever the altitude
MicMode micMode();
int32_t micSample();  // one synthetic ADC sample (12-bit counts), called at the ADC rate by the mic task
int takeButton();  // a pending injected button event (a ButtonEvent value), 0 = none
bool buttonHeld();  // buttonHeld=1: the button is taken to be held down (as well as any real press)
bool takeRpmGlitch(float* rpm);  // true once after rpmGlitch=<rpm>: the next analysis window reports that RPM, confidently

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
inline bool gpsFitted() { return true; }
inline bool takeGpsForget() { return false; }
inline size_t gpsNmea(uint32_t, char*, size_t) { return 0; }
inline bool baroAvail() { return true; }
inline float baroPressureOverride() { return 0; }
inline MicMode micMode() { return MicMode::Real; }
inline int32_t micSample() { return 2048; }
inline int takeButton() { return 0; }
inline bool buttonHeld() { return false; }
inline bool takeRpmGlitch(float*) { return false; }
inline bool battOn() { return false; }
inline float battV() { return 0; }
inline bool noiseOn() { return false; }
inline float noiseDb() { return 0; }
inline void update(uint32_t) {}
#endif

}  // namespace sim
