#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "WebUi.h"
#include "Log.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_core_dump.h>
#include <esp_netif.h>
#include <esp_netif_net_stack.h>
#include <lwip/etharp.h>
#include <WiFiUdp.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <lwip/sockets.h>
#include <sys/stat.h>

#include "App.h"
#include "Hardware.h"
#include "Settings.h"
#include "Version.h"
#include "app/VarioSound.h"
#include "audio/Speech.h"
#include "audio/Volume.h"
#include "comms/AppIcon.h"
#include "comms/NetTrail.h"
#include "logging/LogStore.h"
#include "comms/WebPage.h"
#include "nav/Geo.h"
#include "sensors/Mic.h"
#include "test/Sim.h"
#include "test/ToneTest.h"
#include "test/UnitTest.h"


// Set from custom_ota_password in platformio.ini by tools/pio_ota.py; empty rejects every upload.
#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""
#endif

namespace webui {
namespace {

WebServer server(80);
DNSServer dns;
bool restartPending = false;
uint32_t restartAtMs = 0;
// Network diagnostics, for finding out why a vario drops off the network: what the Wi-Fi driver reports (set from its event
// callback, which only stores numbers) and why the vario last restarted. Shown in the status (wifi.* and resetReason) and
// logged when something changes.
volatile uint32_t diagStaDisc = 0, diagStaReason = 0, diagStaDiscMs = 0, diagGotIp = 0, diagApJoin = 0, diagApLeave = 0, diagStaConn = 0;
uint32_t diagLoggedDisc = 0;
volatile uint32_t diagStaConnAtDisc = 0;
const char* resetReasonName() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC: return "crash (panic)";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout (low supply voltage)";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    default: return "other";
  }
}
void onWifiEvent(arduino_event_id_t ev, arduino_event_info_t info) {
  switch (ev) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED: ++diagStaConn; break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      // (while it searches, the driver reports every failed attempt as a drop: only a drop of a link that was up goes on the trail)
      if (diagStaConn > diagStaConnAtDisc) nettrail::add(nettrail::StaDown, info.wifi_sta_disconnected.reason);
      diagStaConnAtDisc = diagStaConn;
      ++diagStaDisc;
      diagStaReason = info.wifi_sta_disconnected.reason;
      diagStaDiscMs = millis();
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      ++diagGotIp;
      nettrail::add(nettrail::StaUp);
      break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      ++diagApJoin;
      nettrail::add(nettrail::ApJoin, (int)(diagApJoin - diagApLeave));
      break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      ++diagApLeave;
      nettrail::add(nettrail::ApLeave, (int)(diagApJoin - diagApLeave));
      break;
    default: break;
  }
}
bool settingsNeedRestart = false;  // a setting that only applies at power-up was changed since power-up

// Server Wi-Fi runs alongside the vario AP only during its configured boot window.
enum class ServerWifi : uint8_t { Off, Connecting, Connected };
ServerWifi serverWifi = ServerWifi::Off;
uint32_t serverAttemptStartedMs = 0;  // start of the current search for the server network
const char* serverWhyOff = "not set";
char serverSsidUsed[33] = "";
char serverPassUsed[65] = "";
bool maintUsed = false;  // the maintenance-connection switch as it was at power-up

// Giving up the search for the maintenance network (to keep the vario's own network steady, see serverUpdate) used to last until the
// next restart: one drop of the router link, a router that was slow, and the vario stayed off your network until you restarted it.
// A router that was joined earlier in this power-up is known to be there, so it is looked for again every serverRetryMs, on the
// ground only (searching disturbs the vario's own Wi-Fi: never in flight). A network that was never found (away from home) is not
// searched for again until a restart.
constexpr uint32_t kServerRetryMs = 5 * 60 * 1000;
bool serverEverConnected = false;
// ... and "earlier" includes before a restart that was not a power-on: after the watchdog restart of 15 Nov 18:45 the router was
// not found within the 30 s, and the vario stayed off it for good because it "had never been joined". Kept in memory that a
// restart does not clear; switching on starts afresh (the vario may be somewhere else now).
constexpr uint32_t kJoinedMagic = 0x4A4E4431;
RTC_NOINIT_ATTR uint32_t joinedBeforeRestart;
uint32_t serverStoppedMs = 0;
uint32_t serverRetryMs = kServerRetryMs;

// The link that says it is up and is not. Seen on the bench on 15 Nov, twice (5 hours, then 30 minutes): the vario was joined to
// the router the whole time by its own count (no drop reported), yet nothing sent to it arrived, not even a ping, while its own
// network worked. Nothing in the firmware noticed, because all it looked at was "joined". So while joined, something small is
// sent to the router every 10 s (which also keeps the router and a mesh from forgetting the vario), and the router has to keep
// answering the question every network device asks before it sends (its hardware address). When it has not answered for 90 s
// the vario leaves and joins again: on the ground only, as joining disturbs the vario's own network for a few seconds.
constexpr uint32_t kKeepAliveMs = 10000, kDeafMs = 90000;
WiFiUDP keepUdp;
uint32_t keepLastMs = 0, routerHeardMs = 0;

