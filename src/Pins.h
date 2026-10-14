#pragma once

// Seeed XIAO ESP32C3 GPIO numbers for the board labels (Dx).
namespace pins {
constexpr int BATT_ADC = A1;//3;   // D1, battery via 51k/51k divider
constexpr int BUTTON = D2;//4;     // D2, push button to GND
constexpr int I2S_DOUT = D4;//6;   // D4 -> MAX98357A DIN
constexpr int I2S_BCLK = D5;//7;   // D5 -> MAX98357A BCLK
constexpr int I2S_LRC = D6;//21;   // D6 -> MAX98357A LRC (UART0 TX: ROM boot log toggles it briefly at reset)
constexpr int I2C_SDA = D7;//8;    // D8 -> DPS310 SDI (strapping pin, kept high by the I2C pull-up)
constexpr int I2C_SCL = D8;//20;   // D7 -> DPS310 SCK
constexpr int GPS_TX = D9;//9;    // D10 -> GPS RX
constexpr int GPS_RX = D10;//10;     // D9 <- GPS TX (BOOT strapping pin: GPS must not hold it low at power-up)
}  // namespace pins

constexpr uint8_t DPS310_ADDR = 0x77;
