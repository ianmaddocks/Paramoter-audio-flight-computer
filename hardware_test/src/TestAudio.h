#pragma once
#include <stdint.h>

// I2S test signal generator for the MAX98357A: tones, sweeps, simulated vario and voice clips.
namespace testaudio {

enum class Wave : uint8_t { Sine, Square };

bool begin();
bool ok();

void tone(float hz, uint32_t ms, Wave wave);
void sweep(float fromHz, float toHz, uint32_t ms);
void vario(float climbMs);  // runs until stop()
void clip(const char* name);  // plays /v/<name>.adp
void stop();

void setVolume(uint8_t pct);
uint8_t volume();
bool busy();
const char* mode();
const char* lastError();

}  // namespace testaudio