bool routerAnswers(const IPAddress& gw) {
  struct netif* nif = (struct netif*)esp_netif_get_netif_impl(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"));
  ip4_addr_t a;
  a.addr = (uint32_t)gw;
  struct eth_addr* eth = nullptr;
  const ip4_addr_t* ip = nullptr;
  return nif && etharp_find_addr(nif, &a, &eth, &ip) >= 0;
}

void keepAlive(uint32_t now) {
  if (now - keepLastMs < kKeepAliveMs) return;
  keepLastMs = now;
  const IPAddress gw = WiFi.gatewayIP();
  if ((uint32_t)gw == 0) return;
  if (keepUdp.beginPacket(gw, 9)) {  // (port 9: "discard")
    keepUdp.write((uint8_t)0);
    keepUdp.endPacket();
  }
  if (routerAnswers(gw)) {
    routerHeardMs = now;
  } else if (now - routerHeardMs >= kDeafMs && !app.nav.flying) {
    routerHeardMs = now;
    nettrail::add(nettrail::Deaf);
    LOGW("WEB", "Joined to '%s' but the router has not answered for %u s: joining again", serverSsidUsed, (unsigned)(kDeafMs / 1000));
    WiFi.reconnect();
  }
}

void serverStop(const char* why) {
  serverStoppedMs = millis();
  nettrail::add(nettrail::GaveUp);
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  serverWifi = ServerWifi::Off;
  serverWhyOff = why;
  LOGI("WEB", "Server Wi-Fi off (%s)", why);
}

void serverUpdate(uint32_t now) {
  if (diagStaDisc != diagLoggedDisc) {  // say so in the log when the router link dropped (the callback only counts)
    diagLoggedDisc = diagStaDisc;
    LOGW("WEB", "Wi-Fi dropped (reason %u), %u drops since power-up, free heap %u", (unsigned)diagStaReason, (unsigned)diagStaDisc,
         (unsigned)ESP.getFreeHeap());
  }
  if (serverWifi == ServerWifi::Off) {
    if (maintUsed && serverEverConnected && serverSsidUsed[0] && !app.nav.flying && now - serverStoppedMs >= serverRetryMs) {
      LOGI("WEB", "Looking for the maintenance network '%s' again (it was joined earlier)", serverSsidUsed);
      nettrail::add(nettrail::Retry);
      WiFi.mode(WIFI_AP_STA);
      WiFi.begin(serverSsidUsed, serverPassUsed);
      serverWifi = ServerWifi::Connecting;
      serverAttemptStartedMs = now;
    }
    return;
  }
  bool up = WiFi.status() == WL_CONNECTED;
  if (serverWifi == ServerWifi::Connecting && up) {
    serverEverConnected = true;
    joinedBeforeRestart = kJoinedMagic;
    routerHeardMs = now;
    serverWifi = ServerWifi::Connected;
    LOGI("WEB", "Joined server network '%s' as %s (http://paravario.local)", g_cfg.serverSsid,
         WiFi.localIP().toString().c_str());
    // Said once per power-up, and not in the air: tells whoever is at the bench that the maintenance connection is up.
    static bool announced = false;
    if (!announced) {
      announced = true;
      if (!app.nav.flying) speech::say(audio::Priority::Normal, "maint_connected");
    }
  } else if (serverWifi == ServerWifi::Connected && up) {
    keepAlive(now);
  } else if (serverWifi == ServerWifi::Connected && !up) {
    serverWifi = ServerWifi::Connecting;
    serverAttemptStartedMs = now;
    LOGW("WEB", "Lost server Wi-Fi, retrying");
  } else if (serverWifi == ServerWifi::Connecting && g_cfg.serverGiveUpS > 0 &&
             now - serverAttemptStartedMs >= (uint32_t)g_cfg.serverGiveUpS * 1000UL) {
    // The station radio scans and hops channels while it cannot find the network, which makes
    // the vario's own Wi-Fi drop out. Without a server network, run the vario network alone.
    return serverStop("not found, gave up (restart to retry)");
  }
}

void sendJson(const JsonDocument& doc, int code = 200) {
  String s;
  s.reserve(measureJson(doc) + 1);  // one allocation of the right size, not repeated growth
  serializeJson(doc, s);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", s);
}

void sendError(int code, const char* msg) {
  JsonDocument doc;
  doc["error"] = msg;
  sendJson(doc, code);
}

// Who has been talking to the web server, whichever network they came in on (the vario's own access point or
// the server network, where the access point's station count cannot see them): the last time each of up to
// kMaxWeb addresses made a request, and whether that request was one of the test harness's own (it sends
// X-HIL). The status reports how many addresses were heard in the last kWebWindowMs, and how many of those
// were not the harness, so a test run can tell that someone else has a page open.
constexpr int kMaxWeb = 8;
constexpr uint32_t kWebWindowMs = 15000;
struct WebClient {
  uint32_t ip = 0;
  uint32_t lastMs = 0;        // last request of any kind
  uint32_t lastOtherMs = 0;   // last request that was not the harness's
};
WebClient webClients[kMaxWeb];

void noteWebClient() {
  const uint32_t ip = (uint32_t)server.client().remoteIP();
  const uint32_t now = millis() | 1;  // never 0: 0 means 'never'
  const bool harness = server.hasHeader("X-HIL");
  WebClient* slot = nullptr;
  for (WebClient& c : webClients) {
    if (c.ip == ip) { slot = &c; break; }
  }
  if (!slot) {  // a free slot, or the one that has been quiet longest
    slot = &webClients[0];
    for (WebClient& c : webClients) {
      if (c.ip == 0) { slot = &c; break; }
      if (now - c.lastMs > now - slot->lastMs) slot = &c;
    }
    *slot = WebClient();
    slot->ip = ip;
  }
  slot->lastMs = now;
  if (!harness) slot->lastOtherMs = now;
}

// How many addresses were heard from lately; *others: how many of them made a request that was not the harness's.
int webClientsHeard(int* others) {
  const uint32_t now = millis() | 1;
  int all = 0, other = 0;
  for (const WebClient& c : webClients) {
    if (c.ip && c.lastMs && now - c.lastMs <= kWebWindowMs) ++all;
    if (c.ip && c.lastOtherMs && now - c.lastOtherMs <= kWebWindowMs) ++other;
  }
  *others = other;
  return all;
}

void handleStatus() {
  const NavState& n = app.nav;
  JsonDocument doc;
  const char* why = appNotReadyReason();
  doc["ready"] = why == nullptr;
  if (why) doc["notReady"] = why;
  doc["minSats"] = g_cfg.readyMinSats;
  doc["uptime"] = millis() / 1000;
  doc["restartNeeded"] = settingsNeedRestart;
  doc["resetReason"] = resetReasonName();  // why the vario last restarted
  doc["heap"] = ESP.getFreeHeap();
  doc["heapMin"] = ESP.getMinFreeHeap();     // lowest since power-on
  doc["heapBlock"] = ESP.getMaxAllocHeap();  // largest single block available now
  doc["debug"] = dlog::kDebugBuild;
#if PV_TEST
  doc["test"] = true;
  {
    const variosound::BootInfo b = variosound::bootInfo();
    JsonObject bj = doc["boot"].to<JsonObject>();
    bj["done"] = b.done;
    bj["hz"] = b.beepHz;
    bj["beepMs"] = b.beepMs;
    bj["readyGapMs"] = b.readyGapMs;
  }
  {  // lowest free stack (bytes) each task has ever had
    JsonObject sj = doc["stack"].to<JsonObject>();
    for (const char* name : {"loopTask", "audio", "mic"}) {
      TaskHandle_t h = xTaskGetHandle(name);
      if (h) sj[name] = (uint32_t)uxTaskGetStackHighWaterMark(h) * sizeof(StackType_t);
    }
  }
#endif
  doc["version"] = PV_VERSION;
  doc["git"] = PV_GIT;
  doc["release"] = PV_RELEASE;  // the newest git tag, also in the Bluetooth model name ("ParaVario <release>")
  doc["flying"] = n.flying;
  doc["flights"] = app.flights;
  doc["flightTime"] = n.flightTimeS;
  doc["agl"] = n.aglM;
  doc["altMsl"] = n.altMslM;
  doc["vario"] = n.varioMs;
  doc["muted"] = app.varioMuted;

  // Optional hardware: the page hides what is not fitted, and the sections below are left out.
  JsonObject hwj = doc["hw"].to<JsonObject>();
  hwj["gps"] = hw::hasGps();
  hwj["gpsState"] = hw::gpsState() == hw::GpsState::Present ? "present" : hw::gpsState() == hw::GpsState::Absent ? "absent" : "unknown";
  hwj["mic"] = hw::hasMic();
  hwj["spectrum"] = hw::hasSpectrum();
  JsonObject vol = doc["vol"].to<JsonObject>();  // volumes being applied now
  vol["speech"] = volume::speechPct();
  vol["vario"] = volume::varioPct();

  if (hw::hasGps()) {
    JsonObject gps = doc["gps"].to<JsonObject>();
    gps["link"] = app.gpsData.link;
    gps["fix"] = n.fix;
    gps["sats"] = n.sats;
    gps["hdop"] = n.hdop;
    gps["lat"] = n.lat;
    gps["lon"] = n.lon;
    gps["speed"] = n.speedMs;
    gps["moving"] = app.moving.on();  // a direction of travel exists (arrows shown), with hysteresis
    gps["track"] = n.trackDeg;
    gps["alt"] = n.gpsAltM;
    // Glide ratio (ground distance per height lost) over the two windows; a key is left out
    // when there is no glide to report (level, climbing, not flying, too little data yet).
    JsonObject glide = doc["glide"].to<JsonObject>();
    const float g1 = app.glide.shortRatio(millis()), g2 = app.glide.longRatio(millis());
    if (!isnan(g1)) glide["now"] = g1;
    if (!isnan(g2)) glide["avg"] = g2;
  }

  JsonObject baro = doc["baro"].to<JsonObject>();
  baro["ok"] = n.baroOk;
  baro["pressure"] = n.pressurePa;
  baro["temp"] = n.tempC;
#if PV_TEST
  baro["rej"] = app.baro.rejected();
  baro["jumps"] = app.baro.jumps();
  baro["frozen"] = app.baro.frozen();
#endif

  JsonObject batt = doc["batt"].to<JsonObject>();
  batt["v"] = app.battery.volts();
  batt["pct"] = app.battery.percent();
  int battMin = app.battery.minutesLeftOrUnknown();
  if (battMin >= 0) batt["min"] = battMin;
  batt["learned"] = app.battery.learned();

  if (hw::hasMic()) {
    JsonObject micj = doc["mic"].to<JsonObject>();
    micj["ok"] = mic::running();
    float nd = mic::noiseDb();
    if (!isnan(nd)) micj["noise"] = nd;
    micj["vol"] = volume::varioPct();
    micj["rpm"] = mic::rpm();
    micj["raw"] = mic::rpmRaw();
    micj["conf"] = mic::rpmConfidence();
  }

  JsonObject wind = doc["wind"].to<JsonObject>();
  wind["valid"] = app.wind.valid();
  wind["from"] = app.wind.fromDeg();
  wind["speed"] = app.wind.speedMs();
  wind["src"] = app.wind.sourceName();

  JsonObject lz = doc["lz"].to<JsonObject>();
  lz["valid"] = n.lzValid;
  lz["lat"] = n.lzLat;
  lz["lon"] = n.lzLon;
  lz["elev"] = n.lzElevM;
  if (n.lzValid && n.fix) {
    lz["dist"] = geo::distanceM(n.lat, n.lon, n.lzLat, n.lzLon);
    lz["brg"] = geo::bearingDeg(n.lat, n.lon, n.lzLat, n.lzLon);
  }

  if (app.safeArea.safeCount() + app.safeArea.restrictedCount() > 0) {
    JsonObject sa = doc["area"].to<JsonObject>();
    if (app.safeArea.safeCount() > 0) {
      sa["state"] = app.safeArea.stateName();
      sa["edge"] = app.safeArea.edgeM();
    }
    if (app.safeArea.restrictedCount() > 0) {
      sa["rstate"] = app.safeArea.restrictedStateName();
      sa["rdist"] = app.safeArea.restrictedM();
    }
  }

  doc["mode"] = app.emergency.active() ? "emergency" : app.approach.active() ? "approach" : "none";
  doc["approach"] = app.approach.phaseName();
  if (app.emergency.active()) doc["range"] = app.emergency.lastRangeM();
  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["apSsid"] = settings::apSsid();
  wifi["bleName"] = settings::bleName();
  wifi["apIp"] = WiFi.softAPIP().toString();
  wifi["apClients"] = WiFi.softAPgetStationNum();
  {
    int others = 0;
    wifi["webClients"] = webClientsHeard(&others);  // addresses that made a request in the last 15 s
    wifi["webOthers"] = others;                    // ... of which not the test harness (X-HIL)
  }
  wifi["serverSsid"] = serverSsidUsed;
  wifi["server"] = serverWifi == ServerWifi::Connected ? "connected" : serverWifi == ServerWifi::Connecting ? "connecting" : serverWhyOff;
  if (serverWifi == ServerWifi::Connected) {
    wifi["serverIp"] = WiFi.localIP().toString();
    wifi["rssi"] = WiFi.RSSI();
  }
  wifi["maint"] = maintUsed;
  wifi["staConn"] = diagStaConn;      // times the vario joined the router
  wifi["staDisc"] = diagStaDisc;      // times it was dropped (or lost the router)
  wifi["staReason"] = diagStaReason;  // the Wi-Fi reason code of the last drop (e.g. 2 auth expired, 8 left, 200 beacon timeout, 201 no AP found)
  wifi["staDiscAgoS"] = diagStaDisc ? (millis() - diagStaDiscMs) / 1000 : -1;
  wifi["gotIp"] = diagGotIp;
  wifi["apJoin"] = diagApJoin;        // devices that joined the vario's own network
  wifi["apLeave"] = diagApLeave;
  wifi["channel"] = WiFi.channel();
  if (maintUsed != g_cfg.maintWifi || (g_cfg.maintWifi && (strcmp(serverSsidUsed, g_cfg.serverSsid) != 0 ||
                                                           strcmp(serverPassUsed, g_cfg.serverPass) != 0))) {
    wifi["restart"] = true;
  }
  doc["ble"] = app.ble.clients();
  doc["logging"] = app.logger.active();
  sendJson(doc);
}

#if PV_TEST
// Test build only: /api/sim?key=value&... sets simulated inputs (see test/Sim.h); "reset"
// returns the flight logic to its power-on state and "learntest" runs the battery-learner check.
void handleSim() {
  for (int i = 0; i < server.args(); ++i) {
    String k = server.argName(i), v = server.arg(i);
    if (k == "reset") {
      testResetFlight();
    } else if (k == "windflush") {
      testFlushWind();
    } else if (k == "learntest") {
      batteryLearnSelfTest();
    } else if (k == "tonetest") {
      varioToneSelfTest();
    } else if (k == "unit") {
      String js;
      if (!runUnitSuite(v, &js)) {
        sendError(400, "unknown unit suite");
        return;
      }
      server.sendHeader("Cache-Control", "no-store");
      server.send(200, "application/json", js);
      return;
    } else if (!sim::set(k, v)) {
      sendError(400, "unknown sim key");
      return;
    }
  }
  String js;
  sim::describe(js);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", js);
}
#endif

void handleGetSettings();  // (after the pieces code below)

// Big replies (settings table, debug log) are sent in pieces through one fixed buffer, each
// piece in a single network write of about one packet.
//
// Two memory traps are avoided this way. Building a whole reply first needs a large block of
// memory, which cannot be had when memory is short (the page then came up without settings).
// And the web server sends every write as its own packet, each tying up a 1.6 kB Wi-Fi buffer
// until the radio gets it out, so a reply sent as many small writes can briefly take tens of
// kB (up to 32 buffers) and run the vario out of memory.
constexpr size_t kPieceHead = 6;      // "xxxx\r\n" chunk-size line, filled in when sent
constexpr size_t kPieceData = 1400;   // with the head and tail, just under one TCP segment
char gPiece[kPieceHead + kPieceData + 2];
size_t gPieceLen = 0;
const char* gRouteName();
bool gPieceDead = false;  // the client stopped taking data: the rest of this reply is dropped

// One network write of a reply sent in parts. A write to a client that takes nothing (gone, or a link that is stuck) waits up
// to 10 s inside the network library, and a reply of several writes then holds the main loop until the 30 s watchdog restarts
// the vario (15 Nov 18:45, from the stored crash record: the log list, on its third write). So the watchdog is fed before each
// write, and after one write that fails the client is dropped and the rest of the reply is not attempted.
bool writeToClient(const uint8_t* data, size_t len) {
  if (gPieceDead) return false;
  esp_task_wdt_reset();
  if (server.client().write(data, len) == len) return true;
  gPieceDead = true;
  LOGW("WEB", "client stopped taking data during %s: reply dropped", gRouteName());
  server.client().stop();
  return false;
}

void flushPiece() {
  if (!gPieceLen) return;
  char head[kPieceHead + 1];
  snprintf(head, sizeof(head), "%04x\r\n", (unsigned)gPieceLen);
  memcpy(gPiece, head, kPieceHead);
  memcpy(gPiece + kPieceHead + gPieceLen, "\r\n", 2);
  writeToClient((const uint8_t*)gPiece, kPieceHead + gPieceLen + 2);
  gPieceLen = 0;
}

void beginPieces(const char* type) {
  gPieceLen = 0;
  gPieceDead = false;
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);  // chunked transfer
  server.send(200, type, "");
}

