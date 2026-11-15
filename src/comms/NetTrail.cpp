#include "NetTrail.h"

#include <esp_attr.h>
#include <freertos/FreeRTOS.h>

namespace nettrail {
namespace {

constexpr uint32_t kMagic = 0x4E545232;  // "NTR2"
constexpr int kEntries = 48;
struct Entry {
  uint32_t tS;      // seconds since that power-up or restart
  uint8_t code;
  uint8_t heapKb;   // free memory then, in kB
  int16_t arg;
  uint16_t more;    // how many times the same thing happened again right after (kept as one entry: a run of refused requests must
                    // not push the rest off the trail, as it did the first time the trail was needed)
  uint16_t pad;
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
    case Deaf: return "joined, but the router does not answer: joining again";
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
  if (g.count) {
    Entry& last = g.e[(g.next + kEntries - 1) % kEntries];
    if (last.code == code && code == LowMem && last.more < 65535) {
      ++last.more;
      portEXIT_CRITICAL(&mux);
      return;
    }
  }
  Entry& e = g.e[g.next];
  e.more = 0;
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
    out.printf("%s%6lu s  %s %d  (free %u kB)", e.code == Boot ? "--- " : "    ", (unsigned long)e.tS, nameOf(e.code), (int)e.arg,
               (unsigned)e.heapKb);
    if (e.more) out.printf("  and %u more times after it", (unsigned)e.more);
    out.print("\n");
  }
  out.printf("    %6lu s  now  (free %u kB, lowest %u B)\n", (unsigned long)(millis() / 1000), (unsigned)(ESP.getFreeHeap() / 1024),
             (unsigned)ESP.getMinFreeHeap());
}

}  // namespace nettrail
