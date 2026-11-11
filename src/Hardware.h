#pragma once

// Which optional hardware this firmware is built for.
//
// The barometer, speaker, battery sense, button, Bluetooth and Wi-Fi are always present. The GPS
// receiver and the microphone are optional, chosen with build flags (see platformio.ini):
//
//   -DPV_HAS_GPS=0   no GPS: vario, height above takeoff, takeoff/landing from the barometer,
//                    Bluetooth vario sentences. No position, speed, wind, landing zone,
//                    approach/emergency guidance or IGC log.
//   -DPV_HAS_SPECTRUM=0  leaves out the microphone spectrum feature (the Spectrum tab on the
//                    page, /api/spectrum and the code in sensors/Mic.cpp that produces it). Noise
//                    level, automatic volume and engine RPM are not affected. Implied by
//                    PV_HAS_MIC=0.
//   -DPV_HAS_MIC=0   no microphone: fixed speaker volume (no background-noise compensation), no
//                    engine RPM, no spectrum page. The battery is then read with ordinary ADC
//                    reads instead of through the microphone's sampler.
//
// The rest of the code asks hw::hasGps() / hw::hasMic() rather than using #if, so there is one
// code path to read and test. In normal builds both are compile-time constants and the unused
// branches are removed by the compiler; the drivers themselves (sensors/Gps.cpp, sensors/Mic.cpp)
// are left out of the build entirely by their own #if.
//
// The test build can also switch a fitted part "off" at run time (/api/sim?hasGps=0), so the
// reduced variants can be tested on a fully fitted vario.
#ifndef PV_HAS_GPS
#define PV_HAS_GPS 1
#endif
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

#if PV_TEST
bool hasGps();  // defined in test/Sim.cpp
bool hasMic();
#else
constexpr bool hasGps() { return PV_HAS_GPS; }
constexpr bool hasMic() { return PV_HAS_MIC; }
#endif

// The microphone spectrum feature is built in and a microphone is (still) in use.
inline bool hasSpectrum() { return PV_HAS_SPECTRUM && hasMic(); }

}  // namespace hw