void addPiece(const char* data, size_t len) {
  while (len) {
    size_t n = min(len, kPieceData - gPieceLen);
    memcpy(gPiece + kPieceHead + gPieceLen, data, n);
    gPieceLen += n;
    data += n;
    len -= n;
    if (gPieceLen == kPieceData) flushPiece();
  }
}
void addPiece(const String& s) { addPiece(s.c_str(), s.length()); }
void addPiece(const char* text) { addPiece(text, strlen(text)); }

void endPieces() {
  flushPiece();
  if (gPieceDead) return;
  server.sendContent("");  // closing zero-length chunk; also ends the server's chunked mode
}
// Print into the pieces, for text written as it is made.
struct PieceOut : Print {
  size_t write(uint8_t c) override {
    addPiece((const char*)&c, 1);
    return 1;
  }
  size_t write(const uint8_t* b, size_t n) override {
    addPiece((const char*)b, n);
    return n;
  }
};

void handleGetSettings() {
  beginPieces("application/json");
  PieceOut po;
  settings::writeJson(po);
  endPieces();
}


// A reply of known length sent in parts of about one packet (a log, the crash record): the same care as above.
void beginRaw(const char* type, size_t length) {
  gPieceDead = false;
  server.setContentLength(length);
  server.send(200, type, "");
}

