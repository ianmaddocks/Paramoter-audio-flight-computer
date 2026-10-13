#include "Log.h"

#include <stdarg.h>

namespace dlog {
namespace {

constexpr int kLines = 128;
constexpr int kLineLen = 128;
constexpr int kMaxPumpLines = 16;

char gRing[kLines][kLineLen];
uint32_t gSeq = 0;      // sequence number of the next line to be written
uint32_t gPrinted = 0;  // next line to copy to USB serial
portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;

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

uint32_t read(uint32_t from, String& out, uint32_t& dropped) {
  char line[kLineLen];
  portENTER_CRITICAL(&gMux);
  uint32_t end = gSeq;
  portEXIT_CRITICAL(&gMux);
  uint32_t oldest = end > kLines ? end - kLines : 0;
  if (from > end) from = oldest;  // device restarted since the client last asked
  dropped = from < oldest ? oldest - from : 0;
  if (from < oldest) from = oldest;
  out.reserve(out.length() + (end - from) * 64);
  for (uint32_t s = from; s < end; ++s) {
    portENTER_CRITICAL(&gMux);
    bool overwritten = gSeq - s > kLines;
    if (!overwritten) memcpy(line, gRing[s % kLines], kLineLen);
    portEXIT_CRITICAL(&gMux);
    if (overwritten) {
      ++dropped;
      continue;
    }
    out += line;
    out += '\n';
  }
  return end;
}

}  // namespace dlog
