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
  } else {
    return false;
  }
  *json = "{\"suite\":\"" + name + "\",\"pass\":" + r.pass + ",\"fail\":" + r.fail + ",\"failures\":[" + r.failures + "]}";
  return true;
}

#endif
