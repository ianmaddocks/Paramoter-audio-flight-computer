#include "Log.h"

#include <stdarg.h>

namespace dlog {
namespace {

// The release build logs only errors, warnings and a few information lines, so it keeps a
// short history and leaves the memory free; debug builds trace several lines a second.
#if PV_DEBUG
constexpr int kLines = 128;
#else
constexpr int kLines = 40;
#endif
constexpr int kLineLen = 128;
constexpr int kMaxPumpLines = 16;

char gRing[kLines][kLineLen];
uint32_t gSeq = 0;      // sequence number of the next line to be written
uint32_t gPrinted = 0;  // next line to copy to USB serial
portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool gClientSeen = false;

}  // namespace

void write(Level lvl, const char* tag, const char* fmt, ...) {
  char buf[kLineLen];
  uint32_t ms = millis();
  int n = snprintf(buf, sizeof(buf), "[%6lu.%03lu] %c %s: ", (unsigned long)(ms / 1000),
                   (unsigned long)(ms % 1000), "EWID"[lvl], tag);
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
  va_end(ap);

  portENTER_CRITICAL(&gMux);
  memcpy(gRing[gSeq % kLines], buf, kLineLen);
  ++gSeq;
  portEXIT_CRITICAL(&gMux);
}

void pump() {
  char line[kLineLen];
  for (int i = 0; i < kMaxPumpLines; ++i) {
    uint32_t dropped = 0;
    portENTER_CRITICAL(&gMux);
    if (gPrinted == gSeq) {
      portEXIT_CRITICAL(&gMux);
      return;
    }
    portEXIT_CRITICAL(&gMux);
    // With USB plugged in but no terminal reading, the CDC buffer fills and a write stalls
    // (core warns "waiting USB Host - timeout"). Leave lines queued until there is room.
    if (Serial.availableForWrite() < kLineLen + 48) return;
    portENTER_CRITICAL(&gMux);
    if (gSeq - gPrinted > kLines) {
      dropped = gSeq - kLines - gPrinted;
      gPrinted = gSeq - kLines;
    }
    memcpy(line, gRing[gPrinted % kLines], kLineLen);
    ++gPrinted;
    portEXIT_CRITICAL(&gMux);
    if (dropped) Serial.printf("... %lu log lines dropped\n", (unsigned long)dropped);
    Serial.println(line);
  }
}

bool tracing() {
#if PV_TEST
  return gClientSeen;
#else
  return true;
#endif
}

void window(uint32_t& from, uint32_t& end, uint32_t& dropped) {
  gClientSeen = true;
  portENTER_CRITICAL(&gMux);
  end = gSeq;
  portEXIT_CRITICAL(&gMux);
  uint32_t oldest = end > kLines ? end - kLines : 0;
  if (from > end) from = oldest;  // device restarted since the client last asked
  dropped = from < oldest ? oldest - from : 0;
  if (from < oldest) from = oldest;
}

uint32_t readSome(uint32_t from, uint32_t end, int maxLines, String& out) {
  char line[kLineLen];
  uint32_t s = from;
  for (int n = 0; s < end && n < maxLines; ++s, ++n) {
    portENTER_CRITICAL(&gMux);
    bool overwritten = gSeq - s > kLines;  // written over while this reply was being sent
    if (!overwritten) memcpy(line, gRing[s % kLines], kLineLen);
    portEXIT_CRITICAL(&gMux);
    if (overwritten) continue;
    out += line;
    out += '\n';
  }
  return s;
}

}  // namespace dlog