void handleSchema() {
  beginPieces("application/json");
  String one;
  one.reserve(200);
  const int n = settings::schemaCount();
  bool first = true;
  addPiece("[", 1);
  for (int i = 0; i < n; ++i) {
    JsonDocument doc;
    if (!settings::schemaEntry(i, doc.to<JsonObject>())) continue;  // not for this hardware
    one = "";
    serializeJson(doc, one);
    if (!first) addPiece(",", 1);
    first = false;
    addPiece(one);
  }
  addPiece("]", 1);
  endPieces();
}

// The steps are kept apart so that only one of the request, the stored copy and the reply is in
// memory at a time (together they needed about 25 kB).
void handlePostSettings() {
  Settings previous = g_cfg;
  bool restart = false;
  {
    JsonDocument in;
    if (deserializeJson(in, server.arg("plain")) != DeserializationError::Ok || !in.is<JsonObject>()) {
      sendError(400, "invalid JSON");
      return;
    }
    restart = settings::fromJson(in.as<JsonObjectConst>());
  }
  if (restart) settingsNeedRestart = true;
  LOGI("WEB", "settings saved%s", restart ? " (restart needed)" : "");
  if (!settings::save()) {
    g_cfg = previous;
    sendError(500, "save failed");
    return;
  }
  beginPieces("application/json");
  addPiece(restart ? "{\"ok\":true,\"restart\":true,\"settings\":" : "{\"ok\":true,\"restart\":false,\"settings\":");
  PieceOut po;
  settings::writeJson(po);  // straight into the packets: no copy of the whole text
  addPiece("}", 1);
  endPieces();
}

