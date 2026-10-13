#pragma once
#include <stdint.h>

namespace audio {

constexpr uint32_t kSampleRate = 16000;

// Low: dropped if anything is already queued. Normal: queued. High: jumps the queue.
// Urgent: clears the queue and cuts off whatever is playing.
enum class Priority : uint8_t { Low, Normal, High, Urgent };

// A sequence of clip names (files /v/<name>.adp on LittleFS). Names must be string literals.
struct Utterance {
  static constexpr int kMaxClips = 24;
  const char* clips[kMaxClips];
  uint8_t count = 0;
  Priority prio;

  explicit Utterance(Priority p = Priority::Normal) : prio(p) {}
  Utterance& add(const char* clip) {
    if (count < kMaxClips) clips[count++] = clip;
    return *this;
  }
};

void begin();
void say(const Utterance& u);
bool speaking();

// hz = 0 silences. offMs = 0 gives a continuous tone.
void setTone(float hz, uint16_t onMs, uint16_t offMs);
void setVolumes(uint8_t speechPct, uint8_t tonePct);

}  // namespace audio
