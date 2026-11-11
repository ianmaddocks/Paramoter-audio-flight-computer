// Caps the memory the Wi-Fi driver may take for packet buffers.
//
// The Arduino core starts Wi-Fi with up to 32 receive and 32 transmit buffers, each about
// 1.6 kB and taken from free memory as packets queue up. That is up to 100 kB, on a board that
// has about 50 kB free once Wi-Fi, Bluetooth and the vario are running. A burst of traffic (a
// web page loading, a busy home network, the radio pausing for Bluetooth) could take free
// memory to zero: replies were cut short, the page came up without its settings, and the vario
// could drop off the network.
//
// The core offers no setting for these counts, so the linker is told (platformio.ini:
// -Wl,--wrap=esp_wifi_init) to route the one call that starts the Wi-Fi driver through here.
#include <esp_wifi.h>

#include "Log.h"

namespace {
// Receive is left roomier than transmit: with 8 or fewer receive buffers firmware uploads to
// the vario kept failing (packets dropped, the upload timed out), and after one such failure
// the web server stopped answering for over an hour. What the vario sends is small.
constexpr int kRxBuffers = 8;   // was 32: at most ~13 kB instead of ~51 kB
constexpr int kTxBuffers = 6;   // was 32: at most ~10 kB instead of ~51 kB
}  // namespace

extern "C" esp_err_t __real_esp_wifi_init(const wifi_init_config_t* config);

extern "C" esp_err_t __wrap_esp_wifi_init(const wifi_init_config_t* config) {
  wifi_init_config_t capped = *config;
  capped.dynamic_rx_buf_num = kRxBuffers;
  capped.dynamic_tx_buf_num = kTxBuffers;
  esp_err_t err = __real_esp_wifi_init(&capped);
  if (err != ESP_OK) {
    // Never leave the vario without Wi-Fi (its only way in): fall back to the core's own values.
    LOGE("WIFI", "capped Wi-Fi buffers refused (%d), using the defaults", (int)err);
    return __real_esp_wifi_init(config);
  }
  LOGI("WIFI", "Wi-Fi buffers capped at %d receive / %d transmit", kRxBuffers, kTxBuffers);
  return err;
}