// The safe areas: GET gives them, POST replaces them all ({"a":[{"n":name,"on":true,"p":[[lat,lon],...]}]}).
void handleGetAreas() {
  String s;
  app.safeArea.toJson(s);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", s);
}

void handlePostAreas() {
  const char* err = nullptr;
  if (!app.safeArea.setFromJson(server.arg("plain").c_str(), &err)) {
    sendError(400, err ? err : "not usable");
    return;
  }
  JsonDocument doc;
  doc["ok"] = true;
  sendJson(doc);
}

void handleAction() {
  String what = server.arg("do");
  const NavState& n = app.nav;
  LOGI("WEB", "action %s", what.c_str());
  if (what == "approach") {
    actionToggleApproach();
  } else if (what == "emergency") {
    actionToggleEmergency();
  } else if (what == "mute") {
    actionToggleMute();
  } else if (what == "tell_me") {
    actionTellMe();
  } else if (what == "reset_flight") {
    actionResetFlight();
  } else if (what == "lz_here") {
    if (!n.fix) {  // always the case without a GPS
      sendError(409, "no GPS fix");
      return;
    }
    g_cfg.lzMode = LZ_MANUAL;
    g_cfg.lzLat = n.lat;
    g_cfg.lzLon = n.lon;
    g_cfg.lzElevM = n.altMslM;
    settings::save();
    speech::say(audio::Priority::Normal, "lz_set");
  } else if (what == "lz_takeoff") {
    g_cfg.lzMode = LZ_TAKEOFF;
    settings::save();
  } else if (what == "test_voice") {
    audio::Utterance u(audio::Priority::Urgent);
    u.add("system_ready");
    speech::addHeight(u, 152.4f);
    speech::addClock(u, 90);
    speech::addDistance(u, 1250);
    audio::say(u);
  } else if (what == "defaults") {
    settings::resetDefaults();
    restartPending = true;
    restartAtMs = millis() + 500;
  } else if (what == "restart") {
    restartPending = true;
    restartAtMs = millis() + 500;
  } else {
    sendError(400, "unknown action");
    return;
  }
  JsonDocument doc;
  doc["ok"] = true;
  sendJson(doc);
}

// Plain text: first line "#next=<seq> dropped=<n> debug=<0|1>", then one log line per line.
void handleDebugLog() {
  uint32_t since = server.hasArg("since") ? strtoul(server.arg("since").c_str(), nullptr, 10) : 0;
  uint32_t end = 0, dropped = 0;
  dlog::window(since, end, dropped);
  beginPieces("text/plain");
  String line;
  line.reserve(140);
  line = "#next=" + String(end) + " dropped=" + String(dropped) +
         " debug=" + String(dlog::kDebugBuild ? 1 : 0) + "\n";
  addPiece(line);
  while (since < end) {
    line = "";
    since = dlog::readSome(since, end, 1, line);
    addPiece(line);
  }
  endPieces();
}

