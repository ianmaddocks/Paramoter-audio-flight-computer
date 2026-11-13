#pragma once
#include <stdint.h>

namespace audio {

constexpr uint32_t kSampleRate = 16000;

// Low: dropped if anything is already queued. Normal: queued. High: jumps the queue.
// Urgent: clears the queue and cuts off whatever is playing.
enum class Priority : uint8_t { Low, Normal, High, Urgent };

// A sequence of clip names (files /v/<name>.adp on LittleFS). Names must be string literals.
// A name starting with '~' is not a file but a short tone made in place: "~900" is 900 Hz for 120 ms (used as the
// signal before urgent calls, and as the cue inside an utterance). A clip whose file is missing is skipped, unless the
// name is "a|b": then the first of a, b that exists is played ("battery_critical|battery_low", "baro_failed|~400"), so
// a message added in a later voice-clip set still says something on a vario whose clips have not been updated.
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

// What actually gets queued for an utterance: an urgent one gets a short signal tone first, so it is recognised over
// engine noise before the words begin (unless it already starts with a tone or is full).
inline Utterance withSignal(const Utterance& u) {
  Utterance q = u;
  if (u.prio == Priority::Urgent && u.count > 0 && u.count < Utterance::kMaxClips && u.clips[0][0] != '~') {
    for (int i = q.count; i > 0; --i) q.clips[i] = q.clips[i - 1];
    q.clips[0] = "~1000";
    ++q.count;
  }
  return q;
}

void begin();
void say(const Utterance& u);
// One-shot cue tone played at once on top of whatever is sounding (a button click, the hold cue); it does not
// wait in the speech queue and does not interrupt speech or the vario tone.
void cue(float hz, uint16_t ms);
bool speaking();

#if PV_TEST
// Unit tests: while capture is on, say() records the utterance as text instead of playing it.
void testCapture(bool on);
int testSaid();                  // utterances recorded since capture was switched on
const char* testSaidText(int i);  // "[prio] clip clip ..."
#endif

// hz = 0 silences. offMs = 0 gives a continuous tone.
void setTone(float hz, uint16_t onMs, uint16_t offMs);
void setVolumes(uint8_t speechPct, uint8_t tonePct);
// Current vario tone frequency (0 = off), so the microphone can ignore the speaker.
float toneHz();

}  // namespace audio
