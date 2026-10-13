#pragma once

// Seeed XIAO ESP32C3 GPIO numbers for the board labels (Dx).
namespace pins {
constexpr int I2S_DOUT = 6;   // D4 -> MAX98357A DIN
constexpr int I2S_BCLK = 7;   // D5 -> MAX98357A BCLK
constexpr int I2S_LRC = 21;   // D6 -> MAX98357A LRC (UART0 TX: ROM boot log toggles it briefly at reset)
constexpr int I2C_SCL = 20;   // D7 -> DPS310 SCK
constexpr int I2C_SDA = 8;    // D8 -> DPS310 SDI (strapping pin, kept high by the I2C pull-up)
constexpr int GPS_RX = 9;     // D9 <- GPS TX (BOOT strapping pin: GPS must not hold it low at power-up)
constexpr int GPS_TX = 10;    // D10 -> GPS RX
constexpr int BUTTON = 4;     // D2, push button to GND
constexpr int BATT_ADC = 3;   // D1, battery via 51k/51k divider
}  // namespace pins

constexpr uint8_t DPS310_ADDR = 0x77;
