#pragma once
#include <Arduino.h>

// Application log: lines go into a RAM ring buffer (safe from any task), are copied to the
// USB serial port by dlog::pump() in the loop task, and are served to the web UI.
// LOGD lines are compiled in only for the debug build (PV_DEBUG=1).
namespace dlog {

enum Level : uint8_t { kError, kWarn, kInfo, kDebug };

void write(Level lvl, const char* tag, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

// Copies pending lines to USB serial; call from the loop task.
void pump();

// Appends lines with sequence >= from to out (one per line). Returns the next sequence number
// and sets dropped to the number of requested lines that have already been overwritten.
uint32_t read(uint32_t from, String& out, uint32_t& dropped);

constexpr bool kDebugBuild =
#if PV_DEBUG
    true;
#else
    false;
#endif

}  // namespace dlog

#define LOGE(tag, ...) dlog::write(dlog::kError, tag, __VA_ARGS__)
#define LOGW(tag, ...) dlog::write(dlog::kWarn, tag, __VA_ARGS__)
#define LOGI(tag, ...) dlog::write(dlog::kInfo, tag, __VA_ARGS__)
#if PV_DEBUG
#define LOGD(tag, ...) dlog::write(dlog::kDebug, tag, __VA_ARGS__)
#else
#define LOGD(tag, ...) \
  do {                 \
  } while (0)
#endif
