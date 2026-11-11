#pragma once
#include <stdint.h>

// Which hardware this vario has.
//
// The barometer, speaker, microphone, battery sense, button, Bluetooth and Wi-Fi are always
// there. The one part that differs between varios is the GPS module: one firmware serves both,
// and finds out at start-up whether a GPS is fitted (sensors/Gps.cpp). Everything that depends on
// it asks hw::hasGps(), so the page, the guidance and the settings follow what was found:
//
//   GPS present   position, speed, wind, landing zone, approach and emergency guidance, IGC log
//   GPS absent    vario, height above takeoff, takeoff and landing from the barometer, Bluetooth
//                 vario sentences; the GPS tiles, buttons and settings are hidden
//
// How it is found: the vario listens on the GPS serial port for valid NMEA sentences. A module
// sends them from power-on, so the GPS is "present" as soon as some arrive, and "absent" if none
// have come within a few seconds of power-on. It keeps listening, so a module that starts late is
// picked up. A GPS that has been seen once is remembered, so one that later goes quiet shows
// "GPS not responding" instead of vanishing. The setting "GPS module" (Settings > Advanced settings
// > GPS) can force it: Always present, or Not fitted.
//
// Build flags (see platformio.ini) for the microphone, which every current vario has:
//
//   -DPV_HAS_MIC=0       no microphone: fixed speaker volume (no background-noise compensation),
//                        no engine RPM, no spectrum page. The battery is then read with ordinary
//                        ADC reads instead of through the microphone's sampler.
//   -DPV_HAS_SPECTRUM=0  leaves out the microphone spectrum feature (the Spectrum tab on the page,
//                        /api/spectrum and the code in sensors/Mic.cpp that produces it). Noise
//                        level, automatic volume and engine RPM are not affected.
//
// The test build can switch parts "off" at run time (/api/sim?hasGps=0, hasMic=0), so the
// reduced configurations can be tested on a fully fitted vario.
#ifndef PV_HAS_MIC
#define PV_HAS_MIC 1
#endif
#ifndef PV_HAS_SPECTRUM
#define PV_HAS_SPECTRUM 1
#endif
#if !PV_HAS_MIC && PV_HAS_SPECTRUM  // the spectrum is made from the microphone
#undef PV_HAS_SPECTRUM
#define PV_HAS_SPECTRUM 0
#endif

namespace hw {

// What is known about the GPS module. Unknown only for the first seconds after power-on of a vario
// that has never seen a GPS, until the first valid sentence arrives or the detection time is up.
enum class GpsState : uint8_t { Unknown, Present, Absent };

GpsState gpsState();  // sensors/Gps.cpp
inline bool hasGps() { return gpsState() == GpsState::Present; }

#if PV_TEST
bool hasMic();  // defined in test/Sim.cpp
#else
constexpr bool hasMic() { return PV_HAS_MIC; }
#endif

// The microphone spectrum feature is built in and a microphone is (still) in use.
inline bool hasSpectrum() { return PV_HAS_SPECTRUM && hasMic(); }

}  // namespace hw
