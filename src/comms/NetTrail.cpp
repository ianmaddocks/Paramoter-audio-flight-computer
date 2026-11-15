#include "NetTrail.h"

#include <esp_attr.h>
#include <freertos/FreeRTOS.h>

namespace nettrail {
namespace {

constexpr uint32_t kMagic = 0x4E545231;  // "NTR1"
constexpr int kEntries = 48;
struct Entry {
  uint32_t tS;      // seconds since that power-up or restart
  uint8_t code;
  uint8_t heapKb;   // free memory then, in kB
  int16_t arg;
};
struct Store {
  uint32_t magic;
  uint16_t next;   // where the next entry goes
  uint16_t count;
  Entry e[kEntries];
};
RTC_NOINIT_ATTR Store g;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

const char* nameOf(uint8_t c) {
  switch (c) {
    case Boot: return "restarted, reason";
    case StaUp: return "joined the maintenance network";
    case StaDown: return "maintenance link dropped, reason";
    case GaveUp: return "gave up looking for the maintenance network";
    case Retry: return "looking for the maintenance network again";
    case ApJoin: return "a device joined the vario's network, now";
    case ApLeave: return "a device left the vario's network, now";
    case LowMem: return "web request refused: low memory, largest block (B)";
    default: return "?";
  }
}

}  // namespace

void begin(int resetReason) {
  if (g.magic != kMagic || g.next >= kEntries || g.count > kEntries) {
    memset(&g, 0, sizeof(g));
    g.magic = kMagic;
  }
  add(Boot, resetReason);
}

void add(Code code, int arg) {
  const uint32_t heap = ESP.getFreeHeap();
  portENTER_CRITICAL(&mux);
  Entry& e = g.e[g.next];
  e.tS = millis() / 1000;
  e.code = code;
  e.heapKb = heap / 1024 > 255 ? 255 : heap / 1024;
  e.arg = arg > 32767 ? 32767 : arg;
  g.next = (g.next + 1) % kEntries;
  if (g.count < kEntries) ++g.count;
  portEXIT_CRITICAL(&mux);
}

void print(Print& out) {
  const int first = (g.next + kEntries - g.count) % kEntries;
  for (int i = 0; i < g.count; ++i) {
    const Entry& e = g.e[(first + i) % kEntries];
    out.printf("%s%6lu s  %s %d  (free %u kB)\n", e.code == Boot ? "--- " : "    ", (unsigned long)e.tS, nameOf(e.code), (int)e.arg,
               (unsigned)e.heapKb);
  }
  out.printf("    %6lu s  now  (free %u kB, lowest %u B)\n", (unsigned long)(millis() / 1000), (unsigned)(ESP.getFreeHeap() / 1024),
             (unsigned)ESP.getMinFreeHeap());
}

}  // namespace nettrail
