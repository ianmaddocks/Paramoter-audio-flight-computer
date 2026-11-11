#pragma once

// Wi-Fi access point + captive-portal web UI for status, settings, actions and log download.
namespace webui {
void begin();
void update();
// First unmet pre-flight condition (as shown in the banner), or nullptr when ready to fly.
const char* readyBlocker();
}  // namespace webui
