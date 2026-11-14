#pragma once

// Wi-Fi access point + captive-portal web UI for status, settings, actions and log download.
//
// HTTP API (all under /api): status, settings (GET/POST), schema, action, logs, log (GET/DELETE),
// debuglog, spectrum, ota, and in the test build sim. The page itself is comms/WebPage.h.
namespace webui {
void begin();
// Serves pending requests. Call every pass of the main loop.
void update();
#if PV_TEST
void testMaintGiveUp(uint32_t retryMs);  // test build: give up on the maintenance network now, look again after retryMs
#endif
}  // namespace webui
