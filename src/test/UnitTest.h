#pragma once

#if PV_TEST
#include <Arduino.h>
#include <stdarg.h>

// A tiny unit-test harness for the test build: suites of production functions run on the vario itself
// (there is no host compiler for this project) when /api/sim?unit=<suite> is requested. The reply is
// JSON {"suite":..,"pass":n,"fail":m,"failures":[..]} so the HIL runner can assert on it. Suites use
// their own inputs, never the vario's settings or sensors.
struct UnitReport {
  int pass = 0, fail = 0;
  String failures;  // first few failure descriptions, as JSON array items

  void check(bool ok, const char* fmt, ...) {
    if (ok) {
      ++pass;
      return;
    }
    ++fail;
    if (fail > 8) return;
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    for (char* c = buf; *c; ++c)
      if (*c == '"' || *c == '\\') *c = '\'';
    if (failures.length()) failures += ",";
    failures += "\"";
    failures += buf;
    failures += "\"";
  }
  // |got - want| <= tol
  void near(float got, float want, float tol, const char* what) {
    check(fabsf(got - want) <= tol, "%s: got %.3f want %.3f +-%.3f", what, got, want, tol);
  }
};

// Runs the named suite; false if there is no such suite. *json receives the report.
bool runUnitSuite(const String& name, String* json);

// Suites (one per file)
void rpmUnitSuite(UnitReport& r);
void geoUnitSuite(UnitReport& r);
void windUnitSuite(UnitReport& r);
void circuitUnitSuite(UnitReport& r);
void batteryUnitSuite(UnitReport& r);
#endif