#if PV_HAS_SPECTRUM
// Binary: u32 seq, f32 binHz, f32 toneHz (little endian), then mic::kSpectrumBins bytes
// (dBFS = byte / 2.5 - 100). 204 when there is nothing newer than ?since=<seq>.
void handleSpectrum() {
  uint32_t since = server.hasArg("since") ? strtoul(server.arg("since").c_str(), nullptr, 10) : 0;
  uint8_t buf[12 + mic::kSpectrumBins];
  float binHz = 0, toneHz = 0;
  uint32_t seq = hw::hasMic() ? mic::spectrum(buf + 12, &binHz, &toneHz) : 0;
  server.sendHeader("Cache-Control", "no-store");
  if (seq == 0 || seq == since) {
    server.send(204);
    return;
  }
  memcpy(buf, &seq, 4);
  memcpy(buf + 4, &binHz, 4);
  memcpy(buf + 8, &toneHz, 4);
  server.send_P(200, "application/octet-stream", (const char*)buf, sizeof(buf));
}
#endif  // PV_HAS_SPECTRUM

// Binary: u16 sample count, u16 seconds between samples, then that many int16 heights above
// ground in quarter metres, oldest first (the last ten minutes flown, for the flight tab's graph); -32768 marks the gap
// between two flights.
void handleHeight() {
  uint8_t buf[4 + 2 * HeightHistory::kSamples];
  int16_t samples[HeightHistory::kSamples];
  const uint16_t n = (uint16_t)app.heightHistory.copy(samples);
  const uint16_t stepS = HeightHistory::kStepMs / 1000;
  memcpy(buf, &n, 2);
  memcpy(buf + 2, &stepS, 2);
  memcpy(buf + 4, samples, 2 * n);
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "application/octet-stream", (const char*)buf, 4 + 2 * n);
}

// LittleFS.exists() logs an error for a missing path, so check with stat() instead.
bool fsExists(const String& path) {
  struct stat st;
  return stat(("/littlefs" + path).c_str(), &st) == 0;
}

bool validLogName(const String& name) {
  if (name.isEmpty() || name.length() > 32 || !name.endsWith(".igc")) return false;
  for (char c : name) {
    if (!isalnum((unsigned char)c) && c != '-' && c != '_' && c != '.') return false;
  }
  return name.indexOf("..") < 0;
}

// Written out piece by piece from 28 bytes a flight: this request used to build the list of every log piece three times over and a
// JSON document on top (14 kB with 32 logs, of 35 kB free), and ran the vario out of memory, which restarts it.
void handleLogs() {
  std::vector<logstore::Info> flights;
  logstore::summary(flights);
  beginPieces("application/json");
  char buf[96];
  addPiece("{\"files\":[");
  bool first = true;
  for (const logstore::Info& f : flights) {
    // a flight is one IGC file to the owner, however it is stored; p: how many files it is stored in (for the tests)
    const int n = snprintf(buf, sizeof(buf), "%s{\"n\":\"%s.igc\",\"s\":%lu,\"p\":%u}", first ? "" : ",", f.id, (unsigned long)f.size,
                           (unsigned)f.pieces);
    addPiece(buf, n);
    first = false;
  }
  const int n = snprintf(buf, sizeof(buf), "],\"free\":%lu,\"total\":%lu}", (unsigned long)(LittleFS.totalBytes() - LittleFS.usedBytes()),
                         (unsigned long)LittleFS.totalBytes());
  addPiece(buf, n);
  endPieces();
}

// The trail of network events that survives a crash (comms/NetTrail.h), as text.
void handleNetTrail() {
  String s;
  s.reserve(3600);
  class ToString : public Print {
   public:
    explicit ToString(String& s) : s_(s) {}
    size_t write(uint8_t c) override { return s_.concat((char)c) ? 1 : 0; }
    size_t write(const uint8_t* b, size_t n) override { return s_.concat((const char*)b, n) ? n : 0; }

   private:
    String& s_;
  } out(s);
  nettrail::print(out);
  server.send(200, "text/plain", s);
}

void handleLogDownload() {
  String name = server.arg("name");
  if (!validLogName(name)) {
    sendError(400, "bad name");
    return;
  }
  logstore::Reader rd;
  if (!rd.open(name)) {
    sendError(404, "not found");
    return;
  }
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
  beginRaw("application/octet-stream", rd.total());
  int n;
  while ((n = rd.read((uint8_t*)gPiece, kPieceData)) > 0 && writeToClient((const uint8_t*)gPiece, n)) {
  }
  rd.close();
}

// The record the chip keeps in flash of its last crash (which task, where, and every task's stack): kept across restarts and
// firmware updates until the next crash overwrites it. Downloaded as it is stored, for tools/crash_decode.py; ?erase=1 clears it.
void handleCoreDump() {
  if (server.hasArg("erase")) {
    esp_core_dump_image_erase();
    server.send(200, "text/plain", "erased");
    return;
  }
  size_t addr = 0, size = 0;
  const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
  if (!part || esp_core_dump_image_get(&addr, &size) != ESP_OK || addr < part->address || size > part->size) {
    sendError(404, "no crash record stored");
    return;
  }
  beginRaw("application/octet-stream", size);
  for (size_t off = 0; off < size;) {
    const size_t n = min(kPieceData, size - off);
    if (esp_partition_read(part, addr - part->address + off, gPiece, n) != ESP_OK || !writeToClient((const uint8_t*)gPiece, n)) break;
    off += n;
  }
}

void handleLogDelete() {
  String name = server.arg("name");
  if (!validLogName(name)) {
    sendError(400, "bad name");
    return;
  }
  if (!logstore::remove(name)) {
    sendError(404, "not found");
    return;
  }
  JsonDocument doc;
  doc["ok"] = true;
  sendJson(doc);
}

// Firmware or file-system image upload (multipart POST to /api/ota, ?target=fs for the
// LittleFS image, optional ?md5=). Needs the X-OTA-Password header; allowed in flight so a
// bad build can always be replaced. The vario restarts after a good image.
String otaError;
bool otaFs = false;
bool otaOk = false;
const char* kOtaHeaders[] = {"X-OTA-Password", "X-HIL"};

