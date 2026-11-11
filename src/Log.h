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

// Reading the log in batches (so a reader never needs the whole buffer in memory at once):
// window() turns a client's "lines since <from>" into the range [from, end) still held, and sets
// dropped to how many requested lines have already been overwritten. readSome() appends up to
// maxLines of that range to out (one per line) and returns where to continue from.
void window(uint32_t& from, uint32_t& end, uint32_t& dropped);
uint32_t readSome(uint32_t from, uint32_t end, int maxLines, String& out);

// False in the test build until a client first reads the log: the once-a-second traces are held
// back so the start-up lines are still in the buffer when the test runner connects.
bool tracing();

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
