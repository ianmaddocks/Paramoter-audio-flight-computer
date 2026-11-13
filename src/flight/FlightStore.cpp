#include "FlightStore.h"

#include <Preferences.h>

namespace {
constexpr uint32_t kVersion = 2;  // 2: adds the position at the time of the save
const char* kNs = "flight";
const char* kKey = "rec";
}  // namespace

namespace flightstore {

bool load(FlightRecord& rec) {
  Preferences prefs;
  // Read-write open creates the namespace on a fresh device instead of logging NOT_FOUND.
  if (!prefs.begin(kNs, false)) return false;
  bool ok = prefs.isKey(kKey) && prefs.getBytesLength(kKey) == sizeof(rec) &&
            prefs.getBytes(kKey, &rec, sizeof(rec)) == sizeof(rec) && rec.version == kVersion;
  prefs.end();
  return ok;
}

void save(const FlightRecord& rec) {
  FlightRecord r = rec;
  r.version = kVersion;
  Preferences prefs;
  if (!prefs.begin(kNs, false)) return;
  prefs.putBytes(kKey, &r, sizeof(r));
  prefs.end();
}

}  // namespace flightstore
