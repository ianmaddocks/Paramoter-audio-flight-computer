#include "test/UnitTest.h"

#if PV_TEST

bool runUnitSuite(const String& name, String* json) {
  UnitReport r;
  if (false) {
#if PV_UNIT_HAS(1)
  } else if (name == "rpm") {
    rpmUnitSuite(r);
#endif
#if PV_UNIT_HAS(1)
  } else if (name == "geo") {
    geoUnitSuite(r);
#endif
#if PV_UNIT_HAS(2)
  } else if (name == "wind") {
    windUnitSuite(r);
#endif
#if PV_UNIT_HAS(1)
  } else if (name == "circuit") {
    circuitUnitSuite(r);
#endif
#if PV_UNIT_HAS(2)
  } else if (name == "battery") {
    batteryUnitSuite(r);
#endif
#if PV_UNIT_HAS(2)
  } else if (name == "button") {
    buttonUnitSuite(r);
#endif
#if PV_UNIT_HAS(3)
  } else if (name == "audio") {
    audioUnitSuite(r);
#endif
#if PV_UNIT_HAS(2)
  } else if (name == "resume") {
    resumeUnitSuite(r);
#endif
#if PV_UNIT_HAS(2)
  } else if (name == "memguard") {
    memGuardUnitSuite(r);
#endif
#if PV_UNIT_HAS(2)
  } else if (name == "alerts") {
    alertsUnitSuite(r);
#endif
#if PV_UNIT_HAS(1)
  } else if (name == "status") {
    statusUnitSuite(r);
#endif
#if PV_UNIT_HAS(3)
  } else if (name == "rpmtrack") {
    rpmTrackUnitSuite(r);
#endif
#if PV_UNIT_HAS(3)
  } else if (name == "baro") {
    baroUnitSuite(r);
#endif
#if PV_UNIT_HAS(1)
  } else if (name == "moving") {
    movingUnitSuite(r);
#endif
#if PV_UNIT_HAS(1)
  } else if (name == "height") {
    heightUnitSuite(r);
#endif
#if PV_UNIT_HAS(3)
  } else if (name == "flight") {
    flightUnitSuite(r);
#endif
#if PV_UNIT_HAS(3)
  } else if (name == "areas") {
    areasUnitSuite(r);
#endif
  } else {
    return false;
  }
  *json = "{\"suite\":\"" + name + "\",\"pass\":" + r.pass + ",\"fail\":" + r.fail + ",\"failures\":[" + r.failures + "]}";
  return true;
}

#endif
