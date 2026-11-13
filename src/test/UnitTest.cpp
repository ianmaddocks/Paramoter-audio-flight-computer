#include "test/UnitTest.h"

#if PV_TEST

bool runUnitSuite(const String& name, String* json) {
  UnitReport r;
  if (name == "rpm") {
    rpmUnitSuite(r);
  } else if (name == "geo") {
    geoUnitSuite(r);
  } else if (name == "wind") {
    windUnitSuite(r);
  } else if (name == "circuit") {
    circuitUnitSuite(r);
  } else if (name == "battery") {
    batteryUnitSuite(r);
  } else if (name == "button") {
    buttonUnitSuite(r);
  } else if (name == "audio") {
    audioUnitSuite(r);
  } else if (name == "resume") {
    resumeUnitSuite(r);
  } else if (name == "alerts") {
    alertsUnitSuite(r);
  } else if (name == "status") {
    statusUnitSuite(r);
  } else if (name == "rpmtrack") {
    rpmTrackUnitSuite(r);
  } else if (name == "baro") {
    baroUnitSuite(r);
  } else if (name == "height") {
    heightUnitSuite(r);
  } else if (name == "flight") {
    flightUnitSuite(r);
  } else {
    return false;
  }
  *json = "{\"suite\":\"" + name + "\",\"pass\":" + r.pass + ",\"fail\":" + r.fail + ",\"failures\":[" + r.failures + "]}";
  return true;
}

#endif
