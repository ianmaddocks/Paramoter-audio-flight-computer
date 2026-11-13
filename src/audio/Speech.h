#pragma once
#include "audio/Audio.h"

// Builds utterances from vocabulary clips (see tools/vocab.txt), using the configured units.
namespace speech {
void addNumber(audio::Utterance& u, int n);
void addDecimal(audio::Utterance& u, float v);
void addHeight(audio::Utterance& u, float metres);
void addDistance(audio::Utterance& u, float metres);
void addClock(audio::Utterance& u, float relDeg);
void addDuration(audio::Utterance& u, uint32_t seconds);
void addSpeed(audio::Utterance& u, float metresPerS);  // in the configured speed unit
void addTurn(audio::Utterance& u, bool right);

void say(audio::Priority p, const char* clip);
}  // namespace speech