bool otaPasswordOk() {
  const String given = server.header("X-OTA-Password");
  const size_t n = strlen(OTA_PASSWORD);
  uint8_t diff = given.length() != n;
  for (size_t i = 0; i < n; ++i) diff |= (uint8_t)((i < given.length() ? given[i] : 0) ^ OTA_PASSWORD[i]);
  return n > 0 && diff == 0;
}

void otaFail(const String& msg) {
  if (otaError.isEmpty()) otaError = msg;
  if (Update.isRunning()) Update.abort();
}

void handleOtaUpload() {
  esp_task_wdt_reset();  // a whole upload is handled inside one pass of the main loop
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    // The web server library waits for upload data with no time limit, so an upload whose
    // sender has gone (out of range, PC asleep) would block the vario until the connection is
    // noticed dead, by default hours later. Probe the connection so that takes seconds; the
    // loop watchdog in main.cpp is the backstop.
    int idleS = 5, intervalS = 2, probes = 3;
    server.client().setOption(TCP_KEEPIDLE, &idleS);
    server.client().setOption(TCP_KEEPINTVL, &intervalS);
    server.client().setOption(TCP_KEEPCNT, &probes);
    otaError = "";
    otaOk = false;
    otaFs = server.arg("target") == "fs";
    if (!otaPasswordOk()) {
      otaError = "wrong password";
      LOGW("OTA", "Update rejected: wrong password");
      return;
    }
    LOGI("OTA", "%s update from '%s'", otaFs ? "File system" : "Firmware", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, otaFs ? U_SPIFFS : U_FLASH)) {
      otaFail(Update.errorString());
      return;
    }
    if (server.hasArg("md5") && !Update.setMD5(server.arg("md5").c_str())) otaFail("bad md5");
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (otaError.isEmpty() && Update.write(up.buf, up.currentSize) != up.currentSize) {
      otaFail(Update.errorString());
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (otaError.isEmpty() && !Update.end(true)) otaFail(Update.errorString());
    if (otaError.isEmpty()) {
      otaOk = true;
      LOGI("OTA", "Update OK, %u bytes", (unsigned)up.totalSize);
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    // The connection ended before the whole image arrived. Logged with the state at that
    // moment, since the web server reports nothing itself.
    LOGW("OTA", "Upload aborted after %u bytes (client %s, free memory %u, lowest %u)", (unsigned)up.totalSize,
         server.client().connected() ? "still connected" : "gone", (unsigned)ESP.getFreeHeap(),
         (unsigned)ESP.getMinFreeHeap());
    otaFail("upload aborted");
  }
}

void handleOtaDone() {
  const bool ok = otaOk && otaError.isEmpty();
  otaOk = false;
  if (!ok) {
    if (otaError.isEmpty()) otaError = "no image received";
    LOGE("OTA", "Update failed: %s", otaError.c_str());
    sendError(otaError == "wrong password" ? 401 : 500, otaError.c_str());
    otaError = "";
    return;
  }
  JsonDocument doc;
  doc["ok"] = true;
  sendJson(doc);
  restartPending = true;
  restartAtMs = millis() + 1000;
}

void handleRoot() { server.send_P(200, "text/html", kIndexHtml); }

// The app icon: what an iPhone uses when the page is added to the Home Screen (the page then opens
// without Safari's bars), and the favicon. Served from flash and cached for a day.
void handleIcon() {
  server.sendHeader("Cache-Control", "public, max-age=86400");
  server.send_P(200, "image/png", reinterpret_cast<const char*>(kAppIcon), sizeof(kAppIcon));
}

// Anything else (including OS captive-portal probes) is sent to the UI. Registered as the last
// handler rather than with onNotFound(), which makes the core log "request handler not found"
// for every connectivity check a phone or PC makes.
class RedirectToUi : public RequestHandler {
 public:
  bool canHandle(HTTPMethod, String) override { return true; }
  bool handle(WebServer& s, HTTPMethod, String) override {
    String url = "http://" + s.client().localIP().toString() + "/";
    s.sendHeader("Location", url, true);
    s.send(302, "text/plain", url);
    return true;
  }
};

}  // namespace


// (Judged on both: the replies are built from many small pieces and one block of 2-3 kB. A largest block of 7 kB with 20-30 kB
// free is the ordinary state while another request is still being sent, and was served without trouble before this guard: at
// 8000 B for the block alone the guard turned away a request every few seconds of a test run.)
constexpr uint32_t kMinBlockToServe = 4000;
constexpr uint32_t kMinFreeToServe = 12000;

// Registers a route and remembers which one is being served, so the memory trace can name it.
const char* gRoute = "";
namespace {
const char* gRouteName() { return gRoute; }
}  // namespace
void route(const char* path, HTTPMethod method, void (*handler)()) {
  server.on(path, method, [path, handler]() {
    gRoute = path;
    noteWebClient();
    // An allocation that fails stops the firmware dead (a restart, in flight too). A request that arrives while memory is this
    // short is turned away instead: the page and the tools ask again a moment later.
    const uint32_t biggest = ESP.getMaxAllocHeap();
    if (biggest < kMinBlockToServe || ESP.getFreeHeap() < kMinFreeToServe) {
      nettrail::add(nettrail::LowMem, (int)biggest);
      LOGW("MEM", "route %s refused: free %u B, largest block %u B", path, (unsigned)ESP.getFreeHeap(), (unsigned)biggest);
      server.sendHeader("Retry-After", "2");
      server.send(503, "text/plain", "busy");
      return;
    }
#if PV_DEBUG
    // Names the routes that push the memory low-water mark down, in the debug log.
    const uint32_t before = ESP.getMinFreeHeap();
    handler();
    const uint32_t after = ESP.getMinFreeHeap();
    if (after + 1000 < before) {
      LOGW("MEM", "route %s %s lowered the low-water mark %u -> %u B (free now %u)", server.method() == HTTP_POST ? "POST" : "GET",
           path, (unsigned)before, (unsigned)after, (unsigned)ESP.getFreeHeap());
    }
#else
    handler();
#endif
  });
}

#if PV_TEST
// Test build: give up on the maintenance network now, and look for it again after retryMs (0 = the normal 5 minutes).
void testMaintGiveUp(uint32_t retryMs) {
  serverRetryMs = retryMs ? retryMs : kServerRetryMs;
  serverStop("test: gave up");
}
#endif

void begin() {
  nettrail::begin((int)esp_reset_reason());
  if (esp_reset_reason() == ESP_RST_POWERON || esp_reset_reason() == ESP_RST_BROWNOUT) joinedBeforeRestart = 0;
  serverEverConnected = joinedBeforeRestart == kJoinedMagic;
  WiFi.onEvent(onWifiEvent);
  LOGI("WEB", "Restarted: %s", resetReasonName());
  serverAttemptStartedMs = millis();
  strlcpy(serverSsidUsed, g_cfg.serverSsid, sizeof(serverSsidUsed));
  strlcpy(serverPassUsed, g_cfg.serverPass, sizeof(serverPassUsed));
  maintUsed = g_cfg.maintWifi;
  bool joinServer = maintUsed && serverSsidUsed[0] != 0;
  serverWhyOff = g_cfg.maintWifi ? "not set" : "switched off";
  WiFi.setHostname("paravario");
  WiFi.mode(joinServer ? WIFI_AP_STA : WIFI_AP);
  const char* pass = strlen(g_cfg.wifiPass) >= 8 ? g_cfg.wifiPass : nullptr;
  WiFi.softAP(settings::apSsid(), pass);
  dns.start(53, "*", WiFi.softAPIP());
  if (joinServer) {
    WiFi.begin(g_cfg.serverSsid, g_cfg.serverPass);
    serverWifi = ServerWifi::Connecting;
    LOGI("WEB", "Joining server network '%s' (giving up after %ld s)", g_cfg.serverSsid,
         (long)g_cfg.serverGiveUpS);
  }
  if (MDNS.begin("paravario")) MDNS.addService("http", "tcp", 80);

  route("/", HTTP_GET, handleRoot);
  route("/apple-touch-icon.png", HTTP_GET, handleIcon);
  route("/apple-touch-icon-precomposed.png", HTTP_GET, handleIcon);
  route("/favicon.ico", HTTP_GET, handleIcon);
  route("/api/status", HTTP_GET, handleStatus);
  route("/api/settings", HTTP_GET, handleGetSettings);
  route("/api/settings", HTTP_POST, handlePostSettings);
  route("/api/schema", HTTP_GET, handleSchema);
  route("/api/action", HTTP_POST, handleAction);
  route("/api/areas", HTTP_GET, handleGetAreas);
  route("/api/areas", HTTP_POST, handlePostAreas);
  route("/api/logs", HTTP_GET, handleLogs);
  route("/api/debuglog", HTTP_GET, handleDebugLog);
#if PV_HAS_SPECTRUM
  route("/api/spectrum", HTTP_GET, handleSpectrum);
#endif
  route("/api/height", HTTP_GET, handleHeight);
#if PV_TEST
  route("/api/sim", HTTP_GET, handleSim);
#endif
  route("/api/log", HTTP_GET, handleLogDownload);
  route("/api/coredump", HTTP_GET, handleCoreDump);
  route("/api/nettrail", HTTP_GET, handleNetTrail);
  route("/api/log", HTTP_DELETE, handleLogDelete);
  server.on("/api/ota", HTTP_POST, handleOtaDone, handleOtaUpload);
  server.addHandler(new RedirectToUi());
  server.collectHeaders(kOtaHeaders, 2);
  server.begin();
  LOGI("WEB", "Wi-Fi AP '%s' at %s", settings::apSsid(), WiFi.softAPIP().toString().c_str());
}

// Serving a request needs a few kB (reply text, JSON) on top of whatever the Wi-Fi driver is
// holding for packets in flight. When free memory is already short, usually a burst of packets
// from a browser opening several connections at once, the request waits a moment for those
// buffers to drain rather than pushing memory towards zero. Never held for long: a client is
// better served late than not at all.
constexpr uint32_t kMinHeapToServe = 20000;
constexpr uint32_t kMaxHoldMs = 400;

void serveClients() {
  static uint32_t heldSinceMs = 0;
  const uint32_t now = millis();
  if (ESP.getFreeHeap() < kMinHeapToServe && (!heldSinceMs || now - heldSinceMs < kMaxHoldMs)) {
    if (!heldSinceMs) heldSinceMs = now ? now : 1;
    return;
  }
  heldSinceMs = 0;
  server.handleClient();
}

void update() {
#if PV_DEBUG
  // Memory low-water mark around each step, with the request being served, for the debug log.
  static uint32_t lastMin = UINT32_MAX;
  auto mark = [](const char* what) {
    uint32_t m = ESP.getMinFreeHeap();
    if (lastMin == UINT32_MAX) lastMin = m;
    if (m + 1500 <= lastMin) {
      LOGW("MEM", "web: lowest %u -> %u B during %s %s (free now %u)", (unsigned)lastMin, (unsigned)m, what,
           gRoute, (unsigned)ESP.getFreeHeap());
    }
    lastMin = m;
    gRoute = "";
  };
  mark("idle before");
  dns.processNextRequest();
  mark("dns");
  serveClients();
  mark("request");
  serverUpdate(millis());
  mark("wifi state");
#else
  dns.processNextRequest();
  serveClients();
  serverUpdate(millis());
#endif
  if (restartPending && (int32_t)(millis() - restartAtMs) >= 0) ESP.restart();
}

}  // namespace webui
